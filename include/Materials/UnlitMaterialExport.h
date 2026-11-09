// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <assimp/material.h>

namespace UnlitMaterialExport {
// Apply to a newly created export material. Assimp's glTF writer selects
// KHR_materials_unlit from the shading model, not the glTF import flag alone.
inline void apply(aiMaterial& target, bool unlit)
{
    if (!unlit) return;
    const int enabled = 1;
    const aiShadingMode shading = aiShadingMode_Unlit;
    target.AddProperty(&enabled, 1, "$mat.gltf.unlit", 0, 0);
    target.AddProperty(&shading, 1, AI_MATKEY_SHADING_MODEL);
}
}
