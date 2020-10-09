#pragma once

#include "Drawable.h"
#include "GridMesh.h"

class Cube : public GridMesh
{
public:
	Cube(QOpenGLShaderProgram* prog, float size = 1.0f);
	void setSize(const float& size);
};
