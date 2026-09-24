#include "LanguageManager.h"
#include "Logger.h"
#include "MainWindow.h"
#include "ModelViewer.h"
#include "ModelViewerApplication.h"
#include "StartupSplash.h"
#include "ViewportWidget.h"
#include <iostream>
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QScreen>
#include <QStyleFactory>
#include <QThread>
#include <memory>
#include <sstream>
#include <string>

namespace
{
// Installed on qApp when the "Enable Tooltips" setting is off. Qt has no built-in
// global tooltip toggle, so this suppresses every QEvent::ToolTip app-wide instead.
class TooltipSuppressor : public QObject
{
protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override
    {
        return event->type() == QEvent::ToolTip;
    }
};
}

int main(int argc, char** argv)
{
	Q_INIT_RESOURCE(ModelViewer);

#if defined(Q_OS_LINUX)
	// ThemeManager (see its own comments) already takes full manual control
	// of styling/palette rather than deferring to a QPA platform theme
	// plugin - KDE/GNOME integration is worked around there already, for a
	// documented Qt version mismatch that prevents loading either properly.
	// A third-party platform theme tool (qt6ct) some users have configured
	// system-wide goes further than KDE/GNOME's own integration would:
	// active *at all*, regardless of what ThemeManager does, it applies its
	// own app-wide styling layer that Qt's built-in QLineEdit clear button
	// doesn't reliably survive under Fusion - confirmed: the search box's
	// clear button (searchEdit, see MaterialPropertiesPanel) stayed
	// genuinely invisible (while remaining fully clickable) only when
	// QT_QPA_PLATFORMTHEME=qt6ct was set, never otherwise. QT_QPA_PLATFORMTHEME
	// is read once, when QApplication is constructed just below - clearing
	// it here, before that happens, keeps this app on Qt's own generic
	// handling unconditionally, without touching the user's shell/session
	// (where it may be set intentionally for other applications).
	qunsetenv("QT_QPA_PLATFORMTHEME");
#endif

	// Must be called before QApplication is constructed — sets platform OpenGL attributes.
	// On Linux/Wayland this prevents crashes; safe no-op on Windows (gated inside).
	ModelViewerApplication::configureOpenGLAttributes();

	ModelViewerApplication app(argc, argv);

#if QT_VERSION_MAJOR == 6
	// Disable allocation limit for images
	QImageReader::setAllocationLimit(0);
#endif

#ifdef WIN32
	// qDebug() << QStyleFactory::keys();
	// app.setStyle(QStyleFactory::create("windows"));
#endif

	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	// Set the language based on settings or system locale. Done before the splash appears: it is cheap, and it
	// means every splash message - including the first - is shown in the user's language.
	QString langCode = settings.value("App/Language").toString();
	if (langCode.isEmpty())
	{
		langCode = QLocale::system().name(); // e.g., "en_US"
	}
	LanguageManager::instance().loadLanguage(langCode);

	// Splash: opens on the screen the main window will restore to, sharp at that screen's DPI.
	// StartupSplash::report() (also used from MainWindow's constructor) only repaints it; the explicit
	// processEvents() below is deliberately limited to main(), where nothing half-built can be re-entered.
	std::unique_ptr<StartupSplash> splash;
	const QPixmap splashArtwork(":/icons/res/Splashscreen.png");
	if (!splashArtwork.isNull())
	{
		QScreen* splashScreen = StartupSplash::screenForSavedWindow(settings.value("geometry").toByteArray());
		splash = std::make_unique<StartupSplash>(StartupSplash::prepareArtwork(splashArtwork, splashScreen), splashScreen);
		StartupSplash::install(splash.get());
		splash->show();
	}
	auto showStartupStep = [&](const QString& message, int percent) {
		StartupSplash::report(message, percent);
		// Everything except user input: keeps queued work (timers, posted calls) flowing while a stray click
		// or key press can neither dismiss the splash nor reach a half-built main window.
		if (splash)
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
	};
	showStartupStep(QObject::tr("Starting ModelViewer..."), 3);

	// Initialize logger with optional custom max file size
	showStartupStep(QObject::tr("Initializing logging..."), 8);
	Logger::instance().initialize(15 * 1024 * 1024);  // 15 MB per file

	// Set before setConsoleEnabled() below so the panel is created with the
	// right buffer size from the start rather than created then resized.
	int consoleBufferLines = settings.value("consoleBufferLinesSpinBox", 20000).toInt();
	Logger::instance().setConsoleBufferLines(consoleBufferLines);
	bool consoleLogging = settings.value("enableConsoleCheckBox", false).toBool();
	Logger::instance().setConsoleEnabled(consoleLogging);
	bool fileLogging = settings.value("enableLoggingCheckBox", false).toBool();
	Logger::instance().setFileEnabled(fileLogging);
	int logLevel = settings.value("logLevelComboBox", 1).toInt();
	Logger::instance().setMinimumLevel(static_cast<Logger::LogLevel>(logLevel));

	// Qt has no built-in global tooltip toggle; install an app-wide filter to suppress
	// them when disabled. Read once at startup — like MSAA/V-Sync, takes effect on restart.
	bool tooltipsEnabled = settings.value("checkTooltips", true).toBool();
	if (!tooltipsEnabled)
	{
		static TooltipSuppressor tooltipSuppressor;
		app.installEventFilter(&tooltipSuppressor);
	}

	showStartupStep(QObject::tr("Creating main window..."), 12);
	MainWindow* mw = MainWindow::mainWindow();
	// createMdiChild() constructs the first ViewportWidget, whose
	// RtOptixSceneTracer member runs cudaFree(0)/optixInit()/device-
	// context/pipeline setup synchronously in ITS OWN constructor (see
	// ViewportWidget.h's isAvailable() doc comment) - on a fresh CUDA-code
	// rebuild this is a full OptiX PTX recompile, which can take
	// noticeably longer than everything else in this startup sequence
	// combined. A more specific message here (vs. the previous generic
	// "Preparing workspace...") keeps the splash from looking stuck during
	// that step. Only claimed for builds that have OptiX at all, and phrased
	// as detection: the machine may have no NVIDIA GPU or driver.
#ifdef MODELVIEWER_HAVE_OPTIX
	showStartupStep(QObject::tr("Preparing workspace and detecting GPU ray tracing..."), 60);
#else
	showStartupStep(QObject::tr("Preparing workspace..."), 60);
#endif
	ModelViewer* viewer = mw->createMdiChild();
	// Watch for the viewport's first drawn frame BEFORE the window is shown (it can't paint earlier), so the
	// splash can stay up until there is something to see instead of handing over to a blank viewport.
	bool firstFramePainted = false;
	QMetaObject::Connection firstFrameConnection;
	if (splash)
		firstFrameConnection = QObject::connect(viewer->getViewportWidget(), &QOpenGLWidget::frameSwapped,
			[&firstFramePainted]() { firstFramePainted = true; });
	StartupSplash::report(QObject::tr("Starting the viewport..."), 85);
	mw->showMaximized();
	// createMdiChild() only adds the viewer to the QMdiArea via
	// addSubWindow() - unlike on_actionNew_triggered()'s identical setup for
	// every subsequently created document, it never explicitly shows the
	// resulting QMdiSubWindow. Under XWayland this leaves the wrapped
	// QOpenGLWidget's expose event essentially in limbo: the subwindow's own
	// (raster-painted) title bar renders fine as a side effect of MainWindow
	// itself becoming visible, but the QOpenGLWidget viewport inside it never
	// gets the expose event that triggers initializeGL()/paintGL() - it just
	// sits uninitialized (permanently black viewport, then segfaults on close
	// per SceneRenderController::releaseGpuResources() touching GL state that
	// was never set up). Native X11 doesn't hit this - presentDocumentFullscreen()
	// below makes it explicit instead of relying on implicit cascade-through-
	// parent visibility, matching what on_actionNew_triggered() already does.
	mw->presentDocumentFullscreen(viewer);
	if (splash)
	{
		// Bounded wait: if the viewport never paints (some headless/Wayland setups) the splash must not linger.
		StartupSplash::report(QObject::tr("Starting the viewport..."), 92);
		QElapsedTimer waited;
		waited.start();
		while (!firstFramePainted && waited.elapsed() < 3000)
		{
			app.processEvents(QEventLoop::ExcludeUserInputEvents, 25);
			QThread::msleep(5);
		}
		QObject::disconnect(firstFrameConnection);
		showStartupStep(QObject::tr("Ready"), 100);
		splash->finish(mw);
	}

	// Only now construct the console panel (if enabled) - see
	// Logger::notifyApplicationVisible()'s doc comment for why doing this any
	// earlier, before MainWindow had established its own taskbar identity,
	// caused an intermittent taskbar icon mixup.
	Logger::instance().notifyApplicationVisible();

	if (argc > 1)
	{
		QString fileName(argv[1]);
		QFileInfo fi(fileName);
		if (fi.exists())
		{
			mw->openFile(fileName);
			viewer->parentWidget()->close(); // close the first blank document
		}
	}

	QStringList recentFiles = MainWindow::readRecentFiles(settings);
	bool restoreLastOpenFile = settings.value("checkRestoreLastFile", false).toBool();
	if (restoreLastOpenFile && !recentFiles.isEmpty())
	{
		QString lastFile = recentFiles.first();
		QFileInfo fi(lastFile);
		if (fi.exists())
		{
			mw->openFile(lastFile);
			viewer->parentWidget()->close(); // close the first blank document
		}
	}

	// Guard against platforms (Wayland, headless) where no context is current at this point.
	// The main window's ViewportWidget will have initialised by now on most platforms, but it is
	// not guaranteed — a missing context here would crash glGetString.
	if (QOpenGLContext::currentContext())
	{
		QOpenGLFunctions glFuncs(QOpenGLContext::currentContext());
		auto logOpenGLInfo = [&glFuncs](auto& outputStream) {
			std::map<std::string, GLenum> infoMap = {
				{"Renderer", GL_RENDERER},
				{"Vendor", GL_VENDOR},
				{"OpenGL Version", GL_VERSION},
				{"Shader Version", GL_SHADING_LANGUAGE_VERSION}
			};

			for (const auto& [label, value] : infoMap) {
				const char* info = reinterpret_cast<const char*>(glFuncs.glGetString(value));
				outputStream << label << ": " << info << '\n';
			}
		};

		// Log information to std::cout
		logOpenGLInfo(std::cout);

		// Collect information into std::stringstream and set it to mw
		std::stringstream ss;
		logOpenGLInfo(ss);
		mw->setGraphicsInfo(ss.str().c_str());
	}
	else
	{
		qWarning() << "main: OpenGL context not current during startup — graphics info unavailable until viewer initialises.";
		mw->setGraphicsInfo("OpenGL context information unavailable until the viewer is initialised.");
	}

	/*
#ifdef QT_DEBUG
	int n = 0;
	glFuncs.glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	for (int i = 0; i < n; i++)
	{
		const char* extension =
				(const char*)glFuncs.glGetStringi(GL_EXTENSIONS, i);
		printf("GL Extension %d: %s\n", i, extension);
	}
	std::cout << std::endl;

#endif // DEBUG
*/

	QList<QByteArray> formats = QImageReader::supportedImageFormats();
	if (formats.contains("webp"))
	{
		qDebug() << "WebP support is available";
	}
	else
	{
		qDebug() << "WebP support is NOT available";
	}

	int result = app.exec();

	// Shutdown logger when exiting (restores original stream buffers)
	Logger::instance().shutdown();

	return result;
}
