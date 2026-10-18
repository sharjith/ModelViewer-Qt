// LanguageManager.h
#pragma once

#include <QObject>
#include <QTranslator>

class LanguageManager : public QObject
{
    Q_OBJECT

public:
    static LanguageManager& instance();

    void loadLanguage(const QString& langCode);
    QString currentLanguage() const { return _currentLanguage; }

signals:
    void languageChanged();

private:
    LanguageManager();
    QTranslator _appTranslator;
    QTranslator _qtTranslator;
    QString _currentLanguage;
};
