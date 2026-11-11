#include "SheetRenderer.h"

#include "Pdf3DExporter.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEngine>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace Pdf3D
{

namespace
{

// The sheet's world coordinates are points, even when a printer paints in high-resolution pixels.
void setSheetFontSize(QFont& font, const QPainter& painter, qreal sizePt)
{
    font.setPointSizeF(sizePt * 72.0 / painter.device()->logicalDpiY());
}

int cellIndexById(const SheetTemplate& tpl, const QString& id)
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < tpl.cells.size(); ++i)
        if (tpl.cells[i].id == id)
            return i;
    return -1;
}

// The template's logo (SVG as vectors, PNG or JPEG as an image) in its rectangle. A missing or unreadable file leaves the space empty.
void drawLogo(QPainter& p, const SheetTemplate& tpl, const QRectF& rect)
{
    const QString path = sheetLogoPath(tpl);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    const QFileInfo info(path);
    const auto fitted = [&](const QSizeF& natural) -> QRectF {
        if (!tpl.logo.contain || natural.isEmpty())
            return rect;
        QSizeF size = natural;
        size.scale(rect.size(), Qt::KeepAspectRatio);
        return QRectF(QPointF(rect.center().x() - size.width() / 2.0, rect.center().y() - size.height() / 2.0), size);
    };
    if (info.suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0)
    {
        QSvgRenderer renderer(path);
        if (renderer.isValid())
            renderer.render(&p, fitted(QSizeF(renderer.defaultSize())));
        return;
    }
    const QImage image(path);
    if (!image.isNull())
        p.drawImage(fitted(QSizeF(image.size())), image);
}

// A title block cell with a caption (small, grey, top left) and its value below, as the field grid of the sheet has always looked.
void drawFieldCell(QPainter& p, const QRectF& cell, const QString& label, const QString& value, const SheetCell& c)
{
    p.save();
    QFont f = p.font();
    f.setBold(false);
    setSheetFontSize(f, p, 6.5);
    p.setFont(f);
    p.setPen(QColor(110, 110, 110));
    p.drawText(cell.adjusted(5, 2, -3, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, label);
    f.setBold(c.bold);
    setSheetFontSize(f, p, c.fontSize);
    p.setFont(f);
    p.setPen(c.color);
    const QRectF v = cell.adjusted(5, 10, -3, -2);
    p.drawText(v, static_cast<int>(c.align) | Qt::AlignVCenter | Qt::TextSingleLine,
               p.fontMetrics().elidedText(value, Qt::ElideRight, static_cast<int>(v.width())));
    p.restore();
}

} // namespace

QMap<QString, QString> sheetValues(const Pdf3DOptions& options, int partCount, int sheet, int sheetCount)
{
    QMap<QString, QString> v;
    v.insert(QStringLiteral("title"), options.title);
    v.insert(QStringLiteral("subtitle"), options.subtitle);
    v.insert(QStringLiteral("notes"), options.notes);
    v.insert(QStringLiteral("drawingNumber"), options.drawingNumber);
    v.insert(QStringLiteral("revision"), options.revision);
    v.insert(QStringLiteral("author"), options.author);
    v.insert(QStringLiteral("company"), options.company);
    v.insert(QStringLiteral("scale"), options.scale);
    v.insert(QStringLiteral("date"), options.date.isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : options.date);
    v.insert(QStringLiteral("parts"), QString::number(partCount));
    v.insert(QStringLiteral("sheet"), QString::number(sheet));
    v.insert(QStringLiteral("sheets"), QString::number(sheetCount));
    // what the exported parts declare; the user's own entry in a custom field of the same id wins when it is not empty
    v.insert(QStringLiteral("material"), options.sceneMaterial);
    v.insert(QStringLiteral("mass"), options.sceneMass);
    const QStringList sceneNames = sceneSheetPlaceholders();
    for (auto it = options.fieldValues.constBegin(); it != options.fieldValues.constEnd(); ++it)
    {
        if (!v.contains(it.key()) || (sceneNames.contains(it.key()) && !it.value().trimmed().isEmpty()))
            v.insert(it.key(), it.value());
    }
    return v;
}

SheetLayout layoutSheet(QPainter& measure, const SheetTemplate& tpl, const QSizeF& pagePt, const QMap<QString, QString>& values)
{
    const int growCell = cellIndexById(tpl, tpl.growWithCell);
    int scrollCell = -1;
    for (int i = 0; i < tpl.cells.size() && scrollCell < 0; ++i)
        if (tpl.cells[i].scrollWhenOverflow)
            scrollCell = i;

    const auto textHeightIn = [&](int cellIndex, const QRectF& rect) {
        const SheetCell& c = tpl.cells[cellIndex];
        const QString text = substituteSheetText(c.text, values);
        if (text.trimmed().isEmpty())
            return 0.0;
        measure.save();
        QFont nf = measure.font();
        nf.setBold(c.bold);
        setSheetFontSize(nf, measure, c.fontSize);
        measure.setFont(nf);
        const qreal h = measure.boundingRect(QRectF(0, 0, rect.width(), 10000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text).height();
        measure.restore();
        return h;
    };

    // The title block grows with the text of its grow cell (the notes) up to the template's maximum; the 3D area takes the rest.
    double neededText = 0.0;
    if (growCell >= 0)
        neededText = textHeightIn(growCell, resolveSheet(tpl, pagePt, tpl.blockHeight).cells[growCell]);
    bool growOverflows = false;
    const double blockHeight = titleBlockHeightFor(tpl, pagePt, neededText, &growOverflows);

    SheetLayout layout;
    layout.sheet = resolveSheet(tpl, pagePt, blockHeight);
    if (scrollCell >= 0)
    {
        layout.scrollRect = layout.sheet.cells[scrollCell];
        layout.scrollText = substituteSheetText(tpl.cells[scrollCell].text, values);
        layout.scrollFontSize = tpl.cells[scrollCell].fontSize;
        // the grow cell overflows when its text needs more than the maximum block; any other scroll cell when its text needs more than it has
        layout.scroll = scrollCell == growCell ? growOverflows : textHeightIn(scrollCell, layout.scrollRect) > layout.scrollRect.height();
    }
    return layout;
}

void drawSheetFrame(QPainter& p, const SheetTemplate& tpl, const ResolvedSheet& s, const QRectF& area3d, const QMap<QString, QString>& values)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen thin(Qt::black, tpl.lineWidth);
    p.setPen(thin);
    p.setBrush(Qt::NoBrush);
    p.drawRect(s.border);
    p.drawRect(area3d);
    p.drawRect(s.titleBlock);
    for (const QLineF& line : s.lines)
        p.drawLine(line);
    if (tpl.hasLogo && !s.logo.isEmpty())
        drawLogo(p, tpl, s.logo);

    const QFont base = p.font();
    const bool svg = p.paintEngine()->type() == QPaintEngine::SVG;
    for (int i = 0; i < tpl.cells.size() && i < s.cells.size(); ++i)
    {
        const SheetCell& c = tpl.cells[i];
        const QRectF rect = s.cells[i];
        const QString text = substituteSheetText(c.text, values);
        if (!c.label.isEmpty())
        {
            p.setFont(base);
            drawFieldCell(p, rect, sheetLabelText(tpl, c.label), text, c);
            continue;
        }
        if (text.isEmpty())
            continue;
        p.save();
        QFont f = base;
        f.setBold(c.bold);
        setSheetFontSize(f, p, c.fontSize);
        p.setFont(f);
        p.setPen(c.color);
        if (c.wrap)
        {
            const bool hadClip = svg && p.hasClipping();
            const QPainterPath previousClip = hadClip ? p.clipPath() : QPainterPath();
            p.setClipRect(rect, Qt::IntersectClip);
            p.drawText(rect, static_cast<int>(c.align) | Qt::AlignTop | Qt::TextWordWrap, text);
            if (svg)
            {
                // Reset the SVG engine's cell clip before restoring the painter state.
                p.setClipping(false);
                if (hadClip)
                    p.setClipPath(previousClip, Qt::ReplaceClip);
            }
        }
        else
        {
            p.drawText(rect, static_cast<int>(c.align) | Qt::AlignVCenter | Qt::TextSingleLine,
                       p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(rect.width())));
        }
        p.restore();
    }
    p.setPen(thin);
}

QImage renderSheetPreview(const SheetTemplate& tpl, const QMap<QString, QString>& values, int widthPx, int selectedCell, SheetLayout* layoutOut)
{
    const QSizeF pagePt = sheetPageSizePt(tpl);
    const double zoom = std::max(1, widthPx) / pagePt.width();
    QImage image(QSize(std::max(1, widthPx), std::max(1, static_cast<int>(std::lround(pagePt.height() * zoom)))), QImage::Format_ARGB32_Premultiplied);
    image.setDotsPerMeterX(2835);   // 72 dpi: font sizes in points are pixels at zoom 1, as in the PDF
    image.setDotsPerMeterY(2835);
    image.fill(Qt::white);

    QPainter p(&image);
    p.scale(zoom, zoom);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const SheetLayout layout = layoutSheet(p, tpl, pagePt, values);
    if (layoutOut)
        *layoutOut = layout;

    const QRectF area = layout.sheet.area3d;
    p.fillRect(area, QColor(238, 241, 245));
    p.save();
    QFont f = p.font();
    f.setPointSizeF(11);
    p.setFont(f);
    p.setPen(QColor(150, 150, 150));
    p.drawText(area, Qt::AlignCenter, QCoreApplication::translate("Pdf3DExporter", "3D area"));
    p.restore();
    drawSheetFrame(p, tpl, layout.sheet, area, values);

    if (selectedCell >= 0 && selectedCell < layout.sheet.cells.size())
    {
        p.save();
        QPen pen(QColor(30, 110, 220), 1.5, Qt::DashLine);
        p.setPen(pen);
        p.setBrush(QColor(30, 110, 220, 30));
        p.drawRect(layout.sheet.cells[selectedCell]);
        p.restore();
    }
    p.end();
    return image;
}

} // namespace Pdf3D
