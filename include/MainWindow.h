#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QHash>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QProgressBar;
class QPushButton;
class QAction;
class QTabWidget;
class QCheckBox;
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

namespace ads
{
	class CDockManager;
	class CDockWidget;
	class CDockAreaWidget;
}

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	static MainWindow* mainWindow();
	~MainWindow();

	void retranslateUI();

	QPushButton* cancelTaskButton();

	ModelViewer* createMdiChild();

	// Makes viewer's document dock widget the current tab in the document
	// dock area. Documents are native ads::CDockWidgets (see
	// createDocumentDock()), not QMdiSubWindows, so this is just
	// CDockWidget::setAsCurrentTab() - no geometry/maximize-state handling
	// needed at all.
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
	// Mirrors QMdiSubWindow's own internal behavior: watches each document's
	// WindowTitleChange events (fired by ModelViewer::setWindowTitle(), e.g.
	// to append/remove the unsaved-changes "*") and mirrors them onto that
	// document's CDockWidget tab label, which does not track this itself.
	bool eventFilter(QObject* watched, QEvent* event) override;

protected slots:
	void on_actionExit_triggered(bool checked = false);
	void on_actionQuick_Help_triggered();
	void on_actionTutorial_triggered();
	void on_actionView_Logs_triggered();
	void on_actionOpen_Logs_Folder_triggered();
	void on_actionAbout_triggered(bool checked = false);
	void on_actionAbout_Qt_triggered(bool checked = false);

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
    ads::CDockWidget* findDocumentDock(const QString& fileName) const;
	bool canExit();

	// Wraps a newly constructed ModelViewer in its own CDockWidget and adds
	// it to the shared document dock area (creating that area, positioned to
	// the left of the Document/Properties/Environment tool docks, the first
	// time this is called). Used by both createMdiChild() and
	// on_actionNew_triggered().
	ads::CDockWidget* createDocumentDock(ModelViewer* viewer);

	// Next/Previous (Ctrl+Tab family): cycles among all REAL documents,
	// regardless of which ADS area currently hosts them. The original
	// _documentDockArea can stop containing some documents after the user
	// splits tabs into multiple areas, so walking only that area's tab order
	// is incomplete.
	void cycleActiveDocument(int direction);

private:
	enum { MaxRecentFiles = 15 };

	Ui::MainWindow* ui;
	// Document area (left/top) and tool-panel column (right/bottom, Document/
	// Properties/Environment) each get their own independent CDockManager,
	// hosted as the two children of _dockSplitter (owned by MainWindow, not
	// Qt-ADS) instead of sharing one CDockManager's internal splitter tree -
	// see the constructor for why (Qt-ADS's own relayout could otherwise
	// perturb the left/right ratio as a side effect of an operation entirely
	// within one side).
	QSplitter* _dockSplitter = nullptr;
	ads::CDockManager* _documentDockManager = nullptr;
	ads::CDockManager* _toolPanelDockManager = nullptr;
	QAction* _actionSplitterOrientation = nullptr;
	// Set by readSettings() when it successfully restores a persisted
	// _dockSplitter size - tells createDocumentDock()'s first-document
	// bootstrap to skip its own hardcoded 75/25 default instead of
	// clobbering the restored value.
	bool _dockSplitterSizesRestored = false;
	ads::CDockWidget* _propertiesDock = nullptr;
	ads::CDockWidget* _environmentDock = nullptr;
	ads::CDockWidget* _documentDock = nullptr;
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

	// Documents: one CDockWidget per open ModelViewer, initially all tabbed
	// together in _documentDockArea - but a user can drag a document tab to
	// split it off into its own CDockAreaWidget, so there can be MORE than
	// one document area alive at once (only _documentDockArea's own tab
	// group is tracked directly for currentChanged()/activeMdiChild(), see
	// createDocumentDock()). QPointer, not a raw pointer: if the user closes
	// every document in _documentDockArea's own area while OTHER documents
	// remain open elsewhere, Qt-ADS destroys that now-empty area on its own
	// - _documentDocks isn't empty in that case, so the plain
	// "_documentDocks.isEmpty() -> reset to nullptr" check the destroyed-
	// signal handler already does for the all-closed case does not catch
	// this, and a raw pointer would dangle straight into the next
	// updateMenus()/activeMdiChild() call.
	QPointer<ads::CDockAreaWidget> _documentDockArea;
	QHash<ModelViewer*, ads::CDockWidget*> _documentDocks;
	// Permanent, always-registered member of _documentDockArea (created
	// once in the constructor, never closed) so that area is never truly
	// empty and Qt-ADS never tears it down - without this, closing the last
	// document left the tool-panel column expanding to fill the whole
	// window instead of a distinct, empty document region remaining on the
	// left. NoTab, so it never shows a tab of its own alongside real
	// documents; made current again whenever the last real document closes
	// (see createDocumentDock()'s destroyed-signal handler).
	ads::CDockWidget* _documentPlaceholderDock = nullptr;
	// Set at the top of ~MainWindow(), before _documentDockManager/
	// _toolPanelDockManager and their dock widgets get torn down by Qt's
	// cascading parent-child destruction - see the comment in
	// createDocumentDock()'s destroyed-signal handler.
	bool _shuttingDown = false;
	MaterialPropertiesPanel* _materialPropertiesPanel = nullptr;
	ObjectTransformPanel* _objectTransformPanel = nullptr;
	VisualizationEnvironmentPanel* _visualizationEnvironmentPanel = nullptr;
	MaterialVariantsPanel* _materialVariantsPanel = nullptr;
	AnimationsPanel* _animationsPanel = nullptr;
	CamerasPanel* _camerasPanel = nullptr;
	ModelViewer* _lastBoundModelViewer = nullptr;
	// Which document activeMdiChild() reports - kept up to date by
	// handleActiveDocumentChanged(), which is connected to the
	// currentChanged() signal of EVERY document-hosting CDockAreaWidget
	// (there can be more than one - see createDocumentDock()'s
	// dockAreaCreated handler - a user splitting a document tab off creates
	// a new area outside of any code path this class controls), not just
	// the original _documentDockArea.
	ModelViewer* _activeDocument = nullptr;
	QMetaObject::Connection _environmentPanelDisplayModeConnection;
	QMetaObject::Connection _materialPreviewRenderingModeConnection;
	// These four are per-document sources (a specific SceneGraph/
	// ViewportWidget), unlike the panel->viewport forwards below, which are
	// connected once and dispatch through activeMdiChild() instead - an
	// incoming signal has to be wired to the actual emitting object, so
	// these get disconnected from the old document and reconnected to the
	// new one on every rebind instead.
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

	// Connected to _documentDockArea's currentChanged(int) signal once, when
	// that area is first created - drives rebindSharedPanelsTo() and the
	// undo-stack/menu bookkeeping that used to live in the QMdiArea::
	// subWindowActivated handler.
	void handleActiveDocumentChanged();

	// Common body of handleActiveDocumentChanged(), also driven directly by
	// _documentDockManager's focusedDockWidgetChanged() signal - currentChanged(int)
	// on a document's CDockAreaWidget only fires when that area's OWN tab
	// index changes, which never happens when the user activates a document
	// by clicking into a different, already-current, single-tab area (e.g.
	// two documents split side by side) - focusedDockWidgetChanged() is the
	// only Qt-ADS signal that reflects "the user just interacted with a
	// different dock widget" regardless of tab index.
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
