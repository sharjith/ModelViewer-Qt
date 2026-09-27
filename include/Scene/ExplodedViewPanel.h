#pragma once

#include <QIcon>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>
#include <QVector3D>
#include <QQuaternion>
#include <QWidget>
#include <QScopedValueRollback>

#include "ui_ExplodedViewPanel.h"
#include "ExplodedViewManager.h"
#include "GltfAnimationData.h"
#include "TransformCommand.h"

class ViewportWidget;
class ModelViewer;
class SceneGraph;
class QTimer;
class MeshSelectionEditor;
class QTreeWidgetItem;

class ExplodedViewPanel : public QWidget, private Ui::ExplodedViewPanel
{
    Q_OBJECT

public:
    enum class AutoStrategy
    {
        AssemblyAware = 0,
        Classic = 1
    };

    explicit ExplodedViewPanel(ViewportWidget* parent = nullptr);

    void setSceneGraph(SceneGraph* sg);
    void applyContrastTheme(const QColor& textColor);
    void applyBackgroundTheme(const QColor& topColor, const QColor& bottomColor);
    void deactivateInteractiveState();
    QJsonArray presetsToJson();
    QUuid activePresetId() const;
    int activeCapturedStepIndex() const;
    void restorePresetsFromJson(const QJsonArray& presetsJson,
                                const QUuid& activePresetId = QUuid(),
                                int activeStepIndex = -1);

    // Called by ViewportWidget::showExplodedViewPanel(true) to seed the assembly
    // field from whatever is already selected in the viewport / tree.
    void captureCurrentSelection();

    // Stored selection state — consumed by ExplodedViewManager.
    const QSet<QUuid>& assemblyUuids() const;
    QUuid               anchorUuid()   const;
    ExplodedViewManager::Mode mode()   const;
    AutoStrategy        autoStrategy() const;
    QVector3D           userVector()   const;
    float               factor()       const;  // sliderValue / 100.0f

signals:
    // Emitted after picking capture so ModelViewer can clear the screen selection.
    void selectionClearRequested();
    // Emitted when any parameter that affects the explosion changes.
    void explosionParametersChanged();

private slots:
    void on_comboBoxMode_currentIndexChanged(int index);
    void on_sliderExplosion_valueChanged(int value);
    void on_pushButtonSelectAssembly_toggled(bool checked);
    void on_pushButtonSelectAnchor_toggled(bool checked);
    void on_pushButtonCapture_clicked();
    void on_pushButtonReset_clicked();
    void on_pushButtonPresetNew_clicked();
    void on_pushButtonPresetDuplicate_clicked();
    void on_pushButtonPresetActions_clicked();
    void on_pushButtonStartManualPlacement_clicked();
    void on_pushButtonFinishManualPlacement_clicked();
    void on_pushButtonClearManualPlacement_clicked();
    void on_pushButtonReplaceCapture_clicked();
    void on_pushButtonMoveCaptureUp_clicked();
    void on_pushButtonMoveCaptureDown_clicked();
    void onCapturedViewItemChanged(QTreeWidgetItem* item, int column);
    void onCapturedViewsContextMenuRequested(const QPoint& pos);

private:
    enum class PickingTarget { None, Assembly, Anchor };

    struct CapturedTransformTrack
    {
        GltfAnimationBindingTargetKind targetKind = GltfAnimationBindingTargetKind::Node;
        QUuid     meshUuid;
        QUuid     ownerNodeUuid;
        QString   sourceFile;
        QString   targetNodeName;
        int       targetNodeIndex = -1;
        QVector3D startPosition;
        QVector3D endPosition;
        QQuaternion startRotation;
        QQuaternion endRotation;
    };

    struct CapturedExplosionStep
    {
        QUuid id;
        QString name;
        bool isGroup = false;
        QVector<CapturedTransformTrack> tracks;
        QVector<CapturedExplosionStep> children;
    };

    struct ExplodedViewPreset
    {
        QUuid id;
        QString name;
        QSet<QUuid> assemblyUuids;
        QUuid anchorUuid;
        ExplodedViewManager::Mode mode = ExplodedViewManager::Mode::Auto;
        QVector3D userVector = QVector3D(1.0f, 0.0f, 0.0f);
        float factor = 1.0f;
        QVector<QUuid> manualSelectionUuids;
        QMap<QUuid, TransformState> uncapturedManualStates;
        QVector<CapturedExplosionStep> capturedSteps;
        int capturedStepCounter = 1;
        int capturedGroupCounter = 1;
        int outputMode = 0;
        double durationSeconds = 3.0;
        bool loopBack = true;
        bool useCombinedPose = true;
        AutoStrategy autoStrategy = AutoStrategy::AssemblyAware;
    };

    struct PreviewMeshState
    {
        QMatrix4x4 sceneRenderTransform;
        QVector3D explosionOffset;
        QVector3D translation;
        QVector3D rotation;
        QVector3D scale = QVector3D(1.0f, 1.0f, 1.0f);
        QQuaternion rotationQuat;
        QVector3D explodedViewTranslation;
        QVector3D explodedViewRotation;
        QVector3D explodedViewScale = QVector3D(1.0f, 1.0f, 1.0f);
        QQuaternion explodedViewRotationQuat;
        bool hasExactRotation = false;
        bool hasExactExplodedViewRotation = false;
    };

    struct SuspendedAnimationState
    {
        bool valid = false;
        QString sourceFile;
        int clipIndex = -1;
        double timeSeconds = 0.0;
        bool wasPlaying = false;
    };

    // Receives the selectionChanged signal while in picking mode.
    void onPickingSelectionChanged(const QList<int>& ids);

    // Convert a list of mesh-store indices to display text + populate UUID sets.
    void applyAssemblySelection(const QList<int>& ids);
    void mergeAssemblySelection(const QList<int>& ids);
    void applyAnchorSelection(const QList<int>& ids);
    void updateAssemblySelectionDisplay(const QList<int>& ids);
    void applyManualPlacementSelection(const QList<int>& ids);
    void applyManualPlacementEntries(const QVector<QUuid>& selectionUuids);
    void updateManualPlacementSelectionDisplay();
    void clearManualPlacementSelection();
    bool syncActivePresetManualStateFromRuntime();
    void restorePresetManualStateIntoRuntime(const ExplodedViewPreset& preset);
    QVector<QUuid> orderedAssemblyUuids() const;
    QString displayLabelForMeshUuid(const QUuid& uuid) const;
    void showMeshSelectionEditor();
    void reopenMeshSelectionEditor();
    void onMeshSelectionEditorFinished(int result);
    void applyAssemblyEntries(const QVector<QUuid>& assemblyUuids);
    void previewAssemblyEntry(const QUuid& uuid);
    void clearAssemblyPreviewSelection();

    // Derive a human-readable label for the assembly selection.
    QString describeAssemblySelection(const QList<int>& ids) const;

    void updateCaptureButton();
    void updatePresetDirtyIndicator();
    void cancelPickingMode();
    void updateAuthoringModeUi();
    void updateManualPlacementUi();
    void updateManualPlacementEditors();
    void updatePreviewControls();
    void stopDraftPreview(bool restoreScene = true);
    bool ensureDraftPreviewSession();
    void applyDraftPreviewPose(double timeSeconds);
    double currentDraftPreviewDuration() const;
    void updateAssemblyPickButtonVisual(bool awaitingCommit);
    void clearAssemblySelection();
    void updateCapturedViewsList();
    void updateCaptureMoveButtons();
    int currentCapturedStepRow() const;
    void setCurrentCapturedStepRow(int row);
    void applyPopupMenuStyle(QMenu& menu) const;
    QString nextCapturedGroupName() const;
    QSet<QUuid> currentCaptureMeshUuids() const;
    bool buildCurrentCapturedExplosionStep(CapturedExplosionStep& step) const;
    bool captureCurrentExplosionStep();
    bool replaceCapturedExplosionStep(int row);
    bool moveCapturedStep(const QUuid& stepId, int direction);
    bool moveCapturedStepInList(QVector<CapturedExplosionStep>& steps, const QUuid& stepId, int direction);
    bool groupSelectedCaptures();
    bool ungroupCapturedStep(const QUuid& groupId);
    bool removeCapturedStepById(const QUuid& stepId);
    void normalizeCapturedGroups(QVector<CapturedExplosionStep>& steps);
    void refreshCapturedGroupTracks(QVector<CapturedExplosionStep>& steps);
    CapturedExplosionStep* findCapturedStepById(QVector<CapturedExplosionStep>& steps, const QUuid& stepId);
    const CapturedExplosionStep* findCapturedStepById(const QVector<CapturedExplosionStep>& steps, const QUuid& stepId) const;
    bool removeCapturedStepById(QVector<CapturedExplosionStep>& steps, const QUuid& stepId);
    bool ungroupCapturedStep(QVector<CapturedExplosionStep>& steps, const QUuid& groupId);
    void collectCapturedLeafTracks(const CapturedExplosionStep& step, QVector<CapturedTransformTrack>& out) const;
    QVector<CapturedTransformTrack> resolvedTracksForStep(const CapturedExplosionStep& step) const;
    QVector<CapturedExplosionStep> resolvedTopLevelCapturedEntries() const;
    QUuid currentCapturedStepId() const;
    QUuid currentTopLevelCapturedStepId() const;
    CapturedExplosionStep* currentCapturedStep();
    const CapturedExplosionStep* currentCapturedStep() const;
    CapturedExplosionStep* currentTopLevelCapturedStep();
    const CapturedExplosionStep* currentTopLevelCapturedStep() const;
    bool createAnimationsFromCapturedSteps();
    void onPreviewPlayPauseClicked();
    void onPreviewStopClicked();
    void onPreviewLoopToggled(bool checked);
    void onPreviewSliderPressed();
    void onPreviewSliderReleased();
    void onPreviewSliderValueChanged(int value);
    void initializeDefaultPreset();
    void refreshPresetCombo();
    void loadPresetIntoUi(int index);
    void syncActivePresetFromUi();
    QString nextPresetName() const;
    QString nextDuplicatedPresetName(const QString& sourceName) const;
    ExplodedViewPreset* activePreset();
    const ExplodedViewPreset* activePreset() const;
    ExplodedViewPreset& ensureActivePreset();
    ModelViewer* owningModelViewer() const;
    void markDocumentModified();
    QVector<CapturedExplosionStep>& activeCapturedSteps();
    const QVector<CapturedExplosionStep>& activeCapturedSteps() const;

    ViewportWidget*    _viewportWidget   = nullptr;
    SceneGraph*  _sceneGraph = nullptr;

    PickingTarget            _pickingTarget = PickingTarget::None;
    QMetaObject::Connection  _pickingConn;

    QIcon       _assemblySelectIdleIcon;
    QIcon       _assemblySelectCommitIcon;
    QString _popupMenuStyleSheet;
    QVector<ExplodedViewPreset> _presets;
    int _activePresetIndex = -1;
    int _createdAnimationCounter = 1;
    bool _syncingPresetUi = false;
    bool _syncingPreviewControls = false;
    bool _previewScrubbing = false;
    bool _draftPreviewActive = false;
    bool _draftPreviewPlaying = false;
    bool _draftPreviewLoopPlayback = true;
    bool _syncingManualPlacementEditors = false;
    bool _suspendingManualPresetSync = false;
    bool _hasUncapturedAutoPose = false;
    bool _hasUncapturedManualPose = false;
    double _draftPreviewCurrentTime = 0.0;
    QElapsedTimer _draftPreviewElapsed;
    QTimer* _draftPreviewTimer = nullptr;
    QSet<QString> _draftPreviewFiles;
    QHash<QUuid, PreviewMeshState> _draftPreviewMeshStates;
    SuspendedAnimationState _draftPreviewSuspendedAnimation;
    QSet<QUuid> _emptyAssemblyUuids;
    QVector<QUuid> _assemblyEditWorkingUuids;
    QVector<QUuid> _manualPlacementSelectionUuids;
    bool _reopenAssemblyEditDialogAfterPick = false;
    bool _assemblyEditPickActive = false;
    MeshSelectionEditor* _meshSelectionEditor = nullptr;
    bool _syncingCapturedViewsList = false;

private slots:
    void on_pushButtonCaptureView_clicked();
    void on_pushButtonRemoveCapture_clicked();
    void onDraftPreviewTick();
};
