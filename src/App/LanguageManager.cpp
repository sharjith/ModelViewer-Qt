// LanguageManager.cpp
#include "LanguageManager.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QDebug>

#include "PathUtils.h"

LanguageManager::LanguageManager() {}

LanguageManager& LanguageManager::instance()
{
    static LanguageManager mgr;
    return mgr;
}

void LanguageManager::loadLanguage(const QString& langCode)
{
    QString qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    QString appPath = PathUtils::getDataDirectory() + "/translations";

    QString baseLang = langCode.section('_', 0, 0);

    // Uninstall existing
    qApp->removeTranslator(&_appTranslator);
    qApp->removeTranslator(&_qtTranslator);

    bool loaded = false;

    if (_qtTranslator.load("qt_" + baseLang, qtPath))
    {
        qApp->installTranslator(&_qtTranslator);
    }
    else if (_qtTranslator.load("qt_" + langCode, qtPath))
    {
        qApp->installTranslator(&_qtTranslator);
    }

    QStringList candidates = {
        QString("modelviewer_%1").arg(langCode),
        QString("modelviewer_%1").arg(baseLang)
    };

    for (const QString& file : candidates) {
        if (_appTranslator.load(file, appPath)) {
            qApp->installTranslator(&_appTranslator);
            _currentLanguage = langCode;
            loaded = true;
            break;
        }
    }

    if (loaded) {
        emit languageChanged();  // Notify everyone!
    } else {
        qDebug() << "No language loaded for" << langCode;
    }
}
