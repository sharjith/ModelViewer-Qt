#pragma once
#include <map>

class AdaptiveShadowMapper
{
public:
	enum QualityLevel
	{
		LOW_QUALITY,    // Sharp shadows, performance focus
		MEDIUM_QUALITY, // Balanced quality/performance
		HIGH_QUALITY,   // Soft, natural shadows
		ULTRA_QUALITY   // Cinematic quality shadows
	};

	struct ShadowQualityParams
	{
		int maxKernelSize;
		float softnessScale;
		float maxSoftnessClamp;
		float biasMin;
		float biasMax;
		float transitionRange;
		float gammaCorrection;
	};

	// Smoothed size-based quality scaling with interpolation
	float calculateSizeQualityScale(float boundingRadius) const;

	// Interpolated shadow quality parameters
	ShadowQualityParams getShadowQualityParams(float boundingRadius) const;

	// Get interpolated settings between quality levels
	ShadowQualityParams getShadowQualityParamsSmooth(float boundingRadius) const;

	float calculateShadowFactor(float boundingRadius, float lightDistance, float coverageHint = -1.0f);

	// Calculate adaptive shadow softness
	float calculateShadowSoftness(float boundingRadius);

	void setQuality(QualityLevel quality);
	QualityLevel quality() const { return currentQuality; }

private:
	float calculateShadowCoverage(float boundingRadius);

	// Helper for smooth interpolation
	float mix(float a, float b, float t) const;

	// Smooth step function for gradual transitions
	float smoothstep(float edge0, float edge1, float x) const;

private:
	struct QualitySettings
	{
		// Base quality settings
		float texelsPerUnit;
		float maxShadowFactor;
		int maxResolution;
		float softnessMultiplier;

		// Shadow sampling settings
		int baseKernelSize;
		float softnessScale;
		float maxSoftnessClamp;
		float biasMin;
		float biasMax;
		float transitionRange;
		float gammaCorrection;

		// Size scaling factors
		float smallObjectBoost;      // Quality boost for small objects
		float largeObjectReduction;  // Quality reduction for large objects
		float sizeTransitionRange;   // Range for size-based transitions
	};

	std::map<QualityLevel, QualitySettings> qualityMap = {
		{LOW_QUALITY, {
			1.0f, 2.0f, 2048, 0.8f,
			3, 0.1f, 2.0f, 0.005f, 0.015f, 0.002f, 0.8f,
			1.2f, 0.9f, 20.0f
		}},
		{MEDIUM_QUALITY, {
			2.0f, 4.0f, 4096, 1.0f,
			4, 0.15f, 3.5f, 0.002f, 0.008f, 0.003f, 0.75f,
			1.15f, 0.92f, 30.0f
		}},
		{HIGH_QUALITY, {
			3.0f, 6.0f, 6144, 1.15f,
			5, 0.2f, 4.0f, 0.001f, 0.006f, 0.004f, 0.7f,
			1.1f, 0.95f, 40.0f
		}},
		{ULTRA_QUALITY, {
			4.0f, 8.0f, 8192, 1.25f,
			6, 0.25f, 5.0f, 0.0005f, 0.004f, 0.005f, 0.65f,
			1.05f, 0.98f, 50.0f
		}}
	};

	QualityLevel currentQuality = MEDIUM_QUALITY;
};
