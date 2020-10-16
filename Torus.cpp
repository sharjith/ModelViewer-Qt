#include "Torus.h"
#include <cstdio>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/glm.hpp>

Torus::Torus(QOpenGLShaderProgram* prog, float outerRadius, float innerRadius, unsigned int nsides, unsigned int nrings, unsigned int sMax, unsigned int tMax) :GridMesh(prog, "Torus", nsides, nrings),
_innerRadius(innerRadius),
_outerRadius(outerRadius)
{
	_sMax = sMax;
	_tMax = tMax;
	unsigned int faces = nsides * nrings;
	int nVerts = nsides * (nrings + 1);   // One extra ring to duplicate first ring

	// Points
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
	std::vector<unsigned int> el(6 * faces);

	// Generate the vertex data
	float ringFactor = glm::two_pi<float>() / nrings;
	float sideFactor = glm::two_pi<float>() / nsides;
	int idx = 0, tidx = 0;
	for (unsigned int ring = 0; ring <= nrings; ring++) {
		float u = ring * ringFactor;
		float cu = cos(u);
		float su = sin(u);
		for (unsigned int side = 0; side < nsides; side++) {
			float v = side * sideFactor;
			float cv = cos(v);
			float sv = sin(v);
			float r = (_outerRadius + _innerRadius * cv);
			p[idx] = r * cu;
			p[idx + 1] = r * su;
			p[idx + 2] = _innerRadius * sv;
			n[idx] = cv * cu * r;
			n[idx + 1] = cv * su * r;
			n[idx + 2] = sv * r;
			tex[tidx] = u / glm::two_pi<float>() * _sMax;
			tex[tidx + 1] = v / glm::two_pi<float>() * _tMax;
			tidx += 2;
			// Normalize
			float len = sqrt(n[idx] * n[idx] +
				n[idx + 1] * n[idx + 1] +
				n[idx + 2] * n[idx + 2]);
			n[idx] /= len;
			n[idx + 1] /= len;
			n[idx + 2] /= len;
			float cudu = cos(u + 0.0001f);
			float sudu = sin(u + 0.0001f);
			float cvdv = cos(v + 0.0001f);
			float svdv = sin(v) + 0.001f;
			float dr = (outerRadius + innerRadius * cvdv);
			float dx = dr * cudu;
			float dy = dr * sudu;
			float dz = innerRadius * svdv;
			glm::vec3 tgt = glm::vec3(dx, dy, dz) - glm::vec3(p[idx], p[idx + 1], p[idx + 2]);
			glm::vec3 btg = glm::cross(glm::vec3(n[idx], n[idx + 1], n[idx + 2]), tgt);
			tg[idx] = tgt.x; tg[idx + 1] = tgt.y; tg[idx + 2] = tgt.z;
			bt[idx] = btg.x; bt[idx + 1] = btg.y; bt[idx + 2] = btg.z;
			idx += 3;
		}
	}

	idx = 0;
	for (unsigned int ring = 0; ring < nrings; ring++)
	{
		unsigned int ringStart = ring * nsides;
		unsigned int nextRingStart = (ring + 1) * nsides;
		for (unsigned int side = 0; side < nsides; side++)
		{
			int nextSide = (side + 1) % nsides;
			// The quad
			el[idx] = (ringStart + side);
			el[idx + 1] = (nextRingStart + side);
			el[idx + 2] = (nextRingStart + nextSide);
			el[idx + 3] = ringStart + side;
			el[idx + 4] = nextRingStart + nextSide;
			el[idx + 5] = (ringStart + nextSide);
			idx += 6;
		}
	}

	initBuffers(&el, &p, &n, &tex, &tg, &bt);
	computeBounds();
}

TriangleMesh* Torus::clone()
{
	return new Torus(_prog, _outerRadius, _innerRadius, _slices, _stacks, _sMax, _tMax);
}
