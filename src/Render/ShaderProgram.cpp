#include "ShaderProgram.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

ShaderProgram::ShaderProgram()
{
}

ShaderProgram::~ShaderProgram()
{
}

bool ShaderProgram::loadCompileAndLinkShaderFromFile(const QString& vertexProg, const QString& fragmentProg, const QString& geometryProg, const QString& tessControlProg, const QString& tessEvalProg)
{
	bool success = addShaderFromSourceFile(QOpenGLShader::Vertex, vertexProg);
	if (!success)
	{
		qDebug() << "Error in vertex shader:" << objectName() << log();
	}
	if (tessControlProg != "")
	{
		success = addShaderFromSourceFile(QOpenGLShader::TessellationControl, tessControlProg);
		if (!success)
		{
			qDebug() << "Error in tessellation  control shader:" << objectName() << log();
		}
	}
	if (tessEvalProg != "")
	{
		success = addShaderFromSourceFile(QOpenGLShader::TessellationEvaluation, tessEvalProg);
		if (!success)
		{
			qDebug() << "Error in tessellation  evaluation shader:" << objectName() << log();
		}
	}
	if (geometryProg != "")
	{
		success = addShaderFromSourceFile(QOpenGLShader::Geometry, geometryProg);
		if (!success)
		{
			qDebug() << "Error in geometry shader:" << objectName() << log();
		}
	}
	success = addShaderFromSourceFile(QOpenGLShader::Fragment, fragmentProg);
	if (!success)
	{
		qDebug() << "Error in fragment shader:" << objectName() << log();
	}
	if (success)
	{
		success = link();
		if (!success)
		{
			qDebug() << "Error linking shader program:" << objectName() << log();
		}
	}

	return success;
}
