#include "VisualizationEnvironmentPanel.h"
#include "ui_VisualizationEnvironmentPanel.h"
#include "ViewportWidget.h"
#include "LanguageManager.h"
#include "ModelViewer.h"
#include "MaterialPreviewWidget.h"
#include "PathUtils.h"
#include "SceneGraph.h"
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QDir>
#include <QDebug>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	// Fixed slider tick resolution for the Default Light Position sliders -
	// see LightAxisSliderMapping's doc comment in the header.
	constexpr int kLightSliderSteps = 1000;

	// Mirrors ViewportWidget::setSkyBoxZRotation(int)'s own fixed 4-way
	// table (X+/X-/Y-Z+/Y-Z-) - kept in sync with that function's angles[]
	// array by convention rather than a shared constant, since one lives in
	// the viewport (the authoritative rotation-application code) and the
	// other here (the UI's own preset-to-angle bookkeeping for combining
	// with the fine offset slider).
	constexpr float kSkyBoxRotationPresetAngles[4] = { 0.0f, 180.0f, 90.0f, 270.0f };

	// Finds whichever of the 4 preset angles above is closest to `degrees`
	// (wrapping correctly across the 0/360 boundary) and the signed residual
	// offset needed to reach `degrees` from it - always within [-45, 45]
	// since the presets are spaced exactly 90 degrees apart. Used both to
	// combine the combo+slider into a single angle (applySkyBoxRotation())
	// and to decompose a single saved angle back into the two controls
	// (VisualizationEnvironmentPanel::restoreSkyBoxRotationDegrees()).
	void nearestSkyBoxRotationPreset(float degrees, int& outIndex, float& outOffset)
	{
		outIndex = 0;
		float bestDelta = 0.0f;
		float bestAbsDelta = std::numeric_limits<float>::max();
		for (int i = 0; i < 4; ++i)
		{
			// Wrap the raw difference into (-180, 180] before comparing, so
			// e.g. 350 degrees correctly reads as "-10 from the 0 preset",
			// not "+350 from it" or a spurious match against 270.
			float delta = std::fmod(degrees - kSkyBoxRotationPresetAngles[i] + 540.0f, 360.0f) - 180.0f;
			const float absDelta = std::abs(delta);
			if (absDelta < bestAbsDelta)
			{
				bestAbsDelta = absDelta;
				bestDelta = delta;
				outIndex = i;
			}
		}
		outOffset = bestDelta;
	}
}

VisualizationEnvironmentPanel::VisualizationEnvironmentPanel(QWidget* parent)
	: QWidget(parent),
	_modelViewer(nullptr),
	_viewportWidget(nullptr),
	_isInitialized(false),
	_skyBoxLDRIIndex(0),
	_skyBoxHDRIIndex(0)
{
	ui = std::make_unique<Ui::VisualizationEnvironmentPanel>();
	ui->setupUi(this);

	// Embed the enum value in each combo item's UserRole so the handler
	// never depends on positional index — reordering the UI is always safe.
	{
		auto* combo = ui->comboBoxHDRToneMappingMode;
		combo->setItemData(0, static_cast<int>(HDRToneMapMode::KhronosPbrNeutral));
		combo->setItemData(1, static_cast<int>(HDRToneMapMode::ACES_Narkowicz));
		combo->setItemData(2, static_cast<int>(HDRToneMapMode::ACES_Hill));
		combo->setItemData(3, static_cast<int>(HDRToneMapMode::AECS_Hill_Exposure_Boost));
		combo->setItemData(4, static_cast<int>(HDRToneMapMode::Uncharted2ToneMapping));
		combo->setItemData(5, static_cast<int>(HDRToneMapMode::Reinhard));
	}

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		ui->retranslateUi(this);
		});

	setAttribute(Qt::WA_DeleteOnClose);
	ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		
	QTimer::singleShot(0, this, &VisualizationEnvironmentPanel::onLoadSkyBoxPresetMaps);
}

VisualizationEnvironmentPanel::~VisualizationEnvironmentPanel() = default;

bool VisualizationEnvironmentPanel::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _lightTreeResizeHandle)
	{
		auto* me = static_cast<QMouseEvent*>(event);
		switch (event->type())
		{
		case QEvent::MouseButtonPress:
			if (me->button() == Qt::LeftButton)
			{
				_lightTreeDragStartY = me->globalPosition().y();
				_lightTreeDragStartH = ui->treePunctualLights->height();
				return true;
			}
			break;

		case QEvent::MouseMove:
			if (me->buttons() & Qt::LeftButton)
			{
				constexpr int kMinH = 48;
				const int delta = static_cast<int>(me->globalPosition().y() - _lightTreeDragStartY);
				const int newH  = qMax(kMinH, _lightTreeDragStartH + delta);
				// setFixedHeight (sets both min and max) forces the layout to
				// allocate exactly newH pixels — setMaximumHeight alone is
				// insufficient when the tree is already at its natural size.
				ui->treePunctualLights->setFixedHeight(newH);
				ui->groupBoxPunctualLights->updateGeometry();
				return true;
			}
			break;

		case QEvent::MouseButtonRelease:
			return true;

		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void VisualizationEnvironmentPanel::initialize(ModelViewer* modelViewer, ViewportWidget* viewportWidget)
{
	_modelViewer = modelViewer;
	_viewportWidget    = viewportWidget;
	_sceneGraph  = _modelViewer ? _modelViewer->sceneGraph() : nullptr;

	// Load state from ModelViewer
	if (_modelViewer)
	{
		_skyBoxLDRIIndex = _modelViewer->getSkyBoxLDRIIndex();
		_skyBoxHDRIIndex = _modelViewer->getSkyBoxHDRIIndex();
	}

	// One-time UI construction: this panel's own signal/slot wiring and the
	// punctual-lights resize handle never change across documents, so they
	// still only run once - but AFTER _modelViewer/_viewportWidget are
	// assigned above, since connectSignalsAndSlots() dereferences
	// _viewportWidget directly (e.g. isCameraUpAxisZUp()) as part of wiring
	// up. Everything below the guard re-runs on every call instead, since
	// this is now a single shared instance rebound to whichever document is
	// active (see MainWindow::rebindSharedPanelsTo()) rather than one
	// instance per document - initialize() used to be called exactly once,
	// at construction, and this guard made a second call a silent no-op,
	// which would have left the panel permanently showing whichever
	// document happened to be active first.
	if (!_isInitialized)
	{
		connectSignalsAndSlots();

		// ── Punctual-lights tree resize handle ─────────────────────────────────
		// Give the tree a compact default height (shows ~4–5 items); users drag
		// the handle strip below it to reveal more.  The tree's own scroll bar
		// handles any overflow.
		{
			// Fix the tree at a compact default height; the user can drag the handle
			// down to expand.  setFixedHeight is used so the layout immediately
			// honours the size regardless of content count.
			constexpr int kDefaultTreeH = 110;
			ui->treePunctualLights->setFixedHeight(kDefaultTreeH);

			// Thin drag-handle strip inserted below the tree in the group box layout.
			auto* handle = new QFrame(ui->groupBoxPunctualLights);
			handle->setObjectName("treeLightResizeHandle");
			handle->setFrameShape(QFrame::HLine);
			handle->setFrameShadow(QFrame::Sunken);
			handle->setCursor(Qt::SizeVerCursor);
			handle->setFixedHeight(6);
			handle->setToolTip(tr("Drag to resize the lights list"));

			auto* gl = qobject_cast<QGridLayout*>(ui->groupBoxPunctualLights->layout());
			if (gl)
				gl->addWidget(handle, 1, 0);

			handle->installEventFilter(this);
			_lightTreeResizeHandle = handle;
		}
		// ────────────────────────────────────────────────────────────────────────

		_isInitialized = true;
	}

	// Refresh punctual lights tree whenever SceneGraph light data changes
	// (model loaded/unloaded, or individual light toggled from elsewhere).
	// Disconnect the previous document's SceneGraph first - without this,
	// rebinding to a different document repeatedly would leave every earlier
	// SceneGraph still wired to this panel's slot.
	disconnect(_sceneGraphLightDataConnection);
	if (_sceneGraph)
	{
		_sceneGraphLightDataConnection = connect(_sceneGraph, &SceneGraph::lightDataChanged,
		        this, &VisualizationEnvironmentPanel::refreshPunctualLightsTree);
	}

	// Rebind the two ViewportWidget-scoped connections connectSignalsAndSlots()
	// used to make directly, back when it ran once per document - disconnect
	// from whichever document's viewport this panel was previously bound to
	// first, same reasoning as the SceneGraph connection above.
	disconnect(_viewportCameraUpAxisConnection);
	disconnect(_viewportRenderingModeConnection);
	if (_viewportWidget)
	{
		_viewportCameraUpAxisConnection = connect(_viewportWidget, &ViewportWidget::cameraUpAxisChanged,
		        this, &VisualizationEnvironmentPanel::updateSkyBoxRotationLabels);
		updateSkyBoxRotationLabels(_viewportWidget->isCameraUpAxisZUp());

		_viewportRenderingModeConnection = connect(_viewportWidget, &ViewportWidget::renderingModeChanged,
		        this, &VisualizationEnvironmentPanel::updateControlDependencies);
	}

	// Reflect the newly-bound document's actual ground mode/floor offset into
	// the UI - NOT the other way around. This used to push the (freshly
	// Designer-defaulted) UI state onto the viewport, which only made sense
	// when initialize() ran once, for a brand new document whose
	// ViewportWidget genuinely had no ground mode set yet. Now that the same
	// call rebinds an already-configured document, doing that would
	// overwrite its actual ground mode with whatever the PREVIOUSLY active
	// document had left checked in the UI.
	if (_viewportWidget && ui)
	{
		const GroundMode mode = _viewportWidget->groundMode();
		QSignalBlocker blockFloor(ui->radioButtonGroundFloor);
		QSignalBlocker blockNone(ui->radioButtonGroundNone);
		QSignalBlocker blockGrid(ui->radioButtonGroundGrid);
		QSignalBlocker blockInfinitePlane(ui->radioButtonGroundInfinitePlane);
		ui->radioButtonGroundFloor->setChecked(mode == GroundMode::Floor);
		ui->radioButtonGroundGrid->setChecked(mode == GroundMode::Grid);
		ui->radioButtonGroundInfinitePlane->setChecked(mode == GroundMode::InfinitePlane);
		ui->radioButtonGroundNone->setChecked(mode == GroundMode::None);

		QSignalBlocker blockFloorOffset(ui->doubleSpinBoxFloorOffset);
		ui->doubleSpinBoxFloorOffset->setValue(_viewportWidget->getFloorOffsetPercent());
	}

	syncSkyBoxSelectionSilently();
	// Cheap (float assignment + view update, not a texture reload) and
	// idempotent since we're reading the value straight back from the same
	// viewport it's already applied to - safe to call unconditionally here.
	restoreSkyBoxRotationDegrees(_viewportWidget ? _viewportWidget->getSkyBoxZRotationDegrees() : 0.0f);

	// Reflect every other remaining per-document toggle/value directly from
	// the viewport, signals blocked so nothing here re-applies anything back
	// (all of it is already active and correct on this viewport - this is a
	// display-only sync). Covers the same class of bug as the ground-mode/
	// skybox fixes above: any control that ISN'T re-synced here still shows
	// whatever the PREVIOUSLY active document last left it at.
	if (_viewportWidget && ui)
	{
		{
			QSignalBlocker b1(ui->checkBoxShadowMapping), b2(ui->checkBoxSelfShadows),
				b3(ui->checkBoxReflections), b4(ui->checkBoxEnvMapping), b5(ui->checkBoxIBL),
				b6(ui->checkBoxDefaultLights), b7(ui->checkBoxShowLights),
				b8(ui->checkBoxHDRToneMapping), b9(ui->checkBoxGammaCorrection),
				b10(ui->checkBoxFloorTexture), b11(ui->checkBoxSkyBox);
			ui->checkBoxShadowMapping->setChecked(_viewportWidget->areShadowsEnabled());
			ui->checkBoxSelfShadows->setChecked(_viewportWidget->areSelfShadowsEnabled());
			ui->checkBoxReflections->setChecked(_viewportWidget->areReflectionsEnabled());
			ui->checkBoxEnvMapping->setChecked(_viewportWidget->isEnvironmentMapEnabled());
			ui->checkBoxIBL->setChecked(_viewportWidget->isIBLEnabled());
			ui->checkBoxDefaultLights->setChecked(_viewportWidget->areDefaultLightsEnabled());
			ui->checkBoxShowLights->setChecked(_viewportWidget->areLightsShown());
			ui->checkBoxHDRToneMapping->setChecked(_viewportWidget->isHDRToneMappingEnabled());
			ui->checkBoxGammaCorrection->setChecked(_viewportWidget->isGammaCorrectionEnabled());
			ui->checkBoxFloorTexture->setChecked(_viewportWidget->isFloorTextureShown());
			ui->checkBoxSkyBox->setChecked(_viewportWidget->isSkyBoxShown());
		}

		{
			QSignalBlocker b1(ui->doubleSpinBoxScreenGamma), b2(ui->doubleSpinBoxEnvMapExposure),
				b3(ui->doubleSpinBoxIBLExposure), b4(ui->doubleSpinBoxSkyBoxFOV), b5(ui->sliderSkyBoxBlur);
			ui->doubleSpinBoxScreenGamma->setValue(_viewportWidget->getScreenGamma());
			// getEnvMapExposure()/getIBLExposure() return the LINEAR
			// multiplier SceneRenderController actually stores (1.0f =
			// neutral by default), but these spinboxes are in STOPS (see
			// ViewportWidget::setEnvMapExposure()/setIBLExposure(), which
			// convert stops->linear via std::pow(2.0f, exposure) before
			// storing) - std::log2() is the inverse, matching the same
			// conversion ModelViewer.cpp's project save/load code already
			// uses for these exact two fields (envMapExposureStops/
			// iblExposureStops). Without this, the panel showed "1.0"
			// (misread as if it were already in stops) for the neutral
			// linear-1.0 default, when it should show "0.0" (0 stops).
			// 1.0e-6f floor avoids log2(0) = -infinity for a
			// theoretically-possible zero/negative stored value.
			ui->doubleSpinBoxEnvMapExposure->setValue(std::log2(std::max(_viewportWidget->getEnvMapExposure(), 1.0e-6f)));
			ui->doubleSpinBoxIBLExposure->setValue(std::log2(std::max(_viewportWidget->getIBLExposure(), 1.0e-6f)));
			ui->doubleSpinBoxSkyBoxFOV->setValue(_viewportWidget->getSkyBoxFOV());
			ui->sliderSkyBoxBlur->setValue(_viewportWidget->getSkyBoxBlurPercent());
		}

		// Sliders store 0-100 int, viewport stores 0.0-1.0 float (see
		// onShadowDarknessChanged()/onShadowCatcherMetallicChanged()/
		// onShadowCatcherRoughnessChanged() for the same *100/100 pairing).
		{
			QSignalBlocker b1(ui->sliderShadowDarkness), b2(ui->sliderShadowCatcherMetallic),
				b3(ui->sliderShadowCatcherRoughness);
			const int darkness = qRound(_viewportWidget->shadowCatcherDarkness() * 100.0f);
			const int metallic = qRound(_viewportWidget->shadowCatcherMetalness() * 100.0f);
			const int roughness = qRound(_viewportWidget->shadowCatcherRoughness() * 100.0f);
			ui->sliderShadowDarkness->setValue(darkness);
			ui->sliderShadowCatcherMetallic->setValue(metallic);
			ui->sliderShadowCatcherRoughness->setValue(roughness);
			ui->labelShadowDarknessValue->setText(QString::number(darkness / 100.0f, 'f', 2));
			ui->labelShadowCatcherMetallicValue->setText(QString::number(metallic / 100.0f, 'f', 2));
			ui->labelShadowCatcherRoughnessValue->setText(QString::number(roughness / 100.0f, 'f', 2));
		}

		// comboBoxHDRToneMappingMode's items carry their HDRToneMapMode enum
		// value in UserRole (set in the constructor) - find the item whose
		// role matches the viewport's actual current mode.
		{
			const int currentMode = static_cast<int>(_viewportWidget->getHDRToneMappingMode());
			for (int i = 0; i < ui->comboBoxHDRToneMappingMode->count(); ++i)
			{
				if (ui->comboBoxHDRToneMappingMode->itemData(i).toInt() == currentMode)
				{
					QSignalBlocker block(ui->comboBoxHDRToneMappingMode);
					ui->comboBoxHDRToneMappingMode->setCurrentIndex(i);
					break;
				}
			}
		}

		updateButtonStyles();

		// Re-evaluate dependent-control enabled state (checkBoxSelfShadows/
		// comboBoxShadowQuality gated on checkBoxShadowMapping, the ADS/
		// RayTraced mode gates, etc.) now that every checkbox above reflects
		// this document's actual values - moved here from earlier in this
		// function so it sees the FINAL synced state, not whatever was left
		// over from the previous document.
		updateControlDependencies();
	}

	// Repopulate immediately for the newly-bound document rather than waiting
	// for its next lightDataChanged emission, which may not come for a while.
	refreshPunctualLightsTree();
}

void VisualizationEnvironmentPanel::connectSignalsAndSlots()
{
	if (!ui)
		return;

	// ===== Light Color Buttons =====
	connect(ui->pushButtonLightColor, &QPushButton::clicked, this, &VisualizationEnvironmentPanel::onLightColorClicked);
	connect(ui->pushButtonDefaultLights, &QPushButton::clicked, this, &VisualizationEnvironmentPanel::onDefaultLightsClicked);

	// ===== Light Position Sliders =====
	connect(ui->sliderLightPosX, QOverload<int>::of(&QSlider::valueChanged), this, &VisualizationEnvironmentPanel::onLightPosXChanged);
	connect(ui->sliderLightPosY, QOverload<int>::of(&QSlider::valueChanged), this, &VisualizationEnvironmentPanel::onLightPosYChanged);
	connect(ui->sliderLightPosZ, QOverload<int>::of(&QSlider::valueChanged), this, &VisualizationEnvironmentPanel::onLightPosZChanged);

	// ===== Lighting Checkboxes =====
	connect(ui->checkBoxDefaultLights, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onDefaultLightsChanged);
	connect(ui->checkBoxShowLights, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onShowLightsChanged);
	connect(ui->checkBoxIBL, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onIBLChanged);

	// ===== Punctual Lights Tree =====
	connect(ui->treePunctualLights, &QTreeWidget::itemChanged,
	        this, &VisualizationEnvironmentPanel::onPunctualLightItemChanged);

	// Group box hidden until a model with lights is loaded
	ui->groupBoxPunctualLights->setVisible(false);

	// ===== Skybox Controls =====
	// cameraUpAxisChanged/renderingModeChanged are connected per-rebind, not
	// here - see initialize(), which (re)connects them to whichever
	// document's ViewportWidget is currently bound, since this function only
	// ever runs once.
	connect(ui->checkBoxSkyBox, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onSkyBoxStateChanged);
	connect(ui->checkBoxSkyBoxHDRI, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onSkyBoxHDRIChanged);
	connect(ui->checkBoxSkyBoxHDRI, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onLoadSkyBoxPresetMaps);
	connect(ui->sliderSkyBoxBlur, QOverload<int>::of(&QSlider::valueChanged), this, &VisualizationEnvironmentPanel::onSkyBoxBlurChanged);
	connect(ui->doubleSpinBoxSkyBoxFOV, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onSkyBoxFOVChanged);
	connect(ui->comboBoxSkyBoxRotation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VisualizationEnvironmentPanel::onSkyBoxRotationPresetChanged);
	connect(ui->sliderSkyBoxRotationFine, QOverload<int>::of(&QSlider::valueChanged), this, &VisualizationEnvironmentPanel::onSkyBoxRotationFineChanged);
	connect(ui->comboBoxSkyBoxMaps, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VisualizationEnvironmentPanel::onSkyBoxMapsChanged);
	connect(ui->pushButtonSkyBoxTex, &QPushButton::clicked, this, &VisualizationEnvironmentPanel::onSkyBoxTextureClicked);

	// ===== Shadow Controls =====
	connect(ui->checkBoxShadowMapping, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onShadowMappingStateChanged);
	connect(ui->checkBoxSelfShadows, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onSelfShadowsChanged);
	connect(ui->comboBoxShadowQuality, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VisualizationEnvironmentPanel::onShadowQualityChanged);

	// ===== Floor Controls =====
	connect(ui->radioButtonGroundNone, &QRadioButton::toggled, this, &VisualizationEnvironmentPanel::onGroundModeChanged);
	connect(ui->radioButtonGroundFloor, &QRadioButton::toggled, this, &VisualizationEnvironmentPanel::onGroundModeChanged);
	connect(ui->radioButtonGroundGrid, &QRadioButton::toggled, this, &VisualizationEnvironmentPanel::onGroundModeChanged);
	connect(ui->radioButtonGroundInfinitePlane, &QRadioButton::toggled, this, &VisualizationEnvironmentPanel::onGroundModeChanged);
	connect(ui->checkBoxFloorTexture, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onFloorTextureStateChanged);
	connect(ui->checkBoxReflections, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onReflectionsChanged);
	connect(ui->sliderShadowDarkness, &QSlider::valueChanged, this, &VisualizationEnvironmentPanel::onShadowDarknessChanged);
	connect(ui->pushButtonShadowCatcherColor, &QPushButton::clicked, this, &VisualizationEnvironmentPanel::onShadowCatcherColorClicked);
	connect(ui->sliderShadowCatcherMetallic, &QSlider::valueChanged, this, &VisualizationEnvironmentPanel::onShadowCatcherMetallicChanged);
	connect(ui->sliderShadowCatcherRoughness, &QSlider::valueChanged, this, &VisualizationEnvironmentPanel::onShadowCatcherRoughnessChanged);
	connect(ui->checkBoxEnvMapping, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onEnvMappingChanged);
	connect(ui->doubleSpinBoxFloorOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onFloorOffsetChanged);
	connect(ui->doubleSpinBoxRepeatS, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onRepeatSChanged);
	connect(ui->doubleSpinBoxRepeatT, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onRepeatTChanged);
	connect(ui->pushButtonFloorTexture, &QPushButton::clicked, this, &VisualizationEnvironmentPanel::onFloorTextureClicked);

	// ===== HDR Controls =====
	connect(ui->checkBoxHDRToneMapping, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onHDRToneMappingStateChanged);
	connect(ui->comboBoxHDRToneMappingMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VisualizationEnvironmentPanel::onHDRToneMappingModeChanged);
	connect(ui->doubleSpinBoxEnvMapExposure, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onEnvMapExposureChanged);
	connect(ui->doubleSpinBoxIBLExposure, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onIBLExposureChanged);

	// ===== Gamma Controls =====
	connect(ui->checkBoxGammaCorrection, &QCheckBox::toggled, this, &VisualizationEnvironmentPanel::onGammaCorrectionStateChanged);
	connect(ui->doubleSpinBoxScreenGamma, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &VisualizationEnvironmentPanel::onScreenGammaChanged);

	// ===== Default Values Button =====
	connect(ui->pushButtonDefaultEnvValues, &QPushButton::clicked, this, &VisualizationEnvironmentPanel::onDefaultEnvValuesClicked);
}

void VisualizationEnvironmentPanel::updateControlDependencies()
{
	if (!ui)
		return;

	bool skyBoxEnabled = ui->checkBoxSkyBox->isChecked();
	bool floorEnabled = ui->radioButtonGroundFloor->isChecked();
	bool gridEnabled = ui->radioButtonGroundGrid->isChecked();
	bool infinitePlaneSelected = ui->radioButtonGroundInfinitePlane->isChecked();
	bool groundEnabled = floorEnabled || gridEnabled || infinitePlaneSelected;
	bool shadowsEnabled = ui->checkBoxShadowMapping->isChecked();
	bool hdrEnabled = ui->checkBoxHDRToneMapping->isChecked();
	bool gammaEnabled = ui->checkBoxGammaCorrection->isChecked();
	bool skyBoxHDRIEnabled = skyBoxEnabled && ui->checkBoxSkyBoxHDRI->isChecked();
	bool floorTextureEnabled = floorEnabled && ui->checkBoxFloorTexture->isChecked();
	bool adsMode = _viewportWidget && _viewportWidget->getRenderingMode() == RenderingMode::ADS_BLINN_PHONG;
	// NOT RenderingMode::RAY_TRACED - ModelViewer::onRenderingModeSelected()'s
	// "RayTraced" branch deliberately keeps getRenderingMode() at
	// PHYSICALLY_BASED_RENDERING (so the live PBR raster feed keeps showing
	// while the camera moves) and layers ray tracing on top via the
	// SEPARATE _rayTracedArmed flag instead - see ViewportWidget::
	// armRayTracedRenderingMode()'s doc comment. getRenderingMode()==
	// RAY_TRACED is essentially only ever set by the viewerState-restore
	// path (ModelViewer.cpp), never by live interaction, so gating on it
	// left this checkbox permanently disabled during normal use.
	bool rayTracedMode = _viewportWidget && _viewportWidget->isRayTracedRenderingModeArmed();

	// Environment Mapping (ADS) - legacy shadeBlinnPhong() reflection term
	// only, unrelated to Environment IBL's KHR PBR lighting (see
	// evaluateSheenIBL()'s doc comment in main_scene.frag) - only meaningful
	// while ADS is the active shading model.
	ui->checkBoxEnvMapping->setEnabled(adsMode);

	// Skybox dependencies
	ui->labelSkyBoxBlur->setEnabled(skyBoxEnabled);
	ui->sliderSkyBoxBlur->setEnabled(skyBoxEnabled);
	ui->labelSkyBoxBlurValue->setEnabled(skyBoxEnabled);
	ui->labelFOV->setEnabled(skyBoxEnabled);
	ui->doubleSpinBoxSkyBoxFOV->setEnabled(skyBoxEnabled);

	// Rotation deliberately NOT gated on skyBoxEnabled (unlike blur/FOV above,
	// which only affect the drawn background itself): setSkyBoxZRotationDegrees()
	// also rebuilds updateEnvMapRotationMatrix(), which orients the SAME
	// environment map used for IBL reflections/lighting - see that method's
	// own doc comment. That lighting effect is visible whether or not the
	// skybox is drawn behind the model, so hiding the skybox shouldn't lock
	// the user out of rotating the light itself.
	ui->labelSkyBoxRotation->setEnabled(true);
	ui->comboBoxSkyBoxRotation->setEnabled(true);
	ui->sliderSkyBoxRotationFine->setEnabled(true);
	ui->labelSkyBoxRotationFineValue->setEnabled(true);

	// Floor dependencies
	ui->checkBoxReflections->setEnabled(floorEnabled);
	ui->checkBoxFloorTexture->setEnabled(floorEnabled);
	// Infinite Plane / Shadow Catcher (path tracer only, see
	// GroundMode::InfinitePlane's and RtMaterial::isShadowCatcher's doc
	// comments) - raster has no equivalent, so the radio itself is only
	// selectable in ray-traced mode (it stays checked-but-disabled if the
	// user leaves ray-traced mode after selecting it, matching how every
	// other PT-only control in this panel behaves - no forced fallback to
	// a different ground mode). Being its own ground mode (mutually
	// exclusive with Floor/Grid/None) means Reflections/Floor Texture are
	// already naturally disabled whenever this is selected (floorEnabled is
	// false), with no need to cross-disable them explicitly.
	ui->radioButtonGroundInfinitePlane->setEnabled(rayTracedMode);
	// Darkness/Color only mean anything for the shadow-catching illusion
	// itself (isShadowCatcher gate) - meaningless for an ordinary opaque
	// floor, so they stay Infinite-Plane-only. Metallic/Roughness, however,
	// now also drive the ordinary floor's real PBR reflectivity (see
	// RtSceneBuilder::convertFloorMaterial()'s doc comment), so they're
	// unlocked for plain Floor mode too, not just Infinite Plane.
	bool shadowCatcherSettingsEnabled = infinitePlaneSelected && rayTracedMode;
	bool floorReflectanceSettingsEnabled = (floorEnabled || infinitePlaneSelected) && rayTracedMode;
	// Metallic/Roughness now double as the ordinary floor's reflectance
	// controls (see convertFloorMaterial()'s doc comment) - retitle the
	// group box so it doesn't misleadingly say "Shadow Catcher" while the
	// user is actually dialing in plain Floor mode's reflectivity.
	ui->groupBoxShadowCatcher->setTitle(infinitePlaneSelected ? tr("Shadow Catcher Settings") : tr("Floor Reflectance Settings"));
	ui->labelShadowDarkness->setEnabled(shadowCatcherSettingsEnabled);
	ui->sliderShadowDarkness->setEnabled(shadowCatcherSettingsEnabled);
	ui->labelShadowDarknessValue->setEnabled(shadowCatcherSettingsEnabled);
	ui->labelShadowCatcherColor->setEnabled(shadowCatcherSettingsEnabled);
	ui->pushButtonShadowCatcherColor->setEnabled(shadowCatcherSettingsEnabled);
	ui->labelShadowCatcherMetallic->setEnabled(floorReflectanceSettingsEnabled);
	ui->sliderShadowCatcherMetallic->setEnabled(floorReflectanceSettingsEnabled);
	ui->labelShadowCatcherMetallicValue->setEnabled(floorReflectanceSettingsEnabled);
	ui->labelShadowCatcherRoughness->setEnabled(floorReflectanceSettingsEnabled);
	ui->sliderShadowCatcherRoughness->setEnabled(floorReflectanceSettingsEnabled);
	ui->labelShadowCatcherRoughnessValue->setEnabled(floorReflectanceSettingsEnabled);
	ui->labelFloorOffset->setEnabled(groundEnabled);
	ui->doubleSpinBoxFloorOffset->setEnabled(groundEnabled);
	ui->labelRepeatS->setEnabled(floorTextureEnabled);
	ui->labelRepeatT->setEnabled(floorTextureEnabled);
	ui->doubleSpinBoxRepeatS->setEnabled(floorTextureEnabled);
	ui->doubleSpinBoxRepeatT->setEnabled(floorTextureEnabled);	
	ui->pushButtonFloorTexture->setEnabled(floorTextureEnabled);

	// Shadow dependencies
	ui->checkBoxSelfShadows->setEnabled(shadowsEnabled);
	ui->labelShadowQuality->setEnabled(shadowsEnabled);
	ui->comboBoxShadowQuality->setEnabled(shadowsEnabled);

	// HDR dependencies	
	ui->labelToneMappingMode->setEnabled(hdrEnabled);
	ui->labelEnvMapExposure->setEnabled(hdrEnabled);
	ui->labelIBLExposure->setEnabled(hdrEnabled);
	ui->comboBoxHDRToneMappingMode->setEnabled(hdrEnabled);
	ui->doubleSpinBoxEnvMapExposure->setEnabled(hdrEnabled);
	ui->doubleSpinBoxIBLExposure->setEnabled(hdrEnabled);

	// Gamma dependencies
	ui->labelScreenGamma->setEnabled(gammaEnabled);
	ui->doubleSpinBoxScreenGamma->setEnabled(gammaEnabled);
}

void VisualizationEnvironmentPanel::updateButtonStyles()
{
	if (!_viewportWidget || !ui)
		return;

	QVector4D lightColor = _viewportWidget->getDefaultLightColor();
	QColor diffuseColor = QColor::fromRgbF(lightColor.x(), lightColor.y(), lightColor.z());
	QString diffuseStyle = QString("background-color: %1; color: %2; border: 1px solid gray;")
		.arg(diffuseColor.name(), diffuseColor.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
	ui->pushButtonLightColor->setStyleSheet(diffuseStyle);

	const QVector3D catcherColor = _viewportWidget->shadowCatcherBaseColor();
	const QColor catcherQColor = QColor::fromRgbF(catcherColor.x(), catcherColor.y(), catcherColor.z());
	const QString catcherStyle = QString("background-color: %1; color: %2; border: 1px solid gray;")
		.arg(catcherQColor.name(), catcherQColor.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
	ui->pushButtonShadowCatcherColor->setStyleSheet(catcherStyle);
}

// ==================== LIGHT COLOR BUTTONS ====================

void VisualizationEnvironmentPanel::onLightColorClicked()
{
	if (!_viewportWidget || !ui)
		return;

	QVector4D lightColor = _viewportWidget->getDefaultLightColor();
	QColor c = QColorDialog::getColor(QColor::fromRgbF(lightColor.x(), lightColor.y(), lightColor.z(), lightColor.w()), this, "Default Light Color");
	if (c.isValid())
	{
		_viewportWidget->setDefaultLightColor(QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF()));
		updateButtonStyles();
		_viewportWidget->updateView();
	}
}

void VisualizationEnvironmentPanel::onDefaultLightsClicked()
{
	if (!_viewportWidget || !ui)
		return;

	_viewportWidget->setDefaultLightColor(QVector4D(1.0f, 1.0f, 1.0f, 1.0f));

	// Set light position sliders - block signals to prevent cascading during set
	ui->sliderLightPosX->blockSignals(true);
	ui->sliderLightPosY->blockSignals(true);
	ui->sliderLightPosZ->blockSignals(true);

	// (max+min)/2 is still the right midpoint tick regardless of model scale
	// (the slider's int range is now always [0, kLightSliderSteps] - see
	// LightAxisSliderMapping's doc comment in the header), but the Z default
	// below is expressed in real-world offset units, not ticks, so it needs
	// offsetToSliderTick() rather than a direct setValue().
	ui->sliderLightPosX->setValue((ui->sliderLightPosX->maximum() + ui->sliderLightPosX->minimum()) / 2);
	ui->sliderLightPosY->setValue((ui->sliderLightPosY->maximum() + ui->sliderLightPosY->minimum()) / 2);

	const float range = _viewportWidget->getBoundingSphere().getRadius() * 4.0f;
	const float defaultZOffset = (-range / 3.0f + range / 2.0f) / 2.0f;
	ui->sliderLightPosZ->setValue(offsetToSliderTick(_lightPosZMapping, defaultZOffset));

	ui->sliderLightPosX->blockSignals(false);
	ui->sliderLightPosY->blockSignals(false);
	ui->sliderLightPosZ->blockSignals(false);

	updateLightPositionValueLabels();

	// Manually update light offset
	_viewportWidget->setLightOffset(QVector3D(
		sliderTickToOffset(_lightPosXMapping, ui->sliderLightPosX->value()),
		sliderTickToOffset(_lightPosYMapping, ui->sliderLightPosY->value()),
		sliderTickToOffset(_lightPosZMapping, ui->sliderLightPosZ->value())));

	// Set lighting checkboxes - block signals during set
	ui->checkBoxDefaultLights->blockSignals(true);
	ui->checkBoxIBL->blockSignals(true);
	ui->checkBoxShowLights->blockSignals(true);

	ui->checkBoxDefaultLights->setChecked(true);
	ui->checkBoxIBL->setChecked(true);
	ui->checkBoxShowLights->setChecked(false);

	ui->checkBoxDefaultLights->blockSignals(false);
	ui->checkBoxIBL->blockSignals(false);
	ui->checkBoxShowLights->blockSignals(false);

	// Manually trigger ViewportWidget calls
	_viewportWidget->useDefaultLights(true);
	_viewportWidget->usePunctualLights(true);  // kept true; tree checkboxes control per-light enable
	_viewportWidget->useIBL(true);
	_viewportWidget->showLights(false);

	updateButtonStyles();
	_viewportWidget->updateView();
}

// ==================== LIGHT POSITION SLIDERS ====================

void VisualizationEnvironmentPanel::onLightPosXChanged(int value)
{
	if (!_viewportWidget || !ui)
		return;

	_viewportWidget->setLightOffset(QVector3D(
		sliderTickToOffset(_lightPosXMapping, ui->sliderLightPosX->value()),
		sliderTickToOffset(_lightPosYMapping, ui->sliderLightPosY->value()),
		sliderTickToOffset(_lightPosZMapping, ui->sliderLightPosZ->value())));
	updateLightPositionValueLabels();
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onLightPosYChanged(int value)
{
	if (!_viewportWidget || !ui)
		return;

	_viewportWidget->setLightOffset(QVector3D(
		sliderTickToOffset(_lightPosXMapping, ui->sliderLightPosX->value()),
		sliderTickToOffset(_lightPosYMapping, ui->sliderLightPosY->value()),
		sliderTickToOffset(_lightPosZMapping, ui->sliderLightPosZ->value())));
	updateLightPositionValueLabels();
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onLightPosZChanged(int value)
{
	if (!_viewportWidget || !ui)
		return;

	_viewportWidget->setLightOffset(QVector3D(
		sliderTickToOffset(_lightPosXMapping, ui->sliderLightPosX->value()),
		sliderTickToOffset(_lightPosYMapping, ui->sliderLightPosY->value()),
		sliderTickToOffset(_lightPosZMapping, ui->sliderLightPosZ->value())));
	updateLightPositionValueLabels();
	_viewportWidget->updateView();
}

// ==================== LIGHTING CHECKBOXES ====================

void VisualizationEnvironmentPanel::onDefaultLightsChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->useDefaultLights(checked);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::applyRayTracedGroundDefaultsOnce()
{
	if (!_viewportWidget || !ui)
		return;

	// Unconditional, every time Ray-Traced mode is (re-)selected via the
	// toolbar/shortcut - not a "first time only" default. Ground mode and
	// default lights are part of what DEFINES Ray-Traced mode (the
	// shadow-catcher look), same as onDisplayModeChanged()'s realism-driven
	// checkboxes just below it in the call chain - so switching INTO this
	// mode always re-asserts InfinitePlane/lights-off regardless of whatever
	// the user left ground mode/default lights at during a previous PBR or
	// Ray-Traced session. The user is still free to change either
	// afterward, for as long as they stay in this mode - only the mode
	// SWITCH itself is authoritative.
	//
	// All four radios in the group are blocked, not just InfinitePlane -
	// see onDisplayModeChanged()'s identical comment: Qt's auto-exclusive
	// group implicitly unchecks whichever radio (Floor/None/Grid) was
	// previously checked as a side effect of setChecked(true) here, and
	// that implicit uncheck would otherwise emit its own real toggled(false)
	// signal via onGroundModeChanged().
	ui->radioButtonGroundInfinitePlane->blockSignals(true);
	ui->radioButtonGroundFloor->blockSignals(true);
	ui->radioButtonGroundNone->blockSignals(true);
	ui->radioButtonGroundGrid->blockSignals(true);
	ui->radioButtonGroundInfinitePlane->setChecked(true);
	ui->radioButtonGroundInfinitePlane->blockSignals(false);
	ui->radioButtonGroundFloor->blockSignals(false);
	ui->radioButtonGroundNone->blockSignals(false);
	ui->radioButtonGroundGrid->blockSignals(false);
	_viewportWidget->setGroundMode(GroundMode::InfinitePlane);

	ui->checkBoxDefaultLights->blockSignals(true);
	ui->checkBoxDefaultLights->setChecked(false);
	ui->checkBoxDefaultLights->blockSignals(false);
	_viewportWidget->useDefaultLights(false);

	updateControlDependencies();
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::restoreDefaultLightsForAds()
{
	if (!_viewportWidget || !ui)
		return;

	// ADS mode never calls switchToRealisticRendering()/setRealismEnabled(),
	// so onDisplayModeChanged() (which re-asserts default lights on for
	// every ADS/PBR/Ray-Traced switch) never runs for it - meaning default
	// lights stayed off forever after a Ray-Traced session if the only fix
	// were there. ADS deliberately does NOT get the rest of onDisplayModeChanged()'s
	// realism-driven defaults (floor/shadows/reflections/env-map) - it just
	// needs its lights back, independent of Realistic rendering.
	ui->checkBoxDefaultLights->blockSignals(true);
	ui->checkBoxDefaultLights->setChecked(true);
	ui->checkBoxDefaultLights->blockSignals(false);
	_viewportWidget->useDefaultLights(true);
	_viewportWidget->updateView();
}

// ---------------------------------------------------------------------------
// Punctual Lights tree
// ---------------------------------------------------------------------------

static const int kLightIndexRole    = Qt::UserRole;       // int  — light index in file (-1 = file item)
static const int kSourceFileRole    = Qt::UserRole + 1;   // QString — absolute source file path

QTreeWidgetItem* VisualizationEnvironmentPanel::makeLightFileItem(const QString& sourceFile) const
{
	QTreeWidgetItem* item = new QTreeWidgetItem();
	item->setText(0, QFileInfo(sourceFile).fileName());
	item->setData(0, kSourceFileRole, sourceFile);
	item->setData(0, kLightIndexRole, -1);
	// Tri-state: Qt will manage checked/partial/unchecked automatically
	// once children are added with individual check states.
	// Qt::ItemIsUserTristate is intentionally omitted.  With it, clicking a
	// Checked parent would first land on PartiallyChecked (a no-op for the
	// handler), requiring a second click to reach Unchecked.  Without it Qt
	// only toggles Checked ↔ Unchecked on user interaction; PartiallyChecked
	// is still set programmatically by the leaf handler when children diverge.
	item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
	QFont f = item->font(0);
	f.setBold(true);
	item->setFont(0, f);
	// Not selectable — clicking the row only toggles the checkbox
	item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
	return item;
}

QTreeWidgetItem* VisualizationEnvironmentPanel::makeLightLeafItem(const GltfLightEntry& entry,
                                                                   int lightIndex) const
{
	// Pick a type label for the tooltip
	const int t = entry.gpuLight.type;
	QString typeStr = (t == 0) ? QStringLiteral("Directional")
	               : (t == 1) ? QStringLiteral("Point")
	               :             QStringLiteral("Spot");

	QString displayName = entry.name.isEmpty()
	                      ? QString("%1 %2").arg(typeStr).arg(lightIndex + 1)
	                      : entry.name;

	QTreeWidgetItem* item = new QTreeWidgetItem();
	item->setText(0, displayName);
	item->setToolTip(0, typeStr);
	item->setData(0, kLightIndexRole, lightIndex);
	item->setCheckState(0, entry.enabled ? Qt::Checked : Qt::Unchecked);
	item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
	item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
	return item;
}

void VisualizationEnvironmentPanel::refreshPunctualLightsTree()
{
	if (!_sceneGraph || !ui)
		return;

	QTreeWidget* tree = ui->treePunctualLights;

	// Block itemChanged while rebuilding so we don't trigger onPunctualLightItemChanged
	tree->blockSignals(true);
	tree->clear();

	const QStringList files = _sceneGraph->filesWithLights();
	for (const QString& sourceFile : files)
	{
		const GltfLightData ld = _sceneGraph->lightDataForFile(sourceFile);
		if (ld.isEmpty())
			continue;

		QTreeWidgetItem* fileItem = makeLightFileItem(sourceFile);
		tree->addTopLevelItem(fileItem);

		for (int i = 0; i < ld.lights.size(); ++i)
		{
			QTreeWidgetItem* leaf = makeLightLeafItem(ld.lights[i], i);
			leaf->setData(0, kSourceFileRole, sourceFile);
			fileItem->addChild(leaf);
		}

		fileItem->setExpanded(true);

		// Set parent tri-state from children
		bool anyOn  = false, anyOff = false;
		for (int i = 0; i < ld.lights.size(); ++i)
		{
			(ld.lights[i].enabled ? anyOn : anyOff) = true;
		}
		fileItem->setCheckState(0, anyOn && anyOff ? Qt::PartiallyChecked
		                         : anyOn           ? Qt::Checked
		                                           : Qt::Unchecked);
	}

	tree->blockSignals(false);

	// Show the group box only when at least one file has lights
	ui->groupBoxPunctualLights->setVisible(!files.isEmpty());
}

void VisualizationEnvironmentPanel::onPunctualLightItemChanged(QTreeWidgetItem* item, int column)
{
	if (!_sceneGraph || !_viewportWidget || !item || column != 0)
		return;

	const int        lightIndex = item->data(0, kLightIndexRole).toInt();
	const QString    sourceFile = item->data(0, kSourceFileRole).toString();
	const Qt::CheckState state  = item->checkState(0);

	// Block lightDataChanged from firing while we're inside the itemChanged
	// handler.  setLightEnabled() would emit lightDataChanged() →
	// refreshPunctualLightsTree() → tree->clear(), which deletes the 'item'
	// pointer we're still using — instant crash on rapid toggling.
	// The tree UI is managed manually below; we trigger the GPU upload ourselves.
	QSignalBlocker sceneGraphBlocker(_sceneGraph);

	if (lightIndex == -1)
	{
		// File-level item toggled — apply same state to all children.
		ui->treePunctualLights->blockSignals(true);
		const bool enable = (state != Qt::Unchecked);
		for (int i = 0; i < item->childCount(); ++i)
		{
			item->child(i)->setCheckState(0, enable ? Qt::Checked : Qt::Unchecked);
			_sceneGraph->setLightEnabled(sourceFile, i, enable);
		}
		ui->treePunctualLights->blockSignals(false);
	}
	else
	{
		// Leaf item toggled — update SceneGraph for this one light.
		_sceneGraph->setLightEnabled(sourceFile, lightIndex, state == Qt::Checked);

		// Update parent tri-state without re-entering this slot.
		if (QTreeWidgetItem* parent = item->parent())
		{
			ui->treePunctualLights->blockSignals(true);
			bool anyOn = false, anyOff = false;
			for (int i = 0; i < parent->childCount(); ++i)
				(parent->child(i)->checkState(0) == Qt::Checked ? anyOn : anyOff) = true;
			parent->setCheckState(0, anyOn && anyOff ? Qt::PartiallyChecked
			                       : anyOn           ? Qt::Checked
			                                         : Qt::Unchecked);
			ui->treePunctualLights->blockSignals(false);
		}
	}

	// sceneGraphBlocker goes out of scope here — signals re-enabled on _sceneGraph.
	// Rebuild the GPU light list and upload directly without going through the
	// lightDataChanged → refreshPunctualLightsTree path.
	_viewportWidget->applyEnabledLightList(_sceneGraph->buildEnabledLightList());
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onShowLightsChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showLights(checked);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onIBLChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->useIBL(checked);
	_viewportWidget->updateView();
}

// ==================== SKYBOX CONTROLS ====================

void VisualizationEnvironmentPanel::updateSkyBoxRotationLabels(bool zUp)
{
	// Items 0/1 (X+/X-) are the same in both conventions.
	// Items 2/3 name the second horizontal axis: Y in Z-up, Z in Y-up.
	ui->comboBoxSkyBoxRotation->setItemText(2, zUp ? tr("Y+") : tr("Z+"));
	ui->comboBoxSkyBoxRotation->setItemText(3, zUp ? tr("Y-") : tr("Z-"));
}

void VisualizationEnvironmentPanel::onSkyBoxStateChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showSkyBox(checked);
	updateControlDependencies();

	// Load presets if checkbox just enabled and combo is empty
	if (checked && ui->comboBoxSkyBoxMaps->count() == 0)
		onLoadSkyBoxPresetMaps();

	_viewportWidget->updateView();

	// Update preview widget with new environment state
	if (_previewWidget)
		_previewWidget->update();
}

void VisualizationEnvironmentPanel::onSkyBoxHDRIChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setSkyBoxTextureHDRI(checked);

	// Update preview widget when environment type changes
	if (_previewWidget)
		_previewWidget->update();
}

void VisualizationEnvironmentPanel::onSkyBoxBlurChanged(int value)
{
	if (ui)
		ui->labelSkyBoxBlurValue->setText(QString("%1%").arg(value));

	if (!_viewportWidget)
		return;

	_viewportWidget->setSkyBoxBlurPercent(value);
	_viewportWidget->updateView();

	// Update preview widget
	if (_previewWidget)
		_previewWidget->update();
}

void VisualizationEnvironmentPanel::onSkyBoxFOVChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setSkyBoxFOV(value);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onSkyBoxRotationPresetChanged(int index)
{
	Q_UNUSED(index);
	if (!ui)
		return;

	// A newly-chosen preset starts at exactly its own axis angle - reset the
	// fine offset back to 0 rather than carrying over an adjustment that was
	// only meaningful relative to the PREVIOUS preset. blockSignals() so this
	// doesn't recurse into onSkyBoxRotationFineChanged() (which would just
	// re-apply the same result a second time, harmlessly, but there's no
	// reason to); applySkyBoxRotation() below applies the real (now-reset)
	// combined angle regardless of whether the slider's own valueChanged
	// fired.
	ui->sliderSkyBoxRotationFine->blockSignals(true);
	ui->sliderSkyBoxRotationFine->setValue(0);
	ui->sliderSkyBoxRotationFine->blockSignals(false);
	ui->labelSkyBoxRotationFineValue->setText(QStringLiteral("0°"));

	applySkyBoxRotation();
}

void VisualizationEnvironmentPanel::onSkyBoxRotationFineChanged(int offsetDegrees)
{
	if (ui)
		ui->labelSkyBoxRotationFineValue->setText(QString("%1%2°").arg(offsetDegrees > 0 ? QStringLiteral("+") : QString()).arg(offsetDegrees));

	applySkyBoxRotation();
}

void VisualizationEnvironmentPanel::applySkyBoxRotation()
{
	if (!ui || !_viewportWidget)
		return;

	const int presetIndex = ui->comboBoxSkyBoxRotation->currentIndex();
	const float presetAngle = kSkyBoxRotationPresetAngles[presetIndex >= 0 && presetIndex < 4 ? presetIndex : 0];
	const float degrees = presetAngle + static_cast<float>(ui->sliderSkyBoxRotationFine->value());

	_viewportWidget->setSkyBoxZRotationDegrees(degrees);
	_viewportWidget->updateView();

	if (_previewWidget)
		_previewWidget->update();
}

void VisualizationEnvironmentPanel::restoreSkyBoxRotationDegrees(float degrees)
{
	if (!ui || !_viewportWidget)
		return;

	int presetIndex = 0;
	float offset = 0.0f;
	nearestSkyBoxRotationPreset(degrees, presetIndex, offset);
	const int offsetTicks = qRound(offset);

	ui->comboBoxSkyBoxRotation->blockSignals(true);
	ui->sliderSkyBoxRotationFine->blockSignals(true);
	ui->comboBoxSkyBoxRotation->setCurrentIndex(presetIndex);
	ui->sliderSkyBoxRotationFine->setValue(offsetTicks);
	ui->comboBoxSkyBoxRotation->blockSignals(false);
	ui->sliderSkyBoxRotationFine->blockSignals(false);
	ui->labelSkyBoxRotationFineValue->setText(QString("%1%2°").arg(offsetTicks > 0 ? QStringLiteral("+") : QString()).arg(offsetTicks));

	// Apply the EXACT saved angle, not presetAngle + rounded-to-integer
	// offsetTicks - the UI only has integer-degree resolution, but the
	// viewport itself shouldn't lose the saved value's fractional precision
	// (if any) just because of that display-rounding.
	_viewportWidget->setSkyBoxZRotationDegrees(degrees);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onSkyBoxMapsChanged(int index)
{
	if (!_viewportWidget || !ui)
		return;

	// Store current index based on HDRI/LDRI selection
	if (ui->checkBoxSkyBoxHDRI->isChecked())
		_skyBoxHDRIIndex = std::max(0, index);
	else
		_skyBoxLDRIIndex = std::max(0, index);

	// User explicitly picked a preset from the combo, so it now takes
	// priority over any previously-loaded custom folder.
	_customSkyBoxActive = false;

	QString selectedPath = ui->comboBoxSkyBoxMaps->itemData(index).toString();
	if (!selectedPath.isEmpty())
	{
		_viewportWidget->setSkyBoxTextureFolder(selectedPath);
		_viewportWidget->updateView();

		// Update preview widget with new environment map
		if (_previewWidget)
			_previewWidget->update();
	}
}

void VisualizationEnvironmentPanel::onSkyBoxTextureClicked()
{
	if (!_viewportWidget || !ui)
		return;

	QString texpath = ui->checkBoxSkyBoxHDRI->isChecked() ? "/textures/envmap/skyboxes/HDRI" : "/textures/envmap/skyboxes/LDRI";
	QString appPath = PathUtils::getDataDirectory();
	QString dir = QFileDialog::getExistingDirectory(this, tr("Select Skybox Texture Folder"),
		appPath + texpath,
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if (!dir.isEmpty())
	{
		_customSkyBoxActive = true;
		_viewportWidget->setSkyBoxTextureFolder(dir);
		_viewportWidget->updateView();
	}
}

// ==================== SHADOW CONTROLS ====================

void VisualizationEnvironmentPanel::onShadowMappingStateChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showShadows(checked);
	updateControlDependencies();
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onSelfShadowsChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showSelfShadows(checked);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::syncShadowCheckboxes(bool shadowsEnabled, bool selfShadowsEnabled)
{
	if (shadowsEnabled != ui->checkBoxShadowMapping->isChecked())
	{
		ui->checkBoxShadowMapping->blockSignals(true);
		ui->checkBoxShadowMapping->setChecked(shadowsEnabled);
		ui->checkBoxShadowMapping->blockSignals(false);
	}
	if (selfShadowsEnabled != ui->checkBoxSelfShadows->isChecked())
	{
		ui->checkBoxSelfShadows->blockSignals(true);
		ui->checkBoxSelfShadows->setChecked(selfShadowsEnabled);
		ui->checkBoxSelfShadows->blockSignals(false);
	}
	updateControlDependencies(); // keeps checkBoxSelfShadows/labelShadowQuality/comboBoxShadowQuality's enabled state matching checkBoxShadowMapping, same as onShadowMappingStateChanged()
}

void VisualizationEnvironmentPanel::onShadowQualityChanged(int index)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setShadowQuality(static_cast<AdaptiveShadowMapper::QualityLevel>(index));
	_viewportWidget->updateView();
}

// ==================== FLOOR CONTROLS ====================

void VisualizationEnvironmentPanel::onGroundModeChanged()
{
	if (!_viewportWidget || !ui)
		return;

	GroundMode mode = GroundMode::None;
	if (ui->radioButtonGroundFloor->isChecked())
		mode = GroundMode::Floor;
	else if (ui->radioButtonGroundGrid->isChecked())
		mode = GroundMode::Grid;
	else if (ui->radioButtonGroundInfinitePlane->isChecked())
		mode = GroundMode::InfinitePlane;

	_viewportWidget->setGroundMode(mode);
	updateControlDependencies();
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onFloorTextureStateChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showFloorTexture(checked);
	updateControlDependencies();
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onReflectionsChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showReflections(checked);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onShadowDarknessChanged(int value)
{
	const float floatValue = static_cast<float>(value) / 100.0f;
	if (ui)
		ui->labelShadowDarknessValue->setText(QString::number(floatValue, 'f', 2));

	if (!_viewportWidget)
		return;

	_viewportWidget->setShadowCatcherDarkness(floatValue);
	_viewportWidget->updateView();
}

// Flat material the shadow-catcher's continuation bounce shades with -
// never the real floor's color/texture (see RtMaterial::isShadowCatcher's
// doc comment) - mirrors NVIDIA's independent infinitePlaneBaseColor.
void VisualizationEnvironmentPanel::onShadowCatcherColorClicked()
{
	if (!_viewportWidget || !ui)
		return;

	const QVector3D current = _viewportWidget->shadowCatcherBaseColor();
	const QColor c = QColorDialog::getColor(QColor::fromRgbF(current.x(), current.y(), current.z()), this, "Shadow Catcher Color");
	if (c.isValid())
	{
		_viewportWidget->setShadowCatcherBaseColor(QVector3D(c.redF(), c.greenF(), c.blueF()));
		const QString style = QString("background-color: %1; color: %2; border: 1px solid gray;")
			.arg(c.name(), c.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
		ui->pushButtonShadowCatcherColor->setStyleSheet(style);
		_viewportWidget->updateView();
	}
}

void VisualizationEnvironmentPanel::onShadowCatcherMetallicChanged(int value)
{
	const float floatValue = static_cast<float>(value) / 100.0f;
	if (ui)
		ui->labelShadowCatcherMetallicValue->setText(QString::number(floatValue, 'f', 2));

	if (!_viewportWidget)
		return;

	_viewportWidget->setShadowCatcherMetalness(floatValue);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onShadowCatcherRoughnessChanged(int value)
{
	const float floatValue = static_cast<float>(value) / 100.0f;
	if (ui)
		ui->labelShadowCatcherRoughnessValue->setText(QString::number(floatValue, 'f', 2));

	if (!_viewportWidget)
		return;

	_viewportWidget->setShadowCatcherRoughness(floatValue);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onEnvMappingChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->showEnvironment(checked);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onFloorOffsetChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setFloorOffsetPercent(value);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onRepeatSChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setFloorTexRepeatS(value);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onRepeatTChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setFloorTexRepeatT(value);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onFloorTextureClicked()
{
	if (!_viewportWidget || !ui)
		return;

	QString appPath = PathUtils::getDataDirectory();
	QString filter = "Image Files (*.png *.jpg *.jpeg *.bmp *.tiff);;All Files (*)";
	QString fileName = QFileDialog::getOpenFileName(this, "Choose an image for floor texture", appPath + "/textures/envmap/floor", filter);
	if (!fileName.isEmpty())
	{
		QImage buf;
		if (!buf.load(fileName))
		{
			// Fallback to dummy image if load fails
			QImage dummy(128, 128, QImage::Format_ARGB32);
			dummy.fill(1);
			buf = dummy;
		}
		_viewportWidget->setFloorTexture(buf);
		_viewportWidget->updateView();
	}
}

// ==================== HDR CONTROLS ====================

void VisualizationEnvironmentPanel::onHDRToneMappingStateChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->enableHDRToneMapping(checked);
	updateControlDependencies();
	_viewportWidget->updateView();
	if (_previewWidget) _previewWidget->update();
}

void VisualizationEnvironmentPanel::onHDRToneMappingModeChanged(int index)
{
	if (!_viewportWidget)
		return;

	// Each item carries its HDRToneMapMode enum value in UserRole (set in constructor).
	const int enumVal = ui->comboBoxHDRToneMappingMode->itemData(index).toInt();
	_viewportWidget->setHDRToneMappingMode(static_cast<HDRToneMapMode>(enumVal));
	_viewportWidget->updateView();
	if (_previewWidget) _previewWidget->update();
}

void VisualizationEnvironmentPanel::onEnvMapExposureChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setEnvMapExposure(value);
	_viewportWidget->updateView();
}

void VisualizationEnvironmentPanel::onIBLExposureChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setIBLExposure(value);
	_viewportWidget->updateView();
}

// ==================== GAMMA CONTROLS ====================

void VisualizationEnvironmentPanel::onGammaCorrectionStateChanged(bool checked)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->enableGammaCorrection(checked);
	updateControlDependencies();
	_viewportWidget->updateView();
	if (_previewWidget) _previewWidget->update();
}

void VisualizationEnvironmentPanel::onScreenGammaChanged(double value)
{
	if (!_viewportWidget)
		return;

	_viewportWidget->setScreenGamma(value);
	_viewportWidget->updateView();
	if (_previewWidget) _previewWidget->update();
}

// ==================== DEFAULT VALUES BUTTON ====================

void VisualizationEnvironmentPanel::onDefaultEnvValuesClicked()
{
	if (!ui || !_viewportWidget)
		return;

	ui->doubleSpinBoxSkyBoxFOV->setValue(45.0);
	ui->sliderSkyBoxBlur->setValue(0);
	ui->comboBoxSkyBoxRotation->setCurrentIndex(0);
	ui->sliderSkyBoxRotationFine->setValue(0);
	// setCurrentIndex(0)/setValue(0) above are no-ops (no valueChanged/
	// currentIndexChanged signal) if either control was ALREADY at its
	// default - applySkyBoxRotation() here guarantees the 0-degree result
	// actually reaches the viewport regardless of prior state.
	applySkyBoxRotation();
	ui->comboBoxShadowQuality->setCurrentIndex(1);
	ui->doubleSpinBoxFloorOffset->setValue(0.0);
	ui->doubleSpinBoxRepeatS->setValue(1.0);
	ui->doubleSpinBoxRepeatT->setValue(1.0);
	ui->comboBoxHDRToneMappingMode->setCurrentIndex(0);
	// 0.0 is correct here - these spinboxes are in STOPS (see their tooltips
	// and ViewportWidget::setEnvMapExposure()/setIBLExposure(), which convert
	// via std::pow(2.0f, exposure) before storing the linear multiplier in
	// SceneRenderController), so 0 stops = pow(2,0) = 1.0 linear = neutral.
	// (An earlier version of this comment/fix mistakenly changed these to
	// 1.0, before the stops->linear conversion in the setters above was
	// found - reverted.)
	ui->doubleSpinBoxEnvMapExposure->setValue(0.0);
	ui->doubleSpinBoxIBLExposure->setValue(0.0);
	ui->doubleSpinBoxScreenGamma->setValue(2.2);

	// Shadow Catcher settings - defaults match NVIDIA's own vk_gltf_renderer
	// infinitePlane* defaults (mid-grey, non-metal, medium roughness) - see
	// RtSceneBuilder.h's RtFloorParams::shadowCatcherEnabled doc comment.
	// setValue() below triggers onShadowDarknessChanged()/onShadowCatcherMetallicChanged()/
	// onShadowCatcherRoughnessChanged() via their existing valueChanged connections,
	// same as doubleSpinBoxFloorOffset above; the color swatch has no spinbox,
	// so it's set directly and the button style refreshed to match.
	ui->sliderShadowDarkness->setValue(50);
	ui->sliderShadowCatcherMetallic->setValue(0);
	ui->sliderShadowCatcherRoughness->setValue(50);
	_viewportWidget->setShadowCatcherBaseColor(QVector3D(0.5f, 0.5f, 0.5f));
	updateButtonStyles();

	updateControlDependencies();
	_viewportWidget->updateView();
}

// ==================== SKYBOX PRESET MANAGEMENT ====================

void VisualizationEnvironmentPanel::onLoadSkyBoxPresetMaps()
{
	reloadSkyBoxPresets();
}

// Makes the HDRI checkbox + preset combo reflect this document's actual
// current skybox (read from _viewportWidget, the authoritative source) with
// signals blocked throughout - unlike reloadSkyBoxPresets(), this never
// calls setSkyBoxTextureFolder()/updateView(): the correct texture is
// already loaded and rendering for this document, so re-applying it on
// every rebind would just be a wasted (and visibly flickery) reload. Called
// from initialize() on every document switch; reloadSkyBoxPresets() remains
// what actually changes the skybox when the user picks a new one.
void VisualizationEnvironmentPanel::syncSkyBoxSelectionSilently()
{
	if (!_viewportWidget || !ui)
		return;

	const bool isHDRI = _viewportWidget->isSkyBoxHDRIEnabled();
	const QString currentFolder = _viewportWidget->getCurrentSkyboxFolder();

	QSignalBlocker blockHDRICheckbox(ui->checkBoxSkyBoxHDRI);
	ui->checkBoxSkyBoxHDRI->setChecked(isHDRI);

	QString appPath = PathUtils::getDataDirectory();
	QString texPath = appPath + (isHDRI ? "/textures/envmap/skyboxes/HDRI" : "/textures/envmap/skyboxes/LDRI");

	QSignalBlocker blockCombo(ui->comboBoxSkyBoxMaps);
	ui->comboBoxSkyBoxMaps->clear();

	QDir dir(texPath);
	const QStringList folderList = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	int matchedIndex = -1;
	for (int i = 0; i < folderList.size(); ++i)
	{
		const QString fullPath = dir.absoluteFilePath(folderList.at(i));
		ui->comboBoxSkyBoxMaps->addItem(folderList.at(i), fullPath);
		if (fullPath == currentFolder)
			matchedIndex = i;
	}

	// No match means the active folder isn't one of the presets (a custom
	// folder via "Select Custom Map") - leave the combo on whatever it
	// defaults to and rely on _customSkyBoxActive, same as
	// reloadSkyBoxPresets() does, rather than falsely selecting a preset.
	if (matchedIndex >= 0)
	{
		ui->comboBoxSkyBoxMaps->setCurrentIndex(matchedIndex);
		if (isHDRI)
			_skyBoxHDRIIndex = matchedIndex;
		else
			_skyBoxLDRIIndex = matchedIndex;
	}
}

void VisualizationEnvironmentPanel::reloadSkyBoxPresets()
{
	if (!_modelViewer || !_viewportWidget || !ui)
		return;

	bool isHDRI = ui->checkBoxSkyBoxHDRI->isChecked();
	QString appPath = PathUtils::getDataDirectory();
	QString texPath = appPath + (isHDRI ? "/textures/envmap/skyboxes/HDRI" : "/textures/envmap/skyboxes/LDRI");
	
	// Update ModelViewer state
	_modelViewer->setSkyBoxLDRIIndex(_skyBoxLDRIIndex);
	_modelViewer->setSkyBoxHDRIIndex(_skyBoxHDRIIndex);

	// Clear and populate combo box
	ui->comboBoxSkyBoxMaps->blockSignals(true);
	ui->comboBoxSkyBoxMaps->clear();

	QDir dir(texPath);
	QStringList folderList = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

	for (const QString& folderName : folderList)
	{
		QString fullPath = dir.absoluteFilePath(folderName);
		ui->comboBoxSkyBoxMaps->addItem(folderName, fullPath);
	}

	ui->comboBoxSkyBoxMaps->blockSignals(false);

	updateControlDependencies();

	// Restore previous index for this mode
	int indexToRestore = isHDRI ? _skyBoxHDRIIndex : _skyBoxLDRIIndex;
	if (indexToRestore >= 0 && indexToRestore < ui->comboBoxSkyBoxMaps->count())
	{
		ui->comboBoxSkyBoxMaps->setCurrentIndex(indexToRestore);
	}

	// Load texture folder if available - unless a custom folder (loaded via
	// "Select Custom Map", which isn't represented in this preset combo at
	// all) is currently active. Without this check, every PBR/RayTraced
	// mode (re)selection - which calls this via setPBRLightingMode(true) -
	// would silently reload whatever preset index was last active over top
	// of the user's custom skybox.
	if (!_customSkyBoxActive)
	{
		QString selectedPath = ui->comboBoxSkyBoxMaps->itemData(ui->comboBoxSkyBoxMaps->currentIndex()).toString();
		if (!selectedPath.isEmpty())
		{
			_viewportWidget->setSkyBoxTextureFolder(selectedPath);
			_viewportWidget->updateView();
		}
	}
}

// ==================== DISPLAY MODE SYNCHRONIZATION ====================

void VisualizationEnvironmentPanel::onDisplayModeChanged(int mode)
{
	if (!_viewportWidget || !ui)
		return;
	// ViewportWidget::loadRenderSettings() (called from initializeGL(), well
	// before GL-dependent state like _lightCube exists) emits
	// displayModeChanged partway through init - this function reacts by
	// touching viewport render state (setGroundMode() -> floor geometry,
	// setSkyBoxTextureHDRI(), etc.) that isn't ready yet at that point.
	// updateFloorPlane() already guards its own GL-dependent path, but this
	// crashed elsewhere inside this function's reentrant-during-init call,
	// confirming that guard alone wasn't sufficient - reject the whole
	// reentrant call here instead of chasing each individual unsafe call
	// inside it one at a time.
	if (!_viewportWidget->isOpenGLInitialized())
		return;

	bool realShaded = _viewportWidget->isRealismEnabled();
	bool pbrLighting = (_viewportWidget->getRenderingMode() == RenderingMode::PHYSICALLY_BASED_RENDERING);

	// IMPORTANT: every setChecked() below is DELIBERATELY left free to
	// re-entrantly fire its own dedicated handler (onShadowMappingStateChanged(),
	// onReflectionsChanged(), onEnvMappingChanged(), onSkyBoxHDRIChanged(),
	// onHDRToneMappingStateChanged(), onGammaCorrectionStateChanged() - see
	// connectSignalsAndSlots()) - this function only ever sets the CHECKBOX,
	// it never calls _viewportWidget->showShadows()/showReflections()/etc.
	// itself, so that cascade is the ONLY thing that actually applies these
	// defaults to the viewport at all. A previous attempt at this comment
	// claimed blockSignals(true) on `this` (the panel) prevented that cascade
	// - it never did (QObject::blockSignals() only suppresses signals whose
	// SENDER is the object it's called on, and every signal here is emitted
	// by the CHILD widget, not the panel) - and actually blocking the cascade
	// for real (via QSignalBlocker on each child widget, tried once) broke
	// realism mode almost entirely: the floor/shadows/reflections/env-map
	// checkboxes still showed "on", but ViewportWidget's own state was never
	// updated to match, so switching to PBR left the floor invisible, no
	// shadows, etc. until the user manually re-toggled each checkbox by hand.
	//
	// radioButtonGroundFloor/GroundNone/checkBoxDefaultLights are set
	// unconditionally on every mode switch - not a one-time default. Ground
	// mode and default lights are part of what DEFINES each rendering mode
	// (Floor + lights-on for ADS/PBR, InfinitePlane + lights-off for
	// Ray-Traced - see applyRayTracedGroundDefaultsOnce(), which runs
	// right after this function whenever the mode is actually Ray-Traced
	// and re-asserts its own values on top of these), so every toolbar/
	// shortcut mode switch always re-asserts its own canonical values,
	// regardless of whatever the user left ground mode/default lights at
	// during a previous session in a DIFFERENT mode. The user is still free
	// to change either afterward, for as long as they stay in the mode they
	// switched to - only the mode SWITCH itself is authoritative. (An
	// earlier revision gated this behind "only the first time, unless the
	// user already touched it" per-control flags - that made both the
	// switch-into-PBR floor/shadow default AND the switch-into-PT shadow-
	// catcher default silently stop re-applying after the very first manual
	// tweak, since Qt's auto-exclusive radio group also fires a real signal
	// on whichever radio gets implicitly unchecked - see below.)
	//
	// radioButtonGroundGrid/GroundInfinitePlane are ALSO blocked here even
	// though this function never checks/unchecks them directly - they share
	// an auto-exclusive button group with Floor/None, so Qt automatically
	// unchecks whichever of the four was previously checked as an implicit
	// side effect of setChecked(true) on Floor/None below, and that implicit
	// uncheck would otherwise emit its own real toggled(false) signal via
	// onGroundModeChanged().
	ui->checkBoxEnvMapping->setChecked(realShaded || pbrLighting);
	ui->checkBoxShadowMapping->setChecked(realShaded);
	ui->checkBoxSelfShadows->setChecked(realShaded);
	ui->checkBoxReflections->setChecked(realShaded);
	{
		QSignalBlocker blockGroundFloor(ui->radioButtonGroundFloor);
		QSignalBlocker blockGroundNone(ui->radioButtonGroundNone);
		QSignalBlocker blockGroundGrid(ui->radioButtonGroundGrid);
		QSignalBlocker blockGroundInfinitePlane(ui->radioButtonGroundInfinitePlane);
		ui->radioButtonGroundFloor->setChecked(realShaded);
		ui->radioButtonGroundNone->setChecked(!realShaded);
		_viewportWidget->setGroundMode(realShaded ? GroundMode::Floor : GroundMode::None);
	}
	{
		// Deliberately NOT gated on realShaded like the checkboxes above -
		// this signal also fires from setDisplayMode() (Shaded/HollowMesh/
		// MeshEdges/Wireframe/ShadedWithEdges - see ViewportWidget's
		// _viewToolbar->displayModeSelected lambda), none of which touch
		// realism/Ray-Traced state at all, but which DO share this same
		// displayModeChanged signal (both setDisplayMode() and
		// setRealismEnabled() emit it). Gating default lights on realShaded
		// meant switching to Wireframe/HollowMesh/etc. while realism
		// happened to be off (e.g. coming from ADS) silently turned default
		// lights off too - wrong, since only Ray-Traced mode should ever
		// disable them (see applyRayTracedGroundDefaultsOnce(), which
		// overrides this back to off right after, for that one mode only).
		// isRayTracedRenderingModeArmed() correctly stays false for every
		// display-mode toggle, ADS, and PBR alike.
		const bool rayTraced = _viewportWidget->isRayTracedRenderingModeArmed();
		QSignalBlocker blockDefaultLights(ui->checkBoxDefaultLights);
		ui->checkBoxDefaultLights->setChecked(!rayTraced);
		_viewportWidget->useDefaultLights(!rayTraced);
	}
	ui->checkBoxSkyBoxHDRI->setChecked(ui->checkBoxSkyBoxHDRI->isChecked() || (realShaded && pbrLighting));

	bool skyBoxHDRIChecked = ui->checkBoxSkyBoxHDRI->isChecked();
	ui->checkBoxHDRToneMapping->setChecked(skyBoxHDRIChecked && pbrLighting);
	ui->checkBoxGammaCorrection->setChecked(skyBoxHDRIChecked && pbrLighting);

	updateControlDependencies();
	_viewportWidget->setSkyBoxTextureHDRI(skyBoxHDRIChecked);
}

// ==================== PBR LIGHTING MODE ====================

void VisualizationEnvironmentPanel::setPBRLightingMode(bool enable)
{
	if (!_viewportWidget || !ui)
		return;

	// If disabling PBR (switching to ADS), disable PBR-specific settings
	if (!enable)
	{
		// Disable environment mapping
		if (ui->checkBoxEnvMapping)
		{
			ui->checkBoxEnvMapping->blockSignals(true);
			ui->checkBoxEnvMapping->setChecked(false);
			ui->checkBoxEnvMapping->blockSignals(false);
		}
		_viewportWidget->showEnvironment(false);

		// Disable tone mapping and gamma correction (PBR-specific)
		ui->checkBoxHDRToneMapping->blockSignals(true);
		ui->checkBoxGammaCorrection->blockSignals(true);

		ui->checkBoxHDRToneMapping->setChecked(false);
		ui->checkBoxGammaCorrection->setChecked(false);

		ui->checkBoxHDRToneMapping->blockSignals(false);
		ui->checkBoxGammaCorrection->blockSignals(false);

		_viewportWidget->enableHDRToneMapping(false);
		_viewportWidget->enableGammaCorrection(false);
		return;
	}

	// Block signals during state changes
	ui->checkBoxSkyBoxHDRI->blockSignals(true);
	ui->checkBoxHDRToneMapping->blockSignals(true);
	ui->checkBoxGammaCorrection->blockSignals(true);

	ui->checkBoxSkyBoxHDRI->setChecked(true);
	ui->checkBoxHDRToneMapping->setChecked(enable);
	ui->checkBoxGammaCorrection->setChecked(enable);

	// Enable environment mapping when enabling PBR so preview uses bright HDRI
	if (ui->checkBoxEnvMapping)
	{
		ui->checkBoxEnvMapping->blockSignals(true);
		ui->checkBoxEnvMapping->setChecked(true);
		ui->checkBoxEnvMapping->blockSignals(false);
	}

	ui->checkBoxSkyBoxHDRI->blockSignals(false);
	ui->checkBoxHDRToneMapping->blockSignals(false);
	ui->checkBoxGammaCorrection->blockSignals(false);

	// Trigger updates
	onSkyBoxHDRIChanged(true);
	reloadSkyBoxPresets();
	_viewportWidget->enableHDRToneMapping(enable);
	_viewportWidget->enableGammaCorrection(enable);

	// Explicitly enable environment mapping
	_viewportWidget->showEnvironment(true);

	updateControlDependencies();
}

// ==================== LIGHT POSITION RANGE UPDATES (from ModelViewer geometry changes) ====================

void VisualizationEnvironmentPanel::updateLightPositionRanges(float range, float offset)
{
	if (!ui)
		return;

	// X/Y span [-range, range-offset]; Z spans [-range/3, range/2] - the
	// same real-world float ranges this function has always used, just no
	// longer cast directly onto the slider's own int min/max (see
	// LightAxisSliderMapping's doc comment in the header for why - a tiny
	// model's range/offset are both well under 1.0, which used to truncate
	// to a dead 0..0 slider). Each slider always gets kLightSliderSteps
	// ticks of resolution regardless of how small or large that float range
	// actually is.
	auto configureAxis = [](QSlider* slider, LightAxisSliderMapping& mapping, float floatMin, float floatMax)
	{
		mapping.floatMin = floatMin;
		mapping.unitsPerTick = (floatMax - floatMin) / static_cast<float>(kLightSliderSteps);
		slider->blockSignals(true);
		slider->setRange(0, kLightSliderSteps);
		slider->setValue(kLightSliderSteps / 2); // same real-world midpoint the old (max+min)/2 reset to
		slider->blockSignals(false);
	};

	configureAxis(ui->sliderLightPosX, _lightPosXMapping, -range, range - offset);
	configureAxis(ui->sliderLightPosY, _lightPosYMapping, -range, range - offset);
	configureAxis(ui->sliderLightPosZ, _lightPosZMapping, -range / 3.0f, range / 2.0f);

	updateLightPositionValueLabels();

	// Manually trigger light offset update
	if (_viewportWidget)
	{
		_viewportWidget->setLightOffset(QVector3D(
			sliderTickToOffset(_lightPosXMapping, ui->sliderLightPosX->value()),
			sliderTickToOffset(_lightPosYMapping, ui->sliderLightPosY->value()),
			sliderTickToOffset(_lightPosZMapping, ui->sliderLightPosZ->value())));
		_viewportWidget->updateView();
	}
}

float VisualizationEnvironmentPanel::sliderTickToOffset(const LightAxisSliderMapping& mapping, int tick) const
{
	return mapping.floatMin + static_cast<float>(tick) * mapping.unitsPerTick;
}

int VisualizationEnvironmentPanel::offsetToSliderTick(const LightAxisSliderMapping& mapping, float offsetValue) const
{
	if (mapping.unitsPerTick <= 0.0f)
		return 0;
	const int tick = static_cast<int>(std::lround((offsetValue - mapping.floatMin) / mapping.unitsPerTick));
	return std::clamp(tick, 0, kLightSliderSteps);
}

void VisualizationEnvironmentPanel::updateLightPositionValueLabels()
{
	if (!ui)
		return;

	ui->labelLightPosXValue->setText(QString::number(sliderTickToOffset(_lightPosXMapping, ui->sliderLightPosX->value()), 'f', 2));
	ui->labelLightPosYValue->setText(QString::number(sliderTickToOffset(_lightPosYMapping, ui->sliderLightPosY->value()), 'f', 2));
	ui->labelLightPosZValue->setText(QString::number(sliderTickToOffset(_lightPosZMapping, ui->sliderLightPosZ->value()), 'f', 2));
}

// ==================== LIGHT OFFSET RESTORE ====================

void VisualizationEnvironmentPanel::restoreDefaultLightOffset(const QVector3D& offset)
{
	if (!ui || !_viewportWidget)
		return;

	// offsetToSliderTick() already clamps to [0, kLightSliderSteps] - covers
	// the same "stale saved value after the range was re-scaled" case the
	// old qBound() calls handled, just in tick space instead of raw offset
	// units now that a tick no longer equals one world-space unit.
	const int x = offsetToSliderTick(_lightPosXMapping, offset.x());
	const int y = offsetToSliderTick(_lightPosYMapping, offset.y());
	const int z = offsetToSliderTick(_lightPosZMapping, offset.z());

	ui->sliderLightPosX->blockSignals(true);
	ui->sliderLightPosY->blockSignals(true);
	ui->sliderLightPosZ->blockSignals(true);
	ui->sliderLightPosX->setValue(x);
	ui->sliderLightPosY->setValue(y);
	ui->sliderLightPosZ->setValue(z);
	ui->sliderLightPosX->blockSignals(false);
	ui->sliderLightPosY->blockSignals(false);
	ui->sliderLightPosZ->blockSignals(false);

	updateLightPositionValueLabels();

	_viewportWidget->setLightOffset(QVector3D(
		sliderTickToOffset(_lightPosXMapping, x),
		sliderTickToOffset(_lightPosYMapping, y),
		sliderTickToOffset(_lightPosZMapping, z)));
}
