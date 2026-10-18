#include "OverlayTextStyle.h"

#include <QPainter>
#include <QPoint>
#include <QStyle>
#include <QWidget>

#include <iterator>

namespace
{
QPalette::ColorRole effectiveRole(QPalette::ColorRole role)
{
    return role == QPalette::NoRole ? QPalette::WindowText : role;
}

QColor textHaloColor(const QPalette& palette, bool enabled, QPalette::ColorRole role)
{
    const QPalette::ColorGroup group = enabled ? QPalette::Active : QPalette::Disabled;
    const QColor foreground = palette.color(group, effectiveRole(role));
    return foreground.lightnessF() >= 0.5
        ? QColor(0, 0, 0, 215)
        : QColor(255, 255, 255, 220);
}
}

namespace
{
void drawOverlayTextWithHalo(QStyle* style,
                             QPainter* painter,
                             const QRect& rect,
                             int textFlags,
                             const QPalette& palette,
                             bool enabled,
                             const QString& text,
                             QPalette::ColorRole role,
                             const QPoint* offsets,
                             int offsetCount,
                             int haloAlpha)
{
    if (!style || !painter || text.isEmpty())
        return;

    const QPalette::ColorRole colorRole = effectiveRole(role);
    QPalette haloPalette(palette);
    QColor halo = textHaloColor(palette, enabled, colorRole);
    halo.setAlpha(haloAlpha);
    haloPalette.setColor(QPalette::Active, colorRole, halo);
    haloPalette.setColor(QPalette::Inactive, colorRole, halo);
    haloPalette.setColor(QPalette::Disabled, colorRole, halo);

    for (int i = 0; i < offsetCount; ++i)
        style->drawItemText(painter, rect.translated(offsets[i]), textFlags,
                            haloPalette, enabled, text, colorRole);
    style->drawItemText(painter, rect, textFlags, palette, enabled, text, colorRole);
}
}

void drawOutlinedOverlayText(QStyle* style,
                             QPainter* painter,
                             const QRect& rect,
                             int textFlags,
                             const QPalette& palette,
                             bool enabled,
                             const QString& text,
                             QPalette::ColorRole role)
{
    static constexpr QPoint offsets[] = {
        QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1), QPoint(-1, 0),
        QPoint(1, 0), QPoint(-1, 1), QPoint(0, 1), QPoint(1, 1)
    };
    drawOverlayTextWithHalo(style, painter, rect, textFlags, palette, enabled,
                            text, role, offsets, std::size(offsets), 215);
}

OutlinedOverlayTextStyle::OutlinedOverlayTextStyle(QStyle* baseStyle)
    : QProxyStyle(baseStyle)
{
}

void OutlinedOverlayTextStyle::drawItemText(QPainter* painter,
                                            const QRect& rect,
                                            int textFlags,
                                            const QPalette& palette,
                                            bool enabled,
                                            const QString& text,
                                            QPalette::ColorRole role) const
{
    drawOutlinedOverlayText(baseStyle(), painter, rect, textFlags, palette,
                            enabled, text, role);
}

void installOutlinedOverlayTextStyle(QWidget* widget)
{
    if (!widget || widget->property("outlinedOverlayTextStyleInstalled").toBool())
        return;

    widget->setProperty("outlinedOverlayTextStyleInstalled", true);
    widget->setStyle(new OutlinedOverlayTextStyle());
}
