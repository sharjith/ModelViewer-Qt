#pragma once


class SceneMesh;

#include <QObject>
#include <QString>
#include "BoundingBox.h"

class RenderableMesh;

// Why volume/mass/centroid are unavailable, surfaced to the UI instead of a
// bare "N/A" - see the Mass Properties feature's "every validity flag
// carries a reason" requirement. Surface area has no reason of its own: it's
// always computable from the raw triangle soup regardless of closure/
// orientation, independent of everything below.
enum class MeshPropertyUnavailableReason
{
	None,                  // value is valid - no reason needed
	InvalidIndices,        // raw soup fails CGAL::Polygon_mesh_processing::is_polygon_soup_a_polygon_mesh() -
	                       // degenerate/inconsistent faces, or the per-triangle scan itself threw
	OpenBoundary,          // mesh has boundary edges (CGAL::is_closed() false)
	SelfIntersecting,      // CGAL::Polygon_mesh_processing::does_self_intersect() true
	UnresolvedOrientation, // closed + non-self-intersecting but does_bound_a_volume() is still false
	MissingDensity         // volume/centroid ARE valid, but no density has been supplied - mass-only reason
};

// Single shared source of the user-facing reason text, so a reason string
// looks identical everywhere it's surfaced (Mass Properties dialog, and
// later the Surface Analysis / weight-rollup features reusing this same
// enum) rather than each call site inventing its own wording.
QString describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason reason);

class MeshProperties : public QObject
{
	Q_OBJECT
public:
	explicit MeshProperties(SceneMesh* mesh, QObject* parent = nullptr);

	SceneMesh* mesh() const;
	void setMesh(SceneMesh* mesh);

	std::vector<float> meshPoints() const;

	// Always valid whenever the raw per-triangle scan completes (see
	// hasValidGeometry()) - computable regardless of closure/self-
	// intersection/orientation, unlike everything below.
	bool hasValidGeometry() const;
	float surfaceArea() const; // only meaningful if hasValidGeometry()

	// Valid only when the mesh passes all three checks, in order: closed
	// (CGAL::is_closed), non-self-intersecting (PMP::does_self_intersect),
	// and bounds a volume (PMP::does_bound_a_volume) - see
	// calculateSurfaceAreaAndVolume()'s doc comment for why this exact order
	// matters (does_bound_a_volume() is undefined behavior on input that
	// fails the first two).
	bool hasValidVolume() const;
	float volume() const; // cubic mm - only meaningful if hasValidVolume()
	MeshPropertyUnavailableReason volumeUnavailableReason() const; // meaningful only if !hasValidVolume()

	// Geometric (uniform-density) centroid, derived from the same signed-
	// volume integral as volume() - same validity as volume(), NOT the same
	// thing as a real mass-weighted center of mass (that needs an actual
	// density, computed one level up wherever multiple MeshProperties are
	// combined for a multi-mesh selection).
	QVector3D centerOfMass() const; // only meaningful if hasValidVolume()

	// Density is deliberately NOT defaulted to any assumed value (e.g. the
	// old hardcoded 1000 kg/m^3 "water" placeholder) - a mesh has no known
	// mass at all until something (Material's real density, once wired in a
	// later step) explicitly supplies one. -1 is a sentinel meaning "not
	// supplied", never a physically valid density.
	void setDensity(const float& density);
	float density() const; // sentinel -1 if hasMass() is false - callers must check hasMass() first

	// True only when BOTH hasValidVolume() and a real (non-sentinel)
	// density have been supplied. weight() is a live COMPUTED value, not a
	// cached field - calling setDensity() again always immediately changes
	// what weight() returns, on the very next call, with nothing to go
	// stale (the old version cached this in a private field that setDensity()
	// never refreshed).
	bool hasMass() const;
	float weight() const; // kg - only meaningful if hasMass()

	BoundingBox boundingBox() const;

signals:

private:
	void calculateSurfaceAreaAndVolume();

private:
	SceneMesh* _mesh;
	std::vector<float> _meshPoints;

	bool _hasValidGeometry = false;
	float _surfaceArea = 0.0f;

	bool _hasValidVolume = false;
	MeshPropertyUnavailableReason _volumeUnavailableReason = MeshPropertyUnavailableReason::None;
	// Double precision, not float - see calculateSurfaceAreaAndVolume()'s
	// doc comment on why the old float accumulators lost real precision on
	// CAD-sized geometry.
	double _volume = 0.0;
	QVector3D _centerOfMass;

	float _density = -1.0f; // sentinel: "not supplied" - see setDensity()'s doc comment
};
