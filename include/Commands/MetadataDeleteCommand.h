#pragma once

#include "GltfAnimationData.h"
#include "GltfCameraData.h"
#include "GltfVariantData.h"
#include "ModelViewerCommand.h"

#include <QHash>
#include <QString>
#include <QUuid>
#include <QVector>

class MetadataDeleteCommand : public ModelViewerCommand
{
public:
    enum class Kind
    {
        Animation,
        Camera,
        Variant,
    };

    MetadataDeleteCommand(ModelViewer* viewer,
                          ViewportWidget* viewportWidget,
                          Kind kind,
                          const QString& sourceFile,
                          int index,
                          const QString& text = QObject::tr("Delete"));

    void undo() override;
    void redo() override;

private:
    struct AnimationState
    {
        QString file;
        int clipIndex = -1;
        double timeSeconds = 0.0;
        bool playing = false;
    };

    struct CameraState
    {
        QString file;
        int cameraIndex = -1;
    };

    void redoAnimationDelete();
    void undoAnimationDelete();
    void redoCameraDelete();
    void undoCameraDelete();
    void redoVariantDelete();
    void undoVariantDelete();

    Kind _kind;
    QString _sourceFile;
    int _index = -1;

    GltfAnimationData _oldAnimationData;
    int _oldSceneGraphActiveClip = -1;
    AnimationState _oldAnimationState;

    GltfCameraData _oldCameraData;
    CameraState _oldCameraState;

    GltfVariantData _oldVariantData;
    int _oldActiveVariant = -1;
    QHash<QUuid, QVector<GltfVariantMapping>> _oldVariantMappingsByMesh;
};
