#include "MeshInstanceState.h"
#include "Triangle.h"
#include "TriangleMollerTrumbore.h"
#include "Point.h"

#include <QQuaternion>
#include <QVector3D>

#include <algorithm>
#include <atomic>
#include <limits>

// ---------------------------------------------------------------------------
// File-scope helpers
// ---------------------------------------------------------------------------
namespace
{
QQuaternion instanceEulerToQuaternion(const QVector3D& rotation)
{
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix.rotate(rotation.x(), QVector3D(1.0f, 0.0f, 0.0f));
    matrix.rotate(rotation.y(), QVector3D(0.0f, 1.0f, 0.0f));
    matrix.rotate(rotation.z(), QVector3D(0.0f, 0.0f, 1.0f));
    return QQuaternion::fromRotationMatrix(matrix.toGenericMatrix<3, 3>()).normalized();
}
} // namespace

// ---------------------------------------------------------------------------
// Static definition
// ---------------------------------------------------------------------------
std::atomic<quint64> MeshInstanceState::_runtimeBoundsRevision{1};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
MeshInstanceState::MeshInstanceState()
{
    _rotationQuat                = QQuaternion();
    _explodedViewRotationQuat    = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    _transformation.setToIdentity();
    _explodedViewTransformation.setToIdentity();
    _sceneRenderTransform.setToIdentity();
}

MeshInstanceState::~MeshInstanceState()
{
    for (Triangle* t : _triangles)
        delete t;
}

// ---------------------------------------------------------------------------
// Static bounds revision
// ---------------------------------------------------------------------------
quint64 MeshInstanceState::currentRuntimeBoundsRevision()
{
    return _runtimeBoundsRevision.load(std::memory_order_relaxed);
}

void MeshInstanceState::markRuntimeBoundsChanged()
{
    _runtimeBoundsRevision.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Combined render transform
// ---------------------------------------------------------------------------
QMatrix4x4 MeshInstanceState::combinedRenderTransform() const
{
    if (_combinedRenderTransformDirty)
    {
        _cachedCombinedRenderTransform =
            _explodedViewTransformation * _transformation * _sceneRenderTransform;
        if (!_explosionOffset.isNull())
        {
            QMatrix4x4 t;
            t.translate(_explosionOffset);
            _cachedCombinedRenderTransform = t * _cachedCombinedRenderTransform;
        }
        _combinedRenderTransformDirty = false;
    }
    return _cachedCombinedRenderTransform;
}

void MeshInstanceState::invalidateCombinedRenderTransformCache() const
{
    _combinedRenderTransformDirty = true;
}

// ---------------------------------------------------------------------------
// Rebuild helpers
// ---------------------------------------------------------------------------
void MeshInstanceState::rebuildAbsoluteTransformation()
{
    _transformation.setToIdentity();
    _transformation.translate(_transX, _transY, _transZ);
    _transformation.rotate(_rotationQuat);
    _transformation.scale(_scaleX, _scaleY, _scaleZ);
    invalidateCombinedRenderTransformCache();
}

void MeshInstanceState::rebuildExplodedViewTransformation()
{
    _explodedViewTransformation.setToIdentity();
    _explodedViewTransformation.translate(
        _explodedViewTransX, _explodedViewTransY, _explodedViewTransZ);
    _explodedViewTransformation.rotate(_explodedViewRotationQuat);
    _explodedViewTransformation.scale(
        _explodedViewScaleX, _explodedViewScaleY, _explodedViewScaleZ);
    invalidateCombinedRenderTransformCache();
}

// ---------------------------------------------------------------------------
// Fast O(1) AABB update from 8 local AABB corners
// ---------------------------------------------------------------------------
void MeshInstanceState::fastUpdateWorldBounds()
{
    const QMatrix4x4 combined = combinedRenderTransform();
    float xMin =  std::numeric_limits<float>::max();
    float yMin =  std::numeric_limits<float>::max();
    float zMin =  std::numeric_limits<float>::max();
    float xMax = -std::numeric_limits<float>::max();
    float yMax = -std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();
    for (const QVector3D& corner : _localBoundingBox.getCorners())
    {
        const QVector3D tc = combined.map(corner);
        xMin = std::min(xMin, tc.x());
        yMin = std::min(yMin, tc.y());
        zMin = std::min(zMin, tc.z());
        xMax = std::max(xMax, tc.x());
        yMax = std::max(yMax, tc.y());
        zMax = std::max(zMax, tc.z());
    }
    _boundingBox.setLimits(
        static_cast<double>(xMin), static_cast<double>(xMax),
        static_cast<double>(yMin), static_cast<double>(yMax),
        static_cast<double>(zMin), static_cast<double>(zMax));
    markRuntimeBoundsChanged();
}

// ---------------------------------------------------------------------------
// Full O(N) bounds / picking update
// ---------------------------------------------------------------------------
void MeshInstanceState::buildTriangles(const std::vector<unsigned int>& indices)
{
    for (Triangle* t : _triangles)
        delete t;
    _triangles.clear();

    try
    {
        constexpr size_t stride = 3;
        for (size_t i = 0; i < indices.size(); )
        {
            QVector3D v1(_trsfPoints.at(stride * indices.at(i)     + 0),
                         _trsfPoints.at(stride * indices.at(i)     + 1),
                         _trsfPoints.at(stride * indices.at(i)     + 2));
            ++i;
            QVector3D v2(_trsfPoints.at(stride * indices.at(i) + 0),
                         _trsfPoints.at(stride * indices.at(i) + 1),
                         _trsfPoints.at(stride * indices.at(i) + 2));
            ++i;
            QVector3D v3(_trsfPoints.at(stride * indices.at(i) + 0),
                         _trsfPoints.at(stride * indices.at(i) + 1),
                         _trsfPoints.at(stride * indices.at(i) + 2));
            ++i;
            _triangles.push_back(new TriangleMollerTrumbore(v1, v2, v3, nullptr));
        }
    }
    catch (const std::exception& ex)
    {
        Q_UNUSED(ex)
    }
}

void MeshInstanceState::computeBounds()
{
    if (_trsfPoints.empty())
        return;

    QVector3D xmin, xmax, ymin, ymax, zmin, zmax;
    xmin = ymin = zmin = QVector3D(1, 1, 1) *  std::numeric_limits<float>::infinity();
    xmax = ymax = zmax = QVector3D(1, 1, 1) * -std::numeric_limits<float>::infinity();

    for (size_t i = 0; i < _trsfPoints.size(); i += 3)
    {
        const QVector3D p(_trsfPoints[i], _trsfPoints[i + 1], _trsfPoints[i + 2]);
        if (p.x() < xmin.x()) xmin = p;
        if (p.x() > xmax.x()) xmax = p;
        if (p.y() < ymin.y()) ymin = p;
        if (p.y() > ymax.y()) ymax = p;
        if (p.z() < zmin.z()) zmin = p;
        if (p.z() > zmax.z()) zmax = p;
    }

    const QVector3D center(
        (xmin.x() + xmax.x()) * 0.5f,
        (ymin.y() + ymax.y()) * 0.5f,
        (zmin.z() + zmax.z()) * 0.5f);

    float sqRad = 0.0f;
    for (size_t i = 0; i < _trsfPoints.size(); i += 3)
    {
        const QVector3D p(_trsfPoints[i], _trsfPoints[i + 1], _trsfPoints[i + 2]);
        const float d = (p - center).lengthSquared();
        if (d > sqRad) sqRad = d;
    }

    _boundingSphere.setCenter(center);
    _boundingSphere.setRadius(std::sqrt(sqRad));

    _boundingBox.setLimits(
        xmin.x(), xmax.x(),
        ymin.y(), ymax.y(),
        zmin.z(), zmax.z());
}

void MeshInstanceState::updateRuntimeBounds(
    const std::vector<float>& points,
    const std::vector<float>& normals,
    const std::vector<float>& tangents,
    const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    // Recompute local-space AABB from untransformed points (used by fast paths)
    if (!points.empty())
    {
        float lxMin =  std::numeric_limits<float>::max();
        float lyMin =  std::numeric_limits<float>::max();
        float lzMin =  std::numeric_limits<float>::max();
        float lxMax = -std::numeric_limits<float>::max();
        float lyMax = -std::numeric_limits<float>::max();
        float lzMax = -std::numeric_limits<float>::max();
        for (size_t i = 0; i < points.size(); i += 3)
        {
            lxMin = std::min(lxMin, points[i]);
            lyMin = std::min(lyMin, points[i + 1]);
            lzMin = std::min(lzMin, points[i + 2]);
            lxMax = std::max(lxMax, points[i]);
            lyMax = std::max(lyMax, points[i + 1]);
            lzMax = std::max(lzMax, points[i + 2]);
        }
        _localBoundingBox.setLimits(
            static_cast<double>(lxMin), static_cast<double>(lxMax),
            static_cast<double>(lyMin), static_cast<double>(lyMax),
            static_cast<double>(lzMin), static_cast<double>(lzMax));
    }

    _trsfPoints.clear();
    _trsfNormals.clear();
    _trsfTangents.clear();
    _trsfBitangents.clear();

    const QMatrix4x4 combined = combinedRenderTransform();

    // Transform positions
    for (size_t i = 0; i < points.size(); i += 3)
    {
        const QVector3D p(points[i], points[i + 1], points[i + 2]);
        const QVector3D tp = combined.map(p);
        _trsfPoints.push_back(tp.x());
        _trsfPoints.push_back(tp.y());
        _trsfPoints.push_back(tp.z());
    }

    // Transform normals (inverse-transpose: zero out translation column)
    QMatrix4x4 rotMat = combined;
    rotMat.setColumn(3, QVector4D(0, 0, 0, 1));
    for (size_t i = 0; i < normals.size(); i += 3)
    {
        const QVector3D n(normals[i], normals[i + 1], normals[i + 2]);
        const QVector3D tn = rotMat.map(n);
        _trsfNormals.push_back(tn.x());
        _trsfNormals.push_back(tn.y());
        _trsfNormals.push_back(tn.z());
    }

    // Transform tangents
    if (!tangents.empty())
    {
        for (size_t i = 0; i < tangents.size(); i += 3)
        {
            QVector3D tt = rotMat.map(QVector3D(tangents[i], tangents[i + 1], tangents[i + 2]));
            const float len = tt.length();
            tt = (len > 0.001f) ? tt.normalized() : QVector3D(1.f, 0.f, 0.f);
            _trsfTangents.push_back(tt.x());
            _trsfTangents.push_back(tt.y());
            _trsfTangents.push_back(tt.z());
        }
    }

    // Transform bitangents
    if (!bitangents.empty())
    {
        for (size_t i = 0; i < bitangents.size(); i += 3)
        {
            QVector3D tb = rotMat.map(QVector3D(bitangents[i], bitangents[i + 1], bitangents[i + 2]));
            const float len = tb.length();
            tb = (len > 0.001f) ? tb.normalized() : QVector3D(0.f, 1.f, 0.f);
            _trsfBitangents.push_back(tb.x());
            _trsfBitangents.push_back(tb.y());
            _trsfBitangents.push_back(tb.z());
        }
    }

    if (_buildPickingTriangles)
        buildTriangles(indices);
    else
    {
        for (Triangle* t : _triangles)
            delete t;
        _triangles.clear();
    }
    computeBounds();
    markRuntimeBoundsChanged();
}

void MeshInstanceState::fullUpdateRuntimeBounds(
    const std::vector<float>& points,
    const std::vector<float>& normals,
    const std::vector<float>& tangents,
    const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

// ---------------------------------------------------------------------------
// User TRS setters
// ---------------------------------------------------------------------------
QVector3D MeshInstanceState::getTranslation() const
{
    return QVector3D(_transX, _transY, _transZ);
}

void MeshInstanceState::setTranslation(const QVector3D& trans,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _transX = trans.x(); _transY = trans.y(); _transZ = trans.z();
    rebuildAbsoluteTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

QVector3D MeshInstanceState::getRotation() const
{
    return QVector3D(_rotateX, _rotateY, _rotateZ);
}

void MeshInstanceState::setRotation(const QVector3D& rota,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _rotateX = rota.x(); _rotateY = rota.y(); _rotateZ = rota.z();
    _rotationQuat = instanceEulerToQuaternion(rota);
    rebuildAbsoluteTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

QQuaternion MeshInstanceState::getRotationQuaternion() const
{
    return _rotationQuat;
}

void MeshInstanceState::setRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _rotationQuat = quat.normalized();
    _rotateX = displayEuler.x(); _rotateY = displayEuler.y(); _rotateZ = displayEuler.z();
    rebuildAbsoluteTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

QVector3D MeshInstanceState::getScaling() const
{
    return QVector3D(_scaleX, _scaleY, _scaleZ);
}

void MeshInstanceState::setScaling(const QVector3D& scale,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _scaleX = scale.x(); _scaleY = scale.y(); _scaleZ = scale.z();
    rebuildAbsoluteTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

void MeshInstanceState::resetTransformations(
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _transX = _transY = _transZ = 0.f;
    _rotateX = _rotateY = _rotateZ = 0.f;
    _scaleX = _scaleY = _scaleZ = 1.f;
    _rotationQuat = QQuaternion();
    rebuildAbsoluteTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

// Fast TRS setters
void MeshInstanceState::setTranslationFast(const QVector3D& trans)
{
    _transX = trans.x(); _transY = trans.y(); _transZ = trans.z();
    rebuildAbsoluteTransformation();
    fastUpdateWorldBounds();
}

void MeshInstanceState::setRotationFast(const QVector3D& rota)
{
    _rotateX = rota.x(); _rotateY = rota.y(); _rotateZ = rota.z();
    _rotationQuat = instanceEulerToQuaternion(rota);
    rebuildAbsoluteTransformation();
    fastUpdateWorldBounds();
}

void MeshInstanceState::setRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler)
{
    _rotationQuat = quat.normalized();
    _rotateX = displayEuler.x(); _rotateY = displayEuler.y(); _rotateZ = displayEuler.z();
    rebuildAbsoluteTransformation();
    fastUpdateWorldBounds();
}

void MeshInstanceState::setScalingFast(const QVector3D& scale)
{
    _scaleX = scale.x(); _scaleY = scale.y(); _scaleZ = scale.z();
    rebuildAbsoluteTransformation();
    fastUpdateWorldBounds();
}

// ---------------------------------------------------------------------------
// Exploded-view TRS setters
// ---------------------------------------------------------------------------
QVector3D MeshInstanceState::getExplodedViewTranslation() const
{
    return QVector3D(_explodedViewTransX, _explodedViewTransY, _explodedViewTransZ);
}

void MeshInstanceState::setExplodedViewTranslation(const QVector3D& trans,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _explodedViewTransX = trans.x(); _explodedViewTransY = trans.y(); _explodedViewTransZ = trans.z();
    rebuildExplodedViewTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

QVector3D MeshInstanceState::getExplodedViewRotation() const
{
    return QVector3D(_explodedViewRotateX, _explodedViewRotateY, _explodedViewRotateZ);
}

void MeshInstanceState::setExplodedViewRotation(const QVector3D& rota,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _explodedViewRotateX = rota.x(); _explodedViewRotateY = rota.y(); _explodedViewRotateZ = rota.z();
    _explodedViewRotationQuat = instanceEulerToQuaternion(rota);
    rebuildExplodedViewTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

QQuaternion MeshInstanceState::getExplodedViewRotationQuaternion() const
{
    return _explodedViewRotationQuat;
}

void MeshInstanceState::setExplodedViewRotationQuaternion(const QQuaternion& quat,
    const QVector3D& displayEuler,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _explodedViewRotationQuat = quat.normalized();
    _explodedViewRotateX = displayEuler.x(); _explodedViewRotateY = displayEuler.y(); _explodedViewRotateZ = displayEuler.z();
    rebuildExplodedViewTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

QVector3D MeshInstanceState::getExplodedViewScaling() const
{
    return QVector3D(_explodedViewScaleX, _explodedViewScaleY, _explodedViewScaleZ);
}

void MeshInstanceState::setExplodedViewScaling(const QVector3D& scale,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _explodedViewScaleX = scale.x(); _explodedViewScaleY = scale.y(); _explodedViewScaleZ = scale.z();
    rebuildExplodedViewTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

void MeshInstanceState::resetExplodedViewTransformations(
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _explodedViewTransX = _explodedViewTransY = _explodedViewTransZ = 0.f;
    _explodedViewRotateX = _explodedViewRotateY = _explodedViewRotateZ = 0.f;
    _explodedViewScaleX = _explodedViewScaleY = _explodedViewScaleZ = 1.f;
    _explodedViewRotationQuat = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    rebuildExplodedViewTransformation();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

// Fast exploded-view TRS setters
void MeshInstanceState::setExplodedViewTranslationFast(const QVector3D& trans)
{
    _explodedViewTransX = trans.x(); _explodedViewTransY = trans.y(); _explodedViewTransZ = trans.z();
    rebuildExplodedViewTransformation();
    fastUpdateWorldBounds();
}

void MeshInstanceState::setExplodedViewRotationFast(const QVector3D& rota)
{
    _explodedViewRotateX = rota.x(); _explodedViewRotateY = rota.y(); _explodedViewRotateZ = rota.z();
    _explodedViewRotationQuat = instanceEulerToQuaternion(rota);
    rebuildExplodedViewTransformation();
    fastUpdateWorldBounds();
}

void MeshInstanceState::setExplodedViewRotationQuaternionFast(const QQuaternion& quat,
    const QVector3D& displayEuler)
{
    _explodedViewRotationQuat = quat.normalized();
    _explodedViewRotateX = displayEuler.x(); _explodedViewRotateY = displayEuler.y(); _explodedViewRotateZ = displayEuler.z();
    rebuildExplodedViewTransformation();
    fastUpdateWorldBounds();
}

void MeshInstanceState::setExplodedViewScalingFast(const QVector3D& scale)
{
    _explodedViewScaleX = scale.x(); _explodedViewScaleY = scale.y(); _explodedViewScaleZ = scale.z();
    rebuildExplodedViewTransformation();
    fastUpdateWorldBounds();
}

// ---------------------------------------------------------------------------
// Scene render transform
// ---------------------------------------------------------------------------
void MeshInstanceState::setSceneRenderTransform(const QMatrix4x4& trsf,
    const std::vector<float>& points, const std::vector<float>& normals,
    const std::vector<float>& tangents, const std::vector<float>& bitangents,
    const std::vector<unsigned int>& indices)
{
    _sceneRenderTransform = trsf;
    invalidateCombinedRenderTransformCache();
    updateRuntimeBounds(points, normals, tangents, bitangents, indices);
}

void MeshInstanceState::setSceneRenderTransformFast(const QMatrix4x4& trsf)
{
    _sceneRenderTransform = trsf;
    invalidateCombinedRenderTransformCache();
    if (!_localBoundingBox.getCorners().empty())
        fastUpdateWorldBounds();
}

// ---------------------------------------------------------------------------
// Explosion offset
// ---------------------------------------------------------------------------
void MeshInstanceState::setExplosionOffset(const QVector3D& offset)
{
    _explosionOffset = offset;
    invalidateCombinedRenderTransformCache();
}

// ---------------------------------------------------------------------------
// Bounds accessors
// ---------------------------------------------------------------------------
BoundingSphere MeshInstanceState::getBoundingSphere() const
{
    if (_explosionOffset.isNull())
        return _boundingSphere;
    BoundingSphere s = _boundingSphere;
    s.setCenter(_boundingSphere.getCenter() + _explosionOffset);
    return s;
}

QVector3D MeshInstanceState::getStableTransformCenter() const
{
    const Point localCenter = _localBoundingBox.center();
    const QVector3D center(
        static_cast<float>(localCenter.getX()),
        static_cast<float>(localCenter.getY()),
        static_cast<float>(localCenter.getZ()));
    return combinedRenderTransform().map(center);
}

float MeshInstanceState::getStableTransformRadius(const std::vector<float>& points) const
{
    const Point localCenterPoint = _localBoundingBox.center();
    const QVector3D localCenter(
        static_cast<float>(localCenterPoint.getX()),
        static_cast<float>(localCenterPoint.getY()),
        static_cast<float>(localCenterPoint.getZ()));

    float localRadius = 0.0f;
    for (size_t i = 0; i < points.size(); i += 3)
    {
        const QVector3D p(points[i], points[i + 1], points[i + 2]);
        localRadius = std::max(localRadius, (p - localCenter).length());
    }

    const QMatrix4x4 combined = combinedRenderTransform();
    const QVector3D col0(combined(0, 0), combined(1, 0), combined(2, 0));
    const QVector3D col1(combined(0, 1), combined(1, 1), combined(2, 1));
    const QVector3D col2(combined(0, 2), combined(1, 2), combined(2, 2));
    const float maxScale = std::max(col0.length(), std::max(col1.length(), col2.length()));

    return localRadius * maxScale;
}

QRect MeshInstanceState::projectedRect(const QMatrix4x4& modelView,
    const QMatrix4x4& projection,
    const QRect& viewport,
    const QRect& window) const
{
    float xMin = std::numeric_limits<float>::max();
    float xMax = std::numeric_limits<float>::lowest();
    float yMin = std::numeric_limits<float>::max();
    float yMax = std::numeric_limits<float>::lowest();

    for (size_t i = 0; i < _trsfPoints.size(); i += 3)
    {
        const QVector3D point(_trsfPoints[i], _trsfPoints[i + 1], _trsfPoints[i + 2]);
        const QVector3D projPoint = point.project(modelView, projection, viewport);
        xMin = std::min(xMin, projPoint.x());
        xMax = std::max(xMax, projPoint.x());
        yMin = std::min(yMin, projPoint.y());
        yMax = std::max(yMax, projPoint.y());
    }
    return QRect(static_cast<int>(xMin),
                 static_cast<int>(window.height() - yMax),
                 static_cast<int>(xMax - xMin),
                 static_cast<int>(yMax - yMin));
}

// ---------------------------------------------------------------------------
// Ray intersection
// ---------------------------------------------------------------------------
bool MeshInstanceState::intersectsWithRay(const QVector3D& rayPos,
    const QVector3D& rayDir,
    QVector3D& outIntersectionPoint)
{
    // Bring the ray into non-exploded space for triangle testing (triangles
    // are stored at base world positions, without explosion offset applied).
    const QVector3D adjustedRayPos = _explosionOffset.isNull()
        ? rayPos : rayPos - _explosionOffset;

    float closestDistance = std::numeric_limits<float>::max();
    bool  found           = false;
    QVector3D bestIntersection;

    for (int i = 0; i < static_cast<int>(_triangles.size()); ++i)
    {
        QVector3D hitPoint;
        if (_triangles[i]->intersectsWithRay(adjustedRayPos, rayDir, hitPoint))
        {
            const float dist = (hitPoint - adjustedRayPos).length();
            if (dist < closestDistance)
            {
                closestDistance  = dist;
                bestIntersection = hitPoint;
                found            = true;
            }
        }
    }

    if (found)
    {
        outIntersectionPoint = bestIntersection + _explosionOffset;
        return true;
    }
    return false;
}

bool MeshInstanceState::intersectsWithRayDetailed(const QVector3D& rayPos,
    const QVector3D& rayDir,
    QVector3D& outIntersectionPoint,
    int& outTriangleIndex,
    QVector3D& outBarycentric)
{
    // Mirrors intersectsWithRay() above - see its comment for why the ray
    // is adjusted into non-exploded space before testing.
    const QVector3D adjustedRayPos = _explosionOffset.isNull()
        ? rayPos : rayPos - _explosionOffset;

    float closestDistance = std::numeric_limits<float>::max();
    bool  found            = false;
    QVector3D bestIntersection;
    int bestIndex = -1;

    for (int i = 0; i < static_cast<int>(_triangles.size()); ++i)
    {
        QVector3D hitPoint;
        if (_triangles[i]->intersectsWithRay(adjustedRayPos, rayDir, hitPoint))
        {
            const float dist = (hitPoint - adjustedRayPos).length();
            if (dist < closestDistance)
            {
                closestDistance  = dist;
                bestIntersection = hitPoint;
                bestIndex        = i;
                found            = true;
            }
        }
    }

    if (!found)
        return false;

    outIntersectionPoint = bestIntersection + _explosionOffset;
    outTriangleIndex = bestIndex;

    float u, v, w;
    _triangles[bestIndex]->computeBarycentric(bestIntersection, u, v, w);
    outBarycentric = QVector3D(u, v, w);
    return true;
}
