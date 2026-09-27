#pragma once

#include <vector>
#include <QVector3D>

class SceneMesh;

// Draft-angle analysis (the Surface Analysis dialog's "Wall-Thickness" panel,
// "Draft Angle" sub-mode - wall-thickness itself is a separate, not-yet-built
// sub-mode, see Step 7 of the plan) - per-FACE (not averaged per-vertex)
// signed angle between each triangle's own flat normal and a chosen pull
// direction. Rendered via RenderableMesh::setAnalysisOverlayFlatColors()
// (Step 3/4's flat-per-face overlay path) - see that function's doc comment
// for why per-face, not per-vertex: averaging normals across a shared vertex
// can display a draft value neither adjacent face actually has, exactly the
// misleading reading this analysis exists to prevent.
class DraftAngleAnalyzer
{
public:
	// One entry per triangle (mesh->getIndices().size()/3, same face order),
	// in degrees, SIGNED:
	//   0   = wall exactly parallel to pullDirection (the reference "vertical"
	//         wall a straight pull direction naturally clears)
	//   >0  = ordinary positive-draft wall (tilts away from the pull
	//         direction - the part clears the mold on ejection)
	//   <0  = undercut (tilts back toward the pull direction - would
	//         collide with the mold on ejection)
	// pullDirection need not be normalized (normalized internally); a
	// degenerate (near-zero) triangle reports a neutral 0.0 rather than a
	// misleading value from an undefined normal. Both the mesh's geometry
	// and the pull direction are treated as WORLD-space (uses
	// SceneMesh::getTrsfPoints(), the same transformed points MeshProperties
	// already uses) - draft angle is a physical, scene-relative concept, not
	// a local/model-space one.
	static std::vector<float> computeDraftAnglesDegrees(SceneMesh* mesh, const QVector3D& pullDirection);

	// Snapshot-based entry point - identical computation, but reads world-
	// space points/indices directly instead of a live SceneMesh*, so it's
	// safe to call from a background thread (AnalysisComputeWorker) against
	// an AnalysisMeshSnapshot's copied-out geometry. The SceneMesh* overload
	// above is now a thin wrapper around this one.
	static std::vector<float> computeDraftAnglesDegrees(
		const std::vector<float>& points, const std::vector<unsigned int>& indices,
		const QVector3D& pullDirection);
};
