#include "TexturePackingUtils.h"
#include <QFileInfo>
#include <QColor>
#include <QDebug>
#include <QFile>

QImage TexturePackingUtils::packORM(
	const QString& occlusionPath,
	const QString& roughnessPath,
	const QString& metallicPath,
	QString& outErrorMessage,
	bool invertRoughness)
{
	outErrorMessage.clear();

	// Load roughness (required)
	QImage roughnessImg(roughnessPath);
	if (roughnessImg.isNull())
	{
		outErrorMessage = QString("Failed to load roughness texture: %1").arg(roughnessPath);
		logMessage(outErrorMessage);
		return QImage();
	}

	// Load metallic (required)
	QImage metallicImg(metallicPath);
	if (metallicImg.isNull())
	{
		outErrorMessage = QString("Failed to load metallic texture: %1").arg(metallicPath);
		logMessage(outErrorMessage);
		return QImage();
	}

	// Load occlusion (optional - create white default if missing)
	QImage occlusionImg;
	bool hasOcclusion = false;

	if (!occlusionPath.isEmpty() && QFile::exists(occlusionPath))
	{
		occlusionImg = QImage(occlusionPath);
		if (!occlusionImg.isNull())
		{
			hasOcclusion = true;
		}
		else
		{
			logMessage(QString("Warning: Failed to load occlusion texture: %1, using white default").arg(occlusionPath));
		}
	}

	// Determine target dimensions (use maximum of all loaded textures)
	int targetWidth = metallicImg.width();
	int targetHeight = metallicImg.height();

	if (roughnessImg.width() > targetWidth || roughnessImg.height() > targetHeight)
	{
		targetWidth = qMax(targetWidth, roughnessImg.width());
		targetHeight = qMax(targetHeight, roughnessImg.height());
	}

	if (hasOcclusion && (occlusionImg.width() > targetWidth || occlusionImg.height() > targetHeight))
	{
		targetWidth = qMax(targetWidth, occlusionImg.width());
		targetHeight = qMax(targetHeight, occlusionImg.height());
	}

	// Resize all images to match target dimensions if needed
	if (metallicImg.width() != targetWidth || metallicImg.height() != targetHeight)
	{
		metallicImg = resizeToTarget(metallicImg, targetWidth, targetHeight, true);
		if (metallicImg.isNull())
		{
			outErrorMessage = QString("Failed to resize metallic texture to %1x%2").arg(targetWidth).arg(targetHeight);
			logMessage(outErrorMessage);
			return QImage();
		}
	}
	else
	{
		metallicImg = extractChannel(metallicImg, 0);
	}

	if (roughnessImg.width() != targetWidth || roughnessImg.height() != targetHeight)
	{
		roughnessImg = resizeToTarget(roughnessImg, targetWidth, targetHeight, true);
		if (roughnessImg.isNull())
		{
			outErrorMessage = QString("Failed to resize roughness texture to %1x%2").arg(targetWidth).arg(targetHeight);
			logMessage(outErrorMessage);
			return QImage();
		}
	}
	else
	{
		roughnessImg = extractChannel(roughnessImg, 0);
	}

	if (hasOcclusion)
	{
		if (occlusionImg.width() != targetWidth || occlusionImg.height() != targetHeight)
		{
			occlusionImg = resizeToTarget(occlusionImg, targetWidth, targetHeight, true);
			if (occlusionImg.isNull())
			{
				outErrorMessage = QString("Failed to resize occlusion texture to %1x%2").arg(targetWidth).arg(targetHeight);
				logMessage(outErrorMessage);
				return QImage();
			}
		}
		else
		{
			occlusionImg = extractChannel(occlusionImg, 0);
		}
	}

	// Create packed RGBA image using direct scanLine access for efficiency
	QImage packed(targetWidth, targetHeight, QImage::Format_RGBA8888);
	if (packed.isNull())
	{
		outErrorMessage = QString("Failed to create packed image (%1x%2)").arg(targetWidth).arg(targetHeight);
		logMessage(outErrorMessage);
		return QImage();
	}

	// Pack channels using direct byte access: R=Occlusion, G=Roughness, B=Metallic, A=255
	for (int y = 0; y < targetHeight; ++y)
	{
		const uchar* occlusionLine = hasOcclusion ? occlusionImg.scanLine(y) : nullptr;
		const uchar* roughnessLine = roughnessImg.scanLine(y);
		const uchar* metallicLine = metallicImg.scanLine(y);
		uchar* packedLine = packed.scanLine(y);

		for (int x = 0; x < targetWidth; ++x)
		{
			// Pack directly into RGBA8888 format: [R, G, B, A] at offsets 0, 1, 2, 3
			packedLine[x * 4 + 0] = hasOcclusion ? occlusionLine[x] : 255;  // R = Occlusion (or white if no AO)

			// Roughness channel: optionally invert based on invertRoughness flag
			// Material libraries often store SMOOTHNESS (inverse of roughness)
			// When invertRoughness=true: convert smoothness to roughness (roughness = 1.0 - smoothness)
			uchar roughValue = roughnessLine[x];
			packedLine[x * 4 + 1] = invertRoughness ? (255 - roughValue) : roughValue;  // G = Roughness

			packedLine[x * 4 + 2] = metallicLine[x];                        // B = Metallic
			packedLine[x * 4 + 3] = 255;                                    // A = 255 (fully opaque)
		}
	}

	logMessage(QString("Successfully packed ORM textures: %1x%2")
		.arg(targetWidth)
		.arg(targetHeight));

	return packed;
}

QImage TexturePackingUtils::packMetallicRoughness(
	const QString& metallicPath,
	const QString& roughnessPath,
	QString& outErrorMessage,
	bool invertRoughness)
{
	outErrorMessage.clear();

	// Validate inputs
	QString validationError;
	if (!validateTextureCompatibility(metallicPath, roughnessPath, validationError))
	{
		outErrorMessage = validationError;
		logMessage(QString("Validation failed: %1").arg(validationError));
		return QImage();
	}

	// Load metallic texture
	QImage metallicImg(metallicPath);
	if (metallicImg.isNull())
	{
		outErrorMessage = QString("Failed to load metallic texture: %1").arg(metallicPath);
		logMessage(outErrorMessage);
		return QImage();
	}

	// Load roughness texture
	QImage roughnessImg(roughnessPath);
	if (roughnessImg.isNull())
	{
		outErrorMessage = QString("Failed to load roughness texture: %1").arg(roughnessPath);
		logMessage(outErrorMessage);
		return QImage();
	}

	// Determine target dimensions (use maximum of both)
	int targetWidth = qMax(metallicImg.width(), roughnessImg.width());
	int targetHeight = qMax(metallicImg.height(), roughnessImg.height());

	// Resize if dimensions differ
	if (metallicImg.width() != targetWidth || metallicImg.height() != targetHeight)
	{
		metallicImg = resizeToTarget(metallicImg, targetWidth, targetHeight, true);
		if (metallicImg.isNull())
		{
			outErrorMessage = QString("Failed to resize metallic texture to %1x%2").arg(targetWidth).arg(targetHeight);
			logMessage(outErrorMessage);
			return QImage();
		}
	}
	else
	{
		metallicImg = extractChannel(metallicImg, 0);
	}

	if (roughnessImg.width() != targetWidth || roughnessImg.height() != targetHeight)
	{
		roughnessImg = resizeToTarget(roughnessImg, targetWidth, targetHeight, true);
		if (roughnessImg.isNull())
		{
			outErrorMessage = QString("Failed to resize roughness texture to %1x%2").arg(targetWidth).arg(targetHeight);
			logMessage(outErrorMessage);
			return QImage();
		}
	}
	else
	{
		roughnessImg = extractChannel(roughnessImg, 0);
	}

	// Create packed RGBA image
	QImage packed(targetWidth, targetHeight, QImage::Format_RGBA8888);
	if (packed.isNull())
	{
		outErrorMessage = QString("Failed to create packed image (%1x%2)").arg(targetWidth).arg(targetHeight);
		logMessage(outErrorMessage);
		return QImage();
	}

	// Pack channels: R=0, G=Roughness, B=Metallic, A=255
	for (int y = 0; y < targetHeight; ++y)
	{
		for (int x = 0; x < targetWidth; ++x)
		{
			int metalVal = qGray(metallicImg.pixel(x, y));
			int roughVal = qGray(roughnessImg.pixel(x, y));

			// Roughness channel: optionally invert based on invertRoughness flag
			// When invertRoughness=true: convert smoothness to roughness (roughness = 1.0 - smoothness)
			int finalRoughness = invertRoughness ? (255 - roughVal) : roughVal;
			QColor color(0, finalRoughness, metalVal, 255);
			packed.setPixel(x, y, color.rgba());
		}
	}

	logMessage(QString("Successfully packed M/R textures: %1x%2 (Metallic: %3, Roughness: %4)")
		.arg(targetWidth)
		.arg(targetHeight)
		.arg(QFileInfo(metallicPath).fileName())
		.arg(QFileInfo(roughnessPath).fileName()));

	return packed;
}

bool TexturePackingUtils::validateTextureCompatibility(
	const QString& metallicPath,
	const QString& roughnessPath,
	QString& outErrorMessage)
{
	outErrorMessage.clear();

	// Check file existence
	if (!QFile::exists(metallicPath))
	{
		outErrorMessage = QString("Metallic texture file not found: %1").arg(metallicPath);
		return false;
	}

	if (!QFile::exists(roughnessPath))
	{
		outErrorMessage = QString("Roughness texture file not found: %1").arg(roughnessPath);
		return false;
	}

	// Try to load images to verify they're valid texture files
	QImage metallicTest(metallicPath);
	if (metallicTest.isNull())
	{
		outErrorMessage = QString("Metallic texture is not a valid image file: %1").arg(metallicPath);
		return false;
	}

	QImage roughnessTest(roughnessPath);
	if (roughnessTest.isNull())
	{
		outErrorMessage = QString("Roughness texture is not a valid image file: %1").arg(roughnessPath);
		return false;
	}

	// Check dimensions are reasonable (allow mismatch - will be resolved via resize)
	if (metallicTest.width() <= 0 || metallicTest.height() <= 0)
	{
		outErrorMessage = QString("Metallic texture has invalid dimensions: %1x%2")
			.arg(metallicTest.width()).arg(metallicTest.height());
		return false;
	}

	if (roughnessTest.width() <= 0 || roughnessTest.height() <= 0)
	{
		outErrorMessage = QString("Roughness texture has invalid dimensions: %1x%2")
			.arg(roughnessTest.width()).arg(roughnessTest.height());
		return false;
	}

	return true;
}

QImage TexturePackingUtils::extractChannel(const QImage& source, int channelIndex)
{
	if (source.isNull())
		return QImage();

	// Convert to RGB format for consistent channel access
	QImage rgb = source.convertToFormat(QImage::Format_RGB888);
	QImage gray(rgb.width(), rgb.height(), QImage::Format_Grayscale8);

	for (int y = 0; y < rgb.height(); ++y)
	{
		const uchar* rgbLine = rgb.scanLine(y);
		uchar* grayLine = gray.scanLine(y);

		for (int x = 0; x < rgb.width(); ++x)
		{
			// Use proper grayscale conversion (luminosity-weighted)
			// This matches standard image processing and Qt's qGray() formula
			int r = rgbLine[x * 3 + 0];
			int g = rgbLine[x * 3 + 1];
			int b = rgbLine[x * 3 + 2];
			grayLine[x] = (r * 77 + g * 150 + b * 29) >> 8;  // ITU-R BT.601 luminosity
		}
	}

	return gray;
}

QImage TexturePackingUtils::resizeToTarget(const QImage& source, int targetWidth, int targetHeight, bool channelMode)
{
	if (source.isNull() || targetWidth <= 0 || targetHeight <= 0)
		return QImage();

	QImage resized = source.scaled(targetWidth, targetHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	// If size doesn't match exactly (due to aspect ratio), center it
	if (resized.width() != targetWidth || resized.height() != targetHeight)
	{
		QImage padded(targetWidth, targetHeight, QImage::Format_RGBA8888);
		padded.fill(Qt::black);

		int x = (targetWidth - resized.width()) / 2;
		int y = (targetHeight - resized.height()) / 2;

		for (int ry = 0; ry < resized.height(); ++ry)
		{
			for (int rx = 0; rx < resized.width(); ++rx)
			{
				padded.setPixel(x + rx, y + ry, resized.pixel(rx, ry));
			}
		}
		resized = padded;
	}

	if (channelMode)
	{
		// Convert to grayscale using proper luminosity weighting
		// This matches standard image processing (ITU-R BT.601)
		QImage rgb = resized.convertToFormat(QImage::Format_RGB888);
		QImage gray(rgb.width(), rgb.height(), QImage::Format_Grayscale8);

		for (int y = 0; y < rgb.height(); ++y)
		{
			const uchar* rgbLine = rgb.scanLine(y);
			uchar* grayLine = gray.scanLine(y);

			for (int x = 0; x < rgb.width(); ++x)
			{
				// Use proper grayscale conversion (luminosity-weighted)
				int r = rgbLine[x * 3 + 0];
				int g = rgbLine[x * 3 + 1];
				int b = rgbLine[x * 3 + 2];
				grayLine[x] = (r * 11 + g * 16 + b * 5) >> 5;  // ITU-R BT.601
			}
		}

		resized = gray;
	}

	return resized;
}

bool TexturePackingUtils::getImageDimensions(const QString& path, int& outWidth, int& outHeight, QString& outErrorMessage)
{
	outErrorMessage.clear();
	outWidth = outHeight = 0;

	QImage img(path);
	if (img.isNull())
	{
		outErrorMessage = QString("Cannot determine dimensions of %1").arg(path);
		return false;
	}

	outWidth = img.width();
	outHeight = img.height();
	return true;
}

void TexturePackingUtils::logMessage(const QString& message)
{
	qWarning() << "[TexturePackingUtils]" << message;
}
