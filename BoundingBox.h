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


#ifndef _BOUNDINGBOX_H
#define _BOUNDINGBOX_H

class Point;

class BoundingBox
{
public:
    BoundingBox();
    BoundingBox(const double&, const double&, const double&, const double&, const double&, const double&);
    virtual ~BoundingBox();
    void setLimits(const double&, const double&, const double&, const double&, const double&, const double&);
    void getLimits(double& xMin, double& xMax, double& yMin, double& yMax, double& zMin, double& zMax);
    inline double xMax() const { return _xMax; }
    inline double xMin() const { return _xMin; }
    inline double yMax() const { return _yMax; }
    inline double yMin() const { return _yMin; }
    inline double zMax() const { return _zMax; }
    inline double zMin() const { return _zMin; }
    Point center() const;
	double BoundingRadius() const;
    bool contains(const Point& P) const;
    void addBox(const BoundingBox&);
private:
    double _xMax;
    double _xMin;
    double _yMax;
    double _yMin;
    double _zMax;
    double _zMin;
};

#endif /* _BOUNDINGBOX_H */
