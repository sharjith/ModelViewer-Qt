#pragma once

#include "IDrawable.h"
#include <QtOpenGL>
#include <QOpenGLFunctions_4_5_Core>

class Drawable : public IDrawable, public QOpenGLFunctions_4_5_Core
{
public:
	Drawable(QOpenGLShaderProgram* prog) : _prog(prog)
	{		
		initializeOpenGLFunctions();
	}

protected:
	QOpenGLShaderProgram* _prog;
};
