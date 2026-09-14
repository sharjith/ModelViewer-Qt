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
#include <cmath>
#include <variant>

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
}

WallThicknessResult WallThicknessAnalyzer::computeThickness(SceneMesh* mesh)
{
	if (!mesh)
		return WallThicknessResult();
	return computeThickness(mesh->getTrsfPoints(), mesh->getIndices());
}

WallThicknessResult WallThicknessAnalyzer::computeThickness(
	const std::vector<float>& origPoints, const std::vector<unsigned int>& origIndices)
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
