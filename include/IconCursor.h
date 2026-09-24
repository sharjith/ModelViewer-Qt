#pragma once

#include <QCursor>
#include <QPixmap>
#include <QtMath>

// Keep cursor dimensions and hotspots in logical pixels while using the
// high-resolution icon artwork at the viewport's display scale.
inline QCursor makeIconCursor(const char* resourcePath, int logicalSize,
                              qreal devicePixelRatio, int hotX = -1, int hotY = -1)
{
    const int pixelSize = qRound(logicalSize * devicePixelRatio);
    QPixmap pixmap(resourcePath);
    pixmap = pixmap.scaled(pixelSize, pixelSize, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return QCursor(pixmap, hotX, hotY);
}
