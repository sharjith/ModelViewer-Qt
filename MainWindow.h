#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QProgressBar;
class QPushButton;
class QMdiSubWindow;
class QAction;
#ifdef _WIN32
class QWinTaskbarProgress;
#endif //
QT_END_NAMESPACE

namespace Ui
{
	class MainWindow;
}

class ModelViewer;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	static MainWindow* mainWindow();
	~MainWindow();

	QPushButton* cancelTaskButton();

	ModelViewer* createMdiChild();

	void checkSaveAndClose(ModelViewer* viewer);

	void setGraphicsInfo(const QString& info)
	{
		_graphicsInfo = info;
	}

	QString graphicsInfo() const
	{
		return _graphicsInfo;
	}

	static void showStatusMessage(const QString& message, int timeout = 0);
	static void showProgressBar();
	static void hideProgressBar();
	static void setProgressValue(const int& value);

protected:
	MainWindow(QWidget* parent = Q_NULLPTR);
	void showEvent(QShowEvent* event);
	void closeEvent(QCloseEvent* event);
	void dragEnterEvent(QDragEnterEvent* event);
	void dropEvent(QDropEvent* event);

protected slots:
	void on_actionExit_triggered(bool checked = false);
	void on_actionAbout_triggered(bool checked = false);
	void on_actionAbout_Qt_triggered(bool checked = false);

private slots:
	void on_actionNew_triggered();
	void on_actionOpen_triggered();
	void on_actionImport_triggered();
	void on_actionTile_Horizontally_triggered();
	void on_actionTile_Vertically_triggered();
	void on_actionTile_triggered();
	void on_actionCascade_triggered();

	bool loadFile(const QString& fileName);
	void updateMenus();
	void updateRecentFileActions();
	void openRecentFile();
	void updateWindowMenu();
	ModelViewer* activeMdiChild() const;
	QMdiSubWindow* findMdiChild(const QString& fileName) const;

	void cancelFileLoading();

	void closeSubWindow();
	void closeAllSubWindows();

private:
	bool openFile(const QString& fileName);
	void readSettings();
	void writeSettings();
	static bool hasRecentFiles();
	void prependToRecentFiles(const QString& fileName);
	void setRecentFilesVisible(bool visible);

private:
	enum { MaxRecentFiles = 15 };

	Ui::MainWindow* ui;
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
};
