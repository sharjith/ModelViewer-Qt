#pragma once


#include <QApplication>
#include <QStringList>
#include <QUuid>

namespace AppContext
{
    QString& SessionId();
}

class ModelViewerApplication : public QApplication {
public:
    ModelViewerApplication(int& argc, char** argv);

    // Must be called before QApplication is constructed (Wayland / platform attribute requirement)
    static void configureOpenGLAttributes();

    // Static utility method for supported extensions
    static QStringList supportedImportExtensions();

    // Profile-rendering aid: while on, any single event that takes 8 ms or more to deliver is logged ("[SlowEvent]") with
    // its type and receiver, which shows what the GUI thread does between frames.
    static void setSlowEventTraceEnabled(bool enabled) { _slowEventTrace = enabled; }
    static bool slowEventTraceEnabled() { return _slowEventTrace; }
    bool notify(QObject* receiver, QEvent* event) override;
	
    static void setSupportedMSAASamples(int samples) {
        _supportedMSAASamples = samples;
	}

    static int supportedMSAASamples() {
        return _supportedMSAASamples;
	}

    static void setSupportedAnisotropicFilteringLevel(int level)
    {
        _supportedAnisotropicFilteringLevel = level;
    }

    static int supportedAnisotropicFilteringLevel()
    {
        return _supportedAnisotropicFilteringLevel;
	}

    private:
        static void initializeSupportedImportExtensions();

private:
    static QStringList _supportedExtensions;
    static bool _slowEventTrace;
	static int _supportedMSAASamples;
	static int _supportedAnisotropicFilteringLevel;
};
