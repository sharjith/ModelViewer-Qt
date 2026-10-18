#pragma once

#include "MeshGeometry.h"

class GridMesh : public MeshGeometry
{
public:
    GridMesh(unsigned int slices, unsigned int stacks);
    virtual ~GridMesh() = default;

    unsigned int slices() const { return _slices; }
    unsigned int stacks() const { return _stacks; }

protected:
    unsigned int _slices;
    unsigned int _stacks;
};
