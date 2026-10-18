#include "Sphere.h"

#include <cmath>
#include <QVector3D>

#include <glm/gtc/constants.hpp>

Sphere::Sphere(float radius, unsigned int slices, unsigned int stacks,
               unsigned int sMax, unsigned int tMax)
    : GridMesh(slices, stacks), _radius(radius)
{
    _sMax = sMax;
    _tMax = tMax;

    int nVerts   = (_slices + 1) * (_stacks + 1);
    int elements = (_slices * 2 * (_stacks - 1)) * 3;

    std::vector<float> p(3 * nVerts);
    std::vector<float> n(3 * nVerts);
    std::vector<float> tg(3 * nVerts);
    std::vector<float> bt(3 * nVerts);
    std::vector<float> tex(8 * nVerts);   // 4 UV sets × 2 floats
    std::vector<unsigned int> el(elements);

    float theta, phi;
    float thetaFac = glm::two_pi<float>() / _slices;
    float phiFac   = glm::pi<float>() / _stacks;
    float nx, ny, nz, tx, ty, tz, s, t;
    unsigned int idx = 0, tIdx = 0;

    for (unsigned int i = 0; i <= _slices; i++)
    {
        theta = i * thetaFac;
        s = (float)i / _slices * _sMax;
        for (unsigned int j = 0; j <= _stacks; j++)
        {
            phi = j * phiFac;
            t   = (float)j / _stacks * _tMax;

            nx = sinf(phi) * cosf(theta);
            ny = sinf(phi) * sinf(theta);
            nz = cosf(phi);

            tx = -sinf(phi);
            ty = 0;
            tz = cosf(phi);

            QVector3D bi = QVector3D::crossProduct(QVector3D(nx, ny, nz), QVector3D(tx, ty, tz));

            p[idx]     = radius * nx;
            p[idx + 1] = radius * ny;
            p[idx + 2] = radius * nz;

            n[idx]     = nx;
            n[idx + 1] = ny;
            n[idx + 2] = nz;

            tg[idx]     = tx;
            tg[idx + 1] = ty;
            tg[idx + 2] = tz;

            bt[idx]     = bi.x();
            bt[idx + 1] = bi.y();
            bt[idx + 2] = bi.z();

            idx += 3;

            tex[tIdx + 0] = s; tex[tIdx + 1] = t;   // TEXCOORD_0
            tex[tIdx + 2] = s; tex[tIdx + 3] = t;   // TEXCOORD_1
            tex[tIdx + 4] = s; tex[tIdx + 5] = t;   // TEXCOORD_2
            tex[tIdx + 6] = s; tex[tIdx + 7] = t;   // TEXCOORD_3
            tIdx += 8;
        }
    }

    idx = 0;
    for (unsigned int i = 0; i < _slices; i++)
    {
        unsigned int stackStart     = i * (_stacks + 1);
        unsigned int nextStackStart = (i + 1) * (_stacks + 1);
        for (unsigned int j = 0; j < _stacks; j++)
        {
            if (j == 0)
            {
                el[idx]     = stackStart;
                el[idx + 1] = stackStart + 1;
                el[idx + 2] = nextStackStart + 1;
                idx += 3;
            }
            else if (j == _stacks - 1)
            {
                el[idx]     = stackStart + j;
                el[idx + 1] = stackStart + j + 1;
                el[idx + 2] = nextStackStart + j;
                idx += 3;
            }
            else
            {
                el[idx]     = stackStart + j;
                el[idx + 1] = stackStart + j + 1;
                el[idx + 2] = nextStackStart + j + 1;
                el[idx + 3] = nextStackStart + j;
                el[idx + 4] = stackStart + j;
                el[idx + 5] = nextStackStart + j + 1;
                idx += 6;
            }
        }
    }

    _points     = std::move(p);
    _normals    = std::move(n);
    _tangents   = std::move(tg);
    _bitangents = std::move(bt);
    _texCoords  = std::move(tex);
    _indices    = std::move(el);
}
