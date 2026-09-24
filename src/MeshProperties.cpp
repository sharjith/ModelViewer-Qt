#include "MeshProperties.h"
#include <QObject>
#include <iostream>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>

// CGAL is used here ONLY to answer "is this raw triangle soup actually a
// closed, non-self-intersecting solid" - deliberately NOT MeshRepair.h's
// repairSoupToMesh() pipeline, which can (and by design does) alter
// geometry (self-intersection removal, orient-driven point duplication).
// Mass Properties must report on the mesh's ACTUAL as-imported state, not a
// repaired copy of it - a real, reported bug class this file used to get
// wrong in a different way (see computeMeshGeometry()'s doc comment). Kept
// private to this .cpp - MeshProperties.h's callers have no reason to know
// CGAL types exist here.
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h> // merge_duplicate_points_in_polygon_soup() - see its call site below
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/measure.h> // face_area() - used by selfIntersectingAreaRatio() below
#include <QDebug>
#include "SelfIntersectionContact.h"
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h> // orient_polygon_soup() - non-manifold vertex duplication fallback
#include <CGAL/boost/graph/helpers.h> // CGAL::is_closed() - NOT a Polygon_mesh_processing:: function

#include <iterator>
#include <unordered_set>
#include <utility>

namespace
{
	using PropsKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
	using PropsPoint3 = PropsKernel::Point_3;
	using PropsMesh   = CGAL::Surface_mesh<PropsPoint3>;

	// A self-intersection this small a fraction of the mesh's OWN surface area is treated as a mesher artifact (a
	// hairline crossing at a curved seam - a cone/fillet junction, a split ring) rather than a real defect: it is
	// scale-free (the same threshold suits a small washer and a large crank-case alike, since both the numerator and
	// denominator are the mesh's own area) and resolution-independent (unlike a triangle-COUNT ratio, it does not
	// shift if the part happens to be tessellated more finely). Found empirically on two real cases: DIN 128 spring
	// washers (~0.3-0.4% of area) and an engine crank-case/crank-halves import (0.09-0.37%) - see
	// project_step_import_watertight_fixes.md.
	constexpr double kMinorSelfIntersectionAreaRatio = 0.01; // 1% of the mesh's own surface area

	// The fraction of `mesh`'s surface area covered by faces CGAL::Polygon_mesh_processing::self_intersections()
	// reports as part of a crossing pair. Only called once does_self_intersect() has already confirmed there is at
	// least one - self_intersections() itself has no precondition beyond a valid mesh, so it is always safe to call.
	double selfIntersectingAreaRatio(const PropsMesh& mesh)
	{
		std::vector<std::pair<PropsMesh::Face_index, PropsMesh::Face_index>> pairs;
		CGAL::Polygon_mesh_processing::self_intersections(mesh, std::back_inserter(pairs));
		if (pairs.empty())
		{
			return 1.0; // does_self_intersect() said yes but found nothing to enumerate - treat as unbounded, not minor
		}

		std::unordered_set<PropsMesh::Face_index> involved;
		for (const auto& pair : pairs)
		{
			// Surfaces that merely touch (e.g. the split vertices of a knife-edge pinch) are not crossings.
			const FacePairKind kind = classifyFacePair(mesh, pair.first, pair.second);
			if (isContactKind(kind))
			{
				continue;
			}
			involved.insert(pair.first);
			involved.insert(pair.second);
		}

		double totalArea = 0.0, involvedArea = 0.0;
		for (const PropsMesh::Face_index face : mesh.faces())
		{
			const double area = CGAL::to_double(CGAL::Polygon_mesh_processing::face_area(face, mesh));
			totalArea += area;
			if (involved.count(face))
				involvedArea += area;
		}
		return totalArea > 0.0 ? involvedArea / totalArea : 1.0;
	}
}

MeshTopologyCheckResult computeMeshTopology(const std::vector<float>& points, const std::vector<unsigned int>& indices, bool topologyRepaired)
{
	MeshTopologyCheckResult check;

	std::vector<PropsPoint3> soupPoints;
	soupPoints.reserve(points.size() / 3);
	for (size_t i = 0; i + 2 < points.size(); i += 3)
		soupPoints.emplace_back(points[i + 0], points[i + 1], points[i + 2]);

	std::vector<std::array<std::size_t, 3>> soupFaces;
	soupFaces.reserve(indices.size() / 3);
	bool indicesInBounds = true;
	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		const unsigned int a = indices[i + 0], b = indices[i + 1], c = indices[i + 2];
		if (a >= soupPoints.size() || b >= soupPoints.size() || c >= soupPoints.size())
		{
			indicesInBounds = false;
			break;
		}
		soupFaces.push_back({ a, b, c });
	}

	// Weld exact-coincident duplicate points BEFORE the topology check below -
	// see computeMeshGeometry()'s own doc comment for why this is a
	// topology-identity fix, not a geometry repair, and is scoped to this
	// local soup copy only.
	CGAL::Polygon_mesh_processing::merge_duplicate_points_in_polygon_soup(soupPoints, soupFaces);

	if (!indicesInBounds)
	{
		check.unavailableReason = MeshPropertyUnavailableReason::InvalidIndices;
		return check;
	}

	// A soup that fails this specifically (as opposed to the out-of-bounds-index case above) most
	// commonly means a genuinely non-manifold edge (shared by more than 2 faces) - a real, if
	// unusual, CAD topology (confirmed via FreeCAD headless reproduction on a real model,
	// MBB Gehause Rohteil.step: shape.isValid() true at the OCC/BRep level, one edge legitimately
	// shared by 4 faces - a rib/wall feature pinching to a knife-edge). An attempted automatic
	// repair (splitting the offending edge) was tried and reverted - CGAL has no ready-made soup-
	// level tool for this, and a from-scratch fan-walk implementation, though reasoned through
	// carefully and matching the same principle CGAL's own vertex-level duplicate_non_manifold_
	// vertices() uses, still did not produce a valid soup in practice for unknown reasons that
	// couldn't be diagnosed further without compiler/debugger access. Reporting this reason
	// accurately (instead of the generic InvalidIndices) is the extent of the fix for now.
	if (!CGAL::Polygon_mesh_processing::is_polygon_soup_a_polygon_mesh(soupFaces))
	{
		// The weld above also merges the vertices Repair Mesh deliberately split (CGAL's
		// orient_polygon_soup()/duplicate_non_manifold_vertices()), since a split vertex is
		// indistinguishable from a normal-seam duplicate by position - which would make Repair
		// Mesh's output rejected here for the very defect it just fixed. Apply the same vertex
		// duplication (topology only, never moves a point or drops a face) to the local welded
		// soup, and reject only if that still can't produce a valid polygon mesh.
		const size_t faceCountBefore = soupFaces.size();
		if (topologyRepaired)
			CGAL::Polygon_mesh_processing::orient_polygon_soup(soupPoints, soupFaces);
		if (!topologyRepaired || soupFaces.size() != faceCountBefore
			|| !CGAL::Polygon_mesh_processing::is_polygon_soup_a_polygon_mesh(soupFaces))
		{
			check.unavailableReason = MeshPropertyUnavailableReason::NonManifoldTopology;
			return check;
		}
	}

	PropsMesh cgalMesh;
	CGAL::Polygon_mesh_processing::polygon_soup_to_polygon_mesh(soupPoints, soupFaces, cgalMesh);

	if (!CGAL::is_closed(cgalMesh))
	{
		check.unavailableReason = MeshPropertyUnavailableReason::OpenBoundary;
	}
	else if (CGAL::Polygon_mesh_processing::does_self_intersect(cgalMesh))
	{
		// does_bound_a_volume() is undefined behavior on self-intersecting input, so it is never reached from here -
		// a minor crossing (see kMinorSelfIntersectionAreaRatio's doc comment) is accepted on the strength of
		// is_closed() plus the divergence-theorem sum in computeMeshGeometry() below, which is well-defined on ANY
		// closed, consistently-wound mesh and is off by at most about this same small area fraction. A self-
		// intersection this small is very unlikely to coincide with the mesh being wound inconsistently overall - that
		// would ordinarily show up as a large intersecting area (inside-out faces overlapping broadly), which fails
		// this same ratio check and falls through to outright rejection below, same as before this tolerance existed.
		if (selfIntersectingAreaRatio(cgalMesh) <= kMinorSelfIntersectionAreaRatio)
		{
			check.hasValidVolume = true;
			check.isApproximate = true;
		}
		else
		{
			check.unavailableReason = MeshPropertyUnavailableReason::SelfIntersecting;
		}
	}
	else if (!CGAL::Polygon_mesh_processing::does_bound_a_volume(cgalMesh))
	{
		check.unavailableReason = MeshPropertyUnavailableReason::UnresolvedOrientation;
	}
	else
	{
		check.hasValidVolume = true;
	}

	return check;
}

QString describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason reason)
{
	switch (reason)
	{
	case MeshPropertyUnavailableReason::None:                  return QString();
	case MeshPropertyUnavailableReason::InvalidIndices:         return QObject::tr("invalid geometry");
	case MeshPropertyUnavailableReason::NonManifoldTopology:    return QObject::tr("non-manifold topology");
	case MeshPropertyUnavailableReason::OpenBoundary:           return QObject::tr("open surface");
	case MeshPropertyUnavailableReason::SelfIntersecting:       return QObject::tr("self-intersecting");
	case MeshPropertyUnavailableReason::UnresolvedOrientation:  return QObject::tr("unresolved orientation");
	case MeshPropertyUnavailableReason::MissingDensity:         return QObject::tr("no density assigned");
	}
	return QObject::tr("unknown");
}

bool meshHasMass(bool hasValidVolume, float density)
{
	return hasValidVolume && density >= 0.0f;
}

float computeMeshWeight(double volumeInCubicMm, float density)
{
	// 1 m^3 = 1e9 mm^3.
	return static_cast<float>(density * volumeInCubicMm / 1.0e9);
}

namespace
{
	// Result of splitting a mesh into connected pieces and classifying each one.
	struct PieceSplit
	{
		bool ok = false;
		std::vector<unsigned char> faceIsSolid; // per ORIGINAL face: 1 = belongs to a valid solid piece, 0 = shell
		int solidPieces = 0;
		int shellPieces = 0;
		bool anyApproximate = false; // at least one solid piece was accepted despite a minor self-intersection
	};

	size_t findRoot(std::vector<size_t>& parent, size_t x)
	{
		while (parent[x] != x)
		{
			parent[x] = parent[parent[x]]; // path halving
			x = parent[x];
		}
		return x;
	}

	// Splits the mesh into connected pieces (faces sharing a welded vertex) and runs the same closed ->
	// non-self-intersecting -> bounds-a-volume predicate sequence as computeMeshTopology() on each piece on its
	// own. A piece that fails any step - or that isn't even a valid polygon mesh (non-manifold sheets) - is a
	// SHELL: still perfectly good for area, just without an enclosed volume.
	PieceSplit classifyPieces(const std::vector<float>& points, const std::vector<unsigned int>& indices)
	{
		PieceSplit split;
		const size_t faceCount = indices.size() / 3;

		std::vector<PropsPoint3> soupPoints;
		soupPoints.reserve(points.size() / 3);
		for (size_t i = 0; i + 2 < points.size(); i += 3)
			soupPoints.emplace_back(points[i + 0], points[i + 1], points[i + 2]);

		std::vector<std::array<std::size_t, 3>> soupFaces;
		soupFaces.reserve(faceCount);
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const unsigned int a = indices[i + 0], b = indices[i + 1], c = indices[i + 2];
			if (a >= soupPoints.size() || b >= soupPoints.size() || c >= soupPoints.size())
				return split; // out-of-bounds index: no reliable classification, caller falls back to whole-mesh behaviour
			soupFaces.push_back({ a, b, c });
		}

		// Same seam-welding as computeMeshTopology(): a per-face-normal-split export would otherwise look like a
		// pile of disconnected triangles.
		CGAL::Polygon_mesh_processing::merge_duplicate_points_in_polygon_soup(soupPoints, soupFaces);
		if (soupFaces.size() != faceCount)
			return split; // welding must stay 1:1 with the original faces for the per-face result below

		std::vector<size_t> parent(soupPoints.size());
		std::iota(parent.begin(), parent.end(), size_t(0));
		for (const auto& f : soupFaces)
		{
			const size_t ra = findRoot(parent, f[0]);
			const size_t rb = findRoot(parent, f[1]);
			const size_t rc = findRoot(parent, f[2]);
			parent[rb] = ra;
			parent[findRoot(parent, rc)] = ra;
		}

		std::unordered_map<size_t, std::vector<size_t>> facesOfPiece;
		for (size_t f = 0; f < faceCount; ++f)
			facesOfPiece[findRoot(parent, soupFaces[f][0])].push_back(f);

		split.faceIsSolid.assign(faceCount, 0);
		for (const auto& piece : facesOfPiece)
		{
			std::unordered_map<size_t, size_t> localIndex;
			std::vector<PropsPoint3> localPoints;
			std::vector<std::array<std::size_t, 3>> localFaces;
			localFaces.reserve(piece.second.size());
			for (size_t f : piece.second)
			{
				std::array<std::size_t, 3> lf{};
				for (int k = 0; k < 3; ++k)
				{
					const size_t global = soupFaces[f][k];
					auto it = localIndex.find(global);
					if (it == localIndex.end())
					{
						it = localIndex.emplace(global, localPoints.size()).first;
						localPoints.push_back(soupPoints[global]);
					}
					lf[k] = it->second;
				}
				localFaces.push_back(lf);
			}

			bool solid = false;
			if (CGAL::Polygon_mesh_processing::is_polygon_soup_a_polygon_mesh(localFaces))
			{
				PropsMesh pieceMesh;
				CGAL::Polygon_mesh_processing::polygon_soup_to_polygon_mesh(localPoints, localFaces, pieceMesh);
				// Short-circuit order matters: does_bound_a_volume() is undefined behaviour on non-closed or
				// self-intersecting input (see computeMeshGeometry()'s note). A minor self-intersection (see
				// kMinorSelfIntersectionAreaRatio's doc comment) is accepted the same way computeMeshTopology()
				// accepts one on the whole mesh, without calling does_bound_a_volume().
				if (CGAL::is_closed(pieceMesh))
				{
					if (!CGAL::Polygon_mesh_processing::does_self_intersect(pieceMesh))
					{
						solid = CGAL::Polygon_mesh_processing::does_bound_a_volume(pieceMesh);
					}
					else if (selfIntersectingAreaRatio(pieceMesh) <= kMinorSelfIntersectionAreaRatio)
					{
						solid = true;
						split.anyApproximate = true;
					}
				}
			}

			if (solid)
			{
				++split.solidPieces;
				for (size_t f : piece.second)
					split.faceIsSolid[f] = 1;
			}
			else
			{
				++split.shellPieces;
			}
		}

		split.ok = true;
		return split;
	}
}

MeshGeometryComputeResult computeMeshGeometry(const std::vector<float>& points, const std::vector<unsigned int>& indices, const BoundingBox& boundingBox, bool topologyRepaired)
{
	MeshGeometryComputeResult result;
	const size_t offset = 3; // each index points to 3 floats

	if (indices.empty() || indices.size() % 3 != 0)
	{
		result.volumeUnavailableReason = MeshPropertyUnavailableReason::InvalidIndices;
		return result;
	}

	// ------------------------------------------------------------------
	// Step 1: establish volume validity via CGAL's own topology predicates,
	// in this specific order - NOT a numeric near-zero-magnitude threshold
	// (unreliable both ways: a tiny valid closed solid can sum near zero,
	// and an open surface can accidentally sum to something substantial).
	// does_bound_a_volume() is documented UNDEFINED BEHAVIOR on non-closed
	// or self-intersecting input (CGAL_precondition, which is compiled OUT
	// in release builds - so this app must enforce the ordering itself, not
	// rely on CGAL's own assert to catch a violation): is_closed, THEN
	// does_self_intersect, THEN (only if both pass) does_bound_a_volume.
	//
	// Whole mesh first (the common, cheap case). Only when that fails is
	// the mesh split into connected pieces and each classified on its own,
	// so one open sheet - or one overlapping pair of solids - in an
	// otherwise sound import no longer makes the ENTIRE mesh unusable.
	// ------------------------------------------------------------------
	{
		const MeshTopologyCheckResult topology = computeMeshTopology(points, indices, topologyRepaired);
		result.hasValidVolume = topology.hasValidVolume;
		result.volumeUnavailableReason = topology.unavailableReason;
		result.isApproximateVolume = topology.isApproximate;
	}

	// Empty = "no per-piece information": with hasValidVolume every face is solid, without it none is.
	std::vector<unsigned char> faceIsSolid;
	bool hasPieceInfo = false;
	if (result.hasValidVolume)
	{
		result.solidPieceCount = 1; // at least one; the exact count isn't needed on this path
	}
	else
	{
		const PieceSplit split = classifyPieces(points, indices);
		if (split.ok)
		{
			hasPieceInfo = true;
			faceIsSolid = split.faceIsSolid;
			result.solidPieceCount = split.solidPieces;
			result.shellPieceCount = split.shellPieces;
			// Every piece individually solid (e.g. two separate solids that merely overlap, which failed the
			// whole-mesh self-intersection test) is a usable volume; the whole-mesh reason no longer applies.
			if (split.shellPieces == 0 && split.solidPieces > 0)
			{
				result.hasValidVolume = true;
				result.volumeUnavailableReason = MeshPropertyUnavailableReason::None;
				result.isApproximateVolume = split.anyApproximate;
			}
		}
	}

	// ------------------------------------------------------------------
	// Step 2: accumulate surface area (always), the signed volume + volume-
	// weighted centroid of the SOLID pieces via the divergence theorem, and
	// the area + area-weighted centroid of the SHELL pieces. Double
	// precision, not float (float accumulators lose real precision on
	// CAD-sized meshes), and summed relative to the mesh's own bounding-box
	// center rather than the world origin - a known precision trap when
	// real CAD coordinates are far from (0,0,0). Result is in the mesh's OWN
	// native coordinate units - see this function's own doc comment in the
	// header on why unit-scaling happens in the caller, not here.
	// ------------------------------------------------------------------
	const QVector3D refOrigin(
		static_cast<float>((boundingBox.xMin() + boundingBox.xMax()) / 2.0),
		static_cast<float>((boundingBox.yMin() + boundingBox.yMax()) / 2.0),
		static_cast<float>((boundingBox.zMin() + boundingBox.zMax()) / 2.0));

	double surfaceAreaAccum = 0.0;
	double volumeAccum = 0.0;
	double xCen = 0.0, yCen = 0.0, zCen = 0.0;
	double shellAreaAccum = 0.0;
	double shellX = 0.0, shellY = 0.0, shellZ = 0.0;

	try
	{
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const unsigned int ia = indices[i + 0], ib = indices[i + 1], ic = indices[i + 2];
			const size_t pa = offset * ia, pb = offset * ib, pc = offset * ic;

			const QVector3D p1(points.at(pa + 0) - refOrigin.x(), points.at(pa + 1) - refOrigin.y(), points.at(pa + 2) - refOrigin.z());
			const QVector3D p2(points.at(pb + 0) - refOrigin.x(), points.at(pb + 1) - refOrigin.y(), points.at(pb + 2) - refOrigin.z());
			const QVector3D p3(points.at(pc + 0) - refOrigin.x(), points.at(pc + 1) - refOrigin.y(), points.at(pc + 2) - refOrigin.z());

			const double area = static_cast<double>(QVector3D::crossProduct(p2 - p1, p3 - p1).length()) * 0.5;
			surfaceAreaAccum += area;

			const size_t face = i / 3;
			const bool solidFace = hasPieceInfo ? (faceIsSolid[face] != 0) : result.hasValidVolume;
			if (solidFace)
			{
				const double triVolume = static_cast<double>(QVector3D::dotProduct(p1, QVector3D::crossProduct(p2, p3))) / 6.0;
				volumeAccum += triVolume;
				xCen += ((static_cast<double>(p1.x()) + p2.x() + p3.x()) / 4.0) * triVolume;
				yCen += ((static_cast<double>(p1.y()) + p2.y() + p3.y()) / 4.0) * triVolume;
				zCen += ((static_cast<double>(p1.z()) + p2.z() + p3.z()) / 4.0) * triVolume;
			}
			else if (hasPieceInfo)
			{
				shellAreaAccum += area;
				shellX += ((static_cast<double>(p1.x()) + p2.x() + p3.x()) / 3.0) * area;
				shellY += ((static_cast<double>(p1.y()) + p2.y() + p3.y()) / 3.0) * area;
				shellZ += ((static_cast<double>(p1.z()) + p2.z() + p3.z()) / 3.0) * area;
			}
		}
	}
	catch (const std::exception& ex)
	{
		// Reject outright rather than partially proceed - an incomplete
		// result must never be returned as if it were valid.
		std::cout << "Exception raised in computeMeshGeometry\n" << ex.what() << std::endl;
		return MeshGeometryComputeResult{ false, 0.0f, false, MeshPropertyUnavailableReason::InvalidIndices, false, 0.0, QVector3D(0, 0, 0) };
	}

	result.hasValidGeometry = true;
	result.surfaceArea = static_cast<float>(surfaceAreaAccum);

	if (result.solidPieceCount > 0 && std::fabs(volumeAccum) > 0.0)
	{
		// Divide the signed moment accumulators by the SIGNED volume - this
		// alone produces the correct centroid, with no abs() involved
		// anywhere in the division. fabs() below is applied ONLY to the
		// volume value being reported as a magnitude, never to a centroid
		// coordinate: a centroid legitimately has negative coordinates
		// (e.g. a part centered left of the mesh's own bbox center), and
		// that must be preserved, not clamped positive.
		result.centerOfMass = QVector3D(
			static_cast<float>(xCen / volumeAccum),
			static_cast<float>(yCen / volumeAccum),
			static_cast<float>(zCen / volumeAccum)) + refOrigin;
		result.volume = std::fabs(volumeAccum);
	}

	if (shellAreaAccum > 0.0)
	{
		result.shellSurfaceArea = shellAreaAccum;
		result.shellCentroid = QVector3D(
			static_cast<float>(shellX / shellAreaAccum),
			static_cast<float>(shellY / shellAreaAccum),
			static_cast<float>(shellZ / shellAreaAccum)) + refOrigin;
	}

	return result;
}

MeshVolumeSummary summarizeMeshVolume(const MeshGeometryComputeResult& geometry, double lengthScale, float shellThicknessMm)
{
	MeshVolumeSummary summary;
	const double scale2 = lengthScale * lengthScale;
	const double scale3 = scale2 * lengthScale;
	const double solidVolume = geometry.solidPieceCount > 0 ? geometry.volume * scale3 : 0.0;
	const QVector3D solidCom = geometry.centerOfMass * static_cast<float>(lengthScale);

	if (geometry.hasValidVolume)
	{
		summary.valid = true;
		summary.volume = solidVolume;
		summary.centerOfMass = solidCom;
		summary.approximate = geometry.isApproximateVolume;
		return summary;
	}

	// Not a plain solid. Shell pieces can still be used - but only with a real thickness supplied.
	const bool hasShellPieces = geometry.hasValidGeometry && geometry.shellPieceCount > 0 && geometry.shellSurfaceArea > 0.0;
	if (!hasShellPieces)
	{
		summary.reason = geometry.volumeUnavailableReason;
		return summary;
	}
	if (!(shellThicknessMm > 0.0f))
	{
		summary.reason = geometry.volumeUnavailableReason;
		summary.shellCapable = true; // a shell thickness on the material would make this mesh usable
		return summary;
	}

	const double shellVolume = geometry.shellSurfaceArea * scale2 * static_cast<double>(shellThicknessMm);
	const double total = solidVolume + shellVolume;
	summary.valid = true;
	summary.shellPieceCount = geometry.shellPieceCount;
	summary.shellVolume = shellVolume;
	summary.volume = total;
	if (total > 0.0)
	{
		const QVector3D shellCom = geometry.shellCentroid * static_cast<float>(lengthScale);
		summary.centerOfMass = (solidCom * static_cast<float>(solidVolume) + shellCom * static_cast<float>(shellVolume))
			/ static_cast<float>(total);
	}
	return summary;
}
