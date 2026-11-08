#include "SheetPreviewWidget.h"

#include <QKeyEvent>
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
constexpr double kEdgeGrabPixels = 6.0;     // how close to an edge counts as grabbing it
constexpr double kSnapPixels = 6.0;         // how close an edge must come to a guide to snap to it
constexpr double kDragStartPixels = 3.0;    // the pointer must move this far before a click becomes a drag
constexpr double kMinimumCellPoints = 6.0;
}

SheetPreviewWidget::SheetPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
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

void SheetPreviewWidget::setEditable(bool editable)
{
    _editable = editable;
    if (editable)
        setFocusPolicy(Qt::StrongFocus);
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

double SheetPreviewWidget::zoom() const
{
    const QRectF page = pageRect();
    const QSizeF pagePt = sheetPageSizePt(_tpl);
    return (page.isEmpty() || pagePt.width() <= 0.0) ? 1.0 : page.width() / pagePt.width();
}

QPointF SheetPreviewWidget::toPagePoint(const QPointF& widgetPos) const
{
    const QRectF page = pageRect();
    const double z = zoom();
    return QPointF((widgetPos.x() - page.left()) / z, (widgetPos.y() - page.top()) / z);
}

QRectF SheetPreviewWidget::blockRectOf(int cell) const
{
    if (cell < 0 || cell >= _layout.sheet.cells.size())
        return QRectF();
    return _layout.sheet.cells[cell].translated(-_layout.sheet.titleBlock.topLeft());
}

QRectF SheetPreviewWidget::toWidgetRect(const QRectF& blockRect) const
{
    const QRectF page = pageRect();
    const double z = zoom();
    const QRectF onPage = blockRect.translated(_layout.sheet.titleBlock.topLeft());
    return QRectF(page.left() + onPage.left() * z, page.top() + onPage.top() * z, onPage.width() * z, onPage.height() * z);
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

// Which edges of the cell the position is near (all false: inside it, to move it).
void SheetPreviewWidget::zoneAt(const QPointF& widgetPos, int cell, bool* l, bool* r, bool* t, bool* b) const
{
    *l = *r = *t = *b = false;
    const QRectF px = toWidgetRect(blockRectOf(cell));
    if (px.isEmpty())
        return;
    // an edge is only grabbed from inside the cell (or just outside it), and a cell too small to tell apart can only be moved
    const double tolX = std::min(kEdgeGrabPixels, px.width() / 3.0), tolY = std::min(kEdgeGrabPixels, px.height() / 3.0);
    const bool inX = widgetPos.x() >= px.left() - tolX && widgetPos.x() <= px.right() + tolX;
    const bool inY = widgetPos.y() >= px.top() - tolY && widgetPos.y() <= px.bottom() + tolY;
    if (!inX || !inY)
        return;
    *l = std::fabs(widgetPos.x() - px.left()) <= tolX;
    *r = !*l && std::fabs(widgetPos.x() - px.right()) <= tolX;
    *t = std::fabs(widgetPos.y() - px.top()) <= tolY;
    *b = !*t && std::fabs(widgetPos.y() - px.bottom()) <= tolY;
}

void SheetPreviewWidget::updateCursor(const QPointF& widgetPos)
{
    if (!_editable || _selected < 0)
    {
        unsetCursor();
        return;
    }
    const QRectF px = toWidgetRect(blockRectOf(_selected));
    if (!px.adjusted(-kEdgeGrabPixels, -kEdgeGrabPixels, kEdgeGrabPixels, kEdgeGrabPixels).contains(widgetPos))
    {
        unsetCursor();
        return;
    }
    bool l, r, t, b;
    zoneAt(widgetPos, _selected, &l, &r, &t, &b);
    if ((l && t) || (r && b))
        setCursor(Qt::SizeFDiagCursor);
    else if ((r && t) || (l && b))
        setCursor(Qt::SizeBDiagCursor);
    else if (l || r)
        setCursor(Qt::SizeHorCursor);
    else if (t || b)
        setCursor(Qt::SizeVerCursor);
    else
        setCursor(Qt::SizeAllCursor);
}

// The rectangle after snapping its moving edges to the title block and to the other cells' edges, kept inside the block and not smaller
// than a few points.
QRectF SheetPreviewWidget::snapped(QRectF r, const Drag& drag) const
{
    const QSizeF block = _layout.sheet.titleBlock.size();
    const double tolerance = kSnapPixels / zoom();
    QVector<double> xs{ 0.0, block.width() }, ys{ 0.0, block.height() };
    for (int i = 0; i < _layout.sheet.cells.size(); ++i)
    {
        if (i == drag.cell)
            continue;
        const QRectF o = blockRectOf(i);
        xs << o.left() << o.right();
        ys << o.top() << o.bottom();
    }
    // the shift that brings `value` onto the nearest guide within the tolerance (0 when there is none)
    const auto pull = [tolerance](double value, const QVector<double>& guides, bool* found) {
        double best = 0.0;
        double bestDistance = tolerance + 1.0;
        for (double g : guides)
        {
            const double d = g - value;
            if (std::fabs(d) <= tolerance && std::fabs(d) < bestDistance)
            {
                best = d;
                bestDistance = std::fabs(d);
            }
        }
        *found = bestDistance <= tolerance;
        return best;
    };
    if (drag.move)
    {
        bool foundL, foundR;
        const double dl = pull(r.left(), xs, &foundL), dr = pull(r.right(), xs, &foundR);
        if (foundL || foundR)
            r.translate((foundL && (!foundR || std::fabs(dl) <= std::fabs(dr))) ? dl : dr, 0.0);
        bool foundT, foundB;
        const double dt = pull(r.top(), ys, &foundT), db = pull(r.bottom(), ys, &foundB);
        if (foundT || foundB)
            r.translate(0.0, (foundT && (!foundB || std::fabs(dt) <= std::fabs(db))) ? dt : db);
        // keep the whole cell inside the block
        r.translate(std::max(0.0, -r.left()) - std::max(0.0, r.right() - block.width()), std::max(0.0, -r.top()) - std::max(0.0, r.bottom() - block.height()));
    }
    else
    {
        bool found;
        if (drag.left)
            r.setLeft(r.left() + pull(r.left(), xs, &found));
        if (drag.right)
            r.setRight(r.right() + pull(r.right(), xs, &found));
        if (drag.top)
            r.setTop(r.top() + pull(r.top(), ys, &found));
        if (drag.bottom)
            r.setBottom(r.bottom() + pull(r.bottom(), ys, &found));
        r.setLeft(std::clamp(r.left(), 0.0, block.width()));
        r.setRight(std::clamp(r.right(), 0.0, block.width()));
        r.setTop(std::clamp(r.top(), 0.0, block.height()));
        r.setBottom(std::clamp(r.bottom(), 0.0, block.height()));
        // never smaller than a few points: the edge being dragged stops there
        if (r.width() < kMinimumCellPoints)
        {
            if (drag.left)
                r.setLeft(r.right() - kMinimumCellPoints);
            else
                r.setRight(r.left() + kMinimumCellPoints);
        }
        if (r.height() < kMinimumCellPoints)
        {
            if (drag.top)
                r.setTop(r.bottom() - kMinimumCellPoints);
            else
                r.setBottom(r.top() + kMinimumCellPoints);
        }
    }
    return r;
}

void SheetPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (_editable)
        setFocus();
    const QRectF page = pageRect();
    int hit = -1;
    if (!page.isEmpty() && page.contains(event->position()))
    {
        const QPointF pt = toPagePoint(event->position());
        // the selected cell's edges can be grabbed from just outside it; otherwise the last cell under the pointer is on top
        bool l, r, t, b;
        if (_editable && _selected >= 0)
        {
            zoneAt(event->position(), _selected, &l, &r, &t, &b);
            if (l || r || t || b)
                hit = _selected;
        }
        for (int i = _layout.sheet.cells.size() - 1; hit < 0 && i >= 0; --i)
        {
            if (_layout.sheet.cells[i].contains(pt))
                hit = i;
        }
    }
    setSelectedCell(hit);
    emit cellClicked(hit);

    _drag = Drag();
    if (_editable && hit >= 0 && event->button() == Qt::LeftButton)
    {
        _drag.cell = hit;
        _drag.pressPos = event->position();
        _drag.startRect = blockRectOf(hit);
        zoneAt(event->position(), hit, &_drag.left, &_drag.right, &_drag.top, &_drag.bottom);
        _drag.move = !(_drag.left || _drag.right || _drag.top || _drag.bottom);
    }
}

void SheetPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (_drag.cell >= 0 && (event->buttons() & Qt::LeftButton))
    {
        const QPointF deltaPx = event->position() - _drag.pressPos;
        if (!_drag.active && std::hypot(deltaPx.x(), deltaPx.y()) < kDragStartPixels)
            return;
        _drag.active = true;
        const double z = zoom();
        QRectF r = _drag.startRect;
        const QPointF delta(deltaPx.x() / z, deltaPx.y() / z);
        if (_drag.move)
            r.translate(delta);
        else
        {
            if (_drag.left)
                r.setLeft(r.left() + delta.x());
            if (_drag.right)
                r.setRight(r.right() + delta.x());
            if (_drag.top)
                r.setTop(r.top() + delta.y());
            if (_drag.bottom)
                r.setBottom(r.bottom() + delta.y());
        }
        emit cellGeometryChanged(_drag.cell, snapped(r, _drag), _layout.sheet.titleBlock.size());
        return;
    }
    updateCursor(event->position());
}

void SheetPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    _drag = Drag();
    updateCursor(event->position());
}

void SheetPreviewWidget::keyPressEvent(QKeyEvent* event)
{
    if (!_editable || _selected < 0)
    {
        QWidget::keyPressEvent(event);
        return;
    }
    const double step = (event->modifiers() & Qt::ShiftModifier) ? 5.0 : 1.0;
    QPointF delta;
    switch (event->key())
    {
    case Qt::Key_Left:  delta = QPointF(-step, 0.0); break;
    case Qt::Key_Right: delta = QPointF(step, 0.0); break;
    case Qt::Key_Up:    delta = QPointF(0.0, -step); break;
    case Qt::Key_Down:  delta = QPointF(0.0, step); break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    Drag nudge;
    nudge.cell = _selected;
    nudge.move = true;
    const QSizeF block = _layout.sheet.titleBlock.size();
    QRectF r = blockRectOf(_selected).translated(delta);
    r.translate(std::max(0.0, -r.left()) - std::max(0.0, r.right() - block.width()), std::max(0.0, -r.top()) - std::max(0.0, r.bottom() - block.height()));
    emit cellGeometryChanged(_selected, r, block);
    event->accept();
}

} // namespace Pdf3D
