// LanguageManager.cpp
#include "LanguageManager.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QDebug>

#include "PathUtils.h"

// The vcpkg Qt build ships no qtbase_xx.qm, so the standard button captions of QMessageBox /
// QDialogButtonBox (looked up by Qt under the "QPlatformTheme" context) are supplied by the application
// catalog instead. These markers exist only so lupdate keeps the entries.
[[maybe_unused]] static const char* const kQtStandardButtonCaptions[] = {
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Save"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Save All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Open"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&Yes"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes to &All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&No"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "N&o to All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Abort"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Retry"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Ignore"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Close"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Discard"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Help"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Reset"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Restore Defaults"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Don't Save"),
};

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
