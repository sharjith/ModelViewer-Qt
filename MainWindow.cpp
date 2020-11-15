#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ModelViewer.h"
#include "GLWidget.h"
#include <QtOpenGL>
#include <QProgressBar>
#include <QPushButton>

int MainWindow::_viewerCount = 1;
MainWindow* MainWindow::_mainWindow = nullptr;

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	ui = new Ui::MainWindow();
	ui->setupUi(this);

	_mainWindow = this;

	setAttribute(Qt::WA_DeleteOnClose);

	Q_INIT_RESOURCE(ModelViewer);

    _cancelTaskButton = new QPushButton("Cancel Loading", ui->statusBar);
	ui->statusBar->addPermanentWidget(_cancelTaskButton);
	_cancelTaskButton->hide();

    _progressBar = new QProgressBar(ui->statusBar);
    ui->statusBar->addPermanentWidget(_progressBar);
    _progressBar->hide();

	setCentralWidget((ui->mdiArea));
	ModelViewer* viewer = new ModelViewer(ui->mdiArea);
	viewer->setAttribute(Qt::WA_DeleteOnClose);
	_viewers.append(viewer);
	ui->mdiArea->addSubWindow(viewer);
	_bFirstTime = true;
}

MainWindow::~MainWindow()
{
	delete ui;
}

QPushButton* MainWindow::cancelTaskButton()
{
	return _cancelTaskButton;
}

void MainWindow::showStatusMessage(const QString& message)
{
    _mainWindow->statusBar()->showMessage(message);
    _mainWindow->statusBar()->update();
	qApp->processEvents();
}

void MainWindow::showProgressBar()
{
    _mainWindow->_progressBar->show();
    _mainWindow->_cancelTaskButton->show();
}

void MainWindow::hideProgressBar()
{
    _mainWindow->_progressBar->hide();
    _mainWindow->_cancelTaskButton->hide();
}

void MainWindow::setProgressValue(const int& value)
{
    if(value == 0)
        _mainWindow->_progressBar->reset();
    else
        _mainWindow->_progressBar->setValue(value);
    _mainWindow->_progressBar->update();
	qApp->processEvents();
}

void MainWindow::on_actionExit_triggered(bool /*checked*/)
{
	close();
	qApp->exit();
}

void MainWindow::on_actionAbout_triggered(bool /*checked*/)
{
	QMessageBox::about(this, "About 3D Model Viewer", "Application to visualize variour 3D Models like OBJ and StereoLithography models");
}

void MainWindow::on_actionAbout_Qt_triggered(bool /*checked*/)
{
	QMessageBox::aboutQt(this, "About Qt");
}

void MainWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	if (_bFirstTime)
	{
		//std::vector<int> mod = { 5 };
		//_viewers[0]->getGLView()->setDisplayList(mod);
		_viewers[0]->showMaximized();
		_viewers[0]->updateDisplayList();
		_bFirstTime = false;
	}
}

void MainWindow::closeEvent(QCloseEvent* /*event*/)
{
	ui->mdiArea->closeAllSubWindows();
	qApp->exit();
}

void MainWindow::on_actionNew_triggered()
{
	ModelViewer* viewer = new ModelViewer(nullptr);
	viewer->setAttribute(Qt::WA_DeleteOnClose);
	viewer->setWindowTitle(QString("Session %1").arg(++_viewerCount));
	_viewers.append(viewer);
	ui->mdiArea->addSubWindow(viewer);
	viewer->showMaximized();
    //std::vector<int> mod = { 5 };
    //viewer->getGLView()->setDisplayList(mod);
    viewer->updateDisplayList();
}

void MainWindow::on_actionTile_Horizontally_triggered()
{
    ui->mdiArea->tileSubWindows();
	QMdiArea* mdiArea = ui->mdiArea;
	if (mdiArea->subWindowList().isEmpty())
		return;

	QPoint position(0, 0);

	foreach(QMdiSubWindow * window, mdiArea->subWindowList())
	{
		QRect rect(0, 0, mdiArea->width() / mdiArea->subWindowList().count(), mdiArea->height());
		window->setGeometry(rect);
		window->move(position);
		position.setX(position.x() + window->width());
	}
}

void MainWindow::on_actionTile_Vertically_triggered()
{
    ui->mdiArea->tileSubWindows();
	QMdiArea* mdiArea = ui->mdiArea;
	if (mdiArea->subWindowList().isEmpty())
		return;

	QPoint position(0, 0);

	foreach(QMdiSubWindow * window, mdiArea->subWindowList())
	{
		QRect rect(0, 0, mdiArea->width(), mdiArea->height() / mdiArea->subWindowList().count());
		window->setGeometry(rect);
		window->move(position);
		position.setY(position.y() + window->height());
	}
}

void MainWindow::on_actionTile_triggered()
{
	ui->mdiArea->tileSubWindows();
}

void MainWindow::on_actionCascade_triggered()
{
	ui->mdiArea->cascadeSubWindows();
}

MainWindow* MainWindow::mainWindow()
{
	if (_mainWindow == nullptr)
		_mainWindow = new MainWindow();
	return _mainWindow;
}
