#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QMap>

QT_BEGIN_NAMESPACE
class QProgressBar;
class QPushButton;
class QAction;
class QTabWidget;
class QCheckBox;
class QLabel;
class QMdiArea;
class QMdiSubWindow;
class QDockWidget;
class QSplitter;

#ifdef _WIN32
class QWinTaskbarProgress;
#endif //
QT_END_NAMESPACE

namespace Ui
{
	class MainWindow;
}

class ModelViewer;
class QuickHelpDialog;
class MaterialPropertiesPanel;
class ObjectTransformPanel;
class VisualizationEnvironmentPanel;
class MaterialVariantsPanel;
class AnimationsPanel;
class CamerasPanel;
class SelectionSetsPanel;
class SceneStatesPanel;
class SimulationPanel;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	static MainWindow* mainWindow();
	~MainWindow();

	void retranslateUI();

	QPushButton* cancelTaskButton();

	// True when `viewer` is the currently active document - a narrow public
	// wrapper around the private _activeDocument tracker (activeMdiChild()
	// itself stays private), for shared, MainWindow-owned panels like
	// MaterialPropertiesPanel that need to know "is this specific document's
	// state the one I should currently be reflecting" without a full
	// document-switch event (e.g. ModelViewer's own eyedropperArmedChanged
	// forwarding - see MaterialPropertiesPanel::setEyedropperChecked()).
	bool isActiveDocument(ModelViewer* viewer) const { return viewer && viewer == _activeDocument; }

	ModelViewer* createMdiChild();

	// Maximizes viewer's QMdiSubWindow and makes it the active one. Called
	// for the very first document (before the window is even shown) and by
	// on_actionNew_triggered()'s equivalent inline logic - see
	// createDocumentSubWindow().
	void presentDocumentFullscreen(ModelViewer* viewer);

	MaterialPropertiesPanel* materialPropertiesPanel() const { return _materialPropertiesPanel; }
	ObjectTransformPanel* objectTransformPanel() const { return _objectTransformPanel; }
	VisualizationEnvironmentPanel* visualizationEnvironmentPanel() const { return _visualizationEnvironmentPanel; }
	QTabWidget* propertiesTabWidget() const { return _propertiesTabWidget; }
	MaterialVariantsPanel* materialVariantsPanel() const { return _materialVariantsPanel; }
	AnimationsPanel* animationsPanel() const { return _animationsPanel; }
	CamerasPanel* camerasPanel() const { return _camerasPanel; }

	// Raise the shared Properties/Environment docks and, for Properties,
	// select the requested inner tab - replaces ModelViewer's old direct
	// controlstabWidget/tabWidgetVizAttribs page switches now that those
	// widgets are MainWindow-owned singletons instead of per-document ones.
	void showMaterialsPropertiesPage();
	void showTransformationsPropertiesPage();
	void showEnvironmentDockPage();

	// Rebinds the shared Properties/Environment panels to the given document
	// (or clears/disables them if nullptr). Called from
	// handleActiveDocumentChanged().
	void rebindSharedPanelsTo(ModelViewer* viewer);

	void setGraphicsInfo(const QString& info)
	{
		_graphicsInfo = info;
	}

	QString graphicsInfo() const
	{
		return _graphicsInfo;
	}

	bool openFile(const QString& fileName);

	static void showStatusMessage(const QString& message, int timeout = 0);
	static void showProgressBar(const bool showCancelButton = true);
	static void showIndeterminateProgressBar();
	static void resetProgressBar();
	static void hideProgressBar();
	static void setProgressValue(const int& value);
	static void setCancelButtonEnabled(bool enabled);
	static void setCancelButtonText(const QString& text);
	static void requestFileLoadCancel();
	static void clearFileLoadCancel();
	static bool isFileLoadCancelRequested();

	// Disables the menu bar, MDI area, and dock panels for the duration of a
	// cancellable background load (status bar/Cancel button deliberately left
	// alone) - see ViewportWidget::loadAssImpModel()'s doc comment for why
	// this exists: the progressive-loading yield point needs to let user-
	// input events through so the Cancel button is actually clickable, and
	// doing that safely means every OTHER interactive control has to be
	// inert first (same shape as RtRenderDialog::onRenderClicked()'s
	// pushButtonStop handling).
	static void setLoadingUiLocked(bool locked);

	static inline QString recentFilesKey() { return QStringLiteral("recentFileList"); }
	static inline QString fileKey() { return QStringLiteral("file"); }
	static QStringList readRecentFiles(QSettings& settings);
	static void writeRecentFiles(const QStringList& files, QSettings& settings);

protected:
	MainWindow(QWidget* parent = Q_NULLPTR);
	void showEvent(QShowEvent* event);
	void closeEvent(QCloseEvent* event);
	void dragEnterEvent(QDragEnterEvent* event);
	void dropEvent(QDropEvent* event);

protected slots:
	void on_actionExit_triggered(bool checked = false);
	void on_actionQuick_Help_triggered();
	void on_actionTutorial_triggered();
	void on_actionView_Logs_triggered();
	void on_actionOpen_Logs_Folder_triggered();
	void on_actionShow_Console_triggered();
	void on_actionAbout_triggered(bool checked = false);

private slots:
	void on_actionNew_triggered();
	void on_actionOpen_triggered();
	void on_actionImport_triggered();
	void on_actionExport_triggered();
	void on_actionSave_triggered();
	void on_actionSave_As_triggered();
	void on_actionSettings_triggered();
	void on_actionTile_Horizontally_triggered();
	void on_actionTile_Vertically_triggered();
	void on_actionTile_triggered();
	void on_actionCascade_triggered();

	bool loadFile(const QString& fileName);
	void updateMenus();
    void setupViewMenus();
    void updateCornerIcons();   // View > Axonometric Views corner icons (letters follow the UI language)
    void updateViewMenus();
	void updateRecentFileActions();
    void removeFromRecentFiles(const QString& fileName);
	void openRecentFile();
	void updateWindowMenu();

	void cancelFileLoading();

	void closeSubWindow();
	void closeAllSubWindows();

private:	
    QMap<QString, QAction*> _viewActions;
	void readSettings();
	void writeSettings();
	static bool hasRecentFiles();
	void prependToRecentFiles(const QString& fileName);
	void setRecentFilesVisible(bool visible);
    ModelViewer* activeMdiChild() const;
    QMdiSubWindow* findMdiChild(const QString& fileName) const;
	bool canExit();

	// Wraps a newly constructed ModelViewer in a QMdiSubWindow and adds it
	// to _mdiArea. Used by both createMdiChild() and on_actionNew_triggered().
	QMdiSubWindow* createDocumentSubWindow(ModelViewer* viewer);

private:
	enum { MaxRecentFiles = 15 };

	Ui::MainWindow* ui;
	// Documents live in _mdiArea (native QMdiArea - tiling/cascading/
	// restoring, most-recently-used Next/Previous, all built in). The
	// tool-panel column (Document/Properties/Environment) is three plain
	// QDockWidgets, tabified together - but hosted in a NESTED QMainWindow
	// (_rightPanelWindow) rather than in this outer one, specifically so the
	// panel column as a WHOLE sits inside a real QSplitter pane
	// (_rightPanelSplitter) alongside _mdiArea: QMainWindow's own dock-area
	// resize has no equivalent of QSplitter::setChildrenCollapsible() (drag
	// past a pane's minimum size and it snaps to width 0) - there is no way
	// to make a plain top-level dock area collapse fully by dragging its
	// separator, only ever down to its content's minimum width. Docks added
	// to a nested QMainWindow keep every native behavior (floating,
	// undocking, tabbing, per-dock toggleViewAction()) exactly as if they
	// were on the outer one - QSplitter does not care what a pane contains,
	// so the OUTER splitter's handle is what actually gets dragged, and it
	// collapses the WHOLE nested window (all three docks) the same way
	// _documentTabSplitter already collapses-or-not below (this one leaves
	// setChildrenCollapsible() at its default true, deliberately, unlike
	// that one). See the constructor for the setWindowFlags(Qt::Widget)
	// this needs to embed properly instead of trying to be a second
	// top-level window, and readSettings()/writeSettings() for its own
	// saveState()/restoreState() (separate from this outer window's) plus
	// _rightPanelSplitter's own persisted sizes.
	QMdiArea* _mdiArea = nullptr;
	QMainWindow* _rightPanelWindow = nullptr;
	QSplitter* _rightPanelSplitter = nullptr;
	QDockWidget* _propertiesDock = nullptr;
	QDockWidget* _environmentDock = nullptr;
	QDockWidget* _documentDock = nullptr;
	QTabWidget* _propertiesTabWidget = nullptr;
	QTabWidget* _documentTabWidget = nullptr;
	// Second, independently-tabbed group stacked below _documentTabWidget in
	// _documentTabSplitter, inside the same "Document" dock - saved
	// configurations (Selections/States) rather than live document content
	// (Variants/Animations/Cameras, above). See their construction site in
	// the constructor for why this is a second QTabWidget instead of two
	// more tabs on _documentTabWidget.
	QTabWidget* _documentSecondaryTabWidget = nullptr;
	// Vertical splitter holding _documentTabWidget/_documentSecondaryTabWidget
	// - its sizes are persisted separately from QMainWindow's own dock/
	// toolbar layout (saveState()/restoreState() only covers QDockWidget
	// geometry, not an arbitrary child splitter's handle position), see
	// readSettings()/writeSettings().
	QSplitter* _documentTabSplitter = nullptr;
	// Above _documentTabWidget's Variants/Animations/Cameras tabs - moved
	// here from the per-document nav overlay (design change: single shared
	// instances rebound to whichever document is active, like the other
	// Materials/Environment/etc. panels, instead of one pair per document).
	// Rebound in rebindSharedPanelsTo(): reflects the active document's
	// current state on every switch and dispatches toggles to it.
	QCheckBox* _checkBoxAutoFitView = nullptr;
	QCheckBox* _checkBoxSelectionHighlight = nullptr;

	// Set at the top of ~MainWindow(), before _mdiArea/_viewers and their
	// child widgets get torn down by Qt's cascading parent-child
	// destruction - see the comment in createDocumentSubWindow()'s
	// destroyed-signal handler.
	bool _shuttingDown = false;
	MaterialPropertiesPanel* _materialPropertiesPanel = nullptr;
	ObjectTransformPanel* _objectTransformPanel = nullptr;
	VisualizationEnvironmentPanel* _visualizationEnvironmentPanel = nullptr;
	MaterialVariantsPanel* _materialVariantsPanel = nullptr;
	AnimationsPanel* _animationsPanel = nullptr;
	CamerasPanel* _camerasPanel = nullptr;
	SelectionSetsPanel* _selectionSetsPanel = nullptr;
	SceneStatesPanel* _sceneStatesPanel = nullptr;
	// Third tab of the bottom document-dock group (with Selections/States). Shared like the other panels; fed the
	// active document's active simulation result through refreshSimulationPanel().
	SimulationPanel* _simulationPanel = nullptr;
	QMetaObject::Connection _simulationSessionConnection;
	void refreshSimulationPanel(ModelViewer* viewer);
	ModelViewer* _lastBoundModelViewer = nullptr;
	// Guards rebindSharedPanelsTo(nullptr) against running its teardown body
	// more than once per "went from having an active document to having
	// none" transition - QMdiArea's own subWindowActivated(nullptr), the
	// synchronous destroyed() lambda, and its deferred singleShot follow-up
	// can all independently reach rebindSharedPanelsTo(nullptr) for the same
	// close, and activateDocument()'s reentrancy guard doesn't catch a null
	// child. Without this, repeated invocations re-run disconnect()/refresh()
	// against the same panels, interleaved with unrelated event-loop churn
	// (window activation, tab clicks) - observed as an intermittent crash.
	bool _sharedPanelsBound = false;
	// Which document activeMdiChild() reports - kept up to date by
	// handleActiveDocumentChanged(), connected to _mdiArea's single
	// subWindowActivated() signal.
	ModelViewer* _activeDocument = nullptr;
	QMetaObject::Connection _environmentPanelDisplayModeConnection;
	QMetaObject::Connection _materialPreviewRenderingModeConnection;
	// Per-ViewportWidget, like the two above - reconnected to whichever
	// document is newly active on every rebindSharedPanelsTo() so the shared
	// MaterialPropertiesPanel's eyeDropper button reflects THAT document's
	// eyedropper state (including external disarms - another tool taking
	// over, etc.), not whichever document was active before.
	QMetaObject::Connection _materialPropertiesEyedropperConnection;
	// These five are per-document sources (a specific SceneGraph/
	// ViewportWidget/ModelViewer), unlike the panel->viewport forwards
	// below, which are connected once and dispatch through activeMdiChild()
	// instead - an incoming signal has to be wired to the actual emitting
	// object, so these get disconnected from the old document and
	// reconnected to the new one on every rebind instead.
	QMetaObject::Connection _variantDataChangedConnection;
	QMetaObject::Connection _animationDataChangedConnection;
	QMetaObject::Connection _gltfCameraDataChangedConnection;
	QMetaObject::Connection _selectionSetsChangedConnection;
	// Per-ViewportWidget, like _materialPropertiesEyedropperConnection above -
	// keeps actionSaveSelectionSet's enabled state and the Selections
	// panel's active-row highlight live as the active document's own
	// selection changes, not just at document-lifecycle points.
	QMetaObject::Connection _selectionSetsSyncConnection;
	// Per-SceneGraph, mirrors _selectionSetsChangedConnection - no
	// equivalent of _selectionSetsSyncConnection needed since SceneStatesPanel
	// has no active-row highlight to keep live (see its own doc comment).
	QMetaObject::Connection _sceneStatesChangedConnection;
	// Per-SceneGraph (structureChanged fires on import/delete-all) - keeps
	// actionFilterByMaterial/actionFilterByColor enabled only while the
	// active document actually has meshes loaded, live, not just at
	// document-lifecycle points.
	QMetaObject::Connection _hasMeshesSyncConnection;
	QMetaObject::Connection _structureChangedForVariantsConnection;
	QMetaObject::Connection _animationStateChangedConnection;

	// Dims (or restores) a Document dock tab's label to signal whether the
	// active document currently has any data for it - see the tab-styling
	// decision from planning: all three tabs stay visible always, empty ones
	// just read as visually de-emphasized rather than disappearing.
	void setDocumentTabDimmed(int tabIndex, bool dimmed);
	void refreshDocumentDockTabStyling(ModelViewer* viewer);

	// Connected to _mdiArea's subWindowActivated(QMdiSubWindow*) signal -
	// drives rebindSharedPanelsTo() and the undo-stack/menu bookkeeping via
	// activateDocument() below.
	void handleActiveDocumentChanged(QMdiSubWindow* subWindow);

	// Common body of handleActiveDocumentChanged(), also called directly by
	// presentDocumentFullscreen()/openFile()/updateWindowMenu() as a
	// defensive safety net for cases where subWindowActivated() may not
	// fire (e.g. activating an already-active subwindow, or the very first
	// document before the window is shown).
	void activateDocument(ModelViewer* child);
	QProgressBar* _progressBar;
#ifdef _WIN32
	QWinTaskbarProgress* _windowsTaskbarProgress;
#endif
	QPushButton* _cancelTaskButton;
	QList<ModelViewer*> _viewers;

	QAction* recentFileActs[MaxRecentFiles];
	QAction* recentFileSeparator;
	QAction* recentFileSubMenuAct;

	bool _bFirstTime;

	QString _graphicsInfo;

	static int _viewerCount;
	static MainWindow* _mainWindow;
	static bool _fileLoadCancelRequested;

	static QuickHelpDialog* _helpDialog;
};
