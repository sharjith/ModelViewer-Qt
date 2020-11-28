// Triangles that test ray-triangle intersections using Baldwin and Weber's
// precomputed global-to-barycentric coordinate transformation method with the
// transformation matrix as 9 coefficients and a column indicator. These triangles
// are intended for use in ray-triangle intersection timing experiments. The
// global-to-barycentric transformation matrix for the Baldwin and Weber method
// only has 9 coefficients that aren't known a priori, and one column whose contents
// are always (0,0,1,0) but whose position can vary. This triangle class stores
// just the 9 unknown coefficients and the number (1, 2, or 3) of the fixed column,
// providing the most space-efficient possible implementation of the Baldwin &
// Weber algorithm.

// Copyright (C) 2016 by Doug Baldwin.
// This work is licensed under a Creative Commons Attribution-ShareAlike 4.0 International
// License (http://creativecommons.org/licenses/by-sa/4.0/).

// Code altered to Qt by Sharjith N - sharjith@gmail.com

#ifndef TRIANGLEBALDWINWEBER_H
#define TRIANGLEBALDWINWEBER_H

#include "Triangle.h"

class TriangleBaldwinWeber : public Triangle
{
public:
	TriangleBaldwinWeber(const QVector3D& vertex1, const QVector3D& vertex2, const QVector3D& vertex3, QObject* parent = nullptr);
	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

private:
	float _transformation[9];
	unsigned char _fixedColumn;
};

#endif // TRIANGLEBALDWINWEBER_H
