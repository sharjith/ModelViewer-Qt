#include "MergeByAdjacencyCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

MergeByAdjacencyCommand::MergeByAdjacencyCommand(ModelViewer*                  viewer,
                                                   ViewportWidget*               viewportWidget,
                                                   const QVector<SourceEntry>&  sources,
                                                   const QUuid&                  mergedUuid,
                                                   SceneNode*                    targetNode,
                                                   int                            mergedPosition,
                                                   const QSet<QUuid>&             originalSelection,
                                                   const QString&                text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _sources(sources)
    , _mergedUuid(mergedUuid)
    , _targetNode(targetNode)
    , _mergedPosition(mergedPosition)
    , _originalSelection(originalSelection)
    , _firstRedo(true)
    , _merged(true)
{
}

MergeByAdjacencyCommand::~MergeByAdjacencyCommand()
{
    if (!_viewportWidget)
        return;

    if (_merged)
    {
        // Sources sit in the recycle bin; free them permanently.
        for (const SourceEntry& s : _sources)
            _viewportWidget->permanentlyDeleteFromBin(s.uuid);
    }
    else
    {
        // Undone: the merged mesh sits in the recycle bin; free it.
        _viewportWidget->permanentlyDeleteFromBin(_mergedUuid);
    }
}

void MergeByAdjacencyCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    int idx = _viewportWidget->getIndexByUuid(_mergedUuid);
    if (idx >= 0)
        _viewportWidget->moveToRecycleBin(_mergedUuid, idx);

    int pos = 0;
    sg->removeMeshUuid(_mergedUuid, pos);

    for (const SourceEntry& s : _sources)
    {
        _viewportWidget->restoreFromRecycleBin(s.uuid);
        sg->restoreMeshUuid(s.ownerNode, s.uuid, s.position);
    }

    _merged = false;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void MergeByAdjacencyCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    if (_firstRedo)
    {
        // The merge already happened at push time; just select the result.
        _firstRedo = false;
        _merged = true;
        _viewer->setSelectionWithoutUndo(QSet<QUuid>{ _mergedUuid });
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    for (const SourceEntry& s : _sources)
    {
        int idx = _viewportWidget->getIndexByUuid(s.uuid);
        if (idx >= 0)
            _viewportWidget->moveToRecycleBin(s.uuid, idx);

        int pos = 0;
        sg->removeMeshUuid(s.uuid, pos);
    }

    _viewportWidget->restoreFromRecycleBin(_mergedUuid);
    sg->restoreMeshUuid(_targetNode, _mergedUuid, _mergedPosition);

    _merged = true;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(QSet<QUuid>{ _mergedUuid });
}

QSet<QUuid> MergeByAdjacencyCommand::getReferencedUuids() const
{
    QSet<QUuid> result;
    result.insert(_mergedUuid);
    for (const SourceEntry& s : _sources)
        result.insert(s.uuid);
    return result;
}
