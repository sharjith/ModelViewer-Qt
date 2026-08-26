#include "LanguageManager.h"
#include "Logger.h"
#include "MainWindow.h"
#include "ModelViewer.h"
#include "ModelViewerApplication.h"
#include <iostream>
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QSplashScreen>
#include <QStyleFactory>
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

	// Must be called before QApplication is constructed — sets platform OpenGL attributes.
	// On Linux/Wayland this prevents crashes; safe no-op on Windows (gated inside).
	ModelViewerApplication::configureOpenGLAttributes();

	ModelViewerApplication app(argc, argv);

	QPixmap splashPixmap(":/icons/res/Splashscreen.png");
	std::unique_ptr<QSplashScreen> splash;
	auto showSplashMessage = [&](const QString& message) {
		if (!splash) return;
		splash->showMessage(message,
			Qt::AlignBottom | Qt::AlignHCenter,
			Qt::white);
		app.processEvents();
	};

	if (!splashPixmap.isNull())
	{
		QScreen* primaryScreen = app.primaryScreen();
		if (primaryScreen)
		{
			const QSize screenSize = primaryScreen->availableGeometry().size();
			const int maxSplashWidth = std::min(900, static_cast<int>(screenSize.width() * 0.5));
			if (splashPixmap.width() > maxSplashWidth)
			{
				splashPixmap = splashPixmap.scaledToWidth(maxSplashWidth, Qt::SmoothTransformation);
			}
		}
		splash = std::make_unique<QSplashScreen>(splashPixmap);
		splash->setWindowFlag(Qt::WindowStaysOnTopHint);
		splash->show();
		showSplashMessage(QObject::tr("Starting ModelViewer..."));
	}

#if QT_VERSION_MAJOR == 6
	// Disable allocation limit for images
	QImageReader::setAllocationLimit(0);
#endif

#ifdef WIN32
	// qDebug() << QStyleFactory::keys();
	// app.setStyle(QStyleFactory::create("windows"));
#endif

	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	// Set the language based on settings or system locale
	QString langCode = settings.value("App/Language").toString();
	if (langCode.isEmpty())
	{
		langCode = QLocale::system().name(); // e.g., "en_US"
	}
	// Load the language settings
	showSplashMessage(QObject::tr("Loading language..."));
	LanguageManager::instance().loadLanguage(langCode);

	// Initialize logger with optional custom max file size
	showSplashMessage(QObject::tr("Initializing logging..."));
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

	showSplashMessage(QObject::tr("Creating main window..."));
	MainWindow* mw = MainWindow::mainWindow();
	// createMdiChild() constructs the first ViewportWidget, whose
	// RtOptixSceneTracer member runs cudaFree(0)/optixInit()/device-
	// context/pipeline setup synchronously in ITS OWN constructor (see
	// ViewportWidget.h's isAvailable() doc comment) - on a fresh CUDA-code
	// rebuild this is a full OptiX PTX recompile, which can take
	// noticeably longer than everything else in this startup sequence
	// combined. A more specific message here (vs. the previous generic
	// "Preparing workspace...") keeps the splash from looking stuck during
	// that step.
	showSplashMessage(QObject::tr("Preparing workspace and initializing GPU ray tracing..."));
	ModelViewer* viewer = mw->createMdiChild();
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
		showSplashMessage(QObject::tr("Ready"));
		splash->finish(mw);
	}
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
