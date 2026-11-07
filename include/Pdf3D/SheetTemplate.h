#pragma once

#include <QColor>
#include <QLineF>
#include <QMap>
#include <QPageSize>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtCore/qnamespace.h>

// Sheet templates for Save as 3D PDF: the 2D sheet around the 3D area (page size, border, optional logo, title block cells with
// {placeholder} text and the custom fields the dialog asks for) described as JSON data. No expressions or scripts: a template can
// only substitute placeholders, so it cannot execute anything. Design: research/3dpdf/3D_PDF_EXPORT_RESEARCH.md, section 11.
//
// A coordinate inside the title block is measured from its top-left corner; a negative value is measured from the opposite edge
// (-127 as `right` is 127 units in from the right edge). A coordinate is a number in the template's units (points or millimetres)
// or a fraction of the block size, written "1/3" or "50%".
namespace Pdf3D
{

struct SheetEdge
{
    double value = 0.0;        // points, or a fraction of the block size when `fraction` is set
    bool   fraction = false;
};

struct SheetRect
{
    SheetEdge left, right, top, bottom;
};

struct SheetLine
{
    SheetEdge x1, y1, x2, y2;
};

struct SheetCell
{
    QString id;                 // optional; `growWithCell` refers to it
    QString label;              // small caption in the cell corner; a cell with a label is drawn as a field cell
    QString text;               // with {placeholders}; {name|fallback} prints the fallback when the value is empty
    SheetRect rect;
    double fontSize = 9.0;      // points
    bool bold = false;
    QColor color = QColor(Qt::black);
    Qt::Alignment align = Qt::AlignLeft;
    bool wrap = false;
    bool scrollWhenOverflow = false;   // overflowing text becomes a scrollable form field over the cell (one per template)
};

struct SheetLogo
{
    QString file;               // relative to the template's folder, or absolute; PNG, JPEG or SVG
    SheetRect rect;
    bool contain = true;        // keep the aspect ratio inside the rectangle
};

// An extra value the dialog asks for.
struct SheetField
{
    QString id;
    QString label;
    QString type = QStringLiteral("text");   // text, multiline, date, choice
    QString defaultValue;
    QStringList options;                      // type choice
};

enum class TitleBlockAnchor
{
    Bottom,        // full width between the margins
    BottomLeft,    // `blockWidth` wide, at the left margin
    BottomRight    // `blockWidth` wide, at the right margin
};

struct SheetTemplate
{
    int     version = 1;
    QString id;                 // "builtin:default", "user:<file name without .json>"
    QString name;               // shown in the dialog
    QString folder;             // where the file came from (for a relative logo path); empty for built-in templates

    QPageSize::PageSizeId pageSizeId = QPageSize::A4;   // Custom: customSizeMm
    QSizeF customSizeMm;
    bool   landscape = true;

    double borderInset = 20.0;  // points from the page edge to the border
    double margin = 28.0;       // points from the page edge to the title block and the 3D area
    double areaGap = 12.0;      // points between the 3D area and the title block
    double lineWidth = 0.6;     // points, for the border, the 3D area frame, the title block and its lines

    TitleBlockAnchor anchor = TitleBlockAnchor::Bottom;
    double blockWidth = 0.0;    // points; BottomLeft and BottomRight only
    double blockHeight = 90.0;  // points
    double blockMaxHeight = 120.0;   // the block grows up to this when the `growWithCell` text needs more room
    double growPadding = 6.0;   // points below the grown text
    QString growWithCell;       // id of the cell whose text height may enlarge the block (empty: fixed height)

    QVector<SheetLine> lines;
    bool hasLogo = false;
    SheetLogo logo;
    QVector<SheetCell> cells;
    QVector<SheetField> fields;
};

// Every rectangle of a sheet, in painter coordinates (points, origin top left), for one page size and one title block height.
struct ResolvedSheet
{
    QRectF page;
    QRectF border;
    QRectF titleBlock;
    QRectF area3d;              // above the title block; the exporter takes the button strip off its bottom
    QVector<QRectF> cells;      // same order as SheetTemplate::cells
    QVector<QLineF> lines;      // same order as SheetTemplate::lines
    QRectF logo;                // empty without a logo
};

// Parses and validates a template. On failure returns false with a message that names the offending key or cell. `warnings` (may be
// null) receives non-fatal findings such as an unknown placeholder or a missing logo file.
bool parseSheetTemplate(const QByteArray& json, const QString& id, SheetTemplate* out, QString* error, QStringList* warnings = nullptr);

// The page size in points for the template's size and orientation.
QSizeF sheetPageSizePt(const SheetTemplate& tpl);

// The rectangles for a page of `pageSizePt` and a title block of `blockHeightPt` (see titleBlockHeightFor).
ResolvedSheet resolveSheet(const SheetTemplate& tpl, const QSizeF& pageSizePt, double blockHeightPt);

// The title block height for a `growWithCell` text that needs `neededTextHeightPt`: the cell's top plus the text plus the padding,
// within [blockHeight, blockMaxHeight]. `overflows` (may be null) is set when even the maximum is too small. Without a grow cell
// the fixed height is returned.
double titleBlockHeightFor(const SheetTemplate& tpl, const QSizeF& pageSizePt, double neededTextHeightPt, bool* overflows = nullptr);

// One coordinate resolved against `size` (the block width or height).
double resolveSheetEdge(const SheetEdge& edge, double size);

// Placeholders.
QStringList standardSheetPlaceholders();   // title, subtitle, notes, drawingNumber, revision, author, company, scale, date, parts, sheet, sheets
QSet<QString> usedSheetPlaceholders(const SheetTemplate& tpl);
// Replaces {name} and {name|fallback}. A name that is not a key of `values` is reported in `unknown` (may be null) and prints nothing.
QString substituteSheetText(const QString& text, const QMap<QString, QString>& values, QStringList* unknown = nullptr);

// A cell label as shown: the built-in templates' labels are translated, a user template's labels are used as written.
QString sheetLabelText(const SheetTemplate& tpl, const QString& label);

// The built-in templates (Default A4 landscape, A3 landscape) and the user templates found in `folder` (*.json, sorted by name).
// A user file that fails to parse is left out and described in `problems` (may be null).
QVector<SheetTemplate> builtInSheetTemplates();
QVector<SheetTemplate> loadUserSheetTemplates(const QString& folder, QStringList* problems = nullptr);

// <AppDataLocation>/pdf3d-templates, where the user's templates live.
QString userSheetTemplateFolder();

// The template with this id (built-in, then the user folder); Default when the id is unknown.
SheetTemplate findSheetTemplate(const QString& id);

} // namespace Pdf3D
