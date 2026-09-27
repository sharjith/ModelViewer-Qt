#pragma once

#include "GridMesh.h"

class Cube : public GridMesh
{
public:
    explicit Cube(float size = 1.0f);

    void setSize(float size);

    float size() const { return _size; }

protected:
    float _size;
};
