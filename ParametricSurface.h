#pragma once

#include "IParametricSurface.h"
#include "QuadMesh.h"

class ParametricSurface : public QuadMesh, public IParametricSurface
{
public:
	ParametricSurface(QOpenGLShaderProgram *prog, GLuint nSlices, GLuint nStacks);
	virtual ~ParametricSurface();

	virtual Point pointAtParameter(const float &u, const float &v) = 0;
	virtual glm::vec3 normalAtParameter(const float &u, const float &v);

	void buildMesh(GLuint nSlices, GLuint nStacks);

	float getSlices() const { return _slices; }
	float getStacks() const { return _stacks; }

protected:
	float _slices;
	float _stacks;
};
