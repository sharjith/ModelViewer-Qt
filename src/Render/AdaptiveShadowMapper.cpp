#include "AdaptiveShadowMapper.h"
#include <algorithm>
#include <cmath>

// Smoothed size-based quality scaling with interpolation
float AdaptiveShadowMapper::calculateSizeQualityScale(float boundingRadius) const
{
	const auto& settings = qualityMap.at(currentQuality);

	float smallThreshold = 5.0f;
	float largeThreshold = 100.0f;

	if (boundingRadius < smallThreshold)
	{
		// Smooth curve instead of linear scaling
		float t = boundingRadius / smallThreshold;
		t = smoothstep(0.0f, 1.0f, t); // S-curve for smooth transition
		return mix(settings.smallObjectBoost, 1.0f, t);
	}
	else if (boundingRadius > largeThreshold)
	{
		// Gentler reduction with smooth falloff
		float excess = (boundingRadius - largeThreshold) / largeThreshold;
		float reduction = 1.0f / (1.0f + excess * 0.3f); // gentler curve
		return std::max(reduction * settings.largeObjectReduction, 0.7f);
	}

	return 1.0f;
}

// Interpolated shadow quality parameters
AdaptiveShadowMapper::ShadowQualityParams AdaptiveShadowMapper::getShadowQualityParams(float boundingRadius) const
{
	const auto& settings = qualityMap.at(currentQuality);
	float sizeScale = calculateSizeQualityScale(boundingRadius);

	ShadowQualityParams params;

	// Gentler kernel scaling with minimum change
	float kernelFloat = settings.baseKernelSize * sizeScale;
	params.maxKernelSize = std::clamp(
		static_cast<int>(std::round(kernelFloat)),
		std::max(2, settings.baseKernelSize - 1),
		settings.baseKernelSize + 2
	);

	// Much gentler softness adjustment
	params.softnessScale = settings.softnessScale *
		mix(0.95f, 1.05f, (sizeScale - 0.7f) / 0.6f);
	params.maxSoftnessClamp = settings.maxSoftnessClamp *
		mix(0.9f, 1.1f, (sizeScale - 0.7f) / 0.6f);

	// Minimal bias adjustment
	float biasScale = mix(0.95f, 1.05f, 1.0f / sizeScale);
	params.biasMin = settings.biasMin * biasScale;
	params.biasMax = settings.biasMax * biasScale;
	params.transitionRange = settings.transitionRange * biasScale;

	// Very subtle gamma adjustment
	params.gammaCorrection = settings.gammaCorrection +
		(sizeScale - 1.0f) * 0.02f; // reduced from 0.05f

	return params;
}

// Get interpolated settings between quality levels
AdaptiveShadowMapper::ShadowQualityParams AdaptiveShadowMapper::getShadowQualityParamsSmooth(float boundingRadius) const
{
	// Calculate a continuous quality factor (0.0 to 3.0 for 4 levels)
	float qualityFloat = static_cast<float>(currentQuality);

	// Add size-based micro-adjustments
	float sizeScale = calculateSizeQualityScale(boundingRadius);
	if (sizeScale > 1.0f)
	{
		qualityFloat += (sizeScale - 1.0f) * 0.3f; // small boost
	}
	else if (sizeScale < 1.0f)
	{
		qualityFloat -= (1.0f - sizeScale) * 0.2f; // small reduction
	}

	// Clamp and get integer levels for interpolation
	qualityFloat = std::clamp(qualityFloat, 0.0f, 3.0f);
	int lowerLevel = static_cast<int>(std::floor(qualityFloat));
	int upperLevel = std::min(3, lowerLevel + 1);
	float t = qualityFloat - lowerLevel;

	// Interpolate between the two levels
	const auto& lower = qualityMap.at(static_cast<QualityLevel>(lowerLevel));
	const auto& upper = qualityMap.at(static_cast<QualityLevel>(upperLevel));

	ShadowQualityParams params{};
	params.maxKernelSize = static_cast<int>(
		std::round(mix(lower.baseKernelSize, upper.baseKernelSize, t))
		);
	params.softnessScale = mix(lower.softnessScale, upper.softnessScale, t);
	params.maxSoftnessClamp = mix(lower.maxSoftnessClamp, upper.maxSoftnessClamp, t);
	params.biasMin = mix(lower.biasMin, upper.biasMin, t);
	params.biasMax = mix(lower.biasMax, upper.biasMax, t);
	params.transitionRange = mix(lower.transitionRange, upper.transitionRange, t);
	params.gammaCorrection = mix(lower.gammaCorrection, upper.gammaCorrection, t);

	return params;
}

float AdaptiveShadowMapper::calculateShadowFactor(float boundingRadius, float lightDistance, float coverageHint)
{
	const auto& settings = qualityMap[currentQuality];

	float adjustedTexelsPerUnit = settings.texelsPerUnit;

	if (boundingRadius < 5.0f)
	{
		float smallObjectBoost = 5.0f / boundingRadius;
		adjustedTexelsPerUnit *= std::min(smallObjectBoost, 4.0f);
	}

	float shadowCoverage = (coverageHint > 0.0f) ? coverageHint : calculateShadowCoverage(boundingRadius);
	float requiredResolution = shadowCoverage * adjustedTexelsPerUnit;
	float shadowFactor = requiredResolution / 1024.0f;

	return std::clamp(shadowFactor, 2.0f, 8.0f);
}

float AdaptiveShadowMapper::calculateShadowSoftness(float boundingRadius)
{
	const auto& settings = qualityMap[currentQuality];

	return boundingRadius * 2 * 0.00015f * settings.softnessMultiplier;
}

void AdaptiveShadowMapper::setQuality(QualityLevel quality)
{
	currentQuality = quality;
}

float AdaptiveShadowMapper::calculateShadowCoverage(float boundingRadius)
{
	return boundingRadius * 4.0f; // Adjust as needed
}

// Helper for smooth interpolation
float AdaptiveShadowMapper::mix(float a, float b, float t) const
{
	return a + t * (b - a);
}

// Smooth step function for gradual transitions
float AdaptiveShadowMapper::smoothstep(float edge0, float edge1, float x) const
{
	float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
