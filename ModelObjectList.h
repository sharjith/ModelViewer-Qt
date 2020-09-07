#ifndef MODELOBJECTLIST_H
#define MODELOBJECTLIST_H

#include <QGLWidget>
#include <QListWidget>

class ModelObjectList : public QListWidget
{
public:
    ModelObjectList(QWidget *parent = nullptr);

protected slots:
    void keyPressEvent(QKeyEvent* event);
};

#endif // MODELOBJECTLIST_H
