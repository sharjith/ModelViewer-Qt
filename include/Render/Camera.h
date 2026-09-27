/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

 /***************************************************************************
  *            Camera.h
  *
  *  Thu Jun 29 22:46:35 2006
  *  Copyright  2006  N. Sharjith
  *  sharjith@gmail.com
  ****************************************************************************/

  /**********************************************************************

  //Part of this code is taken from "camera" tutorial written by Philipp Crocoll
  //Contact:
  //philipp.crocoll@web.de
  //www.codecolony.de
   **********************************************************************/

#pragma once

#ifdef WIN32
#include <windows.h>
#endif

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector2D>

#include <iostream>


#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define PIdiv180 (PI/180.0)

/////////////////////////////////
//Note: All angles in degrees  //
/////////////////////////////////

// CCamera

class Camera
{
public:
	enum class ViewProjection
	{
		TOP_VIEW = 0,
		BOTTOM_VIEW = (TOP_VIEW + 1),
		FRONT_VIEW = (BOTTOM_VIEW + 1),
		REAR_VIEW = (FRONT_VIEW + 1),
		LEFT_VIEW = (REAR_VIEW + 1),
		RIGHT_VIEW = (LEFT_VIEW + 1),
		NE_ISOMETRIC_VIEW = (RIGHT_VIEW + 1),
		SE_ISOMETRIC_VIEW = (NE_ISOMETRIC_VIEW + 1),
		NW_ISOMETRIC_VIEW = (SE_ISOMETRIC_VIEW + 1),
		SW_ISOMETRIC_VIEW = (NW_ISOMETRIC_VIEW + 1),
		DIMETRIC_VIEW = (SW_ISOMETRIC_VIEW + 1),
		TRIMETRIC_VIEW = (DIMETRIC_VIEW + 1)
	};

	enum class ProjectionType
	{
		ORTHOGRAPHIC = 0,
		PERSPECTIVE = (ORTHOGRAPHIC + 1)
	};

	enum class CameraMode
	{
		Orbit,
		Fly,
		FirstPerson
	};

	Camera();
	Camera(float width, float height, float range, float fov);

	void setScreenSize(float w, float h);
	QPoint getScreenSize() const;

	float getAspectRatio() const;

	void setFOV(float fov);
	float getFOV() const;

	void setViewRange(float range);
	float getViewRange() const;

	void setSceneRadius(float radius);
	float getSceneRadius() const;

	void setProjectionType(ProjectionType proj);
	ProjectionType getProjectionType() const;

	// Oblique (Cavalier/Cabinet) parallel projection: the orthographic projection sheared so that depth
	// recedes along a slanted axis. depthScale is rho (1 = Cavalier, 0.5 = Cabinet; 0 turns the shear
	// off) and angleDegrees is the screen angle of the receding axis from screen-right. Only applies to
	// the orthographic projection of an Orbit camera; the shear pivots on the orbit target so it stays
	// at the centre of the view.
	void setOblique(float depthScale, float angleDegrees);
	bool isOblique() const { return _obliqueDepthScale > 0.0f; }
	// Screen-space shift (view-space x, y units) of a point per unit of distance BEHIND the orbit target.
	// Zero when not oblique; receding points shift up and to the right.
	QVector2D getObliqueShift() const;

	void resetAll(void);
	void updateViewMatrix(void);
	void updateProjectionMatrix(void);

	void rotateX(float iAngle);
	float getRotatedX() const { return _rotatedX; }
	void rotateY(float iAngle);
	float getRotatedY() const { return _rotatedY; }
	void rotateZ(float iAngle);
	float getRotatedZ() const { return _rotatedZ; }

	float& getYaw()   { return _yaw; }
	float& getPitch() { return _pitch; }

	void move(float iDX, float iDY, float iDZ);
	void moveForward(float iDist);
	void moveForwardPlanar(float iDist);
	void moveUpward(float iDist);
	void moveWorldUp(float iDist);
	void moveAcross(float iDist);
	void moveAcrossPlanar(float iDist);
	void setZoom(float iFactor);
	float getZoom() const { return _zoomValue; }
	void setView(ViewProjection iProj);
	void setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir);
	void setWorldUpVector(const QVector3D& upVector);
	void setPosition(float iX, float iY, float iZ);
	void setPosition(QVector3D pos);

	void updateFlyView();

	QVector3D getViewDir()		const { return _viewDir; }
	QVector3D getRightVector() const { return _rightVector; }
	QVector3D getUpVector()	const { return _upVector; }
	QVector3D getWorldUpVector() const { return _worldUpVector; }
	QVector3D getPosition()	const { return _position; }
	QVector3D getRenderPosition() const;
	float getOrbitDistance() const;
	float getOrthoViewDistance() const;

	void setViewMatrix(QMatrix4x4 mat);
	QMatrix4x4 getViewMatrix() const { return _viewMatrix; }

	float computeViewShift(float fovYDegrees, float viewRange, float margin, float maxShiftFactor) const;
	float getShift() const;

	void computeStereoViewProjectionMatrices(int width, int height, float IOD, float depthZ, bool left_eye);

	void setProjectionMatrix(QMatrix4x4 mat);
	QMatrix4x4 getProjectionMatrix() const { return _projectionMatrix; }

	void getRotationAngles(float* oPitch, float* oYaw, float* oRoll);

	void setMode(CameraMode mode);
	CameraMode getMode() const { return _cameraMode; }

	void setYawPitchFromViewDir();

	/*static QQuaternion quaternionFromMatrix(QMatrix4x4 m);
	static void quatToEuler(const QQuaternion& quat, float *rotx,  float *roty, float *rotz);*/

private:

	QVector3D _viewDir;
	QVector3D _rightVector;
	QVector3D _upVector;
	QVector3D _worldUpVector;
	QVector3D _position;

	float _width;
	float _height;
	float _viewRange;
	float _sceneRadius;  // bounding sphere radius of the loaded scene; used to
	                     // floor the perspective far plane so it always covers
	                     // the full scene regardless of zoom depth
	float _FOV;
	float _rotatedX, _rotatedY, _rotatedZ, _zoomValue;
	ViewProjection _viewProj;
	ProjectionType _projectionType;
	ProjectionType _previousProjection;
	float _obliqueDepthScale = 0.0f;   // rho; 0 = plain orthographic
	float _obliqueAngleDegrees = 45.0f;

	QMatrix4x4 _projectionMatrix;
	QMatrix4x4 _viewMatrix;

	float _yaw = -90.0f;  // Initialized to look along -Z
	float _pitch = 0.0f;  // Horizontal

	CameraMode _cameraMode = CameraMode::Orbit;

};
