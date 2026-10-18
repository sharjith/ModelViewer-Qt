#include "ShrinkWrapCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

ShrinkWrapCommand::ShrinkWrapCommand(ModelViewer*       viewer,
                                      ViewportWidget*    viewportWidget,
                                      SceneNode*         wrapNode,
                                      SceneNode*         wrapParent,
                                      int                wrapPosition,
                                      const QUuid&       wrappedMeshUuid,
                                      const QSet<QUuid>& originalSelection,
                                      const QString&     text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _wrapNode(wrapNode)
    , _wrapParent(wrapParent)
    , _wrapPosition(wrapPosition)
    , _wrappedMeshUuid(wrappedMeshUuid)
    , _originalSelection(originalSelection)
    , _firstRedo(true)
    , _attached(true)
{
}

ShrinkWrapCommand::~ShrinkWrapCommand()
{
    // If destroyed while undone, the wrap node is detached (owned by this
    // command) and the wrapped mesh sits in the recycle bin - free both
    // (mirrors GroupMeshesCommand's detached-subtree cleanup and
    // DuplicateCommand's recycle-bin cleanup, combined).
    if (!_attached)
    {
        if (_viewportWidget)
            _viewportWidget->permanentlyDeleteFromBin(_wrappedMeshUuid);
        if (_wrapNode)
            SceneGraph::deleteDetachedSubtree(_wrapNode);
    }
}

void ShrinkWrapCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    int idx = _viewportWidget->getIndexByUuid(_wrappedMeshUuid);
    if (idx >= 0)
        _viewportWidget->moveToRecycleBin(_wrappedMeshUuid, idx);

    int pos = 0;
    sg->removeMeshUuid(_wrappedMeshUuid, pos);

    int outPosition = 0;
    sg->removeChildNode(_wrapParent, _wrapNode, outPosition);

    _attached = false;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void ShrinkWrapCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    if (_firstRedo)
    {
        // The wrap already happened at push time; just select the result.
        _firstRedo = false;
        _attached = true;
        _viewer->setSelectionWithoutUndo(QSet<QUuid>{ _wrappedMeshUuid });
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    sg->insertChildNode(_wrapParent, _wrapNode, _wrapPosition);

    _viewportWidget->restoreFromRecycleBin(_wrappedMeshUuid);
    sg->restoreMeshUuid(_wrapNode, _wrappedMeshUuid, 0);

    _attached = true;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(QSet<QUuid>{ _wrappedMeshUuid });
}

QSet<QUuid> ShrinkWrapCommand::getReferencedUuids() const
{
    return QSet<QUuid>{ _wrappedMeshUuid };
}
