
#include <QApplication>
#include <QCoreApplication>
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
#include <QMdiSubWindow>
#include <assimp/version.h>

#include "PathUtils.h"
#include "RtRenderDialog.h"

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
		// Parented to the ModelViewer (the MDI subwindow's own content
		// widget - added via QMdiArea::addSubWindow(), which reparents it
		// under the QMdiSubWindow it creates), NOT to MainWindow (`this`) -
		// a QDialog still floats as an independent top-level window
		// regardless of which widget is passed as its parent, but the
		// parent IS what Qt uses to auto-destroy child widgets when it's
		// destroyed. Parenting to MainWindow meant the dialog outlived
		// every MDI document, including all of them being closed - this
		// way it closes along with the document it belongs to instead.
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

	// Update menus when undo stack changes
	connect(ui->mdiArea, &QMdiArea::subWindowActivated, this, [this]() {
		updateMenus();

		ModelViewer* child = activeMdiChild();
		if (!child)
			return;

		// Connect to the new child's undo stack
		if (child->getUndoStack())
		{
			connect(child->getUndoStack(), &QUndoStack::indexChanged,
				this, &MainWindow::updateMenus, Qt::UniqueConnection);
		}
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
	setCentralWidget((ui->mdiArea));

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
	ModelViewer* viewer = new ModelViewer(ui->mdiArea);
	QString lastOpenedDir = PathUtils::getDataDirectory() + QString("/test-models");
	viewer->setLastOpenedDir(lastOpenedDir);
	viewer->setAttribute(Qt::WA_DeleteOnClose);
	_viewers.append(viewer);
	// Keep _viewers in sync regardless of how the sub-window is closed
	// (MDI X button, closeAllSubWindows, failed load, etc.). WA_DeleteOnClose
	// deletes the viewer without going through closeSubWindow(), so without
	// this the list accumulates dangling pointers that crash settings apply.
	connect(viewer, &QObject::destroyed, this, [this, viewer]() {
		_viewers.removeAll(viewer);
	});
	ui->mdiArea->addSubWindow(viewer);
	connect(viewer, &ModelViewer::documentModifiedChanged,
	        this, [this, viewer](bool) {
	            if (viewer == activeMdiChild())
	                updateMenus();
	        });

	// Apply persisted perspective FOV immediately so the first view uses the
	// setting before any settingsChanged signal fires.
	{
		QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		const int fov = s.value("fieldOfViewSpinBox", 45).toInt();
		viewer->getViewportWidget()->setPerspFOV(fov);
	}

	return viewer;
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
		_viewers[0]->showMaximized();
		_viewers[0]->updateDisplayList();

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
	_viewers.append(viewer);
	ui->mdiArea->addSubWindow(viewer);
	viewer->showMaximized();
	//std::vector<int> mod = { 5 };
	//viewer->getViewportWidget()->setDisplayList(mod);
	viewer->updateDisplayList();
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
		ViewportWidget* viewportWidget = activeMdiChild()->getViewportWidget();
		if (viewportWidget)
			viewportWidget->cancelAssImpModelLoading();
	}
}

void MainWindow::closeSubWindow()
{
	ModelViewer* viewer = activeMdiChild();	
	viewer->parentWidget()->close();
	// Remove from the list
	_viewers.removeAll(viewer);
}

void MainWindow::closeAllSubWindows()
{
	QList<QMdiSubWindow*> subWindows = ui->mdiArea->subWindowList();
	for (QMdiSubWindow* sub : subWindows)
	{
		ModelViewer* viewer = dynamic_cast<ModelViewer*>(sub->widget());
		if (viewer)
		{
			viewer->parentWidget()->close();
		}
	}
}

bool MainWindow::loadFile(const QString& fileName)
{
	ModelViewer* child = createMdiChild();
	child->setWindowState(Qt::WindowMaximized);
	child->show();
	const bool succeeded = child->loadFile(fileName);
	if (!succeeded)
		child->parentWidget()->close();
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
		ui->actionSave_As->setEnabled(!activeMdiChild()->getViewportWidget()->getMeshStore().empty());
	}
#ifndef QT_NO_CLIPBOARD
	//pasteAct->setEnabled(hasMdiChild);
#endif
	ui->actionImport->setVisible(hasMdiChild);
	ui->actionExport->setVisible(hasMdiChild);
	ui->actionClose->setEnabled(hasMdiChild);
	ui->actionFileClose->setVisible(hasMdiChild);
	ui->actionClose_All->setVisible(hasMdiChild && ui->mdiArea->subWindowList().size() > 1);

	ui->menuWindows->menuAction()->setVisible(hasMdiChild);
	ui->actionTile->setEnabled(hasMdiChild);

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

	// Get the list of MDI child windows
	QList<QMdiSubWindow*> windows = ui->mdiArea->subWindowList();

	// Query each MDI child window
	for (QMdiSubWindow* window : windows)
	{
		ModelViewer* child = qobject_cast<ModelViewer*>(window->widget());
		if (child)
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

