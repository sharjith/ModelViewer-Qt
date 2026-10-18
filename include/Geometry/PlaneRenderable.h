#pragma once

#include "RenderableMesh.h"
#include "Plane.h"

// RenderableMesh wrapper around Plane geometry.
// Exposes setPlane() so callers can rebuild geometry and re-upload to the GPU
// without touching the raw MeshGeometry interface.
class PlaneRenderable : public RenderableMesh
{
public:
    PlaneRenderable(QOpenGLShaderProgram* prog,
                    QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                    float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f,
                    Plane::Orientation orientation = Plane::Orientation::XY_ZNormal);

    // Rebuild the plane geometry and re-upload to the GPU.
    void setPlane(QOpenGLShaderProgram* prog,
                  QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                  float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f,
                  Plane::Orientation orientation = Plane::Orientation::XY_ZNormal);

    RenderableMesh* clone() override;

protected:
    Plane _plane;
};
