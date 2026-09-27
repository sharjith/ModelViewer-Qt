#pragma once

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// GltfVariantData
//
// Data structures for KHR_materials_variants extension support.
//
// A glTF file may declare named material variants at the root level.  Each
// mesh primitive then carries a list of mappings: which material index to use
// for each variant.  The primitive's own "material" field remains the fallback
// (default) material shown when no variant is active.
//
// These structures are populated by AssImpModelLoader::parseGltfVariants()
// during import and stored in SceneGraph keyed by source file path.
// SceneMesh holds a per-mesh copy of the mappings and a pre-built
// Material for every material index referenced by its variants.
// ---------------------------------------------------------------------------

// Mapping for one mesh primitive: which material index is used by which
// subset of variants.  A single primitive may have several mappings (one
// per material that is referenced by at least one variant).
struct GltfVariantMapping
{
    int           materialIndex  = -1;   // index into aiScene::mMaterials[]
    QVector<int>  variantIndices;        // which variant indices use this material
};

// Built by AssImpMeshExporter for each exported mesh; consumed by GltfPostProcessor
// to write back the KHR_materials_variants extension after Assimp export.
struct MeshVariantExportEntry
{
    // Per-primitive mappings copied from RenderableMesh::variantMappings().
    QVector<GltfVariantMapping> variantMappings;

    // Maps allVariantMaterials key (compact aiScene mat index at import time)
    // to the JSON material array index in the exported file.
    // The default material's key (getOriginalMaterialIndex()) is also present here.
    QMap<int, int> matKeyToJsonMatIdx;

    bool hasVariants() const { return !variantMappings.isEmpty(); }
};

// All variant information for one loaded glTF/GLB file.
struct GltfVariantData
{
    QString     sourceFile;   // absolute path of the source file

    // Ordered list of variant names declared in
    // extensions.KHR_materials_variants.variants[].name
    QStringList variantNames;

    // Key:   aiScene::mMeshes[] index (flat primitive index, matching
    //        AssImpMeshData::sceneIndex before global-scene merging).
    // Value: variant→material mappings for that primitive.
    //        Empty entry means the primitive has no variant overrides.
    QHash<int, QVector<GltfVariantMapping>> meshVariantMappings;

    bool isEmpty() const { return variantNames.isEmpty(); }
};
