#pragma once

#include "BoundingSphere.h"
#include "BoundingBox.h"

#include <QMatrix4x4>
#include <QQuaternion>
#include <QRect>
#include <QVector3D>

#include <atomic>
#include <vector>

class Triangle;

// Per-instance mutable scene state for a mesh.
//
// Owns all transform layers (user TRS, exploded-view TRS, scene render TRS,
// explosion offset), the composed combined render transform, world-space bounds,
// the O(N) transformed-vertex caches used for picking/bounds, and the selection
// flag.  It is intentionally free of GL resources and material state.
//
// Methods that recompute bounds from geometry (updateRuntimeBounds,
// fullUpdateRuntimeBounds, resetTransformations, resetExplodedViewTransformations,
// setSceneRenderTransform) accept the geometry arrays from SceneMesh as
// parameters so this class remains decoupled from the geometry type.
class MeshInstanceState
{
public:
    MeshInstanceState();
    ~MeshInstanceState();

    MeshInstanceState(const MeshInstanceState&) = delete;
    MeshInstanceState& operator=(const MeshInstanceState&) = delete;

    // ---- Selection / winding ------------------------------------------------

    void setSelected(bool selected) { _selected = selected; }
    bool isSelected() const { return _selected; }

    void setHasNegativeScale(bool v) { _hasNegativeScale = v; }
    bool hasNegativeScale() const { return _hasNegativeScale; }

    void setActiveVariantIndex(int idx) { _activeVariantIndex = idx; }
    int  activeVariantIndex() const     { return _activeVariantIndex; }

    // ---- User TRS -----------------------------------------------------------

    QVector3D getTranslation() const;
    void setTranslation(const QVector3D& trans,
                        const std::vector<float>& points,
                        const std::vector<float>& normals,
                        const std::vector<float>& tangents,
                        const std::vector<float>& bitangents,
                        const std::vector<unsigned int>& indices);

    QVector3D getRotation() const;
    void setRotation(const QVector3D& rota,
                     const std::vector<float>& points,
                     const std::vector<float>& normals,
                     const std::vector<float>& tangents,
                     const std::vector<float>& bitangents,
                     const std::vector<unsigned int>& indices);

    QQuaternion getRotationQuaternion() const;
    void setRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler,
                               const std::vector<float>& points,
                               const std::vector<float>& normals,
                               const std::vector<float>& tangents,
                               const std::vector<float>& bitangents,
                               const std::vector<unsigned int>& indices);

    QVector3D getScaling() const;
    void setScaling(const QVector3D& scale,
                    const std::vector<float>& points,
                    const std::vector<float>& normals,
                    const std::vector<float>& tangents,
                    const std::vector<float>& bitangents,
                    const std::vector<unsigned int>& indices);

    QMatrix4x4 getTransformation() const { return _transformation; }

    void resetTransformations(const std::vector<float>& points,
                              const std::vector<float>& normals,
                              const std::vector<float>& tangents,
                              const std::vector<float>& bitangents,
                              const std::vector<unsigned int>& indices);

    // Fast variants — rebuild only the world-space AABB from 8 local corners
    // (O(1)).  Use during interactive gizmo drag; call fullUpdateRuntimeBounds()
    // once on drag-commit to resync the O(N) transformed-vertex caches.
    void setTranslationFast(const QVector3D& trans);
    void setRotationFast(const QVector3D& rota);
    void setRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler);
    void setScalingFast(const QVector3D& scale);

    // ---- Exploded-view TRS --------------------------------------------------

    QVector3D getExplodedViewTranslation() const;
    void setExplodedViewTranslation(const QVector3D& trans,
                                    const std::vector<float>& points,
                                    const std::vector<float>& normals,
                                    const std::vector<float>& tangents,
                                    const std::vector<float>& bitangents,
                                    const std::vector<unsigned int>& indices);

    QVector3D getExplodedViewRotation() const;
    void setExplodedViewRotation(const QVector3D& rota,
                                 const std::vector<float>& points,
                                 const std::vector<float>& normals,
                                 const std::vector<float>& tangents,
                                 const std::vector<float>& bitangents,
                                 const std::vector<unsigned int>& indices);

    QQuaternion getExplodedViewRotationQuaternion() const;
    void setExplodedViewRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler,
                                           const std::vector<float>& points,
                                           const std::vector<float>& normals,
                                           const std::vector<float>& tangents,
                                           const std::vector<float>& bitangents,
                                           const std::vector<unsigned int>& indices);

    QVector3D getExplodedViewScaling() const;
    void setExplodedViewScaling(const QVector3D& scale,
                                const std::vector<float>& points,
                                const std::vector<float>& normals,
                                const std::vector<float>& tangents,
                                const std::vector<float>& bitangents,
                                const std::vector<unsigned int>& indices);

    QMatrix4x4 getExplodedViewTransformation() const { return _explodedViewTransformation; }

    void resetExplodedViewTransformations(const std::vector<float>& points,
                                          const std::vector<float>& normals,
                                          const std::vector<float>& tangents,
                                          const std::vector<float>& bitangents,
                                          const std::vector<unsigned int>& indices);

    void setExplodedViewTranslationFast(const QVector3D& trans);
    void setExplodedViewRotationFast(const QVector3D& rota);
    void setExplodedViewRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler);
    void setExplodedViewScalingFast(const QVector3D& scale);

    // ---- Scene render transform (set per animation frame) -------------------

    QMatrix4x4 getSceneRenderTransform() const { return _sceneRenderTransform; }

    void setSceneRenderTransform(const QMatrix4x4& trsf,
                                 const std::vector<float>& points,
                                 const std::vector<float>& normals,
                                 const std::vector<float>& tangents,
                                 const std::vector<float>& bitangents,
                                 const std::vector<unsigned int>& indices);

    // Fast variant: updates the world AABB from local corners only (O(1)).
    // Used every animation frame where full O(N) sync is too expensive.
    void setSceneRenderTransformFast(const QMatrix4x4& trsf);

    // ---- Explosion offset (world-space translation from ExplodedViewManager) -

    void setExplosionOffset(const QVector3D& offset);
    QVector3D explosionOffset() const { return _explosionOffset; }

    // ---- Combined render transform ------------------------------------------
    // Composition: explosionOffset * explodedViewTransformation
    //              * transformation * sceneRenderTransform
    // This exact order is the load-bearing contract; do not change it.
    QMatrix4x4 combinedRenderTransform() const;
    void invalidateCombinedRenderTransformCache() const;

    // ---- Bounds -------------------------------------------------------------

    BoundingSphere getBoundingSphere() const;
    BoundingBox    getBoundingBox() const { return _boundingBox; }

    // Allow procedural subclasses (Cone, Cube, etc.) to inject analytical bounds
    // after initBuffers(), bypassing the generic vertex-sweep computeBounds().
    void setBoundingSphere(const BoundingSphere& bs) { _boundingSphere = bs; markRuntimeBoundsChanged(); }
    void setBoundingBox(const BoundingBox& bb) { _boundingBox = bb; markRuntimeBoundsChanged(); }

    QVector3D getStableTransformCenter() const;
    float     getStableTransformRadius(const std::vector<float>& points) const;

    float getHighestXValue() const { return static_cast<float>(_boundingBox.xMax()); }
    float getLowestXValue()  const { return static_cast<float>(_boundingBox.xMin()); }
    float getHighestYValue() const { return static_cast<float>(_boundingBox.yMax()); }
    float getLowestYValue()  const { return static_cast<float>(_boundingBox.yMin()); }
    float getHighestZValue() const { return static_cast<float>(_boundingBox.zMax()); }
    float getLowestZValue()  const { return static_cast<float>(_boundingBox.zMin()); }

    QRect projectedRect(const QMatrix4x4& modelView,
                        const QMatrix4x4& projection,
                        const QRect& viewport,
                        const QRect& window) const;

    // ---- Runtime bounds update ----------------------------------------------

    // Full O(N) update: recomputes _trsfPoints from geometry, rebuilds picking
    // triangles, updates _localBoundingBox, _boundingBox, _boundingSphere.
    // Controls whether updateRuntimeBounds rebuilds picking triangles.
    // Set to false for non-triangle primitives (lines, points) so index data
    // is never misinterpreted as triangles.  Defaults to true.
    void setBuildPickingTriangles(bool build) { _buildPickingTriangles = build; }
    bool buildPickingTriangles() const { return _buildPickingTriangles; }

    void updateRuntimeBounds(const std::vector<float>& points,
                             const std::vector<float>& normals,
                             const std::vector<float>& tangents,
                             const std::vector<float>& bitangents,
                             const std::vector<unsigned int>& indices);

    void fullUpdateRuntimeBounds(const std::vector<float>& points,
                                 const std::vector<float>& normals,
                                 const std::vector<float>& tangents,
                                 const std::vector<float>& bitangents,
                                 const std::vector<unsigned int>& indices);

    // ---- Transformed geometry access (picking / export) ---------------------

    const std::vector<float>& getTrsfPoints() const     { return _trsfPoints; }
    // World-space normals/tangents/bitangents, transformed by the
    // rotation-only (translation-stripped) part of combinedRenderTransform()
    // - see fullUpdateRuntimeBounds()'s "Transform normals"/"Transform
    // tangents"/"Transform bitangents" steps. Parallel to getTrsfPoints(),
    // one vec3 per vertex in the same order; tangents/bitangents are empty
    // if the source mesh has none.
    const std::vector<float>& getTrsfNormals() const    { return _trsfNormals; }
    const std::vector<float>& getTrsfTangents() const   { return _trsfTangents; }
    const std::vector<float>& getTrsfBitangents() const { return _trsfBitangents; }

    // ---- Ray intersection (picking) -----------------------------------------

    bool intersectsWithRay(const QVector3D& rayPos,
                           const QVector3D& rayDir,
                           QVector3D& outIntersectionPoint);

    // Same closest-hit search as intersectsWithRay(), but also reports
    // which triangle won and its barycentric weights there - the two
    // pieces intersectsWithRay() computes internally and discards. Triangle
    // index k corresponds to indices[3k], indices[3k+1], indices[3k+2] of
    // whichever indices buffer buildTriangles() was last called with (i.e.
    // the same one the caller already has, from SceneMesh). Used to build
    // a MeshSurfaceAnchor for Measurement/Annotation point picking.
    bool intersectsWithRayDetailed(const QVector3D& rayPos,
                                   const QVector3D& rayDir,
                                   QVector3D& outIntersectionPoint,
                                   int& outTriangleIndex,
                                   QVector3D& outBarycentric);

    // ---- Static: monotonic counter incremented whenever bounds change --------
    // Read by the scene runtime to invalidate the visibility BVH.
    static quint64 currentRuntimeBoundsRevision();
    static void    markRuntimeBoundsChanged();

private:
    void rebuildAbsoluteTransformation();
    void rebuildExplodedViewTransformation();
    void fastUpdateWorldBounds();
    void buildTriangles(const std::vector<unsigned int>& indices); // only called when primitiveMode is GL_TRIANGLES
    void computeBounds();

    // User TRS
    float _transX = 0.f, _transY = 0.f, _transZ = 0.f;
    float _rotateX = 0.f, _rotateY = 0.f, _rotateZ = 0.f;
    QQuaternion _rotationQuat;
    float _scaleX = 1.f, _scaleY = 1.f, _scaleZ = 1.f;
    QMatrix4x4 _transformation;

    // Exploded-view TRS (parallel system, separate from user TRS)
    float _explodedViewTransX = 0.f, _explodedViewTransY = 0.f, _explodedViewTransZ = 0.f;
    float _explodedViewRotateX = 0.f, _explodedViewRotateY = 0.f, _explodedViewRotateZ = 0.f;
    QQuaternion _explodedViewRotationQuat;
    float _explodedViewScaleX = 1.f, _explodedViewScaleY = 1.f, _explodedViewScaleZ = 1.f;
    QMatrix4x4 _explodedViewTransformation;

    QMatrix4x4 _sceneRenderTransform;
    QVector3D  _explosionOffset;

    mutable QMatrix4x4 _cachedCombinedRenderTransform;
    mutable bool       _combinedRenderTransformDirty = true;

    // World-space bounds (transform-dependent)
    BoundingSphere _boundingSphere;
    BoundingBox    _boundingBox;

    // Local-space AABB of _points before any transform.  Computed once in
    // updateRuntimeBounds(); used by fast update paths to derive the world
    // AABB from 8 corners without iterating every vertex.
    BoundingBox _localBoundingBox;

    // O(N) transformed geometry caches (world space)
    std::vector<float> _trsfPoints;
    std::vector<float> _trsfNormals;
    std::vector<float> _trsfTangents;
    std::vector<float> _trsfBitangents;

    // Picking geometry (derived from _trsfPoints + indices)
    std::vector<Triangle*> _triangles;

    bool _selected              = false;
    bool _hasNegativeScale      = false;
    bool _buildPickingTriangles = true;  // false for non-triangle primitives
    int  _activeVariantIndex    = -1;

    static std::atomic<quint64> _runtimeBoundsRevision;
};
