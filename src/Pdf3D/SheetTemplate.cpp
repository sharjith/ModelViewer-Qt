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
#include <QSaveFile>
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

// A company-style sample written to the user folder by the dialog's "Create sample" button: a logo, two custom fields and a four-row
// grid. Millimetres, A4 landscape.
const char* kSampleJson = R"JSON({
  "version": 1,
  "name": "Company sample",
  "units": "mm",
  "page": { "size": "A4", "orientation": "landscape" },
  "border": { "inset": 7 },
  "margin": 10,
  "areaGap": 4,
  "lineWidth": 0.6,
  "titleBlock": {
    "anchor": "bottom",
    "height": 36,
    "maxHeight": 50,
    "growWithCell": "notes",
    "growPadding": 2,
    "lines": [
      { "x1": 45, "y1": 0, "x2": 45, "y2": "100%" },
      { "x1": -96, "y1": 0, "x2": -96, "y2": "100%" },
      { "x1": -48, "y1": 0, "x2": -48, "y2": "100%" },
      { "x1": -96, "y1": "1/4", "x2": "100%", "y2": "1/4" },
      { "x1": -96, "y1": "2/4", "x2": "100%", "y2": "2/4" },
      { "x1": -96, "y1": "3/4", "x2": "100%", "y2": "3/4" }
    ],
    "logo": { "file": "sample-logo.svg", "left": 3, "right": 43, "top": 3, "bottom": -3, "fit": "contain" },
    "cells": [
      { "id": "title", "left": 48, "right": -99, "top": 3, "bottom": 13, "text": "{title}", "fontSize": 15, "bold": true },
      { "id": "subtitle", "left": 48, "right": -99, "top": 13, "bottom": 19, "text": "{subtitle}", "fontSize": 9 },
      { "id": "notes", "left": 48, "right": -99, "top": 20, "bottom": -2, "text": "{notes}", "fontSize": 8,
        "color": "#464646", "wrap": true, "scrollWhenOverflow": true },
      { "left": -96, "right": -48, "top": 0, "bottom": "1/4", "label": "Drawing no.", "text": "{drawingNumber}" },
      { "left": -48, "right": "100%", "top": 0, "bottom": "1/4", "label": "Revision", "text": "{revision}" },
      { "left": -96, "right": -48, "top": "1/4", "bottom": "2/4", "label": "Author", "text": "{author}" },
      { "left": -48, "right": "100%", "top": "1/4", "bottom": "2/4", "label": "Approved by", "text": "{approvedBy}" },
      { "left": -96, "right": -48, "top": "2/4", "bottom": "3/4", "label": "Material", "text": "{material}" },
      { "left": -48, "right": "100%", "top": "2/4", "bottom": "3/4", "label": "Scale", "text": "{scale|-}" },
      { "left": -96, "right": -48, "top": "3/4", "bottom": "100%", "label": "Date", "text": "{date}" },
      { "left": -48, "right": "100%", "top": "3/4", "bottom": "100%", "label": "Sheet", "text": "{sheet}/{sheets}" }
    ]
  },
  "fields": [
    { "id": "approvedBy", "label": "Approved by", "type": "text" },
    { "id": "material", "label": "Material", "type": "choice", "options": ["Steel", "Aluminium", "Plastic", "Other"] }
  ]
})JSON";

// A plain geometric mark for the sample (no text, so it needs no font).
const char* kSampleLogoSvg = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="160" height="120" viewBox="0 0 160 120">
  <rect x="4" y="4" width="152" height="112" rx="14" fill="#1f5f8b"/>
  <circle cx="58" cy="60" r="30" fill="#ffffff"/>
  <rect x="84" y="30" width="52" height="60" rx="6" fill="#f2a900"/>
  <polygon points="58,38 80,60 58,82 36,60" fill="#1f5f8b"/>
</svg>
)SVG";

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

SheetRect sheetRectFromBlockRect(const QRectF& rect, const SheetRect& like, const QSizeF& blockSize)
{
    // one coordinate: measured from the near edge (positive) or from the far edge (negative), as a length or as a fraction of `size`
    const auto edge = [](double coordinate, const SheetEdge& style, double size) {
        SheetEdge e;
        e.fraction = style.fraction;
        const double offset = style.value < 0.0 ? coordinate - size : coordinate;
        e.value = style.fraction ? (size > 0.0 ? offset / size : 0.0) : offset;
        return e;
    };
    SheetRect out;
    out.left = edge(rect.left(), like.left, blockSize.width());
    out.right = edge(rect.right(), like.right, blockSize.width());
    out.top = edge(rect.top(), like.top, blockSize.height());
    out.bottom = edge(rect.bottom(), like.bottom, blockSize.height());
    return out;
}

bool parseSheetNumber(const QString& text, const QLocale& locale, double* value)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return false;
    bool ok = false;
    double v = locale.toDouble(s, &ok);
    if (!ok)
        v = QLocale::c().toDouble(s, &ok);   // a plain point is accepted whatever the language
    if (!ok || !std::isfinite(v))
        return false;
    if (value)
        *value = v;
    return true;
}

QString formatSheetNumber(const SheetField& field, double value, const QLocale& locale)
{
    QLocale plain = locale;
    plain.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);   // a sheet reads "1234.50 kg", not "1,234.50 kg"
    const QString number = plain.toString(value, 'f', std::clamp(field.decimals, 0, 6));
    return field.unit.isEmpty() ? number : number + QLatin1Char(' ') + field.unit;
}

double sheetBlockWidthPt(const SheetTemplate& tpl, const QSizeF& pageSizePt)
{
    const double available = pageSizePt.width() - 2.0 * tpl.margin;
    return tpl.anchor == TitleBlockAnchor::Bottom ? available : std::min(tpl.blockWidth, available);
}

double sheetBlockWidthPt(const SheetTemplate& tpl)
{
    return sheetBlockWidthPt(tpl, sheetPageSizePt(tpl));
}

SheetTemplate scaleSheetTemplateWidth(const SheetTemplate& tpl, double ratio)
{
    SheetTemplate out = tpl;
    const auto scale = [ratio](SheetEdge& e) {
        if (!e.fraction)
            e.value *= ratio;
    };
    for (SheetCell& c : out.cells)
    {
        scale(c.rect.left);
        scale(c.rect.right);
    }
    for (SheetLine& l : out.lines)
    {
        scale(l.x1);
        scale(l.x2);
    }
    if (out.hasLogo)
    {
        scale(out.logo.rect.left);
        scale(out.logo.rect.right);
    }
    return out;
}

QVector<QPair<QString, QPageSize::PageSizeId>> sheetPageSizeChoices()
{
    QVector<QPair<QString, QPageSize::PageSizeId>> choices;
    for (const PageSizeName& n : kPageSizes)
        choices.append({ QString::fromLatin1(n.name), n.id });
    return choices;
}

QString sheetEdgeToText(const SheetEdge& edge)
{
    if (edge.fraction)
    {
        const double magnitude = std::fabs(edge.value);
        if (magnitude < 1e-12)
            return QStringLiteral("0");
        const QString sign = edge.value < 0.0 ? QStringLiteral("-") : QString();
        for (int d = 2; d <= 12; ++d)
        {
            const double n = std::round(magnitude * d);
            if (n >= 1.0 && n < d && std::fabs(magnitude * d - n) < 1e-9)   // a proper fraction; whole values read better as a percentage
                return QStringLiteral("%1%2/%3").arg(sign).arg(static_cast<int>(n)).arg(d);
        }
        return QString::number(edge.value * 100.0, 'g', 6) + QLatin1Char('%');
    }
    // millimetres with at most two decimals ("3.53"), without trailing zeros
    QString text = QString::number(edge.value / kPointsPerMm, 'f', 2);
    if (text.contains(QLatin1Char('.')))
    {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

bool parseSheetEdgeText(const QString& text, SheetEdge* edge, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    QString s = text.trimmed();
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    static const QRegularExpression numberPattern(QStringLiteral("^-?\\d+(\\.\\d+)?$"));
    static const QRegularExpression ratioPattern(QStringLiteral("^(-?\\d+(?:\\.\\d+)?)\\s*/\\s*(\\d+(?:\\.\\d+)?)$"));
    static const QRegularExpression percentPattern(QStringLiteral("^(-?\\d+(?:\\.\\d+)?)\\s*%$"));
    SheetEdge parsed;
    const QRegularExpressionMatch ratio = ratioPattern.match(s);
    const QRegularExpressionMatch percent = percentPattern.match(s);
    if (numberPattern.match(s).hasMatch())
    {
        parsed.value = s.toDouble() * kPointsPerMm;
    }
    else if (ratio.hasMatch())
    {
        const double denominator = ratio.captured(2).toDouble();
        if (denominator == 0.0)
            return fail(QStringLiteral("a fraction cannot have 0 below the line"));
        parsed.value = ratio.captured(1).toDouble() / denominator;
        parsed.fraction = true;
    }
    else if (percent.hasMatch())
    {
        parsed.value = percent.captured(1).toDouble() / 100.0;
        parsed.fraction = true;
    }
    else
        return fail(QStringLiteral("write millimetres (12.5 or -99), a fraction (1/4) or a percentage (37.5%)"));
    if (std::fabs(parsed.value) > 100000.0)
        return fail(QStringLiteral("the number is too large"));
    if (edge)
        *edge = parsed;
    return true;
}

bool validateSheetTemplate(const SheetTemplate& tpl, QString* error)
{
    SheetTemplate check;
    QString message;
    const bool ok = parseSheetTemplate(sheetTemplateToJson(tpl), tpl.id, &check, &message);
    if (!ok && error)
        *error = message;
    return ok;
}

QStringList sceneSheetPlaceholders()
{
    return { QStringLiteral("material"), QStringLiteral("mass") };
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

    const double width = sheetBlockWidthPt(tpl, pageSizePt);
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
        constexpr double kEdgeSlack = 0.001;   // 100 mm written in points and read back may land a hair below 100
        if (wMm < 100.0 - kEdgeSlack || hMm < 100.0 - kEdgeSlack || wMm > 5000.0 + kEdgeSlack || hMm > 5000.0 + kEdgeSlack)
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
    const QJsonValue companion = page.value(QLatin1String("printCompanion"));
    if (!companion.isUndefined())
    {
        if (!p.text(companion, QStringLiteral("page.printCompanion"), &t.printCompanionId, 256))
            return finish(false);
        if (!t.printCompanionId.isEmpty() && !t.printCompanionId.startsWith(QLatin1String("user:")) &&
            !t.printCompanionId.startsWith(QLatin1String("builtin:")))
            return p.fail(QStringLiteral("page.printCompanion must be a sheet template ID")), finish(false);
    }
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

    const double blockWidth = sheetBlockWidthPt(t, pagePt);
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
        if (f.type != QLatin1String("text") && f.type != QLatin1String("multiline") && f.type != QLatin1String("date") && f.type != QLatin1String("choice")
            && f.type != QLatin1String("number"))
            return p.fail(QStringLiteral("%1: \"type\" must be text, multiline, date, choice or number").arg(what)), finish(false);
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
        if (f.type == QLatin1String("number"))
        {
            double decimals = 2.0;
            if (o.contains(QLatin1String("decimals")) && (!p.number(o.value(QLatin1String("decimals")), QStringLiteral("%1 \"decimals\"").arg(what), &decimals, 0.0, 6.0) || decimals != std::floor(decimals)))
                return p.fail(QStringLiteral("%1: \"decimals\" must be a whole number from 0 to 6").arg(what)), finish(false);
            f.decimals = static_cast<int>(decimals);
            if (o.contains(QLatin1String("unit")) && !p.text(o.value(QLatin1String("unit")), QStringLiteral("%1 \"unit\"").arg(what), &f.unit, 20))
                return finish(false);
            if (o.contains(QLatin1String("min")) && !p.number(o.value(QLatin1String("min")), QStringLiteral("%1 \"min\"").arg(what), &f.minimum, -1e9, 1e9))
                return finish(false);
            if (o.contains(QLatin1String("max")) && !p.number(o.value(QLatin1String("max")), QStringLiteral("%1 \"max\"").arg(what), &f.maximum, -1e9, 1e9))
                return finish(false);
            if (f.minimum > f.maximum)
                return p.fail(QStringLiteral("%1: the minimum is above the maximum").arg(what)), finish(false);
            double defaultNumber = 0.0;
            if (!f.defaultValue.isEmpty() && (!parseSheetNumber(f.defaultValue, QLocale::c(), &defaultNumber) || defaultNumber < f.minimum || defaultNumber > f.maximum))
                return p.fail(QStringLiteral("%1: the default must be a number within the limits, written with a point").arg(what)), finish(false);
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
            if (!knownNames.contains(name) && !sceneSheetPlaceholders().contains(name) && warnings)
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

namespace
{

QJsonValue edgeJson(const SheetEdge& e)
{
    if (e.fraction)
        return QString::number(e.value * 100.0, 'g', 15) + QLatin1Char('%');
    return e.value;
}

void putRect(QJsonObject& o, const SheetRect& r)
{
    o.insert(QStringLiteral("left"), edgeJson(r.left));
    o.insert(QStringLiteral("right"), edgeJson(r.right));
    o.insert(QStringLiteral("top"), edgeJson(r.top));
    o.insert(QStringLiteral("bottom"), edgeJson(r.bottom));
}

// A file name base from a template name: letters, digits, space, dash and underscore stay, anything else becomes an underscore.
QString uniqueFileBase(const QDir& dir, const QString& base, const QString& suffix)
{
    QString candidate = base;
    for (int n = 2; QFileInfo::exists(dir.filePath(candidate + suffix)); ++n)
        candidate = QStringLiteral("%1 %2").arg(base).arg(n);
    return candidate;
}

bool sameFileContent(const QString& a, const QString& b)
{
    QFile fa(a), fb(b);
    return fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly) && fa.size() == fb.size() && fa.readAll() == fb.readAll();
}

} // namespace

QString sanitizedTemplateFileName(const QString& name)
{
    QString out;
    for (const QChar c : name.trimmed())
        out += (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('_')) ? c : QLatin1Char('_');
    out = out.left(60).trimmed();
    return out.isEmpty() ? QStringLiteral("template") : out;
}

QByteArray sheetTemplateToJson(const SheetTemplate& t)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("name"), t.name);
    root.insert(QStringLiteral("units"), QStringLiteral("pt"));

    QJsonObject page;
    if (t.pageSizeId == QPageSize::Custom)
        page.insert(QStringLiteral("size"), QJsonArray{ t.customSizeMm.width() * kPointsPerMm, t.customSizeMm.height() * kPointsPerMm });
    else
    {
        for (const PageSizeName& n : kPageSizes)
            if (n.id == t.pageSizeId)
                page.insert(QStringLiteral("size"), QString::fromLatin1(n.name));
    }
    page.insert(QStringLiteral("orientation"), t.landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
    if (!t.printCompanionId.isEmpty())
        page.insert(QStringLiteral("printCompanion"), t.printCompanionId);
    root.insert(QStringLiteral("page"), page);
    root.insert(QStringLiteral("border"), QJsonObject{ { QStringLiteral("inset"), t.borderInset } });
    root.insert(QStringLiteral("margin"), t.margin);
    root.insert(QStringLiteral("areaGap"), t.areaGap);
    root.insert(QStringLiteral("lineWidth"), t.lineWidth);

    QJsonObject block;
    block.insert(QStringLiteral("anchor"), t.anchor == TitleBlockAnchor::Bottom ? QStringLiteral("bottom")
                                         : t.anchor == TitleBlockAnchor::BottomLeft ? QStringLiteral("bottom-left") : QStringLiteral("bottom-right"));
    if (t.anchor != TitleBlockAnchor::Bottom)
        block.insert(QStringLiteral("width"), t.blockWidth);
    block.insert(QStringLiteral("height"), t.blockHeight);
    block.insert(QStringLiteral("maxHeight"), t.blockMaxHeight);
    block.insert(QStringLiteral("growPadding"), t.growPadding);
    if (!t.growWithCell.isEmpty())
        block.insert(QStringLiteral("growWithCell"), t.growWithCell);

    QJsonArray lines;
    for (const SheetLine& l : t.lines)
        lines.append(QJsonObject{ { QStringLiteral("x1"), edgeJson(l.x1) }, { QStringLiteral("y1"), edgeJson(l.y1) },
                                  { QStringLiteral("x2"), edgeJson(l.x2) }, { QStringLiteral("y2"), edgeJson(l.y2) } });
    block.insert(QStringLiteral("lines"), lines);

    if (t.hasLogo)
    {
        QJsonObject logo;
        logo.insert(QStringLiteral("file"), t.logo.file);
        putRect(logo, t.logo.rect);
        logo.insert(QStringLiteral("fit"), t.logo.contain ? QStringLiteral("contain") : QStringLiteral("stretch"));
        block.insert(QStringLiteral("logo"), logo);
    }

    QJsonArray cells;
    for (const SheetCell& c : t.cells)
    {
        QJsonObject o;
        if (!c.id.isEmpty())
            o.insert(QStringLiteral("id"), c.id);
        putRect(o, c.rect);
        if (!c.label.isEmpty())
            o.insert(QStringLiteral("label"), c.label);
        o.insert(QStringLiteral("text"), c.text);
        o.insert(QStringLiteral("fontSize"), c.fontSize);
        if (c.bold)
            o.insert(QStringLiteral("bold"), true);
        if (c.color != QColor(Qt::black))
            o.insert(QStringLiteral("color"), c.color.name());
        const Qt::Alignment h = c.align & Qt::AlignHorizontal_Mask;
        if (h == Qt::AlignHCenter)
            o.insert(QStringLiteral("align"), QStringLiteral("center"));
        else if (h == Qt::AlignRight)
            o.insert(QStringLiteral("align"), QStringLiteral("right"));
        if (c.wrap)
            o.insert(QStringLiteral("wrap"), true);
        if (c.scrollWhenOverflow)
            o.insert(QStringLiteral("scrollWhenOverflow"), true);
        cells.append(o);
    }
    block.insert(QStringLiteral("cells"), cells);
    root.insert(QStringLiteral("titleBlock"), block);

    QJsonArray fields;
    for (const SheetField& f : t.fields)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), f.id);
        o.insert(QStringLiteral("label"), f.label);
        o.insert(QStringLiteral("type"), f.type);
        if (!f.defaultValue.isEmpty())
            o.insert(QStringLiteral("default"), f.defaultValue);
        if (!f.options.isEmpty())
            o.insert(QStringLiteral("options"), QJsonArray::fromStringList(f.options));
        if (f.type == QLatin1String("number"))
        {
            if (f.decimals != 2)
                o.insert(QStringLiteral("decimals"), f.decimals);
            if (!f.unit.isEmpty())
                o.insert(QStringLiteral("unit"), f.unit);
            if (f.minimum != -1e9)
                o.insert(QStringLiteral("min"), f.minimum);
            if (f.maximum != 1e9)
                o.insert(QStringLiteral("max"), f.maximum);
        }
        fields.append(o);
    }
    if (!fields.isEmpty())
        root.insert(QStringLiteral("fields"), fields);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QString sheetLogoPath(const SheetTemplate& t)
{
    if (!t.hasLogo || t.logo.file.isEmpty())
        return QString();
    if (QFileInfo(t.logo.file).isAbsolute())
        return t.logo.file;
    return t.folder.isEmpty() ? QString() : QDir(t.folder).filePath(t.logo.file);
}

QString writeUserSheetTemplate(const SheetTemplate& t, const QString& folder, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return QString();
    };
    if (!t.id.startsWith(QLatin1String("user:")) || t.id.size() <= 5)
        return fail(QStringLiteral("Only your own templates can be saved."));
    const QByteArray json = sheetTemplateToJson(t);
    // a template that would not load again is never written
    SheetTemplate check;
    QString parseError;
    if (!parseSheetTemplate(json, t.id, &check, &parseError))
        return fail(parseError);
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return fail(QStringLiteral("The templates folder could not be created."));
    QSaveFile file(QDir(folder).filePath(t.id.mid(5) + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
        return fail(QStringLiteral("%1 could not be written.").arg(file.fileName()));
    return t.id;
}

QString saveSheetTemplateAs(const SheetTemplate& source, const QString& newName, const QString& folder, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return QString();
    };
    const QString name = newName.trimmed();
    if (name.isEmpty() || name.size() > kMaxNameLength)
        return fail(QStringLiteral("A template name must have 1 to %1 characters.").arg(kMaxNameLength));
    for (const SheetTemplate& b : builtInSheetTemplates())
        if (b.name.compare(name, Qt::CaseInsensitive) == 0)
            return fail(QStringLiteral("\"%1\" is the name of a built-in template. Choose another name.").arg(name));
    // a template of yours with this name is selected as it is, never overwritten
    for (const SheetTemplate& u : loadUserSheetTemplates(folder))
        if (u.name.compare(name, Qt::CaseInsensitive) == 0)
            return u.id;

    if (folder.isEmpty() || !QDir().mkpath(folder))
        return fail(QStringLiteral("The templates folder could not be created."));
    const QDir dir(folder);
    SheetTemplate copy = source;
    copy.name = name;
    const QString base = uniqueFileBase(dir, sanitizedTemplateFileName(name), QStringLiteral(".json"));
    copy.id = QStringLiteral("user:") + base;

    // the logo file goes next to the new template, so the template is self-contained
    const QString logoSource = sheetLogoPath(source);
    if (!logoSource.isEmpty() && QFileInfo::exists(logoSource))
    {
        const QFileInfo info(logoSource);
        QString destination = dir.filePath(info.fileName());
        if (QFileInfo(destination).absoluteFilePath() != info.absoluteFilePath() && QFileInfo::exists(destination) && !sameFileContent(logoSource, destination))
        {
            const QString suffix = QLatin1Char('.') + info.suffix();
            destination = dir.filePath(uniqueFileBase(dir, info.completeBaseName(), suffix) + suffix);
        }
        if (QFileInfo(destination).absoluteFilePath() != info.absoluteFilePath() && !QFileInfo::exists(destination) && !QFile::copy(logoSource, destination))
            return fail(QStringLiteral("The logo file could not be copied."));
        copy.logo.file = QFileInfo(destination).fileName();
    }
    copy.folder = dir.absolutePath();
    return writeUserSheetTemplate(copy, folder, error);
}

bool setSheetTemplateLogo(SheetTemplate* t, const QString& logoFile, const QString& folder, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const QFileInfo info(logoFile);
    const QString suffix = info.suffix().toLower();
    if (!info.isFile() || (suffix != QLatin1String("png") && suffix != QLatin1String("jpg") && suffix != QLatin1String("jpeg") && suffix != QLatin1String("svg")))
        return fail(QStringLiteral("The logo must be a PNG, JPEG or SVG file."));
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return fail(QStringLiteral("The templates folder could not be created."));
    const QDir dir(folder);
    QString destination = dir.filePath(info.fileName());
    if (QFileInfo(destination).absoluteFilePath() != info.absoluteFilePath())
    {
        if (QFileInfo::exists(destination) && !sameFileContent(logoFile, destination))
            destination = dir.filePath(uniqueFileBase(dir, info.completeBaseName(), QLatin1Char('.') + info.suffix()) + QLatin1Char('.') + info.suffix());
        if (!QFileInfo::exists(destination) && !QFile::copy(logoFile, destination))
            return fail(QStringLiteral("The logo file could not be copied into the templates folder."));
    }
    t->logo.file = QFileInfo(destination).fileName();
    t->folder = dir.absolutePath();
    t->logo.contain = true;
    if (!t->hasLogo)
    {
        // a new logo cell at the left of the title block; whatever is measured from the left edge moves right to make room, and a thin
        // line separates it from the text
        const double width = std::max(60.0, t->blockHeight * 1.2);
        const double shift = 4.0 + width + 4.0;
        t->hasLogo = true;
        t->logo.rect.left = SheetEdge{ 4.0, false };
        t->logo.rect.right = SheetEdge{ 4.0 + width, false };
        t->logo.rect.top = SheetEdge{ 4.0, false };
        t->logo.rect.bottom = SheetEdge{ -4.0, false };
        const double blockWidth = sheetBlockWidthPt(*t);
        for (SheetCell& c : t->cells)
        {
            if (!c.rect.left.fraction && c.rect.left.value >= 0.0)
            {
                const double oldLeft = resolveSheetEdge(c.rect.left, blockWidth);
                const double oldRight = resolveSheetEdge(c.rect.right, blockWidth);
                c.rect.left.value += shift;
                if (!c.rect.right.fraction && c.rect.right.value > 0.0)
                    c.rect.right.value += shift;
                else if (oldRight - (oldLeft + shift) < std::min(oldRight - oldLeft, 24.0))
                {
                    // the right edge stays where it is (it is measured from the right, or is a fraction) and the cell would be squeezed
                    // to nothing: it moves clear of the logo as a whole instead
                    c.rect.right = SheetEdge{ std::min(oldRight + shift, blockWidth), false };
                }
            }
        }
        SheetLine separator;
        separator.x1 = SheetEdge{ shift - 2.0, false };
        separator.y1 = SheetEdge{ 0.0, false };
        separator.x2 = SheetEdge{ shift - 2.0, false };
        separator.y2 = SheetEdge{ 1.0, true };
        t->lines.append(separator);
    }
    return true;
}

QString writeSampleSheetTemplate(const QString& folder, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return QString();
    };
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return fail(QStringLiteral("The templates folder could not be created."));
    const QDir dir(folder);
    const struct { const char* name; const char* data; } files[] = { { "sample-company.json", kSampleJson }, { "sample-logo.svg", kSampleLogoSvg } };
    for (const auto& f : files)
    {
        const QString path = dir.filePath(QLatin1String(f.name));
        if (QFileInfo::exists(path))
            continue;   // never overwrite the user's own edits
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(f.data) < 0)
            return fail(QStringLiteral("%1 could not be written.").arg(path));
    }
    return QStringLiteral("user:sample-company");
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

const SheetTemplate& printTemplateForOrientation(const SheetTemplate& primary, bool landscape,
                                                const QVector<SheetTemplate>& available)
{
    if (primary.landscape != landscape && !primary.printCompanionId.isEmpty())
    {
        for (const SheetTemplate& candidate : available)
            if (candidate.id == primary.printCompanionId && candidate.landscape == landscape)
                return candidate;
    }
    return primary;
}

} // namespace Pdf3D
