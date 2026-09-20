// Camera.cpp: implementation of the Camera class.
//
//////////////////////////////////////////////////////////////////////

#include "Camera.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <math.h>
#include <cmath>
#include <iomanip>

namespace
{
QVector3D projectOntoWorldUpPlane(const QVector3D& vector, const QVector3D& worldUp)
{
	return vector - QVector3D::dotProduct(vector, worldUp) * worldUp;
}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

// Camera
Camera::Camera() : _width(100.0f), _height(50.0f), _viewRange(200.0f), _sceneRadius(100.0f), _FOV(45.0f)
{
	_projectionType = ProjectionType::ORTHOGRAPHIC;
	_previousProjection = _projectionType;
	_viewProj = ViewProjection::SE_ISOMETRIC_VIEW;
	resetAll();
}

Camera::Camera(float width, float height, float range, float fov) :_width(width), _height(height), _viewRange(range), _sceneRadius(range * 0.5f), _FOV(fov)
{
	_projectionType = ProjectionType::ORTHOGRAPHIC;
	_viewProj = ViewProjection::SE_ISOMETRIC_VIEW;
	resetAll();
	updateProjectionMatrix();
}

void Camera::setScreenSize(float w, float h)
{
	_width = w;
	_height = h;
	updateProjectionMatrix();
}

QPoint Camera::getScreenSize() const
{
	return QPoint(_width, _height);
}

float Camera::getAspectRatio() const
{
	return _width / _height;
}

void Camera::setFOV(float fov)
{
	_FOV = fov;
	updateProjectionMatrix();
	updateViewMatrix();
}

float Camera::getFOV() const
{
	return _FOV;
}

void Camera::setViewRange(float range)
{
	_viewRange = range;
	updateProjectionMatrix();
	updateViewMatrix();
}

void Camera::setSceneRadius(float radius)
{
	_sceneRadius = std::max(radius, 0.0001f);
	updateProjectionMatrix();
}

float Camera::getSceneRadius() const
{
	return _sceneRadius;
}

float Camera::getViewRange() const
{
	return _viewRange;
}

void Camera::setProjectionType(ProjectionType proj)
{
	_projectionType = proj;
	updateProjectionMatrix();
	updateViewMatrix();
}

Camera::ProjectionType Camera::getProjectionType() const
{
	return _projectionType;
}

void Camera::setOblique(float depthScale, float angleDegrees)
{
	_obliqueDepthScale = std::max(depthScale, 0.0f);
	_obliqueAngleDegrees = angleDegrees;
	updateProjectionMatrix();
}

QVector2D Camera::getObliqueShift() const
{
	if (_obliqueDepthScale <= 0.0f)
		return QVector2D(0.0f, 0.0f);
	const float radians = qDegreesToRadians(_obliqueAngleDegrees);
	return QVector2D(_obliqueDepthScale * std::cos(radians), _obliqueDepthScale * std::sin(radians));
}

void Camera::resetAll(void)
{
	//Init with standard OGL values:
	_position = QVector3D(0.0, 0.0, 0.0);
	_viewDir = QVector3D(0.0, 0.0, -1.0);
	_rightVector = QVector3D(1.0, 0.0, 0.0);
	_upVector = QVector3D(0.0, 1.0, 0.0);
	_worldUpVector = QVector3D(0.0, 0.0, 1.0);

	//Only to be sure:
	_rotatedX = _rotatedY = _rotatedZ = 0.0;
	_zoomValue = 1.0;

	_viewMatrix.setToIdentity();
	updateViewMatrix();
}

void Camera::updateViewMatrix(void)
{
	_viewMatrix.setToIdentity();
	QVector3D eye = _position;
	QVector3D viewPoint = _position + _viewDir;

	if (_cameraMode == CameraMode::Orbit)
	{
		eye = getRenderPosition();
		viewPoint = _position;
	}

	//as we know the up vector, we can easily use gluLookAt:
	_viewMatrix.lookAt(eye, viewPoint, _upVector);

	// Camera Zooming
	_viewMatrix.scale(_zoomValue);

	QQuaternion quat = QQuaternion::fromRotationMatrix(_viewMatrix.toGenericMatrix<3, 3>());
	quat.getEulerAngles(&_rotatedY, &_rotatedZ, &_rotatedX);

	/*
	qDebug() << "=========================";
	std::cout << std::fixed;
	std::cout << "Rotated X " << std::setprecision(3) << _rotatedX << '\n';
	std::cout << "Rotated Y " << std::setprecision(3) << _rotatedY << '\n';
	std::cout << "Rotated Z " << std::setprecision(3) << _rotatedZ << '\n';
	qDebug() << "=========================";
	*/
}

void Camera::updateProjectionMatrix(void)
{
	_projectionMatrix.setToIdentity();

	float w = _width;
	float h = _height;
	if (h == 0.0f) h = 1.0f;
	float aspect = w / h;

	// Clamp view range to avoid problems with tiny scenes
	float viewRange = std::max(_viewRange, 0.0001f); // <- adjust threshold if needed
	float halfRange = viewRange * 0.5f;

	if (_projectionType == ProjectionType::ORTHOGRAPHIC)
	{
		float nearPlane = -viewRange * 10.0f;
		float farPlane = viewRange * 10.0f;
		if (_cameraMode == CameraMode::Orbit)
		{
			const float depthCenter = getOrthoViewDistance();
			// Use the larger of viewRange-relative extent or a scene-radius floor.
			// Without the floor, heavy zoom-in (small viewRange) shrinks the far
			// plane well below the scene extent, clipping the back of the model.
			// Example at 200× zoom: viewRange=R/100, depthCenter=0.06R,
			//   plain far = 0.06R + 0.2R = 0.26R, but model back is at 1.06R.
			// With sceneRadius*40 floor: far >= 0.06R + 40R — always covers the
			// full scene regardless of zoom depth, including animated models whose
			// geometry can extend well beyond the bind-pose bounding sphere.
			// Orthographic uses linear depth (no perspective divide) so a large
			// near/far range does not cause z-fighting — this is completely safe.
			const float depthExtent = std::max(viewRange * 200.0f, _sceneRadius * 40.0f);
			// Orthographic projection allows a negative near plane (no perspective
			// singularity at z=0).  Do NOT clamp to 0 — at heavy zoom depthCenter
			// is tiny so the positive clamp was cutting off all geometry between
			// the eye and the orbit centre, causing the tear-into-model artefact.
			nearPlane = depthCenter - depthExtent;
			farPlane  = depthCenter + depthExtent;
		}

		if (w <= h)
		{
			_projectionMatrix.ortho(
				-halfRange, halfRange,
				-halfRange * h / w, halfRange * h / w,
				nearPlane, farPlane
			);
		}
		else
		{
			_projectionMatrix.ortho(
				-halfRange * w / h, halfRange * w / h,
				-halfRange, halfRange,
				nearPlane, farPlane
			);
		}

		// Oblique (Cavalier/Cabinet): shear view space so depth recedes along a slanted axis while the
		// face perpendicular to the view direction keeps its true shape and scale. In view space the
		// camera looks down -z and the orbit target sits at z = -depthCenter, so a point t units behind
		// the target has z = -depthCenter - t. Shifting x, y by +shift * t (receding up and to the right,
		// the classic drawing convention) pivots the shear on the target, leaving it fixed at the centre
		// of the view. Clip-space z is untouched, so the near/far planes above are unaffected.
		if (_obliqueDepthScale > 0.0f && _cameraMode == CameraMode::Orbit)
		{
			const QVector2D shift = getObliqueShift();
			const float depthCenter = getOrthoViewDistance();
			QMatrix4x4 shear;                       // identity
			shear(0, 2) = -shift.x();               // x' = x - shift.x * (z + depthCenter)
			shear(1, 2) = -shift.y();
			shear(0, 3) = -shift.x() * depthCenter;
			shear(1, 3) = -shift.y() * depthCenter;
			_projectionMatrix *= shear;
		}
	}
	else // Perspective
	{
		float aspect = w / h;
		// Near plane: 0.1% of viewRange keeps close-up views from clipping into
		// geometry, floored at an absolute minimum so it never reaches zero.
		float nearPlane = std::max(_viewRange * 0.001f, 0.0001f);
		// Far plane: 1000× viewRange gives a 1:1 000 000 near:far ratio, comfortable
		// for a 24-bit depth buffer.  Floor at 200× sceneRadius so the full model
		// is never clipped when zoomed deep into a small sub-mesh (where viewRange
		// is tiny but the rest of the scene is still far away).
		float farPlane = std::max(_viewRange * 1000.0f, _sceneRadius * 200.0f);

		float fovY = _FOV;
		float fovYRad = fovY * PI / 180.0f;

		// Adjust vertical FOV for tall windows
		float effectiveFOV = fovY;
		if (aspect < 1.0f)
		{
			effectiveFOV = atan(tan(fovYRad * 0.5f) / aspect) * 2.0f * 180.0f / PI;
			fovYRad = effectiveFOV * PI / 180.0f;
		}

		_projectionMatrix.perspective(effectiveFOV, aspect, nearPlane, farPlane);
	}
}


void Camera::rotateX(float iAngle)
{
	_rotatedX += iAngle;

	if ((_rotatedX > 360.0) || (_rotatedX < -360.0))
	{
		_rotatedX = 0;
	}

	//Rotate viewdir around the right vector:
	_viewDir = QVector3D(QVector3D(_viewDir * cos(iAngle * PIdiv180)) + _upVector * sin(iAngle * PIdiv180)).normalized();

	//now compute the new _upVector (by cross product)
	_upVector = QVector3D::crossProduct(_viewDir, _rightVector) * -1;

	updateViewMatrix();
}

void Camera::rotateY(float iAngle)
{
	_rotatedY += iAngle;

	if ((_rotatedY > 360.0) || (_rotatedY < -360.0))
	{
		_rotatedY = 0;
	}

	//Rotate viewdir around the up vector:
	_viewDir = QVector3D(QVector3D(_viewDir * cos(iAngle * PIdiv180)) - _rightVector * sin(iAngle * PIdiv180)).normalized();

	//now compute the new _rightVector (by cross product)
	_rightVector = QVector3D::crossProduct(_viewDir, _upVector);

	updateViewMatrix();
}

void Camera::rotateZ(float iAngle)
{
	_rotatedZ += iAngle;

	if ((_rotatedZ > 360.0) || (_rotatedZ < -360.0))
	{
		_rotatedZ = 0;
	}

	//Rotate viewdir around the right vector:
	_rightVector = QVector3D(QVector3D(_rightVector * cos(iAngle * PIdiv180)) + _upVector * sin(iAngle * PIdiv180)).normalized();

	//now compute the new _upVector (by cross product)
	_upVector = QVector3D::crossProduct(_viewDir, _rightVector) * -1;

	updateViewMatrix();
}

void Camera::move(float iDX, float iDY, float iDZ)
{
	QVector3D Dir(iDX, iDY, iDZ);
	_position = _position + Dir;
	updateViewMatrix();
}

void Camera::moveForward(float iDist)
{
	_position = _position + (_viewDir * iDist);
	updateViewMatrix();
}

void Camera::moveForwardPlanar(float iDist)
{
	QVector3D planarForward = projectOntoWorldUpPlane(_viewDir, _worldUpVector);
	if (planarForward.lengthSquared() <= 1.0e-8f)
		return;

	_position = _position + planarForward.normalized() * iDist;
	updateViewMatrix();
}

void Camera::moveUpward(float iDist)
{
	_position = _position + (_upVector * iDist);
	updateViewMatrix();
}

void Camera::moveWorldUp(float iDist)
{
	_position = _position + _worldUpVector * iDist;
	updateViewMatrix();
}

void Camera::moveAcross(float iDist)
{
	_position = _position + (_rightVector * iDist);
	updateViewMatrix();
}

void Camera::moveAcrossPlanar(float iDist)
{
	QVector3D planarRight = projectOntoWorldUpPlane(_rightVector, _worldUpVector);
	if (planarRight.lengthSquared() <= 1.0e-8f)
	{
		const QVector3D planarForward = projectOntoWorldUpPlane(_viewDir, _worldUpVector);
		planarRight = QVector3D::crossProduct(planarForward, _worldUpVector);
	}

	if (planarRight.lengthSquared() <= 1.0e-8f)
		return;

	_position = _position + planarRight.normalized() * iDist;
	updateViewMatrix();
}

void Camera::setZoom(float iFactor)
{
	_zoomValue = iFactor;
	updateViewMatrix();
}

void Camera::setView(ViewProjection iProj)
{
	//_position = QVector3D();
	_viewDir = QVector3D(0.0, 0.0, -1.0);
	_rightVector = QVector3D(1.0, 0.0, 0.0);
	_upVector = QVector3D(0.0, 1.0, 0.0);
	_rotatedX = _rotatedY = _rotatedZ = 0.0;

	_viewProj = iProj;
	switch (_viewProj)
	{
	case ViewProjection::TOP_VIEW:
		_viewDir = QVector3D(0.0, 0.0, -1.0);
		_rightVector = QVector3D(1.0, 0.0, 0.0);
		_upVector = QVector3D(0.0, 1.0, 0.0);
		break;
	case ViewProjection::BOTTOM_VIEW:
		_viewDir = QVector3D(0.0, 0.0, 1.0);
		_rightVector = QVector3D(1.0, 0.0, 0.0);
		_upVector = QVector3D(0.0, -1.0, 0.0);
		break;
	case ViewProjection::FRONT_VIEW:
		_viewDir = QVector3D(0.0, 1.0, 0.0);
		_rightVector = QVector3D(1.0, 0.0, 0.0);
		_upVector = QVector3D(0.0, 0.0, 1.0);
		break;
	case ViewProjection::REAR_VIEW:
		_viewDir = QVector3D(0.0, -1.0, 0.0);
		_rightVector = QVector3D(-1.0, 0.0, 0.0);
		_upVector = QVector3D(0.0, 0.0, 1.0);
		break;
	case ViewProjection::LEFT_VIEW:
		_viewDir = QVector3D(-1.0, 0.0, 0.0);
		_rightVector = QVector3D(0.0, 1.0, 0.0);
		_upVector = QVector3D(0.0, 0.0, 1.0);
		break;
	case ViewProjection::RIGHT_VIEW:
		_viewDir = QVector3D(1.0, 0.0, 0.0);
		_rightVector = QVector3D(0.0, -1.0, 0.0);
		_upVector = QVector3D(0.0, 0.0, 1.0);
		break;
	case ViewProjection::DIMETRIC_VIEW:
		_viewDir = QVector3D(-2.0, 2.0, -1);
		_rightVector = QVector3D(1, 1, 0);
		_upVector = QVector3D(-1, 1, 0);
		break;
	case ViewProjection::TRIMETRIC_VIEW:
		_viewDir = QVector3D(-0.486f, 0.732f, -0.477f);
		_rightVector = QVector3D(1.181f, 0.778f, 0.010f);
		_upVector = QVector3D(-0.363f, 0.568f, 1.243f);
		break;
	case ViewProjection::NW_ISOMETRIC_VIEW:
		_viewDir = QVector3D(1, -1, -1);
		_rightVector = QVector3D(-1, -1, 0);
		_upVector = QVector3D(1, -1, 1);
		break;
	case ViewProjection::SW_ISOMETRIC_VIEW:
		_viewDir = QVector3D(1, 1, -1);
		_rightVector = QVector3D(1, -1, 0);
		_upVector = QVector3D(1, 1, 0);
		break;
	case ViewProjection::NE_ISOMETRIC_VIEW:
		_viewDir = QVector3D(-1, -1, -1);
		_rightVector = QVector3D(-1, 1, 0);
		_upVector = QVector3D(-1, -1, 1);
		break;
	case ViewProjection::SE_ISOMETRIC_VIEW:
	default:
		_viewDir = QVector3D(-1, 1, -1);
		_rightVector = QVector3D(1, 1, 0);
		_upVector = QVector3D(-1, 1, 0);
		break;
	}
	// Update the rotation angles
	float rx, ry, rz;
	getRotationAngles(&ry, &rz, &rx);
	_rotatedX = rx;
	_rotatedY = ry;
	_rotatedZ = rz;

	updateViewMatrix();

	/*qDebug() << "Rotated X " << _rotatedX;
	qDebug() << "Rotated Y " << _rotatedY;
	qDebug() << "Rotated Z " << _rotatedZ;*/
}

void Camera::setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir)
{
	_position = viewPos;
	_viewDir = viewDir;
	_upVector = upDir;
	_rightVector = rightDir; // QVector3D::crossProduct(_viewDir, _upVector);

	// Update the rotation angles
	float rx, ry, rz;
	getRotationAngles(&ry, &rz, &rx);
	_rotatedX = rx;
	_rotatedY = ry;
	_rotatedZ = rz;

	updateViewMatrix();
}

void Camera::setWorldUpVector(const QVector3D& upVector)
{
	if (upVector.lengthSquared() <= 1.0e-8f)
		return;

	_worldUpVector = upVector.normalized();
}

void Camera::setPosition(float iX, float iY, float iZ)
{
	_position.setX(iX);
	_position.setY(iY);
	_position.setZ(iZ);
	updateViewMatrix();
}

void Camera::setPosition(QVector3D pos)
{
	setPosition(pos.x(), pos.y(), pos.z());
}

QVector3D Camera::getRenderPosition() const
{
	if (_cameraMode != CameraMode::Orbit)
		return _position;

	const float distance = (_projectionType == ProjectionType::ORTHOGRAPHIC)
		? getOrthoViewDistance()
		: getOrbitDistance();
	return _position - _viewDir.normalized() * distance;
}

float Camera::getOrbitDistance() const
{
	return -computeViewShift(_FOV, _viewRange, 1.05f, 1.25f);
}

float Camera::getOrthoViewDistance() const
{
	return std::max(getOrbitDistance() * 4.0f, _viewRange * 6.0f);
}

void Camera::updateFlyView()
{
	float yawRad = qDegreesToRadians(_yaw);
	float pitchRad = qDegreesToRadians(_pitch);
	const QVector3D worldUp = _worldUpVector.normalized();
	const QVector3D referenceForward(1.0f, 0.0f, 0.0f);
	QVector3D referenceRight = QVector3D::crossProduct(referenceForward, worldUp).normalized();
	if (referenceRight.lengthSquared() <= 1.0e-8f)
		referenceRight = QVector3D(0.0f, 0.0f, 1.0f);

	const QVector3D planarForward =
		(std::cos(yawRad) * referenceForward + std::sin(yawRad) * referenceRight).normalized();
	_viewDir = (std::cos(pitchRad) * planarForward + std::sin(pitchRad) * worldUp).normalized();

	_rightVector = QVector3D::crossProduct(_viewDir, _worldUpVector).normalized();
	if (_rightVector.lengthSquared() <= 1.0e-8f)
		_rightVector = QVector3D::crossProduct(_viewDir, QVector3D(1.0f, 0.0f, 0.0f)).normalized();
	_upVector = QVector3D::crossProduct(_rightVector, _viewDir).normalized();

	updateViewMatrix();
}



void Camera::getRotationAngles(float* oPitch, float* oYaw, float* oRoll)
{
	QQuaternion quat = QQuaternion::fromRotationMatrix(_viewMatrix.toGenericMatrix<3, 3>());
	QVector3D euler = quat.toEulerAngles();
	*oPitch = euler.y();
	*oYaw = euler.z();
	*oRoll = euler.x();
}

void Camera::setMode(CameraMode mode)
{
	if (_cameraMode == CameraMode::Orbit &&
		(mode == CameraMode::Fly || mode == CameraMode::FirstPerson))
	{
		setYawPitchFromViewDir(); // Z-up aware
	}

	_cameraMode = mode; // move this before matrix update

	if (mode == CameraMode::Fly || mode == CameraMode::FirstPerson)
	{
		_rightVector = QVector3D::crossProduct(_viewDir, _worldUpVector).normalized();
		if (_rightVector.lengthSquared() <= 1.0e-8f)
			_rightVector = QVector3D::crossProduct(_viewDir, QVector3D(1.0f, 0.0f, 0.0f)).normalized();
		_upVector = QVector3D::crossProduct(_rightVector, _viewDir).normalized();
		updateViewMatrix(); // Apply updated orientation
	}
}


void Camera::setYawPitchFromViewDir()
{
	QVector3D dir = _viewDir.normalized();
	const QVector3D worldUp = _worldUpVector.normalized();
	const QVector3D referenceForward(1.0f, 0.0f, 0.0f);
	const QVector3D referenceRight = QVector3D::crossProduct(referenceForward, worldUp).normalized();
	const QVector3D planarDir = projectOntoWorldUpPlane(dir, worldUp).normalized();

	if (planarDir.lengthSquared() > 1.0e-8f && referenceRight.lengthSquared() > 1.0e-8f)
		_yaw = qRadiansToDegrees(std::atan2(
			QVector3D::dotProduct(planarDir, referenceRight),
			QVector3D::dotProduct(planarDir, referenceForward)));

	_pitch = qRadiansToDegrees(std::asin(std::clamp(QVector3D::dotProduct(dir, worldUp), -1.0f, 1.0f)));

	// Clamp pitch to prevent gimbal issues
	_pitch = std::clamp(_pitch, -89.0f, 89.0f);
}




void Camera::setViewMatrix(QMatrix4x4 mat)
{
	_viewMatrix = mat;
}

void Camera::setProjectionMatrix(QMatrix4x4 mat)
{
	_projectionMatrix = mat;
}

float Camera::computeViewShift(float fovYDegrees, float viewRange, float margin, float maxShiftFactor) const
{
	float fovYRad = qDegreesToRadians(fovYDegrees);
	float shift = -viewRange * margin / std::sin(fovYRad / 2.0f);
	float minShift = -viewRange * maxShiftFactor;
	return std::max(shift, minShift);
}

float Camera::getShift() const
{
	return computeViewShift(_FOV, _viewRange, 1.05f, 1.25f);
}


void Camera::computeStereoViewProjectionMatrices(int width, int height, float IOD, float depthZ, bool left_eye)
{
	// https://hub.packtpub.com/rendering-stereoscopic-3d-models-using-opengl/
	//mirror the parameters with the right eye
	float left_right_direction = -1.0f;
	if (left_eye)
		left_right_direction = 1.0f;
	float aspect_ratio = (float)width / (float)height;
	float nearZ = 1.0f;
	float farZ = _viewRange;
	double frustumshift = (IOD / 2) * nearZ / depthZ;
	float top = tan(_FOV / 2) * nearZ;
	float right = aspect_ratio * top + frustumshift * left_right_direction;
	//half screen
	float left = -aspect_ratio * top + frustumshift * left_right_direction;
	float bottom = -top;
	_projectionMatrix.frustum(left, right, bottom, top, nearZ, farZ);
	// update the view matrix
	QVector3D viewPoint = _position + _viewDir;
	_viewMatrix.lookAt(_position - _viewDir +
		QVector3D(left_right_direction * IOD / 2, 0, 0),
		//eye position
		viewPoint +
		QVector3D(left_right_direction * IOD / 2, 0, 0),
		//centre position
		_upVector //up direction
	);
}

/*
float sign(float num)
{
	return (num > 0) ? 1 : -1;
}
QQuaternion Camera::quaternionFromMatrix(QMatrix4x4 m)
{
	// Adapted from: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/index.htm

	QVector4D v;
	v.setW(sqrt(max(0.0, 1.0 + m(0, 0) + m(1, 1) + m(2, 2))) / 2);
	v.setX(sqrt(max(0.0, 1.0 + m(0, 0) - m(1, 1) - m(2, 2))) / 2);
	v.setY(sqrt(max(0.0, 1.0 - m(0, 0) + m(1, 1) - m(2, 2))) / 2);
	v.setZ(sqrt(max(0.0, 1.0 - m(0, 0) - m(1, 1) + m(2, 2))) / 2);
	v.setX(v.x() * sign(v.x() * (m(2, 1) - m(1, 2))));
	v.setY(v.y() * sign(v.y() * (m(0, 2) - m(2, 0))));
	v.setZ(v.z() * sign(v.z() * (m(1, 0) - m(0, 1))));
	QQuaternion q(v);

	return q;
}

void Camera::quatToEuler(const QQuaternion& quat, float *rotx, float *roty, float *rotz)
{
	float sqw;
	float sqx;
	float sqy;
	float sqz;

	float rotxrad;
	float rotyrad;
	float rotzrad;

	sqw = quat.scalar() * quat.scalar();
	sqx = quat.x() * quat.x();
	sqy = quat.y() * quat.y();
	sqz = quat.z() * quat.z();

	rotxrad = (float)atan2l(2.0 * (quat.y() * quat.z() + quat.x() * quat.scalar()), (-sqx - sqy + sqz + sqw));
	rotyrad = (float)asinl(-2.0 * (quat.x() * quat.z() - quat.y() * quat.scalar()));
	rotzrad = (float)atan2l(2.0 * (quat.x() * quat.y() + quat.z() * quat.scalar()), (sqx - sqy - sqz + sqw));

	*rotx = rotxrad * 180.0 / PI;
	*roty = rotyrad * 180.0 / PI;
	*rotz = rotzrad * 180.0 / PI;
}
*/
