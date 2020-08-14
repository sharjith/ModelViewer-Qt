#include "Plane.h"

#include <cstdio>
#include <cmath>


Plane::Plane(QOpenGLShaderProgram* prog, QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel, float smax, float tmax):
	TriangleMesh(prog, "Plane")
{
    buildMesh(center, xsize, ysize, xdivs, ydivs, zlevel, smax, tmax);
}

void Plane::setPlane(QOpenGLShaderProgram* prog, QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel, float smax, float tmax)
{
    setProg(prog);
    buildMesh(center, xsize, ysize, xdivs, ydivs, zlevel, smax, tmax);
}

void Plane::buildMesh(QVector3D center, float xsize, float ysize, int xdivs, int ydivs, float zlevel, float smax, float tmax)
{
    std::vector<float> p(3 * (xdivs + 1) * (ydivs + 1));
    std::vector<float> n(3 * (xdivs + 1) * (ydivs + 1));
    std::vector<float> tex(2 * (xdivs + 1) * (ydivs + 1));
    std::vector<unsigned int> el(6 * xdivs * ydivs);

    float x2 = xsize / 2.0f;
    float y2 = ysize / 2.0f;
    float iFactor = (float)ysize / ydivs;
    float jFactor = (float)xsize / xdivs;
    float texi = smax / ydivs;
    float texj = tmax / xdivs;
    float x, y;
    int vidx = 0, tidx = 0;
    for( int i = 0; i <= ydivs; i++ ) {
        y = iFactor * i - y2;
        for( int j = 0; j <= xdivs; j++ ) {
            x = jFactor * j - x2;
            p[vidx] = center.x() + x;
            p[vidx+1] = center.y() + y;
            p[vidx+2] = zlevel;
            n[vidx] = 0.0f;
            n[vidx+1] = 0.0f;
            n[vidx+2] = -1.0f;

            tex[tidx] = j * texi;
            tex[tidx+1] = i * texj;

            vidx += 3;
            tidx += 2;
        }
    }

    unsigned int rowStart, nextRowStart;
    int idx = 0;
    for( int i = 0; i < ydivs; i++ ) {
        rowStart = (unsigned int)( i * (xdivs+1) );
        nextRowStart = (unsigned int)( (i+1) * (xdivs+1));
        for( int j = 0; j < xdivs; j++ ) {
            el[idx] = rowStart + j;
            el[idx+1] = nextRowStart + j;
            el[idx+2] = nextRowStart + j + 1;
            el[idx+3] = rowStart + j;
            el[idx+4] = nextRowStart + j + 1;
            el[idx+5] = rowStart + j + 1;
            idx += 6;
        }
    }

    initBuffers(&el, &p, &n, &tex);
}

