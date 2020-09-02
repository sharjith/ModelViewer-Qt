#pragma once

#include <QMainWindow>

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

private:
	Ui::MainWindow* ui;
	QList<ModelViewer*> _viewers;

	bool _bFirstTime;

	static int _viewerCount;
    static MainWindow* _mainWindow;
};
