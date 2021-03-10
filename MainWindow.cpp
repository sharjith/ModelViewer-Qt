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

	connect(ui->mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::updateMenus);
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
	connect(nextAct, &QAction::triggered, ui->mdiArea, &QMdiArea::activateNextSubWindow);

	QAction* previousAct = ui->actionPrevious;
	previousAct->setShortcuts(QKeySequence::PreviousChild);
	previousAct->setStatusTip(tr("Move the focus to the previous "
		"window"));
	connect(previousAct, &QAction::triggered, ui->mdiArea, &QMdiArea::activatePreviousSubWindow);

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
	setCentralWidget((ui->mdiArea));

	_bFirstTime = true;
}

ModelViewer* MainWindow::createMdiChild()
{
	ModelViewer* viewer = new ModelViewer(ui->mdiArea);
	viewer->setLastOpenedDir(QApplication::applicationDirPath());
	viewer->setAttribute(Qt::WA_DeleteOnClose);
	_viewers.append(viewer);
	ui->mdiArea->addSubWindow(viewer);
	return viewer;
}

void MainWindow::checkSaveAndClose(ModelViewer* viewer)
{
	QMessageBox::StandardButton button =
		QMessageBox::question(this, "Document modified", "Do you want to save?", QMessageBox::StandardButtons(QMessageBox::Yes |
			QMessageBox::No | QMessageBox::Cancel));
	if (button == QMessageBox::Yes)
	{
		viewer->save();
		viewer->parentWidget()->close();
	}
	else if (button == QMessageBox::No)
	{
		viewer->parentWidget()->close();
	}
}

MainWindow::~MainWindow()
{
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
}

void MainWindow::writeSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	settings.setValue("geometry", saveGeometry());
}

static inline QString recentFilesKey() { return QStringLiteral("recentFileList"); }
static inline QString fileKey() { return QStringLiteral("file"); }

static QStringList readRecentFiles(QSettings& settings)
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

static void writeRecentFiles(const QStringList& files, QSettings& settings)
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
	_mainWindow->statusBar()->showMessage(message, timeout);
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
	QMessageBox::about(this, 
		"About 3D Model Viewer", 
		QString::fromWCharArray(L"Application to visualize various 3D Models like OBJ and StereoLithography models using the ASSIMP library\n\n"
		"Copyright \u00A9 2021 Sharjith Naramparambath - sharjith@gmail.com\n\n")
		+ graphicsInfo());
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

void MainWindow::closeEvent(QCloseEvent* event)
{
	closeAllSubWindows();
	if (ui->mdiArea->currentSubWindow())
	{
		event->ignore();
	}
	else
	{
		writeSettings();
		event->accept();
	}
	qApp->exit();
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
	QString supportedExtensions = ModelViewer::getSupportedExtensions();
	QApplication::setOverrideCursor(Qt::WaitCursor);
	foreach(const QUrl & url, event->mimeData()->urls())
	{
		QString fileName = url.toLocalFile();	
		ModelViewer::setLastOpenedDir(QFileInfo(fileName).path()); // store path for next time
		QFileInfo fi(fileName);
		QString extn = fi.suffix();
		if (!supportedExtensions.contains(extn, Qt::CaseInsensitive))
		{
			QMessageBox::critical(this, "Error", url.toString() + "\nUnsupported file format: " + extn);
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
	_viewers.append(viewer);
	ui->mdiArea->addSubWindow(viewer);
	viewer->showMaximized();
	//std::vector<int> mod = { 5 };
	//viewer->getGLView()->setDisplayList(mod);
	viewer->updateDisplayList();
}

void MainWindow::on_actionOpen_triggered()
{
	QFileDialog fileDialog(this, tr("Open Model File"), ModelViewer::getLastOpenedDir(), ModelViewer::getSupportedExtensions());
	fileDialog.setFileMode(QFileDialog::ExistingFile);
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
		ui->mdiArea->setActiveSubWindow(existing);
		return true;
	}
	const bool succeeded = loadFile(fileName);	
		
	return succeeded;
}

void MainWindow::cancelFileLoading()
{
	if (activeMdiChild())
	{
		GLWidget* view = activeMdiChild()->getGLView();
		if (view)
			view->cancelAssImpModelLoading();
	}
}

void MainWindow::closeSubWindow()
{
	ModelViewer* viewer = activeMdiChild();
	if (viewer->documentModified())
	{
		checkSaveAndClose(viewer);
	}
	else
		viewer->parentWidget()->close();
}

void MainWindow::closeAllSubWindows()
{
	QList<QMdiSubWindow*> subWindows = ui->mdiArea->subWindowList();
	for (QMdiSubWindow* sub : subWindows)
	{
		ModelViewer* viewer = dynamic_cast<ModelViewer*>(sub->widget());
		if (viewer)
		{
			if (viewer->documentModified())
			{
				checkSaveAndClose(viewer);
			}
			else
				viewer->parentWidget()->close();
		}
	}
}

bool MainWindow::loadFile(const QString& fileName)
{
	ModelViewer* child = createMdiChild();
	child->show();
	const bool succeeded = child->loadFile(fileName);
	if (!succeeded)
		child->parentWidget()->close();
	else
	{
		child->setWindowTitle(QFileInfo(fileName).fileName());
		MainWindow::prependToRecentFiles(fileName);
	}
	return succeeded;
}

void MainWindow::on_actionImport_triggered()
{
	if (activeMdiChild())
		activeMdiChild()->import();
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
	ui->actionSave->setVisible(hasMdiChild);
	ui->actionSave_As->setVisible(hasMdiChild);
	if (hasMdiChild)
	{
		ui->actionSave->setEnabled(activeMdiChild()->documentModified());
		ui->actionSave_As->setEnabled(activeMdiChild()->documentModified());
	}
#ifndef QT_NO_CLIPBOARD
	//pasteAct->setEnabled(hasMdiChild);
#endif
	ui->actionImport->setVisible(hasMdiChild);
	ui->actionClose->setEnabled(hasMdiChild);
	ui->actionFileClose->setVisible(hasMdiChild);
	ui->actionClose_All->setVisible(hasMdiChild && ui->mdiArea->subWindowList().size() > 1);

	ui->menuWindows->menuAction()->setVisible(hasMdiChild);
	ui->actionTile->setEnabled(hasMdiChild);
	ui->actionTile_Horizontally->setEnabled(hasMdiChild);
	ui->actionTile_Vertically->setEnabled(hasMdiChild);
	ui->actionCascade->setEnabled(hasMdiChild);
	ui->actionNext->setVisible(hasMdiChild && ui->mdiArea->subWindowList().size() > 1);
	ui->actionPrevious->setVisible(hasMdiChild && ui->mdiArea->subWindowList().size() > 1);

#ifndef QT_NO_CLIPBOARD
	//bool hasSelection = (activeMdiChild() && activeMdiChild()->textCursor().hasSelection());
	//cutAct->setEnabled(hasSelection);
	//copyAct->setEnabled(hasSelection);
#endif

	ui->menuEdit->menuAction()->setVisible(hasMdiChild);
	if (hasMdiChild)
	{
		ui->actionUndo->setEnabled(activeMdiChild()->hasUndo());
		ui->actionRedo->setEnabled(activeMdiChild()->hasRedo());
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

	QList<QMdiSubWindow*> windows = ui->mdiArea->subWindowList();
	if (!windows.isEmpty())
		ui->menuWindows->addSeparator();

	for (int i = 0; i < windows.size(); ++i) {
		QMdiSubWindow* mdiSubWindow = windows.at(i);
		ModelViewer* child = qobject_cast<ModelViewer*>(mdiSubWindow->widget());

		QString text;
		if (i < 9)
		{
			text = child->currentFile() == "" ? child->windowTitle() : QFileInfo(child->currentFile()).fileName();
		}
		else
		{
			text = child->currentFile() == "" ? child->windowTitle() : QFileInfo(child->currentFile()).fileName();
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
	//QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();
	const QList<QMdiSubWindow*> subWindows = ui->mdiArea->subWindowList();
	for (QMdiSubWindow* window : subWindows)
	{
		ModelViewer* mdiChild = qobject_cast<ModelViewer*>(window->widget());
		QString curFile = mdiChild->currentFile();
		if (curFile == fileName)
			return window;
	}
	return nullptr;
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
		const QString fileName = QFileInfo(recentFiles.at(i)).fileName();
		recentFileActs[i]->setText(tr("&%1 %2").arg(i + 1).arg(fileName));
		recentFileActs[i]->setData(recentFiles.at(i));
		recentFileActs[i]->setVisible(true);
	}
	for (; i < MaxRecentFiles; ++i)
		recentFileActs[i]->setVisible(false);
}

void MainWindow::openRecentFile()
{
	if (const QAction* action = qobject_cast<const QAction*>(sender()))
	{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		openFile(action->data().toString());
		QApplication::restoreOverrideCursor();
		MainWindow::mainWindow()->activateWindow();
		QApplication::alert(MainWindow::mainWindow());
	}
}
