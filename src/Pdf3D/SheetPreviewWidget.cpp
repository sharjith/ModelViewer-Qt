#include "SheetPreviewWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace Pdf3D
{

namespace
{
constexpr qreal kMargin = 8.0;
}

SheetPreviewWidget::SheetPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SheetPreviewWidget::setTemplate(const SheetTemplate& tpl)
{
    _tpl = tpl;
    if (_selected >= _tpl.cells.size())
        _selected = -1;
    _dirty = true;
    updateGeometry();
    update();
}

void SheetPreviewWidget::setValues(const QMap<QString, QString>& values)
{
    if (values == _values)
        return;
    _values = values;
    _dirty = true;
    update();
}

void SheetPreviewWidget::setSelectedCell(int index)
{
    index = (index >= 0 && index < _tpl.cells.size()) ? index : -1;
    if (index == _selected)
        return;
    _selected = index;
    _dirty = true;
    update();
}

QSize SheetPreviewWidget::sizeHint() const
{
    return QSize(560, 400);
}

QSize SheetPreviewWidget::minimumSizeHint() const
{
    return QSize(280, 200);
}

QRectF SheetPreviewWidget::pageRect() const
{
    const QSizeF pagePt = sheetPageSizePt(_tpl);
    if (pagePt.isEmpty())
        return QRectF();
    const QRectF available = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    QSizeF size = pagePt;
    size.scale(available.size(), Qt::KeepAspectRatio);
    return QRectF(QPointF(available.center().x() - size.width() / 2.0, available.center().y() - size.height() / 2.0), size);
}

void SheetPreviewWidget::render(const QSize& pixels, qreal devicePixelRatio)
{
    _image = renderSheetPreview(_tpl, _values, std::max(1, pixels.width()), _selected, &_layout);
    _image.setDevicePixelRatio(devicePixelRatio);
    _imageSize = pixels;
    _dirty = false;
}

void SheetPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    const QRectF page = pageRect();
    if (page.isEmpty())
        return;
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(static_cast<int>(std::lround(page.width() * dpr)), static_cast<int>(std::lround(page.height() * dpr)));
    if (_dirty || _imageSize != pixels)
        render(pixels, dpr);
    p.fillRect(page.translated(3, 3), QColor(0, 0, 0, 50));   // a soft shadow under the page
    p.drawImage(page.topLeft(), _image);
    p.setPen(QColor(120, 120, 120));
    p.setBrush(Qt::NoBrush);
    p.drawRect(page);
}

void SheetPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    const QRectF page = pageRect();
    int hit = -1;
    if (!page.isEmpty() && page.contains(event->position()))
    {
        const double zoom = page.width() / sheetPageSizePt(_tpl).width();
        const QPointF pt((event->position().x() - page.left()) / zoom, (event->position().y() - page.top()) / zoom);
        for (int i = _layout.sheet.cells.size() - 1; i >= 0; --i)   // the last cell is on top
        {
            if (_layout.sheet.cells[i].contains(pt))
            {
                hit = i;
                break;
            }
        }
    }
    setSelectedCell(hit);
    emit cellClicked(hit);
}

} // namespace Pdf3D
