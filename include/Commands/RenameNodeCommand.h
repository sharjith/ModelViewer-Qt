#pragma once

#include "ModelViewerCommand.h"
#include <QUuid>
#include <QString>

class SceneTreeWidget;

/**
 * @brief Undoable rename of an assembly / file node in the scene hierarchy.
 *
 * Updates SceneNode::name and the matching tree item text without a tree rebuild.
 * Mesh (leaf) renames use RenameMeshCommand.
 */
class RenameNodeCommand : public ModelViewerCommand
{
public:
    RenameNodeCommand(ModelViewer*     viewer,
                      ViewportWidget*  viewportWidget,
                      SceneTreeWidget* treeWidget,
                      const QUuid&     nodeUuid,
                      const QString&   oldName,
                      const QString&   newName,
                      const QString&   text = QObject::tr("Rename"));

    void undo() override;
    void redo() override;

private:
    void applyName(const QString& name);

    SceneTreeWidget* _treeWidget;
    QUuid            _nodeUuid;
    QString          _oldName;
    QString          _newName;
};
