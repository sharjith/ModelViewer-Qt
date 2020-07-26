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

    QOpenGLShaderProgram *prog() const
    {
        return _prog;
    }

    void setProg(QOpenGLShaderProgram *prog)
    {
        _prog = prog;
    }

protected:
    QOpenGLShaderProgram* _prog;
};


