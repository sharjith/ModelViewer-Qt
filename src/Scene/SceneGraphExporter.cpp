#include "SceneGraphExporter.h"

#include "SceneGraph.h"
#include "SceneNode.h"
#include "RenderableMesh.h"
#include "SceneMesh.h"
#include "PunctualLights.h"

#include "GltfAnimationData.h"
#include "GltfCameraData.h"

#include <assimp/anim.h>
#include <assimp/camera.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/light.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <QDebug>
#include <QHash>
#include <QMatrix4x4>
#include <QSet>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <set>

namespace
{
    struct ExportNodeTrs
    {
        aiVector3D translation = aiVector3D(0.0f, 0.0f, 0.0f);
        aiQuaternion rotation = aiQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
        aiVector3D scale = aiVector3D(1.0f, 1.0f, 1.0f);
    };

    ExportNodeTrs decomposeNodeTrs(const aiMatrix4x4& matrix)
    {
        aiVector3D scaling;
        aiQuaternion rotation;
        aiVector3D position;
        matrix.Decompose(scaling, rotation, position);

        ExportNodeTrs trs;
        trs.translation = position;
        trs.rotation = rotation;
        trs.scale = scaling;
        return trs;
    }

    aiQuaternion normalizedQuaternion(const aiQuaternion& q)
    {
        aiQuaternion copy = q;
        copy.Normalize();
        return copy;
    }

    aiQuaternion inverseQuaternion(const aiQuaternion& q)
    {
        aiQuaternion copy = normalizedQuaternion(q);
        return copy.Conjugate();
    }

    aiVector3D rotateVectorByQuaternion(const aiQuaternion& q, const aiVector3D& v)
    {
        const aiQuaternion unit = normalizedQuaternion(q);
        const aiQuaternion vecQuat(0.0f, v.x, v.y, v.z);
        const aiQuaternion rotated = unit * vecQuat * inverseQuaternion(unit);
        return aiVector3D(rotated.x, rotated.y, rotated.z);
    }

    aiVector3D remapTranslationKey(const aiVector3D& value,
                                   const ExportNodeTrs& sourceBase,
                                   const ExportNodeTrs& exportBase)
    {
        const aiVector3D sourceDelta = value - sourceBase.translation;
        const aiQuaternion basisDelta =
            normalizedQuaternion(exportBase.rotation * inverseQuaternion(sourceBase.rotation));
        const aiVector3D rotatedDelta = rotateVectorByQuaternion(basisDelta, sourceDelta);
        return exportBase.translation + rotatedDelta;
    }

    aiQuaternion remapRotationKey(const aiQuaternion& value,
                                  const ExportNodeTrs& sourceBase,
                                  const ExportNodeTrs& exportBase)
    {
        const aiQuaternion delta = normalizedQuaternion(value * inverseQuaternion(sourceBase.rotation));
        return normalizedQuaternion(delta * exportBase.rotation);
    }

    float safeScaleRatio(float value, float base)
    {
        return std::abs(base) > 1.0e-8f ? (value / base) : value;
    }

    aiVector3D remapScaleKey(const aiVector3D& value,
                             const ExportNodeTrs& sourceBase,
                             const ExportNodeTrs& exportBase)
    {
        return aiVector3D(
            exportBase.scale.x * safeScaleRatio(value.x, sourceBase.scale.x),
            exportBase.scale.y * safeScaleRatio(value.y, sourceBase.scale.y),
            exportBase.scale.z * safeScaleRatio(value.z, sourceBase.scale.z));
    }

    void collectExportNodeBaseTrs(const aiNode* node, QMap<QString, ExportNodeTrs>& out)
    {
        if (!node)
            return;

        out.insert(QString::fromUtf8(node->mName.C_Str()), decomposeNodeTrs(node->mTransformation));
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            collectExportNodeBaseTrs(node->mChildren[i], out);
    }

    Material exportBaseMaterial(const SceneMesh* mesh)
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

    aiNode* makeIdentityNode(const QString& name)
    {
        aiNode* node = new aiNode();
        node->mName.Set(name.toUtf8().constData());
        node->mTransformation = aiMatrix4x4(); // identity
        return node;
    }

    aiTextureMapMode toAiTextureMapMode(GLenum wrapMode)
    {
        switch (wrapMode)
        {
        case GL_REPEAT:
            return aiTextureMapMode_Wrap;
        case GL_CLAMP_TO_EDGE:
            return aiTextureMapMode_Clamp;
        case GL_MIRRORED_REPEAT:
            return aiTextureMapMode_Mirror;
        default:
            return aiTextureMapMode_Wrap;
        }
    }

    constexpr unsigned int kMaxExportUvChannels = 4;

    struct ExportTextureBinding
    {
        const Material::Texture* texture = nullptr;
        aiTextureType aiType = aiTextureType_NONE;
        unsigned int slot = 0;
    };

    std::vector<ExportTextureBinding> collectExportBindings(const Material& material)
    {
        std::vector<ExportTextureBinding> bindings;
        bindings.reserve(16);

        auto add = [&](Material::TextureType type, aiTextureType aiType, unsigned int slot) {
            const Material::Texture& tex = material.texture(type);
            bindings.push_back({ &tex, aiType, slot });
            };

        // Core PBR bindings (glTF/GLB – post-processor reads these)
        add(Material::TextureType::Albedo,          aiTextureType_BASE_COLOR,       0);
        add(Material::TextureType::Metallic,        aiTextureType_METALNESS,        0);
        add(Material::TextureType::Roughness,       aiTextureType_DIFFUSE_ROUGHNESS,0);
        add(Material::TextureType::Normal,          aiTextureType_NORMALS,          0);
        add(Material::TextureType::AmbientOcclusion,aiTextureType_LIGHTMAP,         0);
        add(Material::TextureType::Emissive,        aiTextureType_EMISSIVE,         0);
        add(Material::TextureType::Opacity,         aiTextureType_OPACITY,          0);

        // Legacy Phong slots for OBJ/FBX/COLLADA exporters.
        // These exporters only read the classic aiTextureType_DIFFUSE (map_Kd),
        // aiTextureType_HEIGHT (OBJ map_bump, preferred over NORMALS), etc.
        // glTF/GLB ignores these; the GltfPostProcessor overwrites from Material anyway.
        add(Material::TextureType::Albedo, aiTextureType_DIFFUSE, 0);  // OBJ map_Kd / FBX DiffuseColor / DAE diffuse
        add(Material::TextureType::Normal, aiTextureType_HEIGHT,  0);  // OBJ map_bump (HEIGHT checked before NORMALS)

        // Extensions / advanced PBR
        add(Material::TextureType::Transmission, aiTextureType_TRANSMISSION, 0);
        add(Material::TextureType::ClearcoatColor, aiTextureType_CLEARCOAT, 0);
        add(Material::TextureType::ClearcoatRoughness, aiTextureType_CLEARCOAT, 1);
        add(Material::TextureType::ClearcoatNormal, aiTextureType_CLEARCOAT, 2);
        add(Material::TextureType::SheenColor, aiTextureType_SHEEN, 0);
        add(Material::TextureType::SheenRoughness, aiTextureType_SHEEN, 1);

        return bindings;
    }

    unsigned int clampUvChannelIndex(int texCoordIndex)
    {
        if (texCoordIndex < 0)
            return 0;
        if (texCoordIndex >= static_cast<int>(kMaxExportUvChannels))
            return 0;
        return static_cast<unsigned int>(texCoordIndex);
    }

    unsigned int maxReferencedUvChannel(const Material& material)
    {
        const auto bindings = collectExportBindings(material);

        unsigned int maxChannel = 0;
        bool anyTexturedBinding = false;

        for (const auto& binding : bindings)
        {
            if (!binding.texture || binding.texture->path.empty())
                continue;

            anyTexturedBinding = true;
            maxChannel = std::max(maxChannel, clampUvChannelIndex(binding.texture->texCoordIndex));
        }

        // Always keep UV0 available if any texturing exists at all.
        return anyTexturedBinding ? maxChannel : 0;
    }

    unsigned int maxReferencedUvChannelForMesh(const SceneMesh* mesh)
    {
        if (!mesh)
            return 0;

        unsigned int maxChannel = maxReferencedUvChannel(exportBaseMaterial(mesh));
        if (mesh->hasVariants())
        {
            const QMap<int, Material>& materials = mesh->allVariantMaterials();
            for (auto it = materials.constBegin(); it != materials.constEnd(); ++it)
                maxChannel = std::max(maxChannel, maxReferencedUvChannel(it.value()));
        }

        return maxChannel;
    }
    
    void addTextureToMaterial(
        aiMaterial* aiMat,
        const Material::Texture& tex,
        aiTextureType aiType,
        unsigned int slot)
    {
        if (!aiMat || tex.path.empty())
            return;

        aiString path(tex.path.c_str());
        aiMat->AddProperty(&path, AI_MATKEY_TEXTURE(aiType, slot));

        int uvIndex = static_cast<int>(clampUvChannelIndex(tex.texCoordIndex));
        aiMat->AddProperty(&uvIndex, 1, AI_MATKEY_UVWSRC(aiType, slot));

        aiTextureMapMode mapModeU = toAiTextureMapMode(tex.wrapS);
        aiTextureMapMode mapModeV = toAiTextureMapMode(tex.wrapT);
        aiMat->AddProperty(&mapModeU, 1, AI_MATKEY_MAPPINGMODE_U(aiType, slot));
        aiMat->AddProperty(&mapModeV, 1, AI_MATKEY_MAPPINGMODE_V(aiType, slot));

        aiUVTransform uvTransform;
        uvTransform.mTranslation = aiVector2D(tex.offset.x, tex.offset.y);
        uvTransform.mScaling = aiVector2D(tex.scale.x, tex.scale.y);
        uvTransform.mRotation = tex.rotation;
        aiMat->AddProperty(&uvTransform, 1, AI_MATKEY_UVTRANSFORM(aiType, slot));
    }

    aiMaterial* makeDefaultMaterial()
    {
        aiMaterial* mat = new aiMaterial();

        aiString matName("DefaultMaterial");
        mat->AddProperty(&matName, AI_MATKEY_NAME);

        // Optional simple neutral diffuse
        aiColor3D diffuse(0.8f, 0.8f, 0.8f);
        mat->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);

        aiColor3D ambient(0.2f, 0.2f, 0.2f);
        mat->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);

        aiColor3D specular(0.2f, 0.2f, 0.2f);
        mat->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);

        return mat;
    }

    QString exportedNodeName(const SceneNode* node)
    {
        if (!node)
            return QString();

        // Only synthetic file-wrapper nodes should lose the file extension.
        if (node->isSynthetic)
        {
            // Prefer sourceFile if available (more reliable than display name).
            if (!node->sourceFile.trimmed().isEmpty())
            {
                const QFileInfo fi(node->sourceFile);
                const QString stem = fi.completeBaseName().trimmed();
                if (!stem.isEmpty())
                    return stem;
            }

            // Fallback to the display name.
            const QFileInfo fi(node->name);
            const QString stem = fi.completeBaseName().trimmed();
            if (!stem.isEmpty())
                return stem;
        }

        // Default: preserve original node name exactly.
        return node->name;
    }

    QString makeUniqueExportNodeName(const QString& baseName, QSet<QString>& usedNames)
    {
        QString candidate = baseName.trimmed();
        if (candidate.isEmpty())
            candidate = QStringLiteral("Node");

        if (!usedNames.contains(candidate))
        {
            usedNames.insert(candidate);
            return candidate;
        }

        for (int suffix = 2;; ++suffix)
        {
            const QString uniqueCandidate = QStringLiteral("%1__%2").arg(candidate).arg(suffix);
            if (!usedNames.contains(uniqueCandidate))
            {
                usedNames.insert(uniqueCandidate);
                return uniqueCandidate;
            }
        }
    }

    SceneNode* findSceneNodeByAiChildPath(SceneNode* aiRootNode, const QVector<int>& aiChildPath)
    {
        SceneNode* node = aiRootNode;
        for (int childIndex : aiChildPath)
        {
            if (!node || childIndex < 0 || childIndex >= node->children.size())
                return nullptr;
            node = node->children[childIndex];
        }
        return node;
    }

    QString textureKeyPart(const Material::Texture& tex, const QString& label)
    {
        if (tex.path.empty())
            return label + "=<none>";

        return QString("%1=%2|uv=%3|scale=%4,%5|offset=%6,%7|rot=%8")
            .arg(label)
            .arg(QString::fromStdString(tex.path))
            .arg(tex.texCoordIndex)
            .arg(tex.scale.x, 0, 'f', 6)
            .arg(tex.scale.y, 0, 'f', 6)
            .arg(tex.offset.x, 0, 'f', 6)
            .arg(tex.offset.y, 0, 'f', 6)
            .arg(tex.rotation, 0, 'f', 6);
    }

    QString buildMaterialReuseKey(const Material& material)
    {
        QStringList parts;
        parts << QString("name=%1").arg(material.name().trimmed());
        parts << QString("albedo=%1,%2,%3")
            .arg(material.albedoColor().x(), 0, 'f', 6)
            .arg(material.albedoColor().y(), 0, 'f', 6)
            .arg(material.albedoColor().z(), 0, 'f', 6);
        parts << QString("metal=%1").arg(material.metalness(), 0, 'f', 6);
        parts << QString("rough=%1").arg(material.roughness(), 0, 'f', 6);
        parts << QString("opacity=%1").arg(material.opacity(), 0, 'f', 6);
        parts << QString("blend=%1").arg(static_cast<int>(material.blendMode()));
        parts << QString("twoSided=%1").arg(material.twoSided() ? 1 : 0);
        parts << QString("alphaCutoff=%1").arg(material.alphaThreshold(), 0, 'f', 6);
        parts << QString("ior=%1").arg(material.ior(), 0, 'f', 6);
        parts << QString("transmission=%1").arg(material.transmission(), 0, 'f', 6);
        parts << QString("thickness=%1").arg(material.thicknessFactor(), 0, 'f', 6);
        parts << QString("clearcoat=%1").arg(material.clearcoat(), 0, 'f', 6);
        parts << QString("clearcoatRough=%1").arg(material.clearcoatRoughness(), 0, 'f', 6);
        parts << QString("sheenRough=%1").arg(material.sheenRoughness(), 0, 'f', 6);

        parts << textureKeyPart(material.texture(Material::TextureType::Albedo), "albedoTex");
        parts << textureKeyPart(material.texture(Material::TextureType::Metallic), "metalTex");
        parts << textureKeyPart(material.texture(Material::TextureType::Roughness), "roughTex");
        parts << textureKeyPart(material.texture(Material::TextureType::Normal), "normalTex");
        parts << textureKeyPart(material.texture(Material::TextureType::AmbientOcclusion), "aoTex");
        parts << textureKeyPart(material.texture(Material::TextureType::Emissive), "emissiveTex");
        parts << textureKeyPart(material.texture(Material::TextureType::Opacity), "opacityTex");
        parts << textureKeyPart(material.texture(Material::TextureType::Transmission), "transmissionTex");
        parts << textureKeyPart(material.texture(Material::TextureType::ClearcoatColor), "clearcoatTex");
        parts << textureKeyPart(material.texture(Material::TextureType::ClearcoatRoughness), "clearcoatRoughTex");
        parts << textureKeyPart(material.texture(Material::TextureType::ClearcoatNormal), "clearcoatNormalTex");
        parts << textureKeyPart(material.texture(Material::TextureType::SheenColor), "sheenColorTex");
        parts << textureKeyPart(material.texture(Material::TextureType::SheenRoughness), "sheenRoughTex");

        return parts.join("||");
    }
}

aiScene* SceneGraphExporter::buildExportScene(
    const SceneGraph* sceneGraph,
    const MeshResolver& resolveMesh,
    bool flattenTransforms,
    const QStringList& allowedSourceFiles,
    QMap<QString, unsigned int>* outAnimMatRemap)
{
    if (!sceneGraph)
        return nullptr;

    SceneNode* graphRoot = sceneGraph->root();
    if (!graphRoot)
        return nullptr;

    aiScene* scene = new aiScene();

    std::vector<aiMesh*> builtMeshes;
    builtMeshes.reserve(64);

    std::vector<aiMaterial*> builtMaterials;
    builtMaterials.reserve(64);
    QHash<QUuid, QString> exportedNodeNameByUuid;
    QHash<QUuid, QString> exportedMeshNodeNameByUuid;
    QSet<QString> usedExportNodeNames;


    // Export root (invisible SceneGraph root becomes a real aiNode container).
    aiNode* exportRoot = makeIdentityNode(QStringLiteral("SceneRoot"));
    scene->mRootNode = exportRoot;

    // Rebuild top-level children from SceneGraph::_root->children.
    // Each direct child is a synthetic fileNode whose importCorrection records the
    // autoOrient+autoScale transform baked into the Assimp root by the loader.
    // We factor that out by temporarily neutralising it from the Assimp root
    // SceneNode's localTransform before the recursive build, then restoring it.
    // This works for both flat formats (worldTransform accumulation) and hierarchy
    // formats (dstNode->mTransformation assignment) with no special-casing.
    //
    // allowedSourceFiles: when non-empty, only file nodes whose sourceFile is in
    // the list are exported.  Empty means "export all".
    const bool filterFiles = !allowedSourceFiles.isEmpty();

    // Collect file nodes that pass the filter.
    QList<SceneNode*> exportFileNodes;
    for (SceneNode* fileNode : graphRoot->children)
    {
        if (!filterFiles || allowedSourceFiles.contains(fileNode->sourceFile))
            exportFileNodes.append(fileNode);
    }

    exportRoot->mNumChildren = static_cast<unsigned int>(exportFileNodes.size());
    if (exportRoot->mNumChildren > 0)
    {
        QMap<QString, unsigned int> materialKeyToIndex;
        exportRoot->mChildren = new aiNode * [exportRoot->mNumChildren];
        for (unsigned int i = 0; i < exportRoot->mNumChildren; ++i)
        {
            SceneNode* fileNode = exportFileNodes.at(static_cast<int>(i));

            // --- Factor out the import correction --------------------------------
            // The correction was applied to the Assimp root node (first child of
            // the synthetic fileNode).  Temporarily pre-multiply its localTransform
            // by the correction inverse so the exported transform is correction-free.
            SceneNode* assimpRoot = (fileNode->isSynthetic && !fileNode->children.isEmpty())
                                    ? fileNode->children.first() : nullptr;
            aiMatrix4x4 savedLocalTransform;
            {
                const auto& ic = fileNode->importCorrection;
                const aiMatrix4x4& lt = assimpRoot ? assimpRoot->localTransform : aiMatrix4x4();
                qDebug() << "[Export] fileNode:" << fileNode->name
                         << "  importCorrection.IsIdentity=" << ic.IsIdentity()
                         << "  importCorrection=[" << ic.a1 << ic.a2 << ic.a3 << ic.a4
                         << " |" << ic.b1 << ic.b2 << ic.b3 << ic.b4
                         << " |" << ic.c1 << ic.c2 << ic.c3 << ic.c4 << "]"
                         << "  assimpRoot->localTransform=[" << lt.a1 << lt.a2 << lt.a3 << lt.a4
                         << " |" << lt.b1 << lt.b2 << lt.b3 << lt.b4
                         << " |" << lt.c1 << lt.c2 << lt.c3 << lt.c4 << "]";
            }
            if (assimpRoot && !fileNode->importCorrection.IsIdentity())
            {
                savedLocalTransform = assimpRoot->localTransform;
                aiMatrix4x4 corrInverse = fileNode->importCorrection;
                corrInverse.Inverse();
                assimpRoot->localTransform = corrInverse * assimpRoot->localTransform;
            }
            // ---------------------------------------------------------------------

            aiNode* dstChild = buildNodeRecursive(fileNode, resolveMesh, builtMeshes, builtMaterials,
                materialKeyToIndex, exportedNodeNameByUuid, exportedMeshNodeNameByUuid, usedExportNodeNames,
                aiMatrix4x4(), flattenTransforms, fileNode->importCorrection, outAnimMatRemap);
            exportRoot->mChildren[i] = dstChild;
            if (dstChild)
                dstChild->mParent = exportRoot;

            // Restore the Assimp root's original localTransform.
            if (assimpRoot && !fileNode->importCorrection.IsIdentity())
                assimpRoot->localTransform = savedLocalTransform;
        }
    }

    // Transfer built meshes into aiScene::mMeshes
    scene->mNumMeshes = static_cast<unsigned int>(builtMeshes.size());
    if (scene->mNumMeshes > 0)
    {
        scene->mMeshes = new aiMesh * [scene->mNumMeshes];
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            scene->mMeshes[i] = builtMeshes[i];
        }
    }

    // Transfer built materials into aiScene::mMaterials.
    if (builtMaterials.empty())
    {
        scene->mNumMaterials = 1;
        scene->mMaterials = new aiMaterial * [1];
        scene->mMaterials[0] = makeDefaultMaterial();
    }
    else
    {
        scene->mNumMaterials = static_cast<unsigned int>(builtMaterials.size());
        scene->mMaterials = new aiMaterial * [scene->mNumMaterials];
        for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
        {
            scene->mMaterials[i] = builtMaterials[i];
        }
    }

    // Transfer enabled lights from SceneGraph into aiScene::mLights (KHR_lights_punctual support)
    const std::vector<GPULight> lights = sceneGraph->buildEnabledLightList();
    if (!lights.empty())
    {
        scene->mNumLights = static_cast<unsigned int>(lights.size());
        scene->mLights = new aiLight*[scene->mNumLights];

        for (unsigned int i = 0; i < scene->mNumLights; ++i)
        {
            const GPULight& gpuLight = lights[i];
            aiLight* aiLight = new ::aiLight();

            aiLight->mName.Set((std::string("Light_") + std::to_string(i)).c_str());
            aiLight->mPosition = aiVector3D(gpuLight.position.x, gpuLight.position.y, gpuLight.position.z);
            aiLight->mDirection = aiVector3D(gpuLight.direction.x, gpuLight.direction.y, gpuLight.direction.z);
            aiLight->mColorDiffuse = aiColor3D(gpuLight.color.x, gpuLight.color.y, gpuLight.color.z);
            aiLight->mColorSpecular = aiColor3D(gpuLight.color.x, gpuLight.color.y, gpuLight.color.z);

            // Set light type
            switch (static_cast<LightType>(gpuLight.type))
            {
            case LightType::Directional:
                aiLight->mType = aiLightSource_DIRECTIONAL;
                break;
            case LightType::Point:
                aiLight->mType = aiLightSource_POINT;
                aiLight->mAttenuationConstant = 1.0f;
                aiLight->mAttenuationLinear = 0.0f;
                aiLight->mAttenuationQuadratic = 0.0f;
                if (gpuLight.range > 0.0f)
                {
                    // Set range as a custom property (KHR extension)
                    // This will be handled by GltfPostProcessor when writing KHR_lights_punctual
                }
                break;
            case LightType::Spot:
                aiLight->mType = aiLightSource_SPOT;
                aiLight->mAttenuationConstant = 1.0f;
                aiLight->mAttenuationLinear = 0.0f;
                aiLight->mAttenuationQuadratic = 0.0f;
                aiLight->mAngleOuterCone = std::acos(gpuLight.outerConeCos);
                aiLight->mAngleInnerCone = std::acos(gpuLight.innerConeCos);
                break;
            }

            scene->mLights[i] = aiLight;
        }

        qDebug() << "[EXPORT-LIGHTS] Exported" << scene->mNumLights << "punctual lights";
    }

    // ===== Export glTF Cameras =====
    // Populate aiScene::mCameras from GltfCameraData stored in SceneGraph.
    // Assimp's glTF2 exporter links each aiCamera to a node whose mName matches
    // aiCamera::mName, so we use the original node name (= camera.nodeName).
    {
        std::vector<aiCamera*> builtCameras;

        for (const SceneNode* fileNode : exportFileNodes)
        {
            const GltfCameraData camData = sceneGraph->gltfCameraDataForFile(fileNode->sourceFile);
            for (const GltfCameraEntry& cam : camData.cameras)
            {
                aiCamera* aiCam = new aiCamera();

                // Name must equal the hosting node's name so Assimp wires them up.
                aiCam->mName.Set(cam.nodeName.toStdString());

                aiCam->mClipPlaneNear = cam.zNear;
                aiCam->mClipPlaneFar  = cam.zFar;

                if (cam.type == GltfCameraType::Perspective)
                {
                    // Assimp's glTF2 exporter writes aiCamera::mHorizontalFOV as
                    // the glTF "yfov" property, so store the vertical FOV there for
                    // a lossless round-trip.  mAspect = 0 signals "unspecified".
                    aiCam->mHorizontalFOV = cam.fovYRadians;
                    aiCam->mAspect        = 0.0f;
                }
                else // Orthographic
                {
                    // glTF orthographic: xmag = mOrthographicWidth,
                    //                    ymag = xmag / mAspect
                    // Store xMag and derive aspect so the exporter recovers yMag.
                    aiCam->mOrthographicWidth = cam.xMag;
                    aiCam->mAspect = (cam.yMag > 0.0f)
                                     ? (cam.xMag / cam.yMag)
                                     : 1.0f;
                }

                // World-space orientation is already encoded in the camera's hosting
                // node transform, which is exported as part of the node hierarchy.
                // No need to duplicate position/direction here.

                builtCameras.push_back(aiCam);
            }
        }

        if (!builtCameras.empty())
        {
            scene->mNumCameras = static_cast<unsigned int>(builtCameras.size());
            scene->mCameras = new aiCamera*[scene->mNumCameras];
            for (unsigned int i = 0; i < scene->mNumCameras; ++i)
                scene->mCameras[i] = builtCameras[i];

            qDebug() << "[EXPORT-CAMERAS] Exported" << scene->mNumCameras << "camera(s)";
        }
    }

    // ===== Export Animations (TRS channels) =====
    // Convert GltfAnimationData → aiAnimation / aiNodeAnim for each exported file.
    //
    // Scope: Translation, Rotation, and Scale node-transform channels are exported
    // faithfully.  Morph-weight (Weights) and pointer-target channels (material
    // properties, node visibility) require glTF-specific JSON surgery and are
    // intentionally skipped here; they can be added via GltfPostProcessor later.
    //
    // Coordinate space: keyframe values are in the original model space (same space
    // as the exported node hierarchy after importCorrection is factored out), so no
    // additional transform is needed.
    {
        std::vector<aiAnimation*> builtAnims;
        QMap<QString, ExportNodeTrs> exportNodeBaseTrsByName;
        collectExportNodeBaseTrs(scene->mRootNode, exportNodeBaseTrsByName);

        for (const SceneNode* fileNode : exportFileNodes)
        {
            const GltfAnimationData animData = sceneGraph->animationDataForFile(fileNode->sourceFile);
            if (animData.isEmpty())
                continue;

            for (const GltfAnimationClip& clip : animData.clips)
            {
                // Group TRS channels by target node name so each node gets exactly
                // one aiNodeAnim (Assimp requires one-per-node per animation).
                QMap<QString, aiNodeAnim*>    nodeAnimByName;
                // Morph-weight channels: one aiMeshMorphAnim per target mesh name.
                QMap<QString, aiMeshMorphAnim*> morphAnimByName;

                for (const GltfAnimationChannel& ch : clip.channels)
                {
                    SceneNode* resolvedTargetNode = nullptr;
                    if (ch.targetKind == GltfAnimationBindingTargetKind::Node
                        && ch.targetNodeIndex >= 0 && ch.targetNodeIndex < animData.nodeBindings.size())
                    {
                        const GltfAnimationNodeBinding& binding = animData.nodeBindings[ch.targetNodeIndex];
                        SceneNode* fileNodeMutable = const_cast<SceneNode*>(fileNode);
                        SceneNode* aiRootNode = fileNodeMutable->children.isEmpty() ? nullptr : fileNodeMutable->children.first();
                        if (binding.hasAiChildPath)
                            resolvedTargetNode = findSceneNodeByAiChildPath(aiRootNode, binding.aiChildPath);

                        if (!resolvedTargetNode && !binding.nodeName.isEmpty())
                        {
                            std::function<SceneNode*(SceneNode*)> findByName = [&](SceneNode* node) -> SceneNode*
                            {
                                if (!node)
                                    return nullptr;
                                if (node->name == binding.nodeName)
                                    return node;
                                for (SceneNode* child : node->children)
                                {
                                    if (SceneNode* match = findByName(child))
                                        return match;
                                }
                                return nullptr;
                            };

                            for (SceneNode* child : fileNodeMutable->children)
                            {
                                resolvedTargetNode = findByName(child);
                                if (resolvedTargetNode)
                                    break;
                            }
                        }
                    }

                    const QString exportedTargetName =
                        ch.targetKind == GltfAnimationBindingTargetKind::Mesh
                            ? exportedMeshNodeNameByUuid.value(ch.targetMeshUuid)
                            : (resolvedTargetNode ? exportedNodeNameByUuid.value(resolvedTargetNode->nodeUuid)
                                                  : QString());

                    const bool isTRS = (ch.targetPath == GltfAnimationTargetPath::Translation ||
                                        ch.targetPath == GltfAnimationTargetPath::Rotation    ||
                                        ch.targetPath == GltfAnimationTargetPath::Scale);
                    const bool isWeights = (ch.targetPath == GltfAnimationTargetPath::Weights);

                    if (!isTRS && !isWeights)
                        continue; // Pointer channels handled by GltfPostProcessor

                    if (isWeights)
                    {
                        // Each weightKey carries a variable-length array of weights
                        // (one per morph target on the mesh).  Assimp represents these
                        // as aiMeshMorphKey entries keyed to the mesh name.
                        if (ch.weightKeys.isEmpty())
                            continue;

                        const QString meshName = !exportedTargetName.isEmpty() ? exportedTargetName : ch.targetNodeName;
                        if (!morphAnimByName.contains(meshName))
                        {
                            aiMeshMorphAnim* ma = new aiMeshMorphAnim();
                            ma->mName.Set(meshName.toStdString());
                            morphAnimByName[meshName] = ma;
                        }
                        aiMeshMorphAnim* ma = morphAnimByName[meshName];

                        constexpr double kTicksPerSecond = 1000.0;
                        ma->mNumKeys = static_cast<unsigned int>(ch.weightKeys.size());
                        ma->mKeys    = new aiMeshMorphKey[ma->mNumKeys];

                        for (unsigned int k = 0; k < ma->mNumKeys; ++k)
                        {
                            const GltfAnimationWeightsKey& wk = ch.weightKeys[k];
                            aiMeshMorphKey& mk = ma->mKeys[k];
                            mk.mTime = wk.timeSeconds * kTicksPerSecond;

                            const int numWeights = wk.values.size();
                            mk.mNumValuesAndWeights = static_cast<unsigned int>(numWeights);
                            mk.mValues  = new unsigned int[numWeights];
                            mk.mWeights = new double[numWeights];
                            for (int wi = 0; wi < numWeights; ++wi)
                            {
                                mk.mValues[wi]  = static_cast<unsigned int>(wi);
                                mk.mWeights[wi] = static_cast<double>(wk.values[wi]);
                            }
                        }
                        continue;
                    }

                    const QString targetNodeName = !exportedTargetName.isEmpty() ? exportedTargetName : ch.targetNodeName;
                    const std::string nodeName = targetNodeName.toStdString();

                    const ExportNodeTrs exportBaseTrs =
                        exportNodeBaseTrsByName.value(targetNodeName, ExportNodeTrs());
                    const ExportNodeTrs sourceBaseTrs =
                        ch.targetKind == GltfAnimationBindingTargetKind::Mesh
                            ? ExportNodeTrs()
                            : (resolvedTargetNode ? decomposeNodeTrs(resolvedTargetNode->localTransform)
                                                  : ExportNodeTrs());

                    if (!nodeAnimByName.contains(targetNodeName))
                    {
                        aiNodeAnim* na = new aiNodeAnim();
                        na->mNodeName.Set(nodeName);
                        nodeAnimByName[targetNodeName] = na;
                    }
                    aiNodeAnim* na = nodeAnimByName[targetNodeName];

                    // Use milliseconds as ticks (mTicksPerSecond = 1000) for
                    // sub-millisecond precision with integer-safe timestamps.
                    constexpr double kTicksPerSecond = 1000.0;

                    if (ch.targetPath == GltfAnimationTargetPath::Translation)
                    {
                        na->mNumPositionKeys = static_cast<unsigned int>(ch.vec3Keys.size());
                        na->mPositionKeys    = new aiVectorKey[na->mNumPositionKeys];
                        for (unsigned int k = 0; k < na->mNumPositionKeys; ++k)
                        {
                            const GltfAnimationVec3Key& key = ch.vec3Keys[k];
                            na->mPositionKeys[k].mTime  = key.timeSeconds * kTicksPerSecond;
                            na->mPositionKeys[k].mValue = remapTranslationKey(
                                aiVector3D(key.value.x(), key.value.y(), key.value.z()),
                                sourceBaseTrs,
                                exportBaseTrs);
                        }
                    }
                    else if (ch.targetPath == GltfAnimationTargetPath::Rotation)
                    {
                        na->mNumRotationKeys = static_cast<unsigned int>(ch.quatKeys.size());
                        na->mRotationKeys    = new aiQuatKey[na->mNumRotationKeys];
                        for (unsigned int k = 0; k < na->mNumRotationKeys; ++k)
                        {
                            const GltfAnimationQuatKey& key = ch.quatKeys[k];
                            // Assimp quaternion: (w, x, y, z)
                            na->mRotationKeys[k].mTime  = key.timeSeconds * kTicksPerSecond;
                            na->mRotationKeys[k].mValue = remapRotationKey(
                                aiQuaternion(key.value.scalar(),
                                             key.value.x(),
                                             key.value.y(),
                                             key.value.z()),
                                sourceBaseTrs,
                                exportBaseTrs);
                        }
                    }
                    else // Scale
                    {
                        na->mNumScalingKeys = static_cast<unsigned int>(ch.vec3Keys.size());
                        na->mScalingKeys    = new aiVectorKey[na->mNumScalingKeys];
                        for (unsigned int k = 0; k < na->mNumScalingKeys; ++k)
                        {
                            const GltfAnimationVec3Key& key = ch.vec3Keys[k];
                            na->mScalingKeys[k].mTime  = key.timeSeconds * kTicksPerSecond;
                            na->mScalingKeys[k].mValue = remapScaleKey(
                                aiVector3D(key.value.x(), key.value.y(), key.value.z()),
                                sourceBaseTrs,
                                exportBaseTrs);
                        }
                    }
                }

                if (nodeAnimByName.isEmpty() && morphAnimByName.isEmpty())
                    continue; // no exportable channels → skip this clip

                // Assimp's glTF2 exporter requires each aiNodeAnim to have at least
                // one keyframe for EVERY channel type (position, rotation, scale).
                // Skeletal animations commonly animate only a subset of TRS channels
                // (e.g. rotation-only joints) — if any array is left empty the
                // exporter crashes or produces invalid output.
                //
                // Safety pass: ensure every aiNodeAnim has ≥1 key of each type.
                // Use a neutral default (zero translation, identity rotation, unit scale)
                // at time 0.  The single key is never interpolated so it does not
                // distort the animation for channels that legitimately have no keys.
                constexpr double kZeroTime = 0.0;
                for (aiNodeAnim* na : nodeAnimByName)
                {
                    const QString nodeName = QString::fromUtf8(na->mNodeName.C_Str());
                    const ExportNodeTrs baseTrs = exportNodeBaseTrsByName.value(nodeName, ExportNodeTrs());

                    if (na->mNumPositionKeys == 0 || na->mPositionKeys == nullptr)
                    {
                        na->mNumPositionKeys = 1;
                        na->mPositionKeys    = new aiVectorKey[1];
                        na->mPositionKeys[0].mTime  = kZeroTime;
                        na->mPositionKeys[0].mValue = baseTrs.translation;
                    }
                    if (na->mNumRotationKeys == 0 || na->mRotationKeys == nullptr)
                    {
                        na->mNumRotationKeys = 1;
                        na->mRotationKeys    = new aiQuatKey[1];
                        na->mRotationKeys[0].mTime  = kZeroTime;
                        na->mRotationKeys[0].mValue = baseTrs.rotation;
                    }
                    if (na->mNumScalingKeys == 0 || na->mScalingKeys == nullptr)
                    {
                        na->mNumScalingKeys = 1;
                        na->mScalingKeys    = new aiVectorKey[1];
                        na->mScalingKeys[0].mTime  = kZeroTime;
                        na->mScalingKeys[0].mValue = baseTrs.scale;
                    }
                }

                aiAnimation* anim = new aiAnimation();
                anim->mName.Set(clip.name.toStdString());
                anim->mDuration       = clip.durationSeconds * 1000.0; // in ticks
                anim->mTicksPerSecond = 1000.0;

                anim->mNumChannels = static_cast<unsigned int>(nodeAnimByName.size());
                if (anim->mNumChannels > 0)
                {
                    anim->mChannels = new aiNodeAnim*[anim->mNumChannels];
                    unsigned int ci = 0;
                    for (aiNodeAnim* na : nodeAnimByName)
                        anim->mChannels[ci++] = na;
                }

                anim->mNumMorphMeshChannels = static_cast<unsigned int>(morphAnimByName.size());
                if (anim->mNumMorphMeshChannels > 0)
                {
                    anim->mMorphMeshChannels = new aiMeshMorphAnim*[anim->mNumMorphMeshChannels];
                    unsigned int mi = 0;
                    for (aiMeshMorphAnim* ma : morphAnimByName)
                        anim->mMorphMeshChannels[mi++] = ma;
                }

                builtAnims.push_back(anim);
            }
        }

        if (!builtAnims.empty())
        {
            scene->mNumAnimations = static_cast<unsigned int>(builtAnims.size());
            scene->mAnimations    = new aiAnimation*[scene->mNumAnimations];
            for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
                scene->mAnimations[i] = builtAnims[i];

            qDebug() << "[EXPORT-ANIMS] Exported" << scene->mNumAnimations << "animation clip(s)";
        }
    }

    // ===== Validate animation channel targets =====
    // Assimp's glTF2 exporter crashes (null-ptr deref) if an aiNodeAnim::mNodeName
    // does not resolve to an existing node in the exported hierarchy.  Collect all
    // exported node names and strip any channels whose target is absent so the
    // exporter receives a self-consistent aiScene.
    {
        // Collect all node names from the exported aiNode hierarchy.
        std::set<std::string> exportedNodeNames;
        std::function<void(const aiNode*)> collectNames = [&](const aiNode* node)
        {
            if (!node) return;
            exportedNodeNames.insert(node->mName.C_Str());
            for (unsigned int i = 0; i < node->mNumChildren; ++i)
                collectNames(node->mChildren[i]);
        };
        collectNames(scene->mRootNode);

        qDebug() << "[EXPORT-VALIDATE] Scene contains" << exportedNodeNames.size() << "exported node(s).";

        // For each animation, remove channels whose target node is not in the hierarchy.
        for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai)
        {
            aiAnimation* anim = scene->mAnimations[ai];
            if (!anim || anim->mNumChannels == 0)
                continue;

            std::vector<aiNodeAnim*> validChannels;
            validChannels.reserve(anim->mNumChannels);

            for (unsigned int ci = 0; ci < anim->mNumChannels; ++ci)
            {
                aiNodeAnim* na = anim->mChannels[ci];
                if (!na) continue;

                const std::string targetName = na->mNodeName.C_Str();
                if (exportedNodeNames.count(targetName) > 0)
                {
                    validChannels.push_back(na);
                }
                else
                {
                    qWarning() << "[EXPORT-VALIDATE] Animation '" << QString::fromUtf8(anim->mName.C_Str())
                               << "' channel" << ci << "target '" << QString::fromStdString(targetName)
                               << "' NOT found in exported node hierarchy — removing channel to prevent crash.";
                    // Log all known exported node names on the first missing target to aid debugging.
                    static bool dumpedNames = false;
                    if (!dumpedNames)
                    {
                        dumpedNames = true;
                        qDebug() << "[EXPORT-VALIDATE] Exported node names:";
                        for (const std::string& n : exportedNodeNames)
                            qDebug() << "  " << QString::fromStdString(n);
                    }
                    delete na;
                }
            }

            if (validChannels.size() < anim->mNumChannels)
            {
                // Replace channel array with the filtered list.
                delete[] anim->mChannels;
                anim->mNumChannels = static_cast<unsigned int>(validChannels.size());
                if (anim->mNumChannels > 0)
                {
                    anim->mChannels = new aiNodeAnim*[anim->mNumChannels];
                    for (unsigned int k = 0; k < anim->mNumChannels; ++k)
                        anim->mChannels[k] = validChannels[k];
                }
                else
                {
                    anim->mChannels = nullptr;
                }
                qDebug() << "[EXPORT-VALIDATE] Animation '" << QString::fromUtf8(anim->mName.C_Str())
                         << "' reduced to" << anim->mNumChannels << "valid channel(s).";
            }
        }
    }

    return scene;
}

aiNode* SceneGraphExporter::buildNodeRecursive(
    const SceneNode* srcNode,
    const MeshResolver& resolveMesh,
    std::vector<aiMesh*>& outMeshes,
    std::vector<aiMaterial*>& outMaterials,
    QMap<QString, unsigned int>& materialKeyToIndex,
    QHash<QUuid, QString>& exportedNodeNameByUuid,
    QHash<QUuid, QString>& exportedMeshNodeNameByUuid,
    QSet<QString>& usedExportNodeNames,
    const aiMatrix4x4& parentWorldTransform,
    bool flattenTransforms,
    const aiMatrix4x4& importCorrection,
    QMap<QString, unsigned int>* animMatRemap
)
{
    if (!srcNode)
        return nullptr;

    // Accumulated world transform for this node (same accumulation as processNode at import).
    // Used to un-bake baked-in vertex world-space positions back to local space.
    aiMatrix4x4 worldTransform = parentWorldTransform * srcNode->localTransform;

    // Assimp's glTF2 exporter uses aiMatrix4x4::IsIdentity() (epsilon ~0.01) to decide
    // whether to write a node's transform. Models like MetalRoughSpheresNoTextures encode
    // sphere positions as tiny per-node translations (0.001–0.006 units) that fall below
    // this threshold, so Assimp silently drops them. When the exporter drops the transform
    // but the un-baking code has already moved vertices to local space, all spheres stack
    // at the origin on reimport.
    //
    // Fix: for leaf mesh nodes (meshes, no children) whose localTransform would be dropped
    // by Assimp (localTransform.IsIdentity() == true with Assimp's epsilon), encode the
    // node's local offset into the vertex positions instead, and export the node as identity.
    // On reimport the parent chain re-applies correctly and positions are preserved.
    //
    // When the localTransform is large enough for Assimp to write (e.g. negative scales,
    // significant rotations/translations), the existing worldTransform un-baking is used
    // unchanged so that normal flipping and other transform-dependent effects are correct.
    // useParentSpaceEncoding: for hierarchy-aware formats, Assimp's glTF2 writer calls
    // aiMatrix4x4::IsIdentity() (epsilon ~0.01) to decide whether to write a node transform.
    // Leaf mesh nodes whose localTransform is sub-epsilon (e.g. 0.001–0.006 unit translations
    // in MetalRoughSpheresNoTextures) would have their transform silently dropped, stacking
    // all affected spheres at the parent position.  For these nodes we emit an identity node
    // transform (consistent with what Assimp would write) and accept the sub-epsilon error;
    // the flat-format path forward-bakes the full worldTransform so it is unaffected.
    const bool isLeafMeshNode = !srcNode->meshUuids.isEmpty() && srcNode->children.isEmpty();
    const bool useParentSpaceEncoding = !flattenTransforms && isLeafMeshNode && srcNode->localTransform.IsIdentity();

    const QString uniqueExportNodeName = makeUniqueExportNodeName(exportedNodeName(srcNode), usedExportNodeNames);
    exportedNodeNameByUuid.insert(srcNode->nodeUuid, uniqueExportNodeName);
    aiNode* dstNode = makeIdentityNode(uniqueExportNodeName);
    // flattenTransforms: every node is identity — Assimp OBJ/PLY/STL writer ignores them anyway.
    // useParentSpaceEncoding: node also identity — parent-offset is baked into vertex data.
    // normal: write the node's local transform so the hierarchy writer can reconstruct world space.
    dstNode->mTransformation = (flattenTransforms || useParentSpaceEncoding)
                               ? aiMatrix4x4()
                               : srcNode->localTransform;

    // Build meshes owned directly by this node.
    //
    // Important for STEP/XCAF-derived scenes:
    // exporting several source meshes directly on one aiNode encourages Assimp's
    // glTF writer to collapse them into a single JSON mesh with multiple
    // primitives. That later forces GltfPostProcessor to recover primitive ->
    // source-mesh identity heuristically, which is where color/material
    // remapping starts to drift for CAD imports.
    //
    // To preserve one source mesh -> one explicit export identity, we wrap each
    // mesh in its own identity child node whenever a source node owns multiple
    // meshes. The parent node still carries the original local transform, so the
    // world-space result is unchanged while the exported structure becomes much
    // closer to a typical glTF scene graph.
    const bool splitMeshesIntoChildNodes = srcNode->meshUuids.size() > 1;

    std::vector<unsigned int> nodeMeshIndices;
    nodeMeshIndices.reserve(splitMeshesIntoChildNodes ? 0 : static_cast<size_t>(srcNode->meshUuids.size()));
    std::vector<aiNode*> meshChildNodes;
    meshChildNodes.reserve(splitMeshesIntoChildNodes ? static_cast<size_t>(srcNode->meshUuids.size()) : 0);

    for (const QUuid& meshUuid : srcNode->meshUuids)
    {
        SceneMesh* triMesh = resolveMesh ? resolveMesh(meshUuid) : nullptr;
        if (!triMesh)
            continue;

        const Material glMaterial = exportBaseMaterial(triMesh);
        unsigned int materialIndex = 0;

        // Use originalMaterialIndex as authoritative mapping to preserve material structure from import.
        // All meshes that referenced the same original material should use the same material in export.
        int originalMatIndex = triMesh->getOriginalMaterialIndex();

        if (originalMatIndex >= 0)
        {
            // Mesh came from Assimp import.
            // Include the source file in the key so that materials from different
            // files that happen to share the same integer index are kept distinct.
            // Without the file qualifier, file-A material 0 and file-B material 0
            // would collapse into a single exported material, scrambling multi-file
            // scenes that use KHR_materials_variants or simply different material sets.
            //
            // Also include a content hash of the live Material so that if the user
            // has modified one mesh's material (e.g. applied a different texture or
            // changed a colour), that mesh gets its own export entry rather than
            // inheriting whichever mesh with the same originalMaterialIndex was
            // visited first.  Unmodified meshes sharing the same original index and
            // identical material content still deduplicate correctly.
            const QString sourceFile = triMesh->getSourceFile();
            const QString contentKey = buildMaterialReuseKey(glMaterial);
            QString originalMatKey = QString("originalMat=%1@%2::%3")
                                     .arg(originalMatIndex).arg(sourceFile).arg(contentKey);
            auto matIt = materialKeyToIndex.find(originalMatKey);

            if (matIt != materialKeyToIndex.end())
            {
                // Already created a material for this original index - reuse it
                materialIndex = matIt.value();
            }
            else
            {
                // First time seeing this original material - create it
                aiMaterial* builtMaterial = buildMaterialFromSceneMesh(triMesh);
                if (!builtMaterial)
                    builtMaterial = makeDefaultMaterial();

                materialIndex = static_cast<unsigned int>(outMaterials.size());
                outMaterials.push_back(builtMaterial);
                materialKeyToIndex.insert(originalMatKey, materialIndex); // key already includes sourceFile
            }

            // Track original→export material index mapping for pointer-animation remapping.
            // Key format: "originalMatIdx@sourceFile"
            if (animMatRemap && !sourceFile.isEmpty())
            {
                const QString remapKey = QString::number(originalMatIndex) + QLatin1Char('@') + sourceFile;
                animMatRemap->insert(remapKey, materialIndex);
            }
        }
        else
        {
            // Fallback for non-import sources: use content-based deduplication
            const QString materialKey = buildMaterialReuseKey(glMaterial);
            auto matIt = materialKeyToIndex.find(materialKey);

            if (matIt != materialKeyToIndex.end())
            {
                // Reuse already-created equivalent material
                materialIndex = matIt.value();
            }
            else
            {
                aiMaterial* builtMaterial = buildMaterialFromSceneMesh(triMesh);
                if (!builtMaterial)
                    builtMaterial = makeDefaultMaterial();

                materialIndex = static_cast<unsigned int>(outMaterials.size());
                outMaterials.push_back(builtMaterial);
                materialKeyToIndex.insert(materialKey, materialIndex);
            }
        }

        aiMesh* builtMesh = buildMeshFromSceneMesh(triMesh, materialIndex);
        if (!builtMesh)
        {
            // IMPORTANT:
            // If mesh build fails, do not delete a reused material.
            // Also do not roll back a newly-created material here because other meshes
            // may still legitimately reuse it during this export pass.
            continue;
        }

        // Per-mesh user TRS (from the interactive gizmo).  This is stored in
        // RenderableMesh::_transformation and is applied BEFORE the scene-hierarchy
        // world transform at render time: effectiveWorld = meshTrs * worldTransform.
        // We must fold it into the export so that the exported file matches what the
        // user sees in the viewport.
        //
        // IMPORTANT — coordinate-system correction:
        // The gizmo TRS is in viewer world space, which is the autoOrient/autoScale
        // corrected space (importCorrection has been applied to the scene root).
        // The exporter factors out importCorrection from the hierarchy so the output
        // file is in the original model coordinate system.  To express the viewer-space
        // TRS correctly in model space we must insert importCorrection between meshTrsAi
        // and worldTransform:
        //   effectiveWorld_model = meshTrs_viewer × importCorrection × worldTransform_model
        // This converts the viewer-space displacement/rotation to the model's axes so
        // that a Y-axis move in the viewer does not appear as a Z-axis move in the file.
        const QMatrix4x4 meshTrsQ = triMesh->getTransformation(); // identity when untouched
        const bool hasMeshTrs = !meshTrsQ.isIdentity();
        aiMatrix4x4 meshTrsAi;  // default = identity
        if (hasMeshTrs)
        {
            meshTrsAi = aiMatrix4x4(
                meshTrsQ(0,0), meshTrsQ(0,1), meshTrsQ(0,2), meshTrsQ(0,3),
                meshTrsQ(1,0), meshTrsQ(1,1), meshTrsQ(1,2), meshTrsQ(1,3),
                meshTrsQ(2,0), meshTrsQ(2,1), meshTrsQ(2,2), meshTrsQ(2,3),
                meshTrsQ(3,0), meshTrsQ(3,1), meshTrsQ(3,2), meshTrsQ(3,3));
        }
        // correctedMeshTrs: viewer-space TRS re-expressed in model (export) space via
        // the similarity transform  C⁻¹ × meshTrs × C  where C = importCorrection.
        //
        // Derivation: the viewer renders  meshTrs × C × H × vertex.  After export and
        // re-import (autoOrient re-applies C), the scene shows  C × node_chain × vertex.
        // For these to match:  node_chain = C⁻¹ × meshTrs × C × H = correctedMeshTrs × H.
        //
        // When importCorrection is identity (Z-up model, or no autoOrient), the
        // similarity collapses to meshTrsAi unchanged, so Z-up exports are unaffected.
        aiMatrix4x4 correctedMeshTrs;   // default = identity (hasMeshTrs=false path)
        if (hasMeshTrs)
        {
            aiMatrix4x4 corrInv = importCorrection;
            corrInv.Inverse();
            correctedMeshTrs = corrInv * meshTrsAi * importCorrection;
        }
        // effectiveWorldTransform = correctedMeshTrs * sceneHierarchyWorld (model space).
        // For flat formats this is baked into vertices.
        // For hierarchy formats it is folded into the node/child-node transform.
        const aiMatrix4x4 effectiveWorldTransform = hasMeshTrs
                                                    ? (correctedMeshTrs * worldTransform)
                                                    : worldTransform;

        // Vertex positions in SceneMesh are stored in mesh-LOCAL space.
        // processMesh() copies Assimp vertex positions verbatim with no world transform
        // applied; the world transform is only used at GPU render time via
        // setSceneRenderTransform().
        //
        // Two export strategies:
        //
        // flattenTransforms (OBJ, PLY, STL):
        //   These Assimp writers ignore every aiNode::mTransformation and write vertex
        //   positions verbatim.  Forward-apply effectiveWorldTransform here so the output
        //   file contains correct world-space positions (node transforms are identity).
        //
        // Hierarchy mode (glTF, FBX, COLLADA):
        //   Keep vertices in local space — the hierarchy-aware writer reads
        //   dstNode->mTransformation and applies it when writing, reconstructing world
        //   space on export (and again on import).  No vertex transform is needed, but
        //   the per-mesh TRS must be folded into the node/child-node transform below.
        if (flattenTransforms && !effectiveWorldTransform.IsIdentity())
        {
            // Forward-bake: local → world space.
            // Normal transform: M^{-T} (cotangent formula).
            aiMatrix3x3 normalBake = aiMatrix3x3(effectiveWorldTransform);
            normalBake = normalBake.Inverse();
            normalBake.Transpose();
            const bool hasNegativeScale = normalBake.Determinant() < 0.0f;

            for (unsigned int vi = 0; vi < builtMesh->mNumVertices; ++vi)
            {
                builtMesh->mVertices[vi] = effectiveWorldTransform * builtMesh->mVertices[vi];

                if (builtMesh->mNormals)
                {
                    aiVector3D n = normalBake * builtMesh->mNormals[vi];
                    if (hasNegativeScale) n = -n;
                    builtMesh->mNormals[vi] = n.NormalizeSafe();
                }
                if (builtMesh->mTangents)
                {
                    aiVector3D t = normalBake * builtMesh->mTangents[vi];
                    if (hasNegativeScale) t = -t;
                    builtMesh->mTangents[vi] = t.NormalizeSafe();
                }
                if (builtMesh->mBitangents)
                {
                    aiVector3D b = normalBake * builtMesh->mBitangents[vi];
                    if (hasNegativeScale) b = -b;
                    builtMesh->mBitangents[vi] = b.NormalizeSafe();
                }
            }
        }
        // Hierarchy mode: vertices remain in local space.
        // If hasMeshTrs, fold the per-mesh TRS into the node/child-node transform so
        // the reader reconstructs the correct world position.
        //
        // The per-mesh TRS lives in "pre-scene" space: effectiveWorld = meshTrs * sceneWorld.
        // For the node (or child node) to produce this, its local transform must be:
        //   newLocal = parentWorld^-1 * meshTrs * parentWorld * srcNode->localTransform
        // For a child node (splitMeshesIntoChildNodes), the effective parent world IS
        // worldTransform, so:
        //   childTransform = worldTransform^-1 * meshTrs * worldTransform
        //
        // This similarity transform re-expresses the world-space TRS in the node's
        // local coordinate system, which the exporter hierarchy reconstructs on import.

        const unsigned int exportMeshIndex = static_cast<unsigned int>(outMeshes.size());
        outMeshes.push_back(builtMesh);

        if (splitMeshesIntoChildNodes)
        {
            const QString uniqueMeshNodeName = makeUniqueExportNodeName(triMesh->getName(), usedExportNodeNames);
            aiNode* meshNode = makeIdentityNode(uniqueMeshNodeName);
            meshNode->mNumMeshes = 1;
            meshNode->mMeshes = new unsigned int[1];
            meshNode->mMeshes[0] = exportMeshIndex;
            exportedMeshNodeNameByUuid.insert(meshUuid, uniqueMeshNodeName);

            // Fold per-mesh user TRS into the child node for hierarchy formats.
            // Use correctedMeshTrs (viewer-space TRS remapped to model/export space).
            if (!flattenTransforms && hasMeshTrs)
            {
                aiMatrix4x4 worldInv = worldTransform;
                worldInv.Inverse();
                meshNode->mTransformation = worldInv * correctedMeshTrs * worldTransform;
            }

            meshChildNodes.push_back(meshNode);
        }
        else
        {
            // Single mesh on the parent node.  Fold the per-mesh TRS directly into
            // dstNode->mTransformation for hierarchy formats.
            //
            // Note: useParentSpaceEncoding is true for glTF leaf nodes whose
            // localTransform is sub-epsilon (IsIdentity() returns true).  In that
            // case dstNode->mTransformation was set to explicit identity above, but
            // when hasMeshTrs we must override it with the folded TRS — otherwise
            // the user's gizmo transform is silently lost in glTF exports.
            if (!flattenTransforms && hasMeshTrs)
            {
                // newLocal = parentWorld^-1 * correctedMeshTrs * worldTransform
                // correctedMeshTrs = C⁻¹ × meshTrs_viewer × C remaps the
                // viewer-space TRS to the exported (model) coordinate system.
                aiMatrix4x4 parentWorldInv = parentWorldTransform;
                parentWorldInv.Inverse();
                dstNode->mTransformation = parentWorldInv * correctedMeshTrs * worldTransform;
            }
            nodeMeshIndices.push_back(exportMeshIndex);
            exportedMeshNodeNameByUuid.insert(meshUuid, uniqueExportNodeName);
        }
    }

    dstNode->mNumMeshes = static_cast<unsigned int>(nodeMeshIndices.size());
    if (dstNode->mNumMeshes > 0)
    {
        dstNode->mMeshes = new unsigned int[dstNode->mNumMeshes];
        for (unsigned int i = 0; i < dstNode->mNumMeshes; ++i)
        {
            dstNode->mMeshes[i] = nodeMeshIndices[i];
        }
    }

    // Recurse children.
    dstNode->mNumChildren = static_cast<unsigned int>(meshChildNodes.size() + srcNode->children.size());
    if (dstNode->mNumChildren > 0)
    {
        dstNode->mChildren = new aiNode * [dstNode->mNumChildren];
        unsigned int childIndex = 0;

        for (aiNode* meshChild : meshChildNodes)
        {
            dstNode->mChildren[childIndex++] = meshChild;
            if (meshChild)
                meshChild->mParent = dstNode;
        }

        for (int i = 0; i < srcNode->children.size(); ++i)
        {
            SceneNode* srcChild = srcNode->children.at(i);
            aiNode* dstChild = buildNodeRecursive(srcChild, resolveMesh, outMeshes, outMaterials,
                materialKeyToIndex, exportedNodeNameByUuid, exportedMeshNodeNameByUuid, usedExportNodeNames,
                worldTransform, flattenTransforms, importCorrection, animMatRemap);
            dstNode->mChildren[childIndex++] = dstChild;
            if (dstChild)
                dstChild->mParent = dstNode;
        }
    }

    return dstNode;
}

aiMesh* SceneGraphExporter::buildMeshFromSceneMesh(const SceneMesh* mesh, unsigned int materialIndex)
{
    if (!mesh)
        return nullptr;

    // We only export AssImp-backed runtime meshes in this first pass,
    // because those expose CPU-side vertices()/indices().
    const SceneMesh* assimpMesh = dynamic_cast<const SceneMesh*>(mesh);
    if (!assimpMesh)
        return nullptr;

    const std::vector<Vertex> verts = assimpMesh->vertices();
    const std::vector<unsigned int> indices = assimpMesh->indices();

    if (verts.empty() || indices.empty())
        return nullptr;

    aiMesh* out = new aiMesh();

    out->mName.Set(mesh->getName().toUtf8().constData());
    out->mPrimitiveTypes = primitiveModeToAiPrimitiveType(mesh->getPrimitiveMode());
    out->mMaterialIndex = materialIndex;

    // --- Vertices ---
    out->mNumVertices = static_cast<unsigned int>(verts.size());
    out->mVertices = new aiVector3D[out->mNumVertices];
    out->mNormals = new aiVector3D[out->mNumVertices];

    for (unsigned int i = 0; i < out->mNumVertices; ++i)
    {
        const Vertex& v = verts[i];

        out->mVertices[i] = aiVector3D(
            v.Position.x,
            v.Position.y,
            v.Position.z);

        out->mNormals[i] = aiVector3D(
            v.Normal.x,
            v.Normal.y,
            v.Normal.z);
    }

    // --- UV channels (material-driven + vertex-driven, up to 4) ---
    // Primary: query material for the highest texCoordIndex referenced by any texture.
    // Fallback: also scan vertex data for channels that contain non-trivial UV values.
    // This prevents silently dropping UV sets when texCoordIndex metadata is not perfectly
    // round-tripped through Assimp (e.g. TEXCOORD_1 for emissive in GLB files).
    unsigned int materialMaxChannel = maxReferencedUvChannelForMesh(mesh);

    unsigned int vertexMaxChannel = 0;
    for (unsigned int ch = 0; ch < kMaxExportUvChannels; ++ch)
    {
        for (const Vertex& v : verts)
        {
            if (v.TexCoords[ch].x != 0.0f || v.TexCoords[ch].y != 0.0f)
            {
                vertexMaxChannel = ch;
                break; // found non-trivial data in this channel; move on to the next
            }
        }
    }

    const unsigned int uvChannelCount = std::min(
        std::max(materialMaxChannel, vertexMaxChannel) + 1,
        kMaxExportUvChannels);

    for (unsigned int channel = 0; channel < uvChannelCount; ++channel)
    {
        out->mTextureCoords[channel] = new aiVector3D[out->mNumVertices];
        out->mNumUVComponents[channel] = 2;

        for (unsigned int i = 0; i < out->mNumVertices; ++i)
        {
            const glm::vec2& uv = verts[i].TexCoords[channel];
            out->mTextureCoords[channel][i] = aiVector3D(uv.x, uv.y, 0.0f);
        }
    }

    // --- Vertex Colors ---
    out->mColors[0] = new aiColor4D[out->mNumVertices];
    for (unsigned int i = 0; i < out->mNumVertices; ++i)
    {
        const Vertex& v = verts[i];
        out->mColors[0][i] = aiColor4D(v.Color.r, v.Color.g, v.Color.b, v.Color.a);
    }

    if (!populateFacesForPrimitive(out, indices, mesh->getPrimitiveMode()))
    {
        delete out;
        return nullptr;
    }

    // --- Skinning (bone weights) ---
    // Export joint indices and weights stored per-vertex so the output glTF / FBX
    // round-trips skeletal animations correctly.
    const QVector<GltfSkinJoint>& skinJoints = mesh->skinJoints();
    if (!skinJoints.isEmpty())
    {
        const int numJoints = skinJoints.size();

        // Accumulate per-bone weight lists from vertex JointIndices/JointWeights.
        std::vector<std::vector<aiVertexWeight>> perBoneWeights(static_cast<size_t>(numJoints));

        for (unsigned int vi = 0; vi < out->mNumVertices; ++vi)
        {
            const Vertex& v = verts[vi];
            const int   jIdx[4] = {
                static_cast<int>(v.JointIndices.x), static_cast<int>(v.JointIndices.y),
                static_cast<int>(v.JointIndices.z), static_cast<int>(v.JointIndices.w)
            };
            const float jWt[4] = { v.JointWeights.x, v.JointWeights.y,
                                   v.JointWeights.z, v.JointWeights.w };

            for (int slot = 0; slot < 4; ++slot)
            {
                if (jIdx[slot] < 0 || jIdx[slot] >= numJoints || jWt[slot] <= 0.0f)
                    continue;
                perBoneWeights[static_cast<size_t>(jIdx[slot])].push_back({ vi, jWt[slot] });
            }
        }

        out->mNumBones = static_cast<unsigned int>(numJoints);
        out->mBones    = new aiBone*[static_cast<size_t>(numJoints)];

        for (int bi = 0; bi < numJoints; ++bi)
        {
            aiBone* bone = new aiBone();
            bone->mName.Set(skinJoints[bi].nodeName.toStdString());
            bone->mOffsetMatrix = skinJoints[bi].inverseBindMatrix;

            const auto& bw = perBoneWeights[static_cast<size_t>(bi)];
            bone->mNumWeights = static_cast<unsigned int>(bw.size());
            bone->mWeights    = bone->mNumWeights > 0
                                    ? new aiVertexWeight[bone->mNumWeights]
                                    : nullptr;
            for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi)
                bone->mWeights[wi] = bw[wi];

            out->mBones[bi] = bone;
        }

        qDebug() << "[EXPORT-SKIN] Mesh" << mesh->getName()
                 << "exported" << numJoints << "bone(s)";
    }

    // --- Morph targets (blend shapes) ---
    // Populate aiAnimMesh entries from stored MorphTargetData so the output
    // glTF / FBX carries blend-shape geometry for weight-animation round-trips.
    if (const SceneMesh* assimpMesh = dynamic_cast<const SceneMesh*>(mesh))
    {
        const QVector<MorphTargetData>& morphTargets = assimpMesh->getMorphTargets();
        if (!morphTargets.isEmpty())
        {
            const unsigned int numVerts = out->mNumVertices;
            out->mNumAnimMeshes = static_cast<unsigned int>(morphTargets.size());
            out->mAnimMeshes    = new aiAnimMesh*[morphTargets.size()];

            const QVector<float>& defWeights = assimpMesh->defaultMorphWeights();

            for (int mi = 0; mi < morphTargets.size(); ++mi)
            {
                const MorphTargetData& mt = morphTargets[mi];
                aiAnimMesh* am = new aiAnimMesh();
                am->mNumVertices = numVerts;

                // Target positions = base + delta (glTF stores deltas; Assimp wants absolute).
                if (!mt.positionDeltas.empty())
                {
                    am->mVertices = new aiVector3D[numVerts];
                    const auto count = std::min(
                        static_cast<unsigned int>(mt.positionDeltas.size()), numVerts);
                    for (unsigned int vi = 0; vi < count; ++vi)
                        am->mVertices[vi] = aiVector3D(
                            out->mVertices[vi].x + mt.positionDeltas[vi].x,
                            out->mVertices[vi].y + mt.positionDeltas[vi].y,
                            out->mVertices[vi].z + mt.positionDeltas[vi].z);
                    // Vertices beyond the delta count are unchanged.
                    for (unsigned int vi = count; vi < numVerts; ++vi)
                        am->mVertices[vi] = out->mVertices[vi];
                }

                // Target normals = base + delta.
                if (!mt.normalDeltas.empty() && out->mNormals)
                {
                    am->mNormals = new aiVector3D[numVerts];
                    const auto count = std::min(
                        static_cast<unsigned int>(mt.normalDeltas.size()), numVerts);
                    for (unsigned int vi = 0; vi < count; ++vi)
                        am->mNormals[vi] = aiVector3D(
                            out->mNormals[vi].x + mt.normalDeltas[vi].x,
                            out->mNormals[vi].y + mt.normalDeltas[vi].y,
                            out->mNormals[vi].z + mt.normalDeltas[vi].z);
                    for (unsigned int vi = count; vi < numVerts; ++vi)
                        am->mNormals[vi] = out->mNormals[vi];
                }

                // Morph target name (glTF extras.targetNames[i]).
                am->mName.Set(std::string("target_") + std::to_string(mi));
                am->mWeight = (mi < defWeights.size()) ? defWeights[mi] : 0.0f;

                out->mAnimMeshes[mi] = am;
            }

            qDebug() << "[EXPORT-MORPH] Mesh" << mesh->getName()
                     << "exported" << morphTargets.size() << "morph target(s)";
        }
    }

    return out;
}

aiMaterial* SceneGraphExporter::buildMaterialFromSceneMesh(const SceneMesh* mesh)
{
    if (!mesh)
        return nullptr;

    const Material material = exportBaseMaterial(mesh);

    aiMaterial* aiMat = new aiMaterial();

    const QString materialName =
        material.name().trimmed().isEmpty()
        ? mesh->getName()
        : material.name().trimmed();

    aiString matName(materialName.toUtf8().constData());
    aiMat->AddProperty(&matName, AI_MATKEY_NAME);

    // Core PBR / legacy-compatible scalar properties.
    {
        aiColor3D albedo(
            static_cast<float>(material.albedoColor().x()),
            static_cast<float>(material.albedoColor().y()),
            static_cast<float>(material.albedoColor().z()));
        aiMat->AddProperty(&albedo, 1, AI_MATKEY_BASE_COLOR);
        aiMat->AddProperty(&albedo, 1, AI_MATKEY_COLOR_DIFFUSE);
    }

    {
        float metallic = material.metalness();
        aiMat->AddProperty(&metallic, 1, AI_MATKEY_METALLIC_FACTOR);
    }

    {
        float roughness = material.roughness();
        aiMat->AddProperty(&roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);
    }

    {
        float opacity = material.opacity();
        aiMat->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
    }

    {
        float ior = material.ior();
        aiMat->AddProperty(&ior, 1, AI_MATKEY_REFRACTI);
    }

    {
        aiColor3D emissive(
            static_cast<float>(material.emissive().x()),
            static_cast<float>(material.emissive().y()),
            static_cast<float>(material.emissive().z()));
        aiMat->AddProperty(&emissive, 1, AI_MATKEY_COLOR_EMISSIVE);

        float emissiveStrength = material.emissiveStrength();
        aiMat->AddProperty(&emissiveStrength, 1, AI_MATKEY_EMISSIVE_INTENSITY);
    }

    if (material.transmission() > 0.0f)
    {
        float transmission = material.transmission();
        aiMat->AddProperty(&transmission, 1, AI_MATKEY_TRANSMISSION_FACTOR);
    }

    if (material.clearcoat() > 0.0f)
    {
        float clearcoat = material.clearcoat();
        aiMat->AddProperty(&clearcoat, 1, AI_MATKEY_CLEARCOAT_FACTOR);

        float clearcoatRoughness = material.clearcoatRoughness();
        aiMat->AddProperty(&clearcoatRoughness, 1, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR);
    }

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

    // Phong-compatible scalar properties for OBJ/FBX/COLLADA exporters.
    // These classic Phong properties are not meaningful for PBR renderers, but
    // OBJ/MTL, FBX, and COLLADA exporters in Assimp only read these keys.
    // Derive approximate values from the PBR inputs so legacy consumers get
    // something visually reasonable rather than nothing.
    {
        // Ns (OBJ shininess): (1 - roughness)^4 * 1000 maps [0,1] roughness to [0,1000]
        const float r = std::min(std::max(material.roughness(), 0.0f), 1.0f);
        float shininess = std::pow(1.0f - r, 4.0f) * 1000.0f;
        aiMat->AddProperty(&shininess, 1, AI_MATKEY_SHININESS);

        // Ks: white specular lobe; OBJ/FBX renderers multiply by shininess
        aiColor3D specular(1.0f, 1.0f, 1.0f);
        aiMat->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);

        // Ka: a dim version of the albedo (10 %) stands in for ambient
        aiColor3D ambient(
            static_cast<float>(material.albedoColor().x()) * 0.1f,
            static_cast<float>(material.albedoColor().y()) * 0.1f,
            static_cast<float>(material.albedoColor().z()) * 0.1f);
        aiMat->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);
    }

    // Basic rendering hints.
    {
        int twoSided = material.twoSided() ? 1 : 0;
        aiMat->AddProperty(&twoSided, 1, "$mat.gltf.doubleSided", 0, 0);
    }

    {
        aiString alphaModeStr;
        switch (material.blendMode())
        {
        case Material::BlendMode::Masked:
        {
            alphaModeStr.Set("MASK");
            float alphaCutoff = material.alphaThreshold();
            aiMat->AddProperty(&alphaCutoff, 1, "$mat.gltf.alphaCutoff", 0, 0);
            break;
        }
        case Material::BlendMode::Alpha:
            alphaModeStr.Set("BLEND");
            break;
        case Material::BlendMode::Opaque:
        default:
            alphaModeStr.Set("OPAQUE");
            break;
        }
        aiMat->AddProperty(&alphaModeStr, "$mat.gltf.alphaMode", 0, 0);
    }

    // Texture bindings.
    const auto bindings = collectExportBindings(material);
    for (const auto& binding : bindings)
    {
        if (!binding.texture)
            continue;

        addTextureToMaterial(aiMat, *binding.texture, binding.aiType, binding.slot);
    }

    return aiMat;
}
