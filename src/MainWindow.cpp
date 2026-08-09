
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QEventLoop>
#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>

#include "ModelViewerApplication.h"
#include "MainWindow.h"
#include "QuickHelpDialog.h"
#include "TutorialDialog.h"
#include "Logger.h"
#include "ui_MainWindow.h"
#include "ModelViewer.h"
#include "ThemeManager.h"
#include "LanguageManager.h"
#include "ViewportWidget.h"
#include <QtOpenGL>
#include <QProgressBar>
#include <QPushButton>
#include <QUuid>
#include <utility>
#include <assimp/version.h>

#include "PathUtils.h"
#include "RtRenderDialog.h"

#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>
#include <DockAreaWidget.h>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTabBar>
#include <QScrollArea>
#include <QSplitter>
#include <QShortcut>
#include <QPointer>
#include <QSignalBlocker>
#include "MaterialPropertiesPanel.h"
#include "ObjectTransformPanel.h"
#include "VisualizationEnvironmentPanel.h"
#include "MaterialPreviewWidget.h"
#include "MaterialVariantsPanel.h"
#include "AnimationsPanel.h"
#include "CamerasPanel.h"

#if defined _WIN32 && QT_VERSION_MAJOR == 5
#include <QWinTaskbarProgress>
#include <QWinTaskbarButton>
#endif

int MainWindow::_viewerCount = 1;
MainWindow* MainWindow::_mainWindow = nullptr;
QuickHelpDialog* MainWindow::_helpDialog = nullptr;
bool MainWindow::_fileLoadCancelRequested = false;

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	ui = new Ui::MainWindow();
	ui->setupUi(this);

	// Qt-ADS docking: documents themselves are native CDockWidgets (see
	// createDocumentDock()), tabbed together in _documentDockArea, which is
	// created on demand by the first call to createDocumentDock() - there's
	// no QMdiArea left to give any special "central widget" protection to,
	// documents are ordinary dock widgets, just hosted in their own
	// CDockManager (_documentDockManager) rather than sharing one with
	// Document/Properties/Environment (_toolPanelDockManager) - see the
	// _dockSplitter setup right below for why.
	{
		using namespace ads;

		// Needed for focusedDockWidgetChanged() below, which is otherwise
		// never emitted - must be set before either CDockManager is
		// constructed (per Qt-ADS's own docs on this flag). Confirmed via
		// Qt-ADS's own source (DockManager.cpp's file-static StaticConfigFlags)
		// that config flags are process-global, not per-instance - one call
		// here covers both managers below.
		CDockManager::setConfigFlag(CDockManager::FocusHighlighting, true);

		// Document area (left/top) and tool-panel column (right/bottom) each
		// get their own independent CDockManager, hosted as the two children
		// of _dockSplitter (owned by MainWindow, not Qt-ADS) instead of
		// sharing one CDockManager's internal splitter tree. Confirmed real
		// bug with the old single-manager setup: Qt-ADS's own drag-drop
		// relayout could perturb the left/right ratio as a side effect of an
		// operation entirely within one side (e.g. floating a document tab
		// out and re-merging it into the existing document area via
		// center-drop shrank the document area and widened the tool-panel
		// column) - patched reactively for a while via constrainToolPanelWidth()
		// (now removed), but the real fix is structural: each manager only
		// ever touches its own side's splitter tree, so there's nothing left
		// for Qt-ADS's relayout to perturb across sides. Verified via source
		// read that drag-and-drop is itself manager-scoped (Qt-ADS's own
		// FloatingDockContainerPrivate::updateDropOverlays() only enumerates
		// the dragged widget's OWN manager's containers when computing drop
		// targets), so this also hard-prevents ever docking a document into
		// the tool-panel region or vice versa - which has no real use case
		// anyway (no mainstream 3D/CAD tool tabs a viewport together with its
		// property inspector).
		_dockSplitter = new QSplitter(Qt::Horizontal);
		setCentralWidget(_dockSplitter);

		// Parented to the splitter, never to `this` (MainWindow) - verified
		// via Qt-ADS's own source that CDockManager's constructor auto-calls
		// parent->setCentralWidget(this) IF its parent qobject_casts to
		// QMainWindow, which would make whichever manager is constructed
		// second silently steal the central widget out from under the
		// splitter. QSplitter auto-adopts any non-window widget parented to
		// it as a new pane (its own childEvent() override), in construction
		// order - no explicit addWidget() call needed.
		_documentDockManager = new CDockManager(_dockSplitter);
		_toolPanelDockManager = new CDockManager(_dockSplitter);

		// FocusHighlighting is needed for focusedDockWidgetChanged() (see
		// below), but its accompanying tab/title-bar highlight color was
		// judged too "in your face" - CDockManager's constructor already
		// loaded Qt-ADS's own focus_highlighting_linux.css onto this widget
		// (CDockManager has no API to keep the signal without the styling),
		// so append an override that resets the "focused" state back to
		// look like an ordinary active-but-unfocused tab, reusing the exact
		// values default_linux.css itself uses for [activeTab="true"]/
		// #tabCloseButton rather than guessing new colors. Appended (not
		// replaced) so normal QSS cascade order - not selector specificity,
		// which is otherwise equal - makes these win over Qt-ADS's own
		// same-widget stylesheet.
		//
		// [activeTab="true"] is also overridden here, flattening Qt-ADS's own
		// default.css/default_linux.css gradient (background: qlineargradient(
		// ... stop:0 palette(window), stop:1 palette(light))) to a flat
		// palette(light) - same end color, no fade. [focused="true"] is kept
		// mirroring [activeTab="true"] exactly, per the reasoning above.
		//
		// The non-active-tab label color is also overridden here: Qt-ADS's own
		// default.css uses color: palette(dark) for it, which this app's
		// ThemeManager never assigns a value for (only Window/Base/Text/etc are
		// set - see getDarkPalette()/getLightPalette()), so it falls back to
		// Qt's auto-computed Dark role - a shade darker than the dark theme's
		// own Window background, reading as near-black-on-near-black. #707070
		// reuses this app's own established "muted but legible" gray (see
		// getDarkPalette()'s QPalette::Disabled Text/WindowText/ButtonText,
		// the same value already used elsewhere for de-emphasized text against
		// this exact palette) rather than guessing a new one.
		//
		// Selectors below are generic (ads--CDockAreaWidget/ads--CDockWidgetTab),
		// not scoped to document vs. tool-panel widgets, so the identical text
		// applies to both managers for visual consistency across both sides.
		const QString dockManagerStyleOverrides = QStringLiteral(R"(
ads--CDockAreaWidget[focused="true"] ads--CDockAreaTitleBar {
	background: transparent;
	border-bottom: none;
	padding-bottom: 0px;
}
ads--CDockWidgetTab QLabel {
	color: #707070;
}
ads--CDockWidgetTab[activeTab="true"] {
	background: palette(light);
}
ads--CDockWidgetTab[focused="true"] {
	background: palette(light);
	border-color: palette(light);
}
ads--CDockWidgetTab[focused="true"] QLabel {
	color: palette(foreground);
}
ads--CDockWidgetTab[focused="true"] > #tabCloseButton {
	qproperty-icon: url(:/ads/images/close-button.svg), url(:/ads/images/close-button-disabled.svg) disabled;
}
ads--CDockWidgetTab[focused="true"] > #tabCloseButton:hover {
	border: 1px solid rgba(0, 0, 0, 32);
	background: rgba(0, 0, 0, 16);
}
ads--CDockWidgetTab[focused="true"] > #tabCloseButton:pressed {
	background: rgba(0, 0, 0, 32);
}
)");
		_documentDockManager->setStyleSheet(_documentDockManager->styleSheet() + dockManagerStyleOverrides);
		_toolPanelDockManager->setStyleSheet(_toolPanelDockManager->styleSheet() + dockManagerStyleOverrides);

		// currentChanged(int) on a document area (connected per-area below/
		// in createDocumentDock()) only fires when that area's own tab index
		// changes, so it misses the case of a user activating a document by
		// clicking into a DIFFERENT, already-current, single-tab area (e.g.
		// two documents split side by side rather than tabbed together).
		// focusedDockWidgetChanged() fires on any change of which dock
		// widget actually has focus, which does cover that case. Connected
		// only to _documentDockManager - the Properties/Environment/Document
		// tool panels live in _toolPanelDockManager entirely and never emit
		// this signal at all now, but the ModelViewer* guard below is kept
		// anyway (e.g. the placeholder dock isn't one either).
		connect(_documentDockManager, &CDockManager::focusedDockWidgetChanged, this,
			[this](ads::CDockWidget*, ads::CDockWidget* now) {
				if (!now)
					return;
				if (ModelViewer* child = qobject_cast<ModelViewer*>(now->widget()))
					activateDocument(child);
			});

		// --- Document dock: Variants + Animations + Cameras sub-tabs, single
		// shared instances - used to be built on demand inside each
		// document's own navigation area (_innerTabWidget), duplicated per
		// MDI child, and only shown when that document's model actually had
		// the relevant data. All three tabs stay visible always now (dimmed
		// via setDocumentTabDimmed() when the active document has none of
		// that data) rather than appearing/disappearing, since this dock is
		// a permanent MainWindow fixture rather than something built fresh
		// per document.
		_documentTabWidget = new QTabWidget();
		_documentTabWidget->setTabPosition(QTabWidget::North);
		_documentTabWidget->setTabShape(QTabWidget::Rounded);
		_documentTabWidget->setIconSize(QSize(32, 32));
		_documentTabWidget->setDocumentMode(false);
		_documentTabWidget->setMovable(true);

		_materialVariantsPanel = new MaterialVariantsPanel();
		_documentTabWidget->addTab(_materialVariantsPanel, QIcon(":/icons/res/material_variants.png"), tr("Variants"));

		_animationsPanel = new AnimationsPanel();
		_documentTabWidget->addTab(_animationsPanel, QIcon(":/icons/res/animations.png"), tr("Animations"));

		_camerasPanel = new CamerasPanel();
		_documentTabWidget->addTab(_camerasPanel, QIcon(":/icons/res/camera.png"), tr("Cameras"));

		// Auto Fit View / Selection Highlighting: moved here from the
		// per-document nav overlay, above the Variants/Animations/Cameras
		// tabs - single shared instances rebound to whichever document is
		// active (see rebindSharedPanelsTo()), matching every other panel
		// in this dock instead of being duplicated per document.
		_checkBoxAutoFitView = new QCheckBox(tr("Auto Fit View On Hide/Show"));
		_checkBoxAutoFitView->setToolTip(tr("Auto Fit View On Hide/Show"));
		_checkBoxSelectionHighlight = new QCheckBox(tr("Selection Highlighting"));
		_checkBoxSelectionHighlight->setToolTip(tr("Selection Highlighting in Viewer"));
		connect(_checkBoxAutoFitView, &QCheckBox::toggled, this, [this](bool checked) {
			if (ModelViewer* child = activeMdiChild())
				child->getViewportWidget()->setAutoFitViewOnUpdate(checked);
		});
		connect(_checkBoxSelectionHighlight, &QCheckBox::toggled, this, [this](bool checked) {
			if (ModelViewer* child = activeMdiChild())
				child->getViewportWidget()->setSelectionHighlighting(checked);
		});
		auto* documentControlsRow = new QWidget();
		auto* documentControlsLayout = new QHBoxLayout(documentControlsRow);
		documentControlsLayout->setContentsMargins(4, 4, 4, 4);
		documentControlsLayout->addWidget(_checkBoxAutoFitView);
		documentControlsLayout->addWidget(_checkBoxSelectionHighlight);
		documentControlsLayout->addStretch(1);

		auto* documentTabContainer = new QWidget();
		auto* documentTabContainerLayout = new QVBoxLayout(documentTabContainer);
		documentTabContainerLayout->setContentsMargins(0, 0, 0, 0);
		documentTabContainerLayout->setSpacing(0);
		documentTabContainerLayout->addWidget(documentControlsRow);
		documentTabContainerLayout->addWidget(_documentTabWidget, 1);

		auto* documentDock = new CDockWidget(_toolPanelDockManager, tr("Document"));
		documentDock->setWidget(documentTabContainer);
		documentDock->setIcon(QIcon(":/icons/res/document-root.png"));
		_toolPanelDockManager->addDockWidget(RightDockWidgetArea, documentDock);
		_documentDock = documentDock;
		// tabWidget() only exists once the dock widget is actually docked
		// somewhere, hence called after addDockWidget() rather than right
		// next to setIcon() above - matches the 32x32 size the inner
		// Variants/Animations/Cameras sub-tabs already use, since Qt-ADS's
		// own default is noticeably smaller than QTabWidget's.
		if (documentDock->tabWidget())
			documentDock->tabWidget()->setIconSize(QSize(24, 24));

		// --- Properties dock: Materials + Transformations sub-tabs, single
		// shared instances (used to be one MaterialPropertiesPanel/
		// ObjectTransformPanel per open document, duplicated per MDI child).
		_propertiesTabWidget = new QTabWidget();
		_propertiesTabWidget->setTabPosition(QTabWidget::North);
		_propertiesTabWidget->setTabShape(QTabWidget::Rounded);
		_propertiesTabWidget->setIconSize(QSize(32, 32));
		_propertiesTabWidget->setDocumentMode(false);
		_propertiesTabWidget->setMovable(true);

		_materialPropertiesPanel = new MaterialPropertiesPanel();
		auto* scrollAreaMaterial = new QScrollArea();
		scrollAreaMaterial->setWidgetResizable(true);
		scrollAreaMaterial->setWidget(_materialPropertiesPanel);
		_propertiesTabWidget->addTab(scrollAreaMaterial, QIcon(":/icons/res/material.png"), tr("Materials"));

		_objectTransformPanel = new ObjectTransformPanel();
		auto* scrollAreaTransform = new QScrollArea();
		scrollAreaTransform->setWidgetResizable(true);
		scrollAreaTransform->setWidget(_objectTransformPanel);
		_propertiesTabWidget->addTab(scrollAreaTransform, QIcon(":/icons/res/transformations.png"), tr("Transformations"));

		_propertiesTabWidget->setCurrentIndex(0);

		_propertiesDock = new CDockWidget(_toolPanelDockManager, tr("Properties"));
		_propertiesDock->setWidget(_propertiesTabWidget);
		_propertiesDock->setIcon(QIcon(":/icons/res/properties.png"));
		_toolPanelDockManager->addDockWidgetTab(RightDockWidgetArea, _propertiesDock);
		if (_propertiesDock->tabWidget())
			_propertiesDock->tabWidget()->setIconSize(QSize(24, 24));

		// --- Environment dock: single shared VisualizationEnvironmentPanel.
		_visualizationEnvironmentPanel = new VisualizationEnvironmentPanel();
		auto* scrollAreaEnv = new QScrollArea();
		scrollAreaEnv->setWidgetResizable(true);
		scrollAreaEnv->setWidget(_visualizationEnvironmentPanel);

		_environmentDock = new CDockWidget(_toolPanelDockManager, tr("Environment"));
		_environmentDock->setWidget(scrollAreaEnv);
		_environmentDock->setIcon(QIcon(":/icons/res/environment.png"));
		_toolPanelDockManager->addDockWidgetTab(RightDockWidgetArea, _environmentDock);
		if (_environmentDock->tabWidget())
			_environmentDock->tabWidget()->setIconSize(QSize(24, 24));

		// Every connection below is made ONCE, for the shared panels'
		// lifetime, rather than per-document as before. Where a handler used
		// to touch the emitting ModelViewer directly (`this`), it now
		// dispatches to activeMdiChild() instead - safe because these panels
		// only ever receive input while docked/floating and visible, which
		// requires their host document to be the active one.
		connect(_objectTransformPanel, &ObjectTransformPanel::applyTransformationsRequested, this, [this]() {
			if (ModelViewer* child = activeMdiChild())
				child->setTransformation();
		});
		connect(_objectTransformPanel, &ObjectTransformPanel::resetTransformationsRequested, this, [this]() {
			if (ModelViewer* child = activeMdiChild())
				child->resetTransformation();
			_objectTransformPanel->resetAllValues();
		});
		connect(_objectTransformPanel, &ObjectTransformPanel::detachRequested, this, [this]() {
			_propertiesDock->setFloating();
		});

		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::materialApplied, this, [this](const Material& mat) {
			if (ModelViewer* child = activeMdiChild())
				child->onCustomMaterialApplied(mat);
		});
		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::meshMaterialApplied, this,
			[this](const QUuid& meshUuid, const Material& material) {
				if (ModelViewer* child = activeMdiChild())
					child->applyMeshMaterial(meshUuid, material);
			});
		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::detachRequested, this, [this]() {
			_propertiesDock->setFloating();
		});
		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::textureSamplerChanged, this,
			[this](Material* material, Material::TextureType type) {
				if (ModelViewer* child = activeMdiChild())
					child->setTextureSamplersToSelectedItems(material, type);
			});
		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::textureCacheClearRequested, this, [this]() {
			if (ModelViewer* child = activeMdiChild())
				child->onTextureCacheCleared();
		});

		connect(_visualizationEnvironmentPanel, &VisualizationEnvironmentPanel::detachRequested, this, [this]() {
			_environmentDock->setFloating();
		});

		// Mirrors the old on_tabWidgetVizAttribs_currentChanged: the
		// Transformations sub-tab shows the viewport's transform gizmo for
		// the active document's current selection while it's frontmost.
		connect(_propertiesTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
			ModelViewer* child = activeMdiChild();
			if (!child)
				return;
			if (index == 1)
			{
				child->getViewportWidget()->showTransformGizmoForSelection(true);
				child->updateTransformationValues();
			}
			else
			{
				child->getViewportWidget()->showTransformGizmoForSelection(false);
			}
		});

		// Variants/Animations/Cameras: same one-time-connect, dispatch-via-
		// activeMdiChild() pattern as the Properties/Environment panels
		// above. The panel->viewport forwards (clipActivated etc.) go
		// straight to the active document's ViewportWidget with no
		// ModelViewer-level method needed, since they're 1:1 forwards with
		// matching signatures; the *DeleteRequested ones need the new
		// ModelViewer::deleteVariant()/deleteAnimationClip()/
		// deleteGltfCamera() methods since they also touch this document's
		// own SceneGraph/undo stack, not just its viewport.
		connect(_materialVariantsPanel, &MaterialVariantsPanel::variantActivated, this,
			[this](const QString& file, int index) {
				if (ModelViewer* child = activeMdiChild())
					child->applyVariant(file, index);
			});
		connect(_materialVariantsPanel, &MaterialVariantsPanel::variantDeleteRequested, this,
			[this](const QString& file, int index) {
				if (ModelViewer* child = activeMdiChild())
					child->deleteVariant(file, index);
			});

		connect(_animationsPanel, &AnimationsPanel::clipActivated, this,
			[this](const QString& file, int clip) {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->setActiveAnimation(file, clip);
			});
		connect(_animationsPanel, &AnimationsPanel::playbackToggled, this,
			[this](bool playing) {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->setAnimationPlaying(playing);
			});
		connect(_animationsPanel, &AnimationsPanel::loopToggled, this,
			[this](bool looping) {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->setAnimationLooping(looping);
			});
		connect(_animationsPanel, &AnimationsPanel::seekRequested, this,
			[this](double seconds) {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->seekAnimation(seconds);
			});
		connect(_animationsPanel, &AnimationsPanel::playbackSpeedChanged, this,
			[this](double speed) {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->setAnimationPlaybackSpeed(speed);
			});
		connect(_animationsPanel, &AnimationsPanel::clipDeleteRequested, this,
			[this](const QString& file, int clip) {
				if (ModelViewer* child = activeMdiChild())
					child->deleteAnimationClip(file, clip);
			});

		connect(_camerasPanel, &CamerasPanel::gltfCameraActivated, this,
			[this](const QString& file, int index) {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->activateGltfCamera(file, index);
			});
		connect(_camerasPanel, &CamerasPanel::systemCameraRequested, this,
			[this]() {
				if (ModelViewer* child = activeMdiChild())
					child->getViewportWidget()->resetToSystemCamera();
			});
		connect(_camerasPanel, &CamerasPanel::gltfCameraDeleteRequested, this,
			[this](const QString& file, int index) {
				if (ModelViewer* child = activeMdiChild())
					child->deleteGltfCamera(file, index);
			});

		// Closing a CDockWidget only hides it (DockWidgetDeleteOnClose isn't
		// set), but with nothing wired to its toggleViewAction() there was no
		// way back in from the UI - a closed dock looked permanently gone.
		auto* viewMenu = new QMenu(tr("View"), this);
		viewMenu->addAction(documentDock->toggleViewAction());
		viewMenu->addAction(_propertiesDock->toggleViewAction());
		viewMenu->addAction(_environmentDock->toggleViewAction());
		viewMenu->addSeparator();

		// Toggles _dockSplitter between side-by-side and stacked - useful on
		// narrow/portrait monitors where a fixed left/right split leaves the
		// document area too cramped. setChecked() here is just the
		// pre-readSettings() default (false/Horizontal); readSettings()
		// below corrects it (via a QSignalBlocker, so it doesn't re-run this
		// toggled handler) once the persisted value is known.
		_actionSplitterOrientation = new QAction(tr("Stack Panels Vertically"), this);
		_actionSplitterOrientation->setCheckable(true);
		connect(_actionSplitterOrientation, &QAction::toggled, this, [this](bool checked) {
			const Qt::Orientation newOrientation = checked ? Qt::Vertical : Qt::Horizontal;
			_dockSplitter->setOrientation(newOrientation);
			// The splitter is already laid out/visible here (unlike at
			// initial construction in createDocumentDock(), where
			// MainWindow::width()/height() is used instead since the
			// splitter has no real geometry yet) - derive the new sizes
			// from its own current extent so the ~75/25 ratio survives the
			// orientation flip instead of resetting to an even split.
			const int extent = (newOrientation == Qt::Horizontal) ? _dockSplitter->width() : _dockSplitter->height();
			_dockSplitter->setSizes({ extent * 3 / 4, extent / 4 });
			});
		viewMenu->addAction(_actionSplitterOrientation);
		menuBar()->insertMenu(ui->menuHelp->menuAction(), viewMenu);

		// A user dragging a document's tab to split it off creates a NEW
		// CDockAreaWidget outside of any code path this class controls
		// (Qt-ADS's own drag-and-drop handling, not createDocumentDock()) -
		// _documentDockArea alone only ever tracked the ORIGINAL area, so
		// switching tabs in a split-off area never ran rebindSharedPanelsTo()/
		// the repaint-on-switch fix at all. Wire the same handler to every
		// newly created area unconditionally (Qt::UniqueConnection guards
		// against connecting twice if this somehow fires more than once for
		// the same area) - safe and correct regardless of what the area
		// happens to host: handleActiveDocumentChanged() already tolerates a
		// non-ModelViewer current widget (qobject_cast fails -> nullptr ->
		// activateDocument(nullptr), the correct "nothing active" outcome).
		// Deliberately does NOT gate on the area already containing a
		// tracked document (an earlier version did, via
		// area->dockWidgets()/_documentDocks.values().contains()) - Qt-ADS's
		// own signal ordering for exactly when a dragged widget is inserted
		// relative to when dockAreaCreated() fires isn't documented, so that
		// gate was a timing assumption that could leave an area's
		// currentChanged() unconnected if the widget wasn't in place yet
		// when the signal fired. Connecting unconditionally has no such
		// dependency, and is safe here specifically because
		// _documentDockManager only ever hosts documents and the
		// placeholder - the tool-panel side has its own separate manager
		// and can never end up here.
		connect(_documentDockManager, &CDockManager::dockAreaCreated, this, [this](CDockAreaWidget* area) {
			if (area)
			{
				connect(area, &CDockAreaWidget::currentChanged, this,
					&MainWindow::handleActiveDocumentChanged, Qt::UniqueConnection);
			}
			});
	}

	// Set the application theme based on user settings
	QSettings themeSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	int iVal = themeSettings.value("comboBoxTheme", 0).toInt();

	ThemeManager* themeManager = new ThemeManager(this);
	themeManager->setTheme(static_cast<ThemeManager::Theme>(iVal));

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
			themeManager, [themeManager](Qt::ColorScheme scheme) {
		themeManager->applyThemeForColorScheme(scheme == Qt::ColorScheme::Dark);
	});
#else
	// Use polling timer fallback for older Qt versions
	QTimer* themeCheckTimer = new QTimer(qApp);
	connect(themeCheckTimer, &QTimer::timeout, [themeManager]() {
		static bool lastDarkMode = themeManager->isSystemInDarkMode();
		bool currentDarkMode = themeManager->isSystemInDarkMode();

		if (currentDarkMode != lastDarkMode) {
			themeManager->applyThemeForColorScheme(currentDarkMode);
			lastDarkMode = currentDarkMode;
		}
	});
	themeCheckTimer->start(1000);
#endif
	
	QMenu* fileMenu = ui->menuFile;
	QAction* exitAct = ui->actionExit;
	recentFileSeparator = fileMenu->insertSeparator(exitAct);

	recentFileSubMenuAct = fileMenu->insertMenu(recentFileSeparator, new QMenu(tr("Recent...")));
	QMenu* recentMenu = recentFileSubMenuAct->menu();
	connect(recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentFileActions);

	for (int i = 0; i < MaxRecentFiles; ++i) {
		recentFileActs[i] = recentMenu->addAction(QString(), this, &MainWindow::openRecentFile);
		recentFileActs[i]->setVisible(false);
	}

	setRecentFilesVisible(MainWindow::hasRecentFiles());

	connect(ui->menuWindows, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

	QAction* closeAct = ui->actionClose;
	closeAct->setStatusTip(tr("Close the active window"));
	connect(closeAct, &QAction::triggered,
		this, &MainWindow::closeSubWindow);

	closeAct = ui->actionFileClose;
	closeAct->setStatusTip(tr("Close the active document"));
	connect(closeAct, &QAction::triggered,
		this, &MainWindow::closeSubWindow);

	QAction* closeAllAct = ui->actionClose_All;
	closeAllAct->setStatusTip(tr("Close all the windows"));
	connect(closeAllAct, &QAction::triggered, this, &MainWindow::closeAllSubWindows);

	QAction* nextAct = ui->actionNext;
	nextAct->setShortcuts(QKeySequence::NextChild);
	nextAct->setStatusTip(tr("Move the focus to the next window"));
	connect(nextAct, &QAction::triggered, this, [this]() {
		cycleActiveDocument(1);
		});

	QAction* previousAct = ui->actionPrevious;
	previousAct->setShortcuts(QKeySequence::PreviousChild);
	previousAct->setStatusTip(tr("Move the focus to the previous "
		"window"));
	connect(previousAct, &QAction::triggered, this, [this]() {
		cycleActiveDocument(-1);
		});

	// Document-scoped shortcuts (Delete, and the rendering-mode/import/
	// export ones below) - single MainWindow-owned instances dispatching to
	// activeMdiChild(), same pattern as Undo/Redo/Ray Tracing right below.
	// These used to be created per-document, inside ModelViewer's own
	// constructor, with QShortcut's default WindowShortcut context relying
	// on QMdiSubWindow's Qt::SubWindow flag to give each document its own
	// window-shortcut scope for free. CDockWidget (see
	// MainWindow::createDocumentDock()) carries no such flag, so with two-
	// plus documents open every one of those per-document shortcuts resolved
	// to the same top-level MainWindow and Qt disabled them all as
	// ambiguous. A single instance per shortcut, scoped to MainWindow, can
	// never be ambiguous with itself regardless of how many documents are
	// open or how they're arranged.
	connect(new QShortcut(QKeySequence(Qt::Key_Delete), this), &QShortcut::activated, this, [this]() {
		ModelViewer* child = activeMdiChild();
		if (!child)
			return;
		// Narrower than the other document shortcuts below: only fires while
		// the active document's own navigation tree OR its 3D viewport has
		// keyboard focus - Delete must not also fire while, say, typing into
		// a search box or a property field. deleteSelectedItems() itself is
		// selection-state-based, not focus-based (both a tree click and a
		// viewport click update the same underlying selection), so either
		// widget having focus is equally valid - this used to check the tree
		// only, which silently broke Delete for viewport-made selections
		// (ViewportWidget::keyPressEvent() has its own now-dead Key_Delete
		// branch for exactly that case - this shortcut, having WindowShortcut
		// context, always intercepts the key first).
		QWidget* focusWidget = QApplication::focusWidget();
		QWidget* tree = child->treeWidgetModel;
		QWidget* viewport = child->getViewportWidget();
		if (focusWidget && ((tree && (focusWidget == tree || tree->isAncestorOf(focusWidget))) ||
		                     focusWidget == viewport))
			child->deleteSelectedItems();
		});
	connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this), &QShortcut::activated, this, [this]() {
		if (ModelViewer* child = activeMdiChild())
			child->importModel();
		});
	connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this), &QShortcut::activated, this, [this]() {
		if (ModelViewer* child = activeMdiChild())
			child->exportModel();
		});
	connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), this), &QShortcut::activated, this, [this]() {
		if (ModelViewer* child = activeMdiChild())
			child->onRenderingModeSelected("ADS");
		});
	connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this), &QShortcut::activated, this, [this]() {
		if (ModelViewer* child = activeMdiChild())
			child->onRenderingModeSelected("PBR");
		});
	connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this), &QShortcut::activated, this, [this]() {
		if (ModelViewer* child = activeMdiChild())
			child->onRenderingModeSelected("RayTraced");
		});

	// Connect undo/redo actions
	connect(ui->actionUndo, &QAction::triggered, this, [this]() {
		if (activeMdiChild())
			activeMdiChild()->undo();
		});

	connect(ui->actionRedo, &QAction::triggered, this, [this]() {
		if (activeMdiChild())
			activeMdiChild()->redo();
		});

	// Tools → Texture Debugger
	connect(ui->actionTextureDebugger, &QAction::triggered, this, [this]() {
		if (activeMdiChild())
			activeMdiChild()->showTextureDebugPanel();
		});

	// Tools → Ray Tracing - non-modal, at most one instance per document
	// (mirrors SettingsDialog's WA_DeleteOnClose pattern for auto-cleanup,
	// but reuses/raises an already-open dialog instead of stacking a new one
	// on each click - repeatedly triggering the menu/shortcut used to spawn
	// a fresh dialog every time, leaving several identical windows open on
	// top of each other with no way to tell them apart).
	connect(ui->actionRayTracing, &QAction::triggered, this, [this]() {
		if (!activeMdiChild())
			return;
		// Opening the dialog does NOT switch rendering mode by itself - it
		// stays whatever it currently is (ADS/PBR/already-RayTraced) until
		// the user actually presses Render inside the dialog (see
		// RtRenderDialog::onRenderClicked()), which is the single place
		// that switches to Ray Traced mode.
		//
		// Parented to the ModelViewer (the document's own content widget,
		// wrapped in its CDockWidget - see createDocumentDock()), NOT to
		// MainWindow (`this`) - a QDialog still floats as an independent
		// top-level window regardless of which widget is passed as its
		// parent, but the parent IS what Qt uses to auto-destroy child
		// widgets when it's destroyed. Parenting to MainWindow meant the
		// dialog outlived every document, including all of them being
		// closed - this way it closes along with the document it belongs to
		// instead.
		//
		// findChild() (direct children only - the dialog parents its own
		// widgets under itself too, but none of THOSE are RtRenderDialogs)
		// doubles as the "is one already open for this document" check:
		// WA_DeleteOnClose means a closed dialog stops being findable here,
		// so this naturally reduces to "create one" the first time and
		// "bring the existing one forward" on every repeat click after.
		ModelViewer* child = activeMdiChild();
		if (RtRenderDialog* existing = child->findChild<RtRenderDialog*>(QString(), Qt::FindDirectChildrenOnly))
		{
			existing->show();
			existing->raise();
			existing->activateWindow();
			return;
		}
		RtRenderDialog* dialog = new RtRenderDialog(child, child);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->show();
		});

	// handleActiveDocumentChanged() is connected to _documentDockArea's
	// currentChanged(int) signal once, when that area is first created by
	// createDocumentDock() - it doesn't exist yet at this point in
	// construction (no document has been created), so there's nothing to
	// connect here.

	updateMenus();

	readSettings();

	setAttribute(Qt::WA_DeleteOnClose);

	_cancelTaskButton = new QPushButton("Cancel Loading", ui->statusBar);
	ui->statusBar->addPermanentWidget(_cancelTaskButton);
	connect(_cancelTaskButton, SIGNAL(clicked()), this, SLOT(cancelFileLoading()));
	_cancelTaskButton->hide();

	_progressBar = new QProgressBar(ui->statusBar);
	ui->statusBar->addPermanentWidget(_progressBar);
	_progressBar->hide();
	//createMdiChild();

	_bFirstTime = true;

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		ui->retranslateUi(this);
		retranslateUI();  // if needed
		});

}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::WindowTitleChange)
	{
		if (ModelViewer* viewer = qobject_cast<ModelViewer*>(watched))
		{
			if (ads::CDockWidget* dock = _documentDocks.value(viewer))
				dock->setWindowTitle(viewer->windowTitle());
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::retranslateUI()
{
	// Recent files submenu
	if (recentFileSubMenuAct && recentFileSubMenuAct->menu())
		recentFileSubMenuAct->menu()->setTitle(tr("Recent..."));

	// Cancel loading button
	if (_cancelTaskButton)
		_cancelTaskButton->setText(tr("Cancel Loading"));

	// Status tips for dynamically created actions
	if (ui->actionClose)
		ui->actionClose->setStatusTip(tr("Close the active window"));
	if (ui->actionFileClose)
		ui->actionFileClose->setStatusTip(tr("Close the active document"));
	if (ui->actionClose_All)
		ui->actionClose_All->setStatusTip(tr("Close all the windows"));
	if (ui->actionNext)
		ui->actionNext->setStatusTip(tr("Move the focus to the next window"));
	if (ui->actionPrevious)
		ui->actionPrevious->setStatusTip(tr("Move the focus to the previous window"));

	// Recent file actions (text set dynamically)
	updateRecentFileActions();
}

ModelViewer* MainWindow::createMdiChild()
{
	// nullptr, not some dock's content area - createDocumentDock() below
	// reparents this into a new CDockWidget regardless, so constructing with
	// a real parent just means the widget gets reparented twice (once
	// implicitly here, once by setWidget()) instead of once. QOpenGLWidget
	// destroys and recreates its GL context on every reparent that changes
	// the top-level window, then relies on a fresh expose event to re-trigger
	// initializeGL() - see refreshFallbackLight()'s doc comment and
	// ViewportWidget::~ViewportWidget()'s cleanup guard for the class of bug
	// this causes if that sequence doesn't complete under XWayland. Matches
	// on_actionNew_triggered()'s identical construction for every
	// subsequently created document, which already uses nullptr here.
	ModelViewer* viewer = new ModelViewer(nullptr);
	QString lastOpenedDir = PathUtils::getDataDirectory() + QString("/test-models");
	viewer->setLastOpenedDir(lastOpenedDir);
	viewer->setAttribute(Qt::WA_DeleteOnClose);
	createDocumentDock(viewer);

	// Apply persisted perspective FOV immediately so the first view uses the
	// setting before any settingsChanged signal fires.
	{
		QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		const int fov = s.value("fieldOfViewSpinBox", 45).toInt();
		viewer->getViewportWidget()->setPerspFOV(fov);
	}

	return viewer;
}

ads::CDockWidget* MainWindow::createDocumentDock(ModelViewer* viewer)
{
	using namespace ads;

	_viewers.append(viewer);
	// Keep _viewers (and _documentDocks) in sync regardless of how the
	// document is closed (dock close button, closeAllSubWindows, failed
	// load, etc.). WA_DeleteOnClose deletes the viewer without going through
	// closeSubWindow(), so without this the lists accumulate dangling
	// pointers that crash settings apply. The CDockWidget is torn down from
	// here too, following the viewer's lifetime rather than the other way
	// around, since ModelViewer::closeEvent() (the unsaved-changes save
	// prompt) is what actually decides whether a close goes through at all -
	// see the closeRequested connection below.
	connect(viewer, &QObject::destroyed, this, [this, viewer]() {
		// On application exit, _viewers/_documentDocks/_documentDockManager
		// are all MainWindow members (or things it owns) being torn down as part of
		// ~MainWindow()'s own cascading parent-child destruction - by the
		// time this fires from deep inside that cascade (QWidget's base-
		// class destructor deleting children, which reaches this document's
		// CDockWidget -> its wrapped ModelViewer -> this destroyed() signal),
		// ~MainWindow()'s BODY and its own member destructors (including
		// _viewers itself, a plain QList<ModelViewer*> member, not a
		// pointer) have already run - C++ destroys derived members before
		// the base class - so _viewers is itself an already-destructed
		// object at this point. Touching it (even just removeAll()) is a
		// real crash (confirmed via a coredump: SIGSEGV inside
		// QArrayDataPointer::reallocateAndGrow() reached straight from this
		// lambda), not just a hypothetical one - the _shuttingDown check
		// must come BEFORE any member access, not after it. Only the normal
		// one-document-at-a-time close path (closeRequested -> viewer->
		// close() -> deferred deleteLater()) reaches this outside of any
		// destructor call stack, where touching everything below is safe.
		if (_shuttingDown)
			return;
		_viewers.removeAll(viewer);

		const bool viewerWasActive = (viewer == _activeDocument);

		// Remove the dock BEFORE deciding what becomes active next (see
		// below) - Qt-ADS's own container-level removal can reassign which
		// widget is current/focused in the affected area as a side effect,
		// so querying focusedDockWidget() after this has run gives a more
		// accurate answer than querying it before.
		if (CDockWidget* dock = _documentDocks.take(viewer))
		{
			_documentDockManager->removeDockWidget(dock);
			dock->deleteLater();
		}
		// _documentPlaceholderDock (see the constructor) stays registered
		// in _documentDockArea even with zero real documents open, so the
		// area itself never actually goes empty/gets torn down anymore -
		// just make the placeholder visible again so the document region
		// doesn't sit on a blank/stale tab.
		if (_documentDocks.isEmpty() && _documentPlaceholderDock)
			_documentPlaceholderDock->setAsCurrentTab();

		// Closing the currently-active document doesn't reliably get caught
		// by a follow-up currentChanged()/focusedDockWidgetChanged() signal -
		// confirmed real gap: if viewer was split into its own area with no
		// other documents in it, removing its dock can leave that area with
		// nothing left to report as "current" at all, and _activeDocument
		// would keep pointing at viewer's now-freed memory until (if ever)
		// some other interaction happens to fix it - the next Save/Export/
		// Undo/shortcut that dereferences activeMdiChild() in the meantime
		// is a real use-after-free. Established directly here instead of
		// relying on that signal, and proactively re-pointed at whichever
		// document is actually still on screen rather than just going
		// blank: CDockManager::focusedDockWidget() (backed by a QPointer
		// internally - confirmed in Qt-ADS's own source, so safe to query
		// regardless of what was just destroyed/removed above) reports
		// whichever dock widget Qt-ADS itself now considers focused, which
		// after a removal is usually whatever the affected area promoted to
		// current. Falls back to the most-recently-remaining open document
		// if that doesn't resolve to one (e.g. focus landed on the
		// placeholder, or on nothing at all), and to nullptr only if none
		// are left.
		if (viewerWasActive)
		{
			// Cleared before activateDocument() below is ever called with
			// ANYTHING, not just when the fallback stays nullptr: it
			// dereferences _lastBoundModelViewer->getUndoStack() to
			// disconnect the previous document's undo-stack signal, and
			// _lastBoundModelViewer is normally the same object as viewer
			// here (both set together at the end of every
			// activateDocument() call) - viewer is already destructed by
			// the time this handler runs (QObject::destroyed() fires from
			// ~QObject(), after the derived ModelViewer destructor has
			// already completed), so leaving that stale would make the
			// upcoming call itself a use-after-free. Nothing to explicitly
			// disconnect anyway: Qt's own connection bookkeeping already
			// dropped it automatically when viewer's QUndoStack (a child
			// object) was destroyed.
			_activeDocument = nullptr;
			_lastBoundModelViewer = nullptr;

			ModelViewer* nextActive = nullptr;
			if (ads::CDockWidget* focused = _documentDockManager->focusedDockWidget())
			{
				if (ModelViewer* candidate = qobject_cast<ModelViewer*>(focused->widget()))
					if (_viewers.contains(candidate))
						nextActive = candidate;
			}
			if (!nextActive && !_viewers.isEmpty())
				nextActive = _viewers.last();

			activateDocument(nextActive);
		}
	});
	connect(viewer, &ModelViewer::documentModifiedChanged,
	        this, [this, viewer](bool) {
	            if (viewer == activeMdiChild())
	                updateMenus();
	        });

	// Constructed with a UUID as the title so the required-unique object
	// name (see CDockWidget's constructor docs) never collides, even across
	// same-named "Session N" or reopened-file tabs; the actual visible tab
	// label is set separately right below and kept in sync afterwards via
	// eventFilter() watching for QEvent::WindowTitleChange, the same
	// mechanism QMdiSubWindow itself uses internally.
	auto* dock = new CDockWidget(_documentDockManager, QUuid::createUuid().toString(QUuid::WithoutBraces));
	dock->setWindowTitle(viewer->windowTitle());
	dock->setWidget(viewer, CDockWidget::ForceNoScrollArea);
	dock->setFeature(CDockWidget::CustomCloseHandling, true);
	connect(dock, &CDockWidget::closeRequested, viewer, &QWidget::close);
	viewer->installEventFilter(this);
	_documentDocks.insert(viewer, dock);

	if (_documentDockArea)
	{
		_documentDockArea = _documentDockManager->addDockWidgetTabToArea(dock, _documentDockArea);
	}
	else
	{
		// First document: establishes the document area inside
		// _documentDockManager's own root container - no anchor relative to
		// the tool-panel side needed anymore (previously anchored via
		// LeftDockWidgetArea relative to _documentDock->dockAreaWidget(),
		// back when both sides shared one CDockManager's splitter tree; now
		// _documentDockManager's root container IS the whole document side,
		// sized against its sibling via _dockSplitter instead - see below).
		//
		// The permanent placeholder (kept alongside every real document
		// from here on, tabbed together, NoTab so it never shows its own
		// tab - see the "made current again" comment in the destroyed-
		// signal handler above) is established HERE, at the same point the
		// document area itself first comes into existence, deliberately
		// NOT earlier in the constructor: readSettings() (called later in
		// the constructor) restores a persisted dock layout from
		// QSettings, and a widget that didn't exist in that saved layout
		// gets flagged "unassigned" by Qt-ADS's restore logic - which,
		// this being the ONLY dock widget in a whole area at that point,
		// triggered a layout restructure that ended up destroying the
		// wrapped ModelViewer along with it once the first real document
		// was later added relative to the now-stale area reference
		// (confirmed via a real crash on startup, reproducible every
		// time). createDocumentDock() only ever runs after readSettings()
		// has already applied whatever it's going to apply, matching
		// exactly when the OLD (placeholder-less) code used to first
		// establish this area too.
		auto* placeholderLabel = new QLabel(tr("No documents open"));
		placeholderLabel->setAlignment(Qt::AlignCenter);
		placeholderLabel->setStyleSheet("color: palette(mid); background: palette(base);");
		_documentPlaceholderDock = new CDockWidget(_documentDockManager, QStringLiteral("DocumentPlaceholder"));
		_documentPlaceholderDock->setWidget(placeholderLabel);
		_documentPlaceholderDock->setFeature(CDockWidget::NoTab, true);
		_documentPlaceholderDock->setFeature(CDockWidget::DockWidgetClosable, false);

		_documentDockArea = _documentDockManager->addDockWidget(LeftDockWidgetArea, _documentPlaceholderDock);
		_documentDockArea = _documentDockManager->addDockWidgetTabToArea(dock, _documentDockArea);

		// Establishes the initial document/tool-panel ratio on the OUTER
		// _dockSplitter (replaces the old single-manager
		// CDockManager::setSplitterSizes() call) - but only if readSettings()
		// (called earlier in the constructor, well before main.cpp calls
		// createMdiChild() which reaches here) didn't already restore a
		// user-adjusted ratio from a previous session; skip clobbering that
		// with the hardcoded default. Uses MainWindow::width()/height(), not
		// the splitter's own geometry, because this runs before the window
		// is ever shown - the splitter hasn't been laid out yet and its own
		// width()/height() would read as an unreliable placeholder.
		if (!_dockSplitterSizesRestored)
		{
			const int extent = (_dockSplitter->orientation() == Qt::Horizontal) ? width() : height();
			_dockSplitter->setSizes({ extent * 3 / 4, extent / 4 });
		}

		connect(_documentDockArea, &CDockAreaWidget::currentChanged, this, &MainWindow::handleActiveDocumentChanged);
	}

	dock->setAsCurrentTab();
	return dock;
}

void MainWindow::cycleActiveDocument(int direction)
{
	if (_viewers.size() < 2)
		return;

	int currentIndex = _viewers.indexOf(activeMdiChild());
	if (currentIndex < 0)
		currentIndex = (direction > 0) ? -1 : 0;

	const int nextIndex = (currentIndex + direction + _viewers.size()) % _viewers.size();
	ModelViewer* nextViewer = _viewers.value(nextIndex);
	if (!nextViewer)
		return;
	if (ads::CDockWidget* dock = _documentDocks.value(nextViewer))
	{
		dock->setAsCurrentTab();
		// setAsCurrentTab() -> CDockAreaWidget::setCurrentDockWidget() ->
		// setCurrentIndex() early-returns without emitting currentChanged()
		// if nextViewer's dock is ALREADY its area's current tab (verified
		// in Qt-ADS's own source) - true whenever documents are split into
		// separate single-tab areas, since cycling between those never
		// changes which tab is current WITHIN either area. Without this
		// explicit call, _activeDocument (and therefore activeMdiChild())
		// would silently go stale in exactly that arrangement, same as
		// presentDocumentFullscreen() needed for the very first document.
		activateDocument(nextViewer);
	}
}

void MainWindow::rebindSharedPanelsTo(ModelViewer* viewer)
{
	if (!viewer)
	{
		// No active document (last one just closed) - disabling isn't
		// enough on its own: these panels are single shared instances that
		// cache the ModelViewer/ViewportWidget/SceneGraph pointers they were
		// last bound to (see initialize() on each), and disabling a widget
		// doesn't touch those. Left uncleared, the pointers go dangling the
		// moment that document is actually destroyed - and a REENTRANT call
		// into one of these panels can still happen after that (confirmed
        // via a real crash: opening a new document runs its first
        // initializeGL() -> loadRenderSettings() ->
        // ModelViewer::onRenderingModeSelected(), which reaches
        // VisualizationEnvironmentPanel::setPBRLightingMode() - and that
        // panel was still holding the PREVIOUS, by-then-destroyed
        // document's ViewportWidget*, so it called a shader bind on freed
        // memory). Passing nullptr through the same initialize()/setter
        // calls the non-null branch below already uses is what actually
        // clears the dangling references - each of these was already
        // confirmed to handle null viewer/viewport/sceneGraph arguments
        // safely (they're guarded internally, or plain setters with no
        // immediate dereference).
		_materialPropertiesPanel->initialize(nullptr, nullptr);
		_objectTransformPanel->setEnabled(false);
		_visualizationEnvironmentPanel->initialize(nullptr, nullptr);
		_materialVariantsPanel->setSceneGraph(nullptr);
		_animationsPanel->setSceneGraph(nullptr);
		_animationsPanel->setViewportWidget(nullptr);
		_camerasPanel->setSceneGraph(nullptr);
		_camerasPanel->setViewportWidget(nullptr);

		_materialPropertiesPanel->setEnabled(false);
		_objectTransformPanel->setEnabled(false);
		_visualizationEnvironmentPanel->setEnabled(false);
		_materialVariantsPanel->setEnabled(false);
		_animationsPanel->setEnabled(false);
		_camerasPanel->setEnabled(false);
		_checkBoxAutoFitView->setEnabled(false);
		_checkBoxSelectionHighlight->setEnabled(false);
		return;
	}

	_materialPropertiesPanel->setEnabled(true);
	_objectTransformPanel->setEnabled(true);
	_visualizationEnvironmentPanel->setEnabled(true);
	_materialVariantsPanel->setEnabled(true);
	_animationsPanel->setEnabled(true);
	_camerasPanel->setEnabled(true);
	_checkBoxAutoFitView->setEnabled(true);
	_checkBoxSelectionHighlight->setEnabled(true);

	ViewportWidget* viewport = viewer->getViewportWidget();

	// Reflect the newly-bound document's own current state, not the
	// previously active document's - blockSignals() so this doesn't loop
	// back through the toggled-> dispatch connections made in the
	// constructor and write it right back (redundantly, but harmlessly,
	// since they'd dispatch to the same viewport this value came from).
	_checkBoxAutoFitView->blockSignals(true);
	_checkBoxAutoFitView->setChecked(viewport->autoFitViewOnUpdate());
	_checkBoxAutoFitView->blockSignals(false);
	_checkBoxSelectionHighlight->blockSignals(true);
	_checkBoxSelectionHighlight->setChecked(viewport->isSelectionHighlighting());
	_checkBoxSelectionHighlight->blockSignals(false);

	// MaterialPropertiesPanel::initialize() has no one-shot guard, so this is
	// already safe to call on every rebind.
	_materialPropertiesPanel->initialize(viewer, viewport);
	// Clears state left over from whichever document was active before -
	// without this, an in-progress mesh-material edit on the previous
	// document would still look "in progress" against the new one.
	_materialPropertiesPanel->setEditingMeshUuid(QUuid());

	_visualizationEnvironmentPanel->setPreviewWidget(_materialPropertiesPanel->getPreviewWidget());
	_visualizationEnvironmentPanel->initialize(viewer, viewport);

	// These two connections are per-ViewportWidget, and every document has
	// its own - reconnect them to the newly-active one each time instead of
	// leaving them wired to whichever viewport was active before.
	disconnect(_environmentPanelDisplayModeConnection);
	_environmentPanelDisplayModeConnection = connect(viewport, QOverload<int>::of(&ViewportWidget::displayModeChanged),
		_visualizationEnvironmentPanel, QOverload<int>::of(&VisualizationEnvironmentPanel::onDisplayModeChanged));

	disconnect(_materialPreviewRenderingModeConnection);
	MaterialPreviewWidget* previewWidget = _materialPropertiesPanel->getPreviewWidget();
	_materialPreviewRenderingModeConnection = connect(viewport, QOverload<int>::of(&ViewportWidget::renderingModeChanged),
		this, [previewWidget](int) { previewWidget->update(); });

	// Unconditional, not just on switching TO the Transformations tab - if
	// the user was already looking at that tab when a different document
	// activated, only the tab-changed handler would have caught it, leaving
	// the panel showing the previous document's selection's values.
	viewer->updateTransformationValues();

	// Document dock: Variants/Animations/Cameras. setSceneGraph()/
	// setViewportWidget() are plain setters with no one-shot guard or
	// ordering hazard (checked their implementations directly, given what
	// the Environment panel's initialize() ordering bug cost earlier) -
	// safe to call on every rebind.
	SceneGraph* sceneGraph = viewer->sceneGraph();
	_materialVariantsPanel->setSceneGraph(sceneGraph);
	_animationsPanel->setSceneGraph(sceneGraph);
	_animationsPanel->setViewportWidget(viewport);
	_camerasPanel->setSceneGraph(sceneGraph);
	_camerasPanel->setViewportWidget(viewport);
	_materialVariantsPanel->refresh();
	_animationsPanel->refresh();
	_camerasPanel->refresh();

	// Per-document sources (this document's SceneGraph/ViewportWidget, not
	// the shared panels) - disconnect from whichever document was
	// previously bound and reconnect to the new one, same reasoning as the
	// Environment panel's SceneGraph::lightDataChanged connection.
	disconnect(_variantDataChangedConnection);
	disconnect(_animationDataChangedConnection);
	disconnect(_gltfCameraDataChangedConnection);
	disconnect(_animationStateChangedConnection);
	if (sceneGraph)
	{
		_variantDataChangedConnection = connect(sceneGraph, &SceneGraph::variantDataChanged, this,
			[this]() { _materialVariantsPanel->refresh(); refreshDocumentDockTabStyling(activeMdiChild()); });
		_animationDataChangedConnection = connect(sceneGraph, &SceneGraph::animationDataChanged, this,
			[this]() { _animationsPanel->refresh(); refreshDocumentDockTabStyling(activeMdiChild()); });
		_gltfCameraDataChangedConnection = connect(sceneGraph, &SceneGraph::gltfCameraDataChanged, this,
			[this]() { _camerasPanel->refresh(); refreshDocumentDockTabStyling(activeMdiChild()); });
	}
	_animationStateChangedConnection = connect(viewport, &ViewportWidget::animationStateChanged,
		_animationsPanel, &AnimationsPanel::refresh);

	refreshDocumentDockTabStyling(viewer);
}

void MainWindow::handleActiveDocumentChanged()
{
	// Determined from the signal sender, not activeMdiChild() (which just
	// returns _activeDocument - the very thing this function updates) -
	// this fires once per document-hosting CDockAreaWidget, and there can
	// be more than one (see createDocumentDock()'s dockAreaCreated
	// handler), so it must reflect whichever area actually changed, not
	// always the original one.
	ModelViewer* child = nullptr;
	if (auto* area = qobject_cast<ads::CDockAreaWidget*>(sender()))
	{
		if (ads::CDockWidget* current = area->currentDockWidget())
			child = qobject_cast<ModelViewer*>(current->widget());
	}
	activateDocument(child);
}

void MainWindow::activateDocument(ModelViewer* child)
{
	// Both currentChanged(int) (tab switch within one area) and
	// focusedDockWidgetChanged() (focus moving to a different area
	// entirely) can fire for what amounts to the same activation - skip
	// redoing the rebind/undo-stack/repaint-nudge work below if the
	// document isn't actually changing.
	if (child && child == _activeDocument)
		return;

	_activeDocument = child;

	updateMenus();

	// Disconnect from the PREVIOUS child's undo stack before connecting to
	// the new one - without this, every document switch added another
	// connection to the last child's stack, so updateMenus() fired once per
	// prior switch on every subsequent undo/redo.
	if (_lastBoundModelViewer && _lastBoundModelViewer->getUndoStack())
		disconnect(_lastBoundModelViewer->getUndoStack(), &QUndoStack::indexChanged, this, &MainWindow::updateMenus);

	rebindSharedPanelsTo(child);

	// RtRenderDialog is per-document (see ui->actionRayTracing's connect()
	// in the constructor) but still floats as an independent top-level
	// window regardless of which widget it's parented to - Qt doesn't
	// hide/show top-level windows just because their parent's own
	// visibility changed, so without this a dialog opened for one document
	// kept showing - and still controlling - that document even while a
	// completely different one became active. Mirrors
	// feature/mdi-unified-panels' RtRenderDialog::onActiveSubWindowChanged(),
	// just driven from here (this codebase's own single choke point for
	// document-activation changes) instead of QMdiArea::subWindowActivated.
	// setVisible(), not show()/hide() - repeated identical calls are a
	// no-op, and this doesn't affect an in-progress render either way, only
	// whether its dialog is currently on screen.
	if (_lastBoundModelViewer)
	{
		if (RtRenderDialog* previousDialog = _lastBoundModelViewer->findChild<RtRenderDialog*>(QString(), Qt::FindDirectChildrenOnly))
			previousDialog->setVisible(false);
	}
	if (child)
	{
		if (RtRenderDialog* newDialog = child->findChild<RtRenderDialog*>(QString(), Qt::FindDirectChildrenOnly))
			newDialog->setVisible(true);
	}

	_lastBoundModelViewer = child;

	if (child && child->getUndoStack())
	{
		connect(child->getUndoStack(), &QUndoStack::indexChanged,
			this, &MainWindow::updateMenus, Qt::UniqueConnection);
	}

	if (child)
	{
		// A tab becoming current only shows the QOpenGLWidget again - it
		// doesn't itself guarantee a repaint, so without this the viewport
		// can sit on stale/blank contents until something else (a click, an
		// animation frame) happens to trigger one. Also grabs keyboard focus
		// the same way a click into the viewport already does (see
		// ViewportWidget::mousePressEvent()), so camera-navigation keys work
		// immediately on switching tabs instead of requiring a click first.
		if (ViewportWidget* viewport = child->getViewportWidget())
		{
			viewport->setFocus();
			// Neither update() nor repaint() actually fixes this (confirmed
			// by testing both): the viewport reports isVisible()==true with
			// a correct size right here, and repaint() forces Qt to run
			// paintGL() synchronously, yet the screen stayed on stale/black
			// contents until a genuine native input event (the mouse
			// entering the viewport OR passing over a nearby widget like the
			// tab's close button) happened. That points at the window
			// manager/compositor never being told this region needs
			// recompositing - Qt's own internal repaint clearly ran, but
			// nothing reached the compositor without real input activity.
			// A real resize (shrink then restore) forces genuine native
			// ConfigureNotify/Expose events the compositor can't ignore,
			// unlike a pure Qt-level update()/repaint() call - matches the
			// same class of Wayland/XWayland compositor quirk already
			// worked around elsewhere in this codebase (see
			// ViewportWidget::paintGL()'s alpha-stomp comment), just showing
			// up as stale pixels here instead of alpha bleed-through.
			//
			// The shrink and restore need to be genuinely separate event-
			// loop passes, not back-to-back calls - two resize() calls with
			// no event processing between them risk Qt coalescing them into
			// a single net no-op (final size == original size), which would
			// never generate a real native event at all. That's consistent
			// with this only failing "randomly": whether Qt happens to
			// process something in between the two calls isn't guaranteed
			// by calling them consecutively in the same function.
			//
			// Windows-only: DWM doesn't have the "Qt-level repaint never
			// reaches the compositor" bug this works around, so on Windows
			// this is pure liability rather than a needed fix - QOpenGLWidget
			// resize there recreates FBOs/backing stores more synchronously
			// than X11/Wayland, and with multiple documents open the QPointer
			// guard only protects against the viewport being destroyed, not
			// against the user switching to yet another tab before these
			// pending timers fire (each still resizes whatever ViewportWidget
			// it captured, even one that's no longer active/visible).
#if !defined(Q_OS_WIN)
			QPointer<ViewportWidget> guardedViewport(viewport);
			QTimer::singleShot(0, this, [guardedViewport]() {
				if (!guardedViewport)
					return;
				const QSize size = guardedViewport->size();
				guardedViewport->resize(size - QSize(1, 1));
				QTimer::singleShot(0, guardedViewport, [guardedViewport, size]() {
					if (guardedViewport)
						guardedViewport->resize(size);
					});
				});
#else
			// On Windows the viewport no longer crashes now that context sharing
			// keeps the heavy GL resources alive, but a newly-current QOpenGLWidget
			// can still sit black until the next native input event delivers a real
			// paint/update pulse. A lightweight updateRequest on the top-level
			// window, deferred until the tab switch has finished, provides that
			// pulse without the resize churn the old workaround used.
			QPointer<ViewportWidget> guardedViewport(viewport);
			QTimer::singleShot(0, this, [guardedViewport]() {
				if (!guardedViewport)
					return;
				guardedViewport->update();
				if (QWidget* topLevel = guardedViewport->window())
					topLevel->update();
				if (QWindow* windowHandle = guardedViewport->window()->windowHandle())
					windowHandle->requestUpdate();
			});
#endif
		}
	}
	else
	{
	}
}

void MainWindow::setDocumentTabDimmed(int tabIndex, bool dimmed)
{
	QTabBar* tabBar = _documentTabWidget->tabBar();
	if (tabIndex < 0 || tabIndex >= tabBar->count())
		return;
	// QPalette::NoRole clears the override and falls back to the tab bar's
	// normal palette - can't just use a fixed "undimmed" color here since
	// that would stop tracking the app's actual theme (dark vs light).
	tabBar->setTabTextColor(tabIndex, dimmed ? tabBar->palette().color(QPalette::Disabled, QPalette::WindowText)
	                                          : QColor());
}

void MainWindow::refreshDocumentDockTabStyling(ModelViewer* viewer)
{
	if (!_documentTabWidget)
		return;

	SceneGraph* sceneGraph = viewer ? viewer->sceneGraph() : nullptr;
	const bool hasVariants = sceneGraph && !sceneGraph->filesWithVariants().isEmpty();
	const bool hasAnimations = sceneGraph && !sceneGraph->filesWithAnimations().isEmpty();
	const bool hasCameras = sceneGraph && !sceneGraph->filesWithGltfCameras().isEmpty();

	setDocumentTabDimmed(0, !hasVariants);
	setDocumentTabDimmed(1, !hasAnimations);
	setDocumentTabDimmed(2, !hasCameras);
}

void MainWindow::showMaterialsPropertiesPage()
{
	_propertiesTabWidget->setCurrentIndex(0);
	_propertiesDock->setAsCurrentTab();
	_propertiesDock->toggleView(true);
}

void MainWindow::showTransformationsPropertiesPage()
{
	_propertiesTabWidget->setCurrentIndex(1);
	_propertiesDock->setAsCurrentTab();
	_propertiesDock->toggleView(true);
	if (ModelViewer* child = activeMdiChild())
	{
		child->getViewportWidget()->showTransformGizmoForSelection(true);
		child->updateTransformationValues();
	}
}

void MainWindow::showEnvironmentDockPage()
{
	_environmentDock->setAsCurrentTab();
	_environmentDock->toggleView(true);
}


MainWindow::~MainWindow()
{
	// Must be set before anything below runs Qt's cascading parent-child
	// destruction of _documentDockManager/_toolPanelDockManager and their
	// dock widgets - see the comment in createDocumentDock()'s
	// destroyed-signal handler.
	_shuttingDown = true;
	delete ui;
}

void MainWindow::readSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
	if (geometry.isEmpty()) {
		const QRect availableGeometry = screen()->availableGeometry();
		resize(availableGeometry.width() / 3, availableGeometry.height() / 2);
		move((availableGeometry.width() - width()) / 2,
			(availableGeometry.height() - height()) / 2);
	}
	else {
		restoreGeometry(geometry);
	}

	// Deliberately do NOT restore Qt-ADS saveState() here. Document docks are
	// created later and carry per-session UUID object names, so a persisted ADS
	// layout cannot round-trip reliably across launches once documents have ever
	// been open. Restoring that stale state made Qt-ADS treat document docks as
	// "unassigned" and forced layout repair paths against widgets that do not
	// exist in the new session. Keep old settings from re-triggering that logic.
	settings.remove("dockState");

	// _dockSplitter's orientation, unlike Qt-ADS's own dockState above, is a
	// plain bool with no per-session-UUID round-trip risk - safe to persist
	// normally. Runs after _dockSplitter/_actionSplitterOrientation already
	// exist (both constructed earlier in the constructor, well before this
	// call) but before any document is created, so createDocumentDock()'s
	// first-document bootstrap picks up the restored orientation when it
	// sizes _dockSplitter. QSignalBlocker avoids re-running the toggled
	// handler (and its own setOrientation()/setSizes() calls) here.
	const bool splitVertically = settings.value("toolPanelSplitVertically", false).toBool();
	_dockSplitter->setOrientation(splitVertically ? Qt::Vertical : Qt::Horizontal);
	if (_actionSplitterOrientation)
	{
		const QSignalBlocker blocker(_actionSplitterOrientation);
		_actionSplitterOrientation->setChecked(splitVertically);
	}

	// Restores a user-adjusted document/tool-panel ratio, same reasoning as
	// orientation above (plain ints, no per-session-UUID round-trip risk
	// unlike Qt-ADS's own dockState). Only applied if the saved list looks
	// sane (exactly 2 entries) - absent on a first-ever run, or if the
	// stored value ever gets corrupted, createDocumentDock()'s bootstrap
	// falls back to its own hardcoded 75/25 default via
	// _dockSplitterSizesRestored staying false.
	const QVariantList savedSizes = settings.value("dockSplitterSizes").toList();
	if (savedSizes.size() == 2)
	{
		_dockSplitter->setSizes({ savedSizes[0].toInt(), savedSizes[1].toInt() });
		_dockSplitterSizesRestored = true;
	}
}

void MainWindow::writeSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	settings.setValue("geometry", saveGeometry());
	settings.remove("dockState");
	settings.setValue("toolPanelSplitVertically", _dockSplitter->orientation() == Qt::Vertical);
	const QList<int> sizes = _dockSplitter->sizes();
	if (sizes.size() == 2)
		settings.setValue("dockSplitterSizes", QVariantList{ sizes[0], sizes[1] });
}


QStringList MainWindow::readRecentFiles(QSettings& settings)
{
	QStringList result;
	const int count = settings.beginReadArray(recentFilesKey());
	for (int i = 0; i < count; ++i) {
		settings.setArrayIndex(i);
		result.append(settings.value(fileKey()).toString());
	}
	settings.endArray();
	return result;
}

void MainWindow::writeRecentFiles(const QStringList& files, QSettings& settings)
{
	const int count = files.size();
	settings.beginWriteArray(recentFilesKey());
	for (int i = 0; i < count; ++i) {
		settings.setArrayIndex(i);
		settings.setValue(fileKey(), files.at(i));
	}
	settings.endArray();
}

QPushButton* MainWindow::cancelTaskButton()
{
	return _cancelTaskButton;
}

void MainWindow::showStatusMessage(const QString& message, int timeout)
{
	if (!_mainWindow)
	{
		return;
	}
	if (QThread::currentThread() != _mainWindow->thread())
	{
		QMetaObject::invokeMethod(_mainWindow, [message, timeout]() {
			MainWindow::showStatusMessage(message, timeout);
		}, Qt::QueuedConnection);
		return;
	}
	_mainWindow->statusBar()->showMessage(message, timeout);
	_mainWindow->statusBar()->update();
}

void MainWindow::showProgressBar(const bool showCancelButton)
{
	if (!_mainWindow)
	{
		return;
	}
	if (QThread::currentThread() != _mainWindow->thread())
	{
		QMetaObject::invokeMethod(_mainWindow, [showCancelButton]() {
			MainWindow::showProgressBar(showCancelButton);
		}, Qt::QueuedConnection);
		return;
	}
	_fileLoadCancelRequested = false;
	_mainWindow->_progressBar->show();
#if defined _WIN32 && QT_VERSION_MAJOR == 5
	_mainWindow->_windowsTaskbarProgress->show();
#endif 
	if (showCancelButton)
	{
		_mainWindow->_cancelTaskButton->setText(QObject::tr("Cancel Loading"));
		_mainWindow->_cancelTaskButton->setEnabled(true);
		_mainWindow->_cancelTaskButton->show();
	}
}

void MainWindow::showIndeterminateProgressBar()
{
	if (!_mainWindow)
	{
		return;
	}
	if (QThread::currentThread() != _mainWindow->thread())
	{
		QMetaObject::invokeMethod(_mainWindow, []() {
			MainWindow::showIndeterminateProgressBar();
		}, Qt::QueuedConnection);
		return;
	}
	_fileLoadCancelRequested = false;
	_mainWindow->_progressBar->setRange(0, 0);
	_mainWindow->_progressBar->show();	
#if defined _WIN32 && QT_VERSION_MAJOR == 5
	_mainWindow->_windowsTaskbarProgress->show();
#endif 
	_mainWindow->_cancelTaskButton->show();
	_mainWindow->_cancelTaskButton->setText(QObject::tr("Cancel Loading"));
	_mainWindow->_cancelTaskButton->setEnabled(true);
}

void MainWindow::resetProgressBar()
{
	if (!_mainWindow)
	{
		return;
	}
	if (QThread::currentThread() != _mainWindow->thread())
	{
		QMetaObject::invokeMethod(_mainWindow, []() {
			MainWindow::resetProgressBar();
		}, Qt::QueuedConnection);
		return;
	}
	_mainWindow->_progressBar->reset();
	_mainWindow->_progressBar->setRange(0, 100);	
}

void MainWindow::hideProgressBar()
{
	if (!_mainWindow)
	{
		return;
	}
	if (QThread::currentThread() != _mainWindow->thread())
	{
		QMetaObject::invokeMethod(_mainWindow, []() {
			MainWindow::hideProgressBar();
		}, Qt::QueuedConnection);
		return;
	}
	_mainWindow->_progressBar->hide();
#if defined _WIN32 && QT_VERSION_MAJOR == 5
	_mainWindow->_windowsTaskbarProgress->hide();
#endif 
	_mainWindow->_cancelTaskButton->hide();
	_mainWindow->_cancelTaskButton->setText(QObject::tr("Cancel Loading"));
	_mainWindow->_cancelTaskButton->setEnabled(true);
	_fileLoadCancelRequested = false;
}

void MainWindow::setProgressValue(const int& value)
{
	if (!_mainWindow)
	{
		return;
	}
	if (QThread::currentThread() != _mainWindow->thread())
	{
		QMetaObject::invokeMethod(_mainWindow, [value]() {
			MainWindow::setProgressValue(value);
		}, Qt::QueuedConnection);
		return;
	}
	if (value == 0)
	{
		_mainWindow->_progressBar->reset();
#if defined _WIN32 && QT_VERSION_MAJOR == 5
		_mainWindow->_windowsTaskbarProgress->reset();
#endif 
	}
	else
	{
		_mainWindow->_progressBar->setValue(value);
#if defined _WIN32 && QT_VERSION_MAJOR == 5
		_mainWindow->_windowsTaskbarProgress->setValue(value);
#endif 
	}
	_mainWindow->_progressBar->update();
}

void MainWindow::setCancelButtonEnabled(bool enabled)
{
	_mainWindow->_cancelTaskButton->setEnabled(enabled);
}

void MainWindow::setCancelButtonText(const QString& text)
{
	_mainWindow->_cancelTaskButton->setText(text);
}

void MainWindow::requestFileLoadCancel()
{
	_fileLoadCancelRequested = true;
}

void MainWindow::clearFileLoadCancel()
{
	_fileLoadCancelRequested = false;
}

bool MainWindow::isFileLoadCancelRequested()
{
	return _fileLoadCancelRequested;
}

void MainWindow::on_actionExit_triggered(bool /*checked*/)
{
	if (canExit())
	{		
		qApp->exit();
	}
}

void MainWindow::on_actionQuick_Help_triggered()
{
	// Create as a member variable or use static to keep one instance
	if (!_helpDialog)
	{
		_helpDialog = new QuickHelpDialog(this);
		_helpDialog->setAttribute(Qt::WA_DeleteOnClose);
		_helpDialog->setModal(false);
		_helpDialog->setWindowModality(Qt::NonModal);
		connect(_helpDialog, &QObject::destroyed, []() {
			_helpDialog = nullptr; // Reset pointer when dialog is closed
			});
	}

	_helpDialog->show();
	_helpDialog->raise();
	_helpDialog->activateWindow();
}

void MainWindow::on_actionTutorial_triggered()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	// Check if user has saved a preference
	QString tutorialMode = settings.value("tutorial/displayMode", "ask").toString();

	if (tutorialMode == "ask")
	{
		// Ask user which method they prefer
		QMessageBox msgBox(this);
		msgBox.setWindowTitle(tr("Tutorial Display Method"));
		msgBox.setText(tr("How would you like to view the tutorial?"));
		msgBox.setInformativeText(tr("Choose between an integrated dialog or opening in your web browser."));
		msgBox.setIcon(QMessageBox::Question);

		QPushButton* dialogButton = msgBox.addButton(tr("Dialog Window"), QMessageBox::AcceptRole);
		QPushButton* browserButton = msgBox.addButton(tr("Web Browser"), QMessageBox::AcceptRole);
		msgBox.addButton(QMessageBox::Cancel);

		QCheckBox* rememberCheckbox = new QCheckBox(tr("Remember my choice"), &msgBox);
		msgBox.setCheckBox(rememberCheckbox);

		msgBox.exec();

		if (msgBox.clickedButton() == dialogButton)
		{
			tutorialMode = "dialog";
			if (rememberCheckbox->isChecked())
			{
				settings.setValue("tutorial/displayMode", "dialog");
				settings.setValue("checkTutorialLaunch", false);
			}
		}
		else if (msgBox.clickedButton() == browserButton)
		{
			tutorialMode = "browser";
			if (rememberCheckbox->isChecked())
			{
				settings.setValue("tutorial/displayMode", "browser");
				settings.setValue("checkTutorialLaunch", false);
			}
		}
		else
		{
			// User cancelled
			return;
		}
	}

	// Open tutorial based on chosen mode
	if (tutorialMode == "dialog")
	{
		TutorialDialog* tutorial = new TutorialDialog(this);
		tutorial->setAttribute(Qt::WA_DeleteOnClose);
		tutorial->show();
	}
	else if (tutorialMode == "browser")
	{
		QString tutorialPath = PathUtils::getDataDirectory() + "/data/tutorials/index.html";
		QFile tutorialFile(tutorialPath);

		if (tutorialFile.exists())
		{
			QDesktopServices::openUrl(QUrl::fromLocalFile(tutorialPath));
		}
		else
		{
			QMessageBox::warning(this, tr("Tutorial Not Found"),
				tr("Tutorial file not found at:\n%1\n\n"
					"Please ensure the tutorial files are installed correctly.").arg(tutorialPath));
		}
	}
}

#include "LogViewer.h"
void MainWindow::on_actionView_Logs_triggered()
{
	// Create as a member variable or use static to keep one instance
	static LogViewer* logViewer = nullptr;
	if (!logViewer)
	{
		logViewer = new LogViewer(this);
		logViewer->setAttribute(Qt::WA_DeleteOnClose);
		connect(logViewer, &QObject::destroyed, []() {
			logViewer = nullptr; // Reset pointer when dialog is closed
			});
	}
	logViewer->show();
	logViewer->raise();
	logViewer->activateWindow();
}

void MainWindow::on_actionOpen_Logs_Folder_triggered()
{
	// Open the logs folder in the system file explorer
	QString logsPath = Logger::instance().getLogDirectory();
	if (QDir(logsPath).exists())
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(logsPath));
	}
	else
	{
		QMessageBox::warning(this, tr("Logs Folder Not Found"),
			tr("The logs folder could not be found at:\n%1\n\n"
				"Please ensure the application has permission to create and access the logs directory.").arg(logsPath));
	}
}

void MainWindow::on_actionAbout_triggered(bool /*checked*/)
{
	unsigned int assimpMajor = aiGetVersionMajor();
	unsigned int assimpMinor = aiGetVersionMinor();

	QString aboutText = QString(tr("Application to visualize various 3D Models like OBJ and StereoLithography models using the ASSIMP library,"
		" and STEP, IGES, and BREP files using the OpenCASCADE library\n\n"
		"App Version: %1\n"
		"ASSIMP Version: %2.%3\n\n"
		"Copyright \u00A9 2021 Sharjith Naramparambath - sharjith@gmail.com\n\n"))
		.arg(APP_VERSION_STRING)
		.arg(assimpMajor)
		.arg(assimpMinor);

	QMessageBox::about(this,
		tr("About 3D Model Viewer"),
		aboutText + graphicsInfo());
}

void MainWindow::on_actionAbout_Qt_triggered(bool /*checked*/)
{
	QMessageBox::aboutQt(this, tr("About Qt"));
}

void MainWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

#if defined _WIN32 && QT_VERSION_MAJOR == 5
	QWinTaskbarButton* windowsTaskbarButton = new QWinTaskbarButton(this);    //Create the taskbar button which will show the progress
	windowsTaskbarButton->setWindow(windowHandle());    //Associate the taskbar button to the progress bar, assuming that the progress bar is its own window
	_windowsTaskbarProgress = windowsTaskbarButton->progress();
#endif

	if (_bFirstTime)
	{
		//std::vector<int> mod = { 5 };
		//_viewers[0]->getViewportWidget()->setDisplayList(mod);
		presentDocumentFullscreen(_viewers[0]);
		_viewers[0]->updateDisplayList();

		// Qt-ADS's dynamic-property-based selectors ([activeTab="true"] etc -
		// used by both its own default styling and the gradient-removal/
		// gray-label overrides appended in the constructor) aren't guaranteed
		// to be re-evaluated on a freshly created tab's very first paint -
		// confirmed via testing: those overrides only took visible effect
		// after the user actually clicked a tab, which is when Qt-ADS's own
		// tab-switch handling happens to force a repolish as a side effect of
		// updating its own state. Re-setting the (unchanged) stylesheet here
		// forces Qt's documented full unpolish/polish cascade through the
		// whole dock manager subtree - deferred one event-loop tick so it
		// runs after this first document's own tab widget actually exists.
		// Applied to both managers: the underlying Qt repolish issue is
		// generic to any CDockManager's tabs on their first real paint
		// (which for both managers is only now, when the window itself is
		// first shown - not when their tabs were constructed), not specific
		// to documents.
		QTimer::singleShot(0, _documentDockManager, [this]() {
			_documentDockManager->setStyleSheet(_documentDockManager->styleSheet());
		});
		QTimer::singleShot(0, _toolPanelDockManager, [this]() {
			_toolPanelDockManager->setStyleSheet(_toolPanelDockManager->styleSheet());
		});

		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		if (settings.value("showQuickHelpOnStartup", true).toBool())
		{
			QTimer::singleShot(150, this, &MainWindow::on_actionQuick_Help_triggered);
		}

		_bFirstTime = false;
	}
}

void MainWindow::closeEvent(QCloseEvent* event)
{	
	if (canExit())
	{
		writeSettings();
		event->accept();
		qApp->exit();		
	}
	else
	{
		event->ignore();
	}	
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent* event)
{
	QStringList supportedExtensions = ModelViewerApplication::supportedImportExtensions();
	QApplication::setOverrideCursor(Qt::WaitCursor);
	foreach(const QUrl & url, event->mimeData()->urls())
	{
		QString fileName = url.toLocalFile();
		ModelViewer::setLastOpenedDir(QFileInfo(fileName).path()); // store path for next time
		QFileInfo fi(fileName);
		QString extn = fi.suffix();
		if (!supportedExtensions[0].contains(extn, Qt::CaseInsensitive)
			&& extn != "mvf")
		{
			QMessageBox::critical(this, tr("Error"), url.toString() + tr("\nUnsupported file format: ") + extn);
		}
		else
		{
			openFile(fileName);
		}
	}
	QApplication::restoreOverrideCursor();
}

void MainWindow::on_actionNew_triggered()
{
	ModelViewer* viewer = new ModelViewer(nullptr);
	viewer->setAttribute(Qt::WA_DeleteOnClose);
	viewer->setWindowTitle(QString("Session %1").arg(++_viewerCount));
	createDocumentDock(viewer);
	//std::vector<int> mod = { 5 };
	//viewer->getViewportWidget()->setDisplayList(mod);
	viewer->updateDisplayList();
}

void MainWindow::presentDocumentFullscreen(ModelViewer* viewer)
{
	// Documents are native CDockWidgets now (see createDocumentDock()) - Qt-
	// ADS manages showing/sizing each document's tab entirely on its own, no
	// manual geometry/frame/maximize-state handling needed here at all.
	if (ads::CDockWidget* dock = _documentDocks.value(viewer))
		dock->setAsCurrentTab();
	// The very first startup document can be visibly current before Qt-ADS
	// has emitted any focus/currentChanged signal that would otherwise drive
	// _activeDocument. Establish it eagerly here so commands that depend on
	// activeMdiChild() (for example Shift+Recent-file import) target the
	// already-visible document even before the first user focus transition.
	activateDocument(viewer);
}

void MainWindow::on_actionOpen_triggered()
{
	QFileDialog fileDialog(this, tr("Open Model File"), ModelViewer::getLastOpenedDir());
	fileDialog.setFileMode(QFileDialog::ExistingFile);	
	QStringList supportedExtensions = ModelViewerApplication::supportedImportExtensions();
	supportedExtensions[0].insert(supportedExtensions[0].lastIndexOf(')'), " *.mvf");
	QStringList nativeFilter = { "ModelViewer Files (*.mvf)" };
	supportedExtensions.append(nativeFilter);
	fileDialog.setNameFilters(supportedExtensions);
	fileDialog.selectNameFilter(ModelViewer::getLastSelectedFilter());
	QString fileName;
	if (fileDialog.exec())
	{
        fileName = fileDialog.selectedFiles().at(0);
		ModelViewer::setLastSelectedFilter(fileDialog.selectedNameFilter());
	}

	if (!fileName.isEmpty())
	{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		openFile(fileName);
		QApplication::restoreOverrideCursor();
		MainWindow::mainWindow()->activateWindow();
		QApplication::alert(MainWindow::mainWindow());
	}
}

bool MainWindow::openFile(const QString& fileName)
{
	if (ads::CDockWidget* existing = findDocumentDock(fileName))
	{
		existing->setAsCurrentTab();
		// See cycleActiveDocument()'s comment: setAsCurrentTab() alone can
		// silently no-op (no currentChanged emitted) if this document is
		// already its area's current tab - explicit call keeps
		// _activeDocument in sync regardless.
		if (ModelViewer* child = qobject_cast<ModelViewer*>(existing->widget()))
			activateDocument(child);
		return true;
	}
	const bool succeeded = loadFile(fileName);

	return succeeded;
}

void MainWindow::cancelFileLoading()
{
	if (activeMdiChild())
	{
		ViewportWidget* viewportWidget = activeMdiChild()->getViewportWidget();
		if (viewportWidget)
			viewportWidget->cancelAssImpModelLoading();
	}
}

void MainWindow::closeSubWindow()
{
	// close() runs ModelViewer::closeEvent() (unsaved-changes save prompt)
	// and, if accepted, WA_DeleteOnClose schedules its deletion - the
	// destroyed-signal handler in createDocumentDock() removes it from
	// _viewers and tears down its CDockWidget from there.
	if (ModelViewer* viewer = activeMdiChild())
		viewer->close();
}

void MainWindow::closeAllSubWindows()
{
	// Snapshot first - closing each viewer mutates _viewers via the
	// destroyed-signal handler in createDocumentDock().
	const QList<ModelViewer*> viewers = _viewers;
	for (ModelViewer* viewer : viewers)
		viewer->close();
}

bool MainWindow::loadFile(const QString& fileName)
{
	ModelViewer* child = createMdiChild();
	presentDocumentFullscreen(child);
	const bool succeeded = child->loadFile(fileName);
	if (!succeeded)
		child->close();
	else
	{
		child->setWindowTitle(QFileInfo(fileName).fileName());
		child->setCurrentFile(fileName);
		child->setDocumentModified(false);
		MainWindow::prependToRecentFiles(fileName);
		updateMenus();
	}
	return succeeded;
}

void MainWindow::on_actionImport_triggered()
{
	if (activeMdiChild())
	{
		activeMdiChild()->importModel();
		updateMenus();
	}

}

void MainWindow::on_actionExport_triggered()
{
	if (activeMdiChild())
		activeMdiChild()->exportModel();
}

void MainWindow::on_actionSave_triggered()
{
	if (activeMdiChild())
		activeMdiChild()->save();
}

void MainWindow::on_actionSave_As_triggered()
{
	if (activeMdiChild())
		activeMdiChild()->saveAs();
}

#include "SettingsDialog.h"
void MainWindow::on_actionSettings_triggered()
{
	SettingsDialog* settingsDialog = new SettingsDialog(this);
	settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
	settingsDialog->setMaxMSAASamples(ModelViewerApplication::supportedMSAASamples());
	settingsDialog->setMaxAnisotropy(ModelViewerApplication::supportedAnisotropicFilteringLevel());	
	settingsDialog->setModal(true);
	settingsDialog->show();

	connect(settingsDialog, &SettingsDialog::settingsChanged, this, [this, settingsDialog]() {
		if (!_viewers.empty())
		{
			int anIsoVals[] = {1, 2, 4, 8, 16};
			int idx = settingsDialog->renderingAnisotropyIndex();
			if (idx < 0 || idx >= static_cast<int>(sizeof(anIsoVals) / sizeof(anIsoVals[0])))
				idx = 0;
			for (ModelViewer* viewer : _viewers)
			{
				ViewportWidget* vp = viewer ? viewer->getViewportWidget() : nullptr;
				if (!vp) continue;

				const ViewportWidget::CameraPose pose = vp->saveCameraPose();

				vp->setAnisotropicFilteringLevel(anIsoVals[idx]);
				vp->setShowCenterAxisOverride(settingsDialog->displayShowCenterTrihedron());
				vp->setShowCornerAxisOverride(settingsDialog->displayShowCornerTrihedron());
				vp->setShowViewCubeOverride(settingsDialog->displayShowViewCube());
				vp->setCornerAxisPosition(static_cast<CornerAxisPosition>(settingsDialog->displayCornerTrihedronPosition()));
				if (vp->getSelectionManager())
					vp->getSelectionManager()->setHoverHighlightMode(settingsDialog->displayHoverHighlightMode());
				vp->setPerspFOV(settingsDialog->displayFieldOfView());
				if (vp->getViewToolbar())
					vp->getViewToolbar()->setFeatureEdgeModesVisible(settingsDialog->displayShowWireframe());
				vp->setDebugOverlayAvailability(
					settingsDialog->displayShowBoundingBox(),
					settingsDialog->displayShowVertexNormals(),
					settingsDialog->displayShowFaceNormals());
				vp->loadBgColorSettings();
				vp->loadNavigationSettings();
				vp->loadRenderSettings();

				vp->restoreCameraPose(pose);
			}
		}
		// Re-read QSettings now that they have been committed (OK / Apply).
		updateMenus();
		});

	// Live toggle: update the Tools menu immediately when the checkbox is
	// flipped, without waiting for OK/Apply (which is when QSettings is written).
	// The bool is passed directly so we don't have to re-read QSettings.
	connect(settingsDialog, &SettingsDialog::textureDebugPanelVisibilityChanged,
	        this, [this](bool enabled) {
		const bool hasMdiChild = (activeMdiChild() != nullptr);
		ui->actionToolsSeparator->setVisible(enabled && hasMdiChild);
		ui->actionTextureDebugger->setVisible(enabled && hasMdiChild);
	});

	connect(settingsDialog, &SettingsDialog::clearCachesRequested, this, [this]() {
		for (ModelViewer* viewer : _viewers)
		{
			ViewportWidget* vp = viewer ? viewer->getViewportWidget() : nullptr;
			if (!vp)
				continue;
			// Each MDI document owns its own GL context; the cache-clearing
			// glDeleteTextures calls require that context current.
			vp->makeCurrent();
			vp->clearTextureCache();
			vp->doneCurrent();
		}
	});
}

// Tile/Cascade have no meaning for documents anymore: they're always-tabbed
// Qt-ADS dock widgets (see createDocumentDock()) with no independent
// per-document geometry left to arrange. The slots are kept as no-ops,
// rather than removed, only because ui_MainWindow.h's auto-connect wiring
// (QMetaObject::connectSlotsByName) expects them to exist; the actions
// themselves are permanently hidden in updateMenus().
void MainWindow::on_actionTile_Horizontally_triggered()
{
}

void MainWindow::on_actionTile_Vertically_triggered()
{
}

void MainWindow::on_actionTile_triggered()
{
}

void MainWindow::on_actionCascade_triggered()
{
}

MainWindow* MainWindow::mainWindow()
{
	if (_mainWindow == nullptr)
		_mainWindow = new MainWindow();
	return _mainWindow;
}

void MainWindow::updateMenus()
{
	bool hasMdiChild = (activeMdiChild() != nullptr);
	ui->actionSave->setVisible(hasMdiChild);
	ui->actionSave_As->setVisible(hasMdiChild);
	if (hasMdiChild)
	{
		ui->actionSave->setEnabled(activeMdiChild()->documentModified());	
		ui->actionSave_As->setEnabled(!activeMdiChild()->getViewportWidget()->getMeshStore().empty());
	}
#ifndef QT_NO_CLIPBOARD
	//pasteAct->setEnabled(hasMdiChild);
#endif
	ui->actionImport->setVisible(hasMdiChild);
	ui->actionExport->setVisible(hasMdiChild);
	ui->actionClose->setEnabled(hasMdiChild);
	ui->actionFileClose->setVisible(hasMdiChild);
	// _documentDocks.count(), not _documentDockArea->dockWidgetsCount():
	// the latter now always includes the permanent placeholder dock (see
	// the constructor), which would make this off-by-one against the
	// actual number of open documents.
	const int documentCount = _documentDocks.count();
	ui->actionClose_All->setVisible(hasMdiChild && documentCount > 1);

	ui->menuWindows->menuAction()->setVisible(hasMdiChild);

	// Tools menu is always visible now that Ray Tracing gives it a
	// permanent, non-debug entry - only the Texture Debugger action (and
	// its separator) stay gated behind the Settings debug flag.
	ui->actionRayTracing->setEnabled(hasMdiChild);
	{
		QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		const bool debugEnabled = s.value("showTextureDebugPanelCheckBox", false).toBool();
		ui->actionToolsSeparator->setVisible(debugEnabled && hasMdiChild);
		ui->actionTextureDebugger->setVisible(debugEnabled && hasMdiChild);
	}
	// Tile/Cascade: no longer meaningful (see on_actionTile*/on_actionCascade
	// _triggered() above) - permanently hidden rather than left visible-but-
	// inert.
	ui->actionTile->setVisible(false);
	ui->actionTile_Horizontally->setVisible(false);
	ui->actionTile_Vertically->setVisible(false);
	ui->actionCascade->setVisible(false);
	ui->actionNext->setVisible(hasMdiChild && documentCount > 1);
	ui->actionPrevious->setVisible(hasMdiChild && documentCount > 1);

#ifndef QT_NO_CLIPBOARD
	//bool hasSelection = (activeMdiChild() && activeMdiChild()->textCursor().hasSelection());
	//cutAct->setEnabled(hasSelection);
	//copyAct->setEnabled(hasSelection);
#endif

	ui->actionUndo->setVisible(hasMdiChild);
	ui->actionRedo->setVisible(hasMdiChild);
	if (hasMdiChild)
	{
		ui->actionUndo->setEnabled(activeMdiChild()->hasUndo());
		ui->actionRedo->setEnabled(activeMdiChild()->hasRedo());
	}
	// Undo/Redo actions
	ui->actionUndo->setVisible(hasMdiChild);
	ui->actionRedo->setVisible(hasMdiChild);

	if (hasMdiChild && activeMdiChild()->getUndoStack())
	{
		QUndoStack* stack = activeMdiChild()->getUndoStack();

		// Update with descriptive text
		if (stack->canUndo())
			ui->actionUndo->setText(tr("&Undo %1").arg(stack->undoText()));
		else
			ui->actionUndo->setText(tr("&Undo"));

		if (stack->canRedo())
			ui->actionRedo->setText(tr("&Redo %1").arg(stack->redoText()));
		else
			ui->actionRedo->setText(tr("&Redo"));

		ui->actionUndo->setEnabled(stack->canUndo());
		ui->actionRedo->setEnabled(stack->canRedo());
	}
	else
	{
		ui->actionUndo->setText(tr("&Undo"));
		ui->actionRedo->setText(tr("&Redo"));
		ui->actionUndo->setEnabled(false);
		ui->actionRedo->setEnabled(false);
	}
}

void MainWindow::updateWindowMenu()
{
	ui->menuWindows->clear();
	ui->menuWindows->addAction(ui->actionClose);
	ui->menuWindows->addAction(ui->actionClose_All);
	ui->menuWindows->addSeparator();
	ui->menuWindows->addAction(ui->actionNext);
	ui->menuWindows->addAction(ui->actionPrevious);

	if (!_viewers.isEmpty())
		ui->menuWindows->addSeparator();

	for (ModelViewer* child : std::as_const(_viewers))
	{
		ads::CDockWidget* dock = _documentDocks.value(child);
		if (!dock)
			continue;

		const QString text = child->currentFile().isEmpty() ? child->windowTitle() : QFileInfo(child->currentFile()).fileName();
		QAction* action = ui->menuWindows->addAction(text, dock, [this, dock, child]() {
			dock->setAsCurrentTab();
			// See cycleActiveDocument()'s comment: setAsCurrentTab() alone
			// can silently no-op if `child` is already its area's current
			// tab - explicit call keeps _activeDocument in sync regardless.
			activateDocument(child);
			});
		action->setCheckable(true);
		action->setChecked(child == activeMdiChild());
	}
}

ModelViewer* MainWindow::activeMdiChild() const
{
	// _activeDocument, not _documentDockArea->currentDockWidget(): a
	// document split off into its own area (see createDocumentDock()'s
	// dockAreaCreated handler) can be the one the user is actually looking
	// at, and _documentDockArea only ever tracked the original area.
	return _activeDocument;
}

ads::CDockWidget* MainWindow::findDocumentDock(const QString& fileName) const
{
	for (auto it = _documentDocks.constBegin(); it != _documentDocks.constEnd(); ++it)
	{
		if (it.key()->currentFile() == fileName)
			return it.value();
	}
	return nullptr;
}

bool MainWindow::canExit()
{
	// Check user preference for exit confirmation
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	bool confirmOnExit = settings.value("checkConfirmExit", true).toBool();
	if (confirmOnExit)
	{
		QMessageBox::StandardButton reply = QMessageBox::question(
			this,
			tr("Confirm Exit"),
			tr("Are you sure you want to exit the application?"),
			QMessageBox::Yes | QMessageBox::No
		);
		if (reply != QMessageBox::Yes)
		{
			return false; // User chose not to exit
		}
	}

	// Query each open document
	for (ModelViewer* child : std::as_const(_viewers))
	{
		// Create a close event and let the child handle it
		// This will trigger ModelViewer::closeEvent which shows the save dialog
		QCloseEvent closeEvent;
		child->closeEvent(&closeEvent);

		// If the child rejected the close (user clicked Cancel), return false
		if (!closeEvent.isAccepted())
		{
			return false;  // Exit cancelled - don't close application
		}
	}

	// All children accepted the close event - safe to exit
	return true;
}

bool MainWindow::hasRecentFiles()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	const int count = settings.beginReadArray(recentFilesKey());
	settings.endArray();
	return count > 0;
}

void MainWindow::prependToRecentFiles(const QString& fileName)
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	const QStringList oldRecentFiles = readRecentFiles(settings);
	QStringList recentFiles = oldRecentFiles;
	recentFiles.removeAll(fileName);
	recentFiles.prepend(fileName);
	if (oldRecentFiles != recentFiles)
		writeRecentFiles(recentFiles, settings);

	setRecentFilesVisible(!recentFiles.isEmpty());
}

void MainWindow::setRecentFilesVisible(bool visible)
{
	recentFileSubMenuAct->setVisible(visible);
	recentFileSeparator->setVisible(visible);
}

void MainWindow::updateRecentFileActions()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	const QStringList recentFiles = readRecentFiles(settings);
	const int count = qMin(int(MaxRecentFiles), recentFiles.size());

	int i = 0;
	for (; i < count; ++i)
	{
		const QString filePath = recentFiles.at(i);
		const QString fileName = QFileInfo(filePath).fileName();

		QAction* act = recentFileActs[i];
		act->setText(tr("&%1 %2").arg(i + 1).arg(fileName));
		act->setData(filePath);
		act->setStatusTip(tr("%1 -> Shift-click to import into active document").arg(filePath));
		act->setToolTip(tr("Click to open • Shift-click to import into active window"));
		act->setVisible(true);
	}

	for (; i < MaxRecentFiles; ++i)
		recentFileActs[i]->setVisible(false);
}

void MainWindow::removeFromRecentFiles(const QString& fileName)
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    QStringList recentFiles = readRecentFiles(settings);
    recentFiles.removeAll(fileName);
    writeRecentFiles(recentFiles, settings);
    setRecentFilesVisible(!recentFiles.isEmpty());
}

void MainWindow::openRecentFile()
{
	QAction* action = qobject_cast<QAction*>(sender());
	if (!action)
		return;

	const QString filePath = action->data().toString();
	if (filePath.isEmpty())
		return;

	if (!QFile::exists(filePath))
	{
		QMessageBox::StandardButton reply = QMessageBox::question(
			this,
			tr("File Not Found"),
			tr("The file '%1' no longer exists. Would you like to remove it from the recent files?")
			.arg(filePath),
			QMessageBox::Yes | QMessageBox::No
		);

		if (reply == QMessageBox::Yes)
		{
			removeFromRecentFiles(filePath);
			updateRecentFileActions();
		}
		return;
	}

	const bool shiftPressed =
		QApplication::keyboardModifiers() & Qt::ShiftModifier;

	QApplication::setOverrideCursor(Qt::WaitCursor);

	if (shiftPressed && activeMdiChild())
	{
		// SHIFT -> Import into active document
		activeMdiChild()->loadFile(filePath);
	}
	else
	{
		// Default -> Open as new document
		openFile(filePath);
	}

	QApplication::restoreOverrideCursor();
	activateWindow();
	QApplication::alert(this);
}

