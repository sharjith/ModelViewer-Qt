#pragma once

// ---------------------------------------------------------------------------
// VisibilityComputationHelper
//
// Pure frustum- and clip-plane culling math, extracted from ViewportWidget so it
// can be used and tested without a GL context.
//
// Callers build a FrustumContext once per frame (after extractFrustumPlanes)
// and a ClippingContext once per frame (or whenever clipping state changes).
// Both structs are plain data — no GL, no Qt signals.
// ---------------------------------------------------------------------------

#include "BoundingBox.h"
#include <QVector4D>

class SceneMesh;

namespace VisibilityComputationHelper {

// Six normalised view-frustum planes, as produced by
// ViewportInteractionController::extractFrustumPlanes().
struct FrustumContext
{
    QVector4D planes[6] = {};
};

// Per-axis clipping state derived from SceneRenderController + scene AABB center.
struct ClipAxisContext
{
    float threshold = 0.0f;  // renderCtrl.clippingCoeff + boundingBox.center.axis
    bool  flipped   = false; // renderCtrl.clippingFlipped
};

struct ClippingContext
{
    ClipAxisContext x, y, z;
    bool yzEnabled = false;  // YZ clipping plane active (clips along X)
    bool zxEnabled = false;  // ZX clipping plane active (clips along Y)
    bool xyEnabled = false;  // XY clipping plane active (clips along Z)
};

// Box-clipping (4th clipping mode) state, ABSOLUTE world coordinates. Kept as
// its own context rather than folded into ClippingContext: that struct and its
// axis-indexed helpers below assume per-axis planes (and are relied on by the
// gizmo/capping code), and the "invisible in all passes" test there is
// vacuously true when no axis is enabled - see isBoundingBoxInvisibleInAllClipPasses().
struct BoxClippingContext
{
    float min[3] = { 0.0f, 0.0f, 0.0f }; // x, y, z lower limits
    float max[3] = { 0.0f, 0.0f, 0.0f }; // x, y, z upper limits
    bool  enabled = false;
    bool  keepInside = false; // true: keep the inside (crop); false: keep the outside (hole, the default)
};

// ---- Frustum tests ---------------------------------------------------------

bool isBoundingBoxOutside(const BoundingBox& bb, const FrustumContext& ctx);
bool isMeshOutside(const SceneMesh* mesh, const FrustumContext& ctx);
bool isMeshFullyInside(const SceneMesh* mesh, const FrustumContext& ctx);

// ---- Clip-plane tests (per axis) ------------------------------------------

bool isBoundingBoxFullyClipped_X(const BoundingBox& bb, const ClippingContext& ctx);
bool isBoundingBoxFullyClipped_Y(const BoundingBox& bb, const ClippingContext& ctx);
bool isBoundingBoxFullyClipped_Z(const BoundingBox& bb, const ClippingContext& ctx);
bool isMeshFullyClipped_X(const SceneMesh* mesh, const ClippingContext& ctx);
bool isMeshFullyClipped_Y(const SceneMesh* mesh, const ClippingContext& ctx);
bool isMeshFullyClipped_Z(const SceneMesh* mesh, const ClippingContext& ctx);

bool isBoundingBoxFullyKept_X(const BoundingBox& bb, const ClippingContext& ctx);
bool isBoundingBoxFullyKept_Y(const BoundingBox& bb, const ClippingContext& ctx);
bool isBoundingBoxFullyKept_Z(const BoundingBox& bb, const ClippingContext& ctx);
bool isMeshFullyKept_X(const SceneMesh* mesh, const ClippingContext& ctx);
bool isMeshFullyKept_Y(const SceneMesh* mesh, const ClippingContext& ctx);
bool isMeshFullyKept_Z(const SceneMesh* mesh, const ClippingContext& ctx);

// Returns true when the AABB straddles the active clipping plane for the
// given plane index (0=X, 1=Y, 2=Z): it is neither fully clipped nor fully kept.
bool isBoundingBoxStraddlesCapPlane(const BoundingBox& bb, int planeIndex,
                                    const ClippingContext& ctx);
bool isMeshStraddlesCapPlane(const SceneMesh* mesh, int planeIndex,
                             const ClippingContext& ctx);

// Returns true when the AABB is fully clipped away by EVERY enabled clipping
// plane — i.e. the mesh contributes nothing in any clip pass.
bool isBoundingBoxInvisibleInAllClipPasses(const BoundingBox& bb,
                                           const ClippingContext& ctx);
bool isMeshInvisibleInAllClipPasses(const SceneMesh* mesh, const ClippingContext& ctx);

// ---- Box-clip tests --------------------------------------------------------

// AABB shares no volume with the box (disjoint on at least one axis): in crop
// mode (keep inside) such a box is entirely clipped away.
bool isBoundingBoxOutsideBox(const BoundingBox& bb, const BoxClippingContext& ctx);
// AABB lies entirely and STRICTLY within the box (no bound touching a face):
// in hole mode (keep outside, the default) such a box is entirely clipped away. Strict because the
// hole-mode shader discard keeps fragments exactly on the box boundary.
bool isBoundingBoxFullyInsideBox(const BoundingBox& bb, const BoxClippingContext& ctx);
// Mesh wrappers. Skinned meshes are NEVER reported as clipped: their stored
// bounds may be the bind pose rather than the rendered pose (the same reason
// ViewportWidget::isMeshVisible() skips the frustum test for them), so an
// AABB test could remove visible animated geometry.
bool isMeshOutsideBox(const SceneMesh* mesh, const BoxClippingContext& ctx);
bool isMeshFullyInsideBox(const SceneMesh* mesh, const BoxClippingContext& ctx);

// Capping-group filter: true when the AABB reaches the plane of box face
// `face` (0..5 = xMin, xMax, yMin, yMax, zMin, zMax) AND overlaps that face's
// rectangle on the other two axes - anything else contributes no cap there.
// Skinned meshes always count as straddling (bind-pose bounds, see above).
bool isBoundingBoxStraddlesBoxFace(const BoundingBox& bb, int face, const BoxClippingContext& ctx);
bool isMeshStraddlesBoxFace(const SceneMesh* mesh, int face, const BoxClippingContext& ctx);

} // namespace VisibilityComputationHelper
