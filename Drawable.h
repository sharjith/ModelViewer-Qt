#pragma once

#include "IDrawable.h"
#include <QtOpenGL>
#include <QOpenGLFunctions_4_5_Core>

class Drawable : public IDrawable, public QOpenGLFunctions_4_5_Core
{
public:
	Drawable(QOpenGLShaderProgram* prog);
	virtual ~Drawable();
	virtual QOpenGLShaderProgram* prog() const;
	virtual void setProg(QOpenGLShaderProgram* prog);
	virtual void setName(const QString& name);
	virtual bool isSelected() const;

protected:
	QOpenGLShaderProgram* _prog;
	QString _name;
	bool _selected;
	static unsigned int _count;
};
