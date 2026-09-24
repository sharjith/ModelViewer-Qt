#include "RtImageExport.h"
#include "RtTonemap.h"

#include <QImage>

#include <ImfRgbaFile.h>
#include <ImfArray.h>

#include <algorithm>

namespace
{
	// Writes a linear HDR RGB buffer straight to an OpenEXR file - half-
	// float (Imf::Rgba) storage via the RgbaOutputFile convenience API,
	// moved here verbatim from RtRenderDialog.cpp's original local helper of
	// the same name/shape. Returns false (and leaves no partial file, since
	// OpenEXR only creates the file on first writePixels()) on any failure.
	bool writeExrFile(const QString& path, const std::vector<glm::vec3>& linearRgb, int width, int height)
	{
		if (width <= 0 || height <= 0 || linearRgb.size() != static_cast<size_t>(width) * height)
			return false;

		try
		{
			Imf::Array2D<Imf::Rgba> pixels(height, width);
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const glm::vec3& c = linearRgb[static_cast<size_t>(y) * width + x];
					pixels[y][x] = Imf::Rgba(c.r, c.g, c.b, 1.0f);
				}
			}

			Imf::RgbaOutputFile file(path.toUtf8().constData(), width, height, Imf::WRITE_RGBA);
			file.setFrameBuffer(&pixels[0][0], 1, width);
			file.writePixels(height);
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
}

namespace RtImageExport
{
	bool isExrFilter(const QString& formatFilter)
	{
		return formatFilter.contains(QStringLiteral("*.exr"));
	}

	QString ldrFormatFromFilter(const QString& formatFilter)
	{
		if (formatFilter.contains(QStringLiteral("*.jpg")))
			return QStringLiteral("JPG");
		if (formatFilter.contains(QStringLiteral("*.bmp")))
			return QStringLiteral("BMP");
		if (formatFilter.contains(QStringLiteral("*.tif")))
			return QStringLiteral("TIFF");
		return QStringLiteral("PNG");
	}

	bool saveOfflineRender(const std::vector<glm::vec3>& linearRgb, int width, int height,
		const QString& path, const QString& formatFilter,
		bool hdrToneMapping, bool gammaCorrection, float screenGamma,
		float iblExposure, int toneMapMode)
	{
		if (isExrFilter(formatFilter))
			return writeExrFile(path, linearRgb, width, height);

		if (width <= 0 || height <= 0 || linearRgb.size() != static_cast<size_t>(width) * height)
			return false;

		// The offline path never touches the GPU/live framebuffer at all, so
		// for LDR formats the linear buffer has to be tonemapped here in
		// plain C++ (RtTonemap.h, a direct port of ray_traced_present.frag)
		// using the passed live settings - there's no already-tonemapped
		// framebuffer to grab.
		QImage image(width, height, QImage::Format_RGB888);
		for (int y = 0; y < height; ++y)
		{
			uchar* line = image.scanLine(y);
			for (int x = 0; x < width; ++x)
			{
				const glm::vec3& c = linearRgb[static_cast<size_t>(y) * width + x];
				const glm::vec3 mapped = RtTonemap::apply(c, hdrToneMapping, gammaCorrection, screenGamma, iblExposure, toneMapMode);
				line[x * 3 + 0] = static_cast<uchar>(std::clamp(mapped.r, 0.0f, 1.0f) * 255.0f + 0.5f);
				line[x * 3 + 1] = static_cast<uchar>(std::clamp(mapped.g, 0.0f, 1.0f) * 255.0f + 0.5f);
				line[x * 3 + 2] = static_cast<uchar>(std::clamp(mapped.b, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
		return image.save(path, ldrFormatFromFilter(formatFilter).toUtf8().constData());
	}
}
