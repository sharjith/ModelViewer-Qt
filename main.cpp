#include <QApplication>
#include <QStyleFactory>
#include <QDebug>

#include "MainWindow.h"

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
	return app.exec();
}