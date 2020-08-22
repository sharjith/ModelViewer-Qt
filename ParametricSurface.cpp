#include "ParametricSurface.h"
#include "Point.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vector_query.hpp>

#include <iostream>

ParametricSurface::ParametricSurface(QOpenGLShaderProgram* prog, unsigned int nSlices, unsigned int nStacks) :
	QuadMesh(prog, "Prametric Surface"),
	_slices(nSlices),
	_stacks(nStacks)
{
}

ParametricSurface::~ParametricSurface()
{
}

glm::vec3 ParametricSurface::normalAtParameter(const float& u, const float& v)
{
	float du = ((abs(firstUParameter()) + abs(lastUParameter())) / _slices) / 10.0f;
	float dv = ((abs(firstVParameter()) + abs(lastVParameter())) / _stacks) / 10.0f;

	Point o = pointAtParameter(u, v);
	Point uDir = pointAtParameter(u + du, v);
	Point vDir = pointAtParameter(u, v + dv);

	glm::vec3 oVec(o.getX(), o.getY(), o.getZ());
	glm::vec3 uVec(uDir.getX(), uDir.getY(), uDir.getZ());
	glm::vec3 vVec(vDir.getX(), vDir.getY(), vDir.getZ());

	glm::vec3 t1 = uVec - oVec;
	glm::vec3 t2 = vVec - oVec;

	if (glm::isNull(t1, glm::epsilon<float>()))
	{
		uDir = pointAtParameter(u + du, v);
		glm::vec3 uVec(uDir.getX(), uDir.getY(), uDir.getZ());
		uVec = -uVec;
		t1 = uVec - oVec;
	}
	if (glm::isNull(t2, glm::epsilon<float>()))
	{
		vDir = pointAtParameter(u, v + dv);
		glm::vec3 vVec(vDir.getX(), vDir.getY(), vDir.getZ());
		vVec = -vVec;
		t2 = vVec - oVec;
	}

	glm::vec3 normal = glm::cross(t2, t1);
	normal = glm::normalize(normal);

	if (glm::isNull(normal, glm::epsilon<float>()))
		return glm::vec3(0, 0, 1);

	return normal;
}

void ParametricSurface::buildMesh()
{
	int nVerts = ((_slices + 1) * (_stacks + 1));
	int elements = ((_slices * (_stacks)) * 4);

	// Verts
	std::vector<float> p(3 * nVerts);
	// Normals
	std::vector<float> n(3 * nVerts);
	// Tex coords
	std::vector<float> tex(2 * nVerts);
	// Elements
	std::vector<unsigned int> el(elements);

	// Generate positions and normals
	float u = firstUParameter(), v = firstVParameter();
	float uFac = abs(lastUParameter() - firstUParameter()) / _slices;
	float vFac = abs(lastVParameter() - firstVParameter()) / _stacks;
	float s, t;
	unsigned int idx = 0, tIdx = 0;
	for (unsigned int i = 0; i <= _slices; i++)
	{
		v = firstVParameter();
		s = (float)i / _slices;
		for (unsigned int j = 0; j <= _stacks; j++)
		{
			t = (float)j / _stacks;
			Point pt = pointAtParameter(u, v);
			p[idx] = pt.getX(); p[idx + 1] = pt.getY(); p[idx + 2] = pt.getZ();
			glm::vec3 normal = normalAtParameter(u, v);
			n[idx] = normal.x; n[idx + 1] = normal.y; n[idx + 2] = normal.z;
			idx += 3;

			tex[tIdx] = s;
			tex[tIdx + 1] = t;
			tIdx += 2;

			v += vFac;
		}
		u += uFac;
	}

	// Generate the element list
	// Body
	idx = 0;
	for (unsigned int i = 0; i < _slices; i++)
	{
		unsigned int stackStart = i * (_stacks + 1);
		unsigned int nextStackStart = (i + 1) * (_stacks + 1);
		for (unsigned int j = 0; j < _stacks; j++)
		{
			// For quad mesh
			el[idx + 0] = stackStart + j;
			el[idx + 1] = stackStart + j + 1;
			el[idx + 2] = nextStackStart + j + 1;
			el[idx + 3] = nextStackStart + j;

			idx += 4;
		}
	}

	initBuffers(&el, &p, &n, &tex);
	computeBoundingSphere(p);
}

bool ParametricSurface::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
	bool intersects = false;

	int offset = (_stacks + 1) * 3;
	for (size_t i = 0; i < _trsfpoints.size() - offset - 3; i++)
	{
		QVector3D v0(_trsfpoints[i + 0], _trsfpoints[i + 1], _trsfpoints[i + 2]);
		QVector3D v1(_trsfpoints[i + 3], _trsfpoints[i + 4], _trsfpoints[i + 5]);
		QVector3D v2(_trsfpoints[offset + i + 0], _trsfpoints[offset + i + 1], _trsfpoints[offset + i + 2]);
		intersects = rayIntersectsTriangle(rayPos, rayDir, v0, v1, v2, outIntersectionPoint);
		if (intersects)
			break;
	}

	return intersects;
}