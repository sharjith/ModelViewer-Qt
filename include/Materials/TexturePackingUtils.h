#pragma once

#include <QString>
#include <QImage>

// Utility class for packing separate Metallic and Roughness textures into
// a single glTF-compliant packed texture (Metallic in Blue, Roughness in Green)
class TexturePackingUtils
{
public:
	// Pack ORM (Occlusion, Roughness, Metallic) into single texture
	// Returns packed QImage with channels: R=Occlusion, G=Roughness, B=Metallic, A=255
	// If occlusionPath is empty, creates default white (no occlusion)
	// invertRoughness: if true, inverts roughness values (255 - value) for smoothness-to-roughness conversion
	// Returns empty QImage on failure; outErrorMessage contains diagnostic info
	static QImage packORM(
		const QString& occlusionPath,
		const QString& roughnessPath,
		const QString& metallicPath,
		QString& outErrorMessage,
		bool invertRoughness = true);

	// Main packing function: combines separate metallic and roughness textures
	// Returns packed QImage with channels: R=0, G=Roughness, B=Metallic, A=255
	// invertRoughness: if true, inverts roughness values (255 - value) for smoothness-to-roughness conversion
	// Returns empty QImage on failure; outErrorMessage contains diagnostic info
	static QImage packMetallicRoughness(
		const QString& metallicPath,
		const QString& roughnessPath,
		QString& outErrorMessage,
		bool invertRoughness = true);

	// Pre-flight validation: checks if both files are readable and compatible
	// Returns true if packing is likely to succeed, false otherwise
	// outErrorMessage explains why validation failed (useful for UI feedback)
	static bool validateTextureCompatibility(
		const QString& metallicPath,
		const QString& roughnessPath,
		QString& outErrorMessage);

private:
	// Extract single channel from image (handles RGB, RGBA, Grayscale)
	// Returns grayscale QImage with values from the specified channel
	static QImage extractChannel(const QImage& source, int channelIndex);

	// Resize image to target dimensions using high-quality scaling
	// channelMode: if true, convert to grayscale first (for proper channel extraction)
	static QImage resizeToTarget(const QImage& source, int targetWidth, int targetHeight, bool channelMode = true);

	// Get dimensions of an image file without fully loading it
	static bool getImageDimensions(const QString& path, int& outWidth, int& outHeight, QString& outErrorMessage);

	// Helper: log diagnostic message (wrapper around qWarning for consistency)
	static void logMessage(const QString& message);
};
