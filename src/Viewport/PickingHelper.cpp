#include "PickingHelper.h"

namespace PickingHelper
{
unsigned int colorToIndex(const QColor& color)
{
    const int alpha = color.alpha();
    const int red = color.red();
    const int green = color.green();
    const int blue = color.blue();
    return ((alpha << 24) | (red << 16) | (green << 8) | blue);
}

QColor indexToColor(unsigned int index)
{
    const int red = ((index >> 16) & 0xFF);
    const int green = ((index >> 8) & 0xFF);
    const int blue = (index & 0xFF);
    const int alpha = ((index >> 24) & 0xFF);
    return QColor(red, green, blue, alpha);
}

QRect viewportRectForPoint(const QPoint& pixel,
    int widgetWidth,
    int widgetHeight,
    bool multiViewActive)
{
    if (!multiViewActive)
        return QRect(0, 0, widgetWidth, widgetHeight);

    if (pixel.x() < widgetWidth / 2 && pixel.y() > widgetHeight / 2)
        return QRect(0, 0, widgetWidth / 2, widgetHeight / 2);

    if (pixel.x() < widgetWidth / 2 && pixel.y() <= widgetHeight / 2)
        return QRect(0, widgetHeight / 2, widgetWidth / 2, widgetHeight / 2);

    if (pixel.x() >= widgetWidth / 2 && pixel.y() < widgetHeight / 2)
        return QRect(widgetWidth / 2, widgetHeight / 2, widgetWidth / 2, widgetHeight / 2);

    return QRect(widgetWidth / 2, 0, widgetWidth / 2, widgetHeight / 2);
}

QRect clientRectForPoint(const QPoint& pixel,
    int widgetWidth,
    int widgetHeight,
    bool multiViewActive)
{
    if (!multiViewActive)
        return QRect(0, 0, widgetWidth, widgetHeight);

    if (pixel.x() < widgetWidth / 2 && pixel.y() > widgetHeight / 2)
        return QRect(0, widgetHeight / 2, widgetWidth / 2, widgetHeight / 2);

    if (pixel.x() < widgetWidth / 2 && pixel.y() <= widgetHeight / 2)
        return QRect(0, 0, widgetWidth / 2, widgetHeight / 2);

    if (pixel.x() >= widgetWidth / 2 && pixel.y() < widgetHeight / 2)
        return QRect(widgetWidth / 2, 0, widgetWidth / 2, widgetHeight / 2);

    return QRect(widgetWidth / 2, widgetHeight / 2, widgetWidth / 2, widgetHeight / 2);
}
}
