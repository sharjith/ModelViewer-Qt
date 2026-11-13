#include "EditUserLightCommand.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

EditUserLightCommand::EditUserLightCommand(ModelViewer*          viewer,
                                           ViewportWidget*       viewportWidget,
                                           int                   index,
                                           Field                 field,
                                           const GltfLightEntry& oldEntry,
                                           const GltfLightEntry& newEntry,
                                           const QString&        text)
    : ModelViewerCommand(viewer, viewportWidget, text)
    , _index(index)
    , _field(field)
    , _oldEntry(oldEntry)
    , _newEntry(newEntry)
{
}

void EditUserLightCommand::redo()
{
    apply(_newEntry);
}

void EditUserLightCommand::undo()
{
    apply(_oldEntry);
}

bool EditUserLightCommand::mergeWith(const QUndoCommand* other)
{
    const auto* next = dynamic_cast<const EditUserLightCommand*>(other);
    if (!next || next->_index != _index || next->_field != _field)
        return false;
    _newEntry = next->_newEntry;
    return true;
}

void EditUserLightCommand::apply(const GltfLightEntry& entry)
{
    if (SceneGraph* sg = _viewer ? _viewer->sceneGraph() : nullptr)
        sg->replaceUserLight(_index, entry);
    if (_viewportWidget)
        _viewportWidget->userLightsChanged();
}
