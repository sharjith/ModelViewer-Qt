#ifndef STLMESH_H
#define STLMESH_H

#include "Drawable.h"
#include "TriangleMesh.h"

class STLMesh : public TriangleMesh
{
public:
	STLMesh(QOpenGLShaderProgram* prog, std::vector<float> points);

	virtual TriangleMesh* clone();

private:
	std::vector<float> _points;	
};

#endif // STLMESH_H
