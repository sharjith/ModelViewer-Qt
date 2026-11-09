#pragma once

#include "SheetTemplate.h"

#include <QImage>
#include <QMap>
#include <QRectF>
#include <QString>

class QPainter;

// Drawing and layout of the 2D sheet around the 3D area, shared by the exporter (Pdf3DExporter.cpp) and the template preview, so the
// preview shows exactly what the PDF gets. Everything is in points; the painter's device must be 72 dpi (a QPdfWriter at 72 dpi, or
// a QImage with 72 dpi dots per metre) so that font sizes and text measurements agree between the two.
namespace Pdf3D
{

struct Pdf3DOptions;

// Every placeholder value of a sheet: the standard ones from the options (date: today when empty), `parts`, `sheet`, `sheets`, then
// the template's custom fields (options.fieldValues; they never replace a standard value).
QMap<QString, QString> sheetValues(const Pdf3DOptions& options, int partCount, int sheet, int sheetCount);

// The result of laying a template out for one page size and one set of values.
struct SheetLayout
{
    ResolvedSheet sheet;
    bool    scroll = false;        // the scroll cell's text does not fit: the exporter adds a scrollable form field over it
    QRectF  scrollRect;            // that cell
    QString scrollText;
    qreal   scrollFontSize = 8.0;
};

// Title block height from the text of the grow cell (measured with `measure`'s font metrics), the rectangles for that height, and
// the scroll cell. `values` are those of the first sheet.
SheetLayout layoutSheet(QPainter& measure, const SheetTemplate& tpl, const QSizeF& pagePt, const QMap<QString, QString>& values);

// Border, 3D area frame, title block with its lines, logo and cells. `area3d` is the 3D area as the exporter uses it (the template's
// 3D area minus the button strip).
void drawSheetFrame(QPainter& p, const SheetTemplate& tpl, const ResolvedSheet& sheet, const QRectF& area3d, const QMap<QString, QString>& values);

// A picture of the whole page (white, the 3D area greyed and labelled) `widthPx` wide, for the template preview. `selectedCell`
// (an index into tpl.cells, or -1) gets a blue frame. `layout` (may be null) receives the layout that was drawn.
QImage renderSheetPreview(const SheetTemplate& tpl, const QMap<QString, QString>& values, int widthPx, int selectedCell = -1, SheetLayout* layout = nullptr);

} // namespace Pdf3D
