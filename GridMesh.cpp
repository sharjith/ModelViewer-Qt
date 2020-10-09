#include "GridMesh.h"

GridMesh::GridMesh(QOpenGLShaderProgram* prog, const QString name, unsigned int slices, unsigned int stacks) : TriangleMesh(prog, name),
_slices(slices),
_stacks(stacks)
{
}

GridMesh::~GridMesh()
{
}
