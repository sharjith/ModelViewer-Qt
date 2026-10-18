#pragma once

#include <QPixmap>
#include <QPointer>
#include <QSplashScreen>

class QScreen;

// The startup splash: the artwork plus, drawn at runtime, a status line, a thin progress bar and the version -
// so none of that has to be baked into the image and re-exported per release.
//
//  * Sharp on high-DPI screens: the artwork is scaled to the target screen's device pixel ratio (prepareArtwork()).
//  * Opens on the screen the main window will restore to (screenForSavedWindow()), not blindly on the primary one.
//  * A stray click does not dismiss it.
//  * Progress can be reported from anywhere with report(), which only repaints the splash - it never pumps the
//    event loop, so it is safe to call from inside constructors (e.g. MainWindow's), where processing queued
//    events could re-enter half-built objects.
class StartupSplash : public QSplashScreen
{
    Q_OBJECT
public:
    // `artwork` must come from prepareArtwork(); `screen` is where the splash is centred.
    StartupSplash(const QPixmap& artwork, QScreen* screen);

    // Scales the full-resolution artwork to at most 900 logical pixels (and at most half the screen width) at
    // the screen's device pixel ratio, never beyond the source's own resolution.
    static QPixmap prepareArtwork(const QPixmap& source, QScreen* screen);
    // The screen a saved MainWindow geometry (QWidget::saveGeometry() blob) belongs to; the primary screen if
    // there is none or it can't be resolved.
    static QScreen* screenForSavedWindow(const QByteArray& savedGeometry);

    void setStatus(const QString& message, int percent);

    // Registers the splash that report() drives (nullptr / destroyed = report() does nothing).
    static void install(StartupSplash* splash);
    static void report(const QString& message, int percent);

protected:
    void drawContents(QPainter* painter) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString _message;
    int _percent = 0;
    QString _versionText;

    static QPointer<StartupSplash> s_instance;
};
