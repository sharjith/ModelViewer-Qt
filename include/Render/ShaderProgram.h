#pragma once

#include <QOpenGLShaderProgram>
#include <QString>

class ShaderProgram : public QOpenGLShaderProgram
{
public:
    ShaderProgram();

    ~ShaderProgram();

    bool loadCompileAndLinkShaderFromFile(const QString& vertexProg,
        const QString& fragmentProg, const QString& geometryProg = "",
        const QString& tessControlProg = "", const QString& tessEvalProg = "");

};
