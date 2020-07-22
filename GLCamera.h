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
 *            GLCamera.h
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

#ifndef _GLCAMERA_H
#define _GLCAMERA_H

#ifdef WIN32
#include <windows.h>
#endif

#include <QMatrix4x4>
#include <QQuaternion>

#include <iostream>
using namespace std;

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define PIdiv180 (PI/180.0)

/////////////////////////////////
//Note: All angles in degrees  //
/////////////////////////////////

// CGLCamera

class GLCamera
{
public:
    enum ViewProjection
    {
        TOP_VIEW	= 0,
        BOTTOM_VIEW	= ( TOP_VIEW + 1 ) ,
        FRONT_VIEW	= ( BOTTOM_VIEW + 1 ) ,
        REAR_VIEW	= ( FRONT_VIEW + 1 ) ,
        LEFT_VIEW	= ( REAR_VIEW + 1 ) ,
        RIGHT_VIEW	= ( LEFT_VIEW + 1 ) ,
        NE_ISOMETRIC_VIEW	= ( RIGHT_VIEW + 1 ) ,
        SE_ISOMETRIC_VIEW	= ( NE_ISOMETRIC_VIEW + 1 ) ,
        NW_ISOMETRIC_VIEW	= ( SE_ISOMETRIC_VIEW + 1 ) ,
        SW_ISOMETRIC_VIEW	= ( NW_ISOMETRIC_VIEW + 1 ) ,
        DIMETRIC_VIEW	= ( SW_ISOMETRIC_VIEW + 1 ) ,
        TRIMETRIC_VIEW	= ( DIMETRIC_VIEW + 1 )
    };

	enum ProjectionType
	{
		ORTHOGRAPHIC = 0,
		PERSPECTIVE = (ORTHOGRAPHIC + 1)
	};

    GLCamera()
    {
        resetAll();
    }

	GLCamera(float width, float height, float range, float fov):_width(width),_height(height),_viewRange(range),_FOV(fov)
	{
		resetAll();
		updateProjectionMatrix();
	}

	void setScreenSize(float w, float h)
	{
		_width = w;
		_height = h;
		updateProjectionMatrix();
	}

	void setFOV(float fov)
	{
		_FOV = fov;
		updateProjectionMatrix();
	}

	void setViewRange(float range)
	{
		_viewRange = range;
		updateProjectionMatrix();
	}

	void setProjection(ProjectionType proj)
	{
		_projectionType = proj;
		updateProjectionMatrix();
	}

    void resetAll(void);
    void updateViewMatrix(void);
	void updateProjectionMatrix(void);
   
    void rotateX(float iAngle);
    float getRotatedX() const { return _rotatedX; }
    void rotateY(float iAngle);
    float getRotatedY() const { return _rotatedY; }
    void rotateZ(float iAngle);
    float getRotatedZ() const { return _rotatedZ; }
	
    void move(float iDX, float iDY, float iDZ);
    void moveForward(float iDist);
    void moveUpward(float iDist);
    void moveAcross(float iDist);
    void setZoom(float iFactor);
    float getZoom() const { return _zoomValue; }
    void setView(ViewProjection iProj);
	void setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir);
	void setPosition(float iX, float iY, float iZ);
	void setPosition(QVector3D pos);

	QVector3D getViewDir()		const { return _viewDir; }
	QVector3D getRightVector() const { return _rightVector; }
	QVector3D getUpVector()	const { return _upVector; }
	QVector3D getPosition()	const { return _position; }

	void setViewMatrix(QMatrix4x4 mat);
	QMatrix4x4 getViewMatrix() const { return _viewMatrix; }

	void computeStereoViewProjectionMatrices(int width, int height, float IOD, float depthZ, bool left_eye);

	void setProjectionMatrix(QMatrix4x4 mat);
	QMatrix4x4 getProjectionMatrix() const { return _projectionMatrix; }

    void getRotationAngles(float* oPitch, float* oYaw, float* oRoll);

    /*static QQuaternion quaternionFromMatrix(QMatrix4x4 m);
    static void quatToEuler(const QQuaternion& quat, float *rotx,  float *roty, float *rotz);*/

private:

    QVector3D _viewDir;
	QVector3D _rightVector;
	QVector3D _upVector;
	QVector3D _position;

	float _width;
	float _height;
	float _viewRange;
	float _FOV;
    float _rotatedX, _rotatedY, _rotatedZ, _zoomValue;
    ViewProjection _viewProj;
	ProjectionType _projectionType;

	QMatrix4x4 _projectionMatrix;
	QMatrix4x4 _viewMatrix;
};


#endif /* _GLCAMERA_H */
