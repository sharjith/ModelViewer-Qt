#include "CutCommand.h"
#include "ModelViewer.h"

CutCommand::CutCommand(ModelViewer*                 viewer,
                       ViewportWidget*                    viewportWidget,
                       const QList<ClipboardEntry>& entries,
                       const QSet<QUuid>&           cutMeshUuids,
                       const QSet<QUuid>&           cutNodeUuids,
                       const QString&               text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _entries(entries)
    , _cutMeshUuids(cutMeshUuids)
    , _cutNodeUuids(cutNodeUuids)
    , _firstRedo(true)
    , _generation(ModelViewer::currentClipboardGeneration())
{
}

void CutCommand::undo()
{
    if (!_viewer) return;
    _viewer->clearCutMarks(_generation);
}

void CutCommand::redo()
{
    if (!_viewer) return;
    if (_firstRedo) { _firstRedo = false; return; }
    _viewer->reapplyCutMarks(_generation, _entries, _cutMeshUuids, _cutNodeUuids);
}
