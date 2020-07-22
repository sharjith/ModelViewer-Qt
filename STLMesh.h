#ifndef STLMESH_H
#define STLMESH_H

#include "Drawable.h"
#include "TriangleMesh.h"

class STLMesh : public TriangleMesh
{
public:
    STLMesh(QOpenGLShaderProgram *prog, QString stlfilepath);

private:
    QString _stlFilePath;
};

#endif // STLMESH_H
