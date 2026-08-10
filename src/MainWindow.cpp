
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

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QDockWidget>
#include <QAbstractScrollArea>
#include <QBrush>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTabBar>
#include <QSet>
#include <QStyle>
#include <QScrollArea>
#include <QShortcut>
#include <QPointer>
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

	// Documents live in a native QMdiArea (tabbed, with tiling/cascading/
	// restoring and most-recently-used Next/Previous via
	// ActivationHistoryOrder, all built in) - built programmatically here
	// rather than via the .ui file, matching this constructor's existing
	// style for everything below. Most properties mirror the pre-Qt-ADS
	// .ui definition of this widget (viewMode is the one deliberate
	// departure - see below).
	{
		_mdiArea = new QMdiArea();
		_mdiArea->setAutoFillBackground(false);
		_mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		_mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		_mdiArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
		_mdiArea->setBackground(QBrush(QColor(35, 35, 35)));
		_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
		// TabbedView, not the pre-Qt-ADS default of SubWindowView - gives a
		// real tab bar across the top of the document area. tileSubWindows()/
		// cascadeSubWindows() (see on_actionTile*/on_actionCascade_triggered())
		// still work in this mode - confirmed via Qt's own source
		// (qmdiarea.cpp): neither function nor the QMdiAreaPrivate::rearrange()
		// they both funnel through checks viewMode at all.
		_mdiArea->setViewMode(QMdiArea::TabbedView);
		// Document mode for a cleaner/flatter tab bar, closable (per-tab close
		// button, handled internally by QMdiArea - closes the underlying
		// QMdiSubWindow the same as the existing Close action) and movable
		// (drag-to-reorder) tabs, and North to match every other tab widget
		// in this app (_documentTabWidget/_propertiesTabWidget above).
		_mdiArea->setDocumentMode(true);
		_mdiArea->setTabsClosable(true);
		_mdiArea->setTabsMovable(true);
		_mdiArea->setTabPosition(QTabWidget::North);
		setCentralWidget(_mdiArea);

		// Single activation source, unlike Qt-ADS's multi-area document
		// splitting - one connection covers every document-switch case.
		connect(_mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::handleActiveDocumentChanged);

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

		// North, not Qt's own default (South) for a tabified dock group's
		// tab bar - matches every other QTabWidget in this app
		// (_documentTabWidget/_propertiesTabWidget above both explicitly
		// set North too).
		setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);

		auto* documentDock = new QDockWidget(tr("Document"), this);
		documentDock->setObjectName(QStringLiteral("documentDock"));
		// A tabified QDockWidget group's tab icon comes from windowIcon(),
		// not any dock-specific API.
		documentDock->setWindowIcon(QIcon(":/icons/res/document-root.png"));
		documentDock->setWidget(documentTabContainer);
		addDockWidget(Qt::RightDockWidgetArea, documentDock);
		_documentDock = documentDock;

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

		_propertiesDock = new QDockWidget(tr("Properties"), this);
		_propertiesDock->setObjectName(QStringLiteral("propertiesDock"));
		_propertiesDock->setWindowIcon(QIcon(":/icons/res/properties.png"));
		_propertiesDock->setWidget(_propertiesTabWidget);
		addDockWidget(Qt::RightDockWidgetArea, _propertiesDock);
		tabifyDockWidget(documentDock, _propertiesDock);

		// --- Environment dock: single shared VisualizationEnvironmentPanel.
		_visualizationEnvironmentPanel = new VisualizationEnvironmentPanel();
		auto* scrollAreaEnv = new QScrollArea();
		scrollAreaEnv->setWidgetResizable(true);
		scrollAreaEnv->setWidget(_visualizationEnvironmentPanel);

		_environmentDock = new QDockWidget(tr("Environment"), this);
		_environmentDock->setObjectName(QStringLiteral("environmentDock"));
		_environmentDock->setWindowIcon(QIcon(":/icons/res/environment.png"));
		_environmentDock->setWidget(scrollAreaEnv);
		addDockWidget(Qt::RightDockWidgetArea, _environmentDock);
		tabifyDockWidget(_propertiesDock, _environmentDock);

		// Default to the Document tab up front, matching the tab order
		// documents were tabbed in above.
		documentDock->raise();

		// Qt's default per-dock title bar redundantly repeats the active
		// tab's label above the tab strip while docked/tabbed. Two custom-
		// title-bar-widget suppression attempts (bare QWidget, then one
		// with an explicit fixed height) both broke drag-to-redock
		// regardless of the widget's size - Qt's built-in drag-to-redock is
		// gated on whether a custom title bar widget is set AT ALL, not on
		// its geometry, so that whole avenue is abandoned; the default
		// title bar widget itself is never touched below.
		//
		// This is a color-only attempt instead: QSS `color: transparent`
		// on ::title previously had no visible effect - that may be
		// because it relies on alpha blending the style's paint path
		// doesn't honor for this sub-control's text specifically, not
		// because the selector/property itself is ignored. Retrying with
		// an OPAQUE color matched to the title bar's own background
		// (palette(window), same value the background rule below uses)
		// instead of true transparency - a different rendering mechanism
		// that may succeed where alpha failed. Safe for dockability either
		// way: this never calls setTitleBarWidget(), so the real,
		// fully-functional default title bar is what's still there
		// underneath - only its rendered pixels are being asked to change.
		this->setStyleSheet(this->styleSheet() + QStringLiteral(
			"QDockWidget[dockTitleHidden=\"true\"]::title {"
			"  background: palette(window);"
			"  color: palette(window);"
			"}"));
		for (QDockWidget* dock : { documentDock, _propertiesDock, _environmentDock })
		{
			dock->setProperty("dockTitleHidden", true);
			connect(dock, &QDockWidget::topLevelChanged, dock, [dock](bool floating) {
				dock->setProperty("dockTitleHidden", !floating);
				dock->style()->unpolish(dock);
				dock->style()->polish(dock);
			});
		}

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

		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::materialApplied, this, [this](const Material& mat) {
			if (ModelViewer* child = activeMdiChild())
				child->onCustomMaterialApplied(mat);
		});
		connect(_materialPropertiesPanel, &MaterialPropertiesPanel::meshMaterialApplied, this,
			[this](const QUuid& meshUuid, const Material& material) {
				if (ModelViewer* child = activeMdiChild())
					child->applyMeshMaterial(meshUuid, material);
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

		// Closing a QDockWidget only hides it, but with nothing wired to its
		// toggleViewAction() there was no way back in from the UI - a closed
		// dock looked permanently gone.
		auto* viewMenu = new QMenu(tr("View"), this);
		viewMenu->addAction(documentDock->toggleViewAction());
		viewMenu->addAction(_propertiesDock->toggleViewAction());
		viewMenu->addAction(_environmentDock->toggleViewAction());
		menuBar()->insertMenu(ui->menuHelp->menuAction(), viewMenu);
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
	connect(nextAct, &QAction::triggered, _mdiArea, &QMdiArea::activateNextSubWindow);

	QAction* previousAct = ui->actionPrevious;
	previousAct->setShortcuts(QKeySequence::PreviousChild);
	previousAct->setStatusTip(tr("Move the focus to the previous "
		"window"));
	connect(previousAct, &QAction::triggered, _mdiArea, &QMdiArea::activatePreviousSubWindow);

	// Document-scoped shortcuts (Delete, and the rendering-mode/import/
	// export ones below) - single MainWindow-owned instances dispatching to
	// activeMdiChild(), same pattern as Undo/Redo/Ray Tracing right below.
	// A single instance per shortcut, scoped to MainWindow, can never be
	// ambiguous with itself regardless of how many documents are open or
	// how they're arranged - an improvement kept from when documents were
	// briefly Qt-ADS dock widgets (which had no per-document shortcut-scope
	// flag), even though QMdiSubWindow's Qt::SubWindow flag would make the
	// old per-document-QShortcut pattern viable again too.
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
		// wrapped in its QMdiSubWindow - see createDocumentSubWindow()), NOT to
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
	// nullptr, not _mdiArea - createDocumentSubWindow() below reparents this
	// into a new QMdiSubWindow regardless, so constructing with _mdiArea as
	// the parent just means the widget gets reparented twice (once
	// implicitly here, once by addSubWindow()) instead of once. QOpenGLWidget
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
	QMdiSubWindow* subWindow = createDocumentSubWindow(viewer);
	qDebug() << "MainWindow: created document via Open/createMdiChild -"
	         << "viewer=" << (void*)viewer << "subWindow=" << (void*)subWindow;

	// Apply persisted perspective FOV immediately so the first view uses the
	// setting before any settingsChanged signal fires.
	{
		QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		const int fov = s.value("fieldOfViewSpinBox", 45).toInt();
		viewer->getViewportWidget()->setPerspFOV(fov);
	}

	return viewer;
}

QMdiSubWindow* MainWindow::createDocumentSubWindow(ModelViewer* viewer)
{
	_viewers.append(viewer);
	// Keep _viewers in sync regardless of how the document is closed (MDI
	// close button, closeAllSubWindows, failed load, etc.). WA_DeleteOnClose
	// deletes the viewer without going through closeSubWindow(), so without
	// this the list accumulates dangling pointers that crash settings apply.
	connect(viewer, &QObject::destroyed, this, [this, viewer]() {
		// On application exit, _viewers is a MainWindow member being torn
		// down as part of ~MainWindow()'s own cascading parent-child
		// destruction - by the time this fires from deep inside that
		// cascade (QWidget's base-class destructor deleting children,
		// which reaches this document's QMdiSubWindow -> its wrapped
		// ModelViewer -> this destroyed() signal), ~MainWindow()'s BODY
		// and its own member destructors (including _viewers itself, a
		// plain QList<ModelViewer*> member, not a pointer) have already
		// run - C++ destroys derived members before the base class - so
		// _viewers is itself an already-destructed object at this point.
		// Touching it (even just removeAll()) is a real crash (confirmed
		// via a coredump: SIGSEGV inside QArrayDataPointer::
		// reallocateAndGrow() reached straight from this lambda), not just
		// a hypothetical one - the _shuttingDown check must come BEFORE
		// any member access, not after it. Only the normal
		// one-document-at-a-time close path (viewer->close() -> deferred
		// deleteLater()) reaches this outside of any destructor call
		// stack, where touching _viewers is safe.
		if (_shuttingDown)
			return;
		// Pointer value only - viewer is mid-QObject-destruction here (this
		// slot runs off QObject::destroyed(), emitted from ~QObject() after
		// ~ModelViewer() has already fully run), so calling any method on it
		// (windowTitle(), etc.) is undefined behavior. Diagnostic for the
		// "tab present, document gone" bug (2026-08-10) - confirms whether a
		// document actually goes through this normal destruction path or
		// disappears some other way.
		qDebug() << "MainWindow: document destroyed (normal close path) - viewer=" << (void*)viewer;
		_viewers.removeAll(viewer);

		const bool viewerWasActive = (viewer == _activeDocument);
		const bool viewerWasLastBound = (viewer == _lastBoundModelViewer);

		// Closing the active document can otherwise leave both
		// activeMdiChild() and activateDocument() pointing through stale
		// ModelViewer pointers until QMdiArea happens to emit a later
		// subWindowActivated() repair signal. Clear those eagerly here.
		if (viewerWasActive)
		{
			_activeDocument = nullptr;
			rebindSharedPanelsTo(nullptr);
			updateMenus();
		}
		if (viewerWasLastBound)
			_lastBoundModelViewer = nullptr;

		// Let QMdiArea finish promoting whichever surviving subwindow is
		// now active, then resync our document bookkeeping from that real
		// post-close state.
		QTimer::singleShot(0, this, [this]() {
			if (_shuttingDown || !_mdiArea)
				return;
			if (QMdiSubWindow* active = _mdiArea->activeSubWindow())
			{
				if (ModelViewer* child = qobject_cast<ModelViewer*>(active->widget()))
				{
					activateDocument(child);
					return;
				}
			}
			activateDocument(nullptr);
		});
	});
	connect(viewer, &ModelViewer::documentModifiedChanged,
	        this, [this, viewer](bool) {
	            if (viewer == activeMdiChild())
	                updateMenus();
	        });

	return _mdiArea->addSubWindow(viewer);
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

void MainWindow::handleActiveDocumentChanged(QMdiSubWindow* subWindow)
{
	ModelViewer* child = subWindow ? qobject_cast<ModelViewer*>(subWindow->widget()) : nullptr;
	activateDocument(child);
}

void MainWindow::activateDocument(ModelViewer* child)
{
	// Called both from handleActiveDocumentChanged() (via _mdiArea's
	// subWindowActivated signal) and directly, as a defensive safety net,
	// from presentDocumentFullscreen()/openFile()/updateWindowMenu() for
	// cases where subWindowActivated() may not fire on its own - skip
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
	_propertiesDock->setVisible(true);
	_propertiesDock->raise();
}

void MainWindow::showTransformationsPropertiesPage()
{
	_propertiesTabWidget->setCurrentIndex(1);
	_propertiesDock->setVisible(true);
	_propertiesDock->raise();
	if (ModelViewer* child = activeMdiChild())
	{
		child->getViewportWidget()->showTransformGizmoForSelection(true);
		child->updateTransformationValues();
	}
}

void MainWindow::showEnvironmentDockPage()
{
	_environmentDock->setVisible(true);
	_environmentDock->raise();
}


MainWindow::~MainWindow()
{
	// Must be set before anything below runs Qt's cascading parent-child
	// destruction of _mdiArea/_viewers and their child widgets - see the
	// comment in createDocumentSubWindow()'s destroyed-signal handler.
	_shuttingDown = true;
	delete ui;
}

void MainWindow::readSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	static constexpr int kNativeMdiDockStateVersion = 1;
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

	// Native QMainWindow dock state is not compatible with the old Qt-ADS
	// bytes that previously lived under "dockState". Gate restore on an
	// explicit version/key pair so a branch switch from Qt-ADS does not try
	// to deserialize stale foreign state into the new MDI/dock layout.
	if (settings.value("dockStateNativeMdiVersion", 0).toInt() == kNativeMdiDockStateVersion)
		restoreState(settings.value("dockStateNativeMdi").toByteArray());
}

void MainWindow::writeSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	static constexpr int kNativeMdiDockStateVersion = 1;
	settings.setValue("geometry", saveGeometry());
	settings.setValue("dockStateNativeMdiVersion", kNativeMdiDockStateVersion);
	settings.setValue("dockStateNativeMdi", saveState());
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

		// Native Qt has no public API for a tabified dock-widget group's
		// icon size (unlike QTabWidget::setIconSize(), used for
		// _documentTabWidget/_propertiesTabWidget's own inner tabs below) -
		// the group's QTabBar is an internal widget QMainWindowLayout
		// creates lazily once the window is actually laid out/shown, but
		// it's still a real, findable child widget. 20x20 chosen to look
		// uniform against the other tab bars in this window (user-tuned),
		// vs. the style's smaller (commonly 16px) default this would
		// otherwise use. Excludes the other three QTabBars already in this window's
		// tree (_documentTabWidget/_propertiesTabWidget's own inner tabs,
		// and _mdiArea's TabbedView document-tab bar) so only the
		// Document/Properties/Environment group's tab bar is touched.
		const QSet<QTabBar*> otherTabBars = { _documentTabWidget->tabBar(), _propertiesTabWidget->tabBar(),
			_mdiArea->findChild<QTabBar*>() };
		for (QTabBar* bar : findChildren<QTabBar*>())
		{
			if (!otherTabBars.contains(bar))
				bar->setIconSize(QSize(20, 20));
		}

		// Document tabs (_mdiArea's own TabbedView tab bar, not touched by
		// the icon-size loop above) default to expanding to fill the whole
		// tab bar width - with only one or two documents open that stretches
		// each tab across a large chunk of the window instead of sizing to
		// its label/icon. setExpanding(false) is Qt's documented switch for
		// this (QTabBar::expanding). setAutoHide(false) is explicit
		// insurance against QTabBar's own default of hiding itself entirely
		// with fewer than 2 tabs - this app always wants the strip visible,
		// even for a single open document.
		if (QTabBar* mdiTabBar = _mdiArea->findChild<QTabBar*>())
		{
			mdiTabBar->setExpanding(false);
			mdiTabBar->setAutoHide(false);
		}

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
	QMdiSubWindow* subWindow = createDocumentSubWindow(viewer);
	qDebug() << "MainWindow: created document via New -"
	         << "viewer=" << (void*)viewer << "subWindow=" << (void*)subWindow
	         << "title=" << viewer->windowTitle();
	viewer->showMaximized();
	// Matches presentDocumentFullscreen()'s eager activateDocument() call
	// (used by every other document-creation path - Open, the startup
	// document) instead of relying solely on QMdiArea's own
	// subWindowActivated signal to establish _activeDocument.
	activateDocument(viewer);
	//std::vector<int> mod = { 5 };
	//viewer->getViewportWidget()->setDisplayList(mod);
	viewer->updateDisplayList();
}

void MainWindow::presentDocumentFullscreen(ModelViewer* viewer)
{
	viewer->showMaximized();
	// The very first startup document can be visibly current before
	// QMdiArea has emitted any subWindowActivated signal that would
	// otherwise drive _activeDocument. Establish it eagerly here so
	// commands that depend on activeMdiChild() (for example Shift+Recent-
	// file import) target the already-visible document even before the
	// first user focus transition.
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
	if (QMdiSubWindow* existing = findMdiChild(fileName))
	{
		_mdiArea->setActiveSubWindow(existing);
		// Defensive, mirroring updateWindowMenu()'s equivalent call: keeps
		// _activeDocument in sync even if setActiveSubWindow() doesn't
		// re-emit subWindowActivated for an already-active subwindow.
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
	// destroyed-signal handler in createDocumentSubWindow() removes it from
	// _viewers; its QMdiSubWindow auto-deletes along with it.
	if (ModelViewer* viewer = activeMdiChild())
		viewer->close();
}

void MainWindow::closeAllSubWindows()
{
	// Snapshot first - closing each viewer mutates _viewers via the
	// destroyed-signal handler in createDocumentSubWindow().
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

void MainWindow::on_actionTile_Horizontally_triggered()
{
	_mdiArea->tileSubWindows();
	if (_mdiArea->subWindowList().isEmpty())
		return;

	QPoint position(0, 0);
	foreach(QMdiSubWindow * window, _mdiArea->subWindowList())
	{
		QRect rect(0, 0, _mdiArea->width() / _mdiArea->subWindowList().count(), _mdiArea->height());
		window->setGeometry(rect);
		window->move(position);
		position.setX(position.x() + window->width());
	}
}

void MainWindow::on_actionTile_Vertically_triggered()
{
	_mdiArea->tileSubWindows();
	if (_mdiArea->subWindowList().isEmpty())
		return;

	QPoint position(0, 0);
	foreach(QMdiSubWindow * window, _mdiArea->subWindowList())
	{
		QRect rect(0, 0, _mdiArea->width(), _mdiArea->height() / _mdiArea->subWindowList().count());
		window->setGeometry(rect);
		window->move(position);
		position.setY(position.y() + window->height());
	}
}

void MainWindow::on_actionTile_triggered()
{
	_mdiArea->tileSubWindows();
}

void MainWindow::on_actionCascade_triggered()
{
	_mdiArea->cascadeSubWindows();
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
	const int documentCount = _viewers.count();
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
	ui->actionTile->setEnabled(hasMdiChild);
	ui->actionTile_Horizontally->setEnabled(hasMdiChild);
	ui->actionTile_Vertically->setEnabled(hasMdiChild);
	ui->actionCascade->setEnabled(hasMdiChild);
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
	ui->menuWindows->addAction(ui->actionCascade);
	ui->menuWindows->addAction(ui->actionTile);
	ui->menuWindows->addAction(ui->actionTile_Horizontally);
	ui->menuWindows->addAction(ui->actionTile_Vertically);
	ui->menuWindows->addSeparator();
	ui->menuWindows->addAction(ui->actionNext);
	ui->menuWindows->addAction(ui->actionPrevious);

	if (!_viewers.isEmpty())
		ui->menuWindows->addSeparator();

	for (ModelViewer* child : std::as_const(_viewers))
	{
		auto* subWindow = qobject_cast<QMdiSubWindow*>(child->parentWidget());
		if (!subWindow)
		{
			// Diagnostic for the "tab present, document gone from the Window
			// menu" bug (2026-08-10) - _viewers still references this
			// ModelViewer, but its parentWidget() no longer resolves to a
			// QMdiSubWindow, so it's silently skipped here without ever
			// being removed from _viewers. Logging the actual parent type
			// pins down whether this is a real reparent/detach or something
			// else (e.g. a mid-destruction qobject_cast quirk).
			qWarning() << "MainWindow::updateWindowMenu - skipping tracked document with non-QMdiSubWindow parent:"
			           << "viewer=" << (void*)child << "title=" << child->windowTitle()
			           << "currentFile=" << child->currentFile()
			           << "parentWidget=" << (child->parentWidget() ? child->parentWidget()->metaObject()->className() : "null");
			continue;
		}

		const QString text = child->currentFile().isEmpty() ? child->windowTitle() : QFileInfo(child->currentFile()).fileName();
		QAction* action = ui->menuWindows->addAction(text, subWindow, [this, subWindow, child]() {
			_mdiArea->setActiveSubWindow(subWindow);
			// Defensive, mirroring openFile()'s equivalent call: keeps
			// _activeDocument in sync even if setActiveSubWindow() doesn't
			// re-emit subWindowActivated for an already-active subwindow.
			activateDocument(child);
			});
		action->setCheckable(true);
		action->setChecked(child == activeMdiChild());
	}
}

ModelViewer* MainWindow::activeMdiChild() const
{
	return _activeDocument;
}

QMdiSubWindow* MainWindow::findMdiChild(const QString& fileName) const
{
	for (QMdiSubWindow* window : _mdiArea->subWindowList())
	{
		if (auto* child = qobject_cast<ModelViewer*>(window->widget()))
			if (child->currentFile() == fileName)
				return window;
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

