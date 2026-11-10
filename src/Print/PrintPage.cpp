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
    const qreal scale = qMax(72, dpi) / 72.0;
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
        const Pdf3D::SheetTemplate tpl = Pdf3D::findSheetTemplate(options.templateId);
        const QMap<QString, QString> values = Pdf3D::sheetValues(options, partCount, 1, 1);
        const Pdf3D::SheetLayout layout = Pdf3D::layoutSheet(p, tpl, pagePt, values);
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

} // namespace PrintPage
