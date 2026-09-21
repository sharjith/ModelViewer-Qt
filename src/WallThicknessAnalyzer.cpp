#include "WallThicknessAnalyzer.h"
#include "SceneMesh.h"
#include "MeshProperties.h" // MeshPropertyUnavailableReason / describeMeshPropertyUnavailableReason - shared reason wording

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h> // merge_duplicate_points_in_polygon_soup()
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/locate.h> // PMP::build_AABB_tree() - declared here, not in AABB_tree.h itself
#include <CGAL/boost/graph/helpers.h> // CGAL::is_closed()
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <variant>

#include <QDebug>

namespace
{
	using WtKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
	using WtPoint3 = WtKernel::Point_3;
	using WtMesh = CGAL::Surface_mesh<WtPoint3>;
	using WtRay3 = WtKernel::Ray_3;
	namespace PMP = CGAL::Polygon_mesh_processing;

	using VertexDescriptor = boost::graph_traits<WtMesh>::vertex_descriptor;
	using FaceDescriptor = boost::graph_traits<WtMesh>::face_descriptor;
	using VPM = boost::property_map<WtMesh, boost::vertex_point_t>::const_type;
	using AABBPrimitive = CGAL::AABB_face_graph_triangle_primitive<WtMesh, VPM>;
	using AABBTraits = CGAL::AABB_traits_3<WtKernel, AABBPrimitive>;
	using AABBTree = CGAL::AABB_tree<AABBTraits>;

	bool isFinitePoint(const std::vector<float>& pts, size_t vertexIndex)
	{
		for (int c = 0; c < 3; ++c)
		{
			if (!std::isfinite(pts[vertexIndex * 3 + c]))
				return false;
		}
		return true;
	}

	using VolumeIdMap = WtMesh::Property_map<FaceDescriptor, std::size_t>;

	// Where a ray hit a triangle (or, for a ray lying in the hit triangle's plane, the nearer segment endpoint - the
	// same "nearest wins" rule the normal-ray path uses), and its distance from `origin`.
	template <typename Variant>
	bool hitPointFrom(const Variant& intersection, const double origin[3], double outHit[3], double& outDistance)
	{
		if (const WtPoint3* p = std::get_if<WtPoint3>(&intersection))
		{
			outHit[0] = CGAL::to_double(p->x()); outHit[1] = CGAL::to_double(p->y()); outHit[2] = CGAL::to_double(p->z());
		}
		else if (const WtKernel::Segment_3* seg = std::get_if<WtKernel::Segment_3>(&intersection))
		{
			const WtPoint3 sp = seg->source();
			const WtPoint3 tp = seg->target();
			const double s[3] = { CGAL::to_double(sp.x()), CGAL::to_double(sp.y()), CGAL::to_double(sp.z()) };
			const double t[3] = { CGAL::to_double(tp.x()), CGAL::to_double(tp.y()), CGAL::to_double(tp.z()) };
			const double sd = (s[0] - origin[0]) * (s[0] - origin[0]) + (s[1] - origin[1]) * (s[1] - origin[1]) + (s[2] - origin[2]) * (s[2] - origin[2]);
			const double td = (t[0] - origin[0]) * (t[0] - origin[0]) + (t[1] - origin[1]) * (t[1] - origin[1]) + (t[2] - origin[2]) * (t[2] - origin[2]);
			const double* nearer = sd <= td ? s : t;
			outHit[0] = nearer[0]; outHit[1] = nearer[1]; outHit[2] = nearer[2];
		}
		else
		{
			return false;
		}
		const double dx = outHit[0] - origin[0], dy = outHit[1] - origin[1], dz = outHit[2] - origin[2];
		outDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
		return std::isfinite(outDistance);
	}

	// Everything computeLocalThickness() produces, indexed by ORIGINAL triangle (degenerate/skipped triangles stay
	// invalid / gridN 0).
	struct LocalThicknessOutput
	{
		std::vector<float> thickness;
		std::vector<bool> valid;
		SubTriangleField samples;
		std::vector<WallThicknessWitness> sampleWitness; // parallel to samples.values
	};

	// LocalThickness estimator - see WallThicknessMethod's doc comment for what it measures and why. `mesh` is
	// the oriented (outward-facing) working mesh, `tree` its AABB tree, `volumeIds` the solid-region id of each
	// face; origFaceIndex[k] is the original triangle of working face k, and origPoints/origIndices the original
	// geometry - sample positions and the sub-triangle grid are defined against the ORIGINAL vertex order, so the
	// renderer (which only knows the original triangles) can place them. Returns false only if the face numbering
	// assumption below does not hold, in which case the caller falls back to the normal-ray path.
	bool computeLocalThickness(
		const WtMesh& mesh, AABBTree& tree, const VolumeIdMap& volumeIds,
		const std::vector<size_t>& origFaceIndex,
		const std::vector<float>& origPoints, const std::vector<unsigned int>& origIndices,
		const WallThicknessParams& params, LocalThicknessOutput& out)
	{
		struct FaceGeom
		{
			double a[3]{}, b[3]{}, c[3]{};      // working-mesh vertices (define the outward normal)
			double oa[3]{}, ob[3]{}, oc[3]{};   // ORIGINAL-order vertices (define the sample grid)
			double n[3]{};                      // outward unit normal
			double area = 0.0;
			double longestEdge = 0.0;
			bool ok = false;
		};

		const size_t faceCount = mesh.number_of_faces();
		const size_t origFaceCount = origIndices.size() / 3;
		if (faceCount == 0 || origFaceIndex.size() < faceCount)
			return false;

		std::vector<FaceGeom> geom(faceCount);
		std::vector<FaceDescriptor> faceOf(faceCount);
		double bbMin[3] = { 1e300, 1e300, 1e300 }, bbMax[3] = { -1e300, -1e300, -1e300 };
		size_t k = 0;
		for (FaceDescriptor f : faces(mesh))
		{
			// The whole scheme indexes per-face data by creation order == descriptor index (polygon_soup_to_
			// polygon_mesh() adds faces one by one, no removals) - the same assumption the normal-ray loop
			// makes. Verify it rather than trust it.
			if (k >= faceCount || static_cast<size_t>(f.idx()) != k)
				return false;
			faceOf[k] = f;

			const auto h = halfedge(f, mesh);
			const WtPoint3& pa = mesh.point(source(h, mesh));
			const WtPoint3& pb = mesh.point(target(h, mesh));
			const WtPoint3& pc = mesh.point(target(next(h, mesh), mesh));
			FaceGeom& g = geom[k];
			const WtPoint3* pts[3] = { &pa, &pb, &pc };
			double* out3[3] = { g.a, g.b, g.c };
			for (int v = 0; v < 3; ++v)
			{
				out3[v][0] = CGAL::to_double(pts[v]->x());
				out3[v][1] = CGAL::to_double(pts[v]->y());
				out3[v][2] = CGAL::to_double(pts[v]->z());
				for (int axis = 0; axis < 3; ++axis)
				{
					bbMin[axis] = std::min(bbMin[axis], out3[v][axis]);
					bbMax[axis] = std::max(bbMax[axis], out3[v][axis]);
				}
			}
			const double e1[3] = { g.b[0] - g.a[0], g.b[1] - g.a[1], g.b[2] - g.a[2] };
			const double e2[3] = { g.c[0] - g.a[0], g.c[1] - g.a[1], g.c[2] - g.a[2] };
			const double nx = e1[1] * e2[2] - e1[2] * e2[1];
			const double ny = e1[2] * e2[0] - e1[0] * e2[2];
			const double nz = e1[0] * e2[1] - e1[1] * e2[0];
			const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (len > 0.0 && std::isfinite(len))
			{
				g.n[0] = nx / len; g.n[1] = ny / len; g.n[2] = nz / len;
				g.area = 0.5 * len;
				const double e3[3] = { g.c[0] - g.b[0], g.c[1] - g.b[1], g.c[2] - g.b[2] };
				g.longestEdge = std::sqrt(std::max({ e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2],
					e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2], e3[0] * e3[0] + e3[1] * e3[1] + e3[2] * e3[2] }));
				g.ok = true;
			}

			const size_t origFace = origFaceIndex[k];
			if (origFace >= origFaceCount)
				return false;
			double* origOut[3] = { g.oa, g.ob, g.oc };
			for (int v = 0; v < 3; ++v)
			{
				const size_t vi = origIndices[origFace * 3 + v];
				if (vi * 3 + 2 >= origPoints.size())
					return false;
				for (int axis = 0; axis < 3; ++axis)
					origOut[v][axis] = static_cast<double>(origPoints[vi * 3 + axis]);
			}
			++k;
		}
		if (k != faceCount)
			return false;

		// ---- Sample density: spacing is a fraction of the model, not of any one triangle, so a huge CAD
		// triangle gets many samples and a tiny one gets one. Widened if the total would exceed the budget. ----
		const double diag = std::sqrt((bbMax[0] - bbMin[0]) * (bbMax[0] - bbMin[0])
			+ (bbMax[1] - bbMin[1]) * (bbMax[1] - bbMin[1]) + (bbMax[2] - bbMin[2]) * (bbMax[2] - bbMin[2]));
		double spacing = diag / static_cast<double>(std::max(1, params.samplesAcrossBoundingBox));
		if (!(spacing > 0.0) || !std::isfinite(spacing))
			spacing = 1.0;
		constexpr int kMaxSubdivisions = 12;
		std::vector<int> subdivisions(faceCount, 1);
		for (int attempt = 0; attempt < 8; ++attempt)
		{
			size_t total = 0;
			for (size_t i = 0; i < faceCount; ++i)
			{
				// The triangle's size for sampling purposes: the geometric mean of its "equivalent side"
				// (sqrt(2 * area)) and its longest edge. Area alone under-samples a long thin CAD triangle along its
				// length (its cells stay long slivers, which shows as sawtooth edges wherever the value changes);
				// the longest edge alone would over-sample it across its width. For a well-shaped triangle the two
				// agree, so nothing changes there.
				const double size = geom[i].ok ? std::sqrt(std::sqrt(2.0 * geom[i].area) * geom[i].longestEdge) : 0.0;
				const int n = geom[i].ok
					? std::clamp(static_cast<int>(std::ceil(size / spacing)), 1, kMaxSubdivisions)
					: 1;
				subdivisions[i] = n;
				total += static_cast<size_t>(n) * static_cast<size_t>(n);
			}
			if (total <= params.sampleBudget)
				break;
			spacing *= std::sqrt(static_cast<double>(total) / static_cast<double>(params.sampleBudget)) * 1.05;
		}

		// Per-sample value storage, indexed through the ORIGINAL triangle so the renderer needs no knowledge of the
		// working mesh: gridN/offset per original triangle, all samples in one flat array (NaN = no value).
		out.samples.gridN.assign(origFaceCount, 0);
		out.samples.offset.assign(origFaceCount, 0);
		std::vector<size_t> sampleBase(faceCount, 0);
		size_t totalSamples = 0;
		for (size_t i = 0; i < faceCount; ++i)
		{
			sampleBase[i] = totalSamples;
			const size_t origFace = origFaceIndex[i];
			out.samples.gridN[origFace] = static_cast<unsigned char>(subdivisions[i]);
			out.samples.offset[origFace] = static_cast<unsigned int>(totalSamples);
			totalSamples += static_cast<size_t>(subdivisions[i]) * static_cast<size_t>(subdivisions[i]);
		}
		out.samples.values.assign(totalSamples, std::numeric_limits<float>::quiet_NaN());
		out.thickness.assign(origFaceCount, 0.0f);
		out.sampleWitness.assign(totalSamples, WallThicknessWitness());

		// ---- The ray cone, in a frame whose +z is the inward normal: the axis alone at a 0 degree spread,
		// otherwise also an inner ring (6 rays at half the cone angle) and an outer ring (12 rays at the full
		// angle). cr.z = cos(angle from the inward normal), kept for the tangent-sphere diameter. ----
		struct ConeRay { double x, y, z; };
		std::vector<ConeRay> cone;
		const double pi = std::acos(-1.0);
		const double coneDegrees = std::clamp(params.coneHalfAngleDegrees, 0.0, 45.0);
		cone.push_back({ 0.0, 0.0, 1.0 });
		if (coneDegrees >= 0.5)
		{
			const double coneAngle = coneDegrees * pi / 180.0;
			for (int i = 0; i < 6; ++i)
			{
				const double theta = coneAngle * 0.5, phi = 2.0 * pi * i / 6.0;
				cone.push_back({ std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) });
			}
			for (int i = 0; i < 12; ++i)
			{
				const double theta = coneAngle, phi = 2.0 * pi * (i + 0.5) / 12.0;
				cone.push_back({ std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) });
			}
		}

		// The tree must be fully built BEFORE it is queried from several threads (a query on an unbuilt tree
		// builds it lazily, which is not safe to race).
		tree.build();
		const bool sphereMethod = params.method == WallThicknessMethod::Sphere;
		if (sphereMethod)
			tree.accelerate_distance_queries(); // built up front for the same reason as build(): queried from several threads

		const double minAlignment = params.minExitAlignment;
		std::vector<unsigned char> validFlags(faceCount, 0);
		// How many hits along one ray may be stepped over before the sample is given up on.
		constexpr int kMaxHitWalk = 8;

		// The pure sphere fit at one surface point `p` (inward unit normal `in`, on face `self`): the largest sphere
		// tangent to the surface at p (centre p + r * in) that no surface point lies inside. The shrinking iteration:
		// the nearest hit straight behind the point bounds the diameter (r0 = d / 2); then, while the surface point q
		// nearest to the centre lies inside the sphere, the sphere tangent at p and passing through q has
		// radius |q - p|^2 / (2 (q - p) . in), strictly smaller, and becomes the new candidate.
		struct SphereFit
		{
			double r = 0.0;
			double upper = 0.0;        // the initial bound: half the distance to the first surface hit straight behind p
			double contact[3] = { 0, 0, 0 };
			FaceDescriptor face;
			bool ok = false;
		};
		const auto fitSphere = [&](const double p[3], const double in[3], const FaceDescriptor self) -> SphereFit
		{
			SphereFit fit;
			const WtRay3 ray(WtPoint3(p[0], p[1], p[2]), WtKernel::Vector_3(in[0], in[1], in[2]));
			const auto first = tree.first_intersection(ray, [self](const FaceDescriptor& id) { return id == self; });
			double distance = 0.0;
			if (!first || !hitPointFrom(first->first, p, fit.contact, distance) || !(distance > 0.0))
				return fit;
			fit.face = first->second;
			fit.r = fit.upper = 0.5 * distance;

			for (int iteration = 0; iteration < 48; ++iteration)
			{
				const double c[3] = { p[0] + fit.r * in[0], p[1] + fit.r * in[1], p[2] + fit.r * in[2] };
				const auto nearest = tree.closest_point_and_primitive(WtPoint3(c[0], c[1], c[2]));
				const double q[3] = { CGAL::to_double(nearest.first.x()), CGAL::to_double(nearest.first.y()), CGAL::to_double(nearest.first.z()) };
				const double dq = std::sqrt((q[0] - c[0]) * (q[0] - c[0]) + (q[1] - c[1]) * (q[1] - c[1]) + (q[2] - c[2]) * (q[2] - c[2]));
				if (dq >= fit.r * (1.0 - 1.0e-3))
					break; // nothing inside the sphere (the origin face touches it at p itself, at distance exactly r)

				const double qp[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
				const double qpLen2 = qp[0] * qp[0] + qp[1] * qp[1] + qp[2] * qp[2];
				const double along = in[0] * qp[0] + in[1] * qp[1] + in[2] * qp[2];
				if (!(along > 1.0e-9 * std::sqrt(qpLen2)))
					break; // a point on or behind the tangent plane cannot lie inside a sphere touching it at p (rounding noise)
				const double rNew = qpLen2 / (2.0 * along);
				if (!(rNew < fit.r))
					break;
				fit.r = rNew;
				fit.contact[0] = q[0]; fit.contact[1] = q[1]; fit.contact[2] = q[2];
				fit.face = nearest.second;
			}
			fit.ok = fit.r > 1.0e-7 * diag && std::isfinite(fit.r);
			return fit;
		};

		const bool edgeRelief = params.edgeRelief;
		constexpr int kMaxReliefSteps = 10;
		constexpr double kEdgeContactMinAngleDegrees = 40.0; // a contact this far off the inward normal is a side contact ...
		constexpr double kEdgeContactMaxAngleDegrees = 75.0; // ... but not a near-tangent one: beyond ~75 degrees (dihedral > 150)
		                                                    // the sphere is limited by the surface's CURVATURE (a thin rod, a
		                                                    // tessellated fillet), which is a real limit, not a sharp edge
		constexpr double kEdgeContactMaxFacing = 0.35;       // ... on a wall that does not face back at the sample

		// Sphere method for one sample: the pure fit, then - with edge relief - moved off a sharp convex edge. Returns
		// the diameter and fills `w` (origin, the point measured at, the contact that limits the sphere, its direction
		// relative to the inward normal, the contacted wall's facing).
		const auto solveSphere = [&](const double p0[3], const double in0[3], const FaceDescriptor self0,
		                             double& outDiameter, WallThicknessWitness& w) -> WallThicknessSampleStatus
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				w.origin[axis] = static_cast<float>(p0[axis]);
				w.source[axis] = w.origin[axis];
			}

			double p[3] = { p0[0], p0[1], p0[2] };
			double in[3] = { in0[0], in0[1], in0[2] };
			SphereFit fit = fitSphere(p, in, self0);
			if (!fit.ok)
				return WallThicknessSampleStatus::NoHit;
			const double upper = fit.upper; // the shift budget is tied to the wall found straight behind the ORIGINAL sample

			// Relief only ever RAISES a value (it removes an edge limit), so the walk keeps the largest fit it saw:
			// a step that lands somewhere worse (a point on an edge, a thinner neighbouring feature) cannot lower it.
			SphereFit best = fit;
			double bestP[3] = { p[0], p[1], p[2] };
			double bestIn[3] = { in[0], in[1], in[2] };
			int bestSteps = 0;

			int steps = 0;
			double totalShift = 0.0;
			while (edgeRelief && steps < kMaxReliefSteps)
			{
				const double cp[3] = { fit.contact[0] - p[0], fit.contact[1] - p[1], fit.contact[2] - p[2] };
				const double cpLen = std::sqrt(cp[0] * cp[0] + cp[1] * cp[1] + cp[2] * cp[2]);
				if (!(cpLen > 0.0))
					break;
				const double along = in[0] * cp[0] + in[1] * cp[1] + in[2] * cp[2];
				const double angle = std::acos(std::clamp(along / cpLen, -1.0, 1.0)) * 180.0 / pi;
				const size_t contactIndex = static_cast<size_t>(fit.face.idx());
				const double facing = contactIndex < geom.size()
					? geom[contactIndex].n[0] * in[0] + geom[contactIndex].n[1] * in[1] + geom[contactIndex].n[2] * in[2] : 1.0;
				if (angle < kEdgeContactMinAngleDegrees || angle > kEdgeContactMaxAngleDegrees || facing > kEdgeContactMaxFacing)
					break; // a wall behind the sample: this is the thickness, nothing to relieve

				// The way toward the edge, in the sample's tangent plane.
				double u[3] = { cp[0] - along * in[0], cp[1] - along * in[1], cp[2] - along * in[2] };
				const double uLen = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
				if (!(uLen > 1.0e-9 * cpLen))
					break;
				for (double& v : u) v /= uLen;

				// Squeezed from both sides (a wall also close on the far side of the sphere's centre)? Then the small
				// value is genuine - the end of a thin rib, a narrow web - and is kept.
				const double c[3] = { p[0] + fit.r * in[0], p[1] + fit.r * in[1], p[2] + fit.r * in[2] };
				const WtRay3 sideRay(WtPoint3(c[0], c[1], c[2]), WtKernel::Vector_3(-u[0], -u[1], -u[2]));
				const auto sideHit = tree.first_intersection(sideRay, [](const FaceDescriptor&) { return false; });
				double sideHitPoint[3] = { 0, 0, 0 };
				double sideDistance = 0.0;
				if (sideHit && hitPointFrom(sideHit->first, c, sideHitPoint, sideDistance) && sideDistance <= 1.3 * fit.r)
					break;

				// Move one radius away from the edge and re-project onto the surface (which may be the next face).
				totalShift += fit.r;
				if (totalShift > 1.5 * upper)
					break;
				const auto projected = tree.closest_point_and_primitive(WtPoint3(p[0] - u[0] * fit.r, p[1] - u[1] * fit.r, p[2] - u[2] * fit.r));
				const FaceDescriptor projectedFace = projected.second;
				const FaceGeom& pg = geom[static_cast<size_t>(projectedFace.idx())];
				if (!pg.ok)
					break;
				double pp[3] = { CGAL::to_double(projected.first.x()), CGAL::to_double(projected.first.y()), CGAL::to_double(projected.first.z()) };
				// The nearest surface point of a point that left its face lies ON an edge, where the neighbouring face
				// touches the sphere at once and collapses it. Keep the new sample inside its face: if any barycentric
				// coordinate is under 3%, pull it 10% of the way to the centroid (which puts every coordinate over 3%).
				{
					const double v0[3] = { pg.b[0] - pg.a[0], pg.b[1] - pg.a[1], pg.b[2] - pg.a[2] };
					const double v1[3] = { pg.c[0] - pg.a[0], pg.c[1] - pg.a[1], pg.c[2] - pg.a[2] };
					const double v2[3] = { pp[0] - pg.a[0], pp[1] - pg.a[1], pp[2] - pg.a[2] };
					const double d00 = v0[0] * v0[0] + v0[1] * v0[1] + v0[2] * v0[2];
					const double d01 = v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2];
					const double d11 = v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
					const double d20 = v2[0] * v0[0] + v2[1] * v0[1] + v2[2] * v0[2];
					const double d21 = v2[0] * v1[0] + v2[1] * v1[1] + v2[2] * v1[2];
					const double denom = d00 * d11 - d01 * d01;
					if (denom > 0.0)
					{
						const double bv = (d11 * d20 - d01 * d21) / denom;
						const double bw = (d00 * d21 - d01 * d20) / denom;
						const double bu = 1.0 - bv - bw;
						if (std::min({ bu, bv, bw }) < 0.03)
						{
							for (int axis = 0; axis < 3; ++axis)
								pp[axis] += 0.1 * ((pg.a[axis] + pg.b[axis] + pg.c[axis]) / 3.0 - pp[axis]);
						}
					}
				}
				const double inNew[3] = { -pg.n[0], -pg.n[1], -pg.n[2] };
				const SphereFit next = fitSphere(pp, inNew, projectedFace);
				if (!next.ok)
					break;
				p[0] = pp[0]; p[1] = pp[1]; p[2] = pp[2];
				in[0] = inNew[0]; in[1] = inNew[1]; in[2] = inNew[2];
				fit = next;
				++steps;
				if (fit.r > best.r)
				{
					best = fit;
					bestP[0] = p[0]; bestP[1] = p[1]; bestP[2] = p[2];
					bestIn[0] = in[0]; bestIn[1] = in[1]; bestIn[2] = in[2];
					bestSteps = steps;
				}
			}
			fit = best;
			p[0] = bestP[0]; p[1] = bestP[1]; p[2] = bestP[2];
			in[0] = bestIn[0]; in[1] = bestIn[1]; in[2] = bestIn[2];
			steps = bestSteps;

			const double cp[3] = { fit.contact[0] - p[0], fit.contact[1] - p[1], fit.contact[2] - p[2] };
			const double cpLen = std::sqrt(cp[0] * cp[0] + cp[1] * cp[1] + cp[2] * cp[2]);
			const size_t contactIndex = static_cast<size_t>(fit.face.idx());
			for (int axis = 0; axis < 3; ++axis)
			{
				w.hit[axis] = static_cast<float>(fit.contact[axis]);
				w.source[axis] = static_cast<float>(p[axis]);
			}
			w.distance = static_cast<float>(2.0 * fit.r);
			w.angleDegrees = static_cast<float>(cpLen > 0.0
				? std::acos(std::clamp((in[0] * cp[0] + in[1] * cp[1] + in[2] * cp[2]) / cpLen, -1.0, 1.0)) * 180.0 / pi : 0.0);
			w.hitTriangle = contactIndex < origFaceIndex.size() ? static_cast<int>(origFaceIndex[contactIndex]) : -1;
			if (contactIndex < geom.size())
				w.facing = static_cast<float>(geom[contactIndex].n[0] * in[0] + geom[contactIndex].n[1] * in[1] + geom[contactIndex].n[2] * in[2]);
			w.reliefSteps = static_cast<unsigned char>(steps);
			w.status = WallThicknessSampleStatus::Valid;
			outDiameter = 2.0 * fit.r;
			return WallThicknessSampleStatus::Valid;
		};

		const auto processFace = [&](size_t i)
		{
			const FaceGeom& g = geom[i];
			if (!g.ok)
				return;
			const FaceDescriptor self = faceOf[i];
			const size_t ownVolume = volumeIds[self];
			const size_t origFace = origFaceIndex[i];

			// Inward normal and an arbitrary orthonormal frame around it.
			const double in[3] = { -g.n[0], -g.n[1], -g.n[2] };
			const int helperAxis = (std::abs(in[0]) <= std::abs(in[1]) && std::abs(in[0]) <= std::abs(in[2])) ? 0
				: (std::abs(in[1]) <= std::abs(in[2]) ? 1 : 2);
			double helper[3] = { 0, 0, 0 };
			helper[helperAxis] = 1.0;
			double t[3] = { in[1] * helper[2] - in[2] * helper[1], in[2] * helper[0] - in[0] * helper[2], in[0] * helper[1] - in[1] * helper[0] };
			const double tLen = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
			if (!(tLen > 0.0))
				return;
			for (double& v : t) v /= tLen;
			const double b[3] = { in[1] * t[2] - in[2] * t[1], in[2] * t[0] - in[0] * t[2], in[0] * t[1] - in[1] * t[0] };

			std::vector<FaceDescriptor> skipped; // faces the current ray must not report (reused across rays)
			skipped.reserve(kMaxHitWalk + 1);

			const int n = subdivisions[i];
			float* sampleValues = out.samples.values.data() + sampleBase[i];
			WallThicknessWitness* sampleWitnesses = out.sampleWitness.data() + sampleBase[i];
			double faceMin = 0.0;
			bool faceValid = false;

			SubTriangleGrid::forEach(n, [&](int sampleIndex, double, double, double, double, double, double, double cu, double cv)
			{
				const double origin[3] = {
					g.oa[0] + cu * (g.ob[0] - g.oa[0]) + cv * (g.oc[0] - g.oa[0]),
					g.oa[1] + cu * (g.ob[1] - g.oa[1]) + cv * (g.oc[1] - g.oa[1]),
					g.oa[2] + cu * (g.ob[2] - g.oa[2]) + cv * (g.oc[2] - g.oa[2]) };

				if (sphereMethod)
				{
					double diameter = 0.0;
					WallThicknessWitness w;
					const WallThicknessSampleStatus status = solveSphere(origin, in, self, diameter, w);
					w.status = status;
					sampleWitnesses[sampleIndex] = w;
					if (status != WallThicknessSampleStatus::Valid)
						return;
					sampleValues[sampleIndex] = static_cast<float>(diameter);
					if (!faceValid || diameter < faceMin)
					{
						faceMin = diameter;
						faceValid = true;
					}
					return;
				}

				double sampleMin = 0.0;
				bool sampleValid = false;
				WallThicknessWitness sampleWitness;
				WallThicknessWitness axisOutcome; // the axis ray's outcome - what a sample with no value reports
				for (size_t rayIndex = 0; rayIndex < cone.size(); ++rayIndex)
				{
					const ConeRay& cr = cone[rayIndex];
					const double d[3] = {
						t[0] * cr.x + b[0] * cr.y + in[0] * cr.z,
						t[1] * cr.x + b[1] * cr.y + in[1] * cr.z,
						t[2] * cr.x + b[2] * cr.y + in[2] * cr.z };
					const WtRay3 ray(WtPoint3(origin[0], origin[1], origin[2]), WtKernel::Vector_3(d[0], d[1], d[2]));

					WallThicknessWitness outcome;
					for (int axis = 0; axis < 3; ++axis)
						outcome.origin[axis] = static_cast<float>(origin[axis]);
					outcome.angleDegrees = static_cast<float>(std::acos(std::clamp(cr.z, -1.0, 1.0)) * 180.0 / pi);

					// Walk the hits along the ray from the nearest outwards, never the origin face itself (the origin
					// lies strictly inside it). A hit that cannot be the wall behind - another body, a face met from
					// outside (touching/overlapping bodies put such faces exactly on top of the true wall, and which
					// of the coincident pair is "nearest" is arbitrary), a degenerate triangle - is stepped over.
					// The first genuine EXIT ends the walk: accepted if it is a wall facing back at the surface,
					// otherwise the ray has left the material through an edge or a steep wall and has no value.
					skipped.clear();
					skipped.push_back(self);
					bool accepted = false;
					double distance = 0.0;
					for (int step = 0; step < kMaxHitWalk; ++step)
					{
						const auto hit = tree.first_intersection(ray, [&skipped](const FaceDescriptor& id)
						{
							return std::find(skipped.begin(), skipped.end(), id) != skipped.end();
						});
						if (!hit)
							break; // outcome keeps the reason of whatever was stepped over (NoHit if nothing was)
						double hitPoint[3] = { 0, 0, 0 };
						if (!hitPointFrom(hit->first, origin, hitPoint, distance))
							break;

						const FaceDescriptor hitFace = hit->second;
						const size_t hitIndex = static_cast<size_t>(hitFace.idx());
						const FaceGeom& hg = geom[hitIndex];
						const double leaving = hg.n[0] * d[0] + hg.n[1] * d[1] + hg.n[2] * d[2];
						const double facingBack = hg.n[0] * in[0] + hg.n[1] * in[1] + hg.n[2] * in[2];

						const auto record = [&](WallThicknessSampleStatus status)
						{
							outcome.status = status;
							for (int axis = 0; axis < 3; ++axis)
								outcome.hit[axis] = static_cast<float>(hitPoint[axis]);
							outcome.distance = static_cast<float>(distance);
							outcome.facing = static_cast<float>(facingBack);
							outcome.hitTriangle = hitIndex < origFaceIndex.size() ? static_cast<int>(origFaceIndex[hitIndex]) : -1;
						};

						if (volumeIds[hitFace] != ownVolume)
						{
							record(WallThicknessSampleStatus::OtherSolid);
							skipped.push_back(hitFace);
							continue;
						}
						if (!hg.ok)
						{
							record(WallThicknessSampleStatus::DegenerateHit);
							skipped.push_back(hitFace);
							continue;
						}
						if (leaving <= 0.0)
						{
							record(WallThicknessSampleStatus::EnteringFace);
							skipped.push_back(hitFace);
							continue;
						}
						if (facingBack < minAlignment)
						{
							record(WallThicknessSampleStatus::GlancingExit);
							break;
						}
						record(WallThicknessSampleStatus::Valid);
						accepted = true;
						break;
					}
					if (rayIndex == 0)
						axisOutcome = outcome;
					if (!accepted)
						continue;

					// The largest sphere TANGENT to the surface at this sample that a wall point at `distance`
					// along a ray at angle theta from the inward normal does not cross has diameter
					// distance / cos(theta) (a sphere tangent at p with centre p + r*n contains the point
					// p + L*d exactly when L <= 2 r cos(theta)). Over a flat wall of thickness w every ray gives
					// >= w with equality on the axis; across a round bar of diameter D every ray gives exactly D -
					// so the minimum over the cone is unbiased for both, unlike projecting the distance onto
					// the normal (distance * cos), which reads a round bar up to 25% thin at a 30 degree cone.
					const double diameter = distance / cr.z; // cr.z = cos(angle to the inward normal)
					if (!sampleValid || diameter < sampleMin)
					{
						sampleMin = diameter;
						sampleValid = true;
						sampleWitness = outcome;
					}
				}
				sampleWitnesses[sampleIndex] = sampleValid ? sampleWitness : axisOutcome;
				if (!sampleValid)
					return;
				sampleValues[sampleIndex] = static_cast<float>(sampleMin);
				if (!faceValid || sampleMin < faceMin)
				{
					faceMin = sampleMin;
					faceValid = true;
				}
			});

			if (faceValid && std::isfinite(faceMin))
			{
				out.thickness[origFace] = static_cast<float>(faceMin);
				validFlags[i] = 1;
			}
		};

		// Faces are independent: split them across the available cores in small chunks.
		const unsigned hardware = std::thread::hardware_concurrency();
		const unsigned threadCount = std::clamp(hardware == 0 ? 4u : hardware, 1u, 16u);
		std::atomic<size_t> next{ 0 };
		constexpr size_t kChunk = 32;
		const auto worker = [&]()
		{
			for (;;)
			{
				const size_t begin = next.fetch_add(kChunk);
				if (begin >= faceCount)
					break;
				const size_t end = std::min(begin + kChunk, faceCount);
				for (size_t i = begin; i < end; ++i)
				{
					try { processFace(i); }
					catch (...) { validFlags[i] = 0; } // never let a CGAL exception escape a worker thread
				}
			}
		};
		std::vector<std::thread> pool;
		for (unsigned i = 1; i < threadCount; ++i)
			pool.emplace_back(worker);
		worker();
		for (std::thread& th : pool)
			th.join();

		out.valid.assign(origFaceCount, false);
		for (size_t i = 0; i < faceCount; ++i)
			out.valid[origFaceIndex[i]] = validFlags[i] != 0;

		// One line saying how the samples fared - the quickest way to see whether gaps in the display are a few
		// stray samples or a systematic problem, and why.
		size_t statusCount[6] = { 0, 0, 0, 0, 0, 0 };
		size_t relievedCount = 0;
		for (size_t k2 = 0; k2 < out.samples.values.size(); ++k2)
		{
			if (!std::isnan(out.samples.values[k2]))
			{
				++statusCount[0];
				if (out.sampleWitness[k2].reliefSteps > 0)
					++relievedCount;
			}
			else
			{
				++statusCount[std::min<size_t>(5, static_cast<size_t>(out.sampleWitness[k2].status))];
			}
		}
		qInfo().noquote() << QStringLiteral("[WallThickness] %1 samples on %2 triangles (%3): %4 valid, no value: %5 no wall found, "
			"%6 other body, %7 entering face, %8 steep/edge-on wall, %9 degenerate wall")
			.arg(totalSamples).arg(faceCount)
			.arg(sphereMethod
				? QStringLiteral("inscribed sphere, edge relief %1, %2 samples relieved").arg(edgeRelief ? QStringLiteral("on") : QStringLiteral("off")).arg(relievedCount)
				: QStringLiteral("rays, spread %1 deg").arg(coneDegrees, 0, 'f', 0))
			.arg(statusCount[0]).arg(statusCount[1]).arg(statusCount[2]).arg(statusCount[3]).arg(statusCount[4]).arg(statusCount[5]);
		return true;
	}
}

WallThicknessResult WallThicknessAnalyzer::computeThickness(SceneMesh* mesh, const WallThicknessParams& params)
{
	if (!mesh)
		return WallThicknessResult();
	return computeThickness(mesh->getTrsfPoints(), mesh->getIndices(), params);
}

WallThicknessResult WallThicknessAnalyzer::computeThickness(
	const std::vector<float>& origPoints, const std::vector<unsigned int>& origIndices,
	const WallThicknessParams& params)
{
	WallThicknessResult result;
	const size_t origVertexCount = origPoints.size() / 3;
	const size_t origFaceCount = origIndices.size() / 3;
	if (origPoints.empty() || origIndices.size() < 3)
	{
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::InvalidIndices);
		return result;
	}

	// ---- Vertex validity + a filtered, ORIGINAL-face-index-tracking face
	// list - see CurvatureAnalyzer.cpp's identical reasoning for why a face
	// touching an out-of-bounds or non-finite vertex is excluded outright
	// rather than fabricated. Degenerate (near-zero-area) triangles are
	// ALSO skipped here - CGAL's own AABB tree documentation calls these
	// out as needing explicit handling, not silent inclusion. origFaceIndex
	// tracks, for each surviving soup face, which ORIGINAL triangle (0-based,
	// matching origIndices/3 order) it came from - needed to scatter the
	// per-face result back into an array sized to the mesh's real triangle
	// count once degenerate/invalid faces have been dropped from the
	// working mesh entirely. ----
	std::vector<bool> vertexFinite(origVertexCount, false);
	for (size_t v = 0; v < origVertexCount; ++v)
		vertexFinite[v] = isFinitePoint(origPoints, v);

	std::vector<std::array<unsigned int, 3>> validFaces;
	std::vector<size_t> origFaceIndex;
	validFaces.reserve(origFaceCount);
	origFaceIndex.reserve(origFaceCount);
	for (size_t f = 0; f < origFaceCount; ++f)
	{
		const unsigned int ia = origIndices[f * 3 + 0];
		const unsigned int ib = origIndices[f * 3 + 1];
		const unsigned int ic = origIndices[f * 3 + 2];
		if (ia >= origVertexCount || ib >= origVertexCount || ic >= origVertexCount)
		{
			// An out-of-bounds index isn't just unusable for THIS analysis -
			// the per-face result below eventually reaches RenderableMesh::
			// setAnalysisOverlayFlatColors(), which walks EVERY original
			// face via the mesh's own real index buffer with no bounds
			// checking of its own. Silently excluding just this one face
			// here while still returning a full-length, otherwise-successful
			// result would leave that same out-of-bounds index to be
			// dereferenced during rendering - reject the WHOLE mesh instead,
			// so it never reaches applyFlatResult() at all. (A defensive
			// bounds-check was also added directly in
			// setAnalysisOverlayFlatColors() itself, as a second,
			// independent layer.)
			result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::InvalidIndices);
			return result;
		}
		if (!vertexFinite[ia] || !vertexFinite[ib] || !vertexFinite[ic])
			continue; // in-bounds but NaN/Inf position - safe to just exclude this one face, no OOB risk downstream

		// True degeneracy test via CGAL::collinear() - an exact geometric
		// predicate (this kernel's orientation test), not a magnitude/angle
		// threshold of any kind. A fixed absolute cross-product cutoff
		// rejects every face of a small part; a relative sin^2(angle)
		// threshold instead wrongly conflates "skinny" with "degenerate" -
		// a real, valid triangle with edges (1000,0,0) and (1000,0.0001,0)
		// has a perfectly well-defined, tiny-but-nonzero area (it's a
		// legitimate thin side-wall triangle, not degenerate), yet a
		// relative-angle threshold flags it anyway since sin^2 of its
		// vertex angle is extremely small regardless of the triangle's
		// actual (nonzero) area. CGAL::collinear() answers the only
		// question that actually matters - are these 3 points exactly
		// collinear (which subsumes the coincident-point case too) - with
		// no epsilon/threshold of any kind, so it can't misclassify either
		// a small-but-valid part or a skinny-but-valid triangle.
		const WtPoint3 wpa(static_cast<double>(origPoints[ia * 3 + 0]), static_cast<double>(origPoints[ia * 3 + 1]), static_cast<double>(origPoints[ia * 3 + 2]));
		const WtPoint3 wpb(static_cast<double>(origPoints[ib * 3 + 0]), static_cast<double>(origPoints[ib * 3 + 1]), static_cast<double>(origPoints[ib * 3 + 2]));
		const WtPoint3 wpc(static_cast<double>(origPoints[ic * 3 + 0]), static_cast<double>(origPoints[ic * 3 + 1]), static_cast<double>(origPoints[ic * 3 + 2]));
		if (CGAL::collinear(wpa, wpb, wpc))
			continue; // degenerate triangle (coincident points or exactly collinear edges)

		validFaces.push_back({ ia, ib, ic });
		origFaceIndex.push_back(f);
	}
	if (validFaces.empty())
	{
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::InvalidIndices);
		return result;
	}

	// ---- Build the soup, weld exact-coincident duplicate points (same
	// seam-duplication fix as MeshProperties.cpp - without it, a
	// conventional per-face-normal-split export would spuriously fail the
	// closed check below), then run the whole-mesh validity gate in the
	// same specific order as MeshProperties.cpp/Step 1's plan, for the same
	// documented-undefined-behavior-otherwise reason. ----
	std::vector<WtPoint3> soupPoints;
	soupPoints.reserve(origVertexCount);
	for (size_t v = 0; v < origVertexCount; ++v)
	{
		soupPoints.emplace_back(
			vertexFinite[v] ? static_cast<double>(origPoints[v * 3 + 0]) : 0.0,
			vertexFinite[v] ? static_cast<double>(origPoints[v * 3 + 1]) : 0.0,
			vertexFinite[v] ? static_cast<double>(origPoints[v * 3 + 2]) : 0.0);
	}
	std::vector<std::array<std::size_t, 3>> soupFaces;
	soupFaces.reserve(validFaces.size());
	for (const auto& f : validFaces)
		soupFaces.push_back({ f[0], f[1], f[2] });

	CGAL::Polygon_mesh_processing::merge_duplicate_points_in_polygon_soup(soupPoints, soupFaces);

	if (!PMP::is_polygon_soup_a_polygon_mesh(soupFaces))
	{
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::InvalidIndices);
		return result;
	}

	WtMesh workingMesh;
	PMP::polygon_soup_to_polygon_mesh(soupPoints, soupFaces, workingMesh);
	if (workingMesh.number_of_vertices() == 0 || workingMesh.number_of_faces() != soupFaces.size())
	{
		// polygon_soup_to_polygon_mesh() can legitimately produce fewer
		// faces than requested if the soup (even after the is_polygon_soup_
		// a_polygon_mesh() gate above) still can't be built consistently -
		// treat that as InvalidIndices too rather than silently analyzing a
		// mesh that no longer corresponds 1:1 to origFaceIndex.
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::InvalidIndices);
		return result;
	}

	if (!CGAL::is_closed(workingMesh))
	{
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::OpenBoundary);
		return result;
	}
	if (PMP::does_self_intersect(workingMesh))
	{
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::SelfIntersecting);
		return result;
	}
	if (!PMP::does_bound_a_volume(workingMesh))
	{
		result.rejectionReason = describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::UnresolvedOrientation);
		return result;
	}

	// ---- Establish genuine inward/outward orientation - passing the gate
	// above does NOT by itself establish which side is "inward" (this
	// plan's own earlier draft overstated that; consistently-oriented faces
	// can still all point either way). workingMesh is already a disposable,
	// local CGAL structure built from a soup snapshot - it has no
	// connection back to the document's real SceneMesh data, so mutating it
	// in place here already satisfies "never mutate the document's actual
	// coordinates" without needing a further internal copy-of-a-copy. ----
	PMP::orient_to_bound_a_volume(workingMesh);

	// ---- Solid-region classification (see this class's own doc comment
	// for why volume_connected_components(), not surface-connected-
	// components). ----
	auto volumeIdMap = workingMesh.add_property_map<FaceDescriptor, std::size_t>("f:volume", 0).first;
	PMP::volume_connected_components(workingMesh, volumeIdMap);

	AABBTree aabbTree;
	PMP::build_AABB_tree(workingMesh, aabbTree);

	// ---- Per-face inward raycast. Iterates workingMesh's faces in their
	// natural creation order, which polygon_soup_to_polygon_mesh() assigns
	// 1:1 with soupFaces' own order (one add_face() call per input polygon,
	// in order) - the same order origFaceIndex was built against above, so
	// index k here always corresponds to origFaceIndex[k]. ----
	result.thicknessPerFace.assign(origFaceCount, 0.0f);
	result.validPerFace.assign(origFaceCount, false);

	if (params.method != WallThicknessMethod::NormalRay)
	{
		LocalThicknessOutput local;
		if (computeLocalThickness(workingMesh, aabbTree, volumeIdMap, origFaceIndex, origPoints, origIndices, params, local))
		{
			// Already indexed by ORIGINAL triangle (degenerate faces dropped from the working mesh stay invalid).
			result.thicknessPerFace = std::move(local.thickness);
			result.validPerFace = std::move(local.valid);
			result.samples = std::move(local.samples);
			result.sampleWitness = std::move(local.sampleWitness);
			result.succeeded = true;
			return result;
		}
		// Face numbering did not match expectations: fall through to the normal-ray estimate rather than fail.
	}

	struct Hit { double distance; std::size_t volumeId; };

	size_t k = 0;
	for (FaceDescriptor f : faces(workingMesh))
	{
		if (k >= origFaceIndex.size())
			break;
		const size_t thisOrigFace = origFaceIndex[k];
		++k;

		const auto h = halfedge(f, workingMesh);
		const VertexDescriptor va = source(h, workingMesh);
		const VertexDescriptor vb = target(h, workingMesh);
		const VertexDescriptor vc = target(next(h, workingMesh), workingMesh);
		const WtPoint3& pa = workingMesh.point(va);
		const WtPoint3& pb = workingMesh.point(vb);
		const WtPoint3& pc = workingMesh.point(vc);

		const double pax = CGAL::to_double(pa.x()), pay = CGAL::to_double(pa.y()), paz = CGAL::to_double(pa.z());
		const double pbx = CGAL::to_double(pb.x()), pby = CGAL::to_double(pb.y()), pbz = CGAL::to_double(pb.z());
		const double pcx = CGAL::to_double(pc.x()), pcy = CGAL::to_double(pc.y()), pcz = CGAL::to_double(pc.z());

		const double centroidX = (pax + pbx + pcx) / 3.0;
		const double centroidY = (pay + pby + pcy) / 3.0;
		const double centroidZ = (paz + pbz + pcz) / 3.0;

		// Outward face normal, from workingMesh's own now-resolved winding
		// (orient_to_bound_a_volume() above) - recomputed geometrically
		// rather than trusting any imported normal, per this class's own
		// doc comment. Computed and normalized in DOUBLE precision, with no
		// absolute-magnitude cutoff: a fixed float epsilon here (e.g. 1e-9)
		// rejects every face of a small part, since a cross-product
		// magnitude scales with the SQUARE of the part's size (a 1e-5-unit
		// cube's faces measure ~1e-10, under any single fixed cutoff, even
		// though this triangle already passed the exact CGAL::collinear()
		// non-degeneracy check above). Only an exact-zero cross product -
		// mathematically only possible for a truly collinear/coincident
		// triangle, which collinear() already excluded - is guarded here,
		// as a pure defensive backstop rather than a source of rejection.
		const double e1x = pbx - pax, e1y = pby - pay, e1z = pbz - paz;
		const double e2x = pcx - pax, e2y = pcy - pay, e2z = pcz - paz;
		const double nx = e1y * e2z - e1z * e2y;
		const double ny = e1z * e2x - e1x * e2z;
		const double nz = e1x * e2y - e1y * e2x;
		const double nLenSq = nx * nx + ny * ny + nz * nz;
		if (nLenSq <= 0.0)
			continue; // exact mathematical degeneracy only - see comment above
		const double nLen = std::sqrt(nLenSq);
		const double inwardX = -(nx / nLen), inwardY = -(ny / nLen), inwardZ = -(nz / nLen);

		// Cast from the TRUE surface centroid - no artificial inward
		// displacement. Self-intersection with the source face itself is
		// excluded by PRIMITIVE ID (hitFace == f) below - the ONLY
		// exclusion applied. No distance or adjacency-based filtering is
		// used: this kernel is Exact_predicates_inexact_constructions, so
		// CGAL's own intersection PREDICATE (does a hit genuinely exist
		// between this ray and that triangle) is already exact - any
		// intersection reported for a face other than f is a real,
		// geometrically genuine hit, not a false positive from floating-
		// point noise, however close it is. Three different distance-based
		// "self-hit noise" guards were tried here and each was defeated by
		// a different case: a mesh-wide-scale nudge/cutoff jumped through
		// (or under-reported) a genuinely thin wall; a coordinate-magnitude
		// cutoff did the same once the part was translated far from the
		// origin; and restricting the cutoff to topologically-adjacent
		// faces still failed for a tetrahedron, where EVERY face is
		// edge-adjacent to every other face by definition, so a thin
		// tetrahedron's genuine opposite-wall hit is always "adjacent".
		// Adjacency answers a different question than "is this self-hit
		// noise" - they are not the same thing, and no distance/adjacency
		// heuristic can safely stand in for the exact predicate CGAL
		// already provides.
		const WtRay3 ray(
			WtPoint3(centroidX, centroidY, centroidZ),
			WtKernel::Vector_3(inwardX, inwardY, inwardZ));

		std::vector<AABBTraits::Intersection_and_primitive_id<WtRay3>::Type> rawHits;
		aabbTree.all_intersections(ray, std::back_inserter(rawHits));

		std::vector<Hit> hits;
		hits.reserve(rawHits.size());
		for (const auto& rh : rawHits)
		{
			const FaceDescriptor hitFace = rh.second;
			if (hitFace == f)
				continue; // exact self-hit exclusion - see this ray's own doc comment above

			double hx, hy, hz;
			if (const WtPoint3* p = std::get_if<WtPoint3>(&rh.first))
			{
				hx = CGAL::to_double(p->x()); hy = CGAL::to_double(p->y()); hz = CGAL::to_double(p->z());
			}
			else if (const WtKernel::Segment_3* seg = std::get_if<WtKernel::Segment_3>(&rh.first))
			{
				// The ray lies exactly in the hit triangle's own plane -
				// a genuinely ambiguous intersection (a whole segment, not
				// a single point) rather than a numerical artifact to
				// discard. Handled explicitly: take whichever endpoint is
				// nearer the ray origin, the same "nearest hit wins"
				// principle applied to every other hit here, rather than
				// an arbitrary fixed choice (the segment's own source()/
				// target() ordering isn't guaranteed to correlate with
				// distance from the ray origin).
				const WtPoint3 sp = seg->source();
				const WtPoint3 tp = seg->target();
				const double spx = CGAL::to_double(sp.x()) - centroidX, spy = CGAL::to_double(sp.y()) - centroidY, spz = CGAL::to_double(sp.z()) - centroidZ;
				const double tpx = CGAL::to_double(tp.x()) - centroidX, tpy = CGAL::to_double(tp.y()) - centroidY, tpz = CGAL::to_double(tp.z()) - centroidZ;
				const double spDistSq = spx * spx + spy * spy + spz * spz;
				const double tpDistSq = tpx * tpx + tpy * tpy + tpz * tpz;
				if (spDistSq <= tpDistSq)
				{
					hx = CGAL::to_double(sp.x()); hy = CGAL::to_double(sp.y()); hz = CGAL::to_double(sp.z());
				}
				else
				{
					hx = CGAL::to_double(tp.x()); hy = CGAL::to_double(tp.y()); hz = CGAL::to_double(tp.z());
				}
			}
			else
			{
				continue;
			}

			const double dx = hx - centroidX, dy = hy - centroidY, dz = hz - centroidZ;
			const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

			hits.push_back({ distance, volumeIdMap[hitFace] });
		}

		if (hits.empty())
			continue; // no-hit ray on an otherwise-valid mesh - left invalid, never a silent 0

		std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.distance < b.distance; });

		const std::size_t ownVolumeId = volumeIdMap[f];
		for (const Hit& hit : hits)
		{
			if (hit.volumeId != ownVolumeId)
				continue; // different solid region - e.g. a genuinely separate body sharing this SceneMesh

			result.thicknessPerFace[thisOrigFace] = static_cast<float>(hit.distance);
			result.validPerFace[thisOrigFace] = true;
			break;
		}
	}

	result.succeeded = true;
	return result;
}
