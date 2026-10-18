#pragma once

#include "ModelViewerCommand.h"
#include <QUuid>
#include <QString>

class SceneTreeWidget;

/**
 * @brief Undoable rename of a single mesh.
 *
 * Stores the UUID together with the old and new names (both already
 * deduplicated by the time the command is constructed).  redo() and undo()
 * update the SceneMesh name and the corresponding SceneTreeWidget leaf
 * item text without triggering a full tree rebuild.
 */
class RenameMeshCommand : public ModelViewerCommand
{
public:
    RenameMeshCommand(ModelViewer*       viewer,
                      ViewportWidget*          viewportWidget,
                      SceneTreeWidget*   treeWidget,
                      const QUuid&       uuid,
                      const QString&     oldName,
                      const QString&     newName,
                      const QString&     text = QObject::tr("Rename"));

    void undo() override;
    void redo() override;

    /** Unique ID — rename commands are never merged with each other. */
    int id() const override { return 13; }

private:
    void applyName(const QString& name);

    SceneTreeWidget* _treeWidget;
    QUuid            _uuid;
    QString          _oldName;
    QString          _newName;
};
