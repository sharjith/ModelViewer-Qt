// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Original shared CPU/CUDA filter selection. Values are the raw sampler enums
// already carried by Material::Texture; this header requires no GL or Qt types.
#ifdef __CUDACC__
#define MV_RT_FILTER_INLINE __host__ __device__ inline
#else
#define MV_RT_FILTER_INLINE inline
#endif

namespace RtTextureFiltering
{
struct Selection
{
    int level0 = 0;
    int level1 = 0;
    float blend = 0.0f;
    bool nearest = false;
};

MV_RT_FILTER_INLINE Selection select(unsigned int magFilter, unsigned int minFilter,
    float lod, int mipCount)
{
    Selection result;
    // The existing ray-footprint estimate is nonnegative. Zero (including
    // shadow/opacity calls with no footprint) uses magnification at base level.
    if (!(lod > 0.0f))
    {
        result.nearest = magFilter == 0x2600u;
        return result;
    }
    result.nearest = minFilter == 0x2600u || minFilter == 0x2700u || minFilter == 0x2702u;
    if (minFilter == 0x2600u || minFilter == 0x2601u || mipCount <= 1)
        return result;
    const int last = mipCount - 1;
    // Saturate before integer conversion (also handles positive infinity).
    const float level = lod < static_cast<float>(last) ? lod : static_cast<float>(last);
    if (minFilter == 0x2700u || minFilter == 0x2701u)
    {
        result.level0 = static_cast<int>(level + 0.5f);
        result.level1 = result.level0;
        return result;
    }
    // MIPMAP_LINEAR, including the existing default for unknown filter values.
    result.level0 = static_cast<int>(level);
    result.level1 = result.level0 < last ? result.level0 + 1 : last;
    result.blend = level - static_cast<float>(result.level0);
    return result;
}
}
#undef MV_RT_FILTER_INLINE
