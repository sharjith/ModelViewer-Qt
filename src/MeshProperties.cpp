#include "MeshProperties.h"
#include <QObject>
#include <iostream>
#include <array>
#include <cmath>

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
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/boost/graph/helpers.h> // CGAL::is_closed() - NOT a Polygon_mesh_processing:: function

namespace
{
	using PropsKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
	using PropsPoint3 = PropsKernel::Point_3;
	using PropsMesh   = CGAL::Surface_mesh<PropsPoint3>;
}

MeshTopologyCheckResult computeMeshTopology(const std::vector<float>& points, const std::vector<unsigned int>& indices)
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

	if (!indicesInBounds || !CGAL::Polygon_mesh_processing::is_polygon_soup_a_polygon_mesh(soupFaces))
	{
		check.unavailableReason = MeshPropertyUnavailableReason::InvalidIndices;
		return check;
	}

	PropsMesh cgalMesh;
	CGAL::Polygon_mesh_processing::polygon_soup_to_polygon_mesh(soupPoints, soupFaces, cgalMesh);

	if (!CGAL::is_closed(cgalMesh))
	{
		check.unavailableReason = MeshPropertyUnavailableReason::OpenBoundary;
	}
	else if (CGAL::Polygon_mesh_processing::does_self_intersect(cgalMesh))
	{
		check.unavailableReason = MeshPropertyUnavailableReason::SelfIntersecting;
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

MeshGeometryComputeResult computeMeshGeometry(const std::vector<float>& points, const std::vector<unsigned int>& indices, const BoundingBox& boundingBox)
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
	// ------------------------------------------------------------------
	{
		const MeshTopologyCheckResult topology = computeMeshTopology(points, indices);
		result.hasValidVolume = topology.hasValidVolume;
		result.volumeUnavailableReason = topology.unavailableReason;
	}

	// ------------------------------------------------------------------
	// Step 2: accumulate surface area (always) and, only if step 1 passed,
	// signed volume + volume-weighted centroid, via the divergence theorem.
	// Double precision, not float (float accumulators lose real precision on
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

			if (result.hasValidVolume)
			{
				const double triVolume = static_cast<double>(QVector3D::dotProduct(p1, QVector3D::crossProduct(p2, p3))) / 6.0;
				volumeAccum += triVolume;
				xCen += ((static_cast<double>(p1.x()) + p2.x() + p3.x()) / 4.0) * triVolume;
				yCen += ((static_cast<double>(p1.y()) + p2.y() + p3.y()) / 4.0) * triVolume;
				zCen += ((static_cast<double>(p1.z()) + p2.z() + p3.z()) / 4.0) * triVolume;
			}
		}
	}
	catch (const std::exception& ex)
	{
		// Reject outright rather than partially proceed - an incomplete
		// result must never be returned as if it were valid.
		std::cout << "Exception raised in computeMeshGeometry\n" << ex.what() << std::endl;
		return MeshGeometryComputeResult{ false, 0.0f, false, MeshPropertyUnavailableReason::InvalidIndices, 0.0, QVector3D(0, 0, 0) };
	}

	result.hasValidGeometry = true;
	result.surfaceArea = static_cast<float>(surfaceAreaAccum);

	if (result.hasValidVolume)
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

	return result;
}
