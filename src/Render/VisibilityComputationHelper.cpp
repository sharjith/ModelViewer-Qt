#include "VisibilityComputationHelper.h"
#include "SceneMesh.h"

namespace VisibilityComputationHelper {

// ---- Frustum tests ---------------------------------------------------------

bool isBoundingBoxOutside(const BoundingBox& bb, const FrustumContext& ctx)
{
    for (int i = 0; i < 6; ++i)
    {
        const QVector4D& p = ctx.planes[i];
        const float sx = p.x() >= 0.0f ? static_cast<float>(bb.xMax()) : static_cast<float>(bb.xMin());
        const float sy = p.y() >= 0.0f ? static_cast<float>(bb.yMax()) : static_cast<float>(bb.yMin());
        const float sz = p.z() >= 0.0f ? static_cast<float>(bb.zMax()) : static_cast<float>(bb.zMin());
        if (p.x() * sx + p.y() * sy + p.z() * sz + p.w() < 0.0f)
            return true;
    }
    return false;
}

bool isMeshOutside(const SceneMesh* mesh, const FrustumContext& ctx)
{
    return mesh ? isBoundingBoxOutside(mesh->getBoundingBox(), ctx) : true;
}

bool isMeshFullyInside(const SceneMesh* mesh, const FrustumContext& ctx)
{
    if (!mesh) return false;
    const BoundingBox& bb = mesh->getBoundingBox();
    for (int i = 0; i < 6; ++i)
    {
        const QVector4D& p = ctx.planes[i];
        // Negative support point — the AABB corner least inside this plane.
        const float sx = p.x() >= 0.0f ? static_cast<float>(bb.xMin()) : static_cast<float>(bb.xMax());
        const float sy = p.y() >= 0.0f ? static_cast<float>(bb.yMin()) : static_cast<float>(bb.yMax());
        const float sz = p.z() >= 0.0f ? static_cast<float>(bb.zMin()) : static_cast<float>(bb.zMax());
        if (p.x() * sx + p.y() * sy + p.z() * sz + p.w() < 0.0f)
            return false;
    }
    return true;
}

// ---- Clip-plane tests (per axis) ------------------------------------------

bool isBoundingBoxFullyClipped_X(const BoundingBox& bb, const ClippingContext& ctx)
{
    return ctx.x.flipped
        ? static_cast<float>(bb.xMax()) < ctx.x.threshold
        : static_cast<float>(bb.xMin()) > ctx.x.threshold;
}

bool isBoundingBoxFullyClipped_Y(const BoundingBox& bb, const ClippingContext& ctx)
{
    return ctx.y.flipped
        ? static_cast<float>(bb.yMax()) < ctx.y.threshold
        : static_cast<float>(bb.yMin()) > ctx.y.threshold;
}

bool isBoundingBoxFullyClipped_Z(const BoundingBox& bb, const ClippingContext& ctx)
{
    return ctx.z.flipped
        ? static_cast<float>(bb.zMax()) < ctx.z.threshold
        : static_cast<float>(bb.zMin()) > ctx.z.threshold;
}

bool isMeshFullyClipped_X(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxFullyClipped_X(mesh->getBoundingBox(), ctx) : true;
}

bool isMeshFullyClipped_Y(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxFullyClipped_Y(mesh->getBoundingBox(), ctx) : true;
}

bool isMeshFullyClipped_Z(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxFullyClipped_Z(mesh->getBoundingBox(), ctx) : true;
}

bool isBoundingBoxFullyKept_X(const BoundingBox& bb, const ClippingContext& ctx)
{
    return ctx.x.flipped
        ? static_cast<float>(bb.xMin()) >= ctx.x.threshold
        : static_cast<float>(bb.xMax()) <= ctx.x.threshold;
}

bool isBoundingBoxFullyKept_Y(const BoundingBox& bb, const ClippingContext& ctx)
{
    return ctx.y.flipped
        ? static_cast<float>(bb.yMin()) >= ctx.y.threshold
        : static_cast<float>(bb.yMax()) <= ctx.y.threshold;
}

bool isBoundingBoxFullyKept_Z(const BoundingBox& bb, const ClippingContext& ctx)
{
    return ctx.z.flipped
        ? static_cast<float>(bb.zMin()) >= ctx.z.threshold
        : static_cast<float>(bb.zMax()) <= ctx.z.threshold;
}

bool isMeshFullyKept_X(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxFullyKept_X(mesh->getBoundingBox(), ctx) : false;
}

bool isMeshFullyKept_Y(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxFullyKept_Y(mesh->getBoundingBox(), ctx) : false;
}

bool isMeshFullyKept_Z(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxFullyKept_Z(mesh->getBoundingBox(), ctx) : false;
}

bool isBoundingBoxStraddlesCapPlane(const BoundingBox& bb, int planeIndex,
                                    const ClippingContext& ctx)
{
    switch (planeIndex)
    {
    case 0: return !isBoundingBoxFullyClipped_X(bb, ctx) && !isBoundingBoxFullyKept_X(bb, ctx);
    case 1: return !isBoundingBoxFullyClipped_Y(bb, ctx) && !isBoundingBoxFullyKept_Y(bb, ctx);
    case 2: return !isBoundingBoxFullyClipped_Z(bb, ctx) && !isBoundingBoxFullyKept_Z(bb, ctx);
    default: return true;
    }
}

bool isMeshStraddlesCapPlane(const SceneMesh* mesh, int planeIndex,
                             const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxStraddlesCapPlane(mesh->getBoundingBox(), planeIndex, ctx) : false;
}

bool isBoundingBoxInvisibleInAllClipPasses(const BoundingBox& bb, const ClippingContext& ctx)
{
    if (ctx.yzEnabled && !isBoundingBoxFullyClipped_X(bb, ctx)) return false;
    if (ctx.zxEnabled && !isBoundingBoxFullyClipped_Y(bb, ctx)) return false;
    if (ctx.xyEnabled && !isBoundingBoxFullyClipped_Z(bb, ctx)) return false;
    return true;
}

bool isMeshInvisibleInAllClipPasses(const SceneMesh* mesh, const ClippingContext& ctx)
{
    return mesh ? isBoundingBoxInvisibleInAllClipPasses(mesh->getBoundingBox(), ctx) : true;
}

// ---- Box-clip tests --------------------------------------------------------

bool isBoundingBoxOutsideBox(const BoundingBox& bb, const BoxClippingContext& ctx)
{
    return static_cast<float>(bb.xMax()) < ctx.min[0] || static_cast<float>(bb.xMin()) > ctx.max[0]
        || static_cast<float>(bb.yMax()) < ctx.min[1] || static_cast<float>(bb.yMin()) > ctx.max[1]
        || static_cast<float>(bb.zMax()) < ctx.min[2] || static_cast<float>(bb.zMin()) > ctx.max[2];
}

bool isBoundingBoxFullyInsideBox(const BoundingBox& bb, const BoxClippingContext& ctx)
{
    // STRICT on purpose: the hole-mode fragment discard in main_scene.frag removes
    // only fragments STRICTLY inside the box (greaterThan/lessThan), so geometry
    // lying exactly on the box boundary is kept. An inclusive test here would cull
    // a whole mesh whose bounds merely touch the box faces while the shader would
    // have kept its boundary fragments - the mesh would vanish entirely. Keep this
    // in lock-step with that shader test.
    return static_cast<float>(bb.xMin()) > ctx.min[0] && static_cast<float>(bb.xMax()) < ctx.max[0]
        && static_cast<float>(bb.yMin()) > ctx.min[1] && static_cast<float>(bb.yMax()) < ctx.max[1]
        && static_cast<float>(bb.zMin()) > ctx.min[2] && static_cast<float>(bb.zMax()) < ctx.max[2];
}

bool isMeshOutsideBox(const SceneMesh* mesh, const BoxClippingContext& ctx)
{
    if (!mesh)
        return true;
    if (mesh->hasSkinning())
        return false;
    return isBoundingBoxOutsideBox(mesh->getBoundingBox(), ctx);
}

bool isMeshFullyInsideBox(const SceneMesh* mesh, const BoxClippingContext& ctx)
{
    if (!mesh)
        return true;
    if (mesh->hasSkinning())
        return false;
    return isBoundingBoxFullyInsideBox(mesh->getBoundingBox(), ctx);
}

bool isBoundingBoxStraddlesBoxFace(const BoundingBox& bb, int face, const BoxClippingContext& ctx)
{
    const int axis = face / 2;
    const float bbMin[3] = { static_cast<float>(bb.xMin()), static_cast<float>(bb.yMin()), static_cast<float>(bb.zMin()) };
    const float bbMax[3] = { static_cast<float>(bb.xMax()), static_cast<float>(bb.yMax()), static_cast<float>(bb.zMax()) };
    const float planePos = (face % 2 == 0) ? ctx.min[axis] : ctx.max[axis];

    // Must reach the face's plane...
    if (bbMin[axis] > planePos || bbMax[axis] < planePos)
        return false;
    // ...and overlap the face's rectangle on the other two axes.
    for (int other = 0; other < 3; ++other)
    {
        if (other == axis)
            continue;
        if (bbMax[other] < ctx.min[other] || bbMin[other] > ctx.max[other])
            return false;
    }
    return true;
}

bool isMeshStraddlesBoxFace(const SceneMesh* mesh, int face, const BoxClippingContext& ctx)
{
    if (!mesh)
        return false;
    if (mesh->hasSkinning())
        return true;
    return isBoundingBoxStraddlesBoxFace(mesh->getBoundingBox(), face, ctx);
}

} // namespace VisibilityComputationHelper
