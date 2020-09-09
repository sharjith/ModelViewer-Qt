#include "Cylinder.h"

#include <cstdio>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

Cylinder::Cylinder(QOpenGLShaderProgram* prog, float radius, float height, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) : QuadMesh(prog, "Cylinder", nSlices, nStacks)
{
    _sMax = sMax;
    _tMax = tMax;
	int nVerts = ((nSlices + 1) * (nStacks + 1)) + (nSlices * 2) + 4;
	int elements = ((nSlices * 2 * (nStacks)) * 3) + (nSlices * 2) * 3;

	// Verts
	std::vector<float> p(3 * nVerts);
	// Normals
	std::vector<float> n(3 * nVerts);
	// Tex coords
	std::vector<float> tex(2 * nVerts);
	// Elements
	std::vector<unsigned int> el(elements);

	// Generate positions and normals
	float theta, phi;
	float thetaFac = glm::two_pi<float>() / nSlices;
	float phiFac = 1.0f / nStacks;
	float nx, ny, nz, s, t;
	unsigned int idx = 0, tIdx = 0;
	for (unsigned int i = 0; i <= nSlices; i++)
	{
		theta = i * thetaFac;
        s = (float)i / nSlices * _sMax;
		for (unsigned int j = 0; j <= nStacks; j++)
		{
			phi = j * phiFac;
            t = (float)j / nStacks * _tMax;
			nx = cosf(theta);
			ny = sinf(theta);
			nz = (phi);
			p[idx] = radius * nx;
			p[idx + 1] = radius * ny;
			p[idx + 2] = height * nz - height / 2.0f;
			glm::vec3 o(0, 0, (nz * height));
			glm::vec3 v((nx * radius), (ny * radius), (nz * height) - height / 2.0f);
			glm::vec3 normal = v - o;
			normal = glm::normalize(normal);
			normal = -normal;
			n[idx] = normal.x;
			n[idx + 1] = normal.y;
			n[idx + 2] = normal.z;
			idx += 3;

			tex[tIdx] = s;
			tex[tIdx + 1] = t;
			tIdx += 2;
		}
	}

	// bottom face
	for (unsigned int i = 0; i <= nSlices; i++)
	{
		theta = i * thetaFac;
        s = (float)i / nSlices;
		nx = cosf(theta);
		ny = sinf(theta);
		nz = 0;

		p[idx] = radius * nx;
		p[idx + 1] = radius * ny;
		p[idx + 2] = nz - height / 2.0f;
		n[idx] = 0;
		n[idx + 1] = 0;
		n[idx + 2] = -1.0f;
		idx += 3;
		s = (-nx + 1.0f) * 0.5f;
		t = (ny + 1.0f) * 0.5f;
		tex[tIdx] = s;
		tex[tIdx + 1] = t;
		tIdx += 2;
	}

	// bottom center
	p[idx] = 0;
	p[idx + 1] = 0;
	p[idx + 2] = -height / 2.0f;
	n[idx] = 0;
	n[idx + 1] = 0;
	n[idx + 2] = -1.0f;
	idx += 3;
	tex[tIdx] = 0.5;
	tex[tIdx + 1] = 0.5;
	tIdx += 2;

	// top face
	for (unsigned int i = 0; i <= nSlices; i++)
	{
		theta = i * thetaFac;
        s = (float)i / nSlices;
		nx = cosf(theta);
		ny = sinf(theta);
		nz = height;

		p[idx] = radius * nx;
		p[idx + 1] = radius * ny;
		p[idx + 2] = nz - height / 2.0f;
		n[idx] = 0;
		n[idx + 1] = 0;
		n[idx + 2] = 1.0f;
		idx += 3;
		s = (nx + 1.0f) * 0.5f;
		t = (ny + 1.0f) * 0.5f;
		tex[tIdx] = s;
		tex[tIdx + 1] = t;
		tIdx += 2;
	}

	// top center
	p[idx] = 0;
	p[idx + 1] = 0;
	p[idx + 2] = height / 2;
	n[idx] = 0;
	n[idx + 1] = 0;
	n[idx + 2] = 1.0f;
	idx += 3;
	tex[tIdx] = 0.5;
	tex[tIdx + 1] = 0.5;
	tIdx += 2;

	// Generate the element list
	// Body
	idx = 0;
	/*for (unsigned int i = 0; i < nSlices; i++)
	{
		unsigned int stackStart = i * (nStacks + 1);
		unsigned int nextStackStart = (i + 1) * (nStacks + 1);
		for (unsigned int j = 0; j < nStacks; j++)
		{
			el[idx + 0] = stackStart + j;
			el[idx + 1] = stackStart + j + 1;
			el[idx + 2] = nextStackStart + j + 1;
			el[idx + 3] = nextStackStart + j;
			el[idx + 4] = stackStart + j;
			el[idx + 5] = nextStackStart + j + 1;
			idx += 6;
		}
	}*/
	for (unsigned int i = 0; i < nSlices; i++)
	{
		unsigned int stackStart = i * (nStacks + 1);
		unsigned int nextStackStart = (i + 1) * (nStacks + 1);
		for (unsigned int j = 0; j < nStacks; j++)
		{
			el[idx + 0] = stackStart + j;
            el[idx + 1] = stackStart + j + 1;
            el[idx + 2] = nextStackStart + j + 1;
            el[idx + 3] = nextStackStart + j;
			//el[idx + 4] = stackStart + j;
			//el[idx + 5] = nextStackStart + j + 1;
			idx += 4;
		}
	}

	// Bottom face
	unsigned int j = ((nSlices + 1) * (nStacks + 1));
	for (unsigned int i = 0; i < nSlices; i++, j++)
	{
		el[idx + 0] = j;
		el[idx + 1] = ((nSlices + 1) * (nStacks + 1)) + nSlices + 1;
		el[idx + 2] = j + 1;
		el[idx + 3] = j;
		idx += 4;
	}

	// Top face
	j = ((nSlices + 1) * (nStacks + 1)) + (nSlices + 2);
	for (unsigned int i = 0; i < nSlices; i++, j++)
	{
		el[idx + 0] = j;
		el[idx + 1] = j + 1;
		el[idx + 2] = (((nSlices + 1) * (nStacks + 1)) + nSlices * 2) + 3;
		el[idx + 3] = j;
		idx += 4;
	}

	initBuffers(&el, &p, &n, &tex);
    computeBounds(p);
}
