#include "LanguageManager.h"
#include "Logger.h"
#include "MainWindow.h"
#include "ModelViewer.h"
#include "ModelViewerApplication.h"
#include <iostream>
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QStyleFactory>
#include <sstream>
#include <string>


int main(int argc, char** argv)
{
	Q_INIT_RESOURCE(ModelViewer);

	ModelViewerApplication::configureOpenGLFormat();
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
	// Set the language based on settings or system locale
	QString langCode = settings.value("App/Language").toString();
	if (langCode.isEmpty())
	{
		langCode = QLocale::system().name(); // e.g., "en_US"
	}
	// Load the language settings
	LanguageManager::instance().loadLanguage(langCode);

	// Initialize logger with optional custom max file size
	Logger::instance().initialize(15 * 1024 * 1024);  // 15 MB per file

	bool consoleLogging = settings.value("enableConsoleCheckBox", false).toBool();
	Logger::instance().setConsoleEnabled(consoleLogging);
	bool fileLogging = settings.value("enableLoggingCheckBox", false).toBool();
	Logger::instance().setFileEnabled(fileLogging);
	int logLevel = settings.value("logLevelComboBox", 1).toInt();
	Logger::instance().setMinimumLevel(static_cast<Logger::LogLevel>(logLevel));

	MainWindow* mw = MainWindow::mainWindow();		
	ModelViewer* viewer = mw->createMdiChild();
	mw->showMaximized();
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
		qWarning() << "OpenGL context is not current during startup graphics-info collection";
		mw->setGraphicsInfo("OpenGL context information is unavailable until the viewer is initialized.");
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
