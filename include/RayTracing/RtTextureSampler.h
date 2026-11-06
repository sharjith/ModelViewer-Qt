// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "RtSceneSnapshot.h"
#include "RtTextureFilter.h"
#include <algorithm>
#include <cmath>

// Pure CPU sampler shared with the renderer regression target. Pixel rows,
// UV flip/transforms and wrap rules retain the existing path-tracer contract.
// Authored nearest/linear and mip filters now select the appropriate lookup.
namespace RtTextureSampling
{
	// Applies the texture's actual declared wrap mode instead of assuming
	// GL_REPEAT unconditionally - a texture authored as a single centered
	// decal (CLAMP_TO_EDGE) tiled repeatedly across the whole surface when
	// wrap mode was ignored (a real, previously-unhandled gap).
	inline float applyWrap(float coord, unsigned int mode)
	{
		constexpr unsigned int kClampToEdge    = 0x812Fu;
		constexpr unsigned int kMirroredRepeat = 0x8370u;

		if (mode == kClampToEdge)
			return std::clamp(coord, 0.0f, 1.0f);

		if (mode == kMirroredRepeat)
		{
			const float t = std::fmod(std::abs(coord), 2.0f);
			return (t > 1.0f) ? (2.0f - t) : t;
		}

		// GL_REPEAT (0x2901) and anything unrecognized.
		return coord - std::floor(coord);
	}

	// Wraps an integer texel index (which may be out-of-range - the whole
	// point of this function is resolving the neighbor-texel indices a
	// bilinear fetch needs at a tile edge/corner) according to the same wrap
	// mode applyWrap() uses for the continuous coordinate. Needed because a
	// bilinear sample near a REPEAT-wrapped tile's edge must blend with the
	// texel from the OPPOSITE edge (not clamp to it), and near a MIRRORED_
	// REPEAT edge must reflect back into the tile - a plain std::clamp would
	// silently behave like CLAMP_TO_EDGE for every wrap mode.
	inline int wrapTexelIndex(int idx, int size, unsigned int mode)
	{
		constexpr unsigned int kClampToEdge    = 0x812Fu;
		constexpr unsigned int kMirroredRepeat = 0x8370u;

		if (mode == kClampToEdge)
			return std::clamp(idx, 0, size - 1);

		if (mode == kMirroredRepeat)
		{
			const int period = 2 * size;
			int m = idx % period;
			if (m < 0) m += period;
			return (m < size) ? m : (period - 1 - m);
		}

		// GL_REPEAT and anything unrecognized.
		int m = idx % size;
		if (m < 0) m += size;
		return m;
	}

	inline glm::vec4 bilinearSampleLevel(int width, int height, const std::vector<uint8_t>& rgba8,
		unsigned int wrapS, unsigned int wrapT, const glm::vec2& st)
	{
		// Standard half-texel-centered bilinear: texel (0,0)'s center sits at
		// continuous position 0.5, matching GL's own texture-unit convention.
		const float fx = st.x * width  - 0.5f;
		const float fy = st.y * height - 0.5f;
		const int x0 = static_cast<int>(std::floor(fx));
		const int y0 = static_cast<int>(std::floor(fy));
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		// Each of the 2x2 neighbor texels is wrapped independently (not
		// clamped) - see wrapTexelIndex()'s doc comment for why a plain
		// clamp would be wrong for REPEAT/MIRRORED_REPEAT at a tile edge.
		auto texel = [&](int x, int y) -> glm::vec4
		{
			x = wrapTexelIndex(x, width,  wrapS);
			y = wrapTexelIndex(y, height, wrapT);
			const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
			return glm::vec4(
				rgba8[idx + 0] / 255.0f,
				rgba8[idx + 1] / 255.0f,
				rgba8[idx + 2] / 255.0f,
				rgba8[idx + 3] / 255.0f);
		};

		const glm::vec4 c00 = texel(x0,     y0);
		const glm::vec4 c10 = texel(x0 + 1, y0);
		const glm::vec4 c01 = texel(x0,     y0 + 1);
		const glm::vec4 c11 = texel(x0 + 1, y0 + 1);

		const glm::vec4 top    = glm::mix(c00, c10, tx);
		const glm::vec4 bottom = glm::mix(c01, c11, tx);
		return glm::mix(top, bottom, ty);
	}

    inline glm::vec4 sampleLevel(int width, int height, const std::vector<uint8_t>& rgba8,
        unsigned int wrapS, unsigned int wrapT, const glm::vec2& st, bool nearest)
    {
        if (!nearest)
            return bilinearSampleLevel(width, height, rgba8, wrapS, wrapT, st);
        const int x = wrapTexelIndex(static_cast<int>(std::floor(st.x * width)), width, wrapS);
        const int y = wrapTexelIndex(static_cast<int>(std::floor(st.y * height)), height, wrapT);
        const size_t index = (static_cast<size_t>(y) * width + x) * 4;
        return glm::vec4(rgba8[index], rgba8[index + 1], rgba8[index + 2], rgba8[index + 3]) / 255.0f;
    }
	inline glm::vec4 sampleTexture(const RtTextureSample& tex, const glm::vec2 (&texCoords)[4], float lod = 0.0f)
	{
		if (tex.width <= 0 || tex.height <= 0 || tex.rgba8.empty())
			return glm::vec4(1.0f);

		// A texture's KHR-declared texCoordIndex can reference any of the
		// mesh's 4 UV channels - hardcoding channel 0 for every texture
		// silently samples the wrong UV set whenever a material uses a
		// non-zero channel (this was a real, previously-unnoticed bug).
		const glm::vec2 rawUv = texCoords[std::clamp(tex.texCoordIndex, 0, 3)];

		// Shader's single explicit pre-transform UV.y flip.
		glm::vec2 uv(rawUv.x, 1.0f - rawUv.y);

		// KHR_texture_transform order: scale, then rotate, then offset.
		glm::vec2 st = uv * tex.uvScale;
		if (tex.uvRotation != 0.0f)
		{
			const float c = std::cos(tex.uvRotation);
			const float s = std::sin(tex.uvRotation);
			st = glm::vec2(st.x * c + st.y * s, -st.x * s + st.y * c);
		}
		st += tex.uvOffset;

		// Wrap per-axis using the texture's actual declared wrapS/wrapT.
		st.x = applyWrap(st.x, tex.wrapS);
		st.y = applyWrap(st.y, tex.wrapT);

        const auto selection = RtTextureFiltering::select(tex.magFilter, tex.minFilter,
            lod, static_cast<int>(tex.mips.size()));
        auto levelSample = [&](int level) -> glm::vec4
        {
            if (level == 0 || tex.mips.empty())
                return sampleLevel(tex.width, tex.height, tex.rgba8, tex.wrapS, tex.wrapT, st, selection.nearest);
            const auto& mip = tex.mips[level];
            return sampleLevel(mip.width, mip.height, mip.rgba8, tex.wrapS, tex.wrapT, st, selection.nearest);
        };
        const glm::vec4 first = levelSample(selection.level0);
        if (selection.blend == 0.0f || selection.level0 == selection.level1)
            return first;
        return glm::mix(first, levelSample(selection.level1), selection.blend);
    }
}
