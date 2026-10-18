#pragma once

#include <QDialog>
#include <QSet>
#include <QVector>

#include "ViewportWidget.h" // TextureSlotInfo

class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QWidget;
class ViewportWidget;
class ModelViewer;

// ---------------------------------------------------------------------------
// TextureDebugPanel
//
// Floating Qt::Tool window that shows the GPU-side texture bindings for the
// currently selected mesh.  Activated via Visualization → Texture Debugger
// (visible only when "Enable Texture Debugger" is checked in Settings → Debug).
//
// Layout:
//   ┌──────────────────────────────────────────────┐
//   │ Mesh: <name>                    [↻ Refresh]  │
//   ├──────────────────────────────────────────────┤
//   │ TEXTURES            ☐ Show inactive slots    │
//   │ ┌── scrollable grid ───────────────────────┐ │
//   │ │  64×64 swatch  64×64 swatch  …           │ │
//   │ │  slot name     slot name     …           │ │
//   │ │  unit badge    unit badge    …           │ │
//   │ └──────────────────────────────────────────┘ │
//   ├──────────────────────────────────────────────┤
//   │ EXTENSIONS                                   │
//   │  ● Sheen   ○ Clearcoat   ○ Iridescence  …   │
//   └──────────────────────────────────────────────┘
// ---------------------------------------------------------------------------
class TextureDebugPanel : public QDialog
{
	Q_OBJECT

public:
	explicit TextureDebugPanel(QWidget* parent = nullptr);

	void setViewportWidget(ViewportWidget* viewportWidget);
	void setModelViewer(ModelViewer* mv);

public slots:
	// Called when the mesh selection changes; triggers a readback for the
	// first selected mesh (or clears the panel when nothing is selected).
	void onSelectionChanged(const QList<int>& selectedIds);

	// Override reject() so Escape routes through closeEvent (which runs
	// cleanup) instead of going directly to hide().
	void reject() override;

	// Called by ViewportWidget after the GL readback completes.
	// NOTE: parameter named 'slotInfos', not 'slots' — 'slots' is a Qt macro.
	void onTextureReadbackReady(const QVector<TextureSlotInfo>& slotInfos,
	                            const QString& meshName);

	// Re-requests a readback for the current mesh (e.g. after the model reloads
	// or the user clicks the Refresh button).
	void refresh();

signals:
	// Emitted when the user accepts the "switch to PBR?" prompt at panel launch.
	// ModelViewer connects this to onRenderingModeSelected("PBR") so the full
	// activation chain runs (HDR skybox, Realistic display mode, toolbar sync…).
	void requestPBRMode();

protected:
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	// UI construction
	void buildUI();

	// Fills _channelCombo with the "All"/geometry/texture/extension groups and
	// their separators, using fresh tr() calls - shared by buildUI() (initial
	// population) and retranslateTexts() (rebuild on language change), so the
	// ~35-item list only needs to be maintained in one place. Does not create
	// _channelCombo itself or touch its current selection.
	void populateChannelCombo();

	// Population helpers — called from onTextureReadbackReady
	void populateThumbnails(const QVector<TextureSlotInfo>& slots);
	void populateExtensions(const QVector<TextureSlotInfo>& slots);

	// Clears all dynamic content (thumbnails, extension badges)
	// without destroying the static chrome.
	void clearDynamicContent();

	void saveWindowGeometry();
	void restoreWindowGeometry();

	// Shows/hides the amber PBR warning strip based on the current rendering mode.
	void updatePBRWarning();

	// This panel builds its whole UI programmatically (no .ui file), so there
	// is no generated retranslateUi() for LanguageManager::languageChanged to
	// call - this is buildUI()'s hand-written equivalent instead. Re-applies
	// tr() to every STATIC piece of chrome (window title, buttons, combo
	// items, section labels) without rebuilding the layout, then calls
	// refresh() to let the existing mesh-name/thumbnail/extension population
	// logic re-derive its own text fresh (those already call tr() every time
	// they run, so no separate translation logic needs to be duplicated
	// here - see onTextureReadbackReady()/populateThumbnails()/
	// populateExtensions()).
	void retranslateTexts();

	// Returns the set of units that have real (active) textures on the current mesh.
	QSet<int> activeUnits() const;

	// Re-evaluates the full enabled/disabled checkbox state and calls
	// ViewportWidget::applyDebugTextureState.  Called whenever a thumbnail is toggled.
	void applyCurrentTextureState();

	// ---- data ---------------------------------------------------------------
	ViewportWidget*    _viewportWidget    = nullptr;
	ModelViewer* _modelViewer = nullptr;
	int          _currentMeshId = -1;

	// Most-recently received slot list; kept so toggling the inactive checkbox
	// can repopulate without a new readback.
	QVector<TextureSlotInfo> _lastSlots;

	// Units that have been disabled by the user via the thumbnail toggles.
	// Cleared when the mesh selection changes.
	QSet<int> _disabledUnits;

	// Extensions that have been disabled by the user via the extension toggles.
	// Key is the internal extension key ("Sheen", "Clearcoat", etc.).
	// Cleared when the mesh selection changes.
	QSet<QString> _disabledExtensions;

	// Channel currently shown in the dropdown (0 = "All" / checkbox mode).
	int _activeChannelId = 0;

	// ---- UI -----------------------------------------------------------------
	QLabel*      _meshNameLabel       = nullptr;
	QPushButton* _refreshButton       = nullptr;
	QLabel*      _statusLabel         = nullptr;   // multi-select / info strip
	QLabel*      _pbrWarningLabel     = nullptr;   // amber strip shown when mode is not PBR

	// Channel isolation dropdown (All / Albedo / Metallic / …)
	QLabel*      _channelLabel        = nullptr;
	QComboBox*   _channelCombo        = nullptr;

	// Textures section
	QLabel*      _texturesSectionLabel = nullptr;
	QCheckBox*   _showInactiveCheck   = nullptr;
	QScrollArea* _thumbnailScroll     = nullptr;
	QWidget*     _thumbnailContainer  = nullptr;
	QGridLayout* _thumbnailGrid       = nullptr;

	// Extensions section
	QGroupBox*   _extensionGroup      = nullptr;
	QGridLayout* _extensionLayout     = nullptr;
};
