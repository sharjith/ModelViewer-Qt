#pragma once

#include <QCursor>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <QtMath>

// Keep cursor dimensions and hotspots in logical pixels while using the
// high-resolution icon artwork at the viewport's display scale.
//
// The viewport calls this on every mouse move of a drag. Each call used to decode the PNG, scale it and
// build a new QCursor, and every new QCursor makes the platform create a new native cursor handle. A long
// session ran the process out of cursor handles; creation then failed and the zoom, pan and rotate cursors
// stayed as the arrow (switching document made a fresh cursor, which worked again). The finished cursor
// is therefore built once per icon, size, scale and hotspot and reused.
inline QCursor makeIconCursor(const char* resourcePath, int logicalSize,
                              qreal devicePixelRatio, int hotX = -1, int hotY = -1)
{
    static QHash<QString, QCursor> cache;

    const QString key = QString::fromLatin1(resourcePath) + QLatin1Char('|')
        + QString::number(logicalSize) + QLatin1Char('|')
        + QString::number(devicePixelRatio, 'g', 6) + QLatin1Char('|')
        + QString::number(hotX) + QLatin1Char('|') + QString::number(hotY);

    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    const int pixelSize = qRound(logicalSize * devicePixelRatio);
    QPixmap pixmap(resourcePath);
    pixmap = pixmap.scaled(pixelSize, pixelSize, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    const QCursor cursor(pixmap, hotX, hotY);
    // Do not keep a failed load: a null pixmap would pin the arrow for good.
    if (!pixmap.isNull())
        cache.insert(key, cursor);
    return cursor;
}
