// CGAL first: OpenCASCADE defines a function-like macro Handle(Class), which would break CGAL headers (CGAL has classes
// with a constructor taking a Handle) if they were included after any OCC header.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>

#include "FaceFallbackTriangulator.h"

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Geom_Surface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangle.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	using K = CGAL::Exact_predicates_inexact_constructions_kernel;

	// Nesting level of a triangle relative to the polygon boundary: 0 = outside, 1 = inside the outer loop, 2 = inside
	// a hole, ... - odd levels are the face's own region.
	struct FaceInfo
	{
		int nesting = -1;
		bool inDomain() const { return nesting > 0 && (nesting % 2) == 1; }
	};
	using Vb = CGAL::Triangulation_vertex_base_with_info_2<int, K>;
	using Fbi = CGAL::Triangulation_face_base_with_info_2<FaceInfo, K>;
	using Fb = CGAL::Constrained_triangulation_face_base_2<K, Fbi>;
	using Tds = CGAL::Triangulation_data_structure_2<Vb, Fb>;
	using CDT = CGAL::Constrained_Delaunay_triangulation_2<K, Tds, CGAL::Exact_predicates_tag>;
	using Point2 = CDT::Point;

	void markDomains(CDT& cdt, CDT::Face_handle start, int index, std::list<CDT::Edge>& border)
	{
		if (start->info().nesting != -1)
			return;
		std::list<CDT::Face_handle> queue;
		queue.push_back(start);
		while (!queue.empty())
		{
			CDT::Face_handle fh = queue.front();
			queue.pop_front();
			if (fh->info().nesting != -1)
				continue;
			fh->info().nesting = index;
			for (int i = 0; i < 3; ++i)
			{
				const CDT::Edge e(fh, i);
				CDT::Face_handle neighbour = fh->neighbor(i);
				if (neighbour->info().nesting != -1)
					continue;
				if (cdt.is_constrained(e))
					border.push_back(e);
				else
					queue.push_back(neighbour);
			}
		}
	}

	void markDomains(CDT& cdt)
	{
		for (auto f = cdt.all_faces_begin(); f != cdt.all_faces_end(); ++f)
			f->info().nesting = -1;
		std::list<CDT::Edge> border;
		markDomains(cdt, cdt.infinite_face(), 0, border);
		while (!border.empty())
		{
			const CDT::Edge e = border.front();
			border.pop_front();
			CDT::Face_handle neighbour = e.first->neighbor(e.second);
			if (neighbour->info().nesting == -1)
				markDomains(cdt, neighbour, e.first->info().nesting + 1, border);
		}
	}

	// The edge's polyline in the direction the wire traverses it (`startPoint` = its first vertex). Taken from a
	// neighbouring face's discretization of the shared edge when there is one; otherwise sampled from the 3D curve.
	bool edgePolyline(const TopoDS_Edge& edge, const TopoDS_Face& face,
	                  const TopTools_IndexedDataMapOfShapeListOfShape& edgeToFaces,
	                  const gp_Pnt& startPoint, bool closedEdge, bool reversedInWire,
	                  std::vector<gp_Pnt>& out, bool& fromNeighbour)
	{
		out.clear();
		fromNeighbour = false;
		if (edgeToFaces.Contains(edge))
		{
			const TopTools_ListOfShape& faces = edgeToFaces.FindFromKey(edge);
			for (TopTools_ListIteratorOfListOfShape it(faces); it.More(); it.Next())
			{
				if (it.Value().IsSame(face))
					continue;
				TopLoc_Location loc;
				const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(it.Value()), loc);
				if (tri.IsNull())
					continue;
				const Handle(Poly_PolygonOnTriangulation) poly = BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
				if (poly.IsNull() || poly->NbNodes() < 2)
					continue;
				const TColStd_Array1OfInteger& nodes = poly->Nodes();
				for (int i = nodes.Lower(); i <= nodes.Upper(); ++i)
					out.push_back(tri->Node(nodes(i)).Transformed(loc.Transformation()));
				fromNeighbour = true;
				break;
			}
		}
		if (out.size() < 2)
		{
			// No neighbour discretized this edge: sample its 3D curve (the result may not match a neighbour's points).
			out.clear();
			const BRepAdaptor_Curve curve(edge);
			const double first = curve.FirstParameter();
			const double last = curve.LastParameter();
			const int kSamples = curve.GetType() == GeomAbs_Line ? 1 : 24;
			for (int i = 0; i <= kSamples; ++i)
				out.push_back(curve.Value(first + (last - first) * i / kSamples));
		}
		if (out.size() >= 2)
		{
			// The wire traverses the edge forward or reversed. An open edge's direction follows from its end points; a
			// closed one (a full circle) has the same start and end, so its orientation in the wire decides.
			const bool reverse = closedEdge ? reversedInWire : out.front().Distance(startPoint) > out.back().Distance(startPoint);
			if (reverse)
				std::reverse(out.begin(), out.end());
		}
		return out.size() >= 2;
	}

	double diagOfPolylines(const std::vector<std::vector<gp_Pnt>>& polylines)
	{
		double minX = 1e300, minY = 1e300, minZ = 1e300, maxX = -1e300, maxY = -1e300, maxZ = -1e300;
		for (const auto& pts : polylines)
		{
			for (const gp_Pnt& p : pts)
			{
				minX = std::min(minX, p.X()); maxX = std::max(maxX, p.X());
				minY = std::min(minY, p.Y()); maxY = std::max(maxY, p.Y());
				minZ = std::min(minZ, p.Z()); maxZ = std::max(maxZ, p.Z());
			}
		}
		return std::sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY) + (maxZ - minZ) * (maxZ - minZ));
	}

	double distancePointSegment(double px, double py, double ax, double ay, double bx, double by)
	{
		const double dx = bx - ax, dy = by - ay;
		const double len2 = dx * dx + dy * dy;
		double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
		t = std::clamp(t, 0.0, 1.0);
		const double cx = ax + t * dx - px, cy = ay + t * dy - py;
		return std::sqrt(cx * cx + cy * cy);
	}
}

FaceFallbackTriangulator::Boundary FaceFallbackTriangulator::captureBoundary(const TopoDS_Face& face, const TopTools_IndexedDataMapOfShapeListOfShape& edgeToFaces)
{
	try
	{
		// ---- 1. the boundary loops in 3D, from the neighbours' shared-edge discretization ----
		std::vector<std::vector<std::vector<gp_Pnt>>> wirePolylines; // per wire, per edge
		double minX = 1e300, minY = 1e300, minZ = 1e300, maxX = -1e300, maxY = -1e300, maxZ = -1e300;
		for (TopExp_Explorer wexp(face, TopAbs_WIRE); wexp.More(); wexp.Next())
		{
			const TopoDS_Wire wire = TopoDS::Wire(wexp.Current());
			std::vector<std::vector<gp_Pnt>> polylines;
			std::vector<bool> fromNeighbour;
			double snapTolerance = 0.0;
			for (BRepTools_WireExplorer we(wire, face); we.More(); we.Next())
			{
				TopoDS_Edge edge = we.Current();
				if (BRep_Tool::Degenerated(edge))
					continue;
				edge.Orientation(we.Orientation());
				const TopoDS_Vertex firstVertex = TopExp::FirstVertex(edge, Standard_True);
				const gp_Pnt start = BRep_Tool::Pnt(firstVertex);
				const bool closedEdge = firstVertex.IsSame(TopExp::LastVertex(edge, Standard_True));
				std::vector<gp_Pnt> pts;
				bool neighbour = false;
				if (!edgePolyline(edge, face, edgeToFaces, start, closedEdge, edge.Orientation() == TopAbs_REVERSED, pts, neighbour))
					return Boundary();
				fromNeighbour.push_back(neighbour);
				// Vertices carry their own tolerance (a STEP part can have 1e-3 or more): the points a sampled edge
				// shares with its neighbours' polylines can differ by about that much and are snapped below.
				snapTolerance = std::max(snapTolerance, BRep_Tool::Tolerance(firstVertex));
				for (const gp_Pnt& p : pts)
				{
					minX = std::min(minX, p.X()); maxX = std::max(maxX, p.X());
					minY = std::min(minY, p.Y()); maxY = std::max(maxY, p.Y());
					minZ = std::min(minZ, p.Z()); maxZ = std::max(maxZ, p.Z());
				}
				polylines.push_back(std::move(pts));
			}
			// An edge no neighbour discretized (a seam, a free edge) was sampled from its 3D curve; move its end points
			// onto the neighbouring polylines' end points so the loop is closed EXACTLY, with no sliver crack.
			const size_t count = polylines.size();
			for (size_t i = 0; i < count && count > 1; ++i)
			{
				if (fromNeighbour[i])
					continue;
				const std::vector<gp_Pnt>& previous = polylines[(i + count - 1) % count];
				const std::vector<gp_Pnt>& following = polylines[(i + 1) % count];
				const double snap = std::max(snapTolerance * 2.0, 1e-4 * diagOfPolylines(polylines));
				if (polylines[i].front().Distance(previous.back()) <= snap)
					polylines[i].front() = previous.back();
				if (polylines[i].back().Distance(following.front()) <= snap)
					polylines[i].back() = following.front();
			}
			if (!polylines.empty())
				wirePolylines.push_back(std::move(polylines));
		}
		if (wirePolylines.empty())
			return Boundary();
		const double diag = std::sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY) + (maxZ - minZ) * (maxZ - minZ));
		const double eps = std::max(1e-9, 1e-7 * diag);

		std::vector<std::vector<gp_Pnt>> loops3d;
		for (const auto& polylines : wirePolylines)
		{
			std::vector<gp_Pnt> chain;
			for (const std::vector<gp_Pnt>& pts : polylines)
			{
				const size_t from = (!chain.empty() && chain.back().Distance(pts.front()) <= eps) ? 1 : 0;
				chain.insert(chain.end(), pts.begin() + from, pts.end());
			}
			if (chain.size() >= 2 && chain.front().Distance(chain.back()) <= eps)
				chain.pop_back();
			if (chain.size() < 3)
				return Boundary();
			loops3d.push_back(std::move(chain));
		}
		Boundary result;
		result.loops = std::move(loops3d);
		result.diag = diag;
		result.valid = true;
		return result;
	}
	catch (...)
	{
		return Boundary();
	}
}

Handle(Poly_Triangulation) FaceFallbackTriangulator::triangulate(const TopoDS_Face& face, const Boundary& boundary)
{
	if (!boundary.valid)
		return Handle(Poly_Triangulation)();
	try
	{
		TopLoc_Location faceLoc;
		const Handle(Geom_Surface) surface = BRep_Tool::Surface(face, faceLoc);
		if (surface.IsNull())
			return Handle(Poly_Triangulation)();
		const gp_Trsf toGlobal = faceLoc.Transformation();
		const gp_Trsf toLocal = toGlobal.Inverted();
		const std::vector<std::vector<gp_Pnt>>& loops3d = boundary.loops;
		const double diag = boundary.diag;

		// ---- 2. project onto the surface: (u, v) loops, periodic surfaces unwrapped along each loop ----
		Handle(ShapeAnalysis_Surface) analysis = new ShapeAnalysis_Surface(surface);
		const bool uPeriodic = surface->IsUPeriodic();
		const bool vPeriodic = surface->IsVPeriodic();
		const double uPeriod = uPeriodic ? surface->UPeriod() : 0.0;
		const double vPeriod = vPeriodic ? surface->VPeriod() : 0.0;
		const double projectionTolerance = std::max(1e-6, 1e-5 * diag);

		std::vector<std::vector<gp_Pnt2d>> loopsUV;
		std::vector<std::vector<gp_Pnt>> loopsKept3d; // parallel to loopsUV (points dropped for duplicate (u, v) removed)
		for (const std::vector<gp_Pnt>& loop : loops3d)
		{
			std::vector<gp_Pnt2d> uv;
			std::vector<gp_Pnt> pts;
			for (const gp_Pnt& p : loop)
			{
				gp_Pnt2d q = analysis->ValueOfUV(p.Transformed(toLocal), projectionTolerance);
				if (!uv.empty())
				{
					const gp_Pnt2d& prev = uv.back();
					if (uPeriodic)
					{
						while (q.X() - prev.X() > 0.5 * uPeriod) q.SetX(q.X() - uPeriod);
						while (q.X() - prev.X() < -0.5 * uPeriod) q.SetX(q.X() + uPeriod);
					}
					if (vPeriodic)
					{
						while (q.Y() - prev.Y() > 0.5 * vPeriod) q.SetY(q.Y() - vPeriod);
						while (q.Y() - prev.Y() < -0.5 * vPeriod) q.SetY(q.Y() + vPeriod);
					}
					if (std::abs(q.X() - prev.X()) < 1e-12 && std::abs(q.Y() - prev.Y()) < 1e-12)
						continue; // same (u, v) as the previous point
				}
				uv.push_back(q);
				pts.push_back(p);
			}
			if (uv.size() < 3)
				return Handle(Poly_Triangulation)();
			// A loop that winds once around a periodic direction is not a polygon in (u, v).
			if (uPeriodic && std::round((uv.front().X() - uv.back().X()) / uPeriod) != 0.0)
				return Handle(Poly_Triangulation)();
			if (vPeriodic && std::round((uv.front().Y() - uv.back().Y()) / vPeriod) != 0.0)
				return Handle(Poly_Triangulation)();
			loopsUV.push_back(std::move(uv));
			loopsKept3d.push_back(std::move(pts));
		}
		// Hole loops on a periodic surface: shift each by whole periods to sit next to the outer loop.
		for (size_t k = 1; k < loopsUV.size(); ++k)
		{
			auto meanOf = [](const std::vector<gp_Pnt2d>& l, bool xAxis) {
				double s = 0.0;
				for (const gp_Pnt2d& p : l) s += xAxis ? p.X() : p.Y();
				return s / static_cast<double>(l.size());
			};
			double du = 0.0, dv = 0.0;
			if (uPeriodic) du = -uPeriod * std::round((meanOf(loopsUV[k], true) - meanOf(loopsUV[0], true)) / uPeriod);
			if (vPeriodic) dv = -vPeriod * std::round((meanOf(loopsUV[k], false) - meanOf(loopsUV[0], false)) / vPeriod);
			if (du != 0.0 || dv != 0.0)
			{
				for (gp_Pnt2d& p : loopsUV[k])
					p.SetCoord(p.X() + du, p.Y() + dv);
			}
		}

		// ---- 3. node list: loop points first, then interior points on curved surfaces ----
		std::vector<gp_Pnt> nodes;
		std::vector<std::array<double, 2>> nodeUV;
		std::vector<std::vector<int>> loopNodeIndex(loopsUV.size());
		std::vector<double> segmentLengths;
		for (size_t k = 0; k < loopsUV.size(); ++k)
		{
			for (size_t i = 0; i < loopsUV[k].size(); ++i)
			{
				loopNodeIndex[k].push_back(static_cast<int>(nodes.size()));
				nodes.push_back(loopsKept3d[k][i]);
				nodeUV.push_back({ loopsUV[k][i].X(), loopsUV[k][i].Y() });
				segmentLengths.push_back(loopsKept3d[k][i].Distance(loopsKept3d[k][(i + 1) % loopsUV[k].size()]));
			}
		}
		const size_t boundaryNodeCount = nodes.size();

		std::vector<std::array<double, 2>> steiner;
		const GeomAdaptor_Surface adaptor(surface);
		if (adaptor.GetType() != GeomAbs_Plane && !segmentLengths.empty())
		{
			std::vector<double> sorted = segmentLengths;
			std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
			const double h = sorted[sorted.size() / 2];
			double uMin = 1e300, uMax = -1e300, vMin = 1e300, vMax = -1e300;
			for (const auto& l : loopsUV)
			{
				for (const gp_Pnt2d& p : l)
				{
					uMin = std::min(uMin, p.X()); uMax = std::max(uMax, p.X());
					vMin = std::min(vMin, p.Y()); vMax = std::max(vMax, p.Y());
				}
			}
			gp_Pnt centre;
			gp_Vec du, dv;
			surface->D1(0.5 * (uMin + uMax), 0.5 * (vMin + vMax), centre, du, dv);
			const double lu = du.Magnitude(), lv = dv.Magnitude();
			if (h > 1e-9 && lu > 1e-9 && lv > 1e-9)
			{
				int nu = std::max(1, static_cast<int>(std::ceil((uMax - uMin) * lu / h)));
				int nv = std::max(1, static_cast<int>(std::ceil((vMax - vMin) * lv / h)));
				while (static_cast<long long>(nu) * nv > 6400)
				{
					nu = std::max(1, nu / 2);
					nv = std::max(1, nv / 2);
				}
				auto inside = [&](double u, double v) {
					bool in = false;
					for (const auto& l : loopsUV)
					{
						for (size_t i = 0, j = l.size() - 1; i < l.size(); j = i++)
						{
							const double yi = l[i].Y(), yj = l[j].Y();
							if ((yi > v) != (yj > v) && u < (l[j].X() - l[i].X()) * (v - yi) / (yj - yi) + l[i].X())
								in = !in;
						}
					}
					return in;
				};
				auto clearance = [&](double u, double v) {
					double best = 1e300;
					for (const auto& l : loopsUV)
					{
						for (size_t i = 0; i < l.size(); ++i)
						{
							const gp_Pnt2d& a = l[i];
							const gp_Pnt2d& b = l[(i + 1) % l.size()];
							best = std::min(best, distancePointSegment(u * lu, v * lv, a.X() * lu, a.Y() * lv, b.X() * lu, b.Y() * lv));
						}
					}
					return best;
				};
				for (int i = 0; i < nu; ++i)
				{
					for (int j = 0; j < nv; ++j)
					{
						const double u = uMin + (i + 0.5) * (uMax - uMin) / nu;
						const double v = vMin + (j + 0.5) * (vMax - vMin) / nv;
						if (inside(u, v) && clearance(u, v) >= 0.35 * h)
							steiner.push_back({ u, v });
					}
				}
			}
		}
		for (const auto& uv : steiner)
		{
			gp_Pnt p;
			surface->D0(uv[0], uv[1], p);
			nodes.push_back(p.Transformed(toGlobal));
			nodeUV.push_back(uv);
		}

		// ---- 4. constrained Delaunay triangulation in (u, v) ----
		CDT cdt;
		std::vector<CDT::Vertex_handle> handles(nodes.size());
		std::unordered_set<const void*> mapped;
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			handles[i] = cdt.insert(Point2(nodeUV[i][0], nodeUV[i][1]));
			handles[i]->info() = static_cast<int>(i);
			mapped.insert(&*handles[i]);
		}
		for (size_t k = 0; k < loopNodeIndex.size(); ++k)
		{
			const std::vector<int>& idx = loopNodeIndex[k];
			for (size_t i = 0; i < idx.size(); ++i)
			{
				CDT::Vertex_handle a = handles[idx[i]];
				CDT::Vertex_handle b = handles[idx[(i + 1) % idx.size()]];
				if (a != b)
					cdt.insert_constraint(a, b);
			}
		}
		for (auto v = cdt.finite_vertices_begin(); v != cdt.finite_vertices_end(); ++v)
		{
			if (mapped.find(&*v) == mapped.end())
				v->info() = -1; // a vertex created by crossing constraints - not expected for a valid loop set
		}
		markDomains(cdt);

		std::vector<std::array<int, 3>> triangles;
		for (auto f = cdt.finite_faces_begin(); f != cdt.finite_faces_end(); ++f)
		{
			if (!f->info().inDomain())
				continue;
			const int a = f->vertex(0)->info(), b = f->vertex(1)->info(), c = f->vertex(2)->info();
			if (a < 0 || b < 0 || c < 0)
				return Handle(Poly_Triangulation)();
			triangles.push_back({ a, b, c });
		}
		if (triangles.empty())
			return Handle(Poly_Triangulation)();

		Handle(Poly_Triangulation) result = new Poly_Triangulation(static_cast<Standard_Integer>(nodes.size()),
			static_cast<Standard_Integer>(triangles.size()), Standard_False);
		for (size_t i = 0; i < nodes.size(); ++i)
			result->SetNode(static_cast<Standard_Integer>(i + 1), nodes[i]);
		for (size_t i = 0; i < triangles.size(); ++i)
			result->SetTriangle(static_cast<Standard_Integer>(i + 1), Poly_Triangle(triangles[i][0] + 1, triangles[i][1] + 1, triangles[i][2] + 1));
		(void)boundaryNodeCount;
		return result;
	}
	catch (...)
	{
		return Handle(Poly_Triangulation)();
	}
}
