// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <vector>

namespace BlendDepthPolicy {
// RGB colour attributes and the white fallback are not transparent coverage.
// Evaluate once on geometry upload, not by rescanning vertices for every draw.
inline bool hasTransparentVertexAlpha(const std::vector<float>& rgba)
{
    for (std::size_t i = 3; i < rgba.size(); i += 4)
        if (rgba[i] < 1.0f) return true;
    return false;
}

// Preserve the existing texture-only depth policy. The vertex-alpha feature
// needs depth-off coverage only where an Alpha material actually uses it.
// This is a compatibility policy, not order-independent transparency.
inline bool disableDepthWrites(float opacity, bool alphaBlend, bool vertexTransparency)
{
    return opacity < 0.999f || (alphaBlend && vertexTransparency);
}
}
