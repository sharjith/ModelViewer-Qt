#pragma once

#include <CGAL/intersections.h>
#include <CGAL/squared_distance_3.h> // squared_distance() - degenerate-overlap scale
#include <CGAL/Kernel_traits.h>
#include <CGAL/boost/graph/iterator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <variant>

// Classifies a face pair reported by CGAL::Polygon_mesh_processing::self_intersections() as
// "contact only" (the two triangles merely touch, along a shared-position edge or at a shared-position
// vertex) as opposed to a genuine crossing/overlap.
//
// Why this exists: a B-Rep with a knife-edge pinch (an edge legitimately shared by 4 faces - see
// MeshProperties.cpp's non-manifold note, MBB Gehause Rohteil.step) becomes a valid polygon mesh only by
// splitting the pinch vertices, which leaves two distinct vertices at the same position. CGAL's
// self-intersection test reports those coincident-but-unconnected triangles as intersecting even though
// the surface only touches itself. Counting them as crossings made a valid solid look badly
// self-intersecting.
//
// Deliberately conservative - anything not positively identified as contact is treated as a real
// crossing (never false-positive-exclude, see feedback memory):
//   - 0 shared vertex positions  -> real (an interior crossing);
//   - 2 shared positions (edge)  -> contact only if the triangles are not coplanar (two non-coplanar
//                                   triangles sharing an edge meet exactly along that edge);
//   - 1 shared position (vertex) -> contact if the triangle-triangle intersection is that single point,
//                                   empty, or a segment of rounding-error length (the kernel's inexact
//                                   constructions return all three for pure vertex contact);
//   - 3 shared positions (duplicate face) -> real.
enum class FacePairKind
{
	ContactEdge,          // 2 shared positions, non-coplanar - touching along the shared edge
	ContactVertex,        // 1 shared position, intersection is just that point
	RealNoSharedPosition, // 0 shared positions
	RealEdgeCoplanar,     // 2 shared positions but coplanar (fold-back / overlap)
	RealVertexExtended,   // 1 shared position but intersection is more than that point
	RealDuplicateFace,    // 3 shared positions
	RealMalformed         // not a triangle
};

inline bool isContactKind(FacePairKind k)
{
	return k == FacePairKind::ContactEdge || k == FacePairKind::ContactVertex;
}

template <class Mesh>
FacePairKind classifyFacePair(const Mesh& mesh, typename Mesh::Face_index f1, typename Mesh::Face_index f2)
{
	using Point = typename Mesh::Point;
	using Kernel = typename CGAL::Kernel_traits<Point>::Kernel;

	auto corners = [&mesh](typename Mesh::Face_index f) {
		std::array<Point, 3> p;
		int i = 0;
		for (auto v : CGAL::vertices_around_face(mesh.halfedge(f), mesh))
		{
			if (i < 3)
				p[i] = mesh.point(v);
			++i;
		}
		return std::make_pair(p, i == 3);
	};

	const auto [a, aIsTriangle] = corners(f1);
	const auto [b, bIsTriangle] = corners(f2);
	if (!aIsTriangle || !bIsTriangle)
		return FacePairKind::RealMalformed;

	int shared = 0;
	int bUnshared = -1; // index in b of a vertex not coincident with any vertex of a (meaningful when shared == 2)
	for (int j = 0; j < 3; ++j)
	{
		bool matched = false;
		for (int i = 0; i < 3; ++i)
			if (a[i] == b[j])
				matched = true;
		if (matched)
			++shared;
		else
			bUnshared = j;
	}

	if (shared == 2)
		return CGAL::coplanar(a[0], a[1], a[2], b[bUnshared]) ? FacePairKind::RealEdgeCoplanar : FacePairKind::ContactEdge;

	if (shared == 1)
	{
		const auto result = CGAL::intersection(typename Kernel::Triangle_3(a[0], a[1], a[2]),
		                                       typename Kernel::Triangle_3(b[0], b[1], b[2]));
		// CGAL's intersection() uses inexact constructions in this kernel, so for triangles that touch only
		// at the shared vertex it can come back as a Point, as "no intersection" at all, or as a degenerate
		// segment of rounding-error length (observed on MBB Gehause Rohteil.step: isect=none and
		// segLen ~1e-16 against edges of length 1-3). All three mean vertex contact.
		if (!result.has_value() || std::holds_alternative<typename Kernel::Point_3>(*result))
			return FacePairKind::ContactVertex;
		if (const auto* seg = std::get_if<typename Kernel::Segment_3>(&*result))
		{
			auto edgeLen2 = [](const std::array<Point, 3>& p) {
				double m = 1e300;
				for (int i = 0; i < 3; ++i)
					m = std::min(m, CGAL::to_double(CGAL::squared_distance(p[i], p[(i + 1) % 3])));
				return m;
			};
			const double scale2 = std::min(edgeLen2(a), edgeLen2(b));
			if (CGAL::to_double(seg->squared_length()) <= 1.0e-18 * scale2) // overlap <= 1e-9 of the shortest edge
				return FacePairKind::ContactVertex;
		}

		return FacePairKind::RealVertexExtended;
	}

	return shared == 0 ? FacePairKind::RealNoSharedPosition : FacePairKind::RealDuplicateFace;
}

template <class Mesh>
bool isContactOnlyFacePair(const Mesh& mesh, typename Mesh::Face_index f1, typename Mesh::Face_index f2)
{
	const FacePairKind k = classifyFacePair(mesh, f1, f2);
	return isContactKind(k);
}
