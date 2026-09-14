#include "MeshProperties.h"
#include "SceneMesh.h"
#include <iostream>
#include <array>
#include <cmath>

// CGAL is used here ONLY to answer "is this raw triangle soup actually a
// closed, non-self-intersecting solid" - deliberately NOT MeshRepair.h's
// repairSoupToMesh() pipeline, which can (and by design does) alter
// geometry (self-intersection removal, orient-driven point duplication).
// Mass Properties must report on the mesh's ACTUAL as-imported state, not a
// repaired copy of it - a real, reported bug class this file used to get
// wrong in a different way (see calculateSurfaceAreaAndVolume()'s doc
// comment). Kept private to this .cpp - MeshProperties.h's callers
// (ModelViewer.cpp et al.) have no reason to know CGAL types exist here.
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

MeshProperties::MeshProperties(SceneMesh* mesh, QObject* parent) : QObject(parent), _mesh(mesh)
{
	_meshPoints = _mesh->getTrsfPoints();
	calculateSurfaceAreaAndVolume();
}

SceneMesh* MeshProperties::mesh() const
{
	return _mesh;
}

void MeshProperties::setMesh(SceneMesh* mesh)
{
	_mesh = mesh;
	_meshPoints.clear();
	_meshPoints = _mesh->getTrsfPoints();
	calculateSurfaceAreaAndVolume();
}

std::vector<float> MeshProperties::meshPoints() const
{
	return _meshPoints;
}

bool MeshProperties::hasValidGeometry() const
{
	return _hasValidGeometry;
}

float MeshProperties::surfaceArea() const
{
	return _surfaceArea;
}

bool MeshProperties::hasValidVolume() const
{
	return _hasValidVolume;
}

float MeshProperties::volume() const
{
	return static_cast<float>(_volume);
}

MeshPropertyUnavailableReason MeshProperties::volumeUnavailableReason() const
{
	return _volumeUnavailableReason;
}

void MeshProperties::setDensity(const float& density)
{
	_density = density;
}

float MeshProperties::density() const
{
	return _density;
}

bool MeshProperties::hasMass() const
{
	return _hasValidVolume && _density >= 0.0f;
}

float MeshProperties::weight() const
{
	// Computed live, every call - never a cached field. The previous version
	// cached this in a private _weight member that setDensity() never
	// refreshed, so calling setDensity() after construction (exactly what a
	// weight-rollup feature needs to do, to apply a mesh's real material
	// density) silently kept reporting mass computed from whatever density
	// was set at construction time - a real, confirmed bug.
	if (!hasMass())
		return 0.0f;
	// _volume is in mm^3 (see calculateSurfaceAreaAndVolume()'s doc comment
	// on units), _density in kg/m^3 - 1 m^3 = 1e9 mm^3.
	return static_cast<float>(_density * _volume / 1.0e9);
}

QVector3D MeshProperties::centerOfMass() const
{
	return _centerOfMass;
}

BoundingBox MeshProperties::boundingBox() const
{
	return _mesh->getBoundingBox();
}

void MeshProperties::calculateSurfaceAreaAndVolume()
{
	_hasValidGeometry = false;
	_surfaceArea = 0.0f;
	_hasValidVolume = false;
	_volumeUnavailableReason = MeshPropertyUnavailableReason::None;
	_volume = 0.0;
	_centerOfMass = QVector3D(0, 0, 0);

	const std::vector<unsigned int> indices = _mesh->getIndices();
	const size_t offset = 3; // each index points to 3 floats

	if (indices.empty() || indices.size() % 3 != 0)
	{
		_volumeUnavailableReason = MeshPropertyUnavailableReason::InvalidIndices;
		return;
	}

	// ------------------------------------------------------------------
	// Step 1: establish volume validity via CGAL's own topology predicates,
	// in this specific order - NOT a numeric near-zero-magnitude threshold
	// (the previous version's approach, which is unreliable both ways: a
	// tiny valid closed solid can sum near zero, and an open surface can
	// accidentally sum to something substantial). does_bound_a_volume() is
	// documented UNDEFINED BEHAVIOR on non-closed or self-intersecting
	// input (CGAL_precondition, which is compiled OUT in release builds -
	// so this app must enforce the ordering itself, not rely on CGAL's own
	// assert to catch a violation): is_closed, THEN does_self_intersect,
	// THEN (only if both pass) does_bound_a_volume.
	// ------------------------------------------------------------------
	{
		std::vector<PropsPoint3> soupPoints;
		soupPoints.reserve(_meshPoints.size() / offset);
		for (size_t i = 0; i + 2 < _meshPoints.size(); i += offset)
			soupPoints.emplace_back(_meshPoints[i + 0], _meshPoints[i + 1], _meshPoints[i + 2]);

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

		// Weld exact-coincident duplicate points BEFORE the topology check
		// below - purely a topology-identity fix, not a geometry repair:
		// a render mesh routinely carries several distinct point-buffer
		// entries at the SAME position (one per triangle corner, split so
		// each corner can carry its own normal/UV - e.g. a conventional
		// 24-vertex cube export, 3 duplicated corners per face for flat
		// shading). Left unwelded, CGAL sees those as topologically
		// UNRELATED vertices, so no two triangles ever share a real edge in
		// its eyes - a perfectly closed, watertight solid then fails
		// CGAL::is_closed() and gets reported as an open boundary, which is
		// wrong. merge_duplicate_points_in_polygon_soup() only merges points
		// at EXACT coordinate equality and remaps face indices accordingly -
		// it does not move, remove, or alter any actual geometry the way
		// MeshRepair.h's pipeline can, so this doesn't reopen the "must
		// report on the mesh's ACTUAL as-imported state" concern in this
		// function's own doc comment above. Scoped to this local soupPoints/
		// soupFaces copy only - the surface-area/volume accumulation loop
		// below reads straight from _meshPoints/indices, unaffected.
		CGAL::Polygon_mesh_processing::merge_duplicate_points_in_polygon_soup(soupPoints, soupFaces);

		if (!indicesInBounds || !CGAL::Polygon_mesh_processing::is_polygon_soup_a_polygon_mesh(soupFaces))
		{
			_volumeUnavailableReason = MeshPropertyUnavailableReason::InvalidIndices;
		}
		else
		{
			PropsMesh cgalMesh;
			CGAL::Polygon_mesh_processing::polygon_soup_to_polygon_mesh(soupPoints, soupFaces, cgalMesh);

			if (!CGAL::is_closed(cgalMesh))
			{
				_volumeUnavailableReason = MeshPropertyUnavailableReason::OpenBoundary;
			}
			else if (CGAL::Polygon_mesh_processing::does_self_intersect(cgalMesh))
			{
				_volumeUnavailableReason = MeshPropertyUnavailableReason::SelfIntersecting;
			}
			else if (!CGAL::Polygon_mesh_processing::does_bound_a_volume(cgalMesh))
			{
				_volumeUnavailableReason = MeshPropertyUnavailableReason::UnresolvedOrientation;
			}
			else
			{
				_hasValidVolume = true;
			}
		}
	}

	// ------------------------------------------------------------------
	// Step 2: accumulate surface area (always) and, only if step 1 passed,
	// signed volume + volume-weighted centroid, via the divergence theorem.
	// Double precision, not float (the previous float accumulators lost
	// real precision on CAD-sized meshes), and summed relative to the
	// mesh's own bounding-box center rather than the world origin - a known
	// precision trap when real CAD coordinates are far from (0,0,0).
	// Units: this app's coordinates are assumed millimetres end-to-end
	// (matches every other length-bearing UI value in this app - e.g. the
	// Environment panel's floor-offset/repeat controls carry no separate
	// unit toggle either); weight() divides by 1e9 to convert mm^3 -> m^3
	// against a kg/m^3 density accordingly. A real per-document/per-import
	// unit override (for files that carry different unit metadata) is a
	// known follow-up, not solved by this pass - see the Mass Properties
	// plan's "Units" note.
	// ------------------------------------------------------------------
	const BoundingBox bbox = _mesh->getBoundingBox();
	const QVector3D refOrigin(
		static_cast<float>((bbox.xMin() + bbox.xMax()) / 2.0),
		static_cast<float>((bbox.yMin() + bbox.yMax()) / 2.0),
		static_cast<float>((bbox.zMin() + bbox.zMax()) / 2.0));

	double surfaceAreaAccum = 0.0;
	double volumeAccum = 0.0;
	double xCen = 0.0, yCen = 0.0, zCen = 0.0;

	try
	{
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const unsigned int ia = indices[i + 0], ib = indices[i + 1], ic = indices[i + 2];
			const size_t pa = offset * ia, pb = offset * ib, pc = offset * ic;

			const QVector3D p1(_meshPoints.at(pa + 0) - refOrigin.x(), _meshPoints.at(pa + 1) - refOrigin.y(), _meshPoints.at(pa + 2) - refOrigin.z());
			const QVector3D p2(_meshPoints.at(pb + 0) - refOrigin.x(), _meshPoints.at(pb + 1) - refOrigin.y(), _meshPoints.at(pb + 2) - refOrigin.z());
			const QVector3D p3(_meshPoints.at(pc + 0) - refOrigin.x(), _meshPoints.at(pc + 1) - refOrigin.y(), _meshPoints.at(pc + 2) - refOrigin.z());

			const double area = static_cast<double>(QVector3D::crossProduct(p2 - p1, p3 - p1).length()) * 0.5;
			surfaceAreaAccum += area;

			if (_hasValidVolume)
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
		// Reject outright rather than partially proceed - the previous
		// version logged and fell through with whatever had accumulated so
		// far, silently returning an incomplete result as if it were valid.
		std::cout << "Exception raised in MeshProperties::calculateSurfaceAreaAndVolume\n" << ex.what() << std::endl;
		_hasValidGeometry = false;
		_hasValidVolume = false;
		_volumeUnavailableReason = MeshPropertyUnavailableReason::InvalidIndices;
		_surfaceArea = 0.0f;
		_volume = 0.0;
		_centerOfMass = QVector3D(0, 0, 0);
		return;
	}

	_hasValidGeometry = true;
	_surfaceArea = static_cast<float>(surfaceAreaAccum);

	if (_hasValidVolume)
	{
		// Divide the signed moment accumulators by the SIGNED volume - this
		// alone produces the correct centroid, with no abs() involved
		// anywhere in the division. fabs() below is applied ONLY to the
		// volume value being reported as a magnitude, never to a centroid
		// coordinate: a centroid legitimately has negative coordinates
		// (e.g. a part centered left of the mesh's own bbox center), and
		// that must be preserved, not clamped positive. The previous
		// version took fabs(volume) BEFORE this division, which flips the
		// sign of the resulting centroid on any reversed-winding mesh - a
		// real, confirmed bug.
		_centerOfMass = QVector3D(
			static_cast<float>(xCen / volumeAccum),
			static_cast<float>(yCen / volumeAccum),
			static_cast<float>(zCen / volumeAccum)) + refOrigin;
		_volume = std::fabs(volumeAccum);
	}
}
