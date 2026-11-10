#pragma once

#include "Pdf3DExporter.h"

#include <QImage>
#include <QSizeF>

// One printed page: the view on white, inside a sheet template (border, title block, logo, fields; the same templates Save as 3D PDF
// uses) or on a plain page. The page is drawn as a raster image, so it prints the same on any printer or print-to-PDF driver.
namespace PrintPage
{

// `pagePt` is the page in points (1/72 inch), `dpi` the resolution of the image (150 draft, 300 normal, 600 fine). The sheet renderer
// measures text in points and needs a 72 dpi paint device (SheetRenderer.h), so the image carries 72 dpi and the painter scales it up.
// `partCount` fills the {parts} placeholder. A null `view` leaves the 3D area empty.
QImage render(const QSizeF& pagePt, int dpi, bool useTemplate, const Pdf3D::Pdf3DOptions& options, int partCount, const QImage& view);

} // namespace PrintPage
