#pragma once

#include "IDrawable.h"
#include <QtOpenGL>
#include <QOpenGLFunctions_4_5_Core>

class Drawable : public QObject, public IDrawable, public QOpenGLFunctions_4_5_Core
{
    Q_OBJECT
public:
	Drawable(QOpenGLShaderProgram* prog);
	virtual ~Drawable();
	virtual QOpenGLShaderProgram* prog() const;
	virtual void setProg(QOpenGLShaderProgram* prog);
	virtual void setAutoIncrName(const QString& name);
	virtual void setName(const QString& name) { _name = name; }
	virtual bool isSelected() const;

protected:
	QOpenGLShaderProgram* _prog;
	QString _name;
	bool _selected;
	static unsigned int _count;
};
