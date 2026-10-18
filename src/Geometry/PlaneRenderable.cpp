#include "PlaneRenderable.h"

PlaneRenderable::PlaneRenderable(QOpenGLShaderProgram* prog,
                                 QVector3D center, float xsize, float ysize,
                                 int xdivs, int ydivs,
                                 float zlevel, float smax, float tmax,
                                 Plane::Orientation orientation)
    : RenderableMesh(prog, "Plane"),
      _plane(center, xsize, ysize, xdivs, ydivs, zlevel, smax, tmax, orientation)
{
    uploadGeometry(_plane);
}

void PlaneRenderable::setPlane(QOpenGLShaderProgram* prog,
                               QVector3D center, float xsize, float ysize,
                               int xdivs, int ydivs,
                               float zlevel, float smax, float tmax,
                               Plane::Orientation orientation)
{
    setProg(prog);
    _plane.setPlane(center, xsize, ysize, xdivs, ydivs, zlevel, smax, tmax, orientation);
    uploadGeometry(_plane);
}

RenderableMesh* PlaneRenderable::clone()
{
    return new PlaneRenderable(_prog,
                               _plane.center(), _plane.xSize(), _plane.ySize(),
                               _plane.xDivs(), _plane.yDivs(),
                               _plane.zLevel(), _plane.sMax(), _plane.tMax(),
                               _plane.orientation());
}
