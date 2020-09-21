#pragma once

#include "IParametricSurface.h"
#include "QuadMesh.h"

class ParametricSurface : public QuadMesh, public IParametricSurface
{
public:
	ParametricSurface(QOpenGLShaderProgram* prog, unsigned int nSlices, unsigned int nStacks, unsigned int sMax = 1, unsigned int tMax = 1);
	virtual ~ParametricSurface();

	virtual Point pointAtParameter(const float& u, const float& v) = 0;
	virtual QVector3D normalAtParameter(const float& u, const float& v);

	void buildMesh();

	float getSlices() const { return _slices; }
	float getStacks() const { return _stacks; }

protected:
	QVector3D _tangent;
	QVector3D _bitangent;
};
