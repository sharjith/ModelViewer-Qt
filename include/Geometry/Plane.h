#pragma once

#include "MeshGeometry.h"
#include <QVector3D>

class Plane : public MeshGeometry
{
public:
    enum class Orientation
    {
        XY_ZNormal,
        XZ_YNormal
    };

    Plane(QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
          float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f,
          Orientation orientation = Orientation::XY_ZNormal);

    // Rebuild geometry in-place and mark vectors dirty (no GPU upload).
    void setPlane(QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                  float zlevel = 0.0f, float smax = 1.0f, float tmax = 1.0f,
                  Orientation orientation = Orientation::XY_ZNormal);

    // Geometry parameter accessors (used by PlaneRenderable::clone())
    QVector3D   center()      const { return _center; }
    float       xSize()       const { return _xSize; }
    float       ySize()       const { return _ySize; }
    int         xDivs()       const { return _xDivs; }
    int         yDivs()       const { return _yDivs; }
    float       zLevel()      const { return _zLevel; }
    Orientation orientation() const { return _orientation; }

protected:
    QVector3D   _center;
    float       _xSize;
    float       _ySize;
    int         _xDivs;
    int         _yDivs;
    float       _zLevel;
    Orientation _orientation;

private:
    void buildMesh(QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                   float zlevel, float smax, float tmax, Orientation orientation);
};
