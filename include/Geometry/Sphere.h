#pragma once

#include "GridMesh.h"

class Sphere : public GridMesh
{
public:
    Sphere(float rad, unsigned int sl, unsigned int st,
           unsigned int sMax = 1, unsigned int tMax = 1);

    float radius() const { return _radius; }

protected:
    float _radius;
};
