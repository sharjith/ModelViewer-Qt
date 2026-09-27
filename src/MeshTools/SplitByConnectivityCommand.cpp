#include "SplitByConnectivityCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

SplitByConnectivityCommand::SplitByConnectivityCommand(ModelViewer*                   viewer,
                                                         ViewportWidget*                viewportWidget,
                                                         const QUuid&                   originalUuid,
                                                         int                             originalPosition,
                                                         SceneNode*                      ownerNode,
                                                         const QVector<FragmentEntry>&  fragments,
                                                         const QSet<QUuid>&              originalSelection,
                                                         const QString&                 text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _originalUuid(originalUuid)
    , _originalPosition(originalPosition)
    , _ownerNode(ownerNode)
    , _fragments(fragments)
    , _originalSelection(originalSelection)
    , _firstRedo(true)
    , _split(true)
{
}

SplitByConnectivityCommand::~SplitByConnectivityCommand()
{
    if (!_viewportWidget)
        return;

    if (_split)
    {
        // Fragments are live; the original sits in the recycle bin - free it permanently.
        _viewportWidget->permanentlyDeleteFromBin(_originalUuid);
    }
    else
    {
        // Undone: the original is live; fragments sit in the recycle bin - free them.
        for (const FragmentEntry& f : _fragments)
            _viewportWidget->permanentlyDeleteFromBin(f.uuid);
    }
}

void SplitByConnectivityCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    for (const FragmentEntry& f : _fragments)
    {
        int idx = _viewportWidget->getIndexByUuid(f.uuid);
        if (idx >= 0)
            _viewportWidget->moveToRecycleBin(f.uuid, idx);

        int pos = 0;
        sg->removeMeshUuid(f.uuid, pos);
    }

    _viewportWidget->restoreFromRecycleBin(_originalUuid);
    sg->restoreMeshUuid(_ownerNode, _originalUuid, _originalPosition);

    _split = false;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void SplitByConnectivityCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    if (_firstRedo)
    {
        // The split already happened at push time; just select the fragments.
        _firstRedo = false;
        _split = true;
        QSet<QUuid> fragSet;
        for (const FragmentEntry& f : _fragments)
            fragSet.insert(f.uuid);
        _viewer->setSelectionWithoutUndo(fragSet);
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    int pos = 0;
    sg->removeMeshUuid(_originalUuid, pos);
    int idx = _viewportWidget->getIndexByUuid(_originalUuid);
    if (idx >= 0)
        _viewportWidget->moveToRecycleBin(_originalUuid, idx);

    for (const FragmentEntry& f : _fragments)
    {
        _viewportWidget->restoreFromRecycleBin(f.uuid);
        sg->restoreMeshUuid(_ownerNode, f.uuid, f.position);
    }

    _split = true;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();

    QSet<QUuid> fragSet;
    for (const FragmentEntry& f : _fragments)
        fragSet.insert(f.uuid);
    _viewer->setSelectionWithoutUndo(fragSet);
}

QSet<QUuid> SplitByConnectivityCommand::getReferencedUuids() const
{
    QSet<QUuid> result;
    result.insert(_originalUuid);
    for (const FragmentEntry& f : _fragments)
        result.insert(f.uuid);
    return result;
}
