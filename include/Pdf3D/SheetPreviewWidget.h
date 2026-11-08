#pragma once

#include "SheetRenderer.h"
#include "SheetTemplate.h"

#include <QImage>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QWidget>

namespace Pdf3D
{

// A picture of the sheet a template makes, drawn by the same code as the exporter (SheetRenderer.h): the page, the 3D area greyed out,
// the border, the title block with its cells filled with the current values, the logo. A click selects the cell under it. When the
// widget is editable the selected cell can also be dragged (to move it) or dragged by an edge or corner (to resize it), and nudged with
// the arrow keys; the edges snap to the title block and to the other cells. The widget does not change the template: it reports the
// new rectangle of the cell (in points from the title block's top left) and the owner applies it.
class SheetPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SheetPreviewWidget(QWidget* parent = nullptr);

    void setTemplate(const SheetTemplate& tpl);
    void setValues(const QMap<QString, QString>& values);
    void setSelectedCell(int index);   // -1: none; the cell gets a blue frame
    int  selectedCell() const { return _selected; }
    void setEditable(bool editable);   // drag and arrow keys change the cell's rectangle (reported through cellGeometryChanged)

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void cellClicked(int index);       // an index into the template's cells, or -1 for a click outside every cell
    // The cell `index` should now occupy `blockRect` (points, from the top left of the title block, which is `blockSize` big).
    void cellGeometryChanged(int index, const QRectF& blockRect, const QSizeF& blockSize);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Drag
    {
        int     cell = -1;
        bool    active = false;        // the pointer has moved far enough to count as a drag
        bool    move = true;           // otherwise the edges below are resized
        bool    left = false, right = false, top = false, bottom = false;
        QPointF pressPos;              // widget coordinates
        QRectF  startRect;             // the cell's rectangle in block coordinates when the button went down
    };

    QRectF pageRect() const;           // where the page is drawn, in widget coordinates (the widget minus a margin, keeping the aspect ratio)
    double zoom() const;               // widget pixels per point of the page
    QPointF toPagePoint(const QPointF& widgetPos) const;
    QRectF blockRectOf(int cell) const;   // the cell's rectangle in block coordinates (as drawn last)
    QRectF toWidgetRect(const QRectF& blockRect) const;
    void zoneAt(const QPointF& widgetPos, int cell, bool* l, bool* r, bool* t, bool* b) const;
    void updateCursor(const QPointF& widgetPos);
    QRectF snapped(QRectF rect, const Drag& drag) const;
    void render(const QSize& pixels, qreal devicePixelRatio);

    SheetTemplate _tpl;
    QMap<QString, QString> _values;
    int _selected = -1;
    bool _editable = false;
    Drag _drag;
    QImage _image;                     // the cached picture
    SheetLayout _layout;               // the layout that picture shows (hit testing, dragging)
    bool _dirty = true;
    QSize _imageSize;                  // the pixel size of the cached picture
};

} // namespace Pdf3D
