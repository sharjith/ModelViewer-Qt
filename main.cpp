#include <QApplication>
#include <QStyleFactory>
#include <QDebug>
#include <QOpenGLFunctions> 

#include "MainWindow.h"
#include <iostream>

int main(int argc, char** argv)
{
	QApplication::setDesktopSettingsAware(true);

	QApplication app(argc, argv);

#ifdef WIN32
	//qDebug() << QStyleFactory::keys();
	//app.setStyle(QStyleFactory::create("windows"));
#endif

	MainWindow* mw = MainWindow::mainWindow();
	mw->showMaximized();

	QOpenGLFunctions glFuncs(QOpenGLContext::currentContext());
	std::cout << "Renderer: " << glFuncs.glGetString(GL_RENDERER) << '\n';
	std::cout << "Vendor:   " << glFuncs.glGetString(GL_VENDOR) << '\n';
	std::cout << "OpenGL Version:  " << glFuncs.glGetString(GL_VERSION) << '\n';
	std::cout << "Shader Version:   " << glFuncs.glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n"
		<< std::endl;

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