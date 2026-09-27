#include "AssImpMeshBuilder.h"
#include "AssImpModelLoader.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"

#include <QString>

SceneMesh* AssImpMeshBuilder::build(const AssImpMeshData& meshData,
                                    QOpenGLShaderProgram* shader,
                                    ViewportWidget* viewportWidget)
{
    std::vector<Material::Texture> textures = meshData.textures;
    for (Material::Texture& texture : textures)
    {
        const QString originalPath = QString::fromStdString(texture.path);

        TextureSamplerSettings samplers{ texture.wrapS, texture.wrapT, texture.minFilter, texture.magFilter };
        if (!texture.imageData.isNull())
        {
            const QString cacheKey = originalPath.isEmpty()
                ? QStringLiteral("embedded://%1/%2/%3")
                    .arg(meshData.name)
                    .arg(QString::fromStdString(texture.type))
                    .arg(textures.size())
                : originalPath;
            texture.id = viewportWidget->getOrCreateTextureCached(cacheKey, texture.imageData, samplers);
        }
        else if (!texture.path.empty())
        {
            const QString texturePath = QString::fromStdString(texture.path);
            if (texturePath.endsWith(".ktx2", Qt::CaseInsensitive))
            {
                texture.id = viewportWidget->getOrLoadKtx2TextureCached(texturePath, texture.type, samplers);
            }
            else
            {
                texture.id = viewportWidget->getOrLoadTextureCached(texturePath, samplers);
            }
        }
        else if (texture.id != 0)
        {
            // Keep externally prepared IDs only when there is no path/image payload to re-resolve.
        }
    }

    Material resolvedMaterial = meshData.material;
    for (const Material::Texture& texture : textures)
    {
        const QString texturePath = QString::fromStdString(texture.path);
        if (texture.type == "albedoMap" || texture.type == "texture_diffuse")
        {
            resolvedMaterial.setAlbedoTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setAlbedoMap(texturePath);
        }
        else if (texture.type == "metallicMap" || texture.type == "texture_specular")
        {
            resolvedMaterial.setMetallicTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setMetallicMap(texturePath);
        }
        else if (texture.type == "roughnessMap")
        {
            resolvedMaterial.setRoughnessTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setRoughnessMap(texturePath);
        }
        else if (texture.type == "normalMap" || texture.type == "texture_normal")
        {
            resolvedMaterial.setNormalTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setNormalMap(texturePath);
        }
        else if (texture.type == "aoMap" || texture.type == "occlusionMap" || texture.type == "occlusion")
        {
            resolvedMaterial.setOcclusionTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setAOMap(texturePath);
        }
        else if (texture.type == "opacityMap" || texture.type == "texture_opacity")
        {
            resolvedMaterial.setOpacityTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setOpacityMap(texturePath);
        }
        else if (texture.type == "heightMap" || texture.type == "texture_height")
        {
            resolvedMaterial.setHeightTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setHeightMap(texturePath);
        }
        else if (texture.type == "emissiveMap" || texture.type == "texture_emissive")
        {
            resolvedMaterial.setEmissiveTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setEmissiveMap(texturePath);
        }
        else if (texture.type == "transmissionMap")
        {
            resolvedMaterial.setTransmissionTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setTransmissionMap(texturePath);
        }
        else if (texture.type == "iorMap")
        {
            resolvedMaterial.setIORTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setIORMap(texturePath);
        }
        else if (texture.type == "sheenColorMap")
        {
            resolvedMaterial.setSheenColorTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setSheenColorMap(texturePath);
        }
        else if (texture.type == "sheenRoughnessMap")
        {
            resolvedMaterial.setSheenRoughnessTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setSheenRoughnessMap(texturePath);
        }
        else if (texture.type == "clearcoatColorMap")
        {
            resolvedMaterial.setClearcoatColorTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setClearcoatColorMap(texturePath);
        }
        else if (texture.type == "clearcoatRoughnessMap")
        {
            resolvedMaterial.setClearcoatRoughnessTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setClearcoatRoughnessMap(texturePath);
        }
        else if (texture.type == "clearcoatNormalMap")
        {
            resolvedMaterial.setClearcoatNormalTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setClearcoatNormalMap(texturePath);
        }
        else if (texture.type == "iridescenceMap")
        {
            resolvedMaterial.setIridescenceTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setIridescenceMap(texturePath);
        }
        else if (texture.type == "iridescenceThicknessMap")
        {
            resolvedMaterial.setIridescenceThicknessTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setIridescenceThicknessMap(texturePath);
        }
        else if (texture.type == "specularFactorMap")
        {
            resolvedMaterial.setSpecularFactorTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setSpecularFactorMap(texturePath);
        }
        else if (texture.type == "specularColorMap")
        {
            resolvedMaterial.setSpecularColorTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setSpecularColorMap(texturePath);
        }
        else if (texture.type == "anisotropyMap")
        {
            resolvedMaterial.setAnisotropyTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setAnisotropyMap(texturePath);
        }
        else if (texture.type == "thicknessMap")
        {
            resolvedMaterial.setThicknessTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setThicknessMap(texturePath);
        }
        else if (texture.type == "diffuseMap")
        {
            resolvedMaterial.setDiffuseTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setDiffuseMap(texturePath);
        }
        else if (texture.type == "diffuseTransmissionMap")
        {
            resolvedMaterial.setDiffuseTransmissionTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setDiffuseTransmissionMap(texturePath);
        }
        else if (texture.type == "diffuseTransmissionColorMap")
        {
            resolvedMaterial.setDiffuseTransmissionColorTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setDiffuseTransmissionColorMap(texturePath);
        }
        else if (texture.type == "specularGlossinessMap")
        {
            resolvedMaterial.setSpecularGlossinessTextureId(texture.id);
            if (!texturePath.isEmpty()) resolvedMaterial.setSpecularGlossinessMap(texturePath);
        }
    }

    // Ensure ADS values are recalculated after copy and texture assignment
    // (copy assignment operator doesn't call updateConsistency)
    resolvedMaterial.updateConsistency();

    auto* mesh = new SceneMesh(shader,
        meshData.name,
        meshData.vertices,
        meshData.indices,
        textures,
        resolvedMaterial,
        !meshData.morphTargets.isEmpty(),
        meshData.primitiveMode);
    mesh->setHasNegativeScale(meshData.hasNegativeScale);
    mesh->setSceneIndex(meshData.sceneIndex);
    mesh->setOriginalMaterialIndex(meshData.originalMaterialIndex);
    mesh->setSourceFile(meshData.sourceFile);
    mesh->setSourceNodeName(meshData.sourceNodeName);
    mesh->setSkinJoints(meshData.skinJoints);
    mesh->setMorphTargets(meshData.morphTargets, meshData.defaultMorphWeights);
    if (meshData.preserveNodeTransform)
        mesh->setSceneRenderTransform(meshData.nodeWorldTransform);
    if (!meshData.precomputedOccEdges.empty())
    {
        std::vector<OccEdgeCircleInfo> circles;
        circles.reserve(meshData.precomputedOccEdgeCircles.size());
        for (const AssImpMeshData::PrecomputedEdgeCircle& c : meshData.precomputedOccEdgeCircles)
        {
            OccEdgeCircleInfo info;
            info.isCircle = c.isCircle;
            info.centerX = c.centerX; info.centerY = c.centerY; info.centerZ = c.centerZ;
            info.axisX = c.axisX;     info.axisY = c.axisY;     info.axisZ = c.axisZ;
            info.radius = c.radius;
            circles.push_back(info);
        }
        mesh->setPrecomputedOccEdges(meshData.precomputedOccEdges,
                                     meshData.precomputedOccEdgeBoundaries,
                                     circles,
                                     meshData.precomputedOccEdgeVertexTolerance);
    }
    if (!meshData.precomputedOccFaceTriangleBounds.empty())
    {
        std::vector<OccFaceAxisInfo> axes;
        axes.reserve(meshData.precomputedOccFaceAxes.size());
        for (const AssImpMeshData::PrecomputedFaceAxis& a : meshData.precomputedOccFaceAxes)
        {
            OccFaceAxisInfo info;
            info.isCylinder = a.isCylinder;
            info.isCone     = a.isCone;
            info.originX = a.originX; info.originY = a.originY; info.originZ = a.originZ;
            info.axisX = a.axisX;     info.axisY = a.axisY;     info.axisZ = a.axisZ;
            axes.push_back(info);
        }

        // Expand the dense (pre-optimization) per-face triangle-range table
        // into a sparse per-triangle list, ORIGINAL triangle order,
        // cylindrical/conical faces only (most CAD faces are flat/spline -
        // no need to carry those). `mesh` was just constructed above, and
        // its constructor already ran SceneMesh::optimizeMesh() (vertex-
        // cache/overdraw/vertex-fetch reordering) - meshData.vertices/
        // indices are the PRE-reorder arrays this table's indices are
        // valid against, so re-identify each face's triangles by position
        // in `mesh`'s own (possibly reordered) current triangle order
        // rather than trusting the table's indices directly (see
        // SceneMesh::remapOccFaceTriangleIndicesByPosition()'s doc comment).
        std::vector<int> srcTriangleIndices, srcFaceIndices;
        const std::vector<int>& faceBounds = meshData.precomputedOccFaceTriangleBounds;
        for (size_t f = 0; f + 1 < faceBounds.size(); ++f)
        {
            if (f >= axes.size() || (!axes[f].isCylinder && !axes[f].isCone))
                continue;
            for (int t = faceBounds[f]; t < faceBounds[f + 1]; ++t)
            {
                srcTriangleIndices.push_back(t);
                srcFaceIndices.push_back(static_cast<int>(f));
            }
        }

        if (!srcTriangleIndices.empty())
        {
            std::vector<int> remappedTriangleIndices, remappedFaceIndices;
            SceneMesh::remapOccFaceTriangleIndicesByPosition(
                meshData.vertices, meshData.indices, srcTriangleIndices, srcFaceIndices,
                mesh, remappedTriangleIndices, remappedFaceIndices);
            mesh->setPrecomputedOccFaceAxes(remappedTriangleIndices, remappedFaceIndices, axes);
        }
    }
    if (!meshData.variantMappings.isEmpty())
    {
        mesh->setVariantMappings(meshData.variantMappings);
        mesh->setAllVariantMaterials(meshData.allVariantMaterials);
    }

    Material runtimeResolved = ViewportWidget::resolveMaterialTextures(viewportWidget, mesh->getMaterial());
    mesh->setMaterial(runtimeResolved);
    mesh->setTextureMaps(runtimeResolved);
    mesh->invertOpacityADSMap(runtimeResolved.isOpacityMapInverted());
    mesh->invertOpacityPBRMap(runtimeResolved.isOpacityMapInverted());

    return mesh;
}
