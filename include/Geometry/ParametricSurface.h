#pragma once

#include "IParametricSurface.h"
#include "GridMesh.h"

// NOTE: ParametricSurface is pure geometry (GridMesh : MeshGeometry).
// Concrete subclasses that render must wrap this in a RenderableMesh and call
// uploadGeometry(*this) after buildMesh() to upload geometry to the GPU.
class ParametricSurface : public GridMesh, public IParametricSurface
{
public:
    ParametricSurface(unsigned int nSlices, unsigned int nStacks,
                      unsigned int sMax = 1, unsigned int tMax = 1);
    virtual ~ParametricSurface();

    virtual Point pointAtParameter(const float& u, const float& v) = 0;
    virtual QVector3D normalAtParameter(const float& u, const float& v);

    // Fills geometry vectors (_points, _normals, _texCoords, _tangents, _bitangents).
    // Caller must invoke RenderableMesh::uploadGeometry(*this) afterward to upload.
    void buildMesh();

    float getSlices() const { return _slices; }
    float getStacks() const { return _stacks; }

protected:
    QVector3D _tangent;
    QVector3D _bitangent;
};
