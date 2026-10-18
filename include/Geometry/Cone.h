#pragma once

#include "GridMesh.h"

class Cone : public GridMesh
{
public:
    Cone(float rad, float height, unsigned int sl, unsigned int st,
         unsigned int sMax = 1, unsigned int tMax = 1);

    void setParameters(float rad, float height, unsigned int sl, unsigned int st,
                       unsigned int sMax = 1, unsigned int tMax = 1);

    float radius() const { return _radius; }
    float height() const { return _height; }

private:
    float _radius;
    float _height;
};
