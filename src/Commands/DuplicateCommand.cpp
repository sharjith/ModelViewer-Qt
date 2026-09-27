#include "DuplicateCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"

DuplicateCommand::DuplicateCommand(ModelViewer*                   viewer,
                                   ViewportWidget*                      viewportWidget,
                                   const QVector<DuplicateEntry>& entries,
                                   const QSet<QUuid>&             originalSelection,
                                   const QString&                 text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _entries(entries)
    , _originalSelection(originalSelection)
    , _firstRedo(true)
    , _inserted(true)
{
}

DuplicateCommand::~DuplicateCommand()
{
    // If destroyed while undone, dupes are in the recycle bin — clean them up.
    if (!_inserted && _viewportWidget)
    {
        for (const DuplicateEntry& e : _entries)
            _viewportWidget->permanentlyDeleteFromBin(e.uuid);
    }
}

void DuplicateCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    SceneGraph* sg = _viewer->sceneGraph();

    for (const DuplicateEntry& e : _entries)
    {
        int idx = _viewportWidget->getIndexByUuid(e.uuid);
        if (idx >= 0)
            _viewportWidget->moveToRecycleBin(e.uuid, idx);

        int pos = 0;
        sg->removeMeshUuid(e.uuid, pos);
    }

    _inserted = false;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();
    _viewer->setSelectionWithoutUndo(_originalSelection);
}

void DuplicateCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    if (_firstRedo)
    {
        // Duplication already happened; just select the new meshes.
        _firstRedo = false;
        _inserted  = true;
        QSet<QUuid> dupeSet;
        for (const DuplicateEntry& e : _entries)
            dupeSet.insert(e.uuid);
        _viewer->setSelectionWithoutUndo(dupeSet);
        return;
    }

    SceneGraph* sg = _viewer->sceneGraph();

    for (const DuplicateEntry& e : _entries)
    {
        _viewportWidget->restoreFromRecycleBin(e.uuid);
        sg->restoreMeshUuid(e.ownerNode, e.uuid, e.position);
    }

    _inserted = true;

    _viewportWidget->updateView();
    _viewer->updateDisplayList();

    QSet<QUuid> dupeSet;
    for (const DuplicateEntry& e : _entries)
        dupeSet.insert(e.uuid);
    _viewer->setSelectionWithoutUndo(dupeSet);
}

QSet<QUuid> DuplicateCommand::getReferencedUuids() const
{
    QSet<QUuid> result;
    for (const DuplicateEntry& e : _entries)
        result.insert(e.uuid);
    return result;
}
