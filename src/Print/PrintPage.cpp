#include "PrintPage.h"
#include "SheetRenderer.h"
#include "SheetTemplate.h"

#include <QCoreApplication>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace PrintPage
{

namespace
{

struct Frame
{
    Pdf3D::SheetTemplate tpl;
    Pdf3D::ResolvedSheet sheet;
    QMap<QString, QString> values;
    QRectF modelArea;
    qreal scale = 1.0;
};

Frame layoutFrame(QPainter& painter, const QSizeF& pagePt, bool useTemplate,
                  const Pdf3D::Pdf3DOptions& options, int partCount, int sheetNumber = 1, int sheetCount = 1)
{
    Frame frame;
    if (!useTemplate)
    {
        frame.modelArea = QRectF(QPointF(), pagePt);
        return frame;
    }

    const auto source = Pdf3D::findSheetTemplate(options.templateId);
    // On very small paper, shrink the frame and text together to reserve space for the model.
    constexpr qreal minimumModelSpan = 24.0;
    const qreal frameWidth = 2.0 * qMax(source.margin, source.borderInset);
    const qreal frameHeight = qMax(2.0 * source.borderInset, 2.0 * source.margin + source.blockMaxHeight + source.areaGap);
    frame.scale = std::min<qreal>({1.0, pagePt.width() / (frameWidth + minimumModelSpan),
                                        pagePt.height() / (frameHeight + minimumModelSpan)});
    const QSizeF layoutPage = pagePt / frame.scale;
    const qreal blockWidthRatio = Pdf3D::sheetBlockWidthPt(source, layoutPage) / Pdf3D::sheetBlockWidthPt(source);
    frame.tpl = Pdf3D::scaleSheetTemplateWidth(source, blockWidthRatio);
    frame.values = Pdf3D::sheetValues(options, partCount, sheetNumber, sheetCount);
    frame.sheet = Pdf3D::layoutSheet(painter, frame.tpl, layoutPage, frame.values).sheet;

    const auto& sheet = frame.sheet;
    const QRectF titleBlockBounds = sheet.titleBlock.adjusted(-0.01, -0.01, 0.01, 0.01);
    const auto insideBlock = [&](const QRectF& rect) { return !rect.isEmpty() && titleBlockBounds.contains(rect); };
    if (sheet.area3d.isEmpty() || !sheet.page.contains(sheet.area3d) ||
        sheet.titleBlock.isEmpty() || !sheet.page.contains(sheet.titleBlock) ||
        !std::all_of(sheet.cells.cbegin(), sheet.cells.cend(), insideBlock) ||
        (frame.tpl.hasLogo && !insideBlock(sheet.logo)))
        return {};

    frame.modelArea = sheet.area3d.adjusted(2, 2, -2, -2);
    return frame;
}

QSizeF imageSizeForFrame(const Frame& frame, const QSize& viewSize, const QSizeF& modelSizeMm)
{
    if (!modelSizeMm.isEmpty())
    {
        const QSizeF requested = modelSizeMm * (72.0 / (25.4 * frame.scale));
        if (requested.width() > frame.modelArea.width() + 0.001 ||
            requested.height() > frame.modelArea.height() + 0.001)
            return {};
        return requested;
    }

    QSizeF fitted(viewSize);
    fitted.scale(frame.modelArea.size(), Qt::KeepAspectRatio);
    return fitted;
}

} // namespace

QSize rasterSize(const QSizeF& pagePt, int dpi, bool useTemplate,
                 const Pdf3D::Pdf3DOptions& options, int partCount, const QSize& viewSize, const QSizeF& modelSizeMm)
{
    if (pagePt.isEmpty() || dpi <= 0 || viewSize.isEmpty())
        return {};

    QImage metrics(1, 1, QImage::Format_RGB32);
    metrics.setDotsPerMeterX(2835);
    metrics.setDotsPerMeterY(2835);
    QPainter painter(&metrics);
    const Frame frame = layoutFrame(painter, pagePt, useTemplate, options, partCount);
    if (frame.modelArea.isEmpty())
        return {};

    QSizeF pixels = imageSizeForFrame(frame, viewSize, modelSizeMm) * (frame.scale * dpi / 72.0);
    if (pixels.isEmpty())
        return {};
    constexpr qreal maxPixels = 12000000.0;
    constexpr qreal maxEdge = 8192.0;
    const qreal limit = std::min<qreal>({1.0, maxEdge / qMax(pixels.width(), pixels.height()),
                                      std::sqrt(maxPixels / (pixels.width() * pixels.height()))});
    pixels *= limit;
    return QSize(qMax(1, qFloor(pixels.width())), qMax(1, qFloor(pixels.height())));
}

bool paint(QPainter& painter, const QSizeF& pagePt, int deviceDpi, bool useTemplate,
           const Pdf3D::Pdf3DOptions& options, int partCount, const QImage& view,
           const QSizeF& modelSizeMm, bool* scaleTooLarge, int sheet, int sheets)
{
    if (scaleTooLarge)
        *scaleTooLarge = false;
    if (!painter.isActive() || pagePt.isEmpty() || deviceDpi <= 0 || view.isNull())
        return false;

    const Frame frame = layoutFrame(painter, pagePt, useTemplate, options, partCount, sheet, sheets);
    if (frame.modelArea.isEmpty())
        return false;

    const qreal deviceScale = deviceDpi / 72.0;
    painter.save();
    painter.scale(deviceScale, deviceScale);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF page(QPointF(), pagePt);
    painter.setClipRect(page, Qt::IntersectClip);
    painter.fillRect(page, Qt::white);

    painter.scale(frame.scale, frame.scale);
    if (useTemplate)
        Pdf3D::drawSheetFrame(painter, frame.tpl, frame.sheet, frame.sheet.area3d, frame.values);

    const QSizeF imageSize = imageSizeForFrame(frame, view.size(), modelSizeMm);
    if (!imageSize.isEmpty())
    {
        const QRectF where(frame.modelArea.center() - QPointF(imageSize.width() / 2, imageSize.height() / 2), imageSize);
        painter.drawImage(where, view);
    }
    else if (scaleTooLarge)
    {
        *scaleTooLarge = !modelSizeMm.isEmpty();
    }
    painter.restore();
    return !imageSize.isEmpty();
}

bool paintSheets(QPagedPaintDevice& device, int dpi, bool useTemplate, const Pdf3D::Pdf3DOptions& options,
                 int partCount, const QVector<Pdf3D::StaticSheet>& sheets, int first, int last)
{
    const int count = static_cast<int>(sheets.size());
    const int end = last > 0 ? qMin(last, count) : count;
    if (first < 1 || first > end)
        return false;

    QPainter painter(&device);
    if (!painter.isActive())
        return false;
    const QSizeF size = device.pageLayout().paintRect(QPageLayout::Point).size();
    for (int page = first; page <= end; ++page)
    {
        if (page > first && !device.newPage())
            return false;
        const Pdf3D::StaticSheet& sheet = sheets.at(page - 1);
        Pdf3D::Pdf3DOptions pageOptions = options;
        pageOptions.subtitle = sheet.name;
        if (!paint(painter, size, dpi, useTemplate, pageOptions, partCount, sheet.image, {}, nullptr, page, count))
            return false;
    }
    return painter.end();
}

bool saveSheets(const QString& path, const QPageLayout& layout, bool useTemplate, const Pdf3D::Pdf3DOptions& options,
                int partCount, const QVector<Pdf3D::StaticSheet>& sheets, QString* error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        bool drawn = false;
        {
            QPdfWriter writer(&file);
            writer.setResolution(72);
            writer.setTitle(options.title);
            writer.setCreator(QStringLiteral("ModelViewer"));
            drawn = writer.setPageLayout(layout) && paintSheets(writer, 72, useTemplate, options, partCount, sheets);
        }
        if (!drawn)
        {
            if (error)
                *error = QCoreApplication::translate("PrintDialog", "The sheets could not be exported.");
            return false;
        }
        if (file.commit())
            return true;
    }
    if (error)
        *error = file.errorString();
    return false;
}

bool saveFile(const QString& path, FileFormat format, const QPageLayout& layout, bool useTemplate,
              const Pdf3D::Pdf3DOptions& options, int partCount, const QImage& view, QString* error,
              const QSizeF& modelSizeMm)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }

    const auto draw = [&](QPaintDevice& device, const QPageLayout& pageLayout, bool fullPaper) {
        QPainter painter(&device);
        if (!painter.isActive())
            return false;

        const QRectF printable = pageLayout.paintRect(QPageLayout::Point);
        if (fullPaper)
        {
            painter.fillRect(pageLayout.fullRect(QPageLayout::Point), Qt::white);
            painter.translate(printable.topLeft());
        }
        const bool drawn = paint(painter, printable.size(), 72, useTemplate, options, partCount, view, modelSizeMm);
        return painter.end() && drawn;
    };

    bool drawn = false;
    if (format == FileFormat::Svg)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QSvgGenerator writer(QSvgGenerator::SvgVersion::Svg11);
#else
        QSvgGenerator writer;
#endif
        writer.setOutputDevice(&file);
        writer.setTitle(options.title);
        writer.setResolution(72);
        const QRectF paper = layout.fullRect(QPageLayout::Point);
        writer.setSize(paper.size().toSize());
        writer.setViewBox(paper);
        drawn = draw(writer, layout, true);
    }
    else
    {
        QPdfWriter writer(&file);
        writer.setTitle(options.title);
        writer.setCreator(QStringLiteral("ModelViewer"));
        writer.setResolution(72);
        drawn = writer.setPageLayout(layout) && draw(writer, writer.pageLayout(), false);
    }

    if (!drawn)
    {
        if (error)
            *error = QCoreApplication::translate("PrintDialog", "The sheet could not be exported.");
        return false;
    }

    if (file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}

} // namespace PrintPage
