#pragma once

#include "GltfLightData.h"
#include <QTreeWidgetItem>
#include <QWidget>
#include <QVector3D>
#include <QVector4D>
#include <memory>

class QFrame;

namespace Ui {
	class VisualizationEnvironmentPanel;
}

class ViewportWidget;
class ModelViewer;
class MaterialPreviewWidget;
class SceneGraph;

class VisualizationEnvironmentPanel : public QWidget
{
	Q_OBJECT

public:
	explicit VisualizationEnvironmentPanel(QWidget* parent = nullptr);
	~VisualizationEnvironmentPanel();

	// Initialization - MUST be called after panel is created
	void initialize(ModelViewer* modelViewer, ViewportWidget* viewportWidget);

	// Public accessors for ModelViewer to manage skybox state
	int getSkyBoxLDRIIndex() const { return _skyBoxLDRIIndex; }
	int getSkyBoxHDRIIndex() const { return _skyBoxHDRIIndex; }
	void setSkyBoxLDRIIndex(int index) { _skyBoxLDRIIndex = index; }
	void setSkyBoxHDRIIndex(int index) { _skyBoxHDRIIndex = index; }

	// Public method for ModelViewer to set PBR mode
	void setPBRLightingMode(bool enable);

	// Set preview widget for updating on environment changes
	void setPreviewWidget(MaterialPreviewWidget* preview) { _previewWidget = preview; }

	// Called when geometry changes to update light position slider ranges
	void updateLightPositionRanges(float range, float offset);

	// Restore slider positions from a saved offset (e.g. on MVF load).
	// Clamps to the current slider range so stale values are handled gracefully.
	void restoreDefaultLightOffset(const QVector3D& offset);

	bool isDetached() { return _detached; }
	void setDetached(bool detached);

signals:
	void detachRequested();

public slots:
	// ===== Light Color Buttons =====
	void onLightColorClicked();
	void onDefaultLightsClicked();

	// ===== Light Position Sliders =====
	void onLightPosXChanged(int value);
	void onLightPosYChanged(int value);
	void onLightPosZChanged(int value);

	// ===== Lighting Checkboxes =====
	void onDefaultLightsChanged(bool checked);
	void onShowLightsChanged(bool checked);
	void onIBLChanged(bool checked);

	// ===== Punctual Lights Tree =====
	// Rebuild the tree from SceneGraph light data; show/hide the group box.
	void refreshPunctualLightsTree();
	// Called when any tree item's check state changes — rebuilds GPU light list.
	void onPunctualLightItemChanged(QTreeWidgetItem* item, int column);

	// ===== Skybox Controls =====
	void onSkyBoxStateChanged(bool checked);
	void onSkyBoxHDRIChanged(bool checked);
	void onSkyBoxBlurChanged(int value);
	void onSkyBoxFOVChanged(double value);
	void onSkyBoxMapsChanged(int index);
	void onSkyBoxTextureClicked();

	// ===== Shadow Controls =====
	void onShadowMappingStateChanged(bool checked);
	void onSelfShadowsChanged(bool checked);
	void onShadowQualityChanged(int index);

	// ===== Floor Controls =====
	void onGroundModeChanged();
	void onFloorTextureStateChanged(bool checked);
	void onReflectionsChanged(bool checked);
	void onEnvMappingChanged(bool checked);
	void onFloorOffsetChanged(double value);
	void onRepeatSChanged(double value);
	void onRepeatTChanged(double value);
	void onFloorTextureClicked();

	// ===== HDR Controls =====
	void onHDRToneMappingStateChanged(bool checked);
	void onHDRToneMappingModeChanged(int index);
	void onEnvMapExposureChanged(double value);
	void onIBLExposureChanged(double value);

	// ===== Gamma Controls =====
	void onGammaCorrectionStateChanged(bool checked);
	void onScreenGammaChanged(double value);

	// ===== Default Values Button =====
	void onDefaultEnvValuesClicked();

	// ===== Skybox Preset Management =====
	void onLoadSkyBoxPresetMaps();

	// ===== Display Mode Changed Signal from ViewportWidget =====
	void onDisplayModeChanged(int mode);

	// ===== Camera Up-Axis =====
	void updateSkyBoxRotationLabels(bool zUp);

	// ===== Detach Button =====
	void onDetachButtonClicked();

public:
	// Helper methods
	void connectSignalsAndSlots();
	void updateControlDependencies();
	void updateButtonStyles();
	void reloadSkyBoxPresets();

private:
	// Member variables
	ModelViewer*    _modelViewer;
	ViewportWidget*       _viewportWidget;
	SceneGraph*     _sceneGraph  = nullptr;
	MaterialPreviewWidget* _previewWidget = nullptr;
	std::unique_ptr<Ui::VisualizationEnvironmentPanel> ui;
	bool _isInitialized;

	// Build one file-level parent item + its light children.
	QTreeWidgetItem* makeLightFileItem(const QString& sourceFile) const;
	QTreeWidgetItem* makeLightLeafItem(const GltfLightEntry& entry, int lightIndex) const;

	// State management - moved from ModelViewer
	int _skyBoxLDRIIndex;
	int _skyBoxHDRIIndex;

	// True when the active skybox came from onSkyBoxTextureClicked()'s
	// "Select Custom Map" folder browse rather than the preset combo box -
	// reloadSkyBoxPresets() must not stomp on it with a preset reload when
	// this is set (see reloadSkyBoxPresets()).
	bool _customSkyBoxActive = false;

	// True once the user has explicitly picked a ground mode radio button -
	// onDisplayModeChanged()'s realism-based floor/none default is only for
	// the very first switch into realistic shading, not something that
	// should re-stomp a deliberate later choice (e.g. "None") every time
	// setRealismEnabled()/displayModeChanged fires again, such as on every
	// Path Tracing "Render" click.
	bool _groundModeUserSet = false;

	bool _detached = false;

	// Resize handle for treePunctualLights
	QFrame* _lightTreeResizeHandle = nullptr;
	qreal   _lightTreeDragStartY   = 0.0;
	int     _lightTreeDragStartH   = 0;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
};
