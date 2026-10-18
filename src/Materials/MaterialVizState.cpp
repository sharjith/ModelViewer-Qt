#include "MaterialVizState.h"

MaterialVizState::MaterialVizState()
    : _hasTextureAlpha(false)
    , _baseThicknessFactor(0.f)
    , _baseAttenuationDistance(std::numeric_limits<float>::infinity())
{
}

void MaterialVizState::cacheBaseVolumeProperties()
{
    _baseThicknessFactor     = _material.thicknessFactor();
    _baseAttenuationDistance = _material.attenuationDistance();
}

void MaterialVizState::applyScaledVolumeProperties()
{
    // The shader applies volume scale through modelMatrix; restore authored values here.
    _material.setThicknessFactor(_baseThicknessFactor);
    _material.setAttenuationDistance(_baseAttenuationDistance);
}

const Material* MaterialVizState::materialForVariant(int variantIndex, int originalMaterialIndex) const
{
    if (_variantMappings.isEmpty())
        return nullptr;

    if (variantIndex < 0)
    {
        auto it = _allVariantMaterials.find(originalMaterialIndex);
        return (it != _allVariantMaterials.end()) ? &it.value() : nullptr;
    }

    for (const GltfVariantMapping& vm : _variantMappings)
    {
        if (vm.variantIndices.contains(variantIndex))
        {
            auto it = _allVariantMaterials.find(vm.materialIndex);
            return (it != _allVariantMaterials.end()) ? &it.value() : nullptr;
        }
    }
    return nullptr;
}
