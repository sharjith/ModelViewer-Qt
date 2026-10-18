#include "HdrImageLoader.h"

#include "stb_image.h"

#include <ImfRgbaFile.h>
#include <ImfArray.h>
#include <ImathBox.h>

#include <QString>
#include <QDebug>

#include <cstdlib>

namespace HdrImageLoader
{
	bool isExr(const std::string& path)
	{
		return QString::fromStdString(path).endsWith(QStringLiteral(".exr"), Qt::CaseInsensitive);
	}

	namespace
	{
		float* loadExr(const std::string& path, int& outWidth, int& outHeight, int& outChannels, bool flipVertically)
		{
			try
			{
				Imf::RgbaInputFile file(path.c_str());
				const Imath::Box2i dw = file.dataWindow();
				const int width  = dw.max.x - dw.min.x + 1;
				const int height = dw.max.y - dw.min.y + 1;
				if (width <= 0 || height <= 0)
					return nullptr;

				Imf::Array2D<Imf::Rgba> pixels(height, width);
				// Standard OpenEXR idiom for a data window that doesn't
				// necessarily start at (0,0) - offset the base pointer so
				// readPixels()'s own (dw.min.y..dw.max.y) row range indexes
				// correctly into the array.
				file.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * width, 1, width);
				file.readPixels(dw.min.y, dw.max.y);

				// Allocated via plain malloc() (stb_image's own STBI_MALLOC,
				// unless this vcpkg build customized it, is the same) so the
				// caller's existing stbi_image_free() call works unchanged.
				float* data = static_cast<float*>(std::malloc(static_cast<size_t>(width) * height * 3 * sizeof(float)));
				if (!data)
					return nullptr;

				for (int y = 0; y < height; ++y)
				{
					const int srcY = flipVertically ? (height - 1 - y) : y;
					float* dstRow = data + static_cast<size_t>(y) * width * 3;
					for (int x = 0; x < width; ++x)
					{
						const Imf::Rgba& px = pixels[srcY][x];
						dstRow[x * 3 + 0] = static_cast<float>(px.r);
						dstRow[x * 3 + 1] = static_cast<float>(px.g);
						dstRow[x * 3 + 2] = static_cast<float>(px.b);
					}
				}

				outWidth = width;
				outHeight = height;
				outChannels = 3;
				return data;
			}
			catch (const std::exception& e)
			{
				qWarning().noquote() << "Failed to read EXR file:" << QString::fromStdString(path) << "-" << e.what();
				return nullptr;
			}
		}
	}

	float* load(const std::string& path, int& outWidth, int& outHeight, int& outChannels, bool flipVertically)
	{
		if (isExr(path))
			return loadExr(path, outWidth, outHeight, outChannels, flipVertically);

		stbi_set_flip_vertically_on_load(flipVertically);
		return stbi_loadf(path.c_str(), &outWidth, &outHeight, &outChannels, 0);
	}
}
