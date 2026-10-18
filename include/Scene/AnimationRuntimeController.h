#pragma once

#include "GltfAnimationData.h"
#include "GltfLightData.h"
#include "PunctualLights.h"
#include "Material.h"
#include "LightOrigin.h"

class SceneGraph;
struct SceneNode;

// types.h (not matrix4x4.h alone) is needed for aiMatrix4x4t's inline
// constructor definitions (matrix4x4.h only declares them).
#include <assimp/types.h>

#include <QElapsedTimer>
#include <QHash>
#include <QMatrix4x4>
#include <QMultiHash>
#include <QSet>
#include <QString>
#include <QQuaternion>
#include <QTimer>
#include <QUuid>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <functional>
#include <vector>

// ---------------------------------------------------------------------------
// AnimationRuntimeController
//
// Owns all animation playback state previously scattered through ViewportWidget.
// ViewportWidget embeds one instance and accesses state through this API.
//
// Introduced in Phase 8 of the mesh/render/runtime separation refactor.
// De-aliased in the controller ownership cleanup pass.
//
// Note: _animationTimer (QTimer*) is created by ViewportWidget with itself as
// parent (required for Qt's object-tree ownership) and registered here via
// setAnimationTimer().
// ---------------------------------------------------------------------------
class AnimationRuntimeController
{
public:
    // ---- Stable per-node runtime transform ---------------------------------
    struct RuntimeNodeTransform
    {
        QVector3D   translation = QVector3D(0.0f, 0.0f, 0.0f);
        QQuaternion rotation;
        QVector3D   scale       = QVector3D(1.0f, 1.0f, 1.0f);
    };

    // ---- Per-file animation runtime state ----------------------------------
    struct RuntimeAnimationFileState
    {
        GltfAnimationData data;

        QHash<QUuid, RuntimeNodeTransform> defaultNodeTransformsByUuid;
        QHash<QUuid, QVector<float>>       defaultNodeMorphWeightsByUuid;
        QHash<QUuid, Material>           defaultMeshMaterials;
        QMultiHash<int, QUuid>             meshUuidsByMaterialIndex;

        QHash<QString, QUuid> nodeUuidByName;
        QHash<int,    QUuid>  nodeUuidByIndex;
        QHash<QUuid,  int>    nodeIndexByUuid;
    };

    struct AnimationSampleResult
    {
        QHash<QUuid, RuntimeNodeTransform>  nodeTransforms;
        QHash<QUuid, RuntimeNodeTransform>  meshTransforms;
        QHash<QUuid, QVector<float>>        morphWeights;
        QHash<QUuid, Material>            animatedMaterials;
        QHash<int, bool>                    nodeVisibility;
        QHash<QUuid, QMatrix4x4>            worldTransforms;
        bool                                affectsShadowCasters = false;
    };

    // ---- Playback state ----------------------------------------------------
    QString activeAnimationFile() const              { return _activeAnimationFile; }
    void    setActiveAnimationFile(const QString& f) { _activeAnimationFile = f; }

    int  activeAnimationClip() const          { return _activeAnimationClip; }
    void setActiveAnimationClip(int clip)     { _activeAnimationClip = clip; }

    double animationCurrentTimeSeconds() const      { return _animationCurrentTimeSeconds; }
    void   setAnimationCurrentTimeSeconds(double t) { _animationCurrentTimeSeconds = t; }

    bool isPlaying() const            { return _animationPlaying; }
    void setPlaying(bool playing)     { _animationPlaying = playing; }

    bool isLooping() const            { return _animationLooping; }
    void setLooping(bool looping)     { _animationLooping = looping; }

    double playbackSpeed() const          { return _animationPlaybackSpeed; }
    void   setPlaybackSpeed(double speed) { _animationPlaybackSpeed = speed; }

    // Timer is created by ViewportWidget (needs ViewportWidget as parent for Qt ownership)
    // and registered here for storage only.
    QTimer* animationTimer() const       { return _animationTimer; }
    void    setAnimationTimer(QTimer* t) { _animationTimer = t; }

    QElapsedTimer& animationElapsed()    { return _animationElapsed; }

    void resetPlayback();
    // Start playback: set playing flag, restart elapsed timer, start timer.
    void resumePlayback();
    // Stop playback: clear playing flag, stop timer.
    void pausePlayback();
    // Clamp speed to [0.25, 4.0], store it, and restart elapsed if playing.
    void applyPlaybackSpeed(double speed);

    // ---- Per-file animation data -------------------------------------------
    QHash<QString, RuntimeAnimationFileState>&       runtimeAnimationsByFile()       { return _runtimeAnimationsByFile; }
    const QHash<QString, RuntimeAnimationFileState>& runtimeAnimationsByFile() const { return _runtimeAnimationsByFile; }

    void removeAnimationFile(const QString& file) { _runtimeAnimationsByFile.remove(file); }
    void clearAllAnimationFiles()                 { _runtimeAnimationsByFile.clear(); }

    // ---- glTF camera activation --------------------------------------------
    QString activeGltfCameraFile() const  { return _activeGltfCameraFile; }
    int     activeGltfCameraIndex() const { return _activeGltfCameraIndex; }
    void    setActiveGltfCamera(const QString& file, int index);

    // ---- Light load staging -----------------------------------------------
    GltfLightData&       pendingLightData()       { return _pendingLightData; }
    const GltfLightData& pendingLightData() const { return _pendingLightData; }

    // ---- Light transform animation -----------------------------------------
    QString                      animatedLightTransformSourceFile() const { return _animatedLightTransformSourceFile; }
    const std::vector<GPULight>& animatedParsedLights() const             { return _animatedParsedLights; }

    void setAnimatedLightTransform(const QString& sourceFile, const std::vector<GPULight>& lights);
    void clearAnimatedLightTransform(const QString& sourceFile);

    // ---- Light visibility animation ----------------------------------------
    QString              animatedLightVisibilitySourceFile() const { return _animatedLightVisibilitySourceFile; }
    const QVector<bool>& animatedLightVisibilityMask() const       { return _animatedLightVisibilityMask; }

    void setAnimatedLightVisibility(const QString& sourceFile, const QVector<bool>& mask);
    void clearAnimatedLightVisibility(const QString& sourceFile);

    // ---- Mesh visibility animation -----------------------------------------
    QString            animatedMeshVisibilitySourceFile() const { return _animatedMeshVisibilitySourceFile; }
    const QSet<QUuid>& animatedHiddenMeshUuids() const          { return _animatedHiddenMeshUuids; }

    void setAnimatedMeshVisibility(const QString& sourceFile, const QSet<QUuid>& hiddenUuids);
    void clearAnimatedMeshVisibility(const QString& sourceFile);

    // Clears all animation-driven light and mesh visibility state (e.g. on scene reset).
    void clearAllAnimatedState();

    // ---- Light runtime transforms ------------------------------------------
    std::vector<GPULight>&       originalParsedLights()            { return _originalParsedLights; }
    const std::vector<GPULight>& originalParsedLights() const      { return _originalParsedLights; }
    std::vector<GPULight>&       currentRepositionedLights()       { return _currentRepositionedLights; }
    const std::vector<GPULight>& currentRepositionedLights() const { return _currentRepositionedLights; }
    QVector<LightOrigin>&        lightFileIndexMap()               { return _lightFileIndexMap; }
    const QVector<LightOrigin>&  lightFileIndexMap() const         { return _lightFileIndexMap; }

    void rebuildCurrentRepositionedLights(
        const std::vector<GPULight>& originalLights,
        std::vector<GPULight>& repositionedLights,
        const QVector<LightOrigin>& lightFileIndexMap,
        const QHash<QString, QMatrix4x4>& userTransforms);
    std::vector<GPULight> rebuildAndBuildUploadLights(
        const std::function<QMatrix4x4(const QString&)>& userTransformResolver,
        const std::function<bool(const LightOrigin&)>& isLightEnabled = {});
    std::vector<GPULight> buildUploadLightsWithSceneGraph(
        const std::function<QMatrix4x4(const QString&)>& userTransformResolver,
        const SceneGraph* sg);
    std::vector<GPULight> buildUploadLights() const;
    std::vector<GPULight> buildUploadLights(
        const std::function<bool(const LightOrigin&)>& isLightEnabled) const;

    AnimationSampleResult sampleClip(
        const RuntimeAnimationFileState& runtime,
        const GltfAnimationClip& clip,
        double timeSeconds,
        const SceneGraph* sg) const;

    QHash<int, bool> buildEffectiveNodeVisibility(
        const RuntimeAnimationFileState& runtime,
        const QHash<int, bool>& nodeVisibilityOverrides) const;

    QVector<bool> buildAnimatedLightVisibilityMask(
        const RuntimeAnimationFileState& runtime,
        const QHash<int, bool>& effectiveNodeVisibility) const;

    QSet<QUuid> collectHiddenAnimatedMeshUuids(
        const RuntimeAnimationFileState& runtime,
        const QHash<int, bool>& effectiveNodeVisibility,
        const SceneNode* fileNode) const;

    bool buildAnimatedLightTransformState(
        const QString& sourceFile,
        const RuntimeAnimationFileState& runtime,
        const AnimationSampleResult& result,
        const SceneNode* fileNode,
        std::vector<GPULight>& outAnimatedLights) const;

    // ---- Light population --------------------------------------------------
    // Populate the parsed-light baseline from a single-file GltfLightData
    // (used during initial model load before the file is in SceneGraph).
    void setParsedLightsFromSingleFile(const GltfLightData& lightData);
    // Rebuild the full parsed-light baseline from all files registered in
    // SceneGraph (authoritative multi-model path, called on lightDataChanged).
    void rebuildParsedLightsFromSceneGraph(const SceneGraph* sg);
    // Clear all parsed and repositioned light state (e.g. on scene reset).
    void clearParsedLights();
    // Return the enabled-filtered repositioned lights for gizmo rendering.
    std::vector<GPULight> buildGizmoLights(const SceneGraph* sg) const;

    // ---- Static helpers ----------------------------------------------------
    static QMatrix4x4           aiToQMatrix(const aiMatrix4x4& m);
    static RuntimeNodeTransform decomposeNodeTransform(const aiMatrix4x4& matrix);
    static QMatrix4x4           composeNodeTransform(const RuntimeNodeTransform& tr);
    static QUuid                resolveRuntimeNodeUuid(const RuntimeAnimationFileState& runtime,
                                                        int targetNodeIndex,
                                                        const QString& fallbackNodeName = {});

    static void applyTexturePointerValue(Material& material,
                                          GltfAnimationTextureTarget textureTarget,
                                          GltfAnimationPointerProperty property,
                                          const QVector2D& vec2Value,
                                          float scalarValue);

    static void applyMaterialFactorPointerValue(Material& material,
                                                 GltfAnimationPointerProperty property,
                                                 const QVector4D& vec4Value);

private:
    // Playback
    QHash<QString, RuntimeAnimationFileState> _runtimeAnimationsByFile;
    QString       _activeAnimationFile;
    int           _activeAnimationClip         = -1;
    double        _animationCurrentTimeSeconds = 0.0;
    bool          _animationPlaying            = false;
    bool          _animationLooping            = true;
    double        _animationPlaybackSpeed      = 1.0;
    QTimer*       _animationTimer              = nullptr;
    QElapsedTimer _animationElapsed;

    // glTF camera
    QString _activeGltfCameraFile;
    int     _activeGltfCameraIndex = -1;

    // Light load staging
    GltfLightData _pendingLightData;

    // Light animation
    QString               _animatedLightTransformSourceFile;
    std::vector<GPULight> _animatedParsedLights;
    QString               _animatedLightVisibilitySourceFile;
    QVector<bool>         _animatedLightVisibilityMask;

    // Canonical punctual-light runtime state
    std::vector<GPULight> _originalParsedLights;
    std::vector<GPULight> _currentRepositionedLights;
    QVector<LightOrigin>  _lightFileIndexMap;

    // Mesh visibility animation
    QString     _animatedMeshVisibilitySourceFile;
    QSet<QUuid> _animatedHiddenMeshUuids;
};
