#pragma once

#include "SheetRenderer.h"
#include "SheetTemplate.h"

#include <QImage>
#include <QMap>
#include <QString>
#include <QWidget>

namespace Pdf3D
{

// A picture of the sheet a template makes, drawn by the same code as the exporter (SheetRenderer.h): the page, the 3D area greyed out,
// the border, the title block with its cells filled with the current values, the logo. A click selects the cell under it.
class SheetPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SheetPreviewWidget(QWidget* parent = nullptr);

    void setTemplate(const SheetTemplate& tpl);
    void setValues(const QMap<QString, QString>& values);
    void setSelectedCell(int index);   // -1: none; the cell gets a blue frame
    int  selectedCell() const { return _selected; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void cellClicked(int index);       // an index into the template's cells, or -1 for a click outside every cell

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRectF pageRect() const;           // where the page is drawn, in widget coordinates (the widget minus a margin, keeping the aspect ratio)
    void render(const QSize& pixels, qreal devicePixelRatio);

    SheetTemplate _tpl;
    QMap<QString, QString> _values;
    int _selected = -1;
    QImage _image;                     // the cached picture
    SheetLayout _layout;               // the layout that picture shows (hit testing)
    bool _dirty = true;
    QSize _imageSize;                  // the pixel size of the cached picture
};

} // namespace Pdf3D
