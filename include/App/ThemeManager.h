#pragma once

#include <QObject>
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QTextStream>

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum Theme
    {
        System,
        Light,
        Dark,        
        DarkOrange,        
   		Dracula,
        Eclippy,
        GruvboxFusion,
        LightGray,
        Manjaroness,
        MaterialDark,
		Monokai,
        NordFusion,
		OneDark,        
        SolarizedDark,
        SolarizedLight,
        Takezo,
		TokyoNightFusion,
    };

    explicit ThemeManager(QObject* parent = nullptr);

    void setTheme(Theme theme);
    Theme currentTheme() const { return _currentTheme; }

    void applyThemeForColorScheme(bool isDarkMode);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    void applyThemeForColorScheme(Qt::ColorScheme scheme);
#endif

	void applyThemefromStyleSheet(const QString& styleSheet);

    bool isSystemInDarkMode() const;
        
signals:
    void themeChanged(Theme theme);

private:
    void applySystemTheme();
    void applySystemAwareTheme();
#if defined(Q_OS_LINUX)
    // Reads the running Plasma session's actual configured font and color
    // scheme from ~/.config/kdeglobals and applies them. Returns false (no-op)
    // when not running under a KDE session or kdeglobals isn't present, so
    // callers can fall back to the generic palette. See its own doc comment
    // in ThemeManager.cpp for why this reads the config file directly instead
    // of using Qt's QT_QPA_PLATFORMTHEME=kde integration.
    bool applyKdePlasmaTheme();

    // Same idea for GNOME: shells out to the gsettings CLI (GNOME has no
    // plain config file like kdeglobals - its settings live in dconf, and
    // gsettings is the standard way to read them without linking GIO/GLib
    // just for this) to read the configured font, light/dark preference, and
    // accent color. Returns false when not on a GNOME session or gsettings
    // isn't available, so callers can fall back to the generic palette.
    bool applyGnomeTheme();
    QString readGSetting(const QString& schema, const QString& key) const;
#endif
    void applyLightTheme();
    void applyDarkTheme();


    QPalette getLightPalette() const;
    QPalette getDarkPalette() const;

    QString getLightStyleSheet() const;
    QString getDarkStyleSheet() const;

    QString getCurrentStyleName() const;

    QString getDarkExtrasStyleSheet() const;

    Theme _currentTheme;
    QSettings* _settings;
};

