#ifndef STLMESH_H
#define STLMESH_H

#include "Drawable.h"
#include "TriangleMesh.h"

class STLMesh : public TriangleMesh
{
public:
	STLMesh(QOpenGLShaderProgram* prog, QString stlfilepath);

	bool loaded() const;

private:
	QString _stlFilePath;
	bool _loaded;
};

#endif // STLMESH_H
