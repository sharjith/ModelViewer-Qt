#pragma once
class SceneMesh;


#include <QObject>
#include <QString>
#include <vector>
#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include "TextureLocationManager.h"
#include "PunctualLights.h"
#include "GltfCameraData.h"
#include "GltfVariantData.h"
#include "GltfAnimationData.h"
#include "TexturePackingUtils.h"

class Material;
class RenderableMesh;
struct Vertex;

/**
 * @class AssImpMeshExporter
 * @brief Enhanced glTF/mesh exporter with full PBR and texture support
 *
 * This class exports ModelViewer scenes to standard 3D formats (glTF, OBJ, FBX, etc.)
 * with complete preservation of:
 * - PBR material properties (metallic, roughness, transmission, etc.)
 * - Texture references with automatic copying and deduplication
 * - Per-mesh transformations (position, rotation, scale)
 * - Advanced material extensions (clearcoat, sheen, iridescence, etc.)
 *
 * Features:
 * - Automatic texture path resolution from multiple sources
 * - Content-based texture deduplication (SHA256)
 * - Portable output with relative texture paths
 * - Proper Assimp scene hierarchy
 * - Comprehensive logging for debugging
 *
 * Usage:
 *   AssImpMeshExporter exporter;
 *   AssImpMeshExporter::ExportSettings settings;
 *   settings.outputDirectory = "/output/path";
 *   settings.copyTextures = true;
 *   settings.useRelativePaths = true;
 *
 *   aiReturn result = exporter.exportMeshes(meshStore, "scene.glb", settings);
 *   if (result == aiReturn_SUCCESS) {
 *       const auto& texPackage = exporter.getLastTexturePackage();
 *       qDebug() << "Exported" << texPackage.textures.size() << "textures";
 *   }
 */
class AssImpMeshExporter : public QObject
{
    Q_OBJECT

public:
    /**
     * @struct ExportSettings
     * @brief Configuration options for export process
     */
    struct ExportSettings
    {
        QString outputDirectory;              ///< Base directory for output
        bool copyTextures = true;             ///< Copy texture files to output
        bool useRelativePaths = true;         ///< Use relative paths in glTF
        bool deduplicateTextures = true;      ///< Remove duplicate textures
        bool verbose = true;                  ///< Enable debug logging
        std::vector<GPULight> lights;
        QVector<GltfCameraEntry> cameras;  ///< glTF cameras to inject via post-processor
        // Ordered variant names for KHR_materials_variants export.
        // Leave empty to omit the extension from the output.
        QStringList variantNames;
        // Pointer-path animation data for KHR_animation_pointer injection.
        // Only relevant for glTF/GLB export; ignored for other formats.
        QVector<GltfAnimationData> animationDataList;
        // Map from glTF node index (source file) → exported node name.
        // Required for resolving targetNodeIndex in Pointer channels.
        QMap<int, QString> nodeIndexToExportedName;
    };

    explicit AssImpMeshExporter(QObject* parent = nullptr);
    ~AssImpMeshExporter() = default;

    /**
     * @brief Export a vector of SceneMesh objects to a 3D file format
     *
     * This is the main export function. It:
     * 1. Packages and copies all textures from materials
     * 2. Creates Assimp mesh structures with vertex data
     * 3. Creates materials with full PBR properties
     * 4. Sets up proper scene hierarchy with transforms
     * 5. Exports via Assimp to the target format (determined by extension)
     *
     * @param meshes Vector of SceneMesh pointers to export
     * @param exportPath Output file path (e.g., "scene.glb", "model.obj")
     * @param settings Export configuration options
     * @return aiReturn_SUCCESS on success, aiReturn_FAILURE on error
     */
    aiReturn exportMeshes(
        const aiScene* scene,
        const std::vector<SceneMesh*>& meshes,
        const QString& exportPath,
        const ExportSettings& settings);

    /**
     * @brief Export an existing Assimp scene
     *
     * Use this for exporting scenes that are already in Assimp format
     * (e.g., from AssimpModelLoader)
     *
     * @param scene Assimp scene to export
     * @param exportPath Output file path
     * @return aiReturn_SUCCESS on success, aiReturn_FAILURE on error
     */
    aiReturn exportScene(const aiScene* scene, const std::string& exportPath);

    /**
     * Enhanced exportScene - Apply materials before export
     *
     * Signature 1: Basic material application only
     * Takes the original mesh objects and applies their materials to the
     * Assimp scene before exporting. Useful when you already have a scene
     * and want to enrich it with material data.
     */
    aiReturn exportScene(
        aiScene* scene,
        const std::vector<SceneMesh*>& meshes,
        const std::string& exportPath);

    /**
     * Enhanced exportScene - Apply materials with texture packaging
     *
     * Signature 2: Material application + texture packaging
     * Combines material application with automatic texture packaging,
     * useful for creating self-contained, portable export bundles.
     * All textures are copied to the output directory with proper deduplication.
     */
    aiReturn exportScene(
        aiScene* scene,
        const std::vector<SceneMesh*>& meshes,
        const std::string& exportPath,
        const ExportSettings& settings);

    /**
     * @brief Get metadata about the last export's texture packaging
     *
     * Useful for debugging and understanding what textures were exported
     * and how they were organized.
     *
     * @return Reference to TexturePackage from last export
     */
    const TexturePackage& getLastTexturePackage() const { return _lastTexturePackage; }

    /**
     * @brief Pack separate Metallic and Roughness textures into one glTF-compliant texture
     *
     * Combines separate M and R textures into a single RGBA image for glTF export:
     * - Red channel = 0 (unused)
     * - Green channel = Roughness
     * - Blue channel = Metallic
     * - Alpha channel = 255 (opaque)
     *
     * Uses caching to avoid redundant packing of the same M/R pairs.
     * Falls back gracefully if packing fails.
     *
     * @param material Source Material with potential separate M/R textures
     * @param texturePackage Resolved texture paths for the material
     * @param outputDirectory Directory to save packed texture
     * @return Path to packed texture file, or empty string on failure
     */
    QString packMetallicRoughnessIfSeparate(
        const Material& material,
        const TexturePackage& texturePackage,
        const QString& outputDirectory);

    /**
     * @brief Pack ORM (Occlusion, Roughness, Metallic) textures into single texture
     *
     * When separate ORM textures are present, combines them into a single glTF-compliant
     * packed texture using efficient scanLine() pixel access. Handles optional occlusion
     * texture (white default if missing).
     *
     * @param material Source Material with separate ORM textures
     * @param texturePackage Resolved texture paths for the material
     * @param outputDirectory Directory to save packed texture
     * @return Path to packed texture file, or empty string on failure
     */
    QString packORMIfSeparate(
        const Material& material,
        const TexturePackage& texturePackage,
        const QString& outputDirectory);

private:
    /**
     * @brief Create an Assimp mesh from vertex/index data
     *
     * Populates positions, normals, UVs (first set), and vertex colors
     *
     * @param vertices Vertex data (includes position, normal, color, UVs)
     * @param indices Triangle indices
     * @param name Mesh name for identification
     * @return Allocated aiMesh*, or nullptr on failure
     */
    aiMesh* createMesh(const std::vector<Vertex>& vertices,
        const std::vector<unsigned int>& indices,
        const std::string& name,
        GLenum primitiveMode);

    /**
     * @brief Create an Assimp material from Material with full PBR
     *
     * Exports all properties: base color, metallic, roughness, transmission,
     * IOR, clearcoat, sheen, emissive, normal scale, etc.
     * Assigns texture paths from the texture package.
     *
     * When material name is empty, uses meshName as fallback to prevent
     * Assimp from deduplicating materials with identical names.
     *
     * @param material Material to convert
     * @param texturePackage Resolved texture paths and mappings
     * @param exportFileLocation Export file path for format detection
     * @param meshName Mesh name (used as fallback if material name is empty)
     * @return Allocated aiMaterial*, or nullptr on failure
     */
    aiMaterial* createMaterial(
        const Material& material,
        const TexturePackage& texturePackage,
        const QString& exportFileLocation,
        const QString& meshName = "");

    /**
     * @brief Assign texture references to an Assimp material
     *
     * Maps Material's 25 texture types to Assimp semantic types and
     * uses resolved relative paths from the texture package.
     * Called by createMaterial().
     *
     * @param aiMat Target Assimp material to populate
     * @param material Source Material
     * @param texturePackage Resolved texture paths and mappings
     */
    void assignTexturesToMaterial(
        aiMaterial* aiMat,
        const Material& material,
        const TexturePackage& texturePackage,
        bool useEmbeddedTextures,
        const QString& exportFileLocation,
        bool isGLTF);

    /**
     * Helper: Apply materials from ModelViewer meshes to Assimp scene
     *
     * Internal method that:
     * - Iterates through scene meshes paired with source mesh objects
     * - Creates Assimp materials from Material data
     * - Assigns textures using texture package mappings
     * - Updates mesh material indices
     *
     * This centralizes material assignment logic for both export signatures.
     */
    void applyMaterialsToScene(
        aiScene* scene,
        const std::vector<SceneMesh*>& meshes,
        const QString& exportFileLocation);

    /**
     * @brief Remove stale mesh entries from a copied aiScene so it matches the
     *        surviving SceneMesh vector in _meshStore.
     *
     * When meshes are deleted by the user, _meshStore shrinks but _globalScene
     * (and any deep copy of it) still contains the original mesh and node
     * entries. Passing the mismatched arrays to applyMaterialsToScene() causes
     * the last M-N scene meshes to keep their old (now out-of-bounds) material
     * indices, which makes the Assimp exporter dereference freed memory.
     *
     * This method walks scene->mMeshes[], identifies entries whose names do not
     * appear in the surviving SceneMesh vector, removes them and frees their
     * memory, and remaps all aiNode mesh index references accordingly.  After
     * this call scene->mNumMeshes == meshes.size() and the two arrays are in
     * 1-to-1 correspondence by position.
     *
     * Must be called BEFORE applyMaterialsToScene().
     *
     * @param scene   Deep-copied aiScene (modified in-place)
     * @param meshes  Surviving SceneMesh* from _meshStore
     */
    void syncSceneToMeshStore(
        aiScene* scene,
        const std::vector<SceneMesh*>& meshes);

    /**
     * @brief Translate absolute texture paths in every aiMaterial to relative
     *        output paths using the texture package path mapping.
     *
     * SceneGraphExporter writes the original (potentially absolute) texture
     * paths into the aiMaterial slots.  For non-GLB formats Assimp writes
     * those paths verbatim into the MTL / FBX / DAE file, producing absolute
     * paths that break portability.  This pass replaces each path with the
     * relative output path recorded by TextureLocationManager::packageTextures().
     *
     * Must be called after packageTextures() and before Assimp::Export().
     * GLB skips this (embedded textures use "*N" references written by
     * embedTexturesInScene()).
     *
     * @param scene  Assimp scene whose material texture paths are updated in-place
     * @param pkg    Texture package produced by packageTextures()
     */
    void updateSceneMaterialPaths(aiScene* scene, const TexturePackage& pkg);

    /**
     * @brief Append PBR-extension lines to an Assimp-generated MTL file.
     *
     * Assimp's OBJ exporter writes only classic Phong MTL keywords.  This
     * method re-opens the written .mtl, locates each material block by name,
     * matches it to the source Material, and appends the de-facto PBR MTL
     * extension lines that Blender, Substance Painter, Maya, and other
     * PBR-aware OBJ importers recognise:
     *   Pm  <metallicFactor>       map_Pm  <metallicTexture>
     *   Pr  <roughnessFactor>      map_Pr  <roughnessTexture>
     *                              map_Ke  <emissiveTexture>
     *                              norm    <normalTexture>   (tangent-space)
     *
     * Paths are expressed relative to the MTL file location using the mapping
     * built by packageTextures().
     *
     * @param mtlPath  Absolute path of the .mtl file to patch
     * @param meshes   Source meshes (provides material names and Material data)
     * @param pkg      Texture package (provides original → relative path mapping)
     */
    void patchMtlWithPbrExtensions(
        const QString& mtlPath,
        const std::vector<SceneMesh*>& meshes,
        const TexturePackage& pkg);

    /**
     * Load image file and create embedded aiTexture
     *
     * Reads image from disk, converts to RGBA format,
     * and creates an aiTexture structure with the pixel data embedded.
     * This texture can then be attached to aiScene->mTextures for embedding
     * in the exported file.
     *
     * @param imagePath Full path to the image file
     * @return aiTexture with embedded data, or nullptr if load fails
     */
    aiTexture* createEmbeddedTexture(const QString& imagePath);

    /**
     * Embed all textures from scene materials into aiScene->mTextures
     *
     * This method:
     * 1. Iterates through all materials in the scene
     * 2. Collects unique texture file paths
     * 3. Loads each texture file from disk
     * 4. Creates aiTexture objects with embedded pixel data
     * 5. Attaches to aiScene->mTextures array
     *
     * After calling this, the scene is ready for GLB export with
     * embedded textures. Assimp will automatically include these
     * textures in the binary GLB output.
     *
     * @param scene The Assimp scene to enrich
     * @param texturePackage Texture file paths and locations
     */
    QStringList embedTexturesInScene(
        aiScene* scene,
        const TexturePackage& texturePackage);

    /**
    * Extract embedded textures from an Assimp scene and save to disk
    * This helps in exporting the textures embedded in a GLB file to separate image files on disk,
    * which can be useful for formats that don't support embedding.
    */
    QMap<QString, QString> extractEmbeddedTextures(
        const aiScene* scene,
        const QString& outputDirectory,
        const QString& textureSubfolder);

    /**
     * @brief Create an Assimp scene with proper hierarchy
     *
     * Sets up parent-child relationships and applies per-mesh transformations
     *
     * @param meshes Assimp meshes (ownership transferred to scene)
     * @param materials Assimp materials (ownership transferred to scene)
     * @param transforms Per-mesh transformation matrices
     * @return Allocated aiScene*, caller must delete
     */
    aiScene* createScene(
        const std::vector<aiMesh*>& meshes,
        const std::vector<aiMaterial*>& materials,
        const std::vector<class QMatrix4x4>& transforms);

    /**
     * @brief Find the appropriate Assimp exporter for a file extension
     *
     * @param filePath Output file path to determine format
     * @param exporter Assimp exporter instance to query
     * @return Pointer to aiExportFormatDesc, or nullptr if unsupported
     */
    const aiExportFormatDesc* findExportFormat(
        const std::string& filePath,
        Assimp::Exporter& exporter);

    /*
    *
     * @brief Patch GLB file to update embedded texture names
     *
     * After export, walks the Assimp-written GLB JSON to derive the correct name for each
     * binary image slot by reverse-mapping material texture slots -> aiScene source paths.
     * This is necessary because Assimp's GLB2 writer serialises textures in its own internal
     * field order, which does not match the iteration order used by embedTexturesInScene().
     * Using orderedNames[] sequentially would put the wrong name on the wrong binary slot.
     *
     * @param glbPath     Path to the exported GLB file
     * @param orderedNames Fallback list of filenames (used for any slot not resolvable via JSON)
     * @param scene       The aiScene after export, used to look up source paths per slot
     */
    void patchGlbImageNames(const QString& glbPath, const QStringList& orderedNames,
        const aiScene* scene, const QString& textureDirectory,
        QMap<QString, int>* embeddedIndexMapping = nullptr);


    /**
     * @brief Check if any of the meshes have GLB virtual paths
     *
     * @param meshes List of SceneMesh pointers
     * @return true if any mesh has a GLB virtual path, false otherwise
     */
    bool hasGlbVirtualPaths(const std::vector<SceneMesh*>& meshes);


    // === Logging utilities ===

    /**
     * @brief Log debug message (respects verbose setting)
     */
    void logMessage(const QString& msg);

    /**
     * @brief Log warning message
     */
    void logWarning(const QString& msg);

    /**
     * @brief Log error message (always logged)
     */
    void logError(const QString& msg);

private:
    TextureLocationManager _textureManager;
    TexturePackage _lastTexturePackage;
    ExportSettings _currentSettings;
    QMap<QString, int> _lastEmbeddedIndexMapping;
    QMap<QString, QString> _packedTextureCache;  // key: "metallic_path|roughness_path" -> packed_file_path

    // Built during exportMeshes when variantNames is non-empty; passed to GltfPostProcessor.
    QVector<MeshVariantExportEntry> _lastVariantEntries;
};
