#include "Plane.h"

#include <cmath>

Plane::Plane(QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
             float zlevel, float smax, float tmax, Orientation orientation)
    : _center(center), _xSize(xsize), _ySize(ysize), _xDivs(xdivs), _yDivs(ydivs),
      _zLevel(zlevel), _orientation(orientation)
{
    _sMax = smax;
    _tMax = tmax;
    buildMesh(center, xsize, ysize, xdivs, ydivs, zlevel, smax, tmax, orientation);
}

void Plane::setPlane(QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                     float zlevel, float smax, float tmax, Orientation orientation)
{
    const auto nearlyEqual = [](float a, float b) -> bool
    {
        const float scale = std::max(1.0f, std::max(std::abs(a), std::abs(b)));
        return std::abs(a - b) <= 1.0e-6f * scale;
    };
    if (nearlyEqual(center.x(), _center.x()) &&
        nearlyEqual(center.y(), _center.y()) &&
        nearlyEqual(center.z(), _center.z()) &&
        nearlyEqual(xsize, _xSize) &&
        nearlyEqual(ysize, _ySize) &&
        xdivs == _xDivs &&
        ydivs == _yDivs &&
        nearlyEqual(zlevel, _zLevel) &&
        nearlyEqual(smax, _sMax) &&
        nearlyEqual(tmax, _tMax) &&
        orientation == _orientation)
        return;

    _center = center;
    _xSize = xsize;
    _ySize = ysize;
    _xDivs = xdivs;
    _yDivs = ydivs;
    _zLevel = zlevel;
    _sMax = smax;
    _tMax = tmax;
    _orientation = orientation;
    buildMesh(_center, _xSize, _ySize, _xDivs, _yDivs, _zLevel, _sMax, _tMax, _orientation);
}

void Plane::buildMesh(QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                      float zlevel, float smax, float tmax, Orientation orientation)
{
    std::vector<float> p(3 * (xdivs + 1) * (ydivs + 1));
    std::vector<float> n(3 * (xdivs + 1) * (ydivs + 1));
    std::vector<float> tex(8 * (xdivs + 1) * (ydivs + 1));  // 4 UV sets × 2 floats
    std::vector<unsigned int> el(6 * xdivs * ydivs);

    float x2 = xsize / 2.0f;
    float y2 = ysize / 2.0f;
    float iFactor = (float)ysize / ydivs;
    float jFactor = (float)xsize / xdivs;
    float texi = smax / ydivs;
    float texj = tmax / xdivs;
    float x, y;
    int vidx = 0, tidx = 0;

    for (int i = 0; i <= ydivs; i++)
    {
        y = iFactor * i - y2;
        for (int j = 0; j <= xdivs; j++)
        {
            x = jFactor * j - x2;
            if (orientation == Orientation::XZ_YNormal)
            {
                p[vidx]     = center.x() + x;
                p[vidx + 1] = zlevel;
                p[vidx + 2] = center.z() + y;
                n[vidx]     = 0.0f;
                n[vidx + 1] = -1.0f;
                n[vidx + 2] = 0.0f;
            }
            else
            {
                p[vidx]     = center.x() + x;
                p[vidx + 1] = center.y() + y;
                p[vidx + 2] = zlevel;
                n[vidx]     = 0.0f;
                n[vidx + 1] = 0.0f;
                n[vidx + 2] = -1.0f;
            }

            float texS = j * texi;
            float texT = i * texj;
            for (int texIdx = 0; texIdx < 4; texIdx++)
            {
                tex[tidx + texIdx * 2]     = texS;
                tex[tidx + texIdx * 2 + 1] = texT;
            }

            vidx += 3;
            tidx += 8;
        }
    }

    unsigned int rowStart, nextRowStart;
    int idx = 0;
    for (int i = 0; i < ydivs; i++)
    {
        rowStart     = (unsigned int)(i * (xdivs + 1));
        nextRowStart = (unsigned int)((i + 1) * (xdivs + 1));
        for (int j = 0; j < xdivs; j++)
        {
            if (orientation == Orientation::XZ_YNormal)
            {
                el[idx]     = rowStart + j;
                el[idx + 1] = nextRowStart + j + 1;
                el[idx + 2] = nextRowStart + j;
                el[idx + 3] = rowStart + j;
                el[idx + 4] = rowStart + j + 1;
                el[idx + 5] = nextRowStart + j + 1;
            }
            else
            {
                el[idx]     = rowStart + j;
                el[idx + 1] = nextRowStart + j;
                el[idx + 2] = nextRowStart + j + 1;
                el[idx + 3] = rowStart + j;
                el[idx + 4] = nextRowStart + j + 1;
                el[idx + 5] = rowStart + j + 1;
            }
            idx += 6;
        }
    }

    _points   = std::move(p);
    _normals  = std::move(n);
    _texCoords = std::move(tex);
    _indices  = std::move(el);
}
