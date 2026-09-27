#include "TransformCommand.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "RenderableMesh.h"

TransformCommand::TransformCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QVector<QUuid>& meshUuids,
    const QVector3D& newTranslation,
    const QVector3D& newRotation,
    const QVector3D& newScale,
    const QString& text)
    : ModelViewerCommand(viewer, viewportWidget, text)
{
    // Capture old states for all meshes before transformation
    for (const QUuid& uuid : meshUuids)
    {
        SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid);
        if (mesh)
        {
            TransformState oldState;
            oldState.translation = mesh->getTranslation();
            oldState.rotation = mesh->getRotation();
            oldState.scale = mesh->getScaling();
            oldState.rotationQuat = mesh->getRotationQuaternion();
            oldState.hasExactRotation = true;

            _oldStates[uuid] = oldState;

            // Store new state (same for all meshes in this command)
            TransformState newState;
            newState.translation = newTranslation;
            newState.rotation = newRotation;
            newState.scale = newScale;

            _newStates[uuid] = newState;
        }
    }
}

TransformCommand::TransformCommand(ModelViewer* viewer,
    ViewportWidget* viewportWidget,
    const QMap<QUuid, TransformState>& oldStates,
    const QMap<QUuid, TransformState>& newStates,
    const QString& text,
    bool fitView,
    Target target)
    : ModelViewerCommand(viewer, viewportWidget, text),
      _oldStates(oldStates),
      _newStates(newStates),
      _fitView(fitView),
      _target(target)
{
}

void TransformCommand::undo()
{
    if (!_viewer || !_viewportWidget)
        return;

    // Apply old transformation states (meshes that were permanently deleted
    // are skipped inside applyTransformStates via the UUID → index lookup).
    applyTransformStates(_oldStates);
}

void TransformCommand::redo()
{
    if (!_viewer || !_viewportWidget)
        return;

    // Apply new transformation states
    applyTransformStates(_newStates);
}

void TransformCommand::applyTransformStates(const QMap<QUuid, TransformState>& states)
{
    // Build map of index -> TransformState for ViewportWidget
    QMap<int, TransformState> indexedStates;

    for (auto it = states.begin(); it != states.end(); ++it)
    {
        const QUuid& uuid = it.key();

        // Get current index
        int index = _viewportWidget->getIndexByUuid(uuid);
        if (index >= 0)
        {
            indexedStates[index] = it.value();
        }
    }

    // Apply all transformations efficiently (one update at end)
    if (!indexedStates.isEmpty())
    {
        if (_target == Target::ExplodedViewTransform)
            _viewportWidget->applyExplodedViewTransforms(indexedStates, _fitView);
        else
            _viewportWidget->applyTransforms(indexedStates, _fitView);
        _viewer->syncLightPositionUiToScene();
        _viewportWidget->update();

        // Update panel to show current transform values
        _viewer->updateTransformationValues();
    }
}

QSet<QUuid> TransformCommand::getReferencedUuids() const
{
    QSet<QUuid> uuids;

    for (auto it = _oldStates.begin(); it != _oldStates.end(); ++it)
    {
        uuids.insert(it.key());
    }

    return uuids;
}

