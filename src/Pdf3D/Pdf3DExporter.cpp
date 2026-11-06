#include "Pdf3DExporter.h"

#include "PdfAppender.h"
#include "U3dWriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDate>
#include <QFont>
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
constexpr qreal kTitleBlockHeight = 80.0;

struct Layout
{
    QRectF page;          // full page, painter coordinates (points, origin top left)
    QRectF area3d;        // the 3D area, painter coordinates
    QRectF titleBlock;
    qreal  pageHeightPt = 0.0;
};

QRectF toPdfRect(const QRectF& painterRect, qreal pageHeightPt)
{
    // PDF origin is at the bottom left, y up: QRectF(left, bottom, width, height) with bottom < top is not what Qt's QRectF
    // means, so the rect is returned as (left, pdfBottom) to (right, pdfTop) using top() = pdfBottom, bottom() = pdfTop.
    const qreal pdfBottom = pageHeightPt - painterRect.bottom();
    const qreal pdfTop = pageHeightPt - painterRect.top();
    return QRectF(QPointF(painterRect.left(), pdfBottom), QPointF(painterRect.right(), pdfTop));
}

void drawSheet(QPainter& p, const Layout& l, const Pdf3DOptions& options, int partCount)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen thin(Qt::black, 0.6);
    p.setPen(thin);
    p.setBrush(Qt::NoBrush);

    // outer border and the 3D area frame
    p.drawRect(l.page.adjusted(kMargin - 8, kMargin - 8, -(kMargin - 8), -(kMargin - 8)));
    p.drawRect(l.area3d);

    // placeholder under the 3D annotation: the poster when there is one, otherwise a hint for readers without 3D support
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

    // title block: left cell with the title, right column with a few fields
    p.drawRect(l.titleBlock);
    const qreal splitX = l.titleBlock.right() - 300.0;
    p.drawLine(QPointF(splitX, l.titleBlock.top()), QPointF(splitX, l.titleBlock.bottom()));
    const qreal midY = l.titleBlock.top() + l.titleBlock.height() / 2.0;
    p.drawLine(QPointF(splitX, midY), QPointF(l.titleBlock.right(), midY));

    QFont titleFont = p.font();
    titleFont.setPointSizeF(15);
    titleFont.setBold(true);
    p.setFont(titleFont);
    const QRectF titleRect(l.titleBlock.left() + 10, l.titleBlock.top() + 8, splitX - l.titleBlock.left() - 20, 34);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, p.fontMetrics().elidedText(options.title, Qt::ElideRight, static_cast<int>(titleRect.width())));

    QFont small = p.font();
    small.setBold(false);
    small.setPointSizeF(9);
    p.setFont(small);
    const QRectF subRect(l.titleBlock.left() + 10, l.titleBlock.top() + 46, splitX - l.titleBlock.left() - 20, 26);
    p.drawText(subRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, options.subtitle);

    small.setPointSizeF(8.5);
    p.setFont(small);
    const QRectF topCell(splitX + 8, l.titleBlock.top() + 4, l.titleBlock.right() - splitX - 16, l.titleBlock.height() / 2.0 - 8);
    const QRectF botCell(splitX + 8, midY + 4, l.titleBlock.right() - splitX - 16, l.titleBlock.height() / 2.0 - 8);
    p.drawText(topCell, Qt::AlignLeft | Qt::AlignVCenter,
               QCoreApplication::translate("Pdf3DExporter", "Parts: %1    Date: %2").arg(partCount).arg(QDate::currentDate().toString(Qt::ISODate)));
    p.drawText(botCell, Qt::AlignLeft | Qt::AlignVCenter,
               QCoreApplication::translate("Pdf3DExporter", "Sheet 1 of 1    ModelViewer"));
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
        layout.titleBlock = QRectF(kMargin, layout.page.bottom() - kMargin - kTitleBlockHeight, layout.page.width() - 2 * kMargin, kTitleBlockHeight);
        layout.area3d = QRectF(QPointF(kMargin, kMargin), QPointF(layout.page.right() - kMargin, layout.titleBlock.top() - 12));

        QPainter painter(&writer);
        if (!painter.isActive())
        {
            result.error = QCoreApplication::translate("Pdf3DExporter", "The PDF page could not be created.");
            return result;
        }
        drawSheet(painter, layout, options, static_cast<int>(scene.nodes.size()));
        painter.end();
    }

    // 3. the annotation, appended as an incremental update
    const QVector3D center = (lo + hi) * 0.5f;
    const float radius = 0.5f * (hi - lo).length();

    Annotation3D annotation;
    annotation.rect = toPdfRect(layout.area3d, layout.pageHeightPt);
    annotation.u3d = u3d.data;
    annotation.viewDictionaries = standardViews(center, radius, options.upAxis);
    annotation.defaultView = 0;
    annotation.description = QStringLiteral("3D model");

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
