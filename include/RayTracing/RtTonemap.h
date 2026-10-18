#pragma once

#include <algorithm>

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// RtTonemap
//
// Direct C++ port of shaders/ray_traced_present.frag's applyToneMapping()
// and its operators, verbatim - same tonemap curves raster/the live PT
// viewport use, just running on the CPU instead of in the presentation
// shader. Needed specifically for offline ray-traced exports to ordinary
// LDR formats (PNG/JPEG/BMP/TIFF): those exports can't rely on grabbing an
// already-tonemapped GPU framebuffer (see RtRenderDialog::onExportClicked()'s
// doc comments) once the export resolution is decoupled from the live
// viewport's own on-screen size, so the tonemap has to happen here instead.
// EXR export never needs this - it writes the untouched linear buffer.
//
// toneMapMode mirrors RenderEnums.h's HDRToneMapMode ordinal exactly, same
// convention ray_traced_present.frag's own uniform uses:
// 0=KhronosPbrNeutral 1=ACES_Narkowicz 2=ACES_Hill 3=ACES_Hill_ExposureBoost
// 4=Uncharted2 5=Reinhard
// ---------------------------------------------------------------------------
namespace RtTonemap
{
	inline glm::vec3 toneMapACES_Narkowicz(glm::vec3 color)
	{
		const float A = 2.51f, B = 0.03f, C = 2.43f, D = 0.59f, E = 0.14f;
		return glm::clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0f, 1.0f);
	}

	inline glm::vec3 RRTAndODTFit(const glm::vec3& color)
	{
		const glm::vec3 a = color * (color + 0.0245786f) - 0.000090537f;
		const glm::vec3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
		return a / b;
	}

	inline glm::vec3 toneMapACES_Hill(glm::vec3 color)
	{
		static const glm::mat3 ACESInputMat(
			0.59719f, 0.07600f, 0.02840f,
			0.35458f, 0.90834f, 0.13383f,
			0.04823f, 0.01566f, 0.83777f);
		static const glm::mat3 ACESOutputMat(
			1.60475f, -0.10208f, -0.00327f,
			-0.53108f, 1.10813f, -0.07276f,
			-0.07367f, -0.00605f, 1.07602f);

		color = ACESInputMat * color;
		color = RRTAndODTFit(color);
		color = ACESOutputMat * color;
		return glm::clamp(color, 0.0f, 1.0f);
	}

	inline glm::vec3 toneMap_KhronosPbrNeutral(glm::vec3 color)
	{
		const float startCompression = 0.8f - 0.04f;
		const float desaturation = 0.15f;

		const float x = std::min({ color.r, color.g, color.b });
		const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
		color -= offset;

		const float peak = std::max({ color.r, color.g, color.b });
		if (peak < startCompression)
			return color;

		const float d = 1.0f - startCompression;
		const float newPeak = 1.0f - d * d / (peak + d - startCompression);
		color *= newPeak / peak;

		const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
		return glm::mix(color, glm::vec3(newPeak), g);
	}

	inline glm::vec3 uncharted2ToneMapping(glm::vec3 color)
	{
		const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f, W = 11.2f;

		color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
		const float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
		return color / white;
	}

	// Mirrors applyToneMapping() exactly - hdrToneMapping/gammaCorrection/
	// screenGamma/iblExposure/toneMapMode are the same live settings
	// SceneRenderController exposes (see RtPresenter::draw()'s call site for
	// the interactive/on-screen equivalent of this function).
	inline glm::vec3 apply(glm::vec3 color, bool hdrToneMapping, bool gammaCorrection,
		float screenGamma, float iblExposure, int toneMapMode)
	{
		if (hdrToneMapping)
		{
			color *= iblExposure;

			switch (toneMapMode)
			{
			case 0: color = toneMap_KhronosPbrNeutral(color); break;
			case 1: color = toneMapACES_Narkowicz(color); break;
			case 2: color = toneMapACES_Hill(color); break;
			case 3: color = toneMapACES_Hill(color / 0.6f); break; // exposure boost, see ray_traced_present.frag
			case 4: color = uncharted2ToneMapping(color); break;
			default: color = color / (color + glm::vec3(1.0f)); break; // Reinhard
			}
		}

		if (gammaCorrection)
			color = glm::pow(glm::max(color, glm::vec3(0.0f)), glm::vec3(1.0f / screenGamma));

		return color;
	}
}
