#include "CoordinateSystemHelper.h"

namespace
{
void orthonormalizeViewBasis(const QVector3D& viewDirection,
                             const QVector3D& upHint,
                             QVector3D& viewDir,
                             QVector3D& upDir,
                             QVector3D& rightDir)
{
    viewDir = viewDirection.normalized();
    rightDir = QVector3D::crossProduct(viewDir, upHint).normalized();
    if (rightDir.lengthSquared() <= 1.0e-8f)
        rightDir = QVector3D::crossProduct(viewDir, QVector3D(1.0f, 0.0f, 0.0f)).normalized();
    if (rightDir.lengthSquared() <= 1.0e-8f)
        rightDir = QVector3D::crossProduct(viewDir, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    upDir = QVector3D::crossProduct(rightDir, viewDir).normalized();
}

void canonicalStandardViewBasis(ViewMode mode,
                                IsoCorner corner,
                                QVector3D& viewDir,
                                QVector3D& upDir,
                                QVector3D& rightDir)
{
    QVector3D viewDirection;
    QVector3D upHint;
    switch (mode)
    {
    case ViewMode::TOP:
        viewDirection = QVector3D(0.0f, 0.0f, -1.0f);
        upHint = QVector3D(0.0f, 1.0f, 0.0f);
        break;
    case ViewMode::BOTTOM:
        viewDirection = QVector3D(0.0f, 0.0f, 1.0f);
        upHint = QVector3D(0.0f, -1.0f, 0.0f);
        break;
    case ViewMode::FRONT:
        viewDirection = QVector3D(0.0f, 1.0f, 0.0f);
        upHint = QVector3D(0.0f, 0.0f, 1.0f);
        break;
    case ViewMode::BACK:
        viewDirection = QVector3D(0.0f, -1.0f, 0.0f);
        upHint = QVector3D(0.0f, 0.0f, 1.0f);
        break;
    case ViewMode::LEFT:
        viewDirection = QVector3D(1.0f, 0.0f, 0.0f);
        upHint = QVector3D(0.0f, 0.0f, 1.0f);
        break;
    case ViewMode::RIGHT:
        viewDirection = QVector3D(-1.0f, 0.0f, 0.0f);
        upHint = QVector3D(0.0f, 0.0f, 1.0f);
        break;
    case ViewMode::ISOMETRIC:
        viewDirection = QVector3D(-1.0f, 1.0f, -1.0f);
        upHint = QVector3D(-1.0f, 1.0f, 0.0f);
        break;
    case ViewMode::DIMETRIC:
        viewDirection = QVector3D(-2.0f, 2.0f, -1.0f);
        upHint = QVector3D(-1.0f, 1.0f, 0.0f);
        break;
    case ViewMode::TRIMETRIC:
        viewDirection = QVector3D(-0.486f, 0.732f, -0.477f);
        upHint = QVector3D(-0.363f, 0.568f, 1.243f);
        break;
    default:
        viewDirection = QVector3D(0.0f, 0.0f, -1.0f);
        upHint = QVector3D(0.0f, 1.0f, 0.0f);
        break;
    }

    // The compass corner turns the axonometric views in 90-degree steps about the (canonical, Z-up)
    // up axis: SE is the canonical view above, NE +90, NW +180, SW +270 degrees (counter-clockwise seen
    // from above; e.g. SE's (-1, 1, -1) becomes NE's (-1, -1, -1)). The up hint turns with it. Applied
    // here, before the up-axis convention rotation, so every corner looks the same in Z-up and Y-up.
    if (isAxonometricMode(mode) && corner != IsoCorner::SE)
    {
        const QQuaternion turn = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, 90.0f * static_cast<float>(corner));
        viewDirection = turn.rotatedVector(viewDirection);
        upHint = turn.rotatedVector(upHint);
    }

    orthonormalizeViewBasis(viewDirection, upHint, viewDir, upDir, rightDir);
}
}

namespace CoordinateSystemHelper
{
QQuaternion cameraUpAxisConventionRotation(bool cameraUpAxisZUp)
{
    return cameraUpAxisZUp
        ? QQuaternion()
        : QQuaternion::fromAxisAndAngle(QVector3D(1.0f, 0.0f, 0.0f), -90.0f);
}

QVector3D transformVectorForCameraUpAxis(bool cameraUpAxisZUp, const QVector3D& vector)
{
    return cameraUpAxisConventionRotation(cameraUpAxisZUp).rotatedVector(vector);
}

void standardViewBasis(bool cameraUpAxisZUp,
                       ViewMode mode,
                       QVector3D& viewDir,
                       QVector3D& upDir,
                       QVector3D& rightDir,
                       IsoCorner corner)
{
    canonicalStandardViewBasis(mode, corner, viewDir, upDir, rightDir);

    const QQuaternion axisRotation = cameraUpAxisConventionRotation(cameraUpAxisZUp);
    viewDir = axisRotation.rotatedVector(viewDir).normalized();
    upDir = axisRotation.rotatedVector(upDir).normalized();
    rightDir = axisRotation.rotatedVector(rightDir).normalized();
}

QQuaternion standardViewRotation(bool cameraUpAxisZUp, ViewMode mode, IsoCorner corner)
{
    QVector3D viewDir;
    QVector3D upDir;
    QVector3D rightDir;
    standardViewBasis(cameraUpAxisZUp, mode, viewDir, upDir, rightDir, corner);

    QMatrix4x4 targetMatrix;
    targetMatrix.setToIdentity();
    targetMatrix.setRow(0, QVector4D(rightDir, 0.0f));
    targetMatrix.setRow(1, QVector4D(upDir, 0.0f));
    targetMatrix.setRow(2, QVector4D(-viewDir, 0.0f));
    return QQuaternion::fromRotationMatrix(targetMatrix.toGenericMatrix<3, 3>()).normalized();
}

Plane::Orientation floorPlaneOrientation(bool cameraUpAxisZUp)
{
    return cameraUpAxisZUp ? Plane::Orientation::XY_ZNormal : Plane::Orientation::XZ_YNormal;
}

QVector3D currentWorldUpVector(bool cameraUpAxisZUp)
{
    return cameraUpAxisZUp
        ? QVector3D(0.0f, 0.0f, 1.0f)
        : QVector3D(0.0f, 1.0f, 0.0f);
}

float coordinateAlongCurrentWorldUp(bool cameraUpAxisZUp, const QVector3D& point)
{
    return cameraUpAxisZUp ? point.z() : point.y();
}

void setCoordinateAlongCurrentWorldUp(bool cameraUpAxisZUp, QVector3D& point, float value)
{
    if (cameraUpAxisZUp)
        point.setZ(value);
    else
        point.setY(value);
}

bool sceneUpAxisIsZUp(SceneUpAxis sceneUpAxis)
{
    return sceneUpAxis == SceneUpAxis::ZUp;
}

float groundPlaneScaleFactor(GroundMode groundMode, float floorSizeFactor)
{
    return (groundMode == GroundMode::Grid) ? 200.0f : floorSizeFactor;
}

float groundPlaneExtent(float floorSize, float floorSizeFactor, GroundMode groundMode)
{
    return floorSize * groundPlaneScaleFactor(groundMode, floorSizeFactor);
}

float groundPlaneZ(const BoundingBox& boundingBox,
                   bool cameraUpAxisZUp,
                   float floorSize,
                   float floorOffsetPercent,
                   float depthBias)
{
    const float lowestUp = cameraUpAxisZUp
        ? static_cast<float>(boundingBox.zMin())
        : static_cast<float>(boundingBox.yMin());
    return lowestUp - (floorSize * floorOffsetPercent) - depthBias;
}
}
