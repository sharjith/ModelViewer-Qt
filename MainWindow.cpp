#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ModelViewer.h"
#include "GLWidget.h"
#include <QtOpenGL>
#include <QProgressBar>
#include <QPushButton>
#include <QMdiSubWindow>

#ifdef _WIN32
#include <QWinTaskbarProgress>
#include <QWinTaskbarButton>
#endif

int MainWindow::_viewerCount = 1;
MainWindow* MainWindow::_mainWindow = nullptr;

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	ui = new Ui::MainWindow();
	ui->setupUi(this);

	_mainWindow = this;

	connect(ui->mdiArea, &QMdiArea::subWindowActivated,
		this, &MainWindow::updateMenus);
	connect(ui->menuWindows, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

	QAction* closeAct = ui->actionClose;
	closeAct->setStatusTip(tr("Close the active window"));
	connect(closeAct, &QAction::triggered,
		ui->mdiArea, &QMdiArea::closeActiveSubWindow);

	QAction* closeAllAct = ui->actionClose_All;
	closeAllAct->setStatusTip(tr("Close all the windows"));
	connect(closeAllAct, &QAction::triggered, ui->mdiArea, &QMdiArea::closeAllSubWindows);

	QAction* nextAct = ui->actionNext;
	nextAct->setShortcuts(QKeySequence::NextChild);
	nextAct->setStatusTip(tr("Move the focus to the next window"));
	connect(nextAct, &QAction::triggered, ui->mdiArea, &QMdiArea::activateNextSubWindow);

	QAction* previousAct = ui->actionPrevious;
	previousAct->setShortcuts(QKeySequence::PreviousChild);
	previousAct->setStatusTip(tr("Move the focus to the previous "
		"window"));
	connect(previousAct, &QAction::triggered, ui->mdiArea, &QMdiArea::activatePreviousSubWindow);

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
    viewer->setLastOpenedDir(QApplication::applicationDirPath());
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
#ifdef _WIN32
	_mainWindow->_windowsTaskbarProgress->show();
#endif // _WIN32
    _mainWindow->_cancelTaskButton->show();
}

void MainWindow::hideProgressBar()
{
    _mainWindow->_progressBar->hide();
#ifdef _WIN32
	_mainWindow->_windowsTaskbarProgress->hide();
#endif // _WIN32
    _mainWindow->_cancelTaskButton->hide();
}

void MainWindow::setProgressValue(const int& value)
{
	if (value == 0)
	{
		_mainWindow->_progressBar->reset();
#ifdef _WIN32
		_mainWindow->_windowsTaskbarProgress->reset();
#endif // _WIN32
	}
	else
	{
		_mainWindow->_progressBar->setValue(value);
#ifdef _WIN32
		_mainWindow->_windowsTaskbarProgress->setValue(value);
#endif // _WIN32
	}
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

#ifdef _WIN32
	QWinTaskbarButton* windowsTaskbarButton = new QWinTaskbarButton(this);    //Create the taskbar button which will show the progress
	windowsTaskbarButton->setWindow(windowHandle());    //Associate the taskbar button to the progress bar, assuming that the progress bar is its own window
	_windowsTaskbarProgress = windowsTaskbarButton->progress();		
#endif

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

void MainWindow::updateMenus()
{
	bool hasMdiChild = (activeMdiChild() != nullptr);
	//saveAct->setEnabled(hasMdiChild);
	//saveAsAct->setEnabled(hasMdiChild);
#ifndef QT_NO_CLIPBOARD
	//pasteAct->setEnabled(hasMdiChild);
#endif
	ui->actionClose->setEnabled(hasMdiChild);
	ui->actionClose_All->setEnabled(hasMdiChild);
	ui->actionTile->setEnabled(hasMdiChild);
	ui->actionTile_Horizontally->setEnabled(hasMdiChild);
	ui->actionTile_Vertically->setEnabled(hasMdiChild);
	ui->actionCascade->setEnabled(hasMdiChild);
	ui->actionNext->setEnabled(hasMdiChild);
	ui->actionPrevious->setEnabled(hasMdiChild);	

#ifndef QT_NO_CLIPBOARD
	//bool hasSelection = (activeMdiChild() && activeMdiChild()->textCursor().hasSelection());
	//cutAct->setEnabled(hasSelection);
	//copyAct->setEnabled(hasSelection);
#endif
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

	QList<QMdiSubWindow*> windows = ui->mdiArea->subWindowList();
	if(!windows.isEmpty())
		ui->menuWindows->addSeparator();

	for (int i = 0; i < windows.size(); ++i) {
		QMdiSubWindow* mdiSubWindow = windows.at(i);
		ModelViewer* child = qobject_cast<ModelViewer*>(mdiSubWindow->widget());

		QString text;
		if (i < 9) {
			text = child->windowTitle();
		}
		else {
			text = child->windowTitle();
		}
		QAction* action = ui->menuWindows->addAction(text, mdiSubWindow, [this, mdiSubWindow]() {
			ui->mdiArea->setActiveSubWindow(mdiSubWindow);
			});
		action->setCheckable(true);
		action->setChecked(child == activeMdiChild());
	}
}

ModelViewer* MainWindow::activeMdiChild() const
{
	if (QMdiSubWindow* activeSubWindow = ui->mdiArea->activeSubWindow())
		return qobject_cast<ModelViewer*>(activeSubWindow->widget());
	return nullptr;
}

QMdiSubWindow* MainWindow::findMdiChild(const QString& fileName) const
{
	QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();

	const QList<QMdiSubWindow*> subWindows = ui->mdiArea->subWindowList();
	for (QMdiSubWindow* window : subWindows) {
		ModelViewer* mdiChild = qobject_cast<ModelViewer*>(window->widget());
		if (mdiChild->windowTitle() == canonicalFilePath)
			return window;
	}
	return nullptr;
}
