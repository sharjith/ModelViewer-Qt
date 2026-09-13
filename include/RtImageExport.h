#pragma once

#include <QString>
#include <vector>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// RtImageExport
//
// Shared tonemap+save logic for a completed offline ray-traced render
// (ViewportWidget::renderRayTracedOffline()'s linear-HDR output), factored
// out of RtRenderDialog::onExportClicked() so BatchRenderViewsDialog can
// reuse the exact same OpenEXR/tonemap code instead of duplicating it.
// ---------------------------------------------------------------------------
namespace RtImageExport
{
	// True if formatFilter (a QFileDialog-style filter string, e.g.
	// "OpenEXR Image (*.exr)", or just "*.exr") names the EXR format - same
	// substring convention RtRenderDialog::onExportClicked() already used.
	bool isExrFilter(const QString& formatFilter);

	// Maps formatFilter to the short format string QImage::save() expects
	// ("PNG"/"JPG"/"BMP"/"TIFF") - defaults to "PNG" if nothing else matches,
	// same fallback RtRenderDialog::onExportClicked() already used.
	QString ldrFormatFromFilter(const QString& formatFilter);

	// Writes a completed offline linear-HDR render to disk. For an EXR
	// filter, writes linearRgb raw/untouched (half-float, via
	// Imf::RgbaOutputFile) - no tonemap applied, matching the raw-HDR
	// convention every EXR export in this app already follows. For an LDR
	// filter (PNG/JPG/BMP/TIFF), tonemaps every pixel via RtTonemap::apply()
	// using the passed tonemap settings (same five parameters
	// ViewportWidget::rayTracingToneMapSettings() fills) into a
	// QImage::Format_RGB888, then QImage::save(). Returns false, writing no
	// partial file, on any failure.
	bool saveOfflineRender(const std::vector<glm::vec3>& linearRgb, int width, int height,
		const QString& path, const QString& formatFilter,
		bool hdrToneMapping, bool gammaCorrection, float screenGamma,
		float iblExposure, int toneMapMode);
}
