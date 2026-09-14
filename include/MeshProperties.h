#pragma once

#include <QString>
#include "BoundingBox.h"

// ---------------------------------------------------------------------------
// Pure, background-thread-safe mesh property computations (surface area,
// volume, centroid, topology validity, mass) - no class, no SceneMesh*, no
// live mesh state. Originally a QObject-derived MeshProperties class held
// this logic tied to a live mesh; it was replaced by these free functions
// once MassPropertiesDialog moved to AnalysisComputeSession's background
// worker (its only caller), which needs a plain point/index snapshot rather
// than a live SceneMesh* to run off the UI thread - see AnalysisMeshSnapshot.
// ---------------------------------------------------------------------------

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

// Result of the closed/non-self-intersecting/bounds-a-volume topology
// sequence - see computeMeshTopology()'s own doc comment for the exact
// ordering and why it matters.
struct MeshTopologyCheckResult
{
	bool hasValidVolume = false;
	MeshPropertyUnavailableReason unavailableReason = MeshPropertyUnavailableReason::None;
};

// The CGAL topology predicate sequence (weld exact-coincident duplicate
// points, then is_closed -> does_self_intersect -> (only if both pass)
// does_bound_a_volume, in that exact order - does_bound_a_volume() is
// documented undefined behavior on non-closed/self-intersecting input) that
// decides whether a raw point/index soup has a well-defined enclosed volume
// at all.
MeshTopologyCheckResult computeMeshTopology(const std::vector<float>& points, const std::vector<unsigned int>& indices);

// Surface area (always), and (only if computeMeshTopology() found a valid
// volume) signed volume + volume-weighted centroid, all in the mesh's OWN
// native coordinate units (i.e. NOT yet scaled by any resolved real-world
// unit - see LengthUnits.h's resolveEffectiveImportUnit(); the caller
// multiplies surfaceArea by scale^2, volume by scale^3, and centerOfMass by
// scale^1 afterward, once it has resolved which unit this mesh's coordinates
// are actually in).
struct MeshGeometryComputeResult
{
	bool hasValidGeometry = false;
	float surfaceArea = 0.0f;
	bool hasValidVolume = false;
	MeshPropertyUnavailableReason volumeUnavailableReason = MeshPropertyUnavailableReason::None;
	double volume = 0.0;
	QVector3D centerOfMass;
};

// The divergence-theorem surface-area/volume/centroid integral - double-
// precision accumulators (CAD-sized meshes lose real precision in float),
// summed relative to the mesh's own bounding-box center rather than the
// world origin (a known precision trap when real CAD coordinates are far
// from the origin). The result's centroid is divided by the SIGNED volume,
// with fabs() applied only to the volume value reported as a magnitude,
// never to the centroid itself - a centroid legitimately has negative
// coordinates (e.g. a part centered left of the mesh's own bbox center).
MeshGeometryComputeResult computeMeshGeometry(const std::vector<float>& points, const std::vector<unsigned int>& indices, const BoundingBox& boundingBox);

// Mass validity/weight - one real implementation shared by every caller
// (MassPropertiesDialog's synchronous UI-thread total-accumulation and its
// AnalysisComputeSession-backed per-mesh computation both need the exact
// same two-line formula).
bool meshHasMass(bool hasValidVolume, float density);
float computeMeshWeight(double volumeInCubicMm, float density); // caller must have already checked meshHasMass(); density in kg/m^3
