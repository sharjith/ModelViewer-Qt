#include "PrintPage.h"
#include "SheetRenderer.h"
#include "SheetTemplate.h"

#include <QPainter>
#include <algorithm>

namespace PrintPage
{

QImage render(const QSizeF& pagePt, int dpi, bool useTemplate, const Pdf3D::Pdf3DOptions& options, int partCount, const QImage& view)
{
    if (pagePt.isEmpty() || view.isNull())
        return {};
    const qreal scale = qMax(1, dpi) / 72.0;
    const QSize px(qMax(1, qRound(pagePt.width() * scale)), qMax(1, qRound(pagePt.height() * scale)));
    QImage page(px, QImage::Format_ARGB32_Premultiplied);
    if (page.isNull())
        return page;
    page.fill(Qt::white);
    // 72 dpi as far as fonts are concerned: the sheet renderer's text measurements and sizes are in points
    page.setDotsPerMeterX(qRound(72.0 / 0.0254));
    page.setDotsPerMeterY(qRound(72.0 / 0.0254));

    QPainter p(&page);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(scale, scale);

    QRectF target;
    if (useTemplate)
    {
        const auto source = Pdf3D::findSheetTemplate(options.templateId);
        // Keep the authored point sizes when the frame fits. On very small paper, shrink the frame and text
        // together to reserve space for the model, even when the title block grows to its maximum.
        constexpr qreal minimumModelSpan = 24.0;
        const qreal frameWidth = 2.0 * qMax(source.margin, source.borderInset);
        const qreal frameHeight = qMax(2.0 * source.borderInset,
                                      2.0 * source.margin + source.blockMaxHeight + source.areaGap);
        const qreal frameScale = std::min<qreal>({1.0, pagePt.width() / (frameWidth + minimumModelSpan),
                                                     pagePt.height() / (frameHeight + minimumModelSpan)});
        const QSizeF layoutPage = pagePt / frameScale;
        const qreal blockWidthRatio = Pdf3D::sheetBlockWidthPt(source, layoutPage) / Pdf3D::sheetBlockWidthPt(source);
        const auto tpl = Pdf3D::scaleSheetTemplateWidth(source, blockWidthRatio);
        p.scale(frameScale, frameScale);

        const QMap<QString, QString> values = Pdf3D::sheetValues(options, partCount, 1, 1);
        const Pdf3D::SheetLayout layout = Pdf3D::layoutSheet(p, tpl, layoutPage, values);
        const auto& sheet = layout.sheet;
        const QRectF titleBlockBounds = sheet.titleBlock.adjusted(-0.01, -0.01, 0.01, 0.01);
        const auto insideBlock = [&](const QRectF& rect) { return !rect.isEmpty() && titleBlockBounds.contains(rect); };
        if (sheet.area3d.isEmpty() || !sheet.page.contains(sheet.area3d) ||
            sheet.titleBlock.isEmpty() || !sheet.page.contains(sheet.titleBlock) ||
            !std::all_of(sheet.cells.cbegin(), sheet.cells.cend(), insideBlock) ||
            (tpl.hasLogo && !insideBlock(sheet.logo)))
            return {};
        Pdf3D::drawSheetFrame(p, tpl, sheet, sheet.area3d, values);
        target = layout.sheet.area3d.adjusted(2, 2, -2, -2);
    }
    else
    {
        constexpr qreal margin = 28.35;   // 10 mm
        target = QRectF(QPointF(margin, margin), pagePt - QSizeF(2 * margin, 2 * margin));
    }

    if (target.isEmpty())
        return {};

    QSizeF s = view.size();
    s.scale(target.size(), Qt::KeepAspectRatio);
    const QRectF where(QPointF(target.center().x() - s.width() / 2, target.center().y() - s.height() / 2), s);
    p.drawImage(where, view);
    p.end();
    return page;
}

void paint(QPainter& painter, const QSize& printableSize, const QImage& page)
{
    const QSize targetSize = page.size().scaled(printableSize, Qt::KeepAspectRatio);
    const QPoint targetTopLeft((printableSize.width() - targetSize.width()) / 2,
                               (printableSize.height() - targetSize.height()) / 2);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(targetTopLeft, targetSize), page);
}

} // namespace PrintPage
