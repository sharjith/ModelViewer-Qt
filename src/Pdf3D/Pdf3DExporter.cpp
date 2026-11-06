#include "Pdf3DExporter.h"

#include "PdfAppender.h"
#include "U3dWriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDate>
#include <QFileInfo>
#include <QFont>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>

namespace Pdf3D
{
namespace
{

constexpr qreal kMargin = 28.0;          // points
constexpr qreal kTitleBlockHeight = 90.0;
constexpr qreal kTitleBlockMaxHeight = 120.0;   // longer notes become a scrollable text field
constexpr qreal kTitleGridWidth = 360.0;   // right-hand field grid
constexpr qreal kNotesTop = 52.0;          // notes start this far below the title block top
constexpr qreal kNotesPointSize = 8.0;

struct Layout
{
    QRectF page;          // full page, painter coordinates (points, origin top left)
    QRectF area3d;        // the 3D area, painter coordinates
    QRectF titleBlock;
    qreal  pageHeightPt = 0.0;
    bool   notesScroll = false;   // the notes do not fit: a scrollable form field is added over the notes cell
};

QRectF notesRectOf(const QRectF& titleBlock)
{
    return QRectF(titleBlock.left() + 10, titleBlock.top() + kNotesTop, titleBlock.width() - kTitleGridWidth - 20, titleBlock.height() - kNotesTop - 4);
}

QRectF toPdfRect(const QRectF& painterRect, qreal pageHeightPt)
{
    // PDF origin is at the bottom left, y up: QRectF(left, bottom, width, height) with bottom < top is not what Qt's QRectF
    // means, so the rect is returned as (left, pdfBottom) to (right, pdfTop) using top() = pdfBottom, bottom() = pdfTop.
    const qreal pdfBottom = pageHeightPt - painterRect.bottom();
    const qreal pdfTop = pageHeightPt - painterRect.top();
    return QRectF(QPointF(painterRect.left(), pdfBottom), QPointF(painterRect.right(), pdfTop));
}

struct PartRow
{
    QString name;
    QString material;
    int     quantity = 0;
};

// One row per distinct (name, mesh) pair, in first-appearance order; instances of the same part add to the quantity.
// The importers name materials "<file base name> (<part>)"; the parts list shows just the part.
QString shortMaterialName(const QString& name, const QStringList& fileBaseNames)
{
    for (const QString& base : fileBaseNames)
    {
        const QString prefix = base + QStringLiteral(" (");
        if (!base.isEmpty() && name.startsWith(prefix) && name.endsWith(QLatin1Char(')')))
            return name.mid(prefix.size(), name.size() - prefix.size() - 1);
    }
    return name;
}

QVector<PartRow> buildPartRows(const U3dScene& scene, const QString& sourceFiles)
{
    QStringList fileBaseNames;
    for (const QString& file : sourceFiles.split(QStringLiteral(", "), Qt::SkipEmptyParts))
        fileBaseNames << QFileInfo(file).completeBaseName();

    QVector<PartRow> rows;
    QHash<QString, int> index;
    for (const U3dNode& n : scene.nodes)
    {
        if (n.meshIndex < 0 || n.meshIndex >= scene.meshes.size())
            continue;
        const QString key = n.displayName + QLatin1Char('|') + QString::number(n.meshIndex);
        auto it = index.constFind(key);
        if (it != index.constEnd())
        {
            ++rows[it.value()].quantity;
            continue;
        }
        PartRow r;
        r.name = n.displayName;
        const int mat = scene.meshes[n.meshIndex].materialIndex;
        r.material = (mat >= 0 && mat < scene.materials.size()) ? shortMaterialName(scene.materials[mat].name, fileBaseNames) : QString();
        r.quantity = 1;
        index.insert(key, rows.size());
        rows.push_back(r);
    }
    return rows;
}

constexpr qreal kPartsHeaderHeight = 22.0;
constexpr qreal kPartsRowHeight = 17.0;

int partsRowsPerSheet(const Layout& l)
{
    return qMax(1, static_cast<int>((l.area3d.height() - kPartsHeaderHeight) / kPartsRowHeight));
}

void drawCell(QPainter& p, const QRectF& cell, const QString& label, const QString& value)
{
    p.save();
    QFont f = p.font();
    f.setBold(false);
    f.setPointSizeF(6.5);
    p.setFont(f);
    p.setPen(QColor(110, 110, 110));
    p.drawText(cell.adjusted(5, 2, -3, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, label);
    f.setPointSizeF(9);
    p.setFont(f);
    p.setPen(Qt::black);
    const QRectF v = cell.adjusted(5, 10, -3, -2);
    p.drawText(v, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, p.fontMetrics().elidedText(value, Qt::ElideRight, static_cast<int>(v.width())));
    p.restore();
}

// Border and title block, identical on every sheet.
void drawFrame(QPainter& p, const Layout& l, const Pdf3DOptions& options, int partCount, int sheet, int sheetCount)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen thin(Qt::black, 0.6);
    p.setPen(thin);
    p.setBrush(Qt::NoBrush);
    p.drawRect(l.page.adjusted(kMargin - 8, kMargin - 8, -(kMargin - 8), -(kMargin - 8)));
    p.drawRect(l.area3d);

    // title block: left cell with title, subtitle and notes; right grid of 2 x 3 fields
    p.drawRect(l.titleBlock);
    const qreal gridW = kTitleGridWidth;
    const qreal splitX = l.titleBlock.right() - gridW;
    p.drawLine(QPointF(splitX, l.titleBlock.top()), QPointF(splitX, l.titleBlock.bottom()));
    const qreal midX = splitX + gridW / 2.0;
    const qreal rowH = l.titleBlock.height() / 3.0;
    p.drawLine(QPointF(midX, l.titleBlock.top()), QPointF(midX, l.titleBlock.bottom()));
    p.drawLine(QPointF(splitX, l.titleBlock.top() + rowH), QPointF(l.titleBlock.right(), l.titleBlock.top() + rowH));
    p.drawLine(QPointF(splitX, l.titleBlock.top() + 2 * rowH), QPointF(l.titleBlock.right(), l.titleBlock.top() + 2 * rowH));

    QFont titleFont = p.font();
    titleFont.setPointSizeF(15);
    titleFont.setBold(true);
    p.setFont(titleFont);
    const QRectF titleRect(l.titleBlock.left() + 10, l.titleBlock.top() + 6, splitX - l.titleBlock.left() - 20, 28);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, p.fontMetrics().elidedText(options.title, Qt::ElideRight, static_cast<int>(titleRect.width())));

    QFont small = p.font();
    small.setBold(false);
    small.setPointSizeF(9);
    p.setFont(small);
    const QRectF subRect(l.titleBlock.left() + 10, l.titleBlock.top() + 36, splitX - l.titleBlock.left() - 20, 14);
    p.drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, p.fontMetrics().elidedText(options.subtitle, Qt::ElideRight, static_cast<int>(subRect.width())));
    small.setPointSizeF(kNotesPointSize);
    p.setFont(small);
    p.setPen(QColor(70, 70, 70));
    const QRectF noteRect = notesRectOf(l.titleBlock);
    p.save();
    p.setClipRect(noteRect);   // overflowing text is cut (a scrollable field with the full text sits on top of it, see below)
    p.drawText(noteRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, options.notes);
    p.restore();
    p.setPen(thin);

    const QString date = options.date.isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : options.date;
    auto cell = [&](int col, int row) { return QRectF(splitX + col * gridW / 2.0, l.titleBlock.top() + row * rowH, gridW / 2.0, rowH); };
    drawCell(p, cell(0, 0), QCoreApplication::translate("Pdf3DExporter", "Drawing no."), options.drawingNumber);
    drawCell(p, cell(1, 0), QCoreApplication::translate("Pdf3DExporter", "Revision"), options.revision);
    drawCell(p, cell(0, 1), QCoreApplication::translate("Pdf3DExporter", "Author"), options.author);
    drawCell(p, cell(1, 1), QCoreApplication::translate("Pdf3DExporter", "Date"), date);
    drawCell(p, cell(0, 2), QCoreApplication::translate("Pdf3DExporter", "Company"), options.company);
    drawCell(p, cell(1, 2), QCoreApplication::translate("Pdf3DExporter", "Scale | parts | sheet"),
             QStringLiteral("%1  |  %2  |  %3/%4").arg(options.scale.isEmpty() ? QStringLiteral("-") : options.scale).arg(partCount).arg(sheet).arg(sheetCount));
}

// Sheet 1: the frame plus the placeholder under the 3D annotation (the poster when there is one, otherwise a hint for readers
// without 3D support).
void drawModelSheet(QPainter& p, const Layout& l, const Pdf3DOptions& options, int partCount, int sheetCount)
{
    drawFrame(p, l, options, partCount, 1, sheetCount);
    QPen thin(Qt::black, 0.6);
    if (!options.poster.isNull())
    {
        QSizeF s = options.poster.size();
        s.scale(l.area3d.size() - QSizeF(4, 4), Qt::KeepAspectRatio);
        const QRectF target(QPointF(l.area3d.center().x() - s.width() / 2, l.area3d.center().y() - s.height() / 2), s);
        p.drawImage(target, options.poster);
    }
    else
    {
        QFont f = p.font();
        f.setPointSizeF(11);
        p.setFont(f);
        p.setPen(QColor(110, 110, 110));
        p.drawText(l.area3d, Qt::AlignCenter | Qt::TextWordWrap,
                   QCoreApplication::translate("Pdf3DExporter", "3D content: open this file in Adobe Acrobat or Reader and enable 3D."));
        p.setPen(thin);
    }
}

// Parts list sheet `sheetIndex` (0-based among the list sheets).
void drawPartsSheet(QPainter& p, const Layout& l, const Pdf3DOptions& options, const QVector<PartRow>& rows, int partCount,
                    int sheetIndex, int sheetCount)
{
    drawFrame(p, l, options, partCount, 2 + sheetIndex, sheetCount);

    const qreal x0 = l.area3d.left();
    const qreal itemW = 46.0, qtyW = 60.0, matW = 260.0;
    const qreal nameW = l.area3d.width() - itemW - qtyW - matW;
    const qreal xs[5] = {x0, x0 + itemW, x0 + itemW + nameW, x0 + itemW + nameW + matW, x0 + l.area3d.width()};

    QFont f = p.font();
    f.setPointSizeF(9);
    f.setBold(true);
    p.setFont(f);
    p.fillRect(QRectF(x0, l.area3d.top(), l.area3d.width(), kPartsHeaderHeight), QColor(235, 235, 235));
    const QString heads[4] = {QCoreApplication::translate("Pdf3DExporter", "Item"), QCoreApplication::translate("Pdf3DExporter", "Part"),
                              QCoreApplication::translate("Pdf3DExporter", "Material"), QCoreApplication::translate("Pdf3DExporter", "Qty")};
    for (int c = 0; c < 4; ++c)
        p.drawText(QRectF(xs[c] + 6, l.area3d.top(), xs[c + 1] - xs[c] - 12, kPartsHeaderHeight), Qt::AlignLeft | Qt::AlignVCenter, heads[c]);
    p.drawLine(QPointF(x0, l.area3d.top() + kPartsHeaderHeight), QPointF(l.area3d.right(), l.area3d.top() + kPartsHeaderHeight));
    f.setBold(false);
    p.setFont(f);

    const int perSheet = partsRowsPerSheet(l);
    const int first = sheetIndex * perSheet;
    const int last = qMin(first + perSheet, static_cast<int>(rows.size()));
    for (int i = first; i < last; ++i)
    {
        const qreal y = l.area3d.top() + kPartsHeaderHeight + (i - first) * kPartsRowHeight;
        const QString cells[4] = {QString::number(i + 1), rows[i].name, rows[i].material, QString::number(rows[i].quantity)};
        for (int c = 0; c < 4; ++c)
        {
            const QRectF r(xs[c] + 6, y, xs[c + 1] - xs[c] - 12, kPartsRowHeight);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, p.fontMetrics().elidedText(cells[c], Qt::ElideRight, static_cast<int>(r.width())));
        }
        p.setPen(QPen(QColor(200, 200, 200), 0.4));
        p.drawLine(QPointF(x0, y + kPartsRowHeight), QPointF(l.area3d.right(), y + kPartsRowHeight));
        p.setPen(QPen(Qt::black, 0.6));
    }
    for (int c = 1; c < 4; ++c)
        p.drawLine(QPointF(xs[c], l.area3d.top()), QPointF(xs[c], l.area3d.top() + kPartsHeaderHeight + (last - first) * kPartsRowHeight));
}

} // namespace

Pdf3DResult Pdf3DExporter::exportToFile(const U3dScene& scene, const Pdf3DOptions& options, const QString& path)
{
    Pdf3DResult result;

    QVector3D lo, hi;
    if (scene.isEmpty() || !scene.computeBounds(&lo, &hi))
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "There is no visible geometry to export.");
        return result;
    }

    // 1. the 3D stream
    const U3dWriteResult u3d = U3dWriter::write(scene);
    if (!u3d.ok)
    {
        result.error = u3d.error;
        return result;
    }

    // 2. the 2D sheet
    QByteArray pdf;
    Layout layout;
    {
        QBuffer buffer(&pdf);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setPdfVersion(QPagedPaintDevice::PdfVersion_1_6);   // 3D annotations are a PDF 1.6 feature
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageOrientation(QPageLayout::Landscape);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        writer.setResolution(72);   // painter units are PDF points
        writer.setTitle(options.title);
        writer.setCreator(QStringLiteral("ModelViewer"));

        layout.pageHeightPt = writer.pageLayout().fullRect(QPageLayout::Point).height();
        layout.page = QRectF(0, 0, writer.pageLayout().fullRect(QPageLayout::Point).width(), layout.pageHeightPt);

        QPainter painter(&writer);
        if (!painter.isActive())
        {
            result.error = QCoreApplication::translate("Pdf3DExporter", "The PDF page could not be created.");
            return result;
        }

        // The title block grows with the notes text (up to kTitleBlockMaxHeight); the 3D area takes the rest of the page.
        qreal blockHeight = kTitleBlockHeight;
        if (!options.notes.trimmed().isEmpty())
        {
            painter.save();
            QFont nf = painter.font();
            nf.setPointSizeF(kNotesPointSize);
            painter.setFont(nf);
            const qreal textWidth = layout.page.width() - 2 * kMargin - kTitleGridWidth - 20;
            const QRectF needed = painter.boundingRect(QRectF(0, 0, textWidth, 10000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, options.notes);
            painter.restore();
            const qreal wanted = kNotesTop + needed.height() + 6.0;
            layout.notesScroll = wanted > kTitleBlockMaxHeight;
            blockHeight = qBound(kTitleBlockHeight, wanted, kTitleBlockMaxHeight);
        }
        layout.titleBlock = QRectF(kMargin, layout.page.bottom() - kMargin - blockHeight, layout.page.width() - 2 * kMargin, blockHeight);
        layout.area3d = QRectF(QPointF(kMargin, kMargin), QPointF(layout.page.right() - kMargin, layout.titleBlock.top() - 12));
        const int partCount = static_cast<int>(scene.nodes.size());
        const QVector<PartRow> rows = options.includePartsList ? buildPartRows(scene, options.subtitle) : QVector<PartRow>();
        const int perSheet = partsRowsPerSheet(layout);
        const int listSheets = rows.isEmpty() ? 0 : (static_cast<int>(rows.size()) + perSheet - 1) / perSheet;
        const int sheetCount = 1 + listSheets;
        drawModelSheet(painter, layout, options, partCount, sheetCount);
        for (int i = 0; i < listSheets; ++i)
        {
            writer.newPage();
            drawPartsSheet(painter, layout, options, rows, partCount, i, sheetCount);
        }
        painter.end();
    }

    // 3. the annotation, appended as an incremental update
    const QVector3D center = (lo + hi) * 0.5f;
    const float radius = 0.5f * (hi - lo).length();

    Annotation3D annotation;
    annotation.rect = toPdfRect(layout.area3d, layout.pageHeightPt);
    annotation.u3d = u3d.data;
    {
        // fitted to the bounding box and the shape of the 3D area (falls back to the sphere fit for a degenerate area)
        const float aspect = layout.area3d.height() > 0.0 ? static_cast<float>(layout.area3d.width() / layout.area3d.height()) : 1.0f;
        QList<QByteArray> standard = standardViewsFitted(lo, hi, aspect, options.upAxis);
        if (standard.isEmpty())
            standard = standardViews(center, radius, options.upAxis);
        const QStringList standardNames = standardViewNames();
        for (int i = 0; i < standard.size() && i < standardNames.size(); ++i)
        {
            if (!options.excludedStandardViews.contains(standardNames[i]))
                annotation.viewDictionaries.append(standard[i]);
        }
    }
    {
        // saved views follow the standard ones; names are made unique ("name (2)") so the view list stays unambiguous
        QSet<QString> used;
        for (const QString& n : standardViewNames())
            used.insert(n);
        for (CameraViewSpec spec : options.extraViews)
        {
            const QString base = spec.name.isEmpty() ? QCoreApplication::translate("Pdf3DExporter", "View") : spec.name;
            QString unique = base;
            for (int i = 2; used.contains(unique); ++i)
                unique = QStringLiteral("%1 (%2)").arg(base).arg(i);
            used.insert(unique);
            spec.name = unique;
            annotation.viewDictionaries.append(makeCameraView(spec, center, radius));
        }
    }
    annotation.defaultView = 0;
    annotation.description = QStringLiteral("3D model");
    if (layout.notesScroll)
    {
        annotation.hasNotesField = true;
        annotation.notesField.rect = toPdfRect(notesRectOf(layout.titleBlock), layout.pageHeightPt);
        annotation.notesField.text = options.notes;
        annotation.notesField.fontSize = kNotesPointSize;
    }

    if (annotation.viewDictionaries.isEmpty())
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "No view is selected: the PDF needs at least one.");
        return result;
    }

    QString error;
    if (!PdfAppender::append3DAnnotation(pdf, annotation, &error))
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "The 3D annotation could not be added to the PDF: %1").arg(error);
        return result;
    }

    // 4. write the file
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }
    if (file.write(pdf) != pdf.size() || !file.commit())
    {
        result.error = QCoreApplication::translate("Pdf3DExporter", "Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace Pdf3D
