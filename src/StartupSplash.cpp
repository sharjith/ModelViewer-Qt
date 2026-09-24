#include "StartupSplash.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QScreen>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <config.h>

QPointer<StartupSplash> StartupSplash::s_instance;

StartupSplash::StartupSplash(const QPixmap& artwork, QScreen* screen)
    // Passed explicitly rather than set afterwards: changing window flags on a shown widget recreates its
    // native window.
    : QSplashScreen(screen, artwork, Qt::WindowStaysOnTopHint)
    , _versionText(tr("Version %1").arg(QStringLiteral(APP_VERSION_STRING)))
{
}

QPixmap StartupSplash::prepareArtwork(const QPixmap& source, QScreen* screen)
{
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    const int logicalWidth = screen ? std::min(900, static_cast<int>(screen->availableGeometry().width() * 0.5)) : 900;
    // Physical pixels wanted at this DPR, but never upscale the source: on a 200% screen the splash simply comes
    // out a little smaller (source width / 2) and stays sharp.
    const int physicalWidth = std::min(static_cast<int>(std::lround(logicalWidth * dpr)), source.width());
    QPixmap scaled = source.width() > physicalWidth ? source.scaledToWidth(physicalWidth, Qt::SmoothTransformation) : source;
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

QScreen* StartupSplash::screenForSavedWindow(const QByteArray& savedGeometry)
{
    if (!savedGeometry.isEmpty())
    {
        QWidget probe;   // never shown; only used to decode the geometry blob
        if (probe.restoreGeometry(savedGeometry))
            if (QScreen* screen = QGuiApplication::screenAt(probe.frameGeometry().center()))
                return screen;
    }
    return QGuiApplication::primaryScreen();
}

void StartupSplash::setStatus(const QString& message, int percent)
{
    _message = message;
    _percent = std::clamp(percent, 0, 100);
    // repaint(), not update(): the caller may be busy for seconds without returning to the event loop.
    if (isVisible())
        repaint();
}

void StartupSplash::install(StartupSplash* splash)
{
    s_instance = splash;
}

void StartupSplash::report(const QString& message, int percent)
{
    if (s_instance)
        s_instance->setStatus(message, percent);
}

void StartupSplash::mousePressEvent(QMouseEvent* event)
{
    // QSplashScreen hides itself on click; a stray click during startup should not dismiss it.
    Q_UNUSED(event);
}

void StartupSplash::drawContents(QPainter* painter)
{
    const QRect r = rect();
    if (r.isEmpty())
        return;

    // Everything scales with the splash so it looks the same at 680 px and at 900 px wide.
    const int bandHeight = std::max(36, static_cast<int>(r.height() * 0.085));
    const int margin = std::max(14, static_cast<int>(r.width() * 0.027));
    const QRect band(0, r.height() - bandHeight, r.width(), bandHeight);

    // A dark gradient behind the text and bar: the artwork's floor reflections would otherwise fight the text.
    QLinearGradient shade(band.topLeft(), band.bottomLeft());
    shade.setColorAt(0.0, QColor(0, 0, 0, 0));
    shade.setColorAt(0.55, QColor(0, 0, 0, 150));
    shade.setColorAt(1.0, QColor(0, 0, 0, 215));
    painter->fillRect(band, shade);

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    const int barHeight = std::max(3, static_cast<int>(bandHeight * 0.075));
    const int barY = r.height() - static_cast<int>(bandHeight * 0.20) - barHeight;
    const QRectF track(margin, barY, r.width() - 2 * margin, barHeight);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 45));
    painter->drawRoundedRect(track, barHeight / 2.0, barHeight / 2.0);
    if (_percent > 0)
    {
        QRectF fill = track;
        fill.setWidth(std::max<qreal>(barHeight, track.width() * _percent / 100.0));
        painter->setBrush(QColor(112, 124, 240));   // the periwinkle of "Ray Tracing" in the artwork
        painter->drawRoundedRect(fill, barHeight / 2.0, barHeight / 2.0);
    }

    QFont font = painter->font();
    font.setPixelSize(std::max(11, static_cast<int>(bandHeight * 0.30)));
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const QRect textRow(margin, band.top() + static_cast<int>(bandHeight * 0.16), r.width() - 2 * margin,
                        barY - band.top() - static_cast<int>(bandHeight * 0.16) - static_cast<int>(bandHeight * 0.06));

    const int versionWidth = metrics.horizontalAdvance(_versionText);
    const int gap = margin;
    const QString message = metrics.elidedText(_message, Qt::ElideRight, std::max(0, textRow.width() - versionWidth - gap));

    const auto drawText = [&](const QRect& rect, int flags, const QString& text)
    {
        painter->setPen(QColor(0, 0, 0, 170));   // 1 px shadow keeps the white readable over bright reflections
        painter->drawText(rect.translated(1, 1), flags, text);
        painter->setPen(QColor(255, 255, 255, 235));
        painter->drawText(rect, flags, text);
    };
    drawText(textRow, Qt::AlignLeft | Qt::AlignVCenter, message);
    drawText(textRow, Qt::AlignRight | Qt::AlignVCenter, _versionText);
}
