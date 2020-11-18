#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QProgressBar;
class QPushButton;
class QMdiSubWindow;
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

	static void showStatusMessage(const QString& message);
    static void showProgressBar();
    static void hideProgressBar();
    static void setProgressValue(const int& value);

protected:
	MainWindow(QWidget* parent = Q_NULLPTR);
	void showEvent(QShowEvent* event);
	void closeEvent(QCloseEvent* event);

protected slots:
	void on_actionExit_triggered(bool checked = false);
	void on_actionAbout_triggered(bool checked = false);
	void on_actionAbout_Qt_triggered(bool checked = false);

private slots:
	void on_actionNew_triggered();

	void on_actionTile_Horizontally_triggered();

	void on_actionTile_Vertically_triggered();

	void on_actionTile_triggered();

	void on_actionCascade_triggered();


	void updateMenus();
	void updateWindowMenu();
	ModelViewer* activeMdiChild() const;
	QMdiSubWindow* findMdiChild(const QString& fileName) const;

private:
	Ui::MainWindow* ui;
    QProgressBar* _progressBar;
#ifdef _WIN32
	QWinTaskbarProgress* _windowsTaskbarProgress;
#endif
	QPushButton* _cancelTaskButton;
	QList<ModelViewer*> _viewers;

    bool _bFirstTime;

	static int _viewerCount;
	static MainWindow* _mainWindow;
};
