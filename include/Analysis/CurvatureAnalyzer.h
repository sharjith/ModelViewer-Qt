#pragma once

#include <atomic>
#include <vector>
#include <QString>
#include <QtGlobal>

class SceneMesh;

// ---------------------------------------------------------------------------
// CurvatureAnalyzer
//
// Mean curvature (CGAL's interpolated_corrected_curvatures), computed on a
// REPAIRED copy of the mesh - unlike DraftAngleAnalyzer/DeviationAnalyzer,
// curvature genuinely needs real halfedge connectivity (a raw triangle soup
// has none), so MeshRepair::repairSoupToMesh() runs first here. Gaussian and
// principal curvature/direction modes are a later addition (interpolated_
// corrected_curvatures already produces both, given the right named
// parameters) - mean curvature ships first as the single most broadly useful
// reading, mirroring how Draft Angle/Zebra Stripe shipped before true
// Wall-Thickness.
//
// Because repair can change vertex count/ordering, results are mapped back
// onto the CALLER's original mesh via CONSTRAINED matching, not a naive
// nearest-point search: CGAL::Polygon_mesh_processing::locate_with_AABB_tree()
// finds each original vertex's (face, barycentric) location on the repaired
// mesh, interpolates the curvature value from that face's three vertices,
// and the result is accepted only if BOTH the interpolated normal at that
// location is reasonably close to the original vertex's own normal AND the
// matched face's connected component corresponds to the original vertex's
// own connected component (majority-voted per component, not per vertex).
// Repair is allowed to reverse a whole component; that orientation is
// majority-detected, accepted, and used to restore the source curvature
// sign before the per-vertex normal test. Distance, orientation, and
// component identity are checked jointly; distance alone would let a match
// jump across a thin double-wall to the geometrically-close opposite side.
// A vertex failing any check is marked invalid rather than
// silently colored from the nearest-but-wrong point.
// ---------------------------------------------------------------------------
struct CurvatureResult
{
	// One entry per vertex of the CALLER's original mesh (getTrsfPoints()
	// order) - mean curvature where valid, 0 (ignored) where not.
	std::vector<float> meanCurvaturePerVertex;
	std::vector<bool> validPerVertex;
	// User-facing disclosure of what repair actually changed, e.g.
	// "12 vertices adjusted, 3 non-manifold vertices fixed during repair" -
	// see this class's doc comment for why repair (unlike Draft Angle/
	// Deviation, which deliberately avoid it) is warranted here.
	QString repairSummary;
	// Non-blocking advisory (empty when nothing to report): set when the
	// input mesh carries source-mesh provenance (SceneMesh::getSourceMeshIds(),
	// populated only by a "Merge Selected"/Mesh-Union-fallback combine - see
	// project memory project_curvature_edge_welding_provenance_design.md) AND
	// the edge-welding step below actually welded across two different
	// source ids at least once. The weld itself is NOT suppressed - this is
	// a detect-and-warn design, not a correctness gate, so
	// meanCurvaturePerVertex/validPerVertex are unaffected either way.
	QString crossBodyWeldAdvisory;
	int crossBodyWeldCount = 0;
	bool succeeded = false;
};

class CurvatureAnalyzer
{
public:
	// ballRadius < 0 uses CGAL's own default (sum of measures on faces
	// immediately around each vertex, no smoothing expansion) - a positive
	// value expands the measure over a ball of that world-space radius,
	// trading locality for noise reduction on a dense/noisy mesh.
	static CurvatureResult computeMeanCurvature(SceneMesh* mesh, double ballRadius = -1.0,
		const std::atomic<bool>* cancelRequested = nullptr);

	// Snapshot-based entry point - identical computation, but reads world-
	// space points/normals/indices directly instead of a live SceneMesh*, so
	// it's safe to call from a background thread against an
	// AnalysisMeshSnapshot's copied-out geometry. The SceneMesh* overload
	// above is now a thin wrapper around this one. `normals` may be empty or
	// incomplete; geometric area-weighted vertex normals are used wherever
	// an imported normal is unavailable, non-finite, or degenerate.
	// `sourceMeshIds`, when non-empty, must be one entry per ORIGINAL vertex
	// (same order/count as `points`/`normals`) - see
	// SceneMesh::getSourceMeshIds()'s doc comment. Empty (the default) is the
	// common case and behaves exactly as before this parameter existed.
	static CurvatureResult computeMeanCurvature(
		const std::vector<float>& points, const std::vector<float>& normals,
		const std::vector<unsigned int>& indices, double ballRadius = -1.0,
		const std::atomic<bool>* cancelRequested = nullptr,
		const std::vector<quint64>& sourceMeshIds = {});
};
