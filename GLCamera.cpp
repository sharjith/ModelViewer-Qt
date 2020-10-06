// GLCamera.cpp: implementation of the GLCamera class.
//
//////////////////////////////////////////////////////////////////////

#include "GLCamera.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <math.h>
#include <cmath>
#include <iomanip>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

// GLCamera
GLCamera::GLCamera()
{
    resetAll();
}

GLCamera::GLCamera(float width, float height, float range, float fov) :_width(width), _height(height), _viewRange(range), _FOV(fov)
{
    resetAll();
    updateProjectionMatrix();
}

void GLCamera::setScreenSize(float w, float h)
{
    _width = w;
    _height = h;
    updateProjectionMatrix();
}

QPoint GLCamera::getScreenSize() const
{
    return QPoint(_width, _height);
}

float GLCamera::getAspectRatio() const
{
    return _width / _height;
}

void GLCamera::setFOV(float fov)
{
    _FOV = fov;
    updateProjectionMatrix();
}

float GLCamera::getFOV() const
{
    return _FOV;
}

void GLCamera::setViewRange(float range)
{
    _viewRange = range;
    updateProjectionMatrix();
}

float GLCamera::getViewRange() const
{
    return _viewRange;
}

void GLCamera::setProjectionType(ProjectionType proj)
{
    _projectionType = proj;
    updateProjectionMatrix();
}

GLCamera::ProjectionType GLCamera::getProjectionType() const
{
    return _projectionType;
}

void GLCamera::resetAll(void)
{
	//Init with standard OGL values:
	_position = QVector3D(0.0, 0.0, 0.0);
	_viewDir = QVector3D(0.0, 0.0, -1.0);
	_rightVector = QVector3D(1.0, 0.0, 0.0);
	_upVector = QVector3D(0.0, 1.0, 0.0);

	//Only to be sure:
	_rotatedX = _rotatedY = _rotatedZ = 0.0;
	_zoomValue = 1.0;

	_viewMatrix.setToIdentity();
	updateViewMatrix();
}

void GLCamera::updateViewMatrix(void)
{
	_viewMatrix.setToIdentity();
	//The point at which the camera looks:
	QVector3D viewPoint = _position + _viewDir;

	//as we know the up vector, we can easily use gluLookAt:
	_viewMatrix.lookAt(_position, viewPoint, _upVector);

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

void GLCamera::updateProjectionMatrix(void)
{
	_projectionMatrix.setToIdentity();
	float w = _width;
	float h = _height;
	float halfRange = _viewRange / 2;
	if (h == 0)
		h = 1.0;
	if (_projectionType == ProjectionType::ORTHOGRAPHIC)
	{
		if (w <= h)
			_projectionMatrix.ortho(-halfRange, halfRange,
				-halfRange * h / w, halfRange * h / w,
				-halfRange * 1000, halfRange * 1000);
		else
			_projectionMatrix.ortho(-halfRange * w / h, halfRange * w / h,
				-halfRange, halfRange,
				-halfRange * 1000, halfRange * 1000);
	}
	else
	{
        if (w <= h)
            _projectionMatrix.perspective(_FOV, static_cast<double>(h) / static_cast<double>(w), 1, halfRange * 10000.0f);
		else
            _projectionMatrix.perspective(_FOV, static_cast<double>(w) / static_cast<double>(h), 1, halfRange * 10000.0f);
        _projectionMatrix.translate(0.0, 0.0, -halfRange * 2.5);
	}
}

void GLCamera::rotateX(float iAngle)
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

void GLCamera::rotateY(float iAngle)
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

void GLCamera::rotateZ(float iAngle)
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

void GLCamera::move(float iDX, float iDY, float iDZ)
{
	QVector3D Dir(iDX, iDY, iDZ);
	_position = _position + Dir;
	updateViewMatrix();
}

void GLCamera::moveForward(float iDist)
{
	_position = _position + (_viewDir * -iDist);
	updateViewMatrix();
}

void GLCamera::moveUpward(float iDist)
{
	_position = _position + (_upVector * iDist);
	updateViewMatrix();
}

void GLCamera::moveAcross(float iDist)
{
	_position = _position + (_rightVector * iDist);
	updateViewMatrix();
}

void GLCamera::setZoom(float iFactor)
{
	_zoomValue = iFactor;
	updateViewMatrix();
}

void GLCamera::setView(ViewProjection iProj)
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

void GLCamera::setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir)
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

void GLCamera::setPosition(float iX, float iY, float iZ)
{
	_position.setX(iX);
	_position.setY(iY);
	_position.setZ(iZ);
	updateViewMatrix();
}

void GLCamera::setPosition(QVector3D pos)
{
	setPosition(pos.x(), pos.y(), pos.z());
}

void GLCamera::getRotationAngles(float* oPitch, float* oYaw, float* oRoll)
{
	QQuaternion quat = QQuaternion::fromRotationMatrix(_viewMatrix.toGenericMatrix<3, 3>());
	QVector3D euler = quat.toEulerAngles();
	*oPitch = euler.y();
	*oYaw = euler.z();
	*oRoll = euler.x();
}

void GLCamera::setViewMatrix(QMatrix4x4 mat)
{
	_viewMatrix = mat;
}

void GLCamera::setProjectionMatrix(QMatrix4x4 mat)
{
	_projectionMatrix = mat;
}

void GLCamera::computeStereoViewProjectionMatrices(int width, int height, float IOD, float depthZ, bool left_eye)
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
QQuaternion GLCamera::quaternionFromMatrix(QMatrix4x4 m)
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

void GLCamera::quatToEuler(const QQuaternion& quat, float *rotx, float *roty, float *rotz)
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
