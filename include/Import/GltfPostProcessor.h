#pragma once


class SceneMesh;

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QString>
#include <QVector2D>
#include <QByteArray>
#include <vector>

#include "PunctualLights.h"
#include "GltfCameraData.h"
#include "GltfVariantData.h"
#include "GltfAnimationData.h"

#include "Material.h"

class RenderableMesh;
/**
 * Post-process exported glTF JSON to add missing optional properties
 *
 * This ensures exported files are complete and robust, avoiding undefined
 * behavior in various glTF importers (especially Assimp in release builds).
 *
 * The processor adds:
 * - texCoord: 0 (default) to all texture references
 * - scale: 1.0 to normalTexture
 * - strength: 1.0 to occlusionTexture
 * - Explicit filter and wrap modes to all samplers
 * - Missing properties in KHR_texture_transform extensions (offset, rotation, scale)
 * - KHR_texture_transform to extensionsUsed if transforms are present
 */
class GltfPostProcessor
{
public:
    /**
     * Post-process a glTF JSON object to add missing optional properties
     *
     * @param gltfJson The glTF JSON object to process (modified in place)
     * @param logCallback Optional callback for logging (receives QString messages)
     * @return true if any modifications were made
     */
    static bool postProcessGltfJson(QJsonObject& gltfJson,
        std::function<void(const QString&)> logCallback = nullptr);

    /**
     * Post-process a glTF JSON object with actual texture transforms from source materials
     *
     * This version writes the actual transform values from the source materials,
     * not just identity defaults. This is necessary because Assimp's glTF exporter
     * doesn't convert AI_MATKEY_UVTRANSFORM to KHR_texture_transform.
     *
     * @param gltfJson The glTF JSON object to process (modified in place)
     * @param meshes Source mesh objects with material data
     * @param logCallback Optional callback for logging
     * @return true if any modifications were made
     */
    static bool postProcessGltfJsonWithMaterials(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        const std::vector<GPULight>& lights,
        std::function<void(const QString&)> logCallback = nullptr,
        const QString& textureSubfolder = "textures",
        const QMap<QString, QString>& pathMapping = {},
        const QMap<QString, int>& embeddedIndexMapping = {});

    /**
     * Post-process a glTF file on disk
     *
     * @param filePath Path to the .gltf file (not .glb)
     * @param logCallback Optional callback for logging
     * @return true if successful
     */
    static bool postProcessGltfFile(const QString& filePath,
        std::function<void(const QString&)> logCallback = nullptr);

    /**
     * Post-process a glTF file with actual texture transform data from source materials
     *
     * @param filePath Path to the .gltf file
     * @param meshes Source mesh objects with material data
     * @param logCallback Optional callback for logging
     * @return true if successful
     */
    static bool postProcessGltfFileWithMaterials(
        const QString& filePath,
        const std::vector<SceneMesh*>& meshes,
        const std::vector<GPULight>& lights,
        std::function<void(const QString&)> logCallback = nullptr,
        const QString& textureSubfolder = "textures",
        const QMap<QString, QString>& pathMapping = {},
        const QMap<QString, int>& embeddedIndexMapping = {});

    /**
     * Post-process a GLB file on disk
     *
     * Reads the GLB, extracts JSON, post-processes it, and rewrites the GLB
     *
     * @param filePath Path to the .glb file
     * @param logCallback Optional callback for logging
     * @return true if successful
     */
    static bool postProcessGlbFile(const QString& filePath,
        std::function<void(const QString&)> logCallback = nullptr);

    /**
     * Post-process a GLB file with actual texture transform data from source materials
     *
     * @param filePath Path to the .glb file
     * @param meshes Source mesh objects with material data
     * @param logCallback Optional callback for logging
     * @return true if successful
     */
    static bool postProcessGlbFileWithMaterials(
        const QString& filePath,
        const std::vector<SceneMesh*>& meshes,
        const std::vector<GPULight>& lights,
        std::function<void(const QString&)> logCallback = nullptr,
        const QString& textureSubfolder = "textures",
        const QMap<QString, QString>& pathMapping = {},
        const QMap<QString, int>& embeddedIndexMapping = {});

	/**
	 * Write punctual light definitions from the source scene into the glTF JSON
	 *
	 * This adds a KHR_lights_punctual extension with light definitions based on the provided GPULight data.
	 */
    static bool writeGltfCameras(
        QJsonObject& gltfJson,
        const QVector<GltfCameraEntry>& cameras,
        std::function<void(const QString&)> logCallback);

    static bool writePunctualLights(
        QJsonObject& gltfJson,
        const std::vector<GPULight>& lights,
        std::function<void(const QString&)> logCallback = nullptr);

    static bool removeTangentAttributes(
        QJsonObject& gltfJson,
        std::function<void(const QString&)> logCallback = nullptr);

    /**
    * Return a normalized path for the GLB file, ensuring consistent formatting across platforms
     * This is important for the patchGlbImageNames function to correctly locate the GLB file
     * and update its embedded texture names.
     *
     * @param path The original file path to the GLB file
     * @return Normalized file path suitable for GLB patching
    */
    static QString normalisedGlbPath(const QString& path);

    /**
     * Register variant export data so the next postProcess*FileWithMaterials call
     * will write the KHR_materials_variants extension into the output glTF/GLB.
     *
     * Call this BEFORE postProcessGltf*FileWithMaterials when exporting a scene
     * that carries KHR_materials_variants data.  Pass empty arrays to disable
     * variant export (or call clearVariantExportData()).
     *
     * @param variantNames  Ordered list of variant names (root-level glTF array).
     * @param entries       Per-mesh export entries built by AssImpMeshExporter
     *                      (one entry per exported mesh, in output order).
     */
    /**
     * Register glTF camera entries to be injected into the next export.
     * Call before postProcessGlb/GltfFileWithMaterials and clear afterwards
     * (clearVariantExportData also clears camera data).
     */
    static void setGltfCameraData(const QVector<GltfCameraEntry>& cameras);
    static void clearGltfCameraData();

    static void setVariantExportData(
        const QStringList& variantNames,
        const QVector<MeshVariantExportEntry>& entries);

    /**
     * Register the Material for each non-default variant material so the
     * post-processor can patch texture indices, transforms, and samplers for
     * materials that are not matched to any source SceneMesh.
     *
     * @param variantMats  Map from JSON material array index to the Material.
     *                     Only non-default (extra) variant materials need to be
     *                     registered here; the default material is already handled
     *                     by the main material-patching pass.
     */
    static void setVariantMaterialData(const QMap<int, Material>& variantMats);

    static void clearVariantExportData();

    /**
     * Register glTF animation data for Pointer-channel injection.
     * The next postProcess*FileWithMaterials call will inject KHR_animation_pointer
     * channels (material texture-transform and node-visibility) that Assimp cannot
     * represent in its aiAnimation structure.
     *
     * @param animDataList  Animation data objects (one per source file) that may
     *                      contain Pointer-path channels to be re-injected.
     * @param nodeIndexToName  Map from glTF node index → exported node name, used to
     *                         resolve targetNodeIndex → JSON node array index.
     */
    static void setPointerAnimationData(
        const QVector<GltfAnimationData>& animDataList,
        const QMap<int, QString>& nodeIndexToName);
    static void clearPointerAnimationData();

private:
    // Material signature for robust matching during export
    // Includes texture paths AND texture transforms to distinguish materials
    // that share identical textures but have different transform properties
    struct MaterialSignature
    {
        QString name;                           // Material name from Material
        QSet<QString> textureFilePaths;         // Actual texture file paths

        // Texture binding: texture type + path + coordinate + transforms
        struct TextureBinding {
            QString textureType;                // "Albedo", "Normal", "Roughness", etc.
            QString path;                       // Texture file path
            int texCoordIndex;                  // Which texture coordinate channel
            float rotationRad;                  // Transform: rotation
            QVector2D scale;                    // Transform: scale
            QVector2D offset;                   // Transform: offset
            // Sampler identity (critical for distinguishing materials that
            // share the same image/UV transform but use different samplers)
            int wrapS = 0;
            int wrapT = 0;
            int magFilter = 0;
            int minFilter = 0;
        };
        std::vector<TextureBinding> textureBindings;  // Complete texture binding info

        int originalMaterialIndex;              // From assimp import
        int meshIndex;                          // Which mesh this signature is from
        // NOTE: Do NOT store material pointer - it may become invalid
        // Access material via meshes[meshIndex]->getMaterial() instead

        // Compute a unique hash based on texture paths AND transforms
        // This distinguishes:
        // - Materials with same textures but different transforms (Chair case)
        // - Materials with same textures but different scale/rotation (STEP case)
        QString computeHash() const;
    };

    static MaterialSignature buildSignatureForMesh(
        int meshIdx,
        const SceneMesh* mesh,
        std::function<void(const QString&)> logCallback);

    // Find material signature index for a JSON material. Returns -1 if not found.
    static int findMaterialBySignature(
        const QString& jsonMatName,
        const QJsonObject& jsonMat,
        const std::vector<MaterialSignature>& signatures,
        int matIdx,
        std::function<void(const QString&)> logCallback);

    static int findMaterialByNameWithDedup(
        const QString& jsonMatName,
        const std::vector<MaterialSignature>& signatures,
        int matIdx,
        std::function<void(const QString&)> logCallback);

    static int findMaterialByIndexFallback(
        int matIdx,
        const std::vector<MaterialSignature>& signatures,
        std::function<void(const QString&)> logCallback);

    static int computeTextureMatchScore(
        const MaterialSignature& sig,
        const QJsonObject& jsonMat);

    static bool fixTextureInfoWithTransforms(QJsonObject& parent, const QString& key);
    static bool fixNormalTextureInfo(QJsonObject& parent, const QString& key);
    static bool fixOcclusionTextureInfo(QJsonObject& parent, const QString& key, float occlusionStrength = 1.0f);
    static bool fixNormalTextureScale(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        std::function<void(const QString&)> logCallback);
    static bool fixClearcoatNormalTextureScale(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        std::function<void(const QString&)> logCallback);
    static bool fixSpecularExtension(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        std::function<void(const QString&)> logCallback = nullptr);
    static bool fixMetallicFactor(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        std::function<void(const QString&)> logCallback = nullptr);
    static bool fixSpecularTextures(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        std::function<void(const QString&)> logCallback = nullptr);
    static int findOrCreateTexture(
        QJsonObject& gltfJson,
        const QString& imagePath,
        int desiredSamplerIdx,
        std::function<void(const QString&)> logCallback = nullptr);

    static void log(const QString& message, std::function<void(const QString&)> callback);

private:
    static const SceneMesh* sourceMeshForMaterial(
        int materialIndex,
        const std::vector<SceneMesh*>& meshes);
    static Material defaultMaterialForMesh(const SceneMesh* mesh);
    static Material sourceMaterialForJsonMaterial(
        int materialIndex,
        const std::vector<SceneMesh*>& meshes);
    static bool mergeOpacityIntoBaseColorTextures(
        QJsonObject& gltfJson,
        const std::vector<SceneMesh*>& meshes,
        const QString& exportFilePath,
        QByteArray* glbBinaryChunk,
        std::function<void(const QString&)> logCallback);

    static QString _textureSubfolder;
    static bool _isGlbExport;
    static QMap<QString, QString> _pathMapping;
    static QMap<QString, int> _embeddedIndexMapping;
    static QMap<int, int> _materialToSourceMeshIndex;

    // KHR_materials_variants export state (set by setVariantExportData before post-processing)
    static QVector<GltfCameraEntry> _gltfCameras;

    static QStringList _variantNames;
    static QVector<MeshVariantExportEntry> _variantEntries;
    // Materials for non-default variant materials keyed by JSON material index.
    // Set by setVariantMaterialData; allows the secondary post-processing pass to
    // patch texture indices / transforms / samplers for materials that have no
    // corresponding source SceneMesh in the main patching loop.
    static QMap<int, Material> _variantMatByJsonIdx;

    // Pointer-animation injection state (set by setPointerAnimationData).
    static QVector<GltfAnimationData> _pointerAnimData;
    static QMap<int, QString>         _pointerAnimNodeNames; // glTF nodeIndex → name

    // Write KHR_materials_variants extension to the JSON using the stored variant data.
    static bool writeKhrMaterialsVariantsExtension(
        QJsonObject& gltfJson,
        std::function<void(const QString&)> logCallback);

    // Inject KHR_animation_pointer channels (Visibility + MaterialTextureTransform)
    // into the existing animation clips that were written by Assimp.
    static bool injectPointerAnimationChannels(
        QJsonObject& gltfJson,
        QByteArray*  glbBinaryChunk,  // nullptr for .gltf, non-null for .glb
        std::function<void(const QString&)> logCallback);

    // Inject standard glTF2 "weights" animation channels for morph-target animations.
    // Called after Assimp export; supplements or replaces whatever Assimp may have
    // written, ensuring the round-trip works regardless of Assimp version.
    static bool injectMorphWeightAnimations(
        QJsonObject& gltfJson,
        QByteArray*  glbBinaryChunk,  // nullptr for .gltf, non-null for .glb
        std::function<void(const QString&)> logCallback);
};
