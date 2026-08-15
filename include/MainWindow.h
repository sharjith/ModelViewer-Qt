#pragma once

#include <QMainWindow>
#include <QSettings>

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

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	static MainWindow* mainWindow();
	~MainWindow();

	void retranslateUI();

	QPushButton* cancelTaskButton();

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
	void updateRecentFileActions();
    void removeFromRecentFiles(const QString& fileName);
	void openRecentFile();
	void updateWindowMenu();

	void cancelFileLoading();

	void closeSubWindow();
	void closeAllSubWindows();

private:	
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
	// QDockWidgets, tabified together in QMainWindow's own right-side dock
	// area - no custom splitter needed, QMainWindow's native dock system
	// already reserves/resizes that area and persists it via
	// saveState()/restoreState().
	QMdiArea* _mdiArea = nullptr;
	QDockWidget* _propertiesDock = nullptr;
	QDockWidget* _environmentDock = nullptr;
	QDockWidget* _documentDock = nullptr;
	QTabWidget* _propertiesTabWidget = nullptr;
	QTabWidget* _documentTabWidget = nullptr;
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
	// These five are per-document sources (a specific SceneGraph/
	// ViewportWidget/ModelViewer), unlike the panel->viewport forwards
	// below, which are connected once and dispatch through activeMdiChild()
	// instead - an incoming signal has to be wired to the actual emitting
	// object, so these get disconnected from the old document and
	// reconnected to the new one on every rebind instead.
	QMetaObject::Connection _variantDataChangedConnection;
	QMetaObject::Connection _animationDataChangedConnection;
	QMetaObject::Connection _gltfCameraDataChangedConnection;
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
