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
  *            BoundingBox.h
  *
  *  Thu Jun 29 22:07:12 2006
  *  Copyright  2006  N. Sharjith
  *  sharjith@gmail.com
  ****************************************************************************/

#pragma once

#include <QRect>
#include <QMatrix4x4>

class Point;

// Minimum gap a Min/Max face pair must keep - prevents dragging or typing a
// face past its partner into a degenerate/inverted box. Shared by Filter by
// Bounding Box and the Clipping Planes editor's box mode.
constexpr double kMinBoxGap = 1.0e-3;

class BoundingBox
{
public:
	BoundingBox();
	BoundingBox(const double& xMin, const double& xMax, const double& yMin, const double& yMax, const double& zMin, const double& zMax);
	virtual ~BoundingBox();
	void setLimits(const double& xMin, const double& xMax, const double& yMin, const double& yMax, const double& zMin, const double& zMax);
	void getLimits(double& xMin, double& xMax, double& yMin, double& yMax, double& zMin, double& zMax);
	inline double xMin() const { return _xMin; }
	inline double xMax() const { return _xMax; }
	inline double yMin() const { return _yMin; }
	inline double yMax() const { return _yMax; }
	inline double zMin() const { return _zMin; }
	inline double zMax() const { return _zMax; }
	inline double getXSize() const { return std::abs(_xMax - _xMin); }
	inline double getYSize() const { return std::abs(_yMax - _yMin); }
	inline double getZSize() const { return std::abs(_zMax - _zMin); }
	inline double getMaxDimension() const	{ return std::max({ getXSize(), getYSize(), getZSize() }); }
	inline std::vector<QVector3D> getCorners() const
	{
		return {
			{static_cast<float>(_xMin), static_cast<float>(_yMin), static_cast<float>(_zMin)},
			{static_cast<float>(_xMin), static_cast<float>(_yMin), static_cast<float>(_zMax)},
			{static_cast<float>(_xMin), static_cast<float>(_yMax), static_cast<float>(_zMin)},
			{static_cast<float>(_xMin), static_cast<float>(_yMax), static_cast<float>(_zMax)},
			{static_cast<float>(_xMax), static_cast<float>(_yMin), static_cast<float>(_zMin)},
			{static_cast<float>(_xMax), static_cast<float>(_yMin), static_cast<float>(_zMax)},
			{static_cast<float>(_xMax), static_cast<float>(_yMax), static_cast<float>(_zMin)},
			{static_cast<float>(_xMax), static_cast<float>(_yMax), static_cast<float>(_zMax)}
		};
	}

	Point center() const;
	Point extent() const;
	std::vector<Point> corners() const;
	double boundingRadius() const;
	bool contains(const Point& P) const;

	// True iff `other` is fully enclosed by this box on all three axes
	// ("Window"-style containment - see FilterByBoundingBoxDialog).
	bool contains(const BoundingBox& other) const;

	// True iff this box and `other` overlap on all three axes at all
	// ("Crossing"-style containment). Standard six-comparison AABB overlap
	// test - the two boxes are disjoint iff they're separated along ANY
	// single axis, so overlap requires no separation on any of the three.
	bool intersects(const BoundingBox& other) const;

	void addBox(const BoundingBox&);
	QRect project(const QMatrix4x4& modelView, const QMatrix4x4& projection, const QRect& viewport, const QRect& window);
private:
	double _xMin;
	double _xMax;
	double _yMin;
	double _yMax;
	double _zMin;
	double _zMax;
};
