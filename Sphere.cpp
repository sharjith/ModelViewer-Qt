#include "Sphere.h"

#include <cstdio>
#include <cmath>

#include <glm/gtc/constants.hpp>

Sphere::Sphere(QOpenGLShaderProgram* prog, float radius, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) : GridMesh(prog, "Sphere", nSlices, nStacks)
{
	_sMax = sMax;
	_tMax = tMax;
	int nVerts = (nSlices + 1) * (nStacks + 1);
	int elements = (nSlices * 2 * (nStacks - 1)) * 3;

	// Verts
	std::vector<float> p(3 * nVerts);
	// Normals
	std::vector<float> n(3 * nVerts);
	// Tangents
	std::vector<float> tg(3 * nVerts);
	// Bitangents
	std::vector<float> bt(3 * nVerts);
	// Tex coords
	std::vector<float> tex(2 * nVerts);
	// Elements
	std::vector<unsigned int> el(elements);

	// Generate positions and normals
	float theta, phi;
	float thetaFac = glm::two_pi<float>() / nSlices;
	float phiFac = glm::pi<float>() / nStacks;
	float nx, ny, nz, tx, ty, tz, s, t;
	unsigned int idx = 0, tIdx = 0;
	for (unsigned int i = 0; i <= nSlices; i++) {
		theta = i * thetaFac;
		s = (float)i / nSlices * _sMax;
		for (unsigned int j = 0; j <= nStacks; j++) {
			phi = j * phiFac;
			t = (float)j / nStacks * _tMax;
			nx = sinf(phi) * cosf(theta);
			ny = sinf(phi) * sinf(theta);
			nz = cosf(phi);
			tx = -sinf(phi);
			ty = 0;
			tz = cos(phi);
			QVector3D bi = QVector3D::crossProduct(QVector3D(nx, ny, nz), QVector3D(tx, ty, tz));
			p[idx] = radius * nx; p[idx + 1] = radius * ny; p[idx + 2] = radius * nz;
			n[idx] = nx; n[idx + 1] = ny; n[idx + 2] = nz;
			tg[idx] = tx; tg[idx + 1] = ty; tg[idx + 2] = tz;
			bt[idx] = bi.x(); bt[idx + 1] = bi.y(); bt[idx + 2] = bi.z();
			idx += 3;

			tex[tIdx] = s;
			tex[tIdx + 1] = t;
			tIdx += 2;
		}
	}

	// Generate the element list
	idx = 0;
    for( unsigned int i = 0; i < nSlices; i++ ) {
		unsigned int stackStart = i * (nStacks + 1);
		unsigned int nextStackStart = (i+1) * (nStacks+1);
		for( unsigned int j = 0; j < nStacks; j++ ) {
			if( j == 0 ) {
				el[idx] = stackStart;
				el[idx+1] = stackStart + 1;
				el[idx+2] = nextStackStart + 1;
				idx += 3;
			} else if( j == nStacks - 1) {
				el[idx] = stackStart + j;
				el[idx+1] = stackStart + j + 1;
				el[idx+2] = nextStackStart + j;
				idx += 3;
			} else {
				el[idx] = stackStart + j;
				el[idx+1] = stackStart + j + 1;
				el[idx+2] = nextStackStart + j + 1;
				el[idx+3] = nextStackStart + j;
				el[idx+4] = stackStart + j;
				el[idx+5] = nextStackStart + j + 1;
				idx += 6;
			}
		}
    }
    /*for (unsigned int i = 0; i < nSlices; i++) {
		unsigned int stackStart = i * (nStacks + 1);
		unsigned int nextStackStart = (i + 1) * (nStacks + 1);
		for (unsigned int j = 0; j < nStacks; j++) {
			if (j == 0) {
				el[idx] = stackStart;
				el[idx + 1] = stackStart + 1;
				el[idx + 2] = nextStackStart + 1;
				el[idx + 3] = stackStart;
				idx += 4;
			}
			else if (j == nStacks - 1) {
				el[idx] = stackStart + j;
				el[idx + 1] = stackStart + j + 1;
				el[idx + 2] = nextStackStart + j;
				el[idx + 3] = stackStart + j;
				idx += 4;
			}
			else {
				el[idx] = stackStart + j;
				el[idx + 1] = stackStart + j + 1;
				el[idx + 2] = nextStackStart + j + 1;
				el[idx + 3] = nextStackStart + j;
				//el[idx + 4] = stackStart + j;
				//el[idx + 5] = nextStackStart + j + 1;
				idx += 4;
			}
        }
    }*/

	initBuffers(&el, &p, &n, &tex, &tg, &bt);
	computeBounds(p);
}
