#include "SheetTemplate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace Pdf3D
{

namespace
{

constexpr int kMaxJsonBytes = 256 * 1024;
constexpr int kMaxCells = 64;
constexpr int kMaxLines = 64;
constexpr int kMaxFields = 16;
constexpr int kMaxTextLength = 500;
constexpr int kMaxNameLength = 80;
constexpr double kPointsPerMm = 72.0 / 25.4;
constexpr double kTolerance = 0.01;   // points

// The labels of the built-in templates are translated with the context the exporter's own strings already use.
// QT_TRANSLATE_NOOP keeps them visible to lupdate.
[[maybe_unused]] const char* const kBuiltInLabels[] = {
    QT_TRANSLATE_NOOP("Pdf3DExporter", "Drawing no."),
    QT_TRANSLATE_NOOP("Pdf3DExporter", "Revision"),
    QT_TRANSLATE_NOOP("Pdf3DExporter", "Author"),
    QT_TRANSLATE_NOOP("Pdf3DExporter", "Date"),
    QT_TRANSLATE_NOOP("Pdf3DExporter", "Company"),
    QT_TRANSLATE_NOOP("Pdf3DExporter", "Scale | parts | sheet"),
};

const char* kDefaultJson = R"JSON({
  "version": 1,
  "name": "Default",
  "units": "pt",
  "page": { "size": "A4", "orientation": "landscape" },
  "border": { "inset": 20 },
  "margin": 28,
  "areaGap": 12,
  "lineWidth": 0.6,
  "titleBlock": {
    "anchor": "bottom",
    "height": 90,
    "maxHeight": 120,
    "growWithCell": "notes",
    "growPadding": 6,
    "lines": [
      { "x1": -360, "y1": 0, "x2": -360, "y2": "3/3" },
      { "x1": -180, "y1": 0, "x2": -180, "y2": "3/3" },
      { "x1": -360, "y1": "1/3", "x2": "3/3", "y2": "1/3" },
      { "x1": -360, "y1": "2/3", "x2": "3/3", "y2": "2/3" }
    ],
    "cells": [
      { "id": "title", "left": 10, "right": -370, "top": 6, "bottom": 34, "text": "{title}", "fontSize": 15, "bold": true },
      { "id": "subtitle", "left": 10, "right": -370, "top": 36, "bottom": 50, "text": "{subtitle}", "fontSize": 9 },
      { "id": "notes", "left": 10, "right": -370, "top": 52, "bottom": -4, "text": "{notes}", "fontSize": 8,
        "color": "#464646", "wrap": true, "scrollWhenOverflow": true },
      { "left": -360, "right": -180, "top": 0, "bottom": "1/3", "label": "Drawing no.", "text": "{drawingNumber}" },
      { "left": -180, "right": "3/3", "top": 0, "bottom": "1/3", "label": "Revision", "text": "{revision}" },
      { "left": -360, "right": -180, "top": "1/3", "bottom": "2/3", "label": "Author", "text": "{author}" },
      { "left": -180, "right": "3/3", "top": "1/3", "bottom": "2/3", "label": "Date", "text": "{date}" },
      { "left": -360, "right": -180, "top": "2/3", "bottom": "3/3", "label": "Company", "text": "{company}" },
      { "left": -180, "right": "3/3", "top": "2/3", "bottom": "3/3", "label": "Scale | parts | sheet",
        "text": "{scale|-}  |  {parts}  |  {sheet}/{sheets}" }
    ]
  }
})JSON";

// The same block on an A3 sheet.
const char* kA3Json = R"JSON({
  "version": 1,
  "name": "A3 landscape",
  "units": "pt",
  "page": { "size": "A3", "orientation": "landscape" },
  "border": { "inset": 20 },
  "margin": 28,
  "areaGap": 12,
  "lineWidth": 0.6,
  "titleBlock": {
    "anchor": "bottom",
    "height": 90,
    "maxHeight": 120,
    "growWithCell": "notes",
    "growPadding": 6,
    "lines": [
      { "x1": -360, "y1": 0, "x2": -360, "y2": "3/3" },
      { "x1": -180, "y1": 0, "x2": -180, "y2": "3/3" },
      { "x1": -360, "y1": "1/3", "x2": "3/3", "y2": "1/3" },
      { "x1": -360, "y1": "2/3", "x2": "3/3", "y2": "2/3" }
    ],
    "cells": [
      { "id": "title", "left": 10, "right": -370, "top": 6, "bottom": 34, "text": "{title}", "fontSize": 15, "bold": true },
      { "id": "subtitle", "left": 10, "right": -370, "top": 36, "bottom": 50, "text": "{subtitle}", "fontSize": 9 },
      { "id": "notes", "left": 10, "right": -370, "top": 52, "bottom": -4, "text": "{notes}", "fontSize": 8,
        "color": "#464646", "wrap": true, "scrollWhenOverflow": true },
      { "left": -360, "right": -180, "top": 0, "bottom": "1/3", "label": "Drawing no.", "text": "{drawingNumber}" },
      { "left": -180, "right": "3/3", "top": 0, "bottom": "1/3", "label": "Revision", "text": "{revision}" },
      { "left": -360, "right": -180, "top": "1/3", "bottom": "2/3", "label": "Author", "text": "{author}" },
      { "left": -180, "right": "3/3", "top": "1/3", "bottom": "2/3", "label": "Date", "text": "{date}" },
      { "left": -360, "right": -180, "top": "2/3", "bottom": "3/3", "label": "Company", "text": "{company}" },
      { "left": -180, "right": "3/3", "top": "2/3", "bottom": "3/3", "label": "Scale | parts | sheet",
        "text": "{scale|-}  |  {parts}  |  {sheet}/{sheets}" }
    ]
  }
})JSON";

struct PageSizeName
{
    const char* name;
    QPageSize::PageSizeId id;
};

const PageSizeName kPageSizes[] = {
    { "A0", QPageSize::A0 }, { "A1", QPageSize::A1 }, { "A2", QPageSize::A2 }, { "A3", QPageSize::A3 }, { "A4", QPageSize::A4 },
    { "A5", QPageSize::A5 }, { "Letter", QPageSize::Letter }, { "Legal", QPageSize::Legal }, { "Tabloid", QPageSize::Tabloid },
};

const QRegularExpression& placeholderPattern()
{
    static const QRegularExpression re(QStringLiteral("\\{([A-Za-z][A-Za-z0-9_]*)(?:\\|([^}]*))?\\}"));
    return re;
}

const QRegularExpression& identifierPattern()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,31}$"));
    return re;
}

struct Parser
{
    double unitToPt = kPointsPerMm;
    QString error;

    bool fail(const QString& message)
    {
        if (error.isEmpty())
            error = message;
        return false;
    }

    bool number(const QJsonValue& v, const QString& what, double* out, double minimum, double maximum)
    {
        if (!v.isDouble())
            return fail(QStringLiteral("%1 must be a number").arg(what));
        const double d = v.toDouble();
        if (!std::isfinite(d) || d < minimum || d > maximum)
            return fail(QStringLiteral("%1 is out of range").arg(what));
        *out = d;
        return true;
    }

    bool length(const QJsonValue& v, const QString& what, double* outPt, double minimumPt, double maximumPt)
    {
        double d = 0.0;
        if (!number(v, what, &d, -1e6, 1e6))
            return false;
        d *= unitToPt;
        if (d < minimumPt || d > maximumPt)
            return fail(QStringLiteral("%1 is out of range").arg(what));
        *outPt = d;
        return true;
    }

    bool edge(const QJsonValue& v, const QString& what, SheetEdge* out)
    {
        if (v.isDouble())
        {
            const double d = v.toDouble();
            if (!std::isfinite(d) || std::abs(d) > 1e5)
                return fail(QStringLiteral("%1 is out of range").arg(what));
            out->value = d * unitToPt;
            out->fraction = false;
            return true;
        }
        if (v.isString())
        {
            const QString s = v.toString().trimmed();
            double f = 0.0;
            bool ok = false;
            if (s.endsWith(QLatin1Char('%')))
            {
                f = s.left(s.size() - 1).toDouble(&ok) / 100.0;
            }
            else if (s.contains(QLatin1Char('/')))
            {
                const QStringList parts = s.split(QLatin1Char('/'));
                bool okA = false, okB = false;
                const double a = parts.value(0).toDouble(&okA), b = parts.value(1).toDouble(&okB);
                ok = parts.size() == 2 && okA && okB && b != 0.0;
                if (ok)
                    f = a / b;
            }
            if (!ok || !std::isfinite(f) || std::abs(f) > 1.0 + 1e-9)
                return fail(QStringLiteral("%1 must be a number, a fraction like \"1/3\" or a percentage like \"50%\" (within 100%)").arg(what));
            out->value = f;
            out->fraction = true;
            return true;
        }
        return fail(QStringLiteral("%1 must be a number, a fraction or a percentage").arg(what));
    }

    bool rect(const QJsonObject& o, const QString& what, SheetRect* out)
    {
        const struct { const char* key; SheetEdge SheetRect::*member; } keys[] = {
            { "left", &SheetRect::left }, { "right", &SheetRect::right }, { "top", &SheetRect::top }, { "bottom", &SheetRect::bottom } };
        for (const auto& k : keys)
        {
            if (!o.contains(QLatin1String(k.key)))
                return fail(QStringLiteral("%1 is missing \"%2\"").arg(what, QLatin1String(k.key)));
            if (!edge(o.value(QLatin1String(k.key)), QStringLiteral("%1 \"%2\"").arg(what, QLatin1String(k.key)), &(out->*k.member)))
                return false;
        }
        return true;
    }

    bool text(const QJsonValue& v, const QString& what, QString* out, int maxLength)
    {
        if (!v.isString())
            return fail(QStringLiteral("%1 must be a string").arg(what));
        const QString s = v.toString();
        if (s.size() > maxLength)
            return fail(QStringLiteral("%1 is longer than %2 characters").arg(what).arg(maxLength));
        *out = s;
        return true;
    }
};

} // namespace

double resolveSheetEdge(const SheetEdge& edge, double size)
{
    const double c = edge.fraction ? edge.value * size : edge.value;
    return c < 0.0 ? size + c : c;
}

QStringList standardSheetPlaceholders()
{
    return { QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("notes"), QStringLiteral("drawingNumber"),
             QStringLiteral("revision"), QStringLiteral("author"), QStringLiteral("company"), QStringLiteral("scale"),
             QStringLiteral("date"), QStringLiteral("parts"), QStringLiteral("sheet"), QStringLiteral("sheets") };
}

QSet<QString> usedSheetPlaceholders(const SheetTemplate& tpl)
{
    QSet<QString> names;
    for (const SheetCell& cell : tpl.cells)
    {
        auto it = placeholderPattern().globalMatch(cell.text);
        while (it.hasNext())
            names.insert(it.next().captured(1));
    }
    return names;
}

QString substituteSheetText(const QString& text, const QMap<QString, QString>& values, QStringList* unknown)
{
    QString result;
    int last = 0;
    auto it = placeholderPattern().globalMatch(text);
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        result += text.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString name = m.captured(1);
        const auto found = values.constFind(name);
        if (found == values.constEnd())
        {
            if (unknown && !unknown->contains(name))
                unknown->append(name);
            continue;
        }
        result += found.value().isEmpty() ? m.captured(2) : found.value();
    }
    result += text.mid(last);
    return result;
}

QSizeF sheetPageSizePt(const SheetTemplate& tpl)
{
    QSizeF mm = tpl.customSizeMm;
    if (tpl.pageSizeId != QPageSize::Custom)
        mm = QPageSize::size(tpl.pageSizeId, QPageSize::Millimeter);
    double w = mm.width() * kPointsPerMm, h = mm.height() * kPointsPerMm;
    if (tpl.landscape != (w > h))
        std::swap(w, h);
    return QSizeF(w, h);
}

ResolvedSheet resolveSheet(const SheetTemplate& tpl, const QSizeF& pageSizePt, double blockHeightPt)
{
    ResolvedSheet r;
    r.page = QRectF(QPointF(0, 0), pageSizePt);
    r.border = r.page.adjusted(tpl.borderInset, tpl.borderInset, -tpl.borderInset, -tpl.borderInset);

    const double fullWidth = pageSizePt.width() - 2.0 * tpl.margin;
    const double width = tpl.anchor == TitleBlockAnchor::Bottom ? fullWidth : std::min(tpl.blockWidth, fullWidth);
    const double left = tpl.anchor == TitleBlockAnchor::BottomRight ? pageSizePt.width() - tpl.margin - width : tpl.margin;
    r.titleBlock = QRectF(left, pageSizePt.height() - tpl.margin - blockHeightPt, width, blockHeightPt);
    r.area3d = QRectF(QPointF(tpl.margin, tpl.margin), QPointF(pageSizePt.width() - tpl.margin, r.titleBlock.top() - tpl.areaGap));

    const auto rectOf = [&](const SheetRect& e) {
        const double x0 = resolveSheetEdge(e.left, r.titleBlock.width()), x1 = resolveSheetEdge(e.right, r.titleBlock.width());
        const double y0 = resolveSheetEdge(e.top, r.titleBlock.height()), y1 = resolveSheetEdge(e.bottom, r.titleBlock.height());
        return QRectF(QPointF(r.titleBlock.left() + x0, r.titleBlock.top() + y0), QPointF(r.titleBlock.left() + x1, r.titleBlock.top() + y1));
    };
    r.cells.reserve(tpl.cells.size());
    for (const SheetCell& c : tpl.cells)
        r.cells.append(rectOf(c.rect));
    for (const SheetLine& l : tpl.lines)
    {
        r.lines.append(QLineF(r.titleBlock.left() + resolveSheetEdge(l.x1, r.titleBlock.width()), r.titleBlock.top() + resolveSheetEdge(l.y1, r.titleBlock.height()),
                              r.titleBlock.left() + resolveSheetEdge(l.x2, r.titleBlock.width()), r.titleBlock.top() + resolveSheetEdge(l.y2, r.titleBlock.height())));
    }
    if (tpl.hasLogo)
        r.logo = rectOf(tpl.logo.rect);
    return r;
}

double titleBlockHeightFor(const SheetTemplate& tpl, const QSizeF&, double neededTextHeightPt, bool* overflows)
{
    if (overflows)
        *overflows = false;
    if (tpl.growWithCell.isEmpty())
        return tpl.blockHeight;
    for (const SheetCell& c : tpl.cells)
    {
        if (c.id != tpl.growWithCell)
            continue;
        const double top = resolveSheetEdge(c.rect.top, tpl.blockHeight);
        const double wanted = top + neededTextHeightPt + tpl.growPadding;
        if (overflows)
            *overflows = wanted > tpl.blockMaxHeight;
        return std::clamp(wanted, tpl.blockHeight, tpl.blockMaxHeight);
    }
    return tpl.blockHeight;
}

bool parseSheetTemplate(const QByteArray& json, const QString& id, SheetTemplate* out, QString* error, QStringList* warnings)
{
    Parser p;
    SheetTemplate t;
    t.id = id;
    const auto finish = [&](bool ok) {
        if (!ok && error)
            *error = p.error.isEmpty() ? QStringLiteral("invalid template") : p.error;
        if (ok && out)
            *out = t;
        return ok;
    };

    if (json.size() > kMaxJsonBytes)
        return p.fail(QStringLiteral("the file is larger than %1 KiB").arg(kMaxJsonBytes / 1024)), finish(false);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull() || !doc.isObject())
        return p.fail(QStringLiteral("not valid JSON (%1)").arg(parseError.errorString())), finish(false);
    const QJsonObject root = doc.object();

    if (root.value(QLatin1String("version")).toInt(-1) != 1)
        return p.fail(QStringLiteral("\"version\" must be 1")), finish(false);
    t.version = 1;

    if (!p.text(root.value(QLatin1String("name")), QStringLiteral("\"name\""), &t.name, kMaxNameLength) || t.name.trimmed().isEmpty())
        return p.fail(QStringLiteral("\"name\" must be a non-empty string")), finish(false);

    const QString units = root.value(QLatin1String("units")).toString(QStringLiteral("mm"));
    if (units == QLatin1String("pt"))
        p.unitToPt = 1.0;
    else if (units == QLatin1String("mm"))
        p.unitToPt = kPointsPerMm;
    else
        return p.fail(QStringLiteral("\"units\" must be \"mm\" or \"pt\"")), finish(false);

    // page
    const QJsonObject page = root.value(QLatin1String("page")).toObject();
    const QJsonValue size = page.value(QLatin1String("size"));
    if (size.isString())
    {
        bool known = false;
        for (const PageSizeName& n : kPageSizes)
            if (size.toString() == QLatin1String(n.name))
            {
                t.pageSizeId = n.id;
                known = true;
            }
        if (!known)
            return p.fail(QStringLiteral("unknown page size \"%1\"").arg(size.toString())), finish(false);
    }
    else if (size.isArray() && size.toArray().size() == 2)
    {
        double w = 0.0, h = 0.0;
        if (!p.number(size.toArray().at(0), QStringLiteral("page width"), &w, 0, 1e6) || !p.number(size.toArray().at(1), QStringLiteral("page height"), &h, 0, 1e6))
            return finish(false);
        const double wMm = w * p.unitToPt / kPointsPerMm, hMm = h * p.unitToPt / kPointsPerMm;
        if (wMm < 100.0 || hMm < 100.0 || wMm > 5000.0 || hMm > 5000.0)
            return p.fail(QStringLiteral("a custom page size must be between 100 mm and 5000 mm")), finish(false);
        t.pageSizeId = QPageSize::Custom;
        t.customSizeMm = QSizeF(wMm, hMm);
    }
    else
        return p.fail(QStringLiteral("\"page.size\" must be a name (A0-A5, Letter, Legal, Tabloid) or [width, height]")), finish(false);
    const QString orientation = page.value(QLatin1String("orientation")).toString(QStringLiteral("landscape"));
    if (orientation != QLatin1String("landscape") && orientation != QLatin1String("portrait"))
        return p.fail(QStringLiteral("\"page.orientation\" must be \"landscape\" or \"portrait\"")), finish(false);
    t.landscape = orientation == QLatin1String("landscape");
    const QSizeF pagePt = sheetPageSizePt(t);

    // frame
    if (root.contains(QLatin1String("border")) && !p.length(root.value(QLatin1String("border")).toObject().value(QLatin1String("inset")), QStringLiteral("\"border.inset\""), &t.borderInset, 0.0, 200.0))
        return finish(false);
    if (root.contains(QLatin1String("margin")) && !p.length(root.value(QLatin1String("margin")), QStringLiteral("\"margin\""), &t.margin, 0.0, 400.0))
        return finish(false);
    if (root.contains(QLatin1String("areaGap")) && !p.length(root.value(QLatin1String("areaGap")), QStringLiteral("\"areaGap\""), &t.areaGap, 0.0, 200.0))
        return finish(false);
    if (root.contains(QLatin1String("lineWidth")) && !p.number(root.value(QLatin1String("lineWidth")), QStringLiteral("\"lineWidth\""), &t.lineWidth, 0.05, 5.0))
        return finish(false);

    // title block
    const QJsonObject block = root.value(QLatin1String("titleBlock")).toObject();
    const QString anchor = block.value(QLatin1String("anchor")).toString(QStringLiteral("bottom"));
    if (anchor == QLatin1String("bottom"))
        t.anchor = TitleBlockAnchor::Bottom;
    else if (anchor == QLatin1String("bottom-left"))
        t.anchor = TitleBlockAnchor::BottomLeft;
    else if (anchor == QLatin1String("bottom-right"))
        t.anchor = TitleBlockAnchor::BottomRight;
    else
        return p.fail(QStringLiteral("\"titleBlock.anchor\" must be bottom, bottom-left or bottom-right")), finish(false);
    if (t.anchor != TitleBlockAnchor::Bottom)
    {
        if (!p.length(block.value(QLatin1String("width")), QStringLiteral("\"titleBlock.width\""), &t.blockWidth, 50.0, pagePt.width()))
            return finish(false);
    }
    if (!p.length(block.value(QLatin1String("height")), QStringLiteral("\"titleBlock.height\""), &t.blockHeight, 20.0, pagePt.height() / 2.0))
        return finish(false);
    t.blockMaxHeight = t.blockHeight;
    if (block.contains(QLatin1String("maxHeight")) && !p.length(block.value(QLatin1String("maxHeight")), QStringLiteral("\"titleBlock.maxHeight\""), &t.blockMaxHeight, 20.0, pagePt.height() / 2.0))
        return finish(false);
    if (t.blockMaxHeight < t.blockHeight)
        return p.fail(QStringLiteral("\"titleBlock.maxHeight\" is smaller than \"titleBlock.height\"")), finish(false);
    if (block.contains(QLatin1String("growPadding")) && !p.length(block.value(QLatin1String("growPadding")), QStringLiteral("\"titleBlock.growPadding\""), &t.growPadding, 0.0, 100.0))
        return finish(false);
    t.growWithCell = block.value(QLatin1String("growWithCell")).toString();

    const double blockWidth = t.anchor == TitleBlockAnchor::Bottom ? pagePt.width() - 2.0 * t.margin : std::min(t.blockWidth, pagePt.width() - 2.0 * t.margin);
    if (blockWidth <= 0.0)
        return p.fail(QStringLiteral("the margins leave no room for the title block")), finish(false);

    // A rectangle must be non-empty and inside the block at the nominal height.
    const auto insideBlock = [&](const SheetRect& r, const QString& what) {
        const double x0 = resolveSheetEdge(r.left, blockWidth), x1 = resolveSheetEdge(r.right, blockWidth);
        const double y0 = resolveSheetEdge(r.top, t.blockHeight), y1 = resolveSheetEdge(r.bottom, t.blockHeight);
        if (x1 <= x0 + kTolerance || y1 <= y0 + kTolerance)
            return p.fail(QStringLiteral("%1 has no area (left/right or top/bottom are not in order)").arg(what));
        if (x0 < -kTolerance || y0 < -kTolerance || x1 > blockWidth + kTolerance || y1 > t.blockHeight + kTolerance)
            return p.fail(QStringLiteral("%1 reaches outside the title block").arg(what));
        return true;
    };

    // fields (parsed first: the placeholder check needs their ids)
    const QJsonArray fieldArray = root.value(QLatin1String("fields")).toArray();
    if (fieldArray.size() > kMaxFields)
        return p.fail(QStringLiteral("more than %1 custom fields").arg(kMaxFields)), finish(false);
    QStringList knownNames = standardSheetPlaceholders();
    for (int i = 0; i < fieldArray.size(); ++i)
    {
        const QString what = QStringLiteral("field %1").arg(i + 1);
        const QJsonObject o = fieldArray.at(i).toObject();
        SheetField f;
        f.id = o.value(QLatin1String("id")).toString();
        if (!identifierPattern().match(f.id).hasMatch())
            return p.fail(QStringLiteral("%1: \"id\" must be letters, digits and underscores, starting with a letter").arg(what)), finish(false);
        if (knownNames.contains(f.id))
            return p.fail(QStringLiteral("%1: \"%2\" is already a placeholder name").arg(what, f.id)), finish(false);
        if (!p.text(o.value(QLatin1String("label")), QStringLiteral("%1 \"label\"").arg(what), &f.label, kMaxNameLength))
            return finish(false);
        f.type = o.value(QLatin1String("type")).toString(QStringLiteral("text"));
        if (f.type != QLatin1String("text") && f.type != QLatin1String("multiline") && f.type != QLatin1String("date") && f.type != QLatin1String("choice"))
            return p.fail(QStringLiteral("%1: \"type\" must be text, multiline, date or choice").arg(what)), finish(false);
        f.defaultValue = o.value(QLatin1String("default")).toString();
        if (f.type == QLatin1String("choice"))
        {
            const QJsonArray options = o.value(QLatin1String("options")).toArray();
            if (options.isEmpty() || options.size() > 32)
                return p.fail(QStringLiteral("%1: a choice needs 1 to 32 \"options\"").arg(what)), finish(false);
            for (const QJsonValue& v : options)
            {
                if (!v.isString() || v.toString().size() > kMaxNameLength)
                    return p.fail(QStringLiteral("%1: an option must be a short string").arg(what)), finish(false);
                f.options.append(v.toString());
            }
        }
        knownNames.append(f.id);
        t.fields.append(f);
    }

    // cells
    const QJsonArray cellArray = block.value(QLatin1String("cells")).toArray();
    if (cellArray.isEmpty() || cellArray.size() > kMaxCells)
        return p.fail(QStringLiteral("the title block needs 1 to %1 cells").arg(kMaxCells)), finish(false);
    int scrollCells = 0;
    QSet<QString> cellIds;
    for (int i = 0; i < cellArray.size(); ++i)
    {
        const QString what = QStringLiteral("cell %1").arg(i + 1);
        const QJsonObject o = cellArray.at(i).toObject();
        SheetCell c;
        c.id = o.value(QLatin1String("id")).toString();
        if (!c.id.isEmpty())
        {
            if (cellIds.contains(c.id))
                return p.fail(QStringLiteral("%1: the id \"%2\" is used twice").arg(what, c.id)), finish(false);
            cellIds.insert(c.id);
        }
        if (!p.rect(o, what, &c.rect) || !insideBlock(c.rect, what))
            return finish(false);
        if (o.contains(QLatin1String("text")) && !p.text(o.value(QLatin1String("text")), QStringLiteral("%1 \"text\"").arg(what), &c.text, kMaxTextLength))
            return finish(false);
        if (o.contains(QLatin1String("label")) && !p.text(o.value(QLatin1String("label")), QStringLiteral("%1 \"label\"").arg(what), &c.label, kMaxNameLength))
            return finish(false);
        if (o.contains(QLatin1String("fontSize")) && !p.number(o.value(QLatin1String("fontSize")), QStringLiteral("%1 \"fontSize\"").arg(what), &c.fontSize, 3.0, 72.0))
            return finish(false);
        c.bold = o.value(QLatin1String("bold")).toBool(false);
        c.wrap = o.value(QLatin1String("wrap")).toBool(false);
        c.scrollWhenOverflow = o.value(QLatin1String("scrollWhenOverflow")).toBool(false);
        scrollCells += c.scrollWhenOverflow ? 1 : 0;
        if (o.contains(QLatin1String("color")))
        {
            c.color = QColor(o.value(QLatin1String("color")).toString());
            if (!c.color.isValid())
                return p.fail(QStringLiteral("%1: \"color\" is not a colour like \"#464646\"").arg(what)), finish(false);
        }
        const QString align = o.value(QLatin1String("align")).toString(QStringLiteral("left"));
        if (align == QLatin1String("left"))
            c.align = Qt::AlignLeft;
        else if (align == QLatin1String("center"))
            c.align = Qt::AlignHCenter;
        else if (align == QLatin1String("right"))
            c.align = Qt::AlignRight;
        else
            return p.fail(QStringLiteral("%1: \"align\" must be left, center or right").arg(what)), finish(false);

        auto it = placeholderPattern().globalMatch(c.text);
        while (it.hasNext())
        {
            const QString name = it.next().captured(1);
            if (!knownNames.contains(name) && warnings)
                warnings->append(QStringLiteral("unknown placeholder {%1} in %2 (it prints nothing)").arg(name, what));
        }
        t.cells.append(c);
    }
    if (scrollCells > 1)
        return p.fail(QStringLiteral("only one cell may set \"scrollWhenOverflow\"")), finish(false);
    if (!t.growWithCell.isEmpty() && !cellIds.contains(t.growWithCell))
        return p.fail(QStringLiteral("\"titleBlock.growWithCell\" names no cell id (\"%1\")").arg(t.growWithCell)), finish(false);

    // lines
    const QJsonArray lineArray = block.value(QLatin1String("lines")).toArray();
    if (lineArray.size() > kMaxLines)
        return p.fail(QStringLiteral("more than %1 lines").arg(kMaxLines)), finish(false);
    for (int i = 0; i < lineArray.size(); ++i)
    {
        const QString what = QStringLiteral("line %1").arg(i + 1);
        const QJsonObject o = lineArray.at(i).toObject();
        SheetLine l;
        const struct { const char* key; SheetEdge SheetLine::*member; } keys[] = {
            { "x1", &SheetLine::x1 }, { "y1", &SheetLine::y1 }, { "x2", &SheetLine::x2 }, { "y2", &SheetLine::y2 } };
        for (const auto& k : keys)
        {
            if (!o.contains(QLatin1String(k.key)))
                return p.fail(QStringLiteral("%1 is missing \"%2\"").arg(what, QLatin1String(k.key))), finish(false);
            if (!p.edge(o.value(QLatin1String(k.key)), QStringLiteral("%1 \"%2\"").arg(what, QLatin1String(k.key)), &(l.*k.member)))
                return finish(false);
        }
        t.lines.append(l);
    }

    // logo
    if (block.contains(QLatin1String("logo")))
    {
        const QJsonObject o = block.value(QLatin1String("logo")).toObject();
        t.hasLogo = true;
        if (!p.text(o.value(QLatin1String("file")), QStringLiteral("logo \"file\""), &t.logo.file, 260) || t.logo.file.trimmed().isEmpty())
            return p.fail(QStringLiteral("logo \"file\" must be a file name or path")), finish(false);
        if (!p.rect(o, QStringLiteral("the logo"), &t.logo.rect) || !insideBlock(t.logo.rect, QStringLiteral("the logo")))
            return finish(false);
        const QString fit = o.value(QLatin1String("fit")).toString(QStringLiteral("contain"));
        if (fit != QLatin1String("contain") && fit != QLatin1String("stretch"))
            return p.fail(QStringLiteral("logo \"fit\" must be contain or stretch")), finish(false);
        t.logo.contain = fit == QLatin1String("contain");
    }

    return finish(true);
}

QString sheetLabelText(const SheetTemplate& tpl, const QString& label)
{
    if (!tpl.folder.isEmpty() || !tpl.id.startsWith(QLatin1String("builtin:")))
        return label;
    return QCoreApplication::translate("Pdf3DExporter", label.toUtf8().constData());
}

QVector<SheetTemplate> builtInSheetTemplates()
{
    QVector<SheetTemplate> list;
    const struct { const char* id; const char* json; } sources[] = {
        { "builtin:default", kDefaultJson }, { "builtin:a3-landscape", kA3Json } };
    for (const auto& s : sources)
    {
        SheetTemplate t;
        QString error;
        if (parseSheetTemplate(QByteArray(s.json), QString::fromLatin1(s.id), &t, &error))
            list.append(t);
        else
            qWarning("Built-in sheet template %s is invalid: %s", s.id, qPrintable(error));
    }
    return list;
}

QVector<SheetTemplate> loadUserSheetTemplates(const QString& folder, QStringList* problems)
{
    QVector<SheetTemplate> list;
    const QDir dir(folder);
    if (folder.isEmpty() || !dir.exists())
        return list;
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files)
    {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
        {
            if (problems)
                problems->append(QStringLiteral("%1: cannot be read").arg(info.fileName()));
            continue;
        }
        SheetTemplate t;
        QString error;
        QStringList warnings;
        if (!parseSheetTemplate(file.read(kMaxJsonBytes + 1), QStringLiteral("user:") + info.completeBaseName(), &t, &error, &warnings))
        {
            if (problems)
                problems->append(QStringLiteral("%1: %2").arg(info.fileName(), error));
            continue;
        }
        t.folder = info.absolutePath();
        if (problems)
        {
            for (const QString& w : warnings)
                problems->append(QStringLiteral("%1: warning: %2").arg(info.fileName(), w));
            if (t.hasLogo)
            {
                const QFileInfo logo(QFileInfo(t.logo.file).isAbsolute() ? t.logo.file : dir.absoluteFilePath(t.logo.file));
                if (!logo.isFile())
                    problems->append(QStringLiteral("%1: warning: the logo file \"%2\" was not found (it is left out)").arg(info.fileName(), t.logo.file));
            }
        }
        list.append(t);
    }
    return list;
}

QString userSheetTemplateFolder()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("pdf3d-templates"));
}

SheetTemplate findSheetTemplate(const QString& id)
{
    const QVector<SheetTemplate> builtIn = builtInSheetTemplates();
    for (const SheetTemplate& t : builtIn)
        if (t.id == id)
            return t;
    if (id.startsWith(QLatin1String("user:")))
    {
        for (const SheetTemplate& t : loadUserSheetTemplates(userSheetTemplateFolder()))
            if (t.id == id)
                return t;
    }
    return builtIn.isEmpty() ? SheetTemplate() : builtIn.first();
}

} // namespace Pdf3D
