#include <QApplication>
#include <QStyleFactory>
#include <QDebug>

#include "MainWindow.h"


int main(int argc, char** argv)
{   
    QApplication::setDesktopSettingsAware(true);

    QApplication app(argc, argv);
#ifdef WIN32
    app.setStyle(QStyleFactory::create("Fusion"));
#endif

	MainWindow* mw = new MainWindow();
    mw->showMaximized();
    return app.exec();
}
