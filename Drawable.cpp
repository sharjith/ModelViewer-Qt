#include "Drawable.h"

unsigned int Drawable::_count = 0;

Drawable::Drawable(QOpenGLShaderProgram* prog) : _prog(prog), _selected(false)
{
	initializeOpenGLFunctions();
	_count++;
}

Drawable::~Drawable()
{
	_count--;
}

QOpenGLShaderProgram* Drawable::prog() const
{
	return _prog;
}

void Drawable::setProg(QOpenGLShaderProgram* prog)
{
	_prog = prog;
}

void Drawable::setAutoIncrName(const QString& name)
{
	_name = name + QString("_%1").arg(_count);
}

bool Drawable::isSelected() const
{
	return _selected;
}