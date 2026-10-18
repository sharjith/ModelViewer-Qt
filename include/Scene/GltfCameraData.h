#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>

// ---------------------------------------------------------------------------
// GltfCameraData
//
// Data structures for glTF camera support.
//
// A glTF file may define one or more cameras that are referenced by nodes
// in the scene graph.  Each camera carries its projection parameters and the
// world-space transform derived from its node's accumulated transform chain.
//
// These structures are populated by AssImpModelLoader::parseSceneCameras()
// during import and stored in SceneGraph keyed by source file path.
// CamerasPanel reads them to build its tree, and ViewportWidget uses them to
// switch the primary camera to a glTF-defined viewpoint.
// ---------------------------------------------------------------------------

enum class GltfCameraType
{
    Perspective,
    Orthographic
};

// One camera entry from the glTF file.
struct GltfCameraEntry
{
    // Name as declared in the glTF "cameras" array and referenced by the node.
    QString         name;

    // Name of the scene-graph node that hosts this camera.  Assimp sets
    // aiCamera::mName to the referencing node's name, so for glTF files
    // this equals 'name'.  Stored separately so the per-frame animation
    // update can look the node up directly in worldTransforms without
    // any extra string massaging.
    QString         nodeName;
    int             nodeIndex       = -1;
    bool            hasAiChildPath  = false;
    QVector<int>    aiChildPath;

    GltfCameraType  type            = GltfCameraType::Perspective;

    // Perspective parameters (valid when type == Perspective).
    // fovYRadians is the vertical field of view in radians (glTF "yfov").
    float           fovYRadians     = 0.7854f;  // π/4 ≈ 45°
    float           zNear           = 0.01f;
    float           zFar            = 10000.0f;

    // Orthographic parameters (valid when type == Orthographic).
    // xMag / yMag are half-extents of the orthographic view box.
    float           xMag            = 1.0f;
    float           yMag            = 1.0f;

    // World-space camera orientation derived from the node's accumulated transform.
    // worldDirection is the normalised viewing direction (glTF cameras look along -Z).
    // worldUp is the normalised up vector.
    QVector3D       worldPosition;
    QVector3D       worldDirection  = { 0.0f,  0.0f, -1.0f };
    QVector3D       worldUp         = { 0.0f,  1.0f,  0.0f };
    bool            needsModelTransformCompensation = true;

    // Set for synthetic entries captured live from the viewport ("Capture
    // View" in the Cameras tab; see ViewportWidget::captureCurrentCameraEntry()),
    // which have no authored glTF node to reference. Tells GltfPostProcessor::
    // writeGltfCameras() to create a brand-new root-level node from
    // worldPosition/worldDirection/worldUp instead of name-matching against
    // an existing exported node. Never set for entries parsed from an
    // actual glTF file - existing name-matching behavior is unchanged.
    bool            needsNewNode = false;

    // Captured-view-only: the exact Camera::getViewRange() at capture time.
    // >= 0 means "use this directly instead of reconstructing it".
    // ViewportWidget::applyGltfCameraEntryTransform() normally re-derives
    // view range from the distance between worldPosition and the scene's
    // bounding-sphere center along worldDirection - a reasonable heuristic
    // for an authored camera someone composed to frame the whole model, but
    // it has no reason to match a freely-navigated captured view (zoomed
    // into one corner, looking away from center, etc.). Since a captured
    // view already carries the exact answer, there's nothing to
    // reconstruct - using the heuristic anyway silently substitutes a
    // different pose/zoom than what was actually captured. -1 means
    // "not set, fall back to the heuristic" (authored cameras / old data).
    float           capturedViewRange = -1.0f;
};

// All camera information for one loaded glTF/GLB source file.
struct GltfCameraData
{
    QString                    sourceFile;
    QVector<GltfCameraEntry>   cameras;

    bool isEmpty() const { return cameras.isEmpty(); }
};

// Synthetic pseudo-file key under which user-captured camera views ("Capture
// View" in the Cameras tab) are stored in SceneGraph's per-file glTF camera
// data. Not a real file - no single loaded file owns a captured view, so it
// gets its own bucket, same reasoning as GltfLightData's own "__mvf__"
// legacy synthetic key. Deliberately routes captured views through the
// exact same activate/delete/save/export machinery as real glTF cameras
// (activateGltfCamera(), MetadataDeleteCommand::Kind::Camera, the export
// camera-collection loop) instead of a parallel code path, so behavior that
// already works correctly for authored cameras - notably the system-camera
// save/restore latch in ViewportInteractionController - works correctly for
// captured views too, for free. CamerasPanel special-cases this one key to
// show a friendly "Captured Views" label instead of a filename.
inline QString capturedViewsSourceFileKey()
{
    return QStringLiteral("__captured_views__");
}
