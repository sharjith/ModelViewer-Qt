#include <QApplication>
#include <QStyleFactory>
#include <QDebug>
#include <QOpenGLFunctions>
#include <QFileInfo>

#include "MainWindow.h"
#include "ModelViewer.h"
#include <iostream>
#include <string>
#include <sstream>

int main(int argc, char** argv)
{
	Q_INIT_RESOURCE(ModelViewer);

	QApplication::setDesktopSettingsAware(true);

	QApplication app(argc, argv);
	QCoreApplication::setApplicationName("ModelViewer");
	QCoreApplication::setOrganizationName("Sharjith N");
	QCoreApplication::setApplicationVersion(QT_VERSION_STR);

#ifdef WIN32
	//qDebug() << QStyleFactory::keys();
	//app.setStyle(QStyleFactory::create("windows"));
#endif

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

	QOpenGLFunctions glFuncs(QOpenGLContext::currentContext());
	std::cout << "Renderer: " << glFuncs.glGetString(GL_RENDERER) << '\n';
	std::cout << "Vendor:   " << glFuncs.glGetString(GL_VENDOR) << '\n';
	std::cout << "OpenGL Version:  " << glFuncs.glGetString(GL_VERSION) << '\n';
	std::cout << "Shader Version:   " << glFuncs.glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n"
		<< std::endl;

	std::stringstream ss;
	ss << "Renderer: " << glFuncs.glGetString(GL_RENDERER) << '\n';
	ss << "Vendor:   " << glFuncs.glGetString(GL_VENDOR) << '\n';
	ss << "OpenGL Version:  " << glFuncs.glGetString(GL_VERSION) << '\n';
	ss << "Shader Version:   " << glFuncs.glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
	mw->setGraphicsInfo(ss.str().c_str());

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

	return app.exec();
}