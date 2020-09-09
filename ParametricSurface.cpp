#include "ParametricSurface.h"
#include "Point.h"
#include <iostream>

ParametricSurface::ParametricSurface(QOpenGLShaderProgram* prog, unsigned int nSlices, unsigned int nStacks, unsigned int sMax, unsigned int tMax) :
	QuadMesh(prog, "Prametric Surface", nSlices, nStacks)	
{
    _sMax = sMax;
    _tMax = tMax;
}

ParametricSurface::~ParametricSurface()
{
}

QVector3D ParametricSurface::normalAtParameter(const float& u, const float& v)
{
	float du = ((abs(firstUParameter()) + abs(lastUParameter())) / _slices) / 10.0f;
	float dv = ((abs(firstVParameter()) + abs(lastVParameter())) / _stacks) / 10.0f;

	Point o = pointAtParameter(u, v);
	Point uDir = pointAtParameter(u + du, v);
	Point vDir = pointAtParameter(u, v + dv);

	QVector3D oVec(o.getX(), o.getY(), o.getZ());
	QVector3D uVec(uDir.getX(), uDir.getY(), uDir.getZ());
	QVector3D vVec(vDir.getX(), vDir.getY(), vDir.getZ());

	QVector3D t1 = uVec - oVec;
	QVector3D t2 = vVec - oVec;

	if (t1.isNull())
	{
		uDir = pointAtParameter(u + du, v);
		QVector3D uVec(uDir.getX(), uDir.getY(), uDir.getZ());
		uVec = -uVec;
		t1 = uVec - oVec;
	}
	if (t2.isNull())
	{
		vDir = pointAtParameter(u, v + dv);
		QVector3D vVec(vDir.getX(), vDir.getY(), vDir.getZ());
		vVec = -vVec;
		t2 = vVec - oVec;
	}

	QVector3D normal = QVector3D::crossProduct(t2, t1);
	normal = normal.normalized();

	if (normal.isNull())
		return QVector3D(0, 0, 1);

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
        s = (float)i / _slices * _sMax;
		for (unsigned int j = 0; j <= _stacks; j++)
		{
            t = (float)j / _stacks * _tMax;
			Point pt = pointAtParameter(u, v);
			p[idx] = pt.getX(); p[idx + 1] = pt.getY(); p[idx + 2] = pt.getZ();
			QVector3D normal = normalAtParameter(u, v);
			n[idx] = normal.x(); n[idx + 1] = normal.y(); n[idx + 2] = normal.z();
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
