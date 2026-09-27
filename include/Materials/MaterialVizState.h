#pragma once

#include "Material.h"
#include "GltfVariantData.h"

#include <QMap>
#include <QVector>
#include <limits>
#include <vector>

// Shader-facing material state for a mesh.
//
// Owns the Material (PBR + ADS properties + texture IDs), the texture-alpha
// flag, and the KHR_materials_variants tables.  Volume thickness/attenuation
// base values are cached here so applyScaledVolumeProperties() can rescale them
// when the mesh is scaled without permanently corrupting the authored values.
//
// Intentionally free of GL resources, transform state, and geometry data.
class MaterialVizState
{
public:
    MaterialVizState();

    // ---- Core material --------------------------------------------------
    Material& material() { return _material; }
    const Material& material() const { return _material; }
    void setMaterial(const Material& m) { _material = m; }

    // ---- Texture-alpha flag ---------------------------------------------
    bool hasTextureAlpha() const { return _hasTextureAlpha; }
    void setHasTextureAlpha(bool v) { _hasTextureAlpha = v; }

    // ---- Volume properties cache ----------------------------------------
    // cacheBaseVolumeProperties() snapshots the current thickness/attenuation
    // values so they can be rescaled without losing the authored originals.
    void cacheBaseVolumeProperties();
    // applyScaledVolumeProperties() rescales cached values by the given uniform
    // scale factor and writes them back into _material.
    void applyScaledVolumeProperties();

    float baseThicknessFactor()     const { return _baseThicknessFactor; }
    float baseAttenuationDistance() const { return _baseAttenuationDistance; }

    // ---- KHR_materials_variants -----------------------------------------
    void setVariantMappings(const QVector<GltfVariantMapping>& m)   { _variantMappings = m; }
    const QVector<GltfVariantMapping>& variantMappings() const       { return _variantMappings; }

    void setAllVariantMaterials(const QMap<int, Material>& mats)   { _allVariantMaterials = mats; }
    const QMap<int, Material>& allVariantMaterials() const          { return _allVariantMaterials; }

    bool hasVariants() const { return !_variantMappings.isEmpty(); }

    // Returns a pointer to the pre-built Material for the given variant index,
    // or nullptr when this mesh has no mapping for that variant (keep default).
    // Pass variantIndex = -1 to retrieve the original/default material.
    const Material* materialForVariant(int variantIndex, int originalMaterialIndex) const;

    // ---- Texture list ---------------------------------------------------
    // Raw textures loaded from the source asset (by SceneMesh). Used to build
    // the optimised PrecomputedTexture binding cache during render setup.
    // SceneMesh keeps a reference alias _textures → this vector for zero
    // call-site churn (same pattern as Material& _material in SceneMesh).
    const std::vector<Material::Texture>& textures() const { return _textures; }
    std::vector<Material::Texture>&       textures()       { return _textures; }
    void setTextures(std::vector<Material::Texture> t)     { _textures = std::move(t); }

    // ---- ADS (Phong/Blinn-Phong) texture map GL IDs ---------------------
    // Separate from the PBR texture IDs stored in Material; set via
    // setDiffuseADSMap() / clearDiffuseADSMap() etc. on SceneMesh.
    unsigned int& diffuseADSMap()  { return _diffuseADSMap; }
    unsigned int& specularADSMap() { return _specularADSMap; }
    unsigned int& emissiveADSMap() { return _emissiveADSMap; }
    unsigned int& normalADSMap()   { return _normalADSMap; }
    unsigned int& heightADSMap()   { return _heightADSMap; }
    unsigned int& opacityADSMap()  { return _opacityADSMap; }

private:
    Material _material;
    bool       _hasTextureAlpha = false;
    float      _baseThicknessFactor     = 0.f;
    float      _baseAttenuationDistance = std::numeric_limits<float>::infinity();

    QVector<GltfVariantMapping>      _variantMappings;
    QMap<int, Material>            _allVariantMaterials;
    std::vector<Material::Texture> _textures;

    unsigned int _diffuseADSMap  = 0;
    unsigned int _specularADSMap = 0;
    unsigned int _emissiveADSMap = 0;
    unsigned int _normalADSMap   = 0;
    unsigned int _heightADSMap   = 0;
    unsigned int _opacityADSMap  = 0;
};
