#pragma once


#include <QColor>
#include <QPoint>
#include <QRect>

namespace PickingHelper
{
unsigned int colorToIndex(const QColor& color);
QColor indexToColor(unsigned int index);

QRect viewportRectForPoint(const QPoint& pixel,
    int widgetWidth,
    int widgetHeight,
    bool multiViewActive);

QRect clientRectForPoint(const QPoint& pixel,
    int widgetWidth,
    int widgetHeight,
    bool multiViewActive);
}
