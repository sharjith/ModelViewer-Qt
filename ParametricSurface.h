#pragma once

#include "IParametricSurface.h"
#include "QuadMesh.h"

class ParametricSurface : public QuadMesh, public IParametricSurface
{
public:
	ParametricSurface(QOpenGLShaderProgram *prog, unsigned int nSlices, unsigned int nStacks);
	virtual ~ParametricSurface();

	virtual Point pointAtParameter(const float &u, const float &v) = 0;
	virtual glm::vec3 normalAtParameter(const float &u, const float &v);

	void buildMesh();

	float getSlices() const { return _slices; }
	float getStacks() const { return _stacks; }

	bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

protected:
	float _slices;
	float _stacks;
};
