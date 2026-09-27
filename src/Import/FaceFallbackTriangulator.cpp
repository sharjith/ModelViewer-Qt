// CGAL first: OpenCASCADE defines a function-like macro Handle(Class), which would break CGAL headers (CGAL has classes
// with a constructor taking a Handle) if they were included after any OCC header.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>

#include "FaceFallbackTriangulator.h"

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_Surface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangle.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
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
			for (const TopoDS_Shape& neighbourShape : edgeToFaces.FindFromKey(edge))
			{
				if (neighbourShape.IsSame(face))
					continue;
				TopLoc_Location loc;
				const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(neighbourShape), loc);
				if (tri.IsNull())
					continue;
				const Handle(Poly_PolygonOnTriangulation) poly = BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
				if (poly.IsNull() || poly->NbNodes() < 2)
					continue;
				const auto& nodes = poly->Nodes();
				std::vector<gp_Pnt> candidate;
				for (int i = nodes.Lower(); i <= nodes.Upper(); ++i)
					candidate.push_back(tri->Node(nodes(i)).Transformed(loc.Transformation()));
				// A polygon that does not follow its curve (a full circle reduced to a couple of nodes, which happens on
				// an edge shared with a face the mesher gave up on) would make the loop meaningless: skip it.
				double polygonLength = 0.0;
				for (size_t i = 1; i < candidate.size(); ++i)
					polygonLength += candidate[i - 1].Distance(candidate[i]);
				double curveLength = 0.0;
				try { curveLength = GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(edge)); } catch (...) {}
				if (curveLength > 0.0 && polygonLength < 0.5 * curveLength)
					continue;
				if (closedEdge && candidate.size() < 4)
					continue;
				out = std::move(candidate);
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
			// closed one (a full circle) has the same start and end, so its orientation in the wire decides - but the
			// direction a neighbour's polygon runs in is not assumed to be the edge's forward direction: it is read off
			// the curve (does the first polygon segment head the way the curve's parameter increases?).
			bool reverse = false;
			if (closedEdge)
			{
				bool polygonForward = true;
				if (fromNeighbour)
				{
					const BRepAdaptor_Curve curve(edge);
					const double first = curve.FirstParameter();
					const double last = curve.LastParameter();
					const gp_Vec tangent(curve.Value(first), curve.Value(first + 0.01 * (last - first)));
					const gp_Vec chord(out[0], out[1]);
					polygonForward = tangent.Dot(chord) >= 0.0;
				}
				reverse = (polygonForward == reversedInWire);
			}
			else
			{
				reverse = out.front().Distance(startPoint) > out.back().Distance(startPoint);
			}
			if (reverse)
				std::reverse(out.begin(), out.end());
		}
		return out.size() >= 2;
	}

	FaceFallbackTriangulator::Boundary failedBoundary(const char* why)
	{
		FaceFallbackTriangulator::Boundary b;
		b.failure = why;
		return b;
	}

	// One edge of a wire in the direction the wire traverses it.
	struct WirePiece
	{
		TopoDS_Edge edge;
		gp_Pnt start;
		gp_Pnt end;
		double tolerance = 0.0;
		bool poleBefore = false; // a degenerate edge (a cone apex, a sphere pole) sits between the previous piece and this one
	};

	// The wire's edges as a connected loop. BRepTools_WireExplorer is not used: it orders edges by the face's
	// pcurves, which is exactly what is broken on the faces this class is for (and a seam edge - present twice in a
	// wire - makes the choice ambiguous). The stored order of a STEP/IGES wire already is the loop order, so it is
	// used as is when it connects, and only re-chained by the 3D end points when it does not.
	bool orderedWirePieces(const TopoDS_Wire& wire, std::vector<WirePiece>& pieces)
	{
		pieces.clear();
		bool poleSeen = false;
		for (TopoDS_Iterator it(wire, false); it.More(); it.Next())
		{
			TopoDS_Edge edge = TopoDS::Edge(it.Value());
			if (BRep_Tool::Degenerated(edge))
			{
				poleSeen = true;
				continue;
			}
			const TopoDS_Vertex first = TopExp::FirstVertex(edge, true);
			const TopoDS_Vertex last = TopExp::LastVertex(edge, true);
			if (first.IsNull() || last.IsNull())
				return false;
			WirePiece piece;
			piece.edge = edge;
			piece.start = BRep_Tool::Pnt(first);
			piece.end = BRep_Tool::Pnt(last);
			piece.tolerance = std::max(BRep_Tool::Tolerance(first), BRep_Tool::Tolerance(last));
			piece.poleBefore = poleSeen;
			poleSeen = false;
			pieces.push_back(piece);
		}
		if (pieces.empty())
			return false;
		if (poleSeen)
			pieces.front().poleBefore = true; // the degenerate edge closes the wire
		// Start the loop right after no pole, so a pole never sits at the loop's wrap-around: rotate a piece with a
		// pole before it away from the front.
		for (size_t r = 0; r < pieces.size() && pieces.front().poleBefore; ++r)
			std::rotate(pieces.begin(), pieces.begin() + 1, pieces.end());
		double tolerance = 0.0;
		double extent = 0.0;
		for (const WirePiece& p : pieces)
		{
			tolerance = std::max(tolerance, 2.0 * p.tolerance);
			extent = std::max(extent, p.start.Distance(p.end));
			extent = std::max(extent, p.start.Distance(pieces.front().start));
		}
		tolerance = std::max(tolerance, 1e-6 * extent);

		bool connected = true;
		for (size_t i = 0; i < pieces.size() && connected; ++i)
			connected = pieces[i].end.Distance(pieces[(i + 1) % pieces.size()].start) <= tolerance;
		if (connected)
			return true;

		std::vector<WirePiece> ordered;
		std::vector<bool> used(pieces.size(), false);
		ordered.push_back(pieces[0]);
		used[0] = true;
		size_t at = 0;
		while (ordered.size() < pieces.size())
		{
			bool found = false;
			for (size_t k = 1; k <= pieces.size() && !found; ++k)
			{
				const size_t j = (at + k) % pieces.size();
				if (!used[j] && ordered.back().end.Distance(pieces[j].start) <= tolerance)
				{
					ordered.push_back(pieces[j]);
					used[j] = true;
					at = j;
					found = true;
				}
			}
			if (!found)
				return false;
		}
		if (ordered.back().end.Distance(ordered.front().start) > tolerance)
			return false;
		pieces = std::move(ordered);
		return true;
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
		std::vector<std::vector<std::string>> wireDescriptions;
		std::vector<std::vector<bool>> wirePoles;
		double minX = 1e300, minY = 1e300, minZ = 1e300, maxX = -1e300, maxY = -1e300, maxZ = -1e300;
		for (TopExp_Explorer wexp(face, TopAbs_WIRE); wexp.More(); wexp.Next())
		{
			const TopoDS_Wire wire = TopoDS::Wire(wexp.Current());
			std::vector<WirePiece> pieces;
			if (!orderedWirePieces(wire, pieces))
				return failedBoundary("a wire's edges do not form a closed loop");
			std::vector<std::vector<gp_Pnt>> polylines;
			std::vector<bool> fromNeighbour;
			std::vector<std::string> descriptions;
			std::vector<bool> poleBefore;
			double snapTolerance = 0.0;
			for (const WirePiece& piece : pieces)
			{
				const TopoDS_Edge& edge = piece.edge;
				const bool closedEdge = TopExp::FirstVertex(edge, true).IsSame(TopExp::LastVertex(edge, true));
				std::vector<gp_Pnt> pts;
				bool neighbour = false;
				if (!edgePolyline(edge, face, edgeToFaces, piece.start, closedEdge, edge.Orientation() == TopAbs_REVERSED, pts, neighbour))
					return failedBoundary("an edge has no usable polyline");
				fromNeighbour.push_back(neighbour);
				poleBefore.push_back(piece.poleBefore);
				descriptions.push_back(std::string(neighbour ? "neighbour" : "sampled") + (closedEdge ? " closed" : " open")
					+ (edge.Orientation() == TopAbs_REVERSED ? " reversed" : " forward") + " n=" + std::to_string(pts.size()));
				// Vertices carry their own tolerance (a STEP part can have 1e-3 or more): the points a sampled edge
				// shares with its neighbours' polylines can differ by about that much and are snapped below.
				snapTolerance = std::max(snapTolerance, piece.tolerance);
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
			{
				wirePolylines.push_back(std::move(polylines));
				wireDescriptions.push_back(std::move(descriptions));
				wirePoles.push_back(std::move(poleBefore));
			}
		}
		if (wirePolylines.empty())
			return failedBoundary("the face has no usable wire");
		const double diag = std::sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY) + (maxZ - minZ) * (maxZ - minZ));
		const double eps = std::max(1e-9, 1e-7 * diag);

		std::vector<std::vector<gp_Pnt>> loops3d;
		std::vector<std::vector<std::pair<size_t, std::string>>> loopPieces;
		std::vector<std::vector<size_t>> loopPoles;
		for (size_t w = 0; w < wirePolylines.size(); ++w)
		{
			const auto& polylines = wirePolylines[w];
			std::vector<gp_Pnt> chain;
			std::vector<std::pair<size_t, std::string>> pieceInfo;
			std::vector<size_t> poles;
			for (size_t i = 0; i < polylines.size(); ++i)
			{
				const std::vector<gp_Pnt>& pts = polylines[i];
				// Across a degenerate edge the same 3D point appears twice, at different (u, v): keep both.
				const size_t from = (!chain.empty() && !wirePoles[w][i] && chain.back().Distance(pts.front()) <= eps) ? 1 : 0;
				if (wirePoles[w][i] && !chain.empty())
					poles.push_back(chain.size());
				pieceInfo.emplace_back(chain.size() - from, wireDescriptions[w][i] + (from ? "" : " (NOT joined to previous)"));
				chain.insert(chain.end(), pts.begin() + from, pts.end());
			}
			if (chain.size() >= 2 && chain.front().Distance(chain.back()) <= eps)
				chain.pop_back();
			if (chain.size() < 3)
				return failedBoundary("a boundary loop has fewer than 3 points");
			loops3d.push_back(std::move(chain));
			loopPieces.push_back(std::move(pieceInfo));
			loopPoles.push_back(std::move(poles));
		}
		Boundary result;
		result.loops = std::move(loops3d);
		result.pieces = std::move(loopPieces);
		result.poleBreaks = std::move(loopPoles);
		result.diag = diag;
		result.valid = true;
		return result;
	}
	catch (...)
	{
		return failedBoundary("exception while capturing the boundary");
	}
}

Handle(Poly_Triangulation) FaceFallbackTriangulator::triangulate(const TopoDS_Face& face, const Boundary& boundary, std::string* failure)
{
	auto fail = [failure](const std::string& why) {
		if (failure)
			*failure = why;
		return Handle(Poly_Triangulation)();
	};
	if (!boundary.valid)
		return fail(boundary.failure.empty() ? "no boundary" : boundary.failure);
	try
	{
		TopLoc_Location faceLoc;
		const Handle(Geom_Surface) surface = BRep_Tool::Surface(face, faceLoc);
		if (surface.IsNull())
			return fail("the face has no surface");
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
			std::vector<size_t> uvIndexOfPoint; // loop point index -> index in `uv` (of the last kept point at or before it)
			const std::vector<size_t> noPoles;
			const std::vector<size_t>& poleBreaks = loopsUV.size() < boundary.poleBreaks.size() ? boundary.poleBreaks[loopsUV.size()] : noPoles;
			auto isPoleBreak = [&](size_t index) { return std::find(poleBreaks.begin(), poleBreaks.end(), index) != poleBreaks.end(); };
			size_t firstPoleUv = 0; // index in `uv` of the first point after a pole (0 = none)
			for (size_t pointIndex = 0; pointIndex < loop.size(); ++pointIndex)
			{
				const gp_Pnt& p = loop[pointIndex];
				gp_Pnt2d q = analysis->ValueOfUV(p.Transformed(toLocal), projectionTolerance);
				const bool afterPole = isPoleBreak(pointIndex);
				const bool atPole = afterPole || isPoleBreak(pointIndex + 1);
				if (atPole && !uv.empty() && uPeriodic)
				{
					// A pole has no meaningful u of its own (the projection returns anything): it takes the u of the
					// seam running into it (the point before it) or out of it (the point after it).
					if (!afterPole)
					{
						q.SetX(uv.back().X());
					}
					else if (pointIndex + 1 < loop.size())
					{
						const gp_Pnt2d next = analysis->ValueOfUV(loop[pointIndex + 1].Transformed(toLocal), projectionTolerance);
						q.SetX(next.X());
					}
				}
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
					if (!afterPole && std::abs(q.X() - prev.X()) < 1e-12 && std::abs(q.Y() - prev.Y()) < 1e-12)
					{
						uvIndexOfPoint.push_back(uv.size() - 1);
						continue; // same (u, v) as the previous point
					}
				}
				if (afterPole && firstPoleUv == 0)
					firstPoleUv = uv.size();
				uvIndexOfPoint.push_back(uv.size());
				uv.push_back(q);
				pts.push_back(p);
			}
			auto describeLoop = [&]() {
				std::string text = " [";
				if (loopsUV.size() < boundary.pieces.size())
				{
					const auto& infos = boundary.pieces[loopsUV.size()];
					for (size_t i = 0; i < infos.size(); ++i)
					{
						const size_t begin = infos[i].first;
						const size_t at = begin < uvIndexOfPoint.size() ? uvIndexOfPoint[begin] : uv.size() - 1;
						text += (i ? "; " : "") + infos[i].second + " from u=" + std::to_string(uv[at].X()) + " v=" + std::to_string(uv[at].Y());
					}
				}
				return text + "]";
			};
			if (uv.size() < 3)
				return fail("a boundary loop collapsed to fewer than 3 distinct (u, v) points");
			// A pole edge spans a whole period in u: the loop's other pieces wind once around, the pole edge (dropped
			// from the wire) winds back. Put the u jump where the pole is, so the polygon closes.
			if (firstPoleUv > 0 && uPeriodic)
			{
				const double windings = std::round((uv.front().X() - uv.back().X()) / uPeriod);
				if (windings != 0.0)
				{
					for (size_t i = firstPoleUv; i < uv.size(); ++i)
						uv[i].SetX(uv[i].X() + windings * uPeriod);
				}
			}
			// A loop that winds once around a periodic direction is not a polygon in (u, v).
			if (uPeriodic && std::round((uv.front().X() - uv.back().X()) / uPeriod) != 0.0)
				return fail("a boundary loop winds around the surface's u period (loop " + std::to_string(loopsUV.size()) + " of "
					+ std::to_string(loops3d.size()) + ", " + std::to_string(uv.size()) + " points, first->last u "
					+ std::to_string(uv.front().X()) + " -> " + std::to_string(uv.back().X()) + ", period " + std::to_string(uPeriod) + ")" + describeLoop());
			if (vPeriodic && std::round((uv.front().Y() - uv.back().Y()) / vPeriod) != 0.0)
				return fail("a boundary loop winds around the surface's v period (loop " + std::to_string(loopsUV.size()) + " of "
					+ std::to_string(loops3d.size()) + ", " + std::to_string(uv.size()) + " points, first->last v "
					+ std::to_string(uv.front().Y()) + " -> " + std::to_string(uv.back().Y()) + ", period " + std::to_string(vPeriod) + ")" + describeLoop());
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
		// Planes, cylinders and cones are covered exactly by triangles between their boundary nodes (a cone is a fan
		// from its apex, a cylinder a strip between its two rims), so they get no interior points: a grid uniform in
		// (u, v) would crowd toward a cone's apex into triangles so small that float rounding of the vertices makes
		// them degenerate, and the importer then drops them and leaves a hole.
		const GeomAbs_SurfaceType surfaceType = adaptor.GetType();
		if (surfaceType != GeomAbs_Plane && surfaceType != GeomAbs_Cylinder && surfaceType != GeomAbs_Cone && !segmentLengths.empty())
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
				return fail("boundary constraints cross each other");
			triangles.push_back({ a, b, c });
		}
		if (triangles.empty())
			return fail("the constrained triangulation has no triangle inside the boundary");

		Handle(Poly_Triangulation) result = new Poly_Triangulation(static_cast<int>(nodes.size()),
			static_cast<int>(triangles.size()), false);
		for (size_t i = 0; i < nodes.size(); ++i)
			result->SetNode(static_cast<int>(i + 1), nodes[i]);
		for (size_t i = 0; i < triangles.size(); ++i)
			result->SetTriangle(static_cast<int>(i + 1), Poly_Triangle(triangles[i][0] + 1, triangles[i][1] + 1, triangles[i][2] + 1));
		(void)boundaryNodeCount;
		return result;
	}
	catch (...)
	{
		return fail("exception while triangulating");
	}
}
