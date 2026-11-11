#pragma once

#include "Pdf3DExporter.h"

#include <QImage>
#include <QSizeF>

class QPageLayout;
class QPagedPaintDevice;
class QPainter;

// Draw the same vector sheet in preview, on a printer, or in a static PDF or SVG.
namespace PrintPage
{

// The painter origin is the top left of the printable area. Page dimensions are in points, and
// deviceDpi converts those points to the painter's coordinates. A supplied modelSizeMm is the
// physical footprint of the complete captured view. Empty means fit the view to the printable area.
bool paint(QPainter& painter, const QSizeF& pagePt, int deviceDpi, bool useTemplate,
           const Pdf3D::Pdf3DOptions& options, int partCount, const QImage& view,
           const QSizeF& modelSizeMm = {}, bool* scaleTooLarge = nullptr, int sheet = 1, int sheets = 1);

// Pixel dimensions for the fitted model at the requested DPI, bounded to a practical capture budget.
QSize rasterSize(const QSizeF& pagePt, int dpi, bool useTemplate,
                 const Pdf3D::Pdf3DOptions& options, int partCount, const QSize& viewSize, const QSizeF& modelSizeMm = {});

bool paintSheets(QPagedPaintDevice& device, int dpi, bool useTemplate, const Pdf3D::Pdf3DOptions& options,
                 int partCount, const QVector<Pdf3D::StaticSheet>& sheets, int first = 1, int last = 0);
bool saveSheets(const QString& path, const QPageLayout& layout, bool useTemplate, const Pdf3D::Pdf3DOptions& options,
                int partCount, const QVector<Pdf3D::StaticSheet>& sheets, QString* error);

enum class FileFormat { Pdf, Svg };

// Atomically save a sheet using the selected paper, orientation and margins.
bool saveFile(const QString& path, FileFormat format, const QPageLayout& layout, bool useTemplate,
              const Pdf3D::Pdf3DOptions& options, int partCount, const QImage& view, QString* error,
              const QSizeF& modelSizeMm = {});

} // namespace PrintPage
