#include "AssImpMeshExporter.h"
#include "RenderableMesh.h"
#include "SceneMesh.h"
#include "Material.h"
#include "GltfPostProcessor.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <set>

namespace
{
void setFaceIndices(aiFace& face, std::initializer_list<unsigned int> values)
{
    face.mNumIndices = static_cast<unsigned int>(values.size());
    face.mIndices = new unsigned int[face.mNumIndices];

    unsigned int dst = 0;
    for (unsigned int value : values)
    {
        face.mIndices[dst++] = value;
    }
}

unsigned int primitiveModeToAiPrimitiveType(GLenum primitiveMode)
{
    switch (primitiveMode)
    {
    case GL_POINTS:
        return aiPrimitiveType_POINT;
    case GL_LINES:
    case GL_LINE_LOOP:
    case GL_LINE_STRIP:
        return aiPrimitiveType_LINE;
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    default:
        return aiPrimitiveType_TRIANGLE;
    }
}

Material exportedDefaultMaterial(const SceneMesh* mesh)
{
    if (!mesh)
        return Material();

    if (mesh->hasVariants())
    {
        if (const Material* originalMaterial = mesh->materialForVariant(-1))
            return *originalMaterial;
    }

    return mesh->getMaterial();
}

int exportedBaseMaterialKey(const SceneMesh* mesh)
{
    return mesh ? mesh->getOriginalMaterialIndex() : -1;
}

bool populateFacesForPrimitive(aiMesh* mesh,
                               const std::vector<unsigned int>& indices,
                               GLenum primitiveMode)
{
    if (!mesh)
        return false;

    switch (primitiveMode)
    {
    case GL_POINTS:
        if (indices.empty())
            return false;

        mesh->mNumFaces = static_cast<unsigned int>(indices.size());
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            setFaceIndices(mesh->mFaces[i], { indices[i] });
        }
        return true;

    case GL_LINES:
    {
        const unsigned int faceCount = static_cast<unsigned int>(indices.size() / 2);
        if (faceCount == 0)
            return false;

        mesh->mNumFaces = faceCount;
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned int i = 0; i < faceCount; ++i)
        {
            setFaceIndices(mesh->mFaces[i], {
                indices[i * 2],
                indices[i * 2 + 1]
            });
        }
        return true;
    }

    case GL_LINE_STRIP:
        if (indices.size() < 2)
            return false;

        mesh->mNumFaces = static_cast<unsigned int>(indices.size() - 1);
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            setFaceIndices(mesh->mFaces[i], { indices[i], indices[i + 1] });
        }
        return true;

    case GL_LINE_LOOP:
        if (indices.size() < 2)
            return false;

        mesh->mNumFaces = static_cast<unsigned int>(indices.size());
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned int i = 0; i + 1 < static_cast<unsigned int>(indices.size()); ++i)
        {
            setFaceIndices(mesh->mFaces[i], { indices[i], indices[i + 1] });
        }
        setFaceIndices(mesh->mFaces[mesh->mNumFaces - 1], { indices.back(), indices.front() });
        return true;

    case GL_TRIANGLES:
    {
        const unsigned int faceCount = static_cast<unsigned int>(indices.size() / 3);
        if (faceCount == 0)
            return false;

        mesh->mNumFaces = faceCount;
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned int i = 0; i < faceCount; ++i)
        {
            setFaceIndices(mesh->mFaces[i], {
                indices[i * 3],
                indices[i * 3 + 1],
                indices[i * 3 + 2]
            });
        }
        return true;
    }

    case GL_TRIANGLE_STRIP:
        if (indices.size() < 3)
            return false;

        mesh->mNumFaces = static_cast<unsigned int>(indices.size() - 2);
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            if (((i + 1) % 2) == 0)
            {
                setFaceIndices(mesh->mFaces[i], { indices[i + 1], indices[i], indices[i + 2] });
            }
            else
            {
                setFaceIndices(mesh->mFaces[i], { indices[i], indices[i + 1], indices[i + 2] });
            }
        }
        return true;

    case GL_TRIANGLE_FAN:
        if (indices.size() < 3)
            return false;

        mesh->mNumFaces = static_cast<unsigned int>(indices.size() - 2);
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        setFaceIndices(mesh->mFaces[0], { indices[0], indices[1], indices[2] });
        for (unsigned int i = 1; i < mesh->mNumFaces; ++i)
        {
            setFaceIndices(mesh->mFaces[i], {
                indices[0],
                mesh->mFaces[i - 1].mIndices[2],
                indices[i + 2]
            });
        }
        return true;

    default:
        return populateFacesForPrimitive(mesh, indices, GL_TRIANGLES);
    }
}

bool textureBindingCompatibleForSharedExport(
    const Material::Texture& a,
    const Material::Texture& b)
{
    auto nearlyEqual = [](float lhs, float rhs) {
        return std::abs(lhs - rhs) < 1e-5f;
    };

    return a.texCoordIndex == b.texCoordIndex &&
           nearlyEqual(a.scale.x, b.scale.x) &&
           nearlyEqual(a.scale.y, b.scale.y) &&
           nearlyEqual(a.offset.x, b.offset.x) &&
           nearlyEqual(a.offset.y, b.offset.y) &&
           nearlyEqual(a.rotation, b.rotation);
}

bool hasDistinctSplitMetallicRoughnessTextures(const aiMaterial* material)
{
    if (!material)
        return false;

    if (material->GetTextureCount(aiTextureType_METALNESS) == 0 ||
        material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) == 0)
    {
        return false;
    }

    aiString metallicPath;
    aiString roughnessPath;
    if (material->GetTexture(aiTextureType_METALNESS, 0, &metallicPath) != AI_SUCCESS ||
        material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &roughnessPath) != AI_SUCCESS)
    {
        return false;
    }

    return QString::fromUtf8(metallicPath.C_Str()) !=
           QString::fromUtf8(roughnessPath.C_Str());
}

bool shouldNormalizePreservedGltfMaterial(const aiMaterial* preservedMaterial,
                                          const Material& sourceMaterial)
{
    // Force normalization when the source Material contains GLB virtual paths
    // ("glb://...") or Assimp embedded-texture references ("*N").
    // buildMaterialFromSceneMesh copies these raw paths into the preserved
    // aiMaterial.  Assimp's GLB exporter cannot resolve them as file-system paths,
    // so the textures would be silently missing in the output.
    // createMaterial() + _lastTexturePackage remaps them to packaged relative paths
    // that Assimp CAN read and embed correctly.
    for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
    {
        const auto& tex = sourceMaterial.texture(static_cast<Material::TextureType>(i));
        if (tex.path.empty())
            continue;
        // glb:// virtual path or *N embedded-texture index
        if (tex.path.rfind("glb://", 0) == 0 || tex.path[0] == '*')
            return true;
    }

    const QString metallicPath = sourceMaterial.metallicMapPath();
    const QString roughnessPath = sourceMaterial.roughnessMapPath();

    if (roughnessPath.isEmpty())
        return false;

    if (metallicPath.isEmpty())
        return true;

    if (metallicPath != roughnessPath)
        return true;

    return hasDistinctSplitMetallicRoughnessTextures(preservedMaterial);
}
}

AssImpMeshExporter::AssImpMeshExporter(QObject* parent)
    : QObject(parent)
{
}

aiReturn AssImpMeshExporter::exportMeshes(
    const aiScene* scene,
    const std::vector<SceneMesh*>& meshes,
    const QString& exportPath,
    const ExportSettings& settings)
{
    _currentSettings = settings;
    _lastEmbeddedIndexMapping.clear();
    _packedTextureCache.clear();
    _lastVariantEntries.clear();

    logMessage(QString("=== AssImpMeshExporter::exportMeshes ==="));
    logMessage(QString("Target: %1").arg(exportPath));
    logMessage(QString("Output directory: %1").arg(settings.outputDirectory));

    if (meshes.empty())
    {
        logError("No meshes to export");
        return aiReturn_FAILURE;
    }

    // ===== STEP 1: Package textures =====
    logMessage("Step 1: Packaging textures...");

    // Check if this is a GLB export (for cleanup later)
    QFileInfo exportFileInfo(exportPath);
    QString ext = exportFileInfo.suffix().toLower();
    bool isGLB = (ext == "glb" || ext == "gltf-binary");
    bool isGLTF = (ext == "gltf");
    QString textureSubfolder = exportFileInfo.baseName() + "_textures";
    QString textureBaseDir = isGLB ? QDir::tempPath() : settings.outputDirectory;

    if (settings.copyTextures)
    {
        // Extract embedded textures ONLY if legacy glb:// paths still exist
        bool needsLegacyExtraction = hasGlbVirtualPaths(meshes);

        if ((isGLB || isGLTF) && scene && scene->mNumTextures > 0 && needsLegacyExtraction)
        {
            logMessage(" Extracting embedded textures (legacy glb:// detected)...");
            QMap<QString, QString> embeddedMapping = extractEmbeddedTextures(scene, textureBaseDir, textureSubfolder);

            for (auto it = embeddedMapping.begin(); it != embeddedMapping.end(); ++it)
            {
                _lastTexturePackage.pathMapping[it.key()] = it.value();
            }

            logMessage(QString(" -> Injected %1 embedded texture mappings").arg(embeddedMapping.size()));
        }
        else
        {
            logMessage(" Skipping embedded extraction (using cached disk textures)");
        }


        logMessage("Step 1b: Packaging textures...");
        _lastTexturePackage = _textureManager.packageTextures(
            meshes,
            textureBaseDir,
            textureSubfolder);

        // Re-inject ONLY if legacy glb:// paths exist// Re-inject ONLY if legacy 0 && needsLegacyExtraction)
        {
            QMap<QString, QString> embeddedMapping = extractEmbeddedTextures(scene, textureBaseDir, textureSubfolder);

            for (auto it = embeddedMapping.begin(); it != embeddedMapping.end(); ++it)
            {
                _lastTexturePackage.pathMapping[it.key()] = it.value();
            }
        }
        
        logMessage(QString("  -> Total texture mappings: %1").arg(_lastTexturePackage.pathMapping.size()));


        logMessage(QString("  -> Packaged %1 unique textures")
            .arg(_lastTexturePackage.textures.size()));

        if (_lastTexturePackage.totalSize > 0)
        {
            logMessage(QString("  -> Total texture size: %1 MB")
                .arg(_lastTexturePackage.totalSize / (1024.0 * 1024.0), 0, 'f', 2));
        }

        if (_lastTexturePackage.duplicatesRemoved > 0)
        {
            logMessage(QString("  -> Removed %1 duplicate textures")
                .arg(_lastTexturePackage.duplicatesRemoved));
        }

        if (isGLB)
        {
            logMessage("  -> Note: Textures will be embedded in GLB and folder will be cleaned up");
        }
    }
    else
    {
        logMessage("  -> Texture copying disabled");
    }

    // ===== STEP 2: Create Assimp structures =====
    logMessage("Step 2: Creating Assimp structures...");

    std::vector<aiMesh*> aiMeshes;
    std::vector<aiMaterial*> aiMaterials;
    std::vector<QMatrix4x4> transforms;
    std::vector<const SceneMesh*> validMeshes; // meshes that successfully made it into aiMeshes

    // Material deduplication map: material content hash -> material index
    QMap<QString, unsigned int> materialContentToIndex;
    std::vector<Material> uniqueMaterials;

    for (const auto* mesh : meshes)
    {
        if (!mesh) continue;

        // Skip invisible meshes
        /*if (!mesh->isVisible())
        {
            logMessage(QString("  -> Skipping invisible mesh: %1").arg(mesh->getName()));
            continue;
        }*/

        // Extract vertex and index data
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        // Try to cast to SceneMesh for direct access
        if (auto assimpMesh = dynamic_cast<const SceneMesh*>(mesh))
        {
            vertices = assimpMesh->vertices();
            indices = assimpMesh->indices();
        }
        else
        {
            logWarning(QString("Non-SceneMesh encountered: %1 - limited support")
                .arg(mesh->getName()));
            // Could implement fallback here if needed
            continue;
        }

        if (vertices.empty() || indices.empty())
        {
            logWarning(QString("  -> Skipping empty mesh: %1")
                .arg(mesh->getName()));
            continue;
        }

        // Create Assimp mesh
        aiMesh* aiMesh = createMesh(
            vertices,
            indices,
            mesh->getName().toStdString(),
            mesh->getPrimitiveMode());
        if (!aiMesh)
        {
            logError(QString("Failed to create Assimp mesh: %1").arg(mesh->getName()));
            continue;
        }

        // Export the primitive base material from the original/default glTF material,
        // not the currently active variant selection in the viewer.
        Material meshMaterial = exportedDefaultMaterial(mesh);

        // HYBRID: Override runtime Material scalars with the original aiScene material when
        // available via originalMaterialIndex. The scene is the authoritative source for scalar
        // diversity (color, metallic, roughness) for non-variant imports. For glTF variant
        // meshes, the runtime Material already reflects the currently selected variant, so
        // pulling scalars from the original aiScene material would corrupt the export.
        int origMatIdx = mesh->getOriginalMaterialIndex();
        if (!mesh->hasVariants() &&
            scene && origMatIdx >= 0 && origMatIdx < static_cast<int>(scene->mNumMaterials))
        {
            const aiMaterial* sceneMat = scene->mMaterials[origMatIdx];

            aiColor3D diffuse(0, 0, 0);
            if (sceneMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
                meshMaterial.setAlbedoColor(QVector3D(diffuse.r, diffuse.g, diffuse.b));
            else if (sceneMat->Get(AI_MATKEY_BASE_COLOR, diffuse) == AI_SUCCESS)
                meshMaterial.setAlbedoColor(QVector3D(diffuse.r, diffuse.g, diffuse.b));

            float metallic = -1.0f;
            if (sceneMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS && metallic >= 0.0f)
                meshMaterial.setMetalness(metallic);

            float roughness = -1.0f;
            if (sceneMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS && roughness >= 0.0f)
                meshMaterial.setRoughness(roughness);

            float opacity = -1.0f;
            if (sceneMat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity >= 0.0f)
                meshMaterial.setOpacity(opacity);

            aiString matName;
            if (sceneMat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS && strlen(matName.C_Str()) > 0
                && meshMaterial.name().isEmpty())
            {
                meshMaterial.setName(QString::fromUtf8(matName.C_Str()));
            }

            logMessage(QString("  -> Scalar override from scene mat[%1]: albedo=[%2,%3,%4] metal=%5 rough=%6")
                .arg(origMatIdx)
                .arg(diffuse.r, 0, 'f', 3).arg(diffuse.g, 0, 'f', 3).arg(diffuse.b, 0, 'f', 3)
                .arg(metallic, 0, 'f', 3).arg(roughness, 0, 'f', 3));
        }

        // Create material directly - no deduplication
        // Deduplication happens at import time via updateAiSceneWithGltfMaterials
        // Each mesh's Material is unique (transforms are baked in)
        aiMaterial* aiMat = createMaterial(meshMaterial, _lastTexturePackage, exportPath, mesh->getName());
        if (!aiMat)
        {
            logError(QString("Failed to create material for: %1").arg(mesh->getName()));
            delete aiMesh;
            continue;
        }

        unsigned int materialIndex = static_cast<unsigned int>(aiMaterials.size());
        aiMesh->mMaterialIndex = materialIndex;
        aiMaterials.push_back(aiMat);

        aiMeshes.push_back(aiMesh);
        validMeshes.push_back(mesh);
        transforms.push_back(mesh->getTransformation());

        logMessage(QString("  -> Mesh added: %1 (%2 vertices, %3 indices)")
            .arg(mesh->getName())
            .arg(vertices.size())
            .arg(indices.size()));
    }

    // ===== STEP 2b: Add variant materials (KHR_materials_variants export) =====
    QMap<int, Material> variantMatsByJsonIdx2b;  // non-default variant mat index -> Material
    if (!settings.variantNames.isEmpty())
    {
        logMessage("Step 2b: Adding variant materials for KHR_materials_variants export...");

        for (size_t vi = 0; vi < validMeshes.size(); ++vi)
        {
            const SceneMesh* mesh = validMeshes[vi];
            MeshVariantExportEntry entry;

            if (!mesh->hasVariants())
            {
                _lastVariantEntries.append(entry);  // empty entry preserves 1:1 indexing
                continue;
            }

            entry.variantMappings = mesh->variantMappings();

            int defaultKey      = exportedBaseMaterialKey(mesh);
            int defaultMatIdx   = static_cast<int>(aiMeshes[vi]->mMaterialIndex);
            entry.matKeyToJsonMatIdx[defaultKey] = defaultMatIdx;

            const QMap<int, Material>& varMats = mesh->allVariantMaterials();
            for (auto it = varMats.constBegin(); it != varMats.constEnd(); ++it)
            {
                int key = it.key();
                if (key == defaultKey) continue;  // default is already in aiMaterials

                aiMaterial* varAiMat = createMaterial(it.value(), _lastTexturePackage, exportPath, mesh->getName());
                if (!varAiMat)
                {
                    logWarning(QString("  -> Failed to create variant material (key %1) for mesh: %2")
                        .arg(key).arg(mesh->getName()));
                    continue;
                }

                int newMatIdx = static_cast<int>(aiMaterials.size());
                aiMaterials.push_back(varAiMat);
                entry.matKeyToJsonMatIdx[key] = newMatIdx;
                variantMatsByJsonIdx2b[newMatIdx] = it.value();

                logMessage(QString("  -> Variant material (key=%1 -> mat[%2]) added for mesh: %3")
                    .arg(key).arg(newMatIdx).arg(mesh->getName()));
            }

            _lastVariantEntries.append(entry);
        }

        logMessage(QString("  -> %1 variant entries built, %2 total materials")
            .arg(_lastVariantEntries.size()).arg(aiMaterials.size()));
    }

    if (aiMeshes.empty())
    {
        logError("No valid meshes created");
        return aiReturn_FAILURE;
    }

    logMessage(QString("  -> RESULT: %1 meshes consolidated to %2 unique materials")
        .arg(aiMeshes.size())
        .arg(aiMaterials.size()));

    // ===== STEP 3: Create scene hierarchy =====
    logMessage("Step 3: Creating scene hierarchy...");

    std::unique_ptr<aiScene> newScene(createScene(aiMeshes, aiMaterials, transforms));
    if (!newScene)
    {
        logError("Failed to create Assimp scene");
        return aiReturn_FAILURE;
    }

    logMessage("  -> Scene hierarchy created");

    // ===== STEP 4: Export via Assimp =====
    logMessage("Step 4: Exporting to file...");

    Assimp::Exporter exporter;
    const aiExportFormatDesc* format = findExportFormat(exportPath.toStdString(), exporter);

    if (!format)
    {
        logError(QString("Unsupported export format: %1")
            .arg(QFileInfo(exportPath).suffix()));
        return aiReturn_FAILURE;
    }

    logMessage(QString("  -> Using format: %1 (%2)")
        .arg(QString::fromLocal8Bit(format->description))
        .arg(QString::fromLocal8Bit(format->id)));

    aiReturn result = exporter.Export(newScene.get(), format->id, exportPath.toStdString().c_str());

    if (result != aiReturn_SUCCESS)
    {
        logError(QString("Assimp export failed: %1")
            .arg(QString::fromLocal8Bit(exporter.GetErrorString())));
        return result;
    }

    logMessage(QString("Export successful!"));
    logMessage(QString("  -> File: %1").arg(exportPath));
    logMessage(QString("  -> Meshes: %1").arg(aiMeshes.size()));
    logMessage(QString("  -> Materials: %1").arg(aiMaterials.size()));
    logMessage(QString("  -> Textures: %1").arg(_lastTexturePackage.textures.size()));

    // ===== STEP 5: Post-process to add lights and fix materials =====
    logMessage("Step 5: Post-processing exported file...");

    auto logCallback = [this](const QString& msg) { logMessage(msg); };

    // Register camera data so the post-processor injects glTF cameras.
    if (!settings.cameras.isEmpty())
        GltfPostProcessor::setGltfCameraData(settings.cameras);
    else
        GltfPostProcessor::clearGltfCameraData();

    // Register variant data so the post-processor writes KHR_materials_variants
    if (!settings.variantNames.isEmpty() && !_lastVariantEntries.isEmpty())
    {
        GltfPostProcessor::setVariantExportData(settings.variantNames, _lastVariantEntries);
        GltfPostProcessor::setVariantMaterialData(variantMatsByJsonIdx2b);
        logMessage(QString("  -> Variant export: %1 variants, %2 mesh entries, %3 variant materials registered")
            .arg(settings.variantNames.size()).arg(_lastVariantEntries.size())
            .arg(variantMatsByJsonIdx2b.size()));
    }
    else
    {
        GltfPostProcessor::clearVariantExportData();
    }

    // Register pointer-animation data for KHR_animation_pointer injection.
    if (!settings.animationDataList.isEmpty())
        GltfPostProcessor::setPointerAnimationData(
            settings.animationDataList, settings.nodeIndexToExportedName);
    else
        GltfPostProcessor::clearPointerAnimationData();

    if (isGLB)
    {
        if (GltfPostProcessor::postProcessGlbFileWithMaterials(
            exportPath, meshes, settings.lights, logCallback, textureSubfolder, _lastTexturePackage.pathMapping, _lastEmbeddedIndexMapping))
            logMessage("  -> Post-processing complete");
        else
            logWarning("  -> Post-processing failed (file may still be valid)");
    }
    else if (QFileInfo(exportPath).suffix().toLower() == "gltf")
    {
        if (GltfPostProcessor::postProcessGltfFileWithMaterials(
            exportPath, meshes, settings.lights, logCallback, textureSubfolder, _lastTexturePackage.pathMapping, _lastEmbeddedIndexMapping))
            logMessage("  -> Post-processing complete");
        else
            logWarning("  -> Post-processing failed (file may still be valid)");
    }

    GltfPostProcessor::clearVariantExportData();    // Always clean up after use
    GltfPostProcessor::clearGltfCameraData();       // Always clean up after use
    GltfPostProcessor::clearPointerAnimationData(); // Always clean up after use

    // ===== STEP 6: Cleanup temp texture folder for GLB exports =====
    if (isGLB && settings.copyTextures && !_lastTexturePackage.textures.empty())
    {
        logMessage("Step 5: Cleaning up temp texture folder (textures embedded in GLB)...");

        QDir dir(_lastTexturePackage.textureDirectory);
        if (dir.removeRecursively())
        {
            logMessage(QString("  -> Removed temp textures folder: %1")
                .arg(_lastTexturePackage.textureDirectory));
        }
        else
        {
            logWarning(QString("  -> Could not remove temp textures folder: %1")
                .arg(_lastTexturePackage.textureDirectory));
        }
    }

    return aiReturn_SUCCESS;
}

/**
 * Enhanced exportScene method with material application
 *
 * This method now accepts the original mesh objects, applies their materials
 * to the Assimp scene meshes, and then exports the enriched scene.
 *
 * @param scene The Assimp scene to export
 * @param meshes The original ModelViewer mesh objects containing materials and properties
 * @param exportPath The destination file path
 * @return Assimp export result code
 */
aiReturn AssImpMeshExporter::exportScene(
    aiScene* scene,
    const std::vector<SceneMesh*>& meshes,
    const std::string& exportPath)
{
    // Default settings with embedding enabled
    ExportSettings settings;
    settings.copyTextures = true;
    settings.outputDirectory = ".";  // Won't be used for embedded export

    return exportScene(scene, meshes, exportPath, settings);
}

/**
 * Overload: exportScene with texture packaging
 *
 * This version also handles texture packaging alongside material application,
 * useful for self-contained exports.
 */
aiReturn AssImpMeshExporter::exportScene(
    aiScene* scene,
    const std::vector<SceneMesh*>& meshes,
    const std::string& exportPath,
    const ExportSettings& settings)
{
    logMessage(QString("=== AssImpMeshExporter::exportScene (GLB with Embedded Textures) ==="));
    logMessage(QString("Target: %1").arg(QString::fromStdString(exportPath)));
    logMessage(QString("Output directory: %1").arg(settings.outputDirectory));

    if (!scene)
    {
        logError("Scene pointer is null");
        return aiReturn_FAILURE;
    }

    if (scene->mNumMeshes == 0)
    {
        logError("Scene contains no meshes");
        return aiReturn_FAILURE;
    }

    if (!meshes.empty() && scene->mNumMeshes != static_cast<unsigned int>(meshes.size()))
    {
        logMessage(QString("Scene mesh count (%1) differs from runtime mesh count (%2); rebuilding export scene from mesh store")
            .arg(scene->mNumMeshes)
            .arg(meshes.size()));
        return exportMeshes(scene, meshes, QString::fromStdString(exportPath), settings);
    }

    _currentSettings = settings;

    // Check if this is a GLB export (for embedding and cleanup)
    QString exportFilePath = QString::fromStdString(exportPath);
    QFileInfo exportFileInfo(exportFilePath);
    QString ext = exportFileInfo.suffix().toLower();
    bool isGLB = (ext == "glb" || ext == "gltf-binary");
    bool isGLTF = (ext == "gltf");
    QString textureSubfolder = exportFileInfo.baseName() + "_textures";
    QString textureBaseDir = isGLB ? QDir::tempPath() : settings.outputDirectory;

    // ===== STEP 1: Package textures =====
    if (settings.copyTextures && !meshes.empty())
    {
        logMessage("Step 1: Packaging textures...");

        // Extract embedded textures ONLY if legacy glb:// paths still exist
        bool needsLegacyExtraction = hasGlbVirtualPaths(meshes);

        if ((isGLB || isGLTF) && scene && scene->mNumTextures > 0 && needsLegacyExtraction)
        {
            logMessage(" Extracting embedded textures (legacy glb:// detected)...");
            QMap<QString, QString> embeddedMapping = extractEmbeddedTextures(scene, textureBaseDir, textureSubfolder);

            for (auto it = embeddedMapping.begin(); it != embeddedMapping.end(); ++it)
            {
                _lastTexturePackage.pathMapping[it.key()] = it.value();
            }

            logMessage(QString(" -> Injected %1 embedded texture mappings").arg(embeddedMapping.size()));
        }
        else
        {
            logMessage(" Skipping embedded extraction (using cached disk textures)");
        }

        _lastTexturePackage = _textureManager.packageTextures(
            meshes,
            textureBaseDir,
            textureSubfolder);

        // Re-inject ONLY if legacy glb:// paths exist// Re-inject ONLY if legacy 0 && needsLegacyExtraction)
        {
            QMap<QString, QString> embeddedMapping = extractEmbeddedTextures(scene, textureBaseDir, textureSubfolder);

            for (auto it = embeddedMapping.begin(); it != embeddedMapping.end(); ++it)
            {
                _lastTexturePackage.pathMapping[it.key()] = it.value();
            }
        }
        
        // Add normalised-path aliases so the GltfPostProcessor's normalisedGlbPath()
        // lookups ("glb://image_N") find the same entries as the full-path keys
        // ("glb://D:/path/model.glb::image_N") that packageTextures stored.
        // Without this, findOrCreateTexture() misses existing embedded textures and
        // creates duplicate URI-based image entries pointing to temp files.
        {
            QList<QPair<QString, QString>> aliases;
            for (auto it = _lastTexturePackage.pathMapping.constBegin();
                 it != _lastTexturePackage.pathMapping.constEnd(); ++it)
            {
                const QString normKey = GltfPostProcessor::normalisedGlbPath(it.key());
                if (normKey != it.key())
                {
                    aliases.append({normKey, it.value()});
                }
            }
            for (const auto& p : aliases)
            {
                const bool replacingExisting =
                    _lastTexturePackage.pathMapping.contains(p.first) &&
                    _lastTexturePackage.pathMapping.value(p.first) != p.second;
                _lastTexturePackage.pathMapping[p.first] = p.second;
            }
            if (!aliases.isEmpty())
                logMessage(QString("  -> Added %1 normalised-path alias(es) for GLB embedded textures")
                               .arg(aliases.size()));
        }

        logMessage(QString("  -> Total texture mappings: %1").arg(_lastTexturePackage.pathMapping.size()));

        logMessage(QString("  -> Packaged %1 unique textures")
            .arg(_lastTexturePackage.textures.size()));

        if (_lastTexturePackage.totalSize > 0)
        {
            logMessage(QString("  -> Total size: %1 MB")
                .arg(_lastTexturePackage.totalSize / (1024.0 * 1024.0), 0, 'f', 2));
        }

        if (isGLB)
        {
            logMessage("  -> Note: Textures will be embedded in GLB and folder will be cleaned up");
        }
    }
    else
    {
        logMessage("Step 1: Texture copying disabled");
    }

    // ===== STEP 2: Sync scene mesh count to surviving _meshStore entries =====
    // _globalScene is never updated when the user deletes meshes, so the deep
    // copy may contain stale aiMesh/aiNode entries.  Prune them here so that
    // scene->mMeshes and the meshes vector are in 1-to-1 correspondence before
    // applyMaterialsToScene() rebuilds the material array.
    logMessage("Step 2: Syncing scene to mesh store...");
    syncSceneToMeshStore(scene, meshes);

    // ===== STEP 3: Apply materials to scene =====
    // syncSceneToMeshStore produces scene->mMeshes[] in ASCENDING sceneIndex order.
    // _meshStore (and therefore `meshes`) is in traversal order, which may differ.
    // Sort a local copy by sceneIndex so the positional assignment in
    // applyMaterialsToScene correctly pairs each SceneMesh with its aiMesh.
    logMessage("Step 3: Applying materials to scene...");
    std::vector<SceneMesh*> sortedMeshes = meshes;
    std::stable_sort(sortedMeshes.begin(), sortedMeshes.end(),
        [](const SceneMesh* a, const SceneMesh* b)
        {
            return a->getSceneIndex() < b->getSceneIndex();
        });
    applyMaterialsToScene(scene, sortedMeshes, QString::fromStdString(exportPath));

    // ===== STEP 3b: Add variant materials (KHR_materials_variants export) =====
    _lastVariantEntries.clear();
    QMap<int, Material> variantMatsByJsonIdx3b;  // non-default variant mat index -> Material
    if (!settings.variantNames.isEmpty())
    {
        logMessage("Step 3b: Adding variant materials for KHR_materials_variants export...");

        // Collect all new variant aiMaterials so we can extend scene->mMaterials
        std::vector<aiMaterial*> variantAiMats;

        for (size_t vi = 0; vi < sortedMeshes.size() && vi < scene->mNumMeshes; ++vi)
        {
            const SceneMesh* mesh = sortedMeshes[vi];
            MeshVariantExportEntry entry;

            if (!mesh || !mesh->hasVariants())
            {
                _lastVariantEntries.append(entry);
                continue;
            }

            entry.variantMappings = mesh->variantMappings();

            // The default aiMaterial index is whatever applyMaterialsToScene assigned
            int defaultKey    = exportedBaseMaterialKey(mesh);
            int defaultMatIdx = static_cast<int>(scene->mMeshes[vi]->mMaterialIndex);
            entry.matKeyToJsonMatIdx[defaultKey] = defaultMatIdx;

            const QMap<int, Material>& varMats = mesh->allVariantMaterials();
            for (auto it = varMats.constBegin(); it != varMats.constEnd(); ++it)
            {
                int key = it.key();
                if (key == defaultKey) continue;

                aiMaterial* varAiMat = createMaterial(
                    it.value(), _lastTexturePackage,
                    QString::fromStdString(exportPath), mesh->getName());
                if (!varAiMat)
                {
                    logWarning(QString("  -> Failed to create variant material (key %1) for: %2")
                        .arg(key).arg(mesh->getName()));
                    continue;
                }

                // New mat index = current scene mat count + number added so far
                int newMatIdx = static_cast<int>(scene->mNumMaterials) +
                                static_cast<int>(variantAiMats.size());
                variantAiMats.push_back(varAiMat);
                entry.matKeyToJsonMatIdx[key] = newMatIdx;
                variantMatsByJsonIdx3b[newMatIdx] = it.value();

                logMessage(QString("  -> Variant material (key=%1 -> mat[%2]) added for: %3")
                    .arg(key).arg(newMatIdx).arg(mesh->getName()));
            }

            _lastVariantEntries.append(entry);
        }

        // Extend scene->mMaterials to include the variant materials
        if (!variantAiMats.empty())
        {
            unsigned int oldCount = scene->mNumMaterials;
            unsigned int newCount = oldCount + static_cast<unsigned int>(variantAiMats.size());
            aiMaterial** newArray = new aiMaterial*[newCount];
            for (unsigned int i = 0; i < oldCount; ++i)
                newArray[i] = scene->mMaterials[i];
            for (size_t i = 0; i < variantAiMats.size(); ++i)
                newArray[oldCount + i] = variantAiMats[i];
            delete[] scene->mMaterials;
            scene->mMaterials = newArray;
            scene->mNumMaterials = newCount;

            logMessage(QString("  -> Extended scene materials: %1 default + %2 variant = %3 total")
                .arg(oldCount).arg(variantAiMats.size()).arg(newCount));
        }
    }

    // ===== STEP 4: Embed textures in scene (CRITICAL FOR GLB) =====
    logMessage("Step 4: Embedding textures in scene...");

    QStringList embeddedTextureNames;
    // Only embed for GLB export (ext already determined in STEP 1)
    if (isGLB)
    {
        embeddedTextureNames = embedTexturesInScene(scene, _lastTexturePackage);
    }
    else
    {
        logMessage("  -> Skipping texture embedding for non-binary format");
    }

    // ===== STEP 4b: Translate texture paths to relative output paths =====
    // SceneGraphExporter writes the original (possibly absolute) texture paths into the
    // aiMaterial slots.  Assimp's OBJ/FBX/COLLADA writers copy those paths verbatim into
    // the output file, producing non-portable absolute references.  Translate each path to
    // its relative output equivalent using the mapping built by packageTextures().
    // GLB skips this because embedTexturesInScene() replaced all paths with "*N" references.
    if (!isGLB && settings.copyTextures)
    {
        logMessage("Step 4b: Updating material texture paths to relative output paths...");
        updateSceneMaterialPaths(scene, _lastTexturePackage);
    }

    // ===== STEP 5: Export =====
    logMessage("Step 5: Exporting scene...");

    Assimp::Exporter exporter;
    const aiExportFormatDesc* format = findExportFormat(exportPath, exporter);

    if (!format)
    {
        logError(QString("Unsupported export format: %1")
            .arg(QString::fromStdString(ext.toStdString())));
        return aiReturn_FAILURE;
    }

    logMessage(QString("  -> Format: %1 (%2)")
        .arg(QString::fromLocal8Bit(format->description))
        .arg(QString::fromLocal8Bit(format->id)));

    // Set export flags for GLB to embed textures
    aiReturn result;
    if (ext == "glb" || ext == "gltf-binary")
    {
        // Use glTF2 exporter with embedding
        logMessage("  -> Exporting with embedded textures...");
        result = exporter.Export(scene, "glb2", exportPath.c_str());
    }
    else
    {
        result = exporter.Export(scene, format->id, exportPath.c_str());
    }

    if (result != aiReturn_SUCCESS)
    {
        logError(QString("Export failed: %1")
            .arg(QString::fromLocal8Bit(exporter.GetErrorString())));
        return result;
    }

    logMessage(QString("Export successful!"));
    logMessage(QString("  -> File: %1").arg(QString::fromStdString(exportPath)));
    logMessage(QString("  -> Meshes: %1").arg(scene->mNumMeshes));
    logMessage(QString("  -> Materials: %1").arg(scene->mNumMaterials));
    logMessage(QString("  -> Embedded textures: %1").arg(scene->mNumTextures));

    // Patch image names into GLB JSON so post-processor can match them
    if (isGLB && !embeddedTextureNames.isEmpty())
        patchGlbImageNames(exportFilePath, embeddedTextureNames, scene,
            _lastTexturePackage.textureDirectory, &_lastEmbeddedIndexMapping);

    // OBJ-specific: patch the .mtl file with PBR extension lines (Pm, Pr, map_Pm,
    // map_Pr, map_Ke, norm) that Assimp's OBJ writer does not emit.
    if (ext == "obj" && settings.copyTextures)
    {
        QString mtlPath = exportFilePath;
        mtlPath.replace(QRegularExpression("\\.obj$",
            QRegularExpression::CaseInsensitiveOption), ".mtl");
        if (QFileInfo::exists(mtlPath))
        {
            logMessage("Step 5b: Patching MTL with PBR extensions...");
            patchMtlWithPbrExtensions(mtlPath, meshes, _lastTexturePackage);
        }
    }

    // ===== STEP 6: Post-process glTF/GLB to add missing optional properties and write transforms =====
    logMessage("Step 6: Post-processing exported file with material transforms...");

    auto logCallback = [this](const QString& msg) {
        logMessage(msg);
        };

    // Register camera data so the post-processor injects glTF cameras.
    if (!settings.cameras.isEmpty())
        GltfPostProcessor::setGltfCameraData(settings.cameras);
    else
        GltfPostProcessor::clearGltfCameraData();

    // Register variant data so the post-processor writes KHR_materials_variants
    if (!settings.variantNames.isEmpty() && !_lastVariantEntries.isEmpty())
    {
        GltfPostProcessor::setVariantExportData(settings.variantNames, _lastVariantEntries);
        GltfPostProcessor::setVariantMaterialData(variantMatsByJsonIdx3b);
        logMessage(QString("  -> Variant export: %1 variants, %2 mesh entries, %3 variant materials registered")
            .arg(settings.variantNames.size()).arg(_lastVariantEntries.size())
            .arg(variantMatsByJsonIdx3b.size()));
    }
    else
    {
        GltfPostProcessor::clearVariantExportData();
    }

    // Register pointer-animation data for KHR_animation_pointer injection.
    if (!settings.animationDataList.isEmpty())
        GltfPostProcessor::setPointerAnimationData(
            settings.animationDataList, settings.nodeIndexToExportedName);
    else
        GltfPostProcessor::clearPointerAnimationData();

    if (isGLB)
    {
        if (GltfPostProcessor::postProcessGlbFileWithMaterials(exportFilePath, meshes, settings.lights, logCallback, textureSubfolder, _lastTexturePackage.pathMapping, _lastEmbeddedIndexMapping))
        {
            logMessage("  -> Post-processing complete");
        }
        else
        {
            logWarning("  -> Post-processing failed (file may still be valid)");
        }
    }
    else if (ext == "gltf")
    {
        if (GltfPostProcessor::postProcessGltfFileWithMaterials(exportFilePath, meshes, settings.lights, logCallback, textureSubfolder, _lastTexturePackage.pathMapping, _lastEmbeddedIndexMapping))
        {
            logMessage("  -> Post-processing complete");
        }
        else
        {
            logWarning("  -> Post-processing failed (file may still be valid)");
        }
    }

    GltfPostProcessor::clearVariantExportData();
    GltfPostProcessor::clearGltfCameraData();
    GltfPostProcessor::clearPointerAnimationData();

    // ===== STEP 6: Cleanup temp texture folder for GLB exports =====
    if (isGLB && settings.copyTextures && !_lastTexturePackage.textures.empty())
    {
        logMessage("Step 6: Cleaning up temp texture folder (textures embedded in GLB)...");

        QDir dir(_lastTexturePackage.textureDirectory);
        if (dir.removeRecursively())
        {
            logMessage(QString("  -> Removed temp textures folder: %1")
                .arg(_lastTexturePackage.textureDirectory));
        }
        else
        {
            logWarning(QString("  -> Could not remove temp textures folder: %1")
                .arg(_lastTexturePackage.textureDirectory));
        }
    }

    return aiReturn_SUCCESS;
}

aiMesh* AssImpMeshExporter::createMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<unsigned int>& indices,
    const std::string& name,
    GLenum primitiveMode)
{
    auto mesh = new aiMesh();
    mesh->mName = aiString(name.c_str());
    mesh->mNumVertices = static_cast<unsigned int>(vertices.size());
    mesh->mPrimitiveTypes = primitiveModeToAiPrimitiveType(primitiveMode);

    // Allocate vertex attributes
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    mesh->mNormals  = new aiVector3D[mesh->mNumVertices];
    mesh->mColors[0] = new aiColor4D[mesh->mNumVertices];

    // Detect which UV channels carry non-trivial data.
    // Channel 0 is always exported; channels 1-3 only if at least one vertex has a
    // non-zero UV in that channel (avoids inflating the mesh with empty UV sets).
    constexpr unsigned int kVertexUVChannels = 4; // matches Vertex::TexCoords[4]
    bool exportChannel[kVertexUVChannels] = { true, false, false, false };
    for (unsigned int ch = 1; ch < kVertexUVChannels; ++ch)
    {
        for (const Vertex& v : vertices)
        {
            if (v.TexCoords[ch].x != 0.0f || v.TexCoords[ch].y != 0.0f)
            {
                exportChannel[ch] = true;
                break;
            }
        }
    }

    for (unsigned int ch = 0; ch < kVertexUVChannels; ++ch)
    {
        if (!exportChannel[ch]) continue;
        mesh->mTextureCoords[ch]    = new aiVector3D[mesh->mNumVertices];
        mesh->mNumUVComponents[ch]  = 2;
    }

    // Copy vertex data
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
    {
        const auto& v = vertices[i];

        // Position
        mesh->mVertices[i] = aiVector3D(v.Position.x, v.Position.y, v.Position.z);

        // Normal
        mesh->mNormals[i] = aiVector3D(v.Normal.x, v.Normal.y, v.Normal.z);

        // UV coordinates — all exported channels
        for (unsigned int ch = 0; ch < kVertexUVChannels; ++ch)
        {
            if (exportChannel[ch])
                mesh->mTextureCoords[ch][i] = aiVector3D(v.TexCoords[ch].x, v.TexCoords[ch].y, 0.0f);
        }

        // Vertex color (RGBA)
        mesh->mColors[0][i] = aiColor4D(v.Color.r, v.Color.g, v.Color.b, v.Color.a);
    }

    if (!populateFacesForPrimitive(mesh, indices, primitiveMode))
    {
        delete mesh;
        return nullptr;
    }

    return mesh;
}

const aiExportFormatDesc* AssImpMeshExporter::findExportFormat(
    const std::string& filePath,
    Assimp::Exporter& exporter)
{
    // Extract file extension
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos)
    {
        return nullptr;
    }

    std::string ext = filePath.substr(dotPos + 1);

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Search for matching exporter
    for (unsigned int i = 0; i < exporter.GetExportFormatCount(); ++i)
    {
        const aiExportFormatDesc* fmt = exporter.GetExportFormatDescription(i);
        if (fmt && ext == fmt->fileExtension)
        {
            return fmt;
        }
    }

    return nullptr;
}

void AssImpMeshExporter::patchGlbImageNames(
    const QString& glbPath,
    const QStringList& orderedNames,
    const aiScene* scene,
    const QString& textureDirectory,
    QMap<QString, int>* embeddedIndexMapping)
{
    QFile file(glbPath);
    if (!file.open(QIODevice::ReadWrite))
    {
        logWarning("patchGlbImageNames: cannot open GLB for patching");
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    // GLB header: magic(4) version(4) length(4)
    // Chunk 0:    chunkLength(4) chunkType(4=0x4E4F534A) chunkData
    if (data.size() < 28)
        return;

    quint32 chunk0Len = *reinterpret_cast<const quint32*>(data.constData() + 12);
    int jsonStart = 20;   // 12 (header) + 8 (chunk0 header)
    int jsonLen = static_cast<int>(chunk0Len);

    QByteArray jsonBytes = data.mid(jsonStart, jsonLen);
    // Strip padding nulls/spaces that GLB appends to 4-byte-align the chunk
    while (!jsonBytes.isEmpty() && (jsonBytes.back() == '\0' || jsonBytes.back() == ' '))
        jsonBytes.chop(1);

    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject())
    {
        logWarning("patchGlbImageNames: failed to parse GLB JSON");
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray images = root.value("images").toArray();
    QJsonArray textures = root.value("textures").toArray();
    QJsonArray materials = root.value("materials").toArray();

    // === CORRECT APPROACH: Derive image names from the Assimp-written JSON ===
    //
    // Problem: embedTexturesInScene() iterates texTypes[] in a fixed order (BASE_COLOR,
    // NORMALS, METALNESS, ...) and builds embeddedNames[] in that order. But Assimp's GLB2
    // writer serialises material texture slots in its own internal field order (e.g. METALNESS
    // before NORMALS), so the binary image slots do NOT match the embeddedNames[] indices.
    // Assigning embeddedNames[i] to binary slot i puts the wrong name on the wrong data,
    // which the post-processor then uses to misroute texture indices.
    //
    // Fix: Walk the Assimp-written JSON. For each material slot (normalTexture,
    // metallicRoughnessTexture, ...) we know both (a) the image index Assimp assigned and
    // (b) what source path the aiScene holds for that slot. From the source path we can
    // derive the correct filename. This builds an imageIndex->name map that is independent
    // of iteration order.

    // Map from well-known JSON slot key -> {aiTextureType, slot-index-in-type}
    struct SlotInfo { aiTextureType type; unsigned int idx; };

    // Standard PBR slots
    const QMap<QString, SlotInfo> slotMap = {
        { "baseColorTexture",         { aiTextureType_BASE_COLOR,        0 } },
        { "metallicRoughnessTexture", { aiTextureType_METALNESS,         0 } },
        { "normalTexture",            { aiTextureType_NORMALS,           0 } },
        { "occlusionTexture",         { aiTextureType_LIGHTMAP,          0 } },
        { "emissiveTexture",          { aiTextureType_EMISSIVE,          0 } },
    };
    // KHR extension texture slots
    const QMap<QString, SlotInfo> extSlotMap = {
        { "clearcoatTexture",               { aiTextureType_CLEARCOAT,     0 } },
        { "clearcoatRoughnessTexture",      { aiTextureType_CLEARCOAT,     1 } },
        { "clearcoatNormalTexture",         { aiTextureType_CLEARCOAT,     2 } },
        { "sheenColorTexture",              { aiTextureType_SHEEN,         0 } },
        { "sheenRoughnessTexture",          { aiTextureType_SHEEN,         1 } },
        { "transmissionTexture",            { aiTextureType_TRANSMISSION,  0 } },
        { "specularTexture",                { aiTextureType_UNKNOWN,       0 } },
        { "specularColorTexture",           { aiTextureType_UNKNOWN,       1 } },
        { "anisotropyTexture",              { aiTextureType_UNKNOWN,       2 } },
        { "thicknessTexture",               { aiTextureType_UNKNOWN,       3 } },
        { "diffuseTexture",                 { aiTextureType_DIFFUSE,       0 } },
        { "specularGlossinessTexture",      { aiTextureType_SPECULAR,      0 } },
        { "iridescenceTexture",             { aiTextureType_UNKNOWN,       4 } },
        { "iridescenceThicknessTexture",    { aiTextureType_UNKNOWN,       5 } },
        { "diffuseTransmissionTexture",     { aiTextureType_UNKNOWN,       6 } },
        { "diffuseTransmissionColorTexture",{ aiTextureType_UNKNOWN,       7 } },
    };

    // Helper: get the source path Assimp stored for a given material/type/slot
    auto getSourcePath = [&](unsigned int matIdx, aiTextureType type, unsigned int slotIdx) -> QString {
        if (!scene || matIdx >= scene->mNumMaterials) return {};
        aiString texPath;
        if (scene->mMaterials[matIdx]->GetTexture(type, slotIdx, &texPath) == aiReturn_SUCCESS)
            return QString::fromLocal8Bit(texPath.C_Str());
        return {};
        };

    // Walk every JSON material, resolve imageIndex -> filename from the aiScene
    QMap<int, QString> imageNameMap;
    QMap<QString, QString> packagedToOriginalPath;
    for (auto it = _lastTexturePackage.pathMapping.constBegin();
         it != _lastTexturePackage.pathMapping.constEnd(); ++it)
    {
        if (!it.value().isEmpty() && !packagedToOriginalPath.contains(it.value()))
            packagedToOriginalPath[it.value()] = it.key();
    }

    for (int mi = 0; mi < materials.size(); ++mi)
    {
        QJsonObject mat = materials[mi].toObject();

        // Helper: given a texture reference object in the JSON, find which image index
        // it points to and record its name from the aiScene.
        auto resolveSlot = [&](const QString& slotKey, const QJsonObject& texRef,
            bool isExtension) {
                int texIdx = texRef.value("index").toInt(-1);
                if (texIdx < 0 || texIdx >= textures.size()) return;

                int imgIdx = textures[texIdx].toObject().value("source").toInt(-1);
                if (imgIdx < 0 || imgIdx >= images.size()) return;

                if (imageNameMap.contains(imgIdx)) return; // already resolved (shared texture)

                const SlotInfo* info = nullptr;
                if (!isExtension)
                {
                    auto it = slotMap.find(slotKey);
                    if (it != slotMap.end()) info = &it.value();
                }
                else
                {
                    auto it = extSlotMap.find(slotKey);
                    if (it != extSlotMap.end()) info = &it.value();
                }
                if (!info) return;

                QString path = getSourcePath(static_cast<unsigned int>(mi), info->type, info->idx);
                if (path.isEmpty()) return;

                QString authoritativePath = packagedToOriginalPath.value(path, path);
                if (authoritativePath == path)
                {
                    const QString sourceFileName = QFileInfo(path).fileName();
                    for (auto it = packagedToOriginalPath.constBegin();
                         it != packagedToOriginalPath.constEnd(); ++it)
                    {
                        if (QFileInfo(it.key()).fileName().compare(sourceFileName, Qt::CaseInsensitive) == 0)
                        {
                            authoritativePath = it.value();
                            break;
                        }
                    }
                }

                QString name = QFileInfo(path).fileName();
                if (!name.isEmpty())
                {
                    imageNameMap[imgIdx] = name;
                    if (embeddedIndexMapping)
                    {
                        int authoritativeIndex = imgIdx;
                        const QString normalizedPath = GltfPostProcessor::normalisedGlbPath(authoritativePath);
                        if (normalizedPath.startsWith("glb://image_"))
                        {
                            bool ok = false;
                            const int parsedIndex = normalizedPath.mid(QString("glb://image_").size()).toInt(&ok);
                            if (ok)
                                authoritativeIndex = parsedIndex;
                        }

                        (*embeddedIndexMapping)[authoritativePath] = authoritativeIndex;
                        if (normalizedPath != authoritativePath && !embeddedIndexMapping->contains(normalizedPath))
                            (*embeddedIndexMapping)[normalizedPath] = authoritativeIndex;
                    }
                    logMessage(QString("  patchGlbImageNames: img[%1] <- '%2'  (slot %3)")
                        .arg(imgIdx).arg(name).arg(slotKey));
                }
            };

        // Standard PBR slots
        QJsonObject pbr = mat.value("pbrMetallicRoughness").toObject();
        for (const QString& key : { "baseColorTexture", "metallicRoughnessTexture" })
        {
            QJsonObject ref = pbr.value(key).toObject();
            if (!ref.isEmpty()) resolveSlot(key, ref, false);
        }
        for (const QString& key : { "normalTexture", "occlusionTexture", "emissiveTexture" })
        {
            QJsonObject ref = mat.value(key).toObject();
            if (!ref.isEmpty()) resolveSlot(key, ref, false);
        }

        // KHR extension slots
        QJsonObject exts = mat.value("extensions").toObject();
        for (const QString& extName : exts.keys())
        {
            QJsonObject extData = exts.value(extName).toObject();
            for (const QString& key : extSlotMap.keys())
            {
                QJsonObject ref = extData.value(key).toObject();
                if (!ref.isEmpty()) resolveSlot(key, ref, true);
            }
        }
    }

    // Fill in names for existing images not resolved via JSON slot lookup
    for (int i = 0; i < images.size() && i < orderedNames.size(); ++i)
    {
        if (!imageNameMap.contains(i))
        {
            logMessage(QString("  patchGlbImageNames: img[%1] <- '%2'  (fallback)")
                .arg(i).arg(orderedNames[i]));
            imageNameMap[i] = orderedNames[i];
        }
    }

    // For any textures in orderedNames that Assimp didn't write a JSON image entry for
    // (e.g. KHR_materials_volume thickness, KHR_materials_anisotropy — Assimp's GLB2 writer
    // has no native support for these extensions, so it drops them from JSON even though
    // they were embedded in mTextures[]).
    // We identify missing ones by comparing orderedNames against imageNameMap coverage,
    // then pull image bytes directly from scene->mTextures[] (authoritative, in-memory)
    // rather than disk files which may be stale or missing for glb://image_N sources.
    QByteArray extraBinData; // image bytes to append after the existing BIN chunk
    if (orderedNames.size() > images.size() && scene && scene->mNumTextures > 0)
    {
        QJsonArray bufferViews = root.value("bufferViews").toArray();

        // Find current end of BIN data — new images will start here
        int binChunkHeaderOffset = jsonStart + jsonLen;
        while (binChunkHeaderOffset % 4 != 0) binChunkHeaderOffset++;
        quint32 existingBinLen = (binChunkHeaderOffset + 8 <= data.size())
            ? *reinterpret_cast<const quint32*>(data.constData() + binChunkHeaderOffset)
            : 0;
        int nextByteOffset = static_cast<int>(existingBinLen);

        // Build set of base filenames already covered by JSON image entries
        QSet<QString> coveredNames;
        for (auto it = imageNameMap.begin(); it != imageNameMap.end(); ++it)
            coveredNames.insert(QFileInfo(it.value()).completeBaseName());

        for (int i = 0; i < orderedNames.size(); ++i)
        {
            const QString& name = orderedNames[i];
            QString baseName = QFileInfo(name).completeBaseName();

            // Skip if already covered by an existing JSON image entry
            if (coveredNames.contains(baseName)) continue;

            // Find the matching aiTexture by base filename (mFilename is the leaf name
            // set in createEmbeddedTexture, e.g. "image_1.jpeg" or "image_1.png")
            QByteArray imgData;
            QString imgName = name;
            for (unsigned int ti = 0; ti < scene->mNumTextures; ++ti)
            {
                const aiTexture* tex = scene->mTextures[ti];
                if (!tex || tex->mWidth == 0) continue;

                QString texBase = QFileInfo(QString::fromLocal8Bit(tex->mFilename.C_Str()))
                                      .completeBaseName();
                if (texBase.compare(baseName, Qt::CaseInsensitive) != 0) continue;

                if (tex->mHeight == 0)
                {
                    // Compressed image stored as raw bytes
                    imgData = QByteArray(reinterpret_cast<const char*>(tex->pcData),
                                         static_cast<int>(tex->mWidth));
                    // Derive name with the real extension from format hint
                    QString hint = QString::fromLocal8Bit(tex->achFormatHint).toLower();
                    if (!hint.isEmpty() && hint != "png")
                        imgName = baseName + "." + hint;
                    else
                        imgName = baseName + ".png";
                }
                break;
            }

            if (imgData.isEmpty())
            {
                // mTextures lookup failed — fall back to disk file
                QString filePath = textureDirectory + "/" + name;
                if (!QFile::exists(filePath))
                    filePath = textureDirectory + "/" + QFileInfo(name).fileName();
                if (QFile::exists(filePath))
                {
                    QFile f(filePath);
                    if (f.open(QIODevice::ReadOnly))
                    {
                        imgData = f.readAll();
                        f.close();
                        imgName = QFileInfo(name).fileName();
                    }
                }
            }

            if (imgData.isEmpty())
            {
                logWarning(QString("patchGlbImageNames: no data for extra image %1: %2")
                    .arg(i).arg(name));
                continue;
            }

            // Detect MIME type from magic bytes
            QString mimeType = "image/png";
            if (imgData.size() >= 2 &&
                static_cast<uchar>(imgData[0]) == 0xFF &&
                static_cast<uchar>(imgData[1]) == 0xD8)
                mimeType = "image/jpeg";

            // Pad to 4-byte alignment
            while (nextByteOffset % 4 != 0) { extraBinData.append('\0'); nextByteOffset++; }

            // Create bufferView
            QJsonObject newBv;
            newBv["buffer"]     = 0;
            newBv["byteOffset"] = nextByteOffset;
            newBv["byteLength"] = imgData.size();
            int newBvIdx = bufferViews.size();
            bufferViews.append(newBv);

            // Create image entry
            QJsonObject newImg;
            newImg["bufferView"] = newBvIdx;
            newImg["mimeType"]   = mimeType;
            newImg["name"]       = imgName;
            images.append(newImg);
            coveredNames.insert(QFileInfo(imgName).completeBaseName());

            extraBinData.append(imgData);
            nextByteOffset += imgData.size();

            logMessage(QString("  patchGlbImageNames: appended img[%1] bufferView[%2] "
                               "offset=%3 length=%4 name='%5' (from mTextures)")
                       .arg(i).arg(newBvIdx)
                       .arg(newBv["byteOffset"].toInt())
                       .arg(imgData.size())
                       .arg(imgName));
        }

        root["bufferViews"] = bufferViews;

        // The new bufferViews above reference bytes appended past the ORIGINAL
        // BIN chunk length (nextByteOffset now holds the true end-of-data
        // offset, including this function's own appends) - buffers[0].byteLength
        // must grow to match, or a spec-compliant loader rejects the file on
        // reopen (a GLB's embedded buffer has no "uri", so a bufferView that
        // overruns its declared byteLength surfaces as a buffer/uri error even
        // though the real defect is this length field, not a URI). This mirrors
        // GltfPostProcessor.cpp's injectPointerAnimationChannels()/
        // injectMorphWeightAnimations(), which already patch buf0["byteLength"]
        // the same way after their own binary-chunk appends.
        if (nextByteOffset > 0)
        {
            QJsonArray buffers = root.value("buffers").toArray();
            if (!buffers.isEmpty())
            {
                QJsonObject buf0 = buffers[0].toObject();
                if (buf0.value("byteLength").toInt() < nextByteOffset)
                {
                    buf0["byteLength"] = nextByteOffset;
                    buffers[0] = buf0;
                    root["buffers"] = buffers;
                }
            }
        }
    }


    // Apply names to the JSON images array
    bool patched = false;
    for (int i = 0; i < images.size(); ++i)
    {
        QJsonObject img = images[i].toObject();
        // Only patch URI-less (embedded) images
        if (!img.contains("uri") || img.value("uri").toString().isEmpty())
        {
            if (imageNameMap.contains(i))
            {
                img["name"] = imageNameMap[i];
                images[i] = img;
                patched = true;
            }
        }
    }

    if (!patched && extraBinData.isEmpty())
        return;

    root["images"] = images;
    doc.setObject(root);

    // Re-encode JSON and pad to 4-byte boundary
    QByteArray newJson = doc.toJson(QJsonDocument::Compact);
    while (newJson.size() % 4 != 0)
        newJson.append(' ');

    // Reconstruct GLB bytes
    QByteArray newData;
    newData.reserve(data.size() + newJson.size() - jsonLen + extraBinData.size());

    // Copy original header (12 bytes)
    newData.append(data.left(12));

    // Write new JSON chunk length + type + data
    quint32 newChunkLen = static_cast<quint32>(newJson.size());
    newData.append(reinterpret_cast<const char*>(&newChunkLen), 4);
    quint32 chunkType = 0x4E4F534Au;
    newData.append(reinterpret_cast<const char*>(&chunkType), 4);
    newData.append(newJson);

    // Locate the original BIN chunk
    int afterJson = jsonStart + jsonLen;
    while (afterJson % 4 != 0) afterJson++;

    if (!extraBinData.isEmpty() && afterJson + 8 <= data.size())
    {
        // Read existing BIN chunk header, append its data, then our extra data
        quint32 oldBinLen = *reinterpret_cast<const quint32*>(data.constData() + afterJson);
        quint32 binType = *reinterpret_cast<const quint32*>(data.constData() + afterJson + 4);

        quint32 newBinLen = oldBinLen + static_cast<quint32>(extraBinData.size());
        newData.append(reinterpret_cast<const char*>(&newBinLen), 4);
        newData.append(reinterpret_cast<const char*>(&binType), 4);
        newData.append(data.mid(afterJson + 8, static_cast<int>(oldBinLen)));
        newData.append(extraBinData);
    }
    else
    {
        // No extra data — just copy BIN chunk as-is
        newData.append(data.mid(afterJson));
    }

    // Fix total length in GLB header
    quint32 totalLen = static_cast<quint32>(newData.size());
    memcpy(newData.data() + 8, &totalLen, 4);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        logWarning("patchGlbImageNames: cannot write patched GLB");
        return;
    }
    file.write(newData);
    file.close();

    logMessage(QString("  -> Patched %1 image names in GLB JSON").arg(orderedNames.size()));
}

bool AssImpMeshExporter::hasGlbVirtualPaths(const std::vector<SceneMesh*>& meshes)
{
    for (const auto* mesh : meshes)
    {
        if (!mesh) continue;

        const Material& mat = mesh->getMaterial();

        for (int t = 0; t < static_cast<int>(Material::TextureType::Count); ++t)
        {
            const auto& tex = mat.texture(static_cast<Material::TextureType>(t));

            if (!tex.path.empty())
            {
                QString path = QString::fromStdString(tex.path);
                if (path.startsWith("glb://"))
                    return true;
            }
        }
    }
    return false;
}

void AssImpMeshExporter::logMessage(const QString& msg)
{
    if (_currentSettings.verbose)
    {
        qDebug() << msg;
    }
}

void AssImpMeshExporter::logWarning(const QString& msg)
{
    if (_currentSettings.verbose)
    {
        qWarning() << msg;
    }
}

void AssImpMeshExporter::logError(const QString& msg)
{
    qCritical() << msg;
}

// ===== FILE: AssImpMeshExporter_Part2.cpp =====
// This is the continuation of AssImpMeshExporter implementation
// Include this in the same compilation unit or link with Part 1
// Contains: Material creation, PBR properties, texture assignment, scene hierarchy

#include "AssImpMeshExporter.h"
#include "Material.h"
#include "RenderableMesh.h"

#include <QMatrix4x4>
#include <QDebug>

aiMaterial* AssImpMeshExporter::createMaterial(
    const Material& material,
    const TexturePackage& texturePackage,
    const QString& exportFileLocation,  // NEW parameter
    const QString& meshName)             // NEW parameter: fallback name if material name is empty
{
    aiMaterial* aiMat = new aiMaterial();

    // Detect export format once (reused throughout)
    QFileInfo fileInfo(exportFileLocation);
    QString ext = fileInfo.suffix().toLower();
    bool isGLTF = (ext == "gltf" || ext == "glb");

    // ==== NAME & DEBUG INFO =====
    // FIX: Use mesh name as fallback if material name is empty
    // This prevents Assimp from deduplicating materials with empty names
    QString finalMatName = material.name();
    bool usedFallback = false;
    if (finalMatName.isEmpty())
    {
        finalMatName = meshName;
        usedFallback = true;
    }
    aiString matName(finalMatName.toStdString());
    aiMat->AddProperty(&matName, AI_MATKEY_NAME);

    // Log material type and format for debugging
    QString shadingModelStr;
    Material::ShadingModel shadingModel = material.shadingModel();
    switch (shadingModel)
    {
        case Material::ShadingModel::PBR:
            shadingModelStr = "PBR (Metallic-Roughness)";
            break;
        case Material::ShadingModel::BlinnPhong:
            shadingModelStr = "ADS (BlinnPhong)";
            break;
        case Material::ShadingModel::Unlit:
            shadingModelStr = "Unlit";
            break;
        case Material::ShadingModel::Toon:
            shadingModelStr = "Toon";
            break;
        default:
            shadingModelStr = "Unknown";
            break;
    }
    logMessage(QString("Creating material: %1 (Format: %2, Model: %3) [fallback=%4]")
        .arg(finalMatName).arg(ext.toUpper()).arg(shadingModelStr).arg(usedFallback ? "Y, meshName=" + meshName : "N"));

    // ===== COLOR PROPERTIES =====
    {
        aiColor3D albedo(
            static_cast<float>(material.albedoColor().x()),
            static_cast<float>(material.albedoColor().y()),
            static_cast<float>(material.albedoColor().z()));
        logMessage(QString("  *** Albedo color in material: [%1, %2, %3]")
            .arg(albedo.r).arg(albedo.g).arg(albedo.b));

        // For glTF export, Assimp's exporter reads from AI_MATKEY_COLOR_DIFFUSE
        // as the base color, not AI_MATKEY_BASE_COLOR. Set both to be safe.
        aiMat->AddProperty(&albedo, 1, AI_MATKEY_BASE_COLOR);
        aiMat->AddProperty(&albedo, 1, AI_MATKEY_COLOR_DIFFUSE);
    }

    // ===== METALLIC & ROUGHNESS =====
    {
        float metallic = material.metalness();
        aiMat->AddProperty(&metallic, 1, AI_MATKEY_METALLIC_FACTOR);
    }

    {
        float roughness = material.roughness();
        aiMat->AddProperty(&roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);
    }

    // ===== TRANSPARENCY & IOR =====
    {
        float opacity = material.opacity();
        aiMat->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
    }

    {
        float ior = material.ior();
        aiMat->AddProperty(&ior, 1, AI_MATKEY_REFRACTI);
    }

    // ===== TRANSMISSION =====
    if (material.transmission() > 0.0f)
    {
        float transmission = material.transmission();
        aiMat->AddProperty(&transmission, 1, AI_MATKEY_TRANSMISSION_FACTOR);
    }

    // ===== EMISSIVE =====
    {
        aiColor3D emissive(
            static_cast<float>(material.emissive().x()),
            static_cast<float>(material.emissive().y()),
            static_cast<float>(material.emissive().z()));
        aiMat->AddProperty(&emissive, 1, AI_MATKEY_COLOR_EMISSIVE);
    }

    // ===== PHASE 1: BASIC GLTF PROPERTIES =====

    // Alpha Mode
    {
        Material::BlendMode blendMode = material.blendMode();
        aiString alphaModeStr;

        if (blendMode == Material::BlendMode::Opaque)
        {
            alphaModeStr.Set("OPAQUE");
        }
        else if (blendMode == Material::BlendMode::Masked)
        {
            alphaModeStr.Set("MASK");
            float alphaCutoff = material.alphaThreshold();
            aiMat->AddProperty(&alphaCutoff, 1, "$mat.gltf.alphaCutoff", 0, 0);
        }
        else if (blendMode == Material::BlendMode::Alpha)
        {
            alphaModeStr.Set("BLEND");
        }

        aiMat->AddProperty(&alphaModeStr, "$mat.gltf.alphaMode", 0, 0);
    }

    // Double Sided
    {
        // Try to get the value - the exact method name may vary
        bool twoSided = material.twoSided();
        // Check if there's a method to get this - for now default to false
        // This needs the actual Material header to determine correct getter
        int twoSidedInt = twoSided ? 1 : 0;
        aiMat->AddProperty(&twoSidedInt, 1, "$mat.gltf.doubleSided", 0, 0);
    }

    // ===== PHASE 2: COMMON EXTENSIONS =====

    {
        float emissiveStrength = material.emissiveStrength();
        aiMat->AddProperty(&emissiveStrength, 1, AI_MATKEY_EMISSIVE_INTENSITY);
    }

    // ===== NORMAL SCALE =====
    {
        float normalScale = material.normalScale();
        aiMat->AddProperty(&normalScale, 1, AI_MATKEY_BUMPSCALING);
    }

    // ===== CLEARCOAT =====
    if (material.clearcoat() > 0.0f)
    {
        float clearcoat = material.clearcoat();
        aiMat->AddProperty(&clearcoat, 1, AI_MATKEY_CLEARCOAT_FACTOR);

        float clearcoatRoughness = material.clearcoatRoughness();
        aiMat->AddProperty(&clearcoatRoughness, 1, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR);
    }

    // ===== SHEEN =====
    if (material.sheenColor().length() > 0.0f)
    {
        aiColor3D sheenColor(
            static_cast<float>(material.sheenColor().x()),
            static_cast<float>(material.sheenColor().y()),
            static_cast<float>(material.sheenColor().z()));
        aiMat->AddProperty(&sheenColor, 1, AI_MATKEY_SHEEN_COLOR_FACTOR);

        float sheenRoughness = material.sheenRoughness();
        aiMat->AddProperty(&sheenRoughness, 1, AI_MATKEY_SHEEN_ROUGHNESS_FACTOR);
    }

    // ===== LEGACY ADS =====
    // Check material's shading model to determine if we should export ADS properties    
    bool isADSMaterial = (shadingModel == Material::ShadingModel::BlinnPhong);

    if (isADSMaterial && !isGLTF)
    {
        // ADS materials export specular properties for OBJ, FBX, etc.
        logMessage(QString("  -> Exporting material as ADS (BlinnPhong model)"));

        aiColor3D ambient(
            static_cast<float>(material.ambient().x()),
            static_cast<float>(material.ambient().y()),
            static_cast<float>(material.ambient().z()));
        aiMat->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);

        aiColor3D diffuse(
            static_cast<float>(material.diffuse().x()),
            static_cast<float>(material.diffuse().y()),
            static_cast<float>(material.diffuse().z()));
        aiMat->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);

        aiColor3D specular(
            static_cast<float>(material.specular().x()),
            static_cast<float>(material.specular().y()),
            static_cast<float>(material.specular().z()));
        aiMat->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);

        float shininess = material.shininess();
        aiMat->AddProperty(&shininess, 1, AI_MATKEY_SHININESS);
    }
    else if (isADSMaterial && isGLTF)
    {
        // Warn if ADS material is being exported to glTF (which doesn't support ADS)
        logWarning(QString("  -> Material marked as ADS but exporting to glTF (PBR only): %1")
            .arg(material.name()));
    }

    // ===== TEXTURES =====
    assignTexturesToMaterial(aiMat, material, texturePackage, true, exportFileLocation, isGLTF);

    return aiMat;
}

void AssImpMeshExporter::assignTexturesToMaterial(
    aiMaterial* aiMat,
    const Material& material,
    const TexturePackage& texturePackage,
    bool useEmbeddedTextures,
    const QString& exportFileLocation,
    bool isGLTF)
{
    logMessage(QString("  -> Assigning textures to material..."));

    // IMPORTANT: glTF texture handling notes:
    // 1. Metallic and Roughness MUST use the SAME texture (metallicRoughnessTexture)
    //    - Blue channel = metallic
    //    - Green channel = roughness
    // 2. Opacity/transparency is in the ALPHA channel of baseColorTexture, NOT a separate texture

    // Check if metallic and roughness use the same texture (for glTF)
    const auto& metallicTex = material.texture(Material::TextureType::Metallic);
    const auto& roughnessTex = material.texture(Material::TextureType::Roughness);
    bool hasMetallicRoughness = !metallicTex.path.empty() || !roughnessTex.path.empty();

    // For proper glTF export, metallic and roughness should point to the same texture
    // If they're different (which shouldn't happen for glTF), use metallic texture
    std::string metallicRoughnessPath = !metallicTex.path.empty() ? metallicTex.path : roughnessTex.path;

    // Build texture mappings based on format.
    // Each entry is {Material::TextureType, aiTextureType, slot-index}.
    // The slot index matters for types shared by multiple logical slots
    // (e.g. CLEARCOAT for clearcoat color/roughness/normal, UNKNOWN for specular/anisotropy).
    struct TexMapping { Material::TextureType mvType; aiTextureType aiType; unsigned int slot; };
    std::vector<TexMapping> textureMappings;

    if (isGLTF)
    {
        // glTF-specific mappings (Metallic/Roughness handled separately below)
        textureMappings = {
            {Material::TextureType::Albedo,             aiTextureType_BASE_COLOR,    0},
            {Material::TextureType::Normal,             aiTextureType_NORMALS,       0},
            {Material::TextureType::AmbientOcclusion,   aiTextureType_LIGHTMAP,      0},
            {Material::TextureType::Emissive,           aiTextureType_EMISSIVE,      0},
            {Material::TextureType::Transmission,       aiTextureType_TRANSMISSION,  0},
            {Material::TextureType::Height,             aiTextureType_HEIGHT,        0},
            {Material::TextureType::ClearcoatColor,     aiTextureType_CLEARCOAT,     0},
            {Material::TextureType::ClearcoatRoughness, aiTextureType_CLEARCOAT,     1},
            {Material::TextureType::ClearcoatNormal,    aiTextureType_CLEARCOAT,     2},
            {Material::TextureType::SheenColor,         aiTextureType_SHEEN,         0},
            {Material::TextureType::SheenRoughness,     aiTextureType_SHEEN,         1},
            {Material::TextureType::SpecularFactor,     aiTextureType_UNKNOWN,       0},
            {Material::TextureType::SpecularColor,      aiTextureType_UNKNOWN,       1},
            {Material::TextureType::Anisotropy,         aiTextureType_UNKNOWN,       2},
            {Material::TextureType::Thickness,          aiTextureType_UNKNOWN,       3},
            {Material::TextureType::Diffuse,            aiTextureType_DIFFUSE,       0},
            {Material::TextureType::SpecularGlossiness, aiTextureType_SPECULAR,      0},
            {Material::TextureType::Iridescence,        aiTextureType_UNKNOWN,       4},
            {Material::TextureType::IridescenceThickness, aiTextureType_UNKNOWN,     5},
            {Material::TextureType::DiffuseTransmission,      aiTextureType_UNKNOWN, 6},
            {Material::TextureType::DiffuseTransmissionColor, aiTextureType_UNKNOWN, 7},


        };
    }
    else
    {
        // Other formats (OBJ, FBX, etc.)
        textureMappings = {
            {Material::TextureType::Albedo,             aiTextureType_BASE_COLOR,        0},
            {Material::TextureType::Metallic,           aiTextureType_METALNESS,         0},
            {Material::TextureType::Roughness,          aiTextureType_DIFFUSE_ROUGHNESS, 0},
            {Material::TextureType::Normal,             aiTextureType_NORMALS,           0},
            {Material::TextureType::AmbientOcclusion,   aiTextureType_LIGHTMAP,          0},
            {Material::TextureType::Emissive,           aiTextureType_EMISSIVE,          0},
            {Material::TextureType::Transmission,       aiTextureType_TRANSMISSION,      0},
            {Material::TextureType::Opacity,            aiTextureType_OPACITY,           0},
            {Material::TextureType::Height,             aiTextureType_HEIGHT,            0},
            {Material::TextureType::ClearcoatColor,     aiTextureType_CLEARCOAT,         0},
            {Material::TextureType::ClearcoatRoughness, aiTextureType_CLEARCOAT,         1},
            {Material::TextureType::ClearcoatNormal,    aiTextureType_CLEARCOAT,         2},
            {Material::TextureType::SheenColor,         aiTextureType_SHEEN,             0},
            {Material::TextureType::SheenRoughness,     aiTextureType_SHEEN,             1},
            {Material::TextureType::Anisotropy,         aiTextureType_UNKNOWN,           2},
            {Material::TextureType::Thickness,          aiTextureType_UNKNOWN,           3},
        };
    }

    for (const auto& mapping : textureMappings)
    {
        const auto& tex = material.texture(mapping.mvType);
        if (tex.path.empty())
            continue;

        // For glTF, skip AO in general loop - will be handled specially after M/R packing
        // (either as packed ORM or as independent texture)
        if (isGLTF && mapping.mvType == Material::TextureType::AmbientOcclusion)
            continue;

        // For glTF, skip Height texture - glTF 2.0 doesn't have a standard height/displacement map
        // Height maps are supported in OBJ, FBX, etc. but not in glTF
        if (isGLTF && mapping.mvType == Material::TextureType::Height)
        {
            logMessage(QString("     -> Skipping Height texture for glTF (not supported in glTF 2.0)"));
            continue;
        }

        QString originalPath = QString::fromStdString(tex.path);

        // Look up with the original path first (handles full "glb://filepath::image_N"
        // URIs that packageTextures stores verbatim).  Fall back to the normalised form
        // "glb://image_N" for any legacy entries injected by extractEmbeddedTextures.
        auto it = texturePackage.pathMapping.find(originalPath);
        if (it == texturePackage.pathMapping.end())
            it = texturePackage.pathMapping.find(GltfPostProcessor::normalisedGlbPath(originalPath));

        if (it == texturePackage.pathMapping.end())
        {
            logWarning(QString("Texture not found in package: %1").arg(originalPath));
            continue;
        }

        QString texturePath = it.value();
        texturePath.replace("\\", "/");
        const unsigned int slot = mapping.slot;
        const aiTextureType aiType = mapping.aiType;

        aiString aiPath(texturePath.toStdString());
        aiMat->AddProperty(&aiPath, AI_MATKEY_TEXTURE(aiType, slot));

        int uvIndex = tex.texCoordIndex;
        aiMat->AddProperty(&uvIndex, 1, AI_MATKEY_UVWSRC(aiType, slot));

        int mappingModeU = aiTextureMapMode_Wrap;
        int mappingModeV = aiTextureMapMode_Wrap;
        aiMat->AddProperty(&mappingModeU, 1, AI_MATKEY_MAPPINGMODE_U(aiType, slot));
        aiMat->AddProperty(&mappingModeV, 1, AI_MATKEY_MAPPINGMODE_V(aiType, slot));

        aiUVTransform uvTransform;
        uvTransform.mTranslation = aiVector2D(tex.offset.x, tex.offset.y);
        uvTransform.mScaling = aiVector2D(tex.scale.x, tex.scale.y);
        uvTransform.mRotation = tex.rotation;
        aiMat->AddProperty(&uvTransform, 1, AI_MATKEY_UVTRANSFORM(aiType, slot));

        logMessage(QString("     -> %1: %2 (UV: scale=[%3,%4] offset=[%5,%6] rotation=%7)")
            .arg(Material::textureTypeToString(mapping.mvType))
            .arg(texturePath)
            .arg(tex.scale.x).arg(tex.scale.y)
            .arg(tex.offset.x).arg(tex.offset.y)
            .arg(tex.rotation));
    }

    // ===== METALLIC-ROUGHNESS COMBINED TEXTURE (glTF ONLY) =====
    // In glTF, metallic and roughness MUST be in the same texture
    // Blue channel = metallic, Green channel = roughness
    // For other formats, they were already handled as separate textures above
    QString packedPath;  // Declared here so it's accessible for AO handling below
    QString aoPath;
    bool addedOrmAsOcclusion = false;  // Track if we already added ORM as occlusion texture

    if (isGLTF && hasMetallicRoughness)
    {
        // Check if we need to pack ORM (Occlusion, Roughness, Metallic) textures
        const auto& aoTex = material.texture(Material::TextureType::AmbientOcclusion);
        aoPath = QString::fromStdString(aoTex.path);
        QString metallicPath = QString::fromStdString(metallicTex.path);
        QString roughnessPath = QString::fromStdString(roughnessTex.path);

        logMessage(QString("=== ORM Packing Debug Info ==="));
        logMessage(QString("  Metallic texture path from material: %1").arg(metallicPath.isEmpty() ? "(empty)" : metallicPath));
        logMessage(QString("  Roughness texture path from material: %1").arg(roughnessPath.isEmpty() ? "(empty)" : roughnessPath));
        logMessage(QString("  AO texture path from material: %1").arg(aoPath.isEmpty() ? "(empty)" : aoPath));
        logMessage(QString("  *** Scalar roughness value in material: %1").arg(material.roughness()));
        logMessage(QString("  *** Scalar metalness value in material: %1").arg(material.metalness()));

        // EDGE CASE HANDLING for M/R textures:
        // - If ROUGHNESS exists: pack into metallicRoughnessTexture (create dummy M if needed)
        // - If ROUGHNESS missing but METALLIC exists: metallic-only - skip M/R packing
        // - If neither exist: use scalars only - skip M/R packing
        if (roughnessPath.isEmpty() && !metallicPath.isEmpty())
        {
            logMessage(QString("  -> Metallic-only material: skipping metallicRoughnessTexture (using metalnessFactor scalar only)"));
            // Don't enter texture packing block - just use metalness scalar
        }
        else if (!roughnessPath.isEmpty())
        {
            // Roughness exists - pack into metallicRoughnessTexture (create dummy M if needed)
            // Try ORM packing first (handles all three textures if available)
            // Returns empty string if packing isn't needed
            packedPath = packORMIfSeparate(material, texturePackage, _currentSettings.outputDirectory);

        if (!packedPath.isEmpty())
        {
            // ORM packing succeeded - use the packed texture
            metallicRoughnessPath = packedPath.toStdString();
            logMessage(QString("     -> Using packed ORM texture: %1").arg(packedPath));
        }
        else if (!metallicPath.isEmpty() && !roughnessPath.isEmpty() && metallicPath != roughnessPath)
        {
            // ORM packing not applicable, try M/R-only packing (for backwards compatibility)
            packedPath = packMetallicRoughnessIfSeparate(material, texturePackage, _currentSettings.outputDirectory);
            if (!packedPath.isEmpty())
            {
                // Use the packed texture instead of the original path
                metallicRoughnessPath = packedPath.toStdString();
                logMessage(QString("     -> Using packed M/R texture: %1").arg(packedPath));
            }
            else
            {
                // Packing failed - fall back to original logic using one of the textures
                logMessage(QString("     -> M/R packing failed, using single texture"));
            }
        }

        QString originalPath = QString::fromStdString(metallicRoughnessPath);
        auto it = texturePackage.pathMapping.find(originalPath);
        if (it == texturePackage.pathMapping.end())
            it = texturePackage.pathMapping.find(GltfPostProcessor::normalisedGlbPath(originalPath));

        // If the path is a newly created packed filename, it won't be in pathMapping yet
        // In that case, use it directly (relative to output directory)
        QString texturePath;
        if (it != texturePackage.pathMapping.end())
        {
            texturePath = it.value();
        }
        else if (!packedPath.isEmpty() && metallicRoughnessPath == packedPath.toStdString())
        {
            // Packed texture - use directly with relative path
            texturePath = packedPath;
        }
        else
        {
            // Fallback: nothing found, skip
            logWarning(QString("Metallic/Roughness texture not found in package"));
        }

        if (!texturePath.isEmpty())
        {
            texturePath = texturePath.replace("\\", "/");

            // Add as BOTH metalness and roughness textures (only if present)
            // For roughness-only materials, only add roughness; for metallic-only, only add metallic
            aiString aiPath(texturePath.toStdString());

            // Use the metallic texture's properties (they should be the same for both)
            const auto& refTex = !metallicTex.path.empty() ? metallicTex : roughnessTex;
            int uvIndex = refTex.texCoordIndex;

            // Only add metalness texture if metallic is actually present
            if (!metallicTex.path.empty())
            {
                logMessage(QString("     -> Adding metalness texture: %1").arg(texturePath));
                aiMat->AddProperty(&aiPath, AI_MATKEY_TEXTURE(aiTextureType_METALNESS, 0));
                aiMat->AddProperty(&uvIndex, 1, AI_MATKEY_UVWSRC(aiTextureType_METALNESS, 0));
            }
            else
            {
                logMessage(QString("     -> Skipping metalness texture (no metallic in material)"));
            }

            // Only add roughness texture if roughness is actually present
            if (!roughnessTex.path.empty())
            {
                logMessage(QString("     -> Adding roughness texture: %1").arg(texturePath));
                aiMat->AddProperty(&aiPath, AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE_ROUGHNESS, 0));
                aiMat->AddProperty(&uvIndex, 1, AI_MATKEY_UVWSRC(aiTextureType_DIFFUSE_ROUGHNESS, 0));
            }
            else
            {
                logMessage(QString("     -> Skipping roughness texture (no roughness in material)"));
            }

            // If we packed ORM, add as occlusion texture with packed texture
            // The packed texture has occlusion data in the R channel
            // (even if AO was missing, packORMIfSeparate creates a white default)
            if (!packedPath.isEmpty())
            {
                const bool canReusePackedOrmForAo =
                    aoTex.path.empty() || textureBindingCompatibleForSharedExport(aoTex, refTex);

                if (canReusePackedOrmForAo)
                {
                    // Use LIGHTMAP type for glTF occlusion export
                    aiMat->AddProperty(&aiPath, AI_MATKEY_TEXTURE(aiTextureType_LIGHTMAP, 0));
                    aiMat->AddProperty(&uvIndex, 1, AI_MATKEY_UVWSRC(aiTextureType_LIGHTMAP, 0));

                    // UV transforms for occlusion (use metallic/roughness texture's transform)
                    aiUVTransform ormOcclusionTransform;
                    ormOcclusionTransform.mTranslation = aiVector2D(refTex.offset.x, refTex.offset.y);
                    ormOcclusionTransform.mScaling = aiVector2D(refTex.scale.x, refTex.scale.y);
                    ormOcclusionTransform.mRotation = refTex.rotation;
                    aiMat->AddProperty(&ormOcclusionTransform, 1, AI_MATKEY_UVTRANSFORM(aiTextureType_LIGHTMAP, 0));

                    addedOrmAsOcclusion = true;  // Mark that we've already handled occlusion
                    logMessage(QString("     -> Adding packed ORM as occlusion texture (R channel): %1").arg(texturePath));
                }
                else
                {
                    logMessage(QString("     -> AO binding differs from packed M/R binding; exporting AO separately"));
                }
            }

            // UV transforms (only for textures that were actually added)
            aiUVTransform uvTransform;
            uvTransform.mTranslation = aiVector2D(refTex.offset.x, refTex.offset.y);
            uvTransform.mScaling = aiVector2D(refTex.scale.x, refTex.scale.y);
            uvTransform.mRotation = refTex.rotation;
            if (!metallicTex.path.empty())
                aiMat->AddProperty(&uvTransform, 1, AI_MATKEY_UVTRANSFORM(aiTextureType_METALNESS, 0));
            if (!roughnessTex.path.empty())
                aiMat->AddProperty(&uvTransform, 1, AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE_ROUGHNESS, 0));

            logMessage(QString("     -> metallicRoughnessTexture (glTF): %1").arg(texturePath));
        }
        else if (!packedPath.isEmpty())
        {
            // If texture path lookup failed but we have a packed path, mark that we handled occlusion
            addedOrmAsOcclusion = true;
            logWarning(QString("Metallic/Roughness texture path not found, but ORM was packed: %1").arg(packedPath));
        }
        }  // End else if: Roughness exists - pack into metallicRoughnessTexture
    }  // End if: hasMetallicRoughness

    // ===== AMBIENT OCCLUSION TEXTURE (glTF ONLY) =====
    // Add independent AO only if we didn't already add it as part of packed ORM
    if (isGLTF && !addedOrmAsOcclusion)
    {
        // Get AO path if we haven't already
        if (aoPath.isEmpty())
        {
            const auto& aoTex = material.texture(Material::TextureType::AmbientOcclusion);
            aoPath = QString::fromStdString(aoTex.path);
        }

        if (!aoPath.isEmpty())
        {
            QString originalAoPath = aoPath;
            auto it = texturePackage.pathMapping.find(originalAoPath);
            if (it == texturePackage.pathMapping.end())
                it = texturePackage.pathMapping.find(GltfPostProcessor::normalisedGlbPath(originalAoPath));

            if (it != texturePackage.pathMapping.end())
            {
                QString aoTexturePath = it.value();
                aoTexturePath.replace("\\", "/");
                aiString aiAoPath(aoTexturePath.toStdString());

                const auto& aoTex = material.texture(Material::TextureType::AmbientOcclusion);
                // Use LIGHTMAP type for glTF occlusion export
                aiMat->AddProperty(&aiAoPath, AI_MATKEY_TEXTURE(aiTextureType_LIGHTMAP, 0));
                int aoUvIndex = aoTex.texCoordIndex;
                aiMat->AddProperty(&aoUvIndex, 1, AI_MATKEY_UVWSRC(aiTextureType_LIGHTMAP, 0));

                // UV transforms for AO
                aiUVTransform aoUvTransform;
                aoUvTransform.mTranslation = aiVector2D(aoTex.offset.x, aoTex.offset.y);
                aoUvTransform.mScaling = aiVector2D(aoTex.scale.x, aoTex.scale.y);
                aoUvTransform.mRotation = aoTex.rotation;
                aiMat->AddProperty(&aoUvTransform, 1, AI_MATKEY_UVTRANSFORM(aiTextureType_LIGHTMAP, 0));

                logMessage(QString("     -> ambientOcclusionTexture (glTF): %1").arg(aoTexturePath));
            }
        }
    }
}

void AssImpMeshExporter::updateSceneMaterialPaths(aiScene* scene, const TexturePackage& pkg)
{
    if (!scene || pkg.pathMapping.isEmpty())
        return;

    // Every aiTextureType that SceneGraphExporter or applyMaterialsToScene may have written.
    static const aiTextureType kTypes[] = {
        aiTextureType_DIFFUSE,
        aiTextureType_SPECULAR,
        aiTextureType_AMBIENT,
        aiTextureType_EMISSIVE,
        aiTextureType_NORMALS,
        aiTextureType_HEIGHT,
        aiTextureType_SHININESS,
        aiTextureType_OPACITY,
        aiTextureType_DISPLACEMENT,
        aiTextureType_LIGHTMAP,
        aiTextureType_REFLECTION,
        aiTextureType_BASE_COLOR,
        aiTextureType_NORMAL_CAMERA,
        aiTextureType_EMISSION_COLOR,
        aiTextureType_METALNESS,
        aiTextureType_DIFFUSE_ROUGHNESS,
        aiTextureType_AMBIENT_OCCLUSION,
        aiTextureType_TRANSMISSION,
        aiTextureType_CLEARCOAT,
        aiTextureType_SHEEN,
        aiTextureType_UNKNOWN,
    };

    for (unsigned int m = 0; m < scene->mNumMaterials; ++m)
    {
        aiMaterial* mat = scene->mMaterials[m];
        if (!mat)
            continue;

        for (aiTextureType type : kTypes)
        {
            const unsigned int count = mat->GetTextureCount(type);
            for (unsigned int slot = 0; slot < count; ++slot)
            {
                aiString aiPath;
                if (mat->GetTexture(type, slot, &aiPath) != AI_SUCCESS)
                    continue;

                const QString orig = QString::fromUtf8(aiPath.C_Str());
                if (orig.isEmpty())
                    continue;

                // Try the stored path, then the normalised glb:// form.
                auto it = pkg.pathMapping.find(orig);
                if (it == pkg.pathMapping.end())
                    it = pkg.pathMapping.find(GltfPostProcessor::normalisedGlbPath(orig));
                if (it == pkg.pathMapping.end())
                    continue;

                QString rel = it.value();
                rel.replace('\\', '/');
                aiString newPath(rel.toStdString());
                mat->AddProperty(&newPath, AI_MATKEY_TEXTURE(type, slot));
            }
        }
    }
}

void AssImpMeshExporter::patchMtlWithPbrExtensions(
    const QString& mtlPath,
    const std::vector<SceneMesh*>& meshes,
    const TexturePackage& pkg)
{
    // Read the file Assimp wrote.
    QFile file(mtlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        logWarning(QString("MTL patch: cannot open %1").arg(mtlPath));
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    // Build name → Material lookup matching SceneGraphExporter's naming logic.
    QMap<QString, Material> matByName;
    for (const SceneMesh* mesh : meshes)
    {
        if (!mesh)
            continue;
        const Material mat = mesh->getMaterial();
        const QString name = mat.name().trimmed().isEmpty()
            ? mesh->getName()
            : mat.name().trimmed();
        matByName[name] = mat;
    }

    // Helper: resolve a texture path to a relative output path.
    auto relPath = [&](const Material::Texture& tex) -> QString
    {
        if (tex.path.empty())
            return {};
        const QString orig = QString::fromStdString(tex.path);
        auto it = pkg.pathMapping.find(orig);
        if (it == pkg.pathMapping.end())
            it = pkg.pathMapping.find(GltfPostProcessor::normalisedGlbPath(orig));
        if (it == pkg.pathMapping.end())
            return {};
        QString rel = it.value();
        rel.replace('\\', '/');
        return rel;
    };

    // Helper: append PBR lines for one material.
    auto appendPbr = [&](QStringList& out, const Material& mat)
    {
        out << "# PBR extensions (Pm/Pr/map_Pm/map_Pr/map_Ke/norm)";
        out << QString("Pm %1").arg(mat.metalness(), 0, 'f', 4);
        out << QString("Pr %1").arg(mat.roughness(), 0, 'f', 4);

        const QString metallicTex = relPath(mat.texture(Material::TextureType::Metallic));
        if (!metallicTex.isEmpty())
            out << QString("map_Pm %1").arg(metallicTex);

        const QString roughnessTex = relPath(mat.texture(Material::TextureType::Roughness));
        if (!roughnessTex.isEmpty())
            out << QString("map_Pr %1").arg(roughnessTex);

        // Emissive texture — Assimp writes Ke scalar but never map_Ke.
        const QString emissiveTex = relPath(mat.texture(Material::TextureType::Emissive));
        if (!emissiveTex.isEmpty())
            out << QString("map_Ke %1").arg(emissiveTex);

        // norm: tangent-space normal map, preferred over bump for PBR-aware importers.
        const QString normalTex = relPath(mat.texture(Material::TextureType::Normal));
        if (!normalTex.isEmpty())
            out << QString("norm %1").arg(normalTex);

        out << "";  // blank separator before next block
    };

    // Walk line-by-line, flushing PBR lines at each newmtl boundary and at EOF.
    const QStringList lines = content.split('\n');
    QStringList output;
    output.reserve(lines.size() + 64);

    const Material* current = nullptr;

    for (const QString& line : lines)
    {
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith("newmtl "))
        {
            // Flush PBR block for the previous material before starting a new one.
            if (current)
                appendPbr(output, *current);

            output << line;

            const QString name = trimmed.mid(7).trimmed();
            auto it = matByName.find(name);
            current = (it != matByName.end()) ? &it.value() : nullptr;
        }
        else
        {
            output << line;
        }
    }

    // Flush the last material block.
    if (current)
        appendPbr(output, *current);

    // Write back.
    QFile outFile(mtlPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        logWarning(QString("MTL patch: cannot write %1").arg(mtlPath));
        return;
    }
    outFile.write(output.join('\n').toUtf8());
    outFile.close();

    logMessage(QString("  -> Patched %1 with PBR extensions").arg(QFileInfo(mtlPath).fileName()));
}

/**
 * Helper method: Apply materials from original meshes to Assimp scene meshes
 *
 * This method:
 * 1. Iterates through scene meshes and corresponding ModelViewer mesh objects
 * 2. Creates/updates Assimp materials from Material data
 * 3. Assigns textures and PBR properties
 * 4. Updates material indices in mesh references
 *
 * @param scene The Assimp scene whose materials will be updated
 * @param meshes The original ModelViewer mesh objects
 */
void AssImpMeshExporter::syncSceneToMeshStore(
    aiScene* scene,
    const std::vector<SceneMesh*>& meshes)
{
    if (!scene || !scene->mRootNode || meshes.empty())
        return;

    // Nothing to do if counts already match
    if (scene->mNumMeshes == static_cast<unsigned int>(meshes.size()))
        return;

    logMessage(QString("syncSceneToMeshStore: scene has %1 meshes, meshStore has %2 — pruning stale entries")
        .arg(scene->mNumMeshes).arg(meshes.size()));

    // Build the set of original aiScene mesh indices that are still alive in _meshStore.
    // Each SceneMesh carries the index it was assigned at load time via setSceneIndex(),
    // so this is an exact, name-independent match.
    QSet<int> survivingSceneIndices;
    for (const SceneMesh* m : meshes)
    {
        int idx = m->getSceneIndex();
        if (idx >= 0)
            survivingSceneIndices.insert(idx);
    }

    // Build old-index → new-index map (-1 means the mesh was deleted)
    std::vector<int> oldToNew(scene->mNumMeshes, -1);
    std::vector<aiMesh*> keptMeshes;
    keptMeshes.reserve(meshes.size());

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh* m = scene->mMeshes[i];
        if (m && survivingSceneIndices.contains(static_cast<int>(i)))
        {
            oldToNew[i] = static_cast<int>(keptMeshes.size());
            keptMeshes.push_back(m);
        }
        else
        {
            // Free the stale aiMesh — it was deep-copied so we own it
            delete m;
            scene->mMeshes[i] = nullptr;
        }
    }

    // Replace the mesh array with the pruned one
    delete[] scene->mMeshes;
    scene->mNumMeshes = static_cast<unsigned int>(keptMeshes.size());
    scene->mMeshes = new aiMesh*[scene->mNumMeshes];
    std::copy(keptMeshes.begin(), keptMeshes.end(), scene->mMeshes);

    // Remap mesh index references in every aiNode, dropping deleted indices
    std::function<void(aiNode*)> remapNode = [&](aiNode* node)
    {
        if (!node)
            return;

        std::vector<unsigned int> remapped;
        remapped.reserve(node->mNumMeshes);

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            unsigned int oldIdx = node->mMeshes[i];
            if (oldIdx < oldToNew.size() && oldToNew[oldIdx] >= 0)
                remapped.push_back(static_cast<unsigned int>(oldToNew[oldIdx]));
            // else: index belonged to a deleted mesh — drop it silently
        }

        delete[] node->mMeshes;
        node->mNumMeshes = static_cast<unsigned int>(remapped.size());
        if (!remapped.empty())
        {
            node->mMeshes = new unsigned int[node->mNumMeshes];
            std::copy(remapped.begin(), remapped.end(), node->mMeshes);
        }
        else
        {
            node->mMeshes = nullptr;
        }

        for (unsigned int c = 0; c < node->mNumChildren; ++c)
            remapNode(node->mChildren[c]);
    };

    remapNode(scene->mRootNode);

    logMessage(QString("syncSceneToMeshStore: pruned to %1 meshes").arg(scene->mNumMeshes));
}

void AssImpMeshExporter::applyMaterialsToScene(
    aiScene* scene,
    const std::vector<SceneMesh*>& meshes,
    const QString& exportFileLocation)  // NEW parameter
{
    if (!scene || meshes.empty())
    {
        logWarning("applyMaterialsToScene: Invalid input");
        return;
    }

    // CRITICAL: SceneGraphExporter already created the correct materials and assigned them to meshes.
    // We preserve that material array and only replace specific preserved materials that would
    // serialize to a non-glTF-compliant metallic/roughness layout.
    const QString exportExt = QFileInfo(exportFileLocation).suffix().toLower();
    const bool isGltfFamily = (exportExt == "gltf" || exportExt == "glb" || exportExt == "gltf-binary");

    logMessage(QString("applyMaterialsToScene: Preserving %1 materials from SceneGraphExporter")
        .arg(scene->mNumMaterials));

    int normalizedCount = 0;
    if (isGltfFamily)
    {
        QMap<unsigned int, const SceneMesh*> materialOwners;

        for (size_t meshIdx = 0; meshIdx < meshes.size() && meshIdx < scene->mNumMeshes; ++meshIdx)
        {
            const SceneMesh* mesh = meshes[meshIdx];
            const aiMesh* sceneMesh = scene->mMeshes[meshIdx];
            if (!mesh || !sceneMesh)
                continue;

            const unsigned int materialIndex = sceneMesh->mMaterialIndex;
            if (materialIndex >= scene->mNumMaterials || materialOwners.contains(materialIndex))
                continue;

            materialOwners.insert(materialIndex, mesh);
        }

        for (auto it = materialOwners.constBegin(); it != materialOwners.constEnd(); ++it)
        {
            const unsigned int materialIndex = it.key();
            const SceneMesh* mesh = it.value();
            aiMaterial* preservedMaterial = scene->mMaterials[materialIndex];
            if (!mesh || !preservedMaterial)
                continue;

            const Material sourceMaterial = exportedDefaultMaterial(mesh);
            if (!shouldNormalizePreservedGltfMaterial(preservedMaterial, sourceMaterial))
                continue;

            aiMaterial* replacement = createMaterial(
                sourceMaterial,
                _lastTexturePackage,
                exportFileLocation,
                mesh->getName());

            if (!replacement)
            {
                logWarning(QString("  -> Failed to normalize preserved glTF material %1; keeping original")
                    .arg(materialIndex));
                continue;
            }

            delete scene->mMaterials[materialIndex];
            scene->mMaterials[materialIndex] = replacement;
            ++normalizedCount;

            logMessage(QString("  -> Normalized preserved glTF material %1 for mesh '%2'")
                .arg(materialIndex)
                .arg(mesh->getName()));
        }
    }

    logMessage(QString("  -> [APPLY-SUMMARY] Scene preserved: %1 materials, %2 meshes (no rebuild)")
        .arg(scene->mNumMaterials).arg(scene->mNumMeshes));
    if (normalizedCount > 0)
    {
        logMessage(QString("  -> [APPLY-SUMMARY] Normalized %1 preserved glTF material(s)")
            .arg(normalizedCount));
    }
}

/**
 * Load image file and return as aiTexture with embedded data
 *
 * This creates an Assimp texture object that contains the actual image data,
 * ready to be embedded in the exported file.
 */
aiTexture* AssImpMeshExporter::createEmbeddedTexture(const QString& imagePath)
{
    QFileInfo fi(imagePath);

    if (!fi.exists() || !fi.isFile())
    {
        logWarning(QString("Texture file not found: %1").arg(imagePath));
        return nullptr;
    }

    // Read the file as binary data (keep original PNG/JPEG format)
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        logWarning(QString("Failed to open texture file: %1").arg(imagePath));
        return nullptr;
    }

    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty())
    {
        logWarning(QString("Texture file is empty: %1").arg(imagePath));
        return nullptr;
    }

    // Create Assimp texture for compressed data
    aiTexture* texture = new aiTexture();

    // Store filename
    texture->mFilename = aiString(fi.fileName().toStdString());

    // For compressed data: set mHeight = 0, mWidth = file size
    texture->mHeight = 0;  // Indicates compressed format
    texture->mWidth = static_cast<unsigned int>(fileData.size());

    // Allocate and copy compressed data
    texture->pcData = reinterpret_cast<aiTexel*>(new unsigned char[fileData.size()]);
    memcpy(texture->pcData, fileData.data(), fileData.size());

    // Set format hint based on file extension
    QString ext = fi.suffix().toLower();
    if (ext == "png")
    {
        texture->achFormatHint[0] = 'p';
        texture->achFormatHint[1] = 'n';
        texture->achFormatHint[2] = 'g';
        texture->achFormatHint[3] = '\0';
    }
    else if (ext == "jpg" || ext == "jpeg")
    {
        texture->achFormatHint[0] = 'j';
        texture->achFormatHint[1] = 'p';
        texture->achFormatHint[2] = 'g';
        texture->achFormatHint[3] = '\0';
    }

    logMessage(QString("  -> Embedded texture: %1 (%2 bytes)")
        .arg(fi.fileName())
        .arg(fileData.size()));

    return texture;
}

/**
 * Embed all textures from scene materials into aiScene->mTextures
 *
 * This walks through all materials, finds referenced texture files,
 * loads them, and attaches to the scene so they get embedded in export.
 */

QStringList AssImpMeshExporter::embedTexturesInScene(aiScene* scene, const TexturePackage& texturePackage)
{
    if (!scene)
    {
        logError("Scene is null");
        return {};
    }

    logMessage("Step: Embedding textures into scene...");

    std::vector<aiTexture*> textures;
    QSet<QString> processedResolvedPaths;
    QStringList embeddedNames;

    auto addTextureIfNeeded = [&](const QString& candidatePath) -> bool {
        QFileInfo fi(candidatePath);
        if (!fi.exists() || !fi.isFile())
            return false;

        QString dedupeKey = fi.canonicalFilePath();
        if (dedupeKey.isEmpty())
            dedupeKey = fi.absoluteFilePath();

        if (processedResolvedPaths.contains(dedupeKey))
            return false;

        aiTexture* texture = createEmbeddedTexture(fi.absoluteFilePath());
        if (!texture)
            return false;

        textures.push_back(texture);
        embeddedNames.append(fi.fileName());
        processedResolvedPaths.insert(dedupeKey);
        return true;
        };

    // Define texture types + slot indices to check.
    // Multi-slot types (CLEARCOAT, SHEEN, UNKNOWN) need each slot checked individually.
    const std::pair<aiTextureType, unsigned int> texSlots[] = {
        {aiTextureType_BASE_COLOR, 0},
        {aiTextureType_NORMALS, 0},
        {aiTextureType_METALNESS, 0},
        {aiTextureType_DIFFUSE_ROUGHNESS, 0},
        {aiTextureType_LIGHTMAP, 0},
        {aiTextureType_EMISSIVE, 0},
        {aiTextureType_TRANSMISSION, 0},
        {aiTextureType_OPACITY, 0},
        {aiTextureType_HEIGHT, 0},
        {aiTextureType_CLEARCOAT, 0}, // clearcoatTexture
        {aiTextureType_CLEARCOAT, 1}, // clearcoatRoughnessTexture
        {aiTextureType_CLEARCOAT, 2}, // clearcoatNormalTexture
        {aiTextureType_SHEEN, 0},     // sheenColorTexture
        {aiTextureType_SHEEN, 1},     // sheenRoughnessTexture
        {aiTextureType_UNKNOWN, 0},   // specularTexture
        {aiTextureType_UNKNOWN, 1},   // specularColorTexture
        {aiTextureType_UNKNOWN, 2},   // anisotropyTexture
        {aiTextureType_UNKNOWN, 3},   // thicknessTexture
        {aiTextureType_SPECULAR, 0},  // specularGlossinessTexture
        {aiTextureType_DIFFUSE, 0},   // diffuseTexture
        {aiTextureType_UNKNOWN, 4},   // iridescenceTexture
        {aiTextureType_UNKNOWN, 5},   // iridescenceThicknessTexture
        {aiTextureType_UNKNOWN, 6},   // diffuseTransmissionTexture
        {aiTextureType_UNKNOWN, 7},   // diffuseTransmissionColorTexture
    };

    // Pass 1: gather textures referenced by materials
    for (unsigned int matIdx = 0; matIdx < scene->mNumMaterials; ++matIdx)
    {
        aiMaterial* mat = scene->mMaterials[matIdx];
        if (!mat)
            continue;

        for (const auto& [texType, slotIdx] : texSlots)
        {
            aiString texPath;
            if (mat->GetTexture(texType, slotIdx, &texPath) != aiReturn_SUCCESS)
                continue;

            QString path = QString::fromLocal8Bit(texPath.C_Str());

            // Prefer packaged/copied path if available
            QString resolvedPath = path;
            auto it = texturePackage.pathMapping.find(path);
            if (it == texturePackage.pathMapping.end())
                it = texturePackage.pathMapping.find(GltfPostProcessor::normalisedGlbPath(path));

            if (it != texturePackage.pathMapping.end() && !it.value().isEmpty())
            {
                QFileInfo mappedFi(it.value());
                if (mappedFi.exists())
                {
                    resolvedPath = mappedFi.absoluteFilePath();
                }
                else
                {
                    QString byName = QDir(texturePackage.textureDirectory).filePath(mappedFi.fileName());
                    if (QFileInfo(byName).exists())
                        resolvedPath = byName;
                }
            }
            else
            {
                QFileInfo directFi(path);
                QString candidate = QDir(texturePackage.textureDirectory).filePath(directFi.fileName());
                if (QFileInfo(candidate).exists())
                    resolvedPath = candidate;
            }

            addTextureIfNeeded(resolvedPath);
        }
    }

    // Pass 2: ensure ALL packaged textures are embedded too (including extension-only textures)
    for (auto it = texturePackage.pathMapping.constBegin();
        it != texturePackage.pathMapping.constEnd(); ++it)
    {
        QString resolvedPath = it.value();

        QFileInfo mappedFi(resolvedPath);
        if (!mappedFi.exists())
        {
            QString candidate = QDir(texturePackage.textureDirectory).filePath(mappedFi.fileName());
            if (QFileInfo(candidate).exists())
            {
                resolvedPath = candidate;
            }
            else if (QFileInfo(it.key()).exists())
            {
                resolvedPath = QFileInfo(it.key()).absoluteFilePath();
            }
        }

        addTextureIfNeeded(resolvedPath);
    }

    // Final attach: write ONCE, with the full combined texture set
    if (!textures.empty())
    {
        scene->mNumTextures = static_cast<unsigned int>(textures.size());
        scene->mTextures = new aiTexture * [scene->mNumTextures];
        std::copy(textures.begin(), textures.end(), scene->mTextures);

        logMessage(QString(" -> Embedded %1 textures in scene").arg(textures.size()));
    }
    else
    {
        scene->mNumTextures = 0;
        scene->mTextures = nullptr;
        logMessage(" -> Embedded 0 textures in scene");
    }

    return embeddedNames;
}


QMap<QString, QString> AssImpMeshExporter::extractEmbeddedTextures(
    const aiScene* scene,
    const QString& outputDirectory,
    const QString& textureSubfolder)
{
    QMap<QString, QString> textureMapping;  // glb://image_N -> <subfolder>/image_N.ext

    if (!scene || scene->mNumTextures == 0)
        return textureMapping;

    QString texDir = outputDirectory + "/" + textureSubfolder;
    QDir().mkpath(texDir);

    logMessage(QString("Extracting %1 embedded texture(s) from GLB...").arg(scene->mNumTextures));

    for (unsigned int i = 0; i < scene->mNumTextures; ++i)
    {
        aiTexture* tex = scene->mTextures[i];
        if (!tex) continue;

        // Get format
        QString format = QString::fromLocal8Bit(tex->achFormatHint);
        if (format.isEmpty()) format = "png";

        // Write with image_N naming to match MaterialProcessor's glb://image_N URIs
        QString filename = QString("image_%1.%2").arg(i).arg(format);
        QString outputPath = texDir + "/" + filename;

        // Check if already exists
        if (QFile::exists(outputPath))
        {
            logMessage(QString("  -> Skipping (already exists): %1").arg(filename));
            // Still add to mapping even if file exists
            QString glbUri = QString("glb://image_%1").arg(i);
            QString relativePath = QString("%1/%2").arg(textureSubfolder).arg(filename);
            textureMapping[glbUri] = relativePath;
            continue;
        }

        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly))
        {
            logWarning(QString("Failed to create texture file: %1").arg(outputPath));
            continue;
        }

        bool success = false;

        if (tex->mHeight == 0)
        {
            // Compressed
            file.write(reinterpret_cast<const char*>(tex->pcData), tex->mWidth);
            file.close();
            success = true;
            logMessage(QString("  -> Extracted: %1 (%2 bytes)").arg(filename).arg(tex->mWidth));
        }
        else
        {
            // Uncompressed RGBA
            file.close();
            QImage img(
                reinterpret_cast<const uchar*>(tex->pcData),
                tex->mWidth,
                tex->mHeight,
                tex->mWidth * 4,
                QImage::Format_RGBA8888
            );

            if (img.save(outputPath, "PNG"))
            {
                success = true;
                logMessage(QString("  -> Extracted: %1 (%2x%3 RGBA)").arg(filename).arg(tex->mWidth).arg(tex->mHeight));
            }
            else
            {
                logWarning(QString("Failed to save: %1").arg(filename));
                QFile::remove(outputPath);
            }
        }

        // Add to mapping if successful
        if (success)
        {
            QString glbUri = QString("glb://image_%1").arg(i);
            QString relativePath = QString("%1/%2").arg(textureSubfolder).arg(filename);
            textureMapping[glbUri] = relativePath;

            logMessage(QString("  -> Mapped: %1 -> %2").arg(glbUri).arg(relativePath));
        }
    }

    logMessage(QString("Extraction complete: %1 textures, %2 mappings")
        .arg(scene->mNumTextures).arg(textureMapping.size()));

    return textureMapping;
}

aiScene* AssImpMeshExporter::createScene(
    const std::vector<aiMesh*>& meshes,
    const std::vector<aiMaterial*>& materials,
    const std::vector<QMatrix4x4>& transforms)
{
    aiScene* scene = new aiScene();

    // Setup mesh array
    scene->mNumMeshes = static_cast<unsigned int>(meshes.size());
    scene->mMeshes = new aiMesh * [scene->mNumMeshes];
    std::copy(meshes.begin(), meshes.end(), scene->mMeshes);

    // Setup material array
    scene->mNumMaterials = static_cast<unsigned int>(materials.size());
    scene->mMaterials = new aiMaterial * [scene->mNumMaterials];
    std::copy(materials.begin(), materials.end(), scene->mMaterials);

    // Create root node
    scene->mRootNode = new aiNode();
    scene->mRootNode->mName = aiString("RootNode");
    scene->mRootNode->mTransformation = aiMatrix4x4();

    // Create child nodes (one per mesh) with transforms
    scene->mRootNode->mNumChildren = static_cast<unsigned int>(meshes.size());
    scene->mRootNode->mChildren = new aiNode * [scene->mRootNode->mNumChildren];

    for (unsigned int i = 0; i < meshes.size(); ++i)
    {
        aiNode* childNode = new aiNode();

        // Determine node name
        std::string nodeName = "Mesh_" + std::to_string(i);
        if (meshes[i]->mName.length > 0)
        {
            nodeName = std::string(meshes[i]->mName.C_Str());
        }
        childNode->mName = aiString(nodeName);

        // Set parent relationship
        childNode->mParent = scene->mRootNode;

        // Assign mesh
        childNode->mNumMeshes = 1;
        childNode->mMeshes = new unsigned int[1];
        childNode->mMeshes[0] = i;

        // ===== APPLY TRANSFORMATION (NEW) =====
        if (i < transforms.size())
        {
            const auto& qmat = transforms[i];

            // Convert QMatrix4x4 to aiMatrix4x4
            // QMatrix4x4 is in column-major order
            childNode->mTransformation = aiMatrix4x4(
                qmat(0, 0), qmat(0, 1), qmat(0, 2), qmat(0, 3),
                qmat(1, 0), qmat(1, 1), qmat(1, 2), qmat(1, 3),
                qmat(2, 0), qmat(2, 1), qmat(2, 2), qmat(2, 3),
                qmat(3, 0), qmat(3, 1), qmat(3, 2), qmat(3, 3)
            );

            logMessage(QString("  -> Transform applied to: %1")
                .arg(QString::fromStdString(nodeName)));
        }
        else
        {
            childNode->mTransformation = aiMatrix4x4();
        }

        scene->mRootNode->mChildren[i] = childNode;
    }

    // Root node has no direct mesh assignments
    scene->mRootNode->mNumMeshes = 0;
    scene->mRootNode->mMeshes = nullptr;

    return scene;
}

QString AssImpMeshExporter::packMetallicRoughnessIfSeparate(
    const Material& material,
    const TexturePackage& texturePackage,
    const QString& outputDirectory)
{
    // Get metallic and roughness texture paths
    const auto& metallicTex = material.texture(Material::TextureType::Metallic);
    const auto& roughnessTex = material.texture(Material::TextureType::Roughness);

    QString metallicPath = QString::fromStdString(metallicTex.path);
    QString roughnessPath = QString::fromStdString(roughnessTex.path);

    // If both are empty or identical, no packing needed
    if (metallicPath.isEmpty() && roughnessPath.isEmpty())
        return QString();  // No M/R texture
    if (!metallicPath.isEmpty() && !roughnessPath.isEmpty() && metallicPath == roughnessPath)
        return QString();  // Already the same texture

    // If only one exists, use it as-is (not an error, just incomplete material)
    if (metallicPath.isEmpty() || roughnessPath.isEmpty())
        return QString();

    // Check cache first
    QString cacheKey = metallicPath + "|" + roughnessPath;
    if (_packedTextureCache.contains(cacheKey))
    {
        logMessage(QString("  -> Using cached packed texture for M/R pair"));
        return _packedTextureCache[cacheKey];
    }

    // Get roughness invert flag from material's packing metadata (defaults to true for smoothness conversion)
    Material::ChannelPacking roughnessPacking = material.packingFor("Roughness");
    bool invertRoughness = roughnessPacking.invert;  // Read from material.json packing settings

    logMessage(QString("  -> Packing separate M/R textures: %1 + %2 (invertRoughness=%3)")
        .arg(QFileInfo(metallicPath).fileName())
        .arg(QFileInfo(roughnessPath).fileName())
        .arg(invertRoughness ? "true" : "false"));

    // Pack the textures
    QString errorMsg;
    QImage packedImage = TexturePackingUtils::packMetallicRoughness(metallicPath, roughnessPath, errorMsg, invertRoughness);
    if (packedImage.isNull())
    {
        logWarning(QString("  -> Failed to pack M/R textures: %1").arg(errorMsg));
        logMessage(QString("  -> Falling back to metallic texture: %1")
            .arg(QFileInfo(metallicPath).fileName()));
        return QString();  // Fallback: use existing single texture
    }

    // Save packed image to texture subfolder with improved naming
    // Convert "bronze_metallic_1024x1024.png" -> "bronze_metallic_packed_mr_1024x1024.png"
    QString baseName = QFileInfo(metallicPath).baseName();
    QString packedFileName;

    // Try to extract dimension suffix (e.g., _1024x1024)
    QRegularExpression dimRegex("(_\\d+x\\d+)$");
    QRegularExpressionMatch match = dimRegex.match(baseName);

    if (match.hasMatch())
    {
        QString dimension = match.captured(1);
        QString nameWithoutDim = baseName.left(baseName.length() - dimension.length());
        packedFileName = nameWithoutDim + "_packed_mr" + dimension + ".png";
    }
    else
    {
        // Fallback if dimension suffix not found
        packedFileName = baseName + "_packed_mr.png";
    }

    QString packedFilePath = QDir(texturePackage.textureDirectory).filePath(packedFileName);

    if (!packedImage.save(packedFilePath))
    {
        logWarning(QString("  -> Failed to save packed texture to: %1").arg(packedFilePath));
        return QString();
    }

    logMessage(QString("  -> Saved packed M/R texture: %1").arg(packedFileName));

    // Return relative path with texture subfolder prefix for glTF references
    QString relativePackedPath = texturePackage.textureSubfolder + "/" + packedFileName;

    // Cache the result
    _packedTextureCache[cacheKey] = relativePackedPath;

    return relativePackedPath;
}

QString AssImpMeshExporter::packORMIfSeparate(
    const Material& material,
    const TexturePackage& texturePackage,
    const QString& outputDirectory)
{
    // Get AO, metallic, and roughness texture paths
    const auto& aoTex = material.texture(Material::TextureType::AmbientOcclusion);
    const auto& metallicTex = material.texture(Material::TextureType::Metallic);
    const auto& roughnessTex = material.texture(Material::TextureType::Roughness);

    QString aoPath = QString::fromStdString(aoTex.path);
    QString metallicPath = QString::fromStdString(metallicTex.path);
    QString roughnessPath = QString::fromStdString(roughnessTex.path);
    QString effectiveAoPath = aoPath;

    // For glTF, we need ROUGHNESS to pack into metallicRoughnessTexture
    // Metallic and Occlusion are optional - we create dummy defaults if needed
    if (roughnessPath.isEmpty())
    {
        // No roughness texture - can't create metallicRoughnessTexture
        // (metallic-only or occlusion-only would be handled separately)
        logMessage(QString("  -> No roughness texture: skipping M/R packing"));
        return QString();
    }

    // Check if we have metallic - if not, we'll create a dummy white texture
    bool isRoughnessOnly = metallicPath.isEmpty();
    QString workingMetallicPath = metallicPath;
    const auto& mrReferenceTex = !metallicTex.path.empty() ? metallicTex : roughnessTex;

    if (!aoPath.isEmpty() && !textureBindingCompatibleForSharedExport(aoTex, mrReferenceTex))
    {
        logMessage(QString("  -> AO binding differs from metallic/roughness binding; keep AO separate")
            + QString(" (ao texCoord=%1, mr texCoord=%2)")
                  .arg(aoTex.texCoordIndex)
                  .arg(mrReferenceTex.texCoordIndex));
        effectiveAoPath.clear();
    }

    // If metallic and roughness are the same and no AO, no packing needed
    if (!isRoughnessOnly && effectiveAoPath.isEmpty() && metallicPath == roughnessPath)
        return QString();

    // Check cache first
    QString cacheKey = effectiveAoPath + "|" + metallicPath + "|" + roughnessPath;
    if (_packedTextureCache.contains(cacheKey))
    {
        logMessage(QString("  -> Using cached packed ORM texture"));
        return _packedTextureCache[cacheKey];
    }

    // Get roughness invert flag from material's packing metadata (defaults to true for smoothness conversion)
    Material::ChannelPacking roughnessPacking = material.packingFor("Roughness");
    bool invertRoughness = roughnessPacking.invert;  // Read from material.json packing settings

    logMessage(QString("  -> Packing ORM textures: AO=%1, M=%2, R=%3 (invertRoughness=%4)")
        .arg(effectiveAoPath.isEmpty() ? "none" : QFileInfo(effectiveAoPath).fileName())
        .arg(isRoughnessOnly ? "(dummy black)" : QFileInfo(metallicPath).fileName())
        .arg(QFileInfo(roughnessPath).fileName())
        .arg(invertRoughness ? "true" : "false"));

    // Pack the three textures using packORM
    // For roughness-only, create a dummy black metallic texture
    QString errorMsg;
    QImage packedImage;

    if (isRoughnessOnly)
    {
        // Create a temporary black image for metallic (1x1 black pixel = no metallic data)
        // Black (0) is semantically correct for "no metallic" - when multiplied by metallicFactor=0, result is 0
        QImage dummyMetallic(1, 1, QImage::Format_RGB888);
        dummyMetallic.fill(QColor(0, 0, 0));  // Black = no metallic data

        // Save dummy to temp file
        QString tempMetallicPath = QDir(texturePackage.textureDirectory).filePath("__temp_metallic.png");
        if (!dummyMetallic.save(tempMetallicPath))
        {
            logWarning(QString("  -> Failed to create dummy metallic texture"));
            return QString();
        }
        workingMetallicPath = tempMetallicPath;
        packedImage = TexturePackingUtils::packORM(effectiveAoPath, roughnessPath, workingMetallicPath, errorMsg, invertRoughness);
        // Clean up temp file
        QFile::remove(tempMetallicPath);
    }
    else
    {
        packedImage = TexturePackingUtils::packORM(effectiveAoPath, roughnessPath, metallicPath, errorMsg, invertRoughness);
    }
    if (packedImage.isNull())
    {
        logWarning(QString("  -> Failed to pack ORM textures: %1").arg(errorMsg));
        return QString();  // Packing failed
    }

    // Save packed image to texture subfolder with improved naming
    // Convert "bronze_metallic_1024x1024.png" -> "bronze_metallic_packed_orm_1024x1024.png"
    QString baseName = QFileInfo(metallicPath).baseName();
    QString packedFileName;

    // Try to extract dimension suffix (e.g., _1024x1024)
    QRegularExpression dimRegex("(_\\d+x\\d+)$");
    QRegularExpressionMatch match = dimRegex.match(baseName);

    if (match.hasMatch())
    {
        QString dimension = match.captured(1);
        QString nameWithoutDim = baseName.left(baseName.length() - dimension.length());
        packedFileName = nameWithoutDim + "_packed_orm" + dimension + ".png";
    }
    else
    {
        // Fallback if dimension suffix not found
        packedFileName = baseName + "_packed_orm.png";
    }

    QString packedFilePath = QDir(texturePackage.textureDirectory).filePath(packedFileName);

    if (!packedImage.save(packedFilePath))
    {
        logWarning(QString("  -> Failed to save packed ORM texture to: %1").arg(packedFilePath));
        return QString();
    }

    logMessage(QString("  -> Saved packed ORM texture: %1").arg(packedFileName));

    // Return relative path with texture subfolder prefix for glTF references
    QString relativePackedPath = texturePackage.textureSubfolder + "/" + packedFileName;

    // Cache the result
    _packedTextureCache[cacheKey] = relativePackedPath;

    return relativePackedPath;
}

