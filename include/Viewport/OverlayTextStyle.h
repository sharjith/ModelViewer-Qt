#pragma once

#include <QPalette>
#include <QProxyStyle>

class QPainter;
class QRect;
class QString;
class QStyle;
class QWidget;

// Draws a crisp one-pixel halo around text so it remains legible over an
// arbitrary viewport image without requiring an opaque panel background.
void drawOutlinedOverlayText(QStyle* style,
                             QPainter* painter,
                             const QRect& rect,
                             int textFlags,
                             const QPalette& palette,
                             bool enabled,
                             const QString& text,
                             QPalette::ColorRole role = QPalette::WindowText);

// A small opt-in style for caption controls that sit directly over the
// viewport.  Editable controls retain their own translucent field styling.
class OutlinedOverlayTextStyle : public QProxyStyle
{
public:
    explicit OutlinedOverlayTextStyle(QStyle* baseStyle = nullptr);

    void drawItemText(QPainter* painter,
                      const QRect& rect,
                      int textFlags,
                      const QPalette& palette,
                      bool enabled,
                      const QString& text,
                      QPalette::ColorRole role = QPalette::NoRole) const override;
};

void installOutlinedOverlayTextStyle(QWidget* widget);
