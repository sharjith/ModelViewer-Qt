#pragma once

#include <vector>
#include "TriangleMesh.h"

class GridMesh : public TriangleMesh
{
public:
	GridMesh(QOpenGLShaderProgram* prog, const QString name, unsigned int slices, unsigned int stacks);
    virtual ~GridMesh();

protected:
	unsigned int _slices;
	unsigned int _stacks;
};
