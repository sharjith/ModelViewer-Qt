#include "GltfPostProcessor.h"
#include "SceneMesh.h"
#include "Material.h"
#include <QDebug>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QJsonParseError>
#include <QMap>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

// Default texture subfolder name - overridden per export by postProcessGltfJsonWithMaterials
QString GltfPostProcessor::_textureSubfolder = "textures";
bool GltfPostProcessor::_isGlbExport = false;
QMap<QString, QString> GltfPostProcessor::_pathMapping;
QMap<QString, int> GltfPostProcessor::_embeddedIndexMapping;
QMap<int, int> GltfPostProcessor::_materialToSourceMeshIndex;
QVector<GltfCameraEntry> GltfPostProcessor::_gltfCameras;

QStringList GltfPostProcessor::_variantNames;
QVector<MeshVariantExportEntry> GltfPostProcessor::_variantEntries;
QMap<int, Material> GltfPostProcessor::_variantMatByJsonIdx;

QVector<GltfAnimationData> GltfPostProcessor::_pointerAnimData;
QMap<int, QString>         GltfPostProcessor::_pointerAnimNodeNames;

void GltfPostProcessor::setPointerAnimationData(
    const QVector<GltfAnimationData>& animDataList,
    const QMap<int, QString>& nodeIndexToName)
{
    _pointerAnimData      = animDataList;
    _pointerAnimNodeNames = nodeIndexToName;
}

void GltfPostProcessor::clearPointerAnimationData()
{
    _pointerAnimData.clear();
    _pointerAnimNodeNames.clear();
}

void GltfPostProcessor::setGltfCameraData(const QVector<GltfCameraEntry>& cameras)
{
    _gltfCameras = cameras;
}

void GltfPostProcessor::clearGltfCameraData()
{
    _gltfCameras.clear();
}

void GltfPostProcessor::setVariantMaterialData(const QMap<int, Material>& variantMats)
{
    _variantMatByJsonIdx = variantMats;
}

void GltfPostProcessor::setVariantExportData(
    const QStringList& variantNames,
    const QVector<MeshVariantExportEntry>& entries)
{
    _variantNames = variantNames;
    _variantEntries = entries;
}

void GltfPostProcessor::clearVariantExportData()
{
    _variantNames.clear();
    _variantEntries.clear();
    _variantMatByJsonIdx.clear();
    // Note: _gltfCameras is NOT cleared here — cameras are managed independently
    // via setGltfCameraData() / clearGltfCameraData() so that the variant else-branch
    // (clearVariantExportData) does not clobber camera data that was just registered.
}

bool GltfPostProcessor::writeKhrMaterialsVariantsExtension(
    QJsonObject& gltfJson,
    std::function<void(const QString&)> logCallback)
{
    if (_variantNames.isEmpty() || _variantEntries.isEmpty())
        return false;

    const QString khrName = QStringLiteral("KHR_materials_variants");
    log("=== Writing KHR_materials_variants extension ===", logCallback);

    // Add extension name to extensionsUsed
    QJsonArray extsUsed = gltfJson.value("extensionsUsed").toArray();
    bool alreadyListed = false;
    for (const auto& v : std::as_const(extsUsed))
    {
        if (v.toString() == khrName) { alreadyListed = true; break; }
    }
    if (!alreadyListed)
        extsUsed.append(khrName);
    gltfJson["extensionsUsed"] = extsUsed;

    // Build root-level variants array: [{name: "Ceramic"}, ...]
    QJsonArray variantsArray;
    for (const QString& name : std::as_const(_variantNames))
    {
        QJsonObject varObj;
        varObj["name"] = name;
        variantsArray.append(varObj);
    }

    QJsonObject rootExts = gltfJson.value("extensions").toObject();
    QJsonObject khrRootObj;
    khrRootObj["variants"] = variantsArray;

    rootExts[khrName] = khrRootObj;
    gltfJson["extensions"] = rootExts;

    log(QString("  Root-level variants: %1").arg(_variantNames.join(", ")), logCallback);

    // Write per-primitive mappings
    QJsonArray meshesArray = gltfJson.value("meshes").toArray();
    bool anyModified = false;

    const qsizetype limit = std::min(_variantEntries.size(), meshesArray.size());
    for (qsizetype mi = 0; mi < limit; ++mi)
    {
        const MeshVariantExportEntry& entry = _variantEntries[mi];
        if (!entry.hasVariants()) continue;

        QJsonObject meshObj = meshesArray[mi].toObject();
        QJsonArray primitivesArray = meshObj.value("primitives").toArray();
        if (primitivesArray.isEmpty()) continue;

        // exportMeshes creates one primitive per mesh
        QJsonObject primObj = primitivesArray[0].toObject();

        // Build mappings array from GltfVariantMapping structs:
        // each entry groups variant indices that use the same material.
        QJsonArray mappingsArray;
        for (const GltfVariantMapping& vm : std::as_const(entry.variantMappings))
        {
            auto it = entry.matKeyToJsonMatIdx.find(vm.materialIndex);
            if (it == entry.matKeyToJsonMatIdx.end())
            {
                log(QString("  [WARN] Mesh[%1]: no JSON mat index for key %2 — skipping")
                    .arg(mi).arg(vm.materialIndex), logCallback);
                continue;
            }

            QJsonArray variantsArr;
            for (int vi : std::as_const(vm.variantIndices))
                variantsArr.append(vi);

            QJsonObject mappingObj;
            mappingObj["material"] = it.value();
            mappingObj["variants"] = variantsArr;
            mappingsArray.append(mappingObj);
        }

        if (mappingsArray.isEmpty()) continue;

        QJsonObject primExts = primObj.value("extensions").toObject();
        QJsonObject khrPrimObj;
        khrPrimObj["mappings"] = mappingsArray;
        primExts[khrName] = khrPrimObj;
        primObj["extensions"] = primExts;
        primitivesArray[0] = primObj;
        meshObj["primitives"] = primitivesArray;
        meshesArray[mi] = meshObj;
        anyModified = true;

        log(QString("  Mesh[%1]: wrote %2 variant mappings").arg(mi).arg(mappingsArray.size()), logCallback);
    }

    if (anyModified)
        gltfJson["meshes"] = meshesArray;

    log("=== KHR_materials_variants extension complete ===", logCallback);
    return true;
}

namespace
{
QString resolvePackagedPath(const QMap<QString, QString>& pathMapping, const QString& sourcePath)
{
    auto it = pathMapping.find(sourcePath);
    if (it != pathMapping.end() && !it.value().isEmpty())
        return it.value();

    QString normalisedPath = sourcePath;
    if (sourcePath.startsWith("glb://") && sourcePath.contains("::"))
        normalisedPath = "glb://" + sourcePath.mid(sourcePath.lastIndexOf("::") + 2);

    if (normalisedPath != sourcePath)
    {
        it = pathMapping.find(normalisedPath);
        if (it != pathMapping.end())
            return it.value();
    }

    return {};
}

QString resolveTextureFileForExport(const QString& sourcePath,
                                    const QString& exportFilePath,
                                    const QMap<QString, QString>& pathMapping,
                                    bool isGlbExport)
{
    if (sourcePath.isEmpty())
        return {};

    QFileInfo directInfo(sourcePath);
    if (directInfo.isAbsolute() && directInfo.exists())
        return QDir::cleanPath(sourcePath);

    if (directInfo.exists())
        return directInfo.absoluteFilePath();

    QString packaged = resolvePackagedPath(pathMapping, sourcePath);
    if (!packaged.isEmpty())
    {
        QFileInfo packagedInfo(packaged);
        if (packagedInfo.isAbsolute() && packagedInfo.exists())
            return packagedInfo.absoluteFilePath();

        QString candidate = isGlbExport
            ? QDir(QDir::tempPath()).filePath(packaged)
            : QFileInfo(exportFilePath).dir().filePath(packaged);

        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }

    return {};
}

float samplePackedOpacity(const QRgb pixel, const Material::ChannelPacking& packing)
{
    const float channels[4] = {
        qRed(pixel) / 255.0f,
        qGreen(pixel) / 255.0f,
        qBlue(pixel) / 255.0f,
        qAlpha(pixel) / 255.0f
    };

    int ch = packing.channel;
    if (ch < 0 || ch > 3)
        ch = 0;

    float value = channels[ch];
    if (packing.invert)
        value = 1.0f - value;

    value = value * packing.scale + packing.bias;
    return std::clamp(value, 0.0f, 1.0f);
}

bool isIdentityTextureTransform(const Material::Texture& tex)
{
    auto nearlyEqual = [](float a, float b) {
        return std::abs(a - b) < 1e-5f;
    };

    return tex.texCoordIndex == 0 &&
           nearlyEqual(tex.scale.x, 1.0f) &&
           nearlyEqual(tex.scale.y, 1.0f) &&
           nearlyEqual(tex.offset.x, 0.0f) &&
           nearlyEqual(tex.offset.y, 0.0f) &&
           nearlyEqual(tex.rotation, 0.0f);
}

void applyTextureTransformToInfo(QJsonObject& texInfo, const Material::Texture& tex)
{
    if (tex.texCoordIndex != 0)
        texInfo["texCoord"] = tex.texCoordIndex;

    if (isIdentityTextureTransform(tex))
        return;

    QJsonObject transform;
    if (std::abs(tex.offset.x) > 1e-5f || std::abs(tex.offset.y) > 1e-5f)
    {
        QJsonArray offset;
        offset.append(static_cast<double>(tex.offset.x));
        offset.append(static_cast<double>(tex.offset.y));
        transform["offset"] = offset;
    }

    if (std::abs(tex.scale.x - 1.0f) > 1e-5f || std::abs(tex.scale.y - 1.0f) > 1e-5f)
    {
        QJsonArray scale;
        scale.append(static_cast<double>(tex.scale.x));
        scale.append(static_cast<double>(tex.scale.y));
        transform["scale"] = scale;
    }

    if (std::abs(tex.rotation) > 1e-5f)
        transform["rotation"] = static_cast<double>(tex.rotation);

    if (tex.texCoordIndex != 0)
        transform["texCoord"] = tex.texCoordIndex;

    QJsonObject extensions = texInfo.value("extensions").toObject();
    extensions["KHR_texture_transform"] = transform;
    texInfo["extensions"] = extensions;
}

QImage buildMergedBaseColorImage(const Material& material,
                                 const QString& baseColorPath,
                                 const QString& opacityPath,
                                 std::function<void(const QString&)> logCallback)
{
    auto emitLog = [&](const QString& msg) {
        if (logCallback)
            logCallback(msg);
        else
            qDebug() << msg;
    };

    QImage opacityImage(opacityPath);
    if (opacityImage.isNull())
    {
        emitLog(QString("  [OpacityMerge] Failed to load opacity image: %1").arg(opacityPath));
        return {};
    }
    opacityImage = opacityImage.convertToFormat(QImage::Format_RGBA8888);

    QImage baseImage;
    if (!baseColorPath.isEmpty())
    {
        baseImage = QImage(baseColorPath);
        if (!baseImage.isNull())
            baseImage = baseImage.convertToFormat(QImage::Format_RGBA8888);
        else
            emitLog(QString("  [OpacityMerge] Failed to load base color image: %1").arg(baseColorPath));
    }

    if (baseImage.isNull())
    {
        baseImage = QImage(opacityImage.size(), QImage::Format_RGBA8888);
        QColor fillColor;
        QVector3D albedo = material.albedoColor();
        fillColor.setRgbF(
            std::clamp(albedo.x(), 0.0f, 1.0f),
            std::clamp(albedo.y(), 0.0f, 1.0f),
            std::clamp(albedo.z(), 0.0f, 1.0f),
            1.0f);
        baseImage.fill(fillColor);
    }

    if (opacityImage.size() != baseImage.size())
        opacityImage = opacityImage.scaled(baseImage.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const Material::ChannelPacking opacityPacking = material.packingFor("opacity");

    for (int y = 0; y < baseImage.height(); ++y)
    {
        QRgb* baseScan = reinterpret_cast<QRgb*>(baseImage.scanLine(y));
        const QRgb* opacityScan = reinterpret_cast<const QRgb*>(opacityImage.constScanLine(y));
        for (int x = 0; x < baseImage.width(); ++x)
        {
            const float baseAlpha = qAlpha(baseScan[x]) / 255.0f;
            const float opacityAlpha = samplePackedOpacity(opacityScan[x], opacityPacking);
            const int finalAlpha = qBound(0, qRound((baseAlpha * opacityAlpha) * 255.0f), 255);
            baseScan[x] = qRgba(qRed(baseScan[x]), qGreen(baseScan[x]), qBlue(baseScan[x]), finalAlpha);
        }
    }

    return baseImage;
}
}

void GltfPostProcessor::log(const QString& message, std::function<void(const QString&)> callback)
{
    if (callback)
    {
        callback(message);
    }
    else
    {
        qDebug() << message;
    }
}

const SceneMesh* GltfPostProcessor::sourceMeshForMaterial(
    int materialIndex,
    const std::vector<SceneMesh*>& meshes)
{
    auto mappedIt = _materialToSourceMeshIndex.find(materialIndex);
    if (mappedIt != _materialToSourceMeshIndex.end())
    {
        int meshIndex = mappedIt.value();
        if (meshIndex >= 0 && meshIndex < static_cast<int>(meshes.size()))
            return meshes[meshIndex];
    }

    if (materialIndex >= 0 && materialIndex < static_cast<int>(meshes.size()))
        return meshes[materialIndex];

    return nullptr;
}

Material GltfPostProcessor::defaultMaterialForMesh(const SceneMesh* mesh)
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

Material GltfPostProcessor::sourceMaterialForJsonMaterial(
    int materialIndex,
    const std::vector<SceneMesh*>& meshes)
{
    auto variantIt = _variantMatByJsonIdx.constFind(materialIndex);
    if (variantIt != _variantMatByJsonIdx.constEnd())
        return variantIt.value();

    const SceneMesh* sourceMesh = sourceMeshForMaterial(materialIndex, meshes);
    return defaultMaterialForMesh(sourceMesh);
}

bool GltfPostProcessor::postProcessGltfJson(QJsonObject& gltfJson,
    std::function<void(const QString&)> logCallback)
{
    bool modified = false;
    bool hasTextureTransforms = false;
    bool hasAnisotropyMaterials = false;

    log("=== glTF Post-Processor ===", logCallback);

    // Helper to check if a texture has transforms
    auto hasTransforms = [](const QJsonObject& obj) -> bool {
        if (obj.contains("extensions"))
        {
            QJsonObject ext = obj["extensions"].toObject();
            return ext.contains("KHR_texture_transform");
        }
        return false;
        };

    // 1. Fix materials
    if (gltfJson.contains("materials"))
    {
        QJsonArray materials = gltfJson["materials"].toArray();
        bool materialsModified = false;

        for (int i = 0; i < materials.size(); ++i)
        {
            QJsonObject mat = materials[i].toObject();
            bool matModified = false;

            // Fix PBR metallic-roughness textures
            if (mat.contains("pbrMetallicRoughness"))
            {
                QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();

                // Check for transforms
                if (pbr.contains("baseColorTexture"))
                {
                    if (hasTransforms(pbr["baseColorTexture"].toObject()))
                        hasTextureTransforms = true;
                    if (fixTextureInfoWithTransforms(pbr, "baseColorTexture"))
                        matModified = true;
                }

                if (pbr.contains("metallicRoughnessTexture"))
                {
                    if (hasTransforms(pbr["metallicRoughnessTexture"].toObject()))
                        hasTextureTransforms = true;
                    if (fixTextureInfoWithTransforms(pbr, "metallicRoughnessTexture"))
                        matModified = true;
                }

                if (matModified)
                    mat["pbrMetallicRoughness"] = pbr;
            }

            // Fix normal texture (has scale + transforms)
            if (mat.contains("normalTexture"))
            {
                if (hasTransforms(mat["normalTexture"].toObject()))
                    hasTextureTransforms = true;
                if (fixNormalTextureInfo(mat, "normalTexture"))
                    matModified = true;
            }

            // Fix occlusion texture (has strength + transforms)
            if (mat.contains("occlusionTexture"))
            {
                if (hasTransforms(mat["occlusionTexture"].toObject()))
                    hasTextureTransforms = true;
                if (fixOcclusionTextureInfo(mat, "occlusionTexture"))
                    matModified = true;
            }

            // Fix emissive texture (transforms only)
            if (mat.contains("emissiveTexture"))
            {
                if (hasTransforms(mat["emissiveTexture"].toObject()))
                    hasTextureTransforms = true;
                if (fixTextureInfoWithTransforms(mat, "emissiveTexture"))
                    matModified = true;
            }

            // Fix extension textures if present
            if (mat.contains("extensions"))
            {
                QJsonObject extensions = mat["extensions"].toObject();
                bool extModified = false;

                if (extensions.contains("KHR_materials_anisotropy"))
                    hasAnisotropyMaterials = true;

                // KHR_materials_clearcoat
                if (extensions.contains("KHR_materials_clearcoat"))
                {
                    QJsonObject clearcoat = extensions["KHR_materials_clearcoat"].toObject();

                    if (clearcoat.contains("clearcoatTexture"))
                    {
                        if (hasTransforms(clearcoat["clearcoatTexture"].toObject()))
                            hasTextureTransforms = true;
                        if (fixTextureInfoWithTransforms(clearcoat, "clearcoatTexture"))
                            extModified = true;
                    }

                    if (clearcoat.contains("clearcoatRoughnessTexture"))
                    {
                        if (hasTransforms(clearcoat["clearcoatRoughnessTexture"].toObject()))
                            hasTextureTransforms = true;
                        if (fixTextureInfoWithTransforms(clearcoat, "clearcoatRoughnessTexture"))
                            extModified = true;
                    }

                    if (clearcoat.contains("clearcoatNormalTexture"))
                    {
                        if (hasTransforms(clearcoat["clearcoatNormalTexture"].toObject()))
                            hasTextureTransforms = true;
                        if (fixTextureInfoWithTransforms(clearcoat, "clearcoatNormalTexture"))
                            extModified = true;
                    }

                    if (extModified)
                        extensions["KHR_materials_clearcoat"] = clearcoat;
                }

                // KHR_materials_transmission
                if (extensions.contains("KHR_materials_transmission"))
                {
                    QJsonObject transmission = extensions["KHR_materials_transmission"].toObject();

                    if (transmission.contains("transmissionTexture"))
                    {
                        if (hasTransforms(transmission["transmissionTexture"].toObject()))
                            hasTextureTransforms = true;
                        if (fixTextureInfoWithTransforms(transmission, "transmissionTexture"))
                            extModified = true;
                    }

                    if (extModified)
                        extensions["KHR_materials_transmission"] = transmission;
                }

                // KHR_materials_sheen
                if (extensions.contains("KHR_materials_sheen"))
                {
                    QJsonObject sheen = extensions["KHR_materials_sheen"].toObject();

                    if (sheen.contains("sheenColorTexture"))
                    {
                        if (hasTransforms(sheen["sheenColorTexture"].toObject()))
                            hasTextureTransforms = true;
                        if (fixTextureInfoWithTransforms(sheen, "sheenColorTexture"))
                            extModified = true;
                    }

                    if (sheen.contains("sheenRoughnessTexture"))
                    {
                        if (hasTransforms(sheen["sheenRoughnessTexture"].toObject()))
                            hasTextureTransforms = true;
                        if (fixTextureInfoWithTransforms(sheen, "sheenRoughnessTexture"))
                            extModified = true;
                    }

                    if (extModified)
                        extensions["KHR_materials_sheen"] = sheen;
                }

                if (extModified)
                {
                    mat["extensions"] = extensions;
                    matModified = true;
                }
            }

            if (matModified)
            {
                materials[i] = mat;
                materialsModified = true;
                log(QString("  Fixed material %1: %2").arg(i).arg(mat["name"].toString()), logCallback);
            }
        }

        if (materialsModified)
        {
            gltfJson["materials"] = materials;
            modified = true;
        }
    }

    // 1b. Ensure KHR_texture_transform is in extensionsUsed if we found transforms
    if (hasTextureTransforms)
    {
        QJsonArray extensionsUsed;
        if (gltfJson.contains("extensionsUsed"))
        {
            extensionsUsed = gltfJson["extensionsUsed"].toArray();
        }

        // Check if KHR_texture_transform is already listed
        bool hasExtension = false;
        for (const QJsonValue& val : extensionsUsed)
        {
            if (val.toString() == "KHR_texture_transform")
            {
                hasExtension = true;
                break;
            }
        }

        if (!hasExtension)
        {
            extensionsUsed.append("KHR_texture_transform");
            gltfJson["extensionsUsed"] = extensionsUsed;
            modified = true;
            log("  Added KHR_texture_transform to extensionsUsed", logCallback);
        }
    }

    // 2. Fix samplers - only fill in missing properties for existing samplers
    if (gltfJson.contains("samplers"))
    {
        QJsonArray samplers = gltfJson["samplers"].toArray();
        bool samplersModified = false;
        int fixedCount = 0;

        for (int i = 0; i < samplers.size(); ++i)
        {
            QJsonObject sampler = samplers[i].toObject();
            bool thisFixed = false;

            // Add missing filter properties
            if (!sampler.contains("magFilter"))
            {
                sampler["magFilter"] = 9729; // GL_LINEAR
                thisFixed = true;
            }

            if (!sampler.contains("minFilter"))
            {
                sampler["minFilter"] = 9987; // GL_LINEAR_MIPMAP_LINEAR
                thisFixed = true;
            }

            // Add missing wrap properties
            if (!sampler.contains("wrapS"))
            {
                sampler["wrapS"] = 10497; // GL_REPEAT
                thisFixed = true;
            }

            if (!sampler.contains("wrapT"))
            {
                sampler["wrapT"] = 10497; // GL_REPEAT
                thisFixed = true;
            }

            if (thisFixed)
            {
                samplers[i] = sampler;
                samplersModified = true;
                fixedCount++;
            }
        }

        if (samplersModified)
        {
            gltfJson["samplers"] = samplers;
            modified = true;
            log(QString("  Fixed %1 sampler(s) with missing properties").arg(fixedCount), logCallback);
        }
    }

    if (modified)
    {
        log("  Post-processing complete: Fixed missing properties", logCallback);
    }
    else
    {
        log("  No modifications needed", logCallback);
    }

    return modified;
}

bool GltfPostProcessor::postProcessGltfFile(const QString& filePath,
    std::function<void(const QString&)> logCallback)
{
    // Read the file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log(QString("Failed to open glTF file for reading: %1").arg(filePath), logCallback);
        return false;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    // Parse JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        log(QString("Failed to parse glTF JSON: %1").arg(parseError.errorString()), logCallback);
        return false;
    }

    if (!doc.isObject())
    {
        log("glTF JSON root is not an object", logCallback);
        return false;
    }

    // Post-process
    QJsonObject gltfJson = doc.object();
    bool modified = postProcessGltfJson(gltfJson, logCallback);

    if (!modified)
    {
        log(QString("No modifications needed for: %1").arg(filePath), logCallback);
        return true;
    }

    // Write back
    doc.setObject(gltfJson);

    if (!file.open(QIODevice::WriteOnly))
    {
        log(QString("Failed to open glTF file for writing: %1").arg(filePath), logCallback);
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    log(QString("Successfully post-processed: %1").arg(filePath), logCallback);
    return true;
}

bool GltfPostProcessor::postProcessGlbFile(const QString& filePath,
    std::function<void(const QString&)> logCallback)
{
    // Read GLB file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log(QString("Failed to open GLB file for reading: %1").arg(filePath), logCallback);
        return false;
    }

    QByteArray glbData = file.readAll();
    file.close();

    if (glbData.size() < 12)
    {
        log("GLB file too small", logCallback);
        return false;
    }

    // Parse GLB header
    const char* data = glbData.constData();
    quint32 magic = *reinterpret_cast<const quint32*>(data);
    quint32 version = *reinterpret_cast<const quint32*>(data + 4);
    quint32 length = *reinterpret_cast<const quint32*>(data + 8);

    if (magic != 0x46546C67) // "glTF"
    {
        log("Invalid GLB magic number", logCallback);
        return false;
    }

    if (version != 2)
    {
        log("Only glTF 2.0 GLB files are supported", logCallback);
        return false;
    }

    // Read JSON chunk
    int offset = 12;
    if (offset + 8 > glbData.size())
    {
        log("GLB file truncated (no JSON chunk)", logCallback);
        return false;
    }

    quint32 jsonChunkLength = *reinterpret_cast<const quint32*>(data + offset);
    quint32 jsonChunkType = *reinterpret_cast<const quint32*>(data + offset + 4);

    if (jsonChunkType != 0x4E4F534A) // "JSON"
    {
        log("Invalid JSON chunk type", logCallback);
        return false;
    }

    offset += 8;
    if (offset + jsonChunkLength > static_cast<quint32>(glbData.size()))
    {
        log("GLB file truncated (JSON chunk too large)", logCallback);
        return false;
    }

    QByteArray jsonData = glbData.mid(offset, jsonChunkLength);
    offset += jsonChunkLength;

    // Parse JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        log(QString("Failed to parse GLB JSON: %1").arg(parseError.errorString()), logCallback);
        return false;
    }

    if (!doc.isObject())
    {
        log("GLB JSON root is not an object", logCallback);
        return false;
    }

    // Post-process JSON
    QJsonObject gltfJson = doc.object();
    bool modified = postProcessGltfJson(gltfJson, logCallback);

    if (!modified)
    {
        log(QString("No modifications needed for: %1").arg(filePath), logCallback);
        return true;
    }

    // Reconstruct GLB with modified JSON
    doc.setObject(gltfJson);
    QByteArray newJsonData = doc.toJson(QJsonDocument::Compact);

    // Pad JSON to 4-byte boundary
    while (newJsonData.size() % 4 != 0)
    {
        newJsonData.append(' ');
    }

    // Build new GLB
    QByteArray newGlbData;
    quint32 newJsonLength = newJsonData.size();
    quint32 newLength = 12 + 8 + newJsonLength;

    // Copy binary chunks (if any)
    QByteArray binaryData;
    if (offset < glbData.size())
    {
        binaryData = glbData.mid(offset);
        newLength += binaryData.size();
    }

    // Write header
    newGlbData.append(reinterpret_cast<const char*>(&magic), 4);
    newGlbData.append(reinterpret_cast<const char*>(&version), 4);
    newGlbData.append(reinterpret_cast<const char*>(&newLength), 4);

    // Write JSON chunk
    quint32 jsonType = 0x4E4F534A; // "JSON"
    newGlbData.append(reinterpret_cast<const char*>(&newJsonLength), 4);
    newGlbData.append(reinterpret_cast<const char*>(&jsonType), 4);
    newGlbData.append(newJsonData);

    // Write binary chunks
    if (!binaryData.isEmpty())
    {
        newGlbData.append(binaryData);
    }

    // Write back to file
    if (!file.open(QIODevice::WriteOnly))
    {
        log(QString("Failed to open GLB file for writing: %1").arg(filePath), logCallback);
        return false;
    }

    file.write(newGlbData);
    file.close();

    log(QString("Successfully post-processed GLB: %1").arg(filePath), logCallback);
    return true;
}

bool GltfPostProcessor::postProcessGltfFileWithMaterials(
    const QString& filePath,
    const std::vector<SceneMesh*>& meshes,
    const std::vector<GPULight>& lights,
    std::function<void(const QString&)> logCallback,
    const QString& textureSubfolder,
    const QMap<QString, QString>& pathMapping,
    const QMap<QString, int>& embeddedIndexMapping)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log(QString("Failed to open glTF file: %1").arg(filePath), logCallback);
        return false;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        log("Failed to parse glTF JSON", logCallback);
        return false;
    }

    QJsonObject gltfJson = doc.object();

    _isGlbExport = false;
    // Process with material transforms
    postProcessGltfJsonWithMaterials(gltfJson, meshes, lights, logCallback, textureSubfolder, pathMapping, embeddedIndexMapping);
    mergeOpacityIntoBaseColorTextures(gltfJson, meshes, filePath, nullptr, logCallback);
    injectPointerAnimationChannels(gltfJson, nullptr, logCallback);
    injectMorphWeightAnimations(gltfJson, nullptr, logCallback);

    doc.setObject(gltfJson);

    if (!file.open(QIODevice::WriteOnly))
    {
        log(QString("Failed to write glTF file: %1").arg(filePath), logCallback);
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    log(QString("Successfully post-processed with materials: %1").arg(filePath), logCallback);
    return true;
}

bool GltfPostProcessor::postProcessGlbFileWithMaterials(
    const QString& filePath,
    const std::vector<SceneMesh*>& meshes,
    const std::vector<GPULight>& lights,
    std::function<void(const QString&)> logCallback,
    const QString& textureSubfolder,
    const QMap<QString, QString>& pathMapping,
    const QMap<QString, int>& embeddedIndexMapping)
{
    // Read GLB file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log(QString("Failed to open GLB file for reading: %1").arg(filePath), logCallback);
        return false;
    }

    QByteArray glbData = file.readAll();
    file.close();

    if (glbData.size() < 12)
    {
        log("GLB file too small", logCallback);
        return false;
    }

    // Parse GLB header
    const char* data = glbData.constData();
    quint32 magic = *reinterpret_cast<const quint32*>(data);
    quint32 version = *reinterpret_cast<const quint32*>(data + 4);
    quint32 length = *reinterpret_cast<const quint32*>(data + 8);

    if (magic != 0x46546C67) // "glTF"
    {
        log("Invalid GLB magic number", logCallback);
        return false;
    }

    if (version != 2)
    {
        log("Only glTF 2.0 GLB files are supported", logCallback);
        return false;
    }

    // Read JSON chunk
    int offset = 12;
    if (offset + 8 > glbData.size())
    {
        log("GLB file truncated (no JSON chunk)", logCallback);
        return false;
    }

    quint32 jsonChunkLength = *reinterpret_cast<const quint32*>(data + offset);
    quint32 jsonChunkType = *reinterpret_cast<const quint32*>(data + offset + 4);

    if (jsonChunkType != 0x4E4F534A) // "JSON"
    {
        log("Invalid JSON chunk type", logCallback);
        return false;
    }

    offset += 8;
    if (offset + jsonChunkLength > static_cast<quint32>(glbData.size()))
    {
        log("GLB file truncated (JSON chunk too large)", logCallback);
        return false;
    }

    QByteArray jsonData = glbData.mid(offset, jsonChunkLength);
    offset += jsonChunkLength;

    // Parse JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        log(QString("Failed to parse GLB JSON: %1").arg(parseError.errorString()), logCallback);
        return false;
    }

    if (!doc.isObject())
    {
        log("GLB JSON root is not an object", logCallback);
        return false;
    }

    QJsonObject gltfJson = doc.object();

    _isGlbExport = true;
    // Post-process with material transforms
    postProcessGltfJsonWithMaterials(gltfJson, meshes, lights, logCallback, textureSubfolder, pathMapping, embeddedIndexMapping);
    QByteArray binaryChunkData;
    if (offset < glbData.size())
        binaryChunkData = glbData.mid(offset);

    mergeOpacityIntoBaseColorTextures(gltfJson, meshes, filePath, &binaryChunkData, logCallback);
    qDebug() << "[GLB-POSTPROCESS] binaryChunkData.size() after mergeOpacity=" << binaryChunkData.size();
    injectPointerAnimationChannels(gltfJson, &binaryChunkData, logCallback);
    qDebug() << "[GLB-POSTPROCESS] binaryChunkData.size() after injectPointer=" << binaryChunkData.size();
    injectMorphWeightAnimations(gltfJson, &binaryChunkData, logCallback);
    qDebug() << "[GLB-POSTPROCESS] binaryChunkData.size() after injectMorph=" << binaryChunkData.size();

    // If binaryChunkData was modified (new accessor data appended), the binary
    // chunk header (first 8 bytes: chunkLength + chunkType) still holds the
    // OLD chunk length written by Assimp.  Update the chunkLength field so that
    // any reader that trusts the chunk header (e.g. extractJsonFromGLB) can read
    // the full buffer, including the newly injected animation accessor data.
    if (binaryChunkData.size() >= 8)
    {
        const quint32 oldChunkLen = *reinterpret_cast<const quint32*>(binaryChunkData.constData());
        const quint32 newChunkLen = static_cast<quint32>(binaryChunkData.size() - 8);
        qDebug() << "[GLB-POSTPROCESS] updating binary chunk header:" << oldChunkLen << "->" << newChunkLen;
        memcpy(binaryChunkData.data(), &newChunkLen, 4);
    }

    // Reconstruct GLB with modified JSON
    doc.setObject(gltfJson);
    QByteArray newJsonData = doc.toJson(QJsonDocument::Compact);

    // Pad JSON to 4-byte boundary
    while (newJsonData.size() % 4 != 0)
    {
        newJsonData.append(' ');
    }

    // Build new GLB
    QByteArray newGlbData;
    quint32 newJsonLength = newJsonData.size();
    quint32 newLength = 12 + 8 + newJsonLength;

    // Copy binary chunks (if any) - this preserves embedded textures
    QByteArray binaryData = binaryChunkData;
    if (!binaryData.isEmpty())
        newLength += binaryData.size();

    // Write header
    newGlbData.append(reinterpret_cast<const char*>(&magic), 4);
    newGlbData.append(reinterpret_cast<const char*>(&version), 4);
    newGlbData.append(reinterpret_cast<const char*>(&newLength), 4);

    // Write JSON chunk
    quint32 jsonType = 0x4E4F534A; // "JSON"
    newGlbData.append(reinterpret_cast<const char*>(&newJsonLength), 4);
    newGlbData.append(reinterpret_cast<const char*>(&jsonType), 4);
    newGlbData.append(newJsonData);

    // Write binary chunks (includes embedded textures)
    if (!binaryData.isEmpty())
    {
        newGlbData.append(binaryData);
    }

    // Write back to file
    if (!file.open(QIODevice::WriteOnly))
    {
        log(QString("Failed to open GLB file for writing: %1").arg(filePath), logCallback);
        return false;
    }

    file.write(newGlbData);
    file.close();

    log(QString("Successfully post-processed GLB with materials: %1").arg(filePath), logCallback);
    return true;
}

bool GltfPostProcessor::writePunctualLights(
    QJsonObject& gltfJson,
    const std::vector<GPULight>& lights,
    std::function<void(const QString&)> logCallback)
{
    if (lights.empty())
        return false;

    log(QString("Writing %1 punctual light(s)...").arg(lights.size()), logCallback);

    QJsonArray lightsArray;
    QJsonArray nodesArray = gltfJson.value("nodes").toArray();

    for (size_t i = 0; i < lights.size(); ++i)
    {
        const GPULight& light = lights[i];

        // --- Determine type string ---
        QString typeStr;
        LightType lt = static_cast<LightType>(light.type);
        if (lt == LightType::Directional) typeStr = "directional";
        else if (lt == LightType::Point)       typeStr = "point";
        else if (lt == LightType::Spot)        typeStr = "spot";
        else
        {
            log(QString("  Light %1: unknown type %2, skipping").arg(i).arg(light.type), logCallback);
            continue;
        }

        // --- Build light definition ---
        QJsonObject lightDef;
        lightDef["type"] = typeStr;
        lightDef["name"] = QString("light_%1").arg(i);
        lightDef["intensity"] = static_cast<double>(light.intensity);

        // Color (only write if non-white)
        if (std::abs(light.color.x - 1.0f) > 0.001f ||
            std::abs(light.color.y - 1.0f) > 0.001f ||
            std::abs(light.color.z - 1.0f) > 0.001f)
        {
            QJsonArray colorArr;
            colorArr.append(static_cast<double>(light.color.x));
            colorArr.append(static_cast<double>(light.color.y));
            colorArr.append(static_cast<double>(light.color.z));
            lightDef["color"] = colorArr;
        }

        // Range (omit for directional or infinite/zero range)
        if (lt != LightType::Directional && light.range > 0.0f)
            lightDef["range"] = static_cast<double>(light.range);

        // Spot cone angles (stored as cosines in GPULight, convert back to radians)
        if (lt == LightType::Spot)
        {
            float innerAngle = std::acos(glm::clamp(light.innerConeCos, -1.0f, 1.0f));
            float outerAngle = std::acos(glm::clamp(light.outerConeCos, -1.0f, 1.0f));
            QJsonObject spotDef;
            spotDef["innerConeAngle"] = static_cast<double>(innerAngle);
            spotDef["outerConeAngle"] = static_cast<double>(outerAngle);
            lightDef["spot"] = spotDef;
        }

        int lightIndex = lightsArray.size();
        lightsArray.append(lightDef);

        // --- Build a node for this light ---
        // glTF lights live on nodes: position via translation, direction via rotation.
        // Default light direction in glTF is -Z (0,0,-1).
        QJsonObject lightNode;
        lightNode["name"] = QString("light_node_%1").arg(i);

        // Translation
        QJsonArray translation;
        translation.append(static_cast<double>(light.position.x));
        translation.append(static_cast<double>(light.position.y));
        translation.append(static_cast<double>(light.position.z));
        lightNode["translation"] = translation;

        // Rotation: from glTF default (-Z) to actual light direction
        glm::vec3 defaultDir(0.0f, 0.0f, -1.0f);
        glm::vec3 dir = glm::normalize(light.direction);
        float dot = glm::dot(defaultDir, dir);

        glm::quat rot;
        if (dot >= 1.0f - 1e-6f)
        {
            rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // identity - already pointing -Z
        }
        else if (dot <= -1.0f + 1e-6f)
        {
            // Exactly opposite - 180� around any perpendicular axis
            rot = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        else
        {
            glm::vec3 axis = glm::normalize(glm::cross(defaultDir, dir));
            float angle = std::acos(dot);
            rot = glm::angleAxis(angle, axis);
        }
        QJsonArray rotation;
        rotation.append(static_cast<double>(rot.x));
        rotation.append(static_cast<double>(rot.y));
        rotation.append(static_cast<double>(rot.z));
        rotation.append(static_cast<double>(rot.w));
        lightNode["rotation"] = rotation;

        // KHR_lights_punctual reference on the node
        QJsonObject nodeExt;
        QJsonObject lightRef;
        lightRef["light"] = lightIndex;
        nodeExt["KHR_lights_punctual"] = lightRef;
        lightNode["extensions"] = nodeExt;

        int nodeIndex = nodesArray.size();
        nodesArray.append(lightNode);

        log(QString("  -> %1 light '%2' at node %3")
            .arg(typeStr).arg(lightDef["name"].toString()).arg(nodeIndex), logCallback);
    }

    if (lightsArray.isEmpty())
        return false;

    // --- Write lights array to top-level extensions ---
    QJsonObject topExtensions = gltfJson.value("extensions").toObject();
    QJsonObject khrLights;
    khrLights["lights"] = lightsArray;
    topExtensions["KHR_lights_punctual"] = khrLights;
    gltfJson["extensions"] = topExtensions;

    // --- Write updated nodes array ---
    gltfJson["nodes"] = nodesArray;

    // --- Add light nodes to the first scene ---
    if (gltfJson.contains("scenes"))
    {
        QJsonArray scenes = gltfJson["scenes"].toArray();
        if (!scenes.isEmpty())
        {
            QJsonObject scene = scenes[0].toObject();
            QJsonArray sceneNodes = scene.value("nodes").toArray();
            int firstLightNode = nodesArray.size() - static_cast<int>(lightsArray.size());
            for (int i = firstLightNode; i < nodesArray.size(); ++i)
                sceneNodes.append(i);
            scene["nodes"] = sceneNodes;
            scenes[0] = scene;
            gltfJson["scenes"] = scenes;
        }
    }

    // --- Add KHR_lights_punctual to extensionsUsed ---
    QJsonArray extensionsUsed = gltfJson.value("extensionsUsed").toArray();
    bool listed = false;
    for (const QJsonValue& v : extensionsUsed)
        if (v.toString() == "KHR_lights_punctual") { listed = true; break; }
    if (!listed)
    {
        extensionsUsed.append("KHR_lights_punctual");
        gltfJson["extensionsUsed"] = extensionsUsed;
    }

    log(QString("  -> KHR_lights_punctual written (%1 lights)").arg(lightsArray.size()), logCallback);
    return true;
}

// Inject glTF camera definitions and wire them to their hosting nodes.
//
// Assimp's glTF2 exporter does not write cameras, so this post-processing
// step adds them directly to the JSON.  Each GltfCameraEntry carries the
// original node name; we scan the exported nodes array for a matching
// "name" field and add "camera": <index> to that node.
bool GltfPostProcessor::writeGltfCameras(
    QJsonObject& gltfJson,
    const QVector<GltfCameraEntry>& cameras,
    std::function<void(const QString&)> logCallback)
{
    if (cameras.isEmpty())
        return false;

    log(QString("Writing %1 glTF camera(s)...").arg(cameras.size()), logCallback);

    QJsonArray camerasArray = gltfJson.value("cameras").toArray(); // preserve any existing
    QJsonArray nodesArray   = gltfJson.value("nodes").toArray();

    // Build a node-name → node-index lookup for fast matching.
    QMap<QString, int> nodeIndexByName;
    for (int ni = 0; ni < nodesArray.size(); ++ni)
    {
        const QString nodeName = nodesArray[ni].toObject().value("name").toString();
        if (!nodeName.isEmpty())
            nodeIndexByName[nodeName] = ni;
    }

    int injectedCount = 0;
    for (const GltfCameraEntry& cam : cameras)
    {
        const int cameraIndex = camerasArray.size();

        // --- Build camera definition ---
        QJsonObject camDef;
        camDef["name"] = cam.name.isEmpty() ? cam.nodeName : cam.name;

        if (cam.type == GltfCameraType::Perspective)
        {
            camDef["type"] = QStringLiteral("perspective");
            QJsonObject persp;
            persp["yfov"]  = static_cast<double>(cam.fovYRadians);
            persp["znear"] = static_cast<double>(cam.zNear);
            if (cam.zFar > 0.0f && cam.zFar < 1e38f)
                persp["zfar"] = static_cast<double>(cam.zFar);
            // aspectRatio is intentionally omitted — glTF consumers compute it
            // from the render target, and omitting it is spec-compliant.
            camDef["perspective"] = persp;
        }
        else // Orthographic
        {
            camDef["type"] = QStringLiteral("orthographic");
            QJsonObject ortho;
            ortho["xmag"]  = static_cast<double>(cam.xMag);
            ortho["ymag"]  = static_cast<double>(cam.yMag);
            ortho["znear"] = static_cast<double>(cam.zNear);
            ortho["zfar"]  = static_cast<double>(cam.zFar);
            camDef["orthographic"] = ortho;
        }

        camerasArray.append(camDef);

        // --- Captured-view cameras: no existing node to match against,
        // so build a brand-new root-level node from the world-space pose
        // instead (mirrors how KHR_lights_punctual nodes are synthesized
        // from GPULight position/direction in writePunctualLights() above).
        if (cam.needsNewNode)
        {
            const glm::vec3 dir = glm::normalize(
                glm::vec3(cam.worldDirection.x(), cam.worldDirection.y(), cam.worldDirection.z()));
            glm::vec3 up(cam.worldUp.x(), cam.worldUp.y(), cam.worldUp.z());
            glm::vec3 right = glm::normalize(glm::cross(dir, up));
            up = glm::normalize(glm::cross(right, dir));

            // glTF cameras look down local -Z with local +Y up, +X right -
            // build that basis as matrix columns and read off the rotation.
            const glm::mat3 basis(right, up, -dir);
            const glm::quat rot = glm::quat_cast(basis);

            QJsonObject newNode;
            newNode["name"] = camDef["name"];
            newNode["camera"] = cameraIndex;
            newNode["translation"] = QJsonArray{
                static_cast<double>(cam.worldPosition.x()),
                static_cast<double>(cam.worldPosition.y()),
                static_cast<double>(cam.worldPosition.z()) };
            newNode["rotation"] = QJsonArray{
                static_cast<double>(rot.x), static_cast<double>(rot.y),
                static_cast<double>(rot.z), static_cast<double>(rot.w) };

            const int newNodeIndex = nodesArray.size();
            nodesArray.append(newNode);

            if (gltfJson.contains("scenes"))
            {
                QJsonArray scenes = gltfJson["scenes"].toArray();
                const int sceneIdx = gltfJson.value("scene").toInt(0);
                if (sceneIdx >= 0 && sceneIdx < scenes.size())
                {
                    QJsonObject scene = scenes[sceneIdx].toObject();
                    QJsonArray sceneNodes = scene.value("nodes").toArray();
                    sceneNodes.append(newNodeIndex);
                    scene["nodes"] = sceneNodes;
                    scenes[sceneIdx] = scene;
                    gltfJson["scenes"] = scenes;
                }
            }

            log(QString("  -> Camera '%1' (%2) written to new node[%3]")
                .arg(camDef["name"].toString())
                .arg(cam.type == GltfCameraType::Perspective ? "perspective" : "orthographic")
                .arg(newNodeIndex), logCallback);
            ++injectedCount;
            continue;
        }

        // --- Wire the camera to its hosting node ---
        const QString targetNode = cam.nodeName.isEmpty() ? cam.name : cam.nodeName;
        if (nodeIndexByName.contains(targetNode))
        {
            const int ni = nodeIndexByName[targetNode];
            QJsonObject node = nodesArray[ni].toObject();
            node["camera"] = cameraIndex;
            nodesArray[ni] = node;
            log(QString("  -> Camera '%1' (%2) wired to node[%3] '%4'")
                .arg(camDef["name"].toString())
                .arg(cam.type == GltfCameraType::Perspective ? "perspective" : "orthographic")
                .arg(ni).arg(targetNode), logCallback);
            ++injectedCount;
        }
        else
        {
            log(QString("  -> Camera '%1': hosting node '%2' not found in exported hierarchy — camera definition added but not referenced")
                .arg(camDef["name"].toString()).arg(targetNode), logCallback);
        }
    }

    gltfJson["cameras"] = camerasArray;
    gltfJson["nodes"]   = nodesArray;

    log(QString("  -> %1/%2 camera(s) successfully wired to nodes")
        .arg(injectedCount).arg(cameras.size()), logCallback);
    return injectedCount > 0;
}

// Remove TANGENT attributes from all mesh primitives
// This works around an Assimp bug where glTF files with TANGENT attribute
// fail to load correctly in release builds (UVs get corrupted)
bool GltfPostProcessor::removeTangentAttributes(
    QJsonObject& gltfJson,
    std::function<void(const QString&)> logCallback)
{
    bool modified = false;

    if (!gltfJson.contains("meshes"))
        return false;

    QJsonArray meshes = gltfJson["meshes"].toArray();

    for (int i = 0; i < meshes.size(); ++i)
    {
        QJsonObject mesh = meshes[i].toObject();

        if (!mesh.contains("primitives"))
            continue;

        QJsonArray primitives = mesh["primitives"].toArray();

        for (int j = 0; j < primitives.size(); ++j)
        {
            QJsonObject prim = primitives[j].toObject();

            if (!prim.contains("attributes"))
                continue;

            QJsonObject attributes = prim["attributes"].toObject();

            // Remove TANGENT attribute if present
            if (attributes.contains("TANGENT"))
            {
                attributes.remove("TANGENT");
                prim["attributes"] = attributes;
                primitives[j] = prim;
                modified = true;

                log(QString("  -> Removed TANGENT from mesh %1, primitive %2")
                    .arg(i).arg(j), logCallback);
            }
        }

        if (modified)
        {
            mesh["primitives"] = primitives;
            meshes[i] = mesh;
        }
    }

    if (modified)
    {
        gltfJson["meshes"] = meshes;
        log("  -> TANGENT attributes removed from glTF", logCallback);
    }

    return modified;
}

// ============================================================================
// Material Signature Helper Functions
// ============================================================================

QString GltfPostProcessor::MaterialSignature::computeHash() const
{
    QString hash = name;

    // Include complete texture binding information (order matters for hash consistency)
    // Sort by texture type first, then path, to ensure consistent ordering
    std::vector<TextureBinding> sortedBindings = textureBindings;
    std::sort(sortedBindings.begin(), sortedBindings.end(),
        [](const TextureBinding& a, const TextureBinding& b) {
            if (a.textureType != b.textureType)
                return a.textureType < b.textureType;
            if (a.path != b.path)
                return a.path < b.path;
            if (a.texCoordIndex != b.texCoordIndex)
                return a.texCoordIndex < b.texCoordIndex;
            if (a.wrapS != b.wrapS)
                return a.wrapS < b.wrapS;
            if (a.wrapT != b.wrapT)
                return a.wrapT < b.wrapT;
            if (a.magFilter != b.magFilter)
                return a.magFilter < b.magFilter;
            return a.minFilter < b.minFilter;
        });

    // Hash each texture binding: type + path + texCoord + transforms
    for (const auto& binding : sortedBindings)
    {
        hash += QString("|%1::%2::%3:%4,%5:%6,%7:%8,%9")
            .arg(binding.textureType)                                    // texture type
            .arg(binding.path)                                           // texture path
            .arg(binding.texCoordIndex)                                  // texCoord index
            .arg(binding.rotationRad, 0, 'f', 4)                         // rotation
            .arg(binding.scale.x(), 0, 'f', 4)                           // scale X
            .arg(binding.scale.y(), 0, 'f', 4)                           // scale Y
            .arg(binding.offset.x(), 0, 'f', 4)                          // offset X
            .arg(binding.offset.y(), 0, 'f', 4)                          // offset Y
            .arg(binding.rotationRad > 0.001 ? "R" : "")                 // has rotation flag
            .arg(binding.wrapS)
            .arg(binding.wrapT)
            .arg(binding.magFilter)
            .arg(binding.minFilter);
    }

    return hash;
}

GltfPostProcessor::MaterialSignature GltfPostProcessor::buildSignatureForMesh(
    int meshIdx,
    const SceneMesh* mesh,
    std::function<void(const QString&)> logCallback)
{
    MaterialSignature sig;
    sig.meshIndex = meshIdx;
    const Material glMat = defaultMaterialForMesh(mesh);
    sig.name = glMat.name();
    sig.originalMaterialIndex = mesh->getOriginalMaterialIndex();

    // Lambda to extract complete texture binding data (type + path + texCoord + transforms)
    auto extractTextureBinding = [&](Material::TextureType type, const QString& typeStr) {
        const Material::Texture& tex = glMat.texture(type);
        if (!tex.path.empty()) {
            QString texPath = QString::fromStdString(tex.path);
            sig.textureFilePaths.insert(texPath);

            // Store complete texture binding information
            MaterialSignature::TextureBinding binding;
            binding.textureType = typeStr;
            binding.path = texPath;
            binding.texCoordIndex = tex.texCoordIndex;      // CRITICAL: texture coordinate index
            binding.rotationRad = tex.rotation;
            binding.scale = QVector2D(tex.scale.x, tex.scale.y);
            binding.offset = QVector2D(tex.offset.x, tex.offset.y);

            binding.wrapS = static_cast<int>(tex.wrapS);
            binding.wrapT = static_cast<int>(tex.wrapT);
            binding.magFilter = static_cast<int>(tex.magFilter);
            binding.minFilter = static_cast<int>(tex.minFilter);

            sig.textureBindings.push_back(binding);
        }
    };

    // Collect texture bindings from all material texture slots
    // Each binding is uniquely identified by type + path + texCoord
    extractTextureBinding(Material::TextureType::Albedo, "Albedo");
    extractTextureBinding(Material::TextureType::Normal, "Normal");
    extractTextureBinding(Material::TextureType::Metallic, "Metallic");
    extractTextureBinding(Material::TextureType::Roughness, "Roughness");
    extractTextureBinding(Material::TextureType::AmbientOcclusion, "AmbientOcclusion");
    extractTextureBinding(Material::TextureType::Emissive, "Emissive");

    return sig;
}

int GltfPostProcessor::computeTextureMatchScore(
    const MaterialSignature& sig,
    const QJsonObject& jsonMat)
{
    int score = 0;

    // Check if material has textures defined
    bool hasPBR = jsonMat.contains("pbrMetallicRoughness");
    if (hasPBR)
    {
        QJsonObject pbr = jsonMat["pbrMetallicRoughness"].toObject();
        if (pbr.contains("baseColorTexture"))
            score += 30;
        if (pbr.contains("metallicRoughnessTexture"))
            score += 25;
    }

    if (jsonMat.contains("normalTexture"))
        score += 25;
    if (jsonMat.contains("occlusionTexture"))
        score += 15;

    // Count JSON textures
    int jsonTexCount = 0;
    if (hasPBR)
    {
        QJsonObject pbr = jsonMat["pbrMetallicRoughness"].toObject();
        if (pbr.contains("baseColorTexture")) jsonTexCount++;
        if (pbr.contains("metallicRoughnessTexture")) jsonTexCount++;
    }
    if (jsonMat.contains("normalTexture")) jsonTexCount++;
    if (jsonMat.contains("occlusionTexture")) jsonTexCount++;

    // Bonus if texture count roughly matches
    int sigTexCount = sig.textureFilePaths.size();
    if (sigTexCount > 0 && jsonTexCount > 0)
    {
        if (sigTexCount >= jsonTexCount && sigTexCount <= jsonTexCount + 1)
            score += 20;
    }

    return score;
}

int GltfPostProcessor::findMaterialBySignature(
    const QString& jsonMatName,
    const QJsonObject& jsonMat,
    const std::vector<MaterialSignature>& signatures,
    int matIdx,
    std::function<void(const QString&)> logCallback)
{
    if (signatures.empty())
        return -1;

    // Find all candidates with matching name
    QVector<int> candidates;
    for (int i = 0; i < static_cast<int>(signatures.size()); ++i)
    {
        if (signatures[i].name == jsonMatName)
            candidates.push_back(i);
    }

    if (candidates.empty())
        return -1;

    // Single candidate - easy decision
    if (candidates.size() == 1)
    {
        int idx = candidates[0];
        log(QString("  Material[%1] '%2': Unique name match (signature %3)")
            .arg(matIdx).arg(jsonMatName).arg(idx), logCallback);
        return idx;
    }

    // Multiple candidates - score by texture match
    int bestIdx = candidates[0];
    int bestScore = computeTextureMatchScore(signatures[bestIdx], jsonMat);

    for (int idx : candidates)
    {
        int score = computeTextureMatchScore(signatures[idx], jsonMat);
        if (score > bestScore)
        {
            bestScore = score;
            bestIdx = idx;
        }
    }

    log(QString("  Material[%1] '%2': Selected by fingerprint (signature %3, score=%4)")
        .arg(matIdx).arg(jsonMatName).arg(bestIdx).arg(bestScore), logCallback);
    return bestIdx;
}

int GltfPostProcessor::findMaterialByNameWithDedup(
    const QString& jsonMatName,
    const std::vector<MaterialSignature>& signatures,
    int matIdx,
    std::function<void(const QString&)> logCallback)
{
    int matchCount = 0;
    int firstSigIdx = -1;

    for (int i = 0; i < static_cast<int>(signatures.size()); ++i)
    {
        if (signatures[i].name == jsonMatName)
        {
            matchCount++;
            if (firstSigIdx == -1)
            {
                firstSigIdx = i;
            }
        }
    }

    if (matchCount == 0)
        return -1;

    if (firstSigIdx < 0)
        return -1;

    if (matchCount > 1)
    {
        log(QString("  Material[%1] '%2': WARNING %3 instances found, using first (signature %4)")
            .arg(matIdx).arg(jsonMatName).arg(matchCount).arg(firstSigIdx), logCallback);
    }
    else
    {
        log(QString("  Material[%1] '%2': Name-based match (signature %3)")
            .arg(matIdx).arg(jsonMatName).arg(firstSigIdx), logCallback);
    }

    return firstSigIdx;
}

int GltfPostProcessor::findMaterialByIndexFallback(
    int matIdx,
    const std::vector<MaterialSignature>& signatures,
    std::function<void(const QString&)> logCallback)
{
    for (int i = 0; i < static_cast<int>(signatures.size()); ++i)
    {
        if (signatures[i].originalMaterialIndex == matIdx)
        {
            log(QString("  Material[%1]: Index fallback match (original index, signature %2)")
                .arg(matIdx).arg(i), logCallback);
            return i;
        }
    }
    return -1;
}

// ============================================================================

bool GltfPostProcessor::postProcessGltfJsonWithMaterials(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    const std::vector<GPULight>& lights,
    std::function<void(const QString&)> logCallback,
    const QString& textureSubfolder,
    const QMap<QString, QString>& pathMapping,
    const QMap<QString, int>& embeddedIndexMapping)
{
    log("=== glTF Post-Processor (with material transforms) ===", logCallback);

    // Store for use by findOrCreateTexture throughout this processing pass
    _textureSubfolder = textureSubfolder.isEmpty() ? "textures" : textureSubfolder;
    _pathMapping = pathMapping;
    _embeddedIndexMapping = embeddedIndexMapping;
    _materialToSourceMeshIndex.clear();

    removeTangentAttributes(gltfJson, logCallback);

    // Track which JSON material indices have been patched, and by which source mesh
    // (a material may be shared by multiple meshes - first mesh wins)
    QMap<int, int> patchedMaterials;  // json material index -> source mesh index

    // First, write actual transforms from source materials
    if (gltfJson.contains("materials") && !meshes.empty())
    {
        QJsonArray materials = gltfJson["materials"].toArray();
        QJsonArray images;
        QJsonArray textures;
        QJsonArray samplers;

        if (gltfJson.contains("images"))
            images = gltfJson["images"].toArray();
        if (gltfJson.contains("textures"))
            textures = gltfJson["textures"].toArray();
        if (gltfJson.contains("samplers"))
            samplers = gltfJson["samplers"].toArray();

        // Clear Assimp's samplers - we'll create correct ones from source materials
        samplers = QJsonArray();
        gltfJson["samplers"] = samplers;

        // ===== MATERIAL IDENTITY-BASED MATCHING =====
        // NEW APPROACH: Use originalMaterialIndex as the authoritative identifier
        //
        // Instead of trying to infer which source mesh became which JSON mesh via name queues,
        // we build a direct map from originalMaterialIndex to source mesh properties.
        // This survives Assimp's reordering because it's based on semantic identity, not position.
        //
        // The key insight: during import, each mesh captures its originalMaterialIndex.
        // This is the true material identity. We use it to rebuild the correct material
        // assignments during export, bypassing deduplication ambiguity.

        // Build material identity map: originalMaterialIndex -> source mesh index with best match
        QMap<int, int> origMatIdxToMeshIdx;  // originalMaterialIndex -> preferred source mesh index

        for (int k = 0; k < static_cast<int>(meshes.size()); ++k)
        {
            if (!meshes[k]) continue;

            int origMatIdx = meshes[k]->getOriginalMaterialIndex();

            // For each originalMaterialIndex, keep track of a source mesh that uses it
            // Prefer the first mesh we encounter for each index
            if (origMatIdx >= 0 && !origMatIdxToMeshIdx.contains(origMatIdx))
            {
                origMatIdxToMeshIdx[origMatIdx] = k;
                log(QString("  Material Identity: originalMaterialIndex[%1] -> source mesh[%2] (%3)")
                    .arg(origMatIdx).arg(k).arg(meshes[k]->getName()), logCallback);
            }
        }

        // Build mesh name -> source mesh index map for fast lookup by name
        QMap<QString, QList<int>> meshNameToIndices;
        for (int k = 0; k < static_cast<int>(meshes.size()); ++k)
        {
            if (!meshes[k]) continue;
            QString fullName = meshes[k]->getName();
            if (!fullName.isEmpty())
            {
                meshNameToIndices[fullName].append(k);
            }
        }

        // Build material name -> source mesh index map for direct material-name-based matching.
        // This is the primary resolution path: JSON material names are set from source mesh
        // material names during export, so they are unique and authoritative.
        QMap<QString, int> materialNameToSourceMeshIdx;
        for (int k = 0; k < static_cast<int>(meshes.size()); ++k)
        {
            if (!meshes[k]) continue;
            QString matName = defaultMaterialForMesh(meshes[k]).name();
            if (!matName.isEmpty() && !materialNameToSourceMeshIdx.contains(matName))
            {
                materialNameToSourceMeshIdx[matName] = k;
                log(QString("  Material Name Map: '%1' -> source mesh[%2]").arg(matName).arg(k), logCallback);
            }
        }

        log(QString("  Material Identity Map: %1 unique material indices").arg(origMatIdxToMeshIdx.size()), logCallback);
        log(QString("  Material Name Map: %1 unique material names").arg(materialNameToSourceMeshIdx.size()), logCallback);
        log(QString("  Mesh Name Lookup: %1 named mesh groups").arg(meshNameToIndices.size()), logCallback);
        log(QString("  [SOURCE MESHES] meshes.size()=%1").arg(meshes.size()), logCallback);

        // We may need to update the JSON meshes array (to redirect primitive material
        // pointers when we clone a collapsed material slot). Work on a mutable copy.
        QJsonArray jsonMeshes = gltfJson.value("meshes").toArray();
        log(QString("  [JSON MESHES] jsonMeshes.size()=%1").arg(jsonMeshes.size()), logCallback);

        auto resolveSourceMeshIndex = [&](const QString& targetName,
                                          int jsonMeshIndex,
                                          const QString& matchLabel) -> int
        {
            if (meshNameToIndices.contains(targetName))
            {
                const QList<int>& candidates = meshNameToIndices[targetName];
                if (!candidates.isEmpty())
                {
                    int resolved = candidates[0];
                    log(QString("  [%1] json mesh[%2] '%3' -> source mesh[%4] (exact match)")
                        .arg(matchLabel).arg(jsonMeshIndex).arg(targetName).arg(resolved), logCallback);
                    return resolved;
                }
            }

            for (auto it = meshNameToIndices.begin(); it != meshNameToIndices.end(); ++it)
            {
                const QString& sourceName = it.key();
                const QList<int>& candidates = it.value();
                if (!candidates.isEmpty() &&
                    (sourceName.contains(targetName) || targetName.contains(sourceName)))
                {
                    int resolved = candidates[0];
                    log(QString("  [%1] json mesh[%2] '%3' -> source mesh[%4] (partial: '%5')")
                        .arg(matchLabel).arg(jsonMeshIndex).arg(targetName).arg(resolved).arg(sourceName), logCallback);
                    return resolved;
                }
            }

            return -1;
        };

        for (int j = 0; j < jsonMeshes.size(); ++j)
        {
            QJsonObject jMesh = jsonMeshes[j].toObject();
            QString meshName = jMesh["name"].toString();

            QJsonArray primitives = jMesh["primitives"].toArray();
            if (primitives.isEmpty()) continue;

            log(QString("[MESH LOOP] Processing JSON mesh[%1] name='%2' primitives=%3")
                .arg(j).arg(meshName).arg(primitives.size()), logCallback);

            int baseMeshIdx = resolveSourceMeshIndex(meshName, j, "NAME MATCH");
            if (baseMeshIdx < 0)
            {
                log(QString("  WARNING: no source mesh found for json mesh '%1' (index %2)")
                    .arg(meshName).arg(j), logCallback);
                continue;
            }

            const int suffixSeparator = meshName.lastIndexOf('-');
            const QString primitiveNamePrefix = suffixSeparator >= 0 ? meshName.left(suffixSeparator + 1) : meshName;

            // ===== MATERIAL SLOT ASSIGNMENT =====
            // Assign each primitive independently. Assimp can collapse multiple source
            // meshes into one JSON mesh with several primitives, so primitive[0] and
            // primitive[1] may need different source material identities.
            for (int primIdx = 0; primIdx < primitives.size(); ++primIdx)
            {
                QJsonObject prim = primitives[primIdx].toObject();
                int matIdx = prim.value("material").toInt(-1);
                if (matIdx < 0 || matIdx >= materials.size())
                    continue;

                QString targetSourceName = meshName;
                int meshIdx = baseMeshIdx;

                // Disambiguation: when multiple source meshes share the same mesh name
                // (e.g. King_Black / King_White sharing "King_Shared" geometry), the mesh-name
                // lookup is ambiguous and always returns candidates[0]. In that case, fall back
                // to matching by the JSON material's name, which is unique per export because it
                // comes from Material::name(). Only apply this when the mesh name is genuinely
                // ambiguous — if mesh names are unique, the baseMeshIdx resolution is already correct.
                {
                    bool meshNameIsAmbiguous = meshNameToIndices.contains(meshName) &&
                                               meshNameToIndices[meshName].size() > 1;
                    if (meshNameIsAmbiguous)
                    {
                        QString jsonMatName = materials[matIdx].toObject()["name"].toString();
                        if (!jsonMatName.isEmpty() && materialNameToSourceMeshIdx.contains(jsonMatName))
                        {
                            meshIdx = materialNameToSourceMeshIdx[jsonMatName];
                            log(QString("  [MAT NAME] json mesh[%1] primitive[%2] material[%3] '%4' -> source mesh[%5] (mesh name was ambiguous)")
                                .arg(j).arg(primIdx).arg(matIdx).arg(jsonMatName).arg(meshIdx), logCallback);
                        }
                    }
                }

                if (primitives.size() > 1 && suffixSeparator >= 0)
                {
                    QString primitiveSpecificName = primitiveNamePrefix + QString::number(primIdx);
                    int primitiveMeshIdx = resolveSourceMeshIndex(
                        primitiveSpecificName, j, QString("PRIMITIVE %1").arg(primIdx));
                    if (primitiveMeshIdx >= 0)
                    {
                        targetSourceName = primitiveSpecificName;
                        meshIdx = primitiveMeshIdx;
                    }
                }

                // For consolidated meshes without suffix-based names (e.g. STEP/FBX files where
                // Assimp groups many source meshes into one JSON mesh with N primitives),
                // use sequential offset from baseMeshIdx. applyMaterialsToScene assigns
                // mMaterialIndex=i per source mesh, so primitive[k] was created from meshes[baseMeshIdx+k].
                if (primitives.size() > 1 && suffixSeparator < 0 && primIdx > 0)
                {
                    int candidateMeshIdx = baseMeshIdx + primIdx;
                    if (candidateMeshIdx < static_cast<int>(meshes.size()))
                    {
                        meshIdx = candidateMeshIdx;
                        log(QString("  [PRIM OFFSET] primitive[%1] -> source mesh[%2] (base=%3 + offset)")
                            .arg(primIdx).arg(meshIdx).arg(baseMeshIdx), logCallback);
                    }
                }

                if (meshIdx < 0 || meshIdx >= static_cast<int>(meshes.size()))
                    continue;

                int origMatIdx = meshes[meshIdx]->getOriginalMaterialIndex();
                log(QString("  [MATCH] json mesh[%1] primitive[%2] name='%3' material[%4] -> source mesh[%5] origMatIdx=%6 matName='%7'")
                    .arg(j).arg(primIdx).arg(targetSourceName).arg(matIdx).arg(meshIdx)
                    .arg(origMatIdx).arg(defaultMaterialForMesh(meshes[meshIdx]).name()), logCallback);

                if (!patchedMaterials.contains(matIdx))
                {
                    patchedMaterials[matIdx] = meshIdx;
                    log(QString("  Assigned: json mesh[%1] primitive[%2] '%3' -> material[%4] (origMatIdx=%5)")
                        .arg(j).arg(primIdx).arg(targetSourceName).arg(matIdx).arg(origMatIdx), logCallback);
                }
                else if (patchedMaterials[matIdx] == meshIdx)
                {
                    log(QString("  Shared: json mesh[%1] primitive[%2] '%3' -> material[%4] (same source mesh)")
                        .arg(j).arg(primIdx).arg(targetSourceName).arg(matIdx), logCallback);
                }
                else
                {
                    int existingMeshIdx = patchedMaterials[matIdx];
                    int clonedIdx = materials.size();
                    materials.append(materials[matIdx]);  // deep copy

                    prim["material"] = clonedIdx;
                    primitives[primIdx] = prim;
                    jMesh["primitives"] = primitives;
                    jsonMeshes[j] = jMesh;

                    patchedMaterials[clonedIdx] = meshIdx;
                    log(QString("  Cloned: json mesh[%1] primitive[%2] '%3' -> material[%4]→[%5] (origMatIdx=%6 vs existing origMatIdx=%7)")
                        .arg(j).arg(primIdx).arg(targetSourceName).arg(matIdx).arg(clonedIdx).arg(origMatIdx)
                        .arg(meshes[existingMeshIdx]->getOriginalMaterialIndex()), logCallback);
                }
            }  // end primitive material slot assignment
        }  // end for jsonMeshes

        // Write back the (possibly updated) meshes array so primitive material
        // redirections are visible to the rest of the pipeline.
        gltfJson["meshes"] = jsonMeshes;
        _materialToSourceMeshIndex = patchedMaterials;

        // Helper: write KHR_texture_transform from a Material::Texture
        auto writeTransform = [&](QJsonObject& parent, const QString& key, const Material::Texture& tex) -> bool {
            if (!parent.contains(key))
            {
                log(QString("    writeTransform(%1): key not in parent").arg(key), logCallback);
                return false;
            }

            log(QString("    writeTransform(%1): scale=[%2,%3] offset=[%4,%5] rotation=%6 texCoord=%7")
                .arg(key).arg(tex.scale.x).arg(tex.scale.y)
                .arg(tex.offset.x).arg(tex.offset.y).arg(tex.rotation).arg(tex.texCoordIndex), logCallback);

            bool hasTransform = (tex.scale.x != 1.0f || tex.scale.y != 1.0f ||
                tex.offset.x != 0.0f || tex.offset.y != 0.0f ||
                tex.rotation != 0.0f || tex.texCoordIndex != 0);

            if (!hasTransform)
            {
                log(QString("      -> No transform (all defaults)"), logCallback);
                return false;
            }

            QJsonObject texInfo = parent[key].toObject();
            QJsonObject extensions = texInfo.value("extensions").toObject();
            QJsonArray scale, offset;
            scale.append(static_cast<double>(tex.scale.x));
            scale.append(static_cast<double>(tex.scale.y));
            offset.append(static_cast<double>(tex.offset.x));
            offset.append(static_cast<double>(tex.offset.y));

            QJsonObject transform;
            transform["scale"] = scale;
            transform["offset"] = offset;
            transform["rotation"] = static_cast<double>(tex.rotation);
            if (tex.texCoordIndex != 0)
                transform["texCoord"] = tex.texCoordIndex;

            extensions["KHR_texture_transform"] = transform;
            texInfo["extensions"] = extensions;
            if (tex.texCoordIndex != 0)
                texInfo["texCoord"] = tex.texCoordIndex;

            parent[key] = texInfo;
            log(QString("      -> KHR_texture_transform written"), logCallback);
            return true;
            };

        // Helper: get or create a sampler
        QMap<QString, int> samplerConfigToIndex;
        QVector<QJsonObject> createdSamplers;
        int samplerBaseIndex = samplers.size();  // Track where new samplers will start

       auto getOrCreateSampler = [&](int mag, int min, int wrapS, int wrapT) -> int {
            QString hash = QString("%1_%2_%3_%4").arg(mag).arg(min).arg(wrapS).arg(wrapT);
            if (samplerConfigToIndex.contains(hash))
                return samplerConfigToIndex[hash] + samplerBaseIndex;  // Offset by base

            int idx = createdSamplers.size();
            QJsonObject sampler;
            sampler["magFilter"] = mag;
            sampler["minFilter"] = min;
            sampler["wrapS"] = wrapS;
            sampler["wrapT"] = wrapT;
            createdSamplers.append(sampler);

            int finalIdx = idx + samplerBaseIndex;
            samplerConfigToIndex[hash] = idx;

            // Keep JSON in sync immediately so later passes don't see stale sampler count.
            QJsonArray currentSamplers = gltfJson.value("samplers").toArray();
            while (currentSamplers.size() < samplerBaseIndex)
                currentSamplers.append(QJsonObject());
            currentSamplers.append(sampler);
            gltfJson["samplers"] = currentSamplers;
            samplers = currentSamplers;

            log(QString("    Sampler[%1]: mag=%2, min=%3, wrapS=%4, wrapT=%5 (final index: %6)")
                .arg(idx).arg(mag).arg(min).arg(wrapS).arg(wrapT).arg(finalIdx), logCallback);
            return finalIdx;
            };

       auto desiredSamplerForSourceTex = [&](const Material::Texture& sourceTex) -> int {
           return getOrCreateSampler(
               static_cast<int>(sourceTex.magFilter),
               static_cast<int>(sourceTex.minFilter),
               static_cast<int>(sourceTex.wrapS),
               static_cast<int>(sourceTex.wrapT));
           };

        // Helper: find or create a texture entry for the same image source + sampler.
        // IMPORTANT:
        // glTF texture objects are (source image + sampler) pairs.
        // If two materials need the same image with different samplers,
        // we must create separate texture entries instead of overwriting
        // the shared texture's sampler in-place.
        auto getOrCreateTextureVariant = [&](const QJsonObject& baseTexture,
            int desiredSamplerIdx) -> int {
                int sourceIdx = baseTexture.value("source").toInt(-1);
                if (sourceIdx < 0)
                    return -1;

                for (int i = 0; i < textures.size(); ++i)
                {
                    QJsonObject existingTex = textures[i].toObject();
                    int existingSource = existingTex.value("source").toInt(-1);
                    int existingSampler = existingTex.contains("sampler")
                        ? existingTex.value("sampler").toInt(-1)
                        : -1;

                    if (existingSource == sourceIdx && existingSampler == desiredSamplerIdx)
                        return i;
                }

                QJsonObject newTex = baseTexture;
                newTex["source"] = sourceIdx;
                newTex["sampler"] = desiredSamplerIdx;

                int newIndex = textures.size();
                textures.append(newTex);


                // CRITICAL: keep gltfJson["textures"] in sync immediately.
                            // findOrCreateTexture() re-reads textures from gltfJson every time,
                            // so if we only mutate the local 'textures' array, later texture
                            // creations can reuse stale indices and break material references.
                gltfJson["textures"] = textures;


                log(QString("    Created texture variant[%1] for source[%2] with sampler[%3]")
                    .arg(newIndex).arg(sourceIdx).arg(desiredSamplerIdx), logCallback);

                return newIndex;
            };

        // Helper: update texture sampler
        // NOTE:
        // Do NOT overwrite textures[texIndex]["sampler"] directly, because that
        // can break other materials sharing the same texture entry.
        auto updateTextureSampler = [&](QJsonObject& parent,
            const QString& key,
            const Material::Texture& sourceTex) {
                if (!parent.contains(key))
                    return;

                QJsonObject texInfo = parent[key].toObject();
                int texIndex = texInfo.value("index").toInt(-1);
                if (texIndex < 0 || texIndex >= textures.size())
                    return;

                int samplerIdx = getOrCreateSampler(
                    sourceTex.magFilter,
                    sourceTex.minFilter,
                    sourceTex.wrapS,
                    sourceTex.wrapT);

                QJsonObject currentTex = textures[texIndex].toObject();
                int currentSamplerIdx = currentTex.contains("sampler")
                    ? currentTex.value("sampler").toInt(-1)
                    : -1;

                if (currentSamplerIdx == samplerIdx)
                {
                    log(QString("    %1: texture[%2] already uses sampler[%3]")
                        .arg(key).arg(texIndex).arg(samplerIdx), logCallback);
                    return;
                }

                int variantTexIndex = getOrCreateTextureVariant(currentTex, samplerIdx);
                if (variantTexIndex < 0)
                    return;

                texInfo["index"] = variantTexIndex;
                parent[key] = texInfo;

                log(QString("    %1: texture[%2] -> texture variant[%3] with sampler[%4]")
                    .arg(key).arg(texIndex).arg(variantTexIndex).arg(samplerIdx), logCallback);
            };

        // Build material signatures from all source meshes for robust matching
        // CRITICAL: Keep full signatures indexed by mesh index for primary matching!
        // Do NOT deduplicate before patching, as that breaks the index correspondence.
        log("=== Building Material Signatures ===", logCallback);
        std::vector<MaterialSignature> signatures;  // Full array: signatures[i] = signature for mesh i
        for (int i = 0; i < static_cast<int>(meshes.size()); ++i)
        {
            MaterialSignature sig = buildSignatureForMesh(i, meshes[i], logCallback);
            signatures.push_back(sig);
            log(QString("  Signature[%1]: name='%2' textures=%3 originalIdx=%4")
                .arg(i).arg(sig.name).arg(sig.textureFilePaths.size())
                .arg(sig.originalMaterialIndex), logCallback);
        }

        log(QString("  RESULT: %1 material signatures built (one per mesh)")
            .arg(signatures.size()), logCallback);

        // Now patch each material that was mapped from a source mesh
        log("=== Patching Materials with Signature Matching ===", logCallback);

        // Note: jsonMatToSignatureMap removed (consolidation disabled)

        // ===== FIX TEXTURE INDICES =====
            // Assimp reorders meshes during export, so texture indices in the
            // exported JSON may point to the wrong images. We correct them by
            // finding the image index that matches the source Material's texture path.

        auto fixTextureIndex = [&](QJsonObject& parent, const QString& key,
            const Material::Texture sourceTex) {
                if (!parent.contains(key)) return;
                if (sourceTex.path.empty()) return;

                // Get just the filename from the full source path
                QString sourcePath = QString::fromStdString(sourceTex.path);
                QString sourceFilename = QFileInfo(normalisedGlbPath(sourcePath)).fileName();

                const int desiredSamplerIdx = desiredSamplerForSourceTex(sourceTex);

                // Find the image index in the JSON images array with a matching filename
                int correctImageIdx = -1;

                auto embeddedIt = _embeddedIndexMapping.find(sourcePath);
                if (embeddedIt != _embeddedIndexMapping.end() &&
                    embeddedIt.value() >= 0 &&
                    embeddedIt.value() < images.size())
                {
                    correctImageIdx = embeddedIt.value();
                    log(QString("    Using authoritative embedded image index %1 for '%2'")
                        .arg(correctImageIdx).arg(sourcePath), logCallback);
                }

                for (int ii = 0; ii < images.size(); ++ii)
                {
                    if (correctImageIdx >= 0)
                        break;
                    QString imgUri = images[ii].toObject().value("uri").toString();
                    if (QFileInfo(imgUri).fileName().compare(sourceFilename, Qt::CaseInsensitive) == 0 ||
                        QFileInfo(imgUri).completeBaseName().compare(sourceFilename, Qt::CaseInsensitive) == 0)
                    {
                        correctImageIdx = ii;
                        break;
                    }
                }

                // GLB fallback: images have no URI, match by "name" field instead.
                // Resolve original path through pathMapping to get the packaged filename.
                if (correctImageIdx < 0 && !_pathMapping.isEmpty())
                {
                    QString packagedRelPath = resolvePackagedPath(_pathMapping, sourcePath);
                    QString packagedName = QFileInfo(packagedRelPath).fileName();
                    if (!packagedName.isEmpty())
                    {
                        for (int ii = 0; ii < images.size(); ++ii)
                        {
                            QString imgName = images[ii].toObject().value("name").toString();
                            if (!imgName.isEmpty() &&
                                QFileInfo(imgName).fileName().compare(packagedName, Qt::CaseInsensitive) == 0)
                            {
                                correctImageIdx = ii;
                                break;
                            }
                        }
                    }
                }

                if (correctImageIdx < 0)
                {
                    // Image not exported by Assimp - create it via findOrCreateTexture
                    int newTexIdx = findOrCreateTexture(gltfJson, sourcePath, -1, logCallback);
                    if (newTexIdx < 0)
                    {
                        log(QString("    WARNING: Could not create texture for '%1'").arg(sourceFilename), logCallback);
                        return;
                    }
                    // Reload images/textures arrays since findOrCreateTexture may have modified gltfJson
                    images = gltfJson.value("images").toArray();
                    textures = gltfJson.value("textures").toArray();
                    QJsonObject texInfo = parent[key].toObject();
                    texInfo["index"] = newTexIdx;
                    parent[key] = texInfo;
                    log(QString("    Created missing image+texture for '%1' -> tex[%2]")
                        .arg(sourceFilename).arg(newTexIdx), logCallback);
                    return;
                }

                // Find or create a texture entry pointing to this image
                // (reuse existing if same source+sampler, otherwise create new)
                QJsonObject texInfo = parent[key].toObject();
                int currentTexIdx = texInfo.value("index").toInt(-1);

                // Check if current texture already points to the right image
                // AND already uses the right sampler.
                if (currentTexIdx >= 0 && currentTexIdx < textures.size())
                {
                    QJsonObject currentTexObj = textures[currentTexIdx].toObject();
                    int currentSource = currentTexObj.value("source").toInt(-1);
                    int currentSampler = currentTexObj.contains("sampler")
                        ? currentTexObj.value("sampler").toInt(-1)
                        : -1;

                    if (currentSource == correctImageIdx && currentSampler == desiredSamplerIdx)
                        return; // Already correct
                }

                // Find an existing texture entry pointing to correctImageIdx,
                // or create a new one
                int correctTexIdx = -1;
                for (int ti = 0; ti < textures.size(); ++ti)
                {
                    QJsonObject existingTexObj = textures[ti].toObject();
                    int existingSource = existingTexObj.value("source").toInt(-1);
                    int existingSampler = existingTexObj.contains("sampler")
                        ? existingTexObj.value("sampler").toInt(-1)
                        : -1;

                    if (existingSource == correctImageIdx && existingSampler == desiredSamplerIdx)
                    {
                        correctTexIdx = ti;
                        break;
                    }
                }

                if (correctTexIdx < 0)
                {
                    // Create a new texture entry for the correct image + sampler pair
                    QJsonObject newTex;
                    newTex["source"] = correctImageIdx;
                    newTex["sampler"] = desiredSamplerIdx;

                    correctTexIdx = textures.size();
                    textures.append(newTex);
                    gltfJson["textures"] = textures; // keep JSON in sync immediately

                    log(QString("    Created texture[%1] for image[%2] '%3' with sampler[%4]")
                        .arg(correctTexIdx).arg(correctImageIdx).arg(sourceFilename).arg(desiredSamplerIdx),
                        logCallback);
                }

                texInfo["index"] = correctTexIdx;
                parent[key] = texInfo;

                log(QString("    Fixed %1: texture index %2 -> %3 (image '%4')")
                    .arg(key).arg(currentTexIdx).arg(correctTexIdx).arg(sourceFilename), logCallback);
            };

        for (auto it = patchedMaterials.begin(); it != patchedMaterials.end(); ++it)
        {
            int matIdx = it.key();
            int meshIdx = it.value();  // Source mesh index from our direct mapping

            QJsonObject mat = materials[matIdx].toObject();
            QString jsonMatName = mat["name"].toString();

            log(QString("Material[%1] '%2': from source mesh[%3] (origMatIdx=%4):").arg(matIdx).arg(jsonMatName).arg(meshIdx)
                .arg(meshIdx >= 0 && meshIdx < static_cast<int>(meshes.size()) ? meshes[meshIdx]->getOriginalMaterialIndex() : -1), logCallback);

            // NEW STRATEGY: Use the meshIdx from our direct mapping as PRIMARY source
            // This is the authoritative mapping built during mesh-to-material assignment.
            // All signature matching is now secondary/fallback.

            int matchedSigIdx = -1;

            // Primary: Direct mapping - find signature for this exact source mesh
            if (meshIdx >= 0 && meshIdx < static_cast<int>(signatures.size()))
            {
                if (signatures[meshIdx].meshIndex == meshIdx)
                {
                    matchedSigIdx = meshIdx;
                    log(QString("  [DIRECT MATCH] Using source mesh[%1] signature directly").arg(meshIdx), logCallback);
                }
            }

            // Secondary fallback: Try fingerprint matching if direct didn't work
            if (matchedSigIdx < 0)
            {
                matchedSigIdx = findMaterialBySignature(
                    jsonMatName, mat, signatures, matIdx, logCallback);
            }

            // Tertiary fallback: Name matching with dedup
            if (matchedSigIdx < 0)
            {
                matchedSigIdx = findMaterialByNameWithDedup(
                    jsonMatName, signatures, matIdx, logCallback);
            }

            // Quaternary fallback: Index-based matching
            if (matchedSigIdx < 0)
            {
                matchedSigIdx = findMaterialByIndexFallback(matIdx, signatures, logCallback);
            }

            // Ultimate fallback: use the meshIdx we already have
            if (matchedSigIdx < 0)
            {
                if (meshIdx >= 0 && meshIdx < static_cast<int>(meshes.size()))
                {
                    matchedSigIdx = meshIdx;
                    log(QString("  [FINAL FALLBACK] Using mapped mesh[%1] as signature").arg(meshIdx), logCallback);
                }
                else
                {
                    log(QString("  ERROR: No material signature found and meshIdx=%1 invalid!").arg(meshIdx), logCallback);
                    continue;
                }
            }

            if (matchedSigIdx < 0 || matchedSigIdx >= static_cast<int>(signatures.size()))
            {
                log(QString("  ERROR: Invalid signature index %1").arg(matchedSigIdx), logCallback);
                continue;
            }

            const MaterialSignature& matchedSig = signatures[matchedSigIdx];
            log(QString("  -> Using signature '%1'").arg(matchedSig.name), logCallback);

            // Validate we can access the source material
            int sourceMeshIdx = matchedSig.meshIndex;
            if (sourceMeshIdx < 0 || sourceMeshIdx >= static_cast<int>(meshes.size()))
            {
                log(QString("  ERROR: Invalid mesh index %1 in signature").arg(sourceMeshIdx), logCallback);
                continue;
            }

            if (!meshes[sourceMeshIdx])
            {
                log(QString(" ERROR: meshes[%1] is null").arg(sourceMeshIdx), logCallback);
                continue;
            }

            // IMPORTANT: take a stable local copy.
            // getMaterial() may return by value; never bind that to a returned reference.
            Material sourceMaterial = defaultMaterialForMesh(meshes[sourceMeshIdx]);

            // ===== FIX BASE COLOR =====
            // Assimp's glTF exporter doesn't properly export material colors from aiMaterial.
            // We patch the baseColorFactor directly in the JSON with the correct albedoColor from Material.
            {
                int meshIdx = matchedSig.meshIndex;
                if (meshIdx < 0 || meshIdx >= static_cast<int>(meshes.size()))
                {
                    log(QString("  WARNING: Cannot access mesh for base color, skipping"), logCallback);
                }
                else
                {
                    QVector3D albedo = sourceMaterial.albedoColor();
                    if (mat.contains("pbrMetallicRoughness"))
                    {
                        QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                        QJsonArray baseColorFactor;
                        baseColorFactor.append(static_cast<double>(albedo.x()));
                        baseColorFactor.append(static_cast<double>(albedo.y()));
                        baseColorFactor.append(static_cast<double>(albedo.z()));
                        baseColorFactor.append(1.0);  // alpha
                        pbr["baseColorFactor"] = baseColorFactor;
                        mat["pbrMetallicRoughness"] = pbr;

                        log(QString("    Patched baseColorFactor: [%1, %2, %3]")
                            .arg(albedo.x(), 0, 'f', 4)
                            .arg(albedo.y(), 0, 'f', 4)
                            .arg(albedo.z(), 0, 'f', 4), logCallback);
                    }
                }
            }

            // Fix texture indices for all slots before writing transforms/samplers
            if (mat.contains("pbrMetallicRoughness"))
            {
                QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                fixTextureIndex(pbr, "baseColorTexture", sourceMaterial.texture(Material::TextureType::Albedo));

                // SKIP fixTextureIndex for metallicRoughnessTexture if:
                // 1. Metallic and roughness are separate files (packing was done in exporter)
                // 2. Metallic is missing (roughness-only material - don't apply empty metallic texture)
                const auto& metallicTex = sourceMaterial.texture(Material::TextureType::Metallic);
                const auto& roughnessTex = sourceMaterial.texture(Material::TextureType::Roughness);
                bool hasPackedMR = (!metallicTex.path.empty() && !roughnessTex.path.empty() &&
                                     metallicTex.path != roughnessTex.path);
                bool isRoughnessOnly = (metallicTex.path.empty() && !roughnessTex.path.empty());

                if (!hasPackedMR && !isRoughnessOnly)
                {
                    // Only fix M/R texture if not packed AND metallic exists
                    fixTextureIndex(pbr, "metallicRoughnessTexture", metallicTex);
                }
                // else: skip to avoid corrupting metallicRoughnessTexture (packed M/R or roughness-only)

                mat["pbrMetallicRoughness"] = pbr;
            }
            fixTextureIndex(mat, "normalTexture", sourceMaterial.texture(Material::TextureType::Normal));

            // Handle AO for packed ORM: set occlusionTexture to point to the packed ORM texture
            // (packed ORM has AO data in R channel, so occlusionTexture should use it instead of original AO)
            bool handledAsPackedORM = false;
            if (mat.contains("pbrMetallicRoughness"))
            {
                QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                if (pbr.contains("metallicRoughnessTexture"))
                {
                    QJsonObject mrObj = pbr["metallicRoughnessTexture"].toObject();
                    int texIdx = mrObj.value("index").toInt(-1);
                    log(QString("    [ORM Detection] M/R texture index: %1, total textures: %2").arg(texIdx).arg(textures.size()), logCallback);
                    if (texIdx >= 0 && texIdx < textures.size())
                    {
                        QJsonObject texObj = textures[texIdx].toObject();
                        int imgIdx = texObj.value("source").toInt(-1);
                        log(QString("    [ORM Detection] M/R image source: %1, total images: %2").arg(imgIdx).arg(images.size()), logCallback);
                        if (imgIdx >= 0 && imgIdx < images.size())
                        {
                            QString imgUri = images[imgIdx].toObject().value("uri").toString();
                            log(QString("    [ORM Detection] M/R image URI: %1").arg(imgUri), logCallback);
                            if (imgUri.contains("_packed_orm"))
                            {
                                // Set occlusionTexture to use the same packed ORM texture as metallicRoughnessTexture
                                QJsonObject occlusionObj;
                                occlusionObj["index"] = texIdx;  // Point to same texture as M/R
                                mat["occlusionTexture"] = occlusionObj;
                                handledAsPackedORM = true;
                                log("    [ORM Detection] MATCHED! Set occlusionTexture to packed ORM texture (same as metallicRoughnessTexture)", logCallback);
                            }
                            else
                            {
                                log(QString("    [ORM Detection] No match - URI does not contain '_packed_orm_'"), logCallback);
                            }
                        }
                        else
                        {
                            log(QString("    [ORM Detection] Invalid image index"), logCallback);
                        }
                    }
                    else
                    {
                        log(QString("    [ORM Detection] Invalid texture index"), logCallback);
                    }
                }
                else
                {
                    log(QString("    [ORM Detection] No metallicRoughnessTexture found"), logCallback);
                }
            }
            else
            {
                log(QString("    [ORM Detection] No pbrMetallicRoughness found"), logCallback);
            }

            if (!handledAsPackedORM)
            {
                log(QString("    [ORM Detection] Not handled as packed ORM - calling fixTextureIndex for original AO"), logCallback);
                fixTextureIndex(mat, "occlusionTexture", sourceMaterial.texture(Material::TextureType::AmbientOcclusion));
            }

            // Set occlusion texture strength from source material
            if (mat.contains("occlusionTexture"))
            {
                QJsonObject texInfo = mat["occlusionTexture"].toObject();
                float aoStrength = sourceMaterial.occlusionStrength();
                if (!texInfo.contains("strength") || texInfo.value("strength").toDouble() != aoStrength)
                {
                    texInfo["strength"] = static_cast<double>(aoStrength);
                    mat["occlusionTexture"] = texInfo;
                    log(QString("    Set occlusionTexture.strength to %1").arg(aoStrength), logCallback);
                }
            }

            fixTextureIndex(mat, "emissiveTexture", sourceMaterial.texture(Material::TextureType::Emissive));

            // --- Texture transforms ---
            bool transformsWritten = false;
            if (mat.contains("pbrMetallicRoughness"))
            {
                QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                if (writeTransform(pbr, "baseColorTexture", sourceMaterial.texture(Material::TextureType::Albedo)))
                    transformsWritten = true;
                if (writeTransform(pbr, "metallicRoughnessTexture", sourceMaterial.texture(Material::TextureType::Metallic)))
                    transformsWritten = true;
                if (transformsWritten)
                    mat["pbrMetallicRoughness"] = pbr;
            }
            if (writeTransform(mat, "normalTexture", sourceMaterial.texture(Material::TextureType::Normal)))
                transformsWritten = true;
            // Skip occlusion texture transform if we handled it as packed ORM
            // (the packed ORM texture transform comes from metallicRoughnessTexture, not from AO)
            if (!handledAsPackedORM && writeTransform(mat, "occlusionTexture", sourceMaterial.texture(Material::TextureType::AmbientOcclusion)))
                transformsWritten = true;
            if (writeTransform(mat, "emissiveTexture", sourceMaterial.texture(Material::TextureType::Emissive)))
                transformsWritten = true;

            // --- Samplers ---
            if (mat.contains("pbrMetallicRoughness"))
            {
                QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                updateTextureSampler(pbr, "baseColorTexture", sourceMaterial.texture(Material::TextureType::Albedo));
                updateTextureSampler(pbr, "metallicRoughnessTexture", sourceMaterial.texture(Material::TextureType::Metallic));
            }
            updateTextureSampler(mat, "normalTexture", sourceMaterial.texture(Material::TextureType::Normal));
            updateTextureSampler(mat, "occlusionTexture", sourceMaterial.texture(Material::TextureType::AmbientOcclusion));
            updateTextureSampler(mat, "emissiveTexture", sourceMaterial.texture(Material::TextureType::Emissive));

            // --- Basic material properties ---
            Material::BlendMode blendMode = sourceMaterial.blendMode();
            if (blendMode == Material::BlendMode::Opaque)
                mat["alphaMode"] = "OPAQUE";
            else if (blendMode == Material::BlendMode::Masked)
            {
                mat["alphaMode"] = "MASK";
                mat["alphaCutoff"] = static_cast<double>(sourceMaterial.alphaThreshold());
            }
            else if (blendMode == Material::BlendMode::Alpha)
                mat["alphaMode"] = "BLEND";

            if (sourceMaterial.twoSided())
                mat["doubleSided"] = true;

            QVector3D emissive = sourceMaterial.emissive();
            if (emissive.length() > 0.0f)
            {
                QJsonArray arr;
                arr.append(static_cast<double>(emissive.x()));
                arr.append(static_cast<double>(emissive.y()));
                arr.append(static_cast<double>(emissive.z()));
                mat["emissiveFactor"] = arr;
            }

            if (mat.contains("pbrMetallicRoughness"))
            {
                QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();

                bool hasMRTex = pbr.contains("metallicRoughnessTexture");
                float originalMetallicFactor = pbr.contains("metallicFactor") ?
                    static_cast<float>(pbr["metallicFactor"].toDouble()) : sourceMaterial.metalness();
                float metallicFactor = originalMetallicFactor;

                // IMPORTANT: Force metallicFactor to 1.0 if:
                // 1. There's a metallicRoughnessTexture in JSON (packed ORM)
                // 2. AND the source material has any metallic data (texture OR metalness factor)
                // This handles materials with packed ORM where JSON has metallicFactor=0 but material has metallic data
                const auto& metallicTex = sourceMaterial.texture(Material::TextureType::Metallic);
                const auto& roughnessTex = sourceMaterial.texture(Material::TextureType::Roughness);

                // Check if material has actual metallic data (not just roughness-only)
                bool hasMetallicTexture = !metallicTex.path.empty();
                bool hasMetallicFactor = sourceMaterial.metalness() > 0.0f;
                bool hasMetallicData = hasMetallicTexture || hasMetallicFactor;

                log(QString("    [MetallicFactor] hasMRTex=%1 json=%2 materialMetalness=%3 metalTexPath='%4' roughTexPath='%5' hasMetallicData=%6")
                    .arg(hasMRTex).arg(originalMetallicFactor, 0, 'f', 4)
                    .arg(sourceMaterial.metalness(), 0, 'f', 4)
                    .arg(QString::fromStdString(metallicTex.path))
                    .arg(QString::fromStdString(roughnessTex.path))
                    .arg(hasMetallicData), logCallback);

                // Force to 1.0 if JSON has M/R texture AND material has metallic data
                // This ensures packed ORM materials export correctly even if JSON shows metallicFactor=0
                if (hasMRTex && metallicFactor == 0.0f && hasMetallicData) {
                    metallicFactor = 1.0f;
                    log(QString("      -> Forced metallicFactor from %1 to 1.0 (has M/R texture + metallic data)")
                        .arg(originalMetallicFactor, 0, 'f', 4), logCallback);
                }
                pbr["metallicFactor"] = static_cast<double>(metallicFactor);

                if (pbr.contains("baseColorFactor"))
                {
                    QJsonArray existing = pbr["baseColorFactor"].toArray();
                    if (existing.size() >= 4)
                    {
                        // Always use the material's intended albedoColor for baseColorFactor
                        // This ensures tints and color multipliers are preserved
                        QVector3D albedo = sourceMaterial.albedoColor();
                        existing[0] = static_cast<double>(albedo.x());
                        existing[1] = static_cast<double>(albedo.y());
                        existing[2] = static_cast<double>(albedo.z());
                        existing[3] = static_cast<double>(sourceMaterial.opacity());
                        pbr["baseColorFactor"] = existing;
                        log(QString("    Corrected baseColorFactor to [%1, %2, %3, %4]")
                            .arg(albedo.x()).arg(albedo.y()).arg(albedo.z()).arg(sourceMaterial.opacity()), logCallback);
                    }
                }
                else
                {
                    QVector3D albedo = sourceMaterial.albedoColor();
                    float opacity = sourceMaterial.opacity();
                    if ((std::abs(albedo.x() - 1.0f) > 0.001f) ||
                        (std::abs(albedo.y() - 1.0f) > 0.001f) ||
                        (std::abs(albedo.z() - 1.0f) > 0.001f) ||
                        (opacity < 0.999f))
                    {
                        QJsonArray arr;
                        arr.append(static_cast<double>(albedo.x()));
                        arr.append(static_cast<double>(albedo.y()));
                        arr.append(static_cast<double>(albedo.z()));
                        arr.append(static_cast<double>(opacity));
                        pbr["baseColorFactor"] = arr;
                    }
                }

                float roughness = sourceMaterial.roughness();
                pbr["roughnessFactor"] = static_cast<double>(roughness);
                log(QString("    Corrected roughnessFactor to %1")
                    .arg(roughness, 0, 'f', 4), logCallback);

                mat["pbrMetallicRoughness"] = pbr;
            }

            // --- KHR extensions ---
            QJsonObject extensions = mat.value("extensions").toObject();
            bool hasExtensions = false;

            if (sourceMaterial.transmission() > 0.0f || !sourceMaterial.transmissionMapPath().isEmpty())
            {
                QJsonObject trans;
                trans["transmissionFactor"] = static_cast<double>(sourceMaterial.transmission());

                if (!sourceMaterial.transmissionMapPath().isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sourceMaterial.transmissionMapPath(), -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; trans["transmissionTexture"] = t; }
                }

                extensions["KHR_materials_transmission"] = trans;
                hasExtensions = true;
            }

            float ior = sourceMaterial.ior();
            if (std::abs(ior - 1.5f) > 0.001f)
            {
                QJsonObject iorExt;
                iorExt["ior"] = static_cast<double>(ior);
                extensions["KHR_materials_ior"] = iorExt;
                hasExtensions = true;
            }

            if (sourceMaterial.clearcoat() > 0.0f || !sourceMaterial.clearcoatColorMapPath().isEmpty()
                || !sourceMaterial.clearcoatRoughnessMapPath().isEmpty() || !sourceMaterial.clearcoatNormalMapPath().isEmpty())
            {
                QJsonObject cc;
                cc["clearcoatFactor"] = static_cast<double>(sourceMaterial.clearcoat());
                cc["clearcoatRoughnessFactor"] = static_cast<double>(sourceMaterial.clearcoatRoughness());

                QString clearcoatColorMap = sourceMaterial.clearcoatColorMapPath();
                if (!clearcoatColorMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, clearcoatColorMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; cc["clearcoatTexture"] = t; }
                }

                QString clearcoatRoughnessMap = sourceMaterial.clearcoatRoughnessMapPath();
                if (!clearcoatRoughnessMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, clearcoatRoughnessMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; cc["clearcoatRoughnessTexture"] = t; }
                }

                QString clearcoatNormalMap = sourceMaterial.clearcoatNormalMapPath();
                if (!clearcoatNormalMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, clearcoatNormalMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; cc["clearcoatNormalTexture"] = t; }
                }

                extensions["KHR_materials_clearcoat"] = cc;
                hasExtensions = true;
            }

            QVector3D sheenColor = sourceMaterial.sheenColor();
            if (sheenColor.length() > 0.0f || !sourceMaterial.sheenColorMapPath().isEmpty()
                || !sourceMaterial.sheenRoughnessMapPath().isEmpty())
            {
                QJsonObject sheen;
                QJsonArray arr;
                arr.append(static_cast<double>(sheenColor.x()));
                arr.append(static_cast<double>(sheenColor.y()));
                arr.append(static_cast<double>(sheenColor.z()));
                sheen["sheenColorFactor"] = arr;
                sheen["sheenRoughnessFactor"] = static_cast<double>(sourceMaterial.sheenRoughness());

                QString sheenColorMap = sourceMaterial.sheenColorMapPath();
                if (!sheenColorMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sheenColorMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; sheen["sheenColorTexture"] = t; }
                }

                QString sheenRoughnessMap = sourceMaterial.sheenRoughnessMapPath();
                if (!sheenRoughnessMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sheenRoughnessMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; sheen["sheenRoughnessTexture"] = t; }
                }

                extensions["KHR_materials_sheen"] = sheen;
                hasExtensions = true;
            }

            if (sourceMaterial.iridescenceFactor() > 0.0f || !sourceMaterial.iridescenceMap().isEmpty()
                || !sourceMaterial.iridescenceThicknessMap().isEmpty())
            {
                QJsonObject irid;
                irid["iridescenceFactor"] = static_cast<double>(sourceMaterial.iridescenceFactor());
                irid["iridescenceIor"] = static_cast<double>(sourceMaterial.iridescenceIor());
                irid["iridescenceThicknessMinimum"] = static_cast<double>(sourceMaterial.iridescenceThicknessMin());
                irid["iridescenceThicknessMaximum"] = static_cast<double>(sourceMaterial.iridescenceThicknessMax());

                QString iridMap = sourceMaterial.iridescenceMap();
                if (!iridMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, iridMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; irid["iridescenceTexture"] = t; }
                }

                QString iridThicknessMap = sourceMaterial.iridescenceThicknessMap();
                if (!iridThicknessMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, iridThicknessMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; irid["iridescenceThicknessTexture"] = t; }
                }

                extensions["KHR_materials_iridescence"] = irid;
                hasExtensions = true;
            }

            if (sourceMaterial.thicknessFactor() > 0.0f)
            {
                QJsonObject vol;
                vol["thicknessFactor"] = static_cast<double>(sourceMaterial.thicknessFactor());
                vol["attenuationDistance"] = static_cast<double>(sourceMaterial.attenuationDistance());
                QVector3D ac = sourceMaterial.attenuationColor();
                QJsonArray arr;
                arr.append(static_cast<double>(ac.x()));
                arr.append(static_cast<double>(ac.y()));
                arr.append(static_cast<double>(ac.z()));
                vol["attenuationColor"] = arr;
                QString thicknessMap = sourceMaterial.thicknessMap();
                if (!thicknessMap.isEmpty())
                {
                    int texIndex = findOrCreateTexture(gltfJson, thicknessMap, -1, logCallback);
                    if (texIndex >= 0)
                    {
                        QJsonObject texInfo;
                        texInfo["index"] = texIndex;
                        vol["thicknessTexture"] = texInfo;
                    }
                }
                extensions["KHR_materials_volume"] = vol;
                hasExtensions = true;
            }

            float specularFactor = sourceMaterial.specularFactor();
            QVector3D specularColor = sourceMaterial.specularColorFactor();
            QString specularFactorMap = sourceMaterial.specularFactorMap();
            QString specularColorMap = sourceMaterial.specularColorMap();

            // Check if this material has non-default specular values
            bool hasNonDefaultSpecular = (std::abs(specularFactor - 1.0f) > 0.001f) ||
                (specularColor - QVector3D(1, 1, 1)).length() > 0.001f ||
                !specularFactorMap.isEmpty() || !specularColorMap.isEmpty();

            // For metallic-roughness materials, we should ALWAYS write KHR_materials_specular
            // to properly define the specular response, even if using default values
            // Only skip if using the old specular-glossiness workflow
            bool shouldWriteSpecular = (hasNonDefaultSpecular || mat.contains("pbrMetallicRoughness")) &&
                                       !sourceMaterial.getUseSpecularGlossiness();

            log(QString("    [Specular] factor=%1 color=[%2,%3,%4] nonDefault=%5 hasPBR=%6 useSpecGloss=%7 shouldWrite=%8")
                .arg(specularFactor, 0, 'f', 4).arg(specularColor.x(), 0, 'f', 4)
                .arg(specularColor.y(), 0, 'f', 4).arg(specularColor.z(), 0, 'f', 4)
                .arg(hasNonDefaultSpecular).arg(mat.contains("pbrMetallicRoughness"))
                .arg(sourceMaterial.getUseSpecularGlossiness()).arg(shouldWriteSpecular), logCallback);

            if (shouldWriteSpecular)
            {
                QJsonObject spec;
                spec["specularFactor"] = static_cast<double>(specularFactor);
                QJsonArray arr;
                arr.append(static_cast<double>(specularColor.x()));
                arr.append(static_cast<double>(specularColor.y()));
                arr.append(static_cast<double>(specularColor.z()));
                spec["specularColorFactor"] = arr;
                if (!specularFactorMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, specularFactorMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; spec["specularTexture"] = t; }
                }
                if (!specularColorMap.isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, specularColorMap, -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; spec["specularColorTexture"] = t; }
                }
                extensions["KHR_materials_specular"] = spec;
                hasExtensions = true;
                log("      -> KHR_materials_specular written", logCallback);
            }
            else if (hasNonDefaultSpecular)
            {
                log("      -> SKIPPED KHR_materials_specular (useSpecularGlossiness=true)", logCallback);
            }

            if (sourceMaterial.anisotropyStrength() > 0.0f || !sourceMaterial.anisotropyMap().isEmpty())
            {
                QJsonObject aniso;
                aniso["anisotropyStrength"] = static_cast<double>(sourceMaterial.anisotropyStrength());
                aniso["anisotropyRotation"] = static_cast<double>(sourceMaterial.anisotropyRotation());

                if (!sourceMaterial.anisotropyMap().isEmpty())
                {
                    const Material::Texture& anisoTex = sourceMaterial.texture(Material::TextureType::Anisotropy);
                    const int anisotropySamplerIdx = getOrCreateSampler(
                        anisoTex.magFilter,
                        anisoTex.minFilter,
                        anisoTex.wrapS,
                        anisoTex.wrapT);
                    int ti = findOrCreateTexture(gltfJson, sourceMaterial.anisotropyMap(), anisotropySamplerIdx, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; aniso["anisotropyTexture"] = t; }
                }

                extensions["KHR_materials_anisotropy"] = aniso;
                hasExtensions = true;
            }

            if (sourceMaterial.dispersion() > 0.0f)
            {
                QJsonObject disp;
                disp["dispersion"] = static_cast<double>(sourceMaterial.dispersion());
                extensions["KHR_materials_dispersion"] = disp;
                hasExtensions = true;
            }

            if (std::abs(sourceMaterial.emissiveStrength() - 1.0f) > 0.001f)
            {
                QJsonObject es;
                es["emissiveStrength"] = static_cast<double>(sourceMaterial.emissiveStrength());
                extensions["KHR_materials_emissive_strength"] = es;
                hasExtensions = true;
            }

            if (sourceMaterial.isUnlit())
            {
                extensions["KHR_materials_unlit"] = QJsonObject();
                hasExtensions = true;
            }

            // KHR_materials_volume_scatter
            if (sourceMaterial.hasVolumeScattering())
            {
                QJsonObject scatter;
                QVector3D msc = sourceMaterial.multiScatterColor();
                QJsonArray mscArr;
                mscArr.append(static_cast<double>(msc.x()));
                mscArr.append(static_cast<double>(msc.y()));
                mscArr.append(static_cast<double>(msc.z()));
                scatter["multiscatterColor"] = mscArr;
                extensions["KHR_materials_volume_scatter"] = scatter;
                hasExtensions = true;
            }

            // KHR_materials_diffuse_transmission
            if (sourceMaterial.diffuseTransmissionFactor() > 0.0f
                || !sourceMaterial.diffuseTransmissionMap().isEmpty()
                || !sourceMaterial.diffuseTransmissionColorMap().isEmpty())
            {
                QJsonObject dt;
                dt["diffuseTransmissionFactor"] = static_cast<double>(sourceMaterial.diffuseTransmissionFactor());

                QVector3D dtc = sourceMaterial.diffuseTransmissionColorFactor();
                QJsonArray dtcArr;
                dtcArr.append(static_cast<double>(dtc.x()));
                dtcArr.append(static_cast<double>(dtc.y()));
                dtcArr.append(static_cast<double>(dtc.z()));
                dt["diffuseTransmissionColorFactor"] = dtcArr;

                if (!sourceMaterial.diffuseTransmissionMap().isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sourceMaterial.diffuseTransmissionMap(), -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; dt["diffuseTransmissionTexture"] = t; }
                }
                if (!sourceMaterial.diffuseTransmissionColorMap().isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sourceMaterial.diffuseTransmissionColorMap(), -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; dt["diffuseTransmissionColorTexture"] = t; }
                }

                extensions["KHR_materials_diffuse_transmission"] = dt;
                hasExtensions = true;
            }

            // KHR_materials_pbrSpecularGlossiness
            if (sourceMaterial.getUseSpecularGlossiness())
            {
                QJsonObject sg;

                QVector3D diff = sourceMaterial.diffuseColor();
                QJsonArray diffArr;
                diffArr.append(static_cast<double>(diff.x()));
                diffArr.append(static_cast<double>(diff.y()));
                diffArr.append(static_cast<double>(diff.z()));
                diffArr.append(static_cast<double>(sourceMaterial.opacity()));
                sg["diffuseFactor"] = diffArr;

                QVector3D spec = sourceMaterial.specularColor();
                QJsonArray specArr;
                specArr.append(static_cast<double>(spec.x()));
                specArr.append(static_cast<double>(spec.y()));
                specArr.append(static_cast<double>(spec.z()));
                sg["specularFactor"] = specArr;

                sg["glossinessFactor"] = static_cast<double>(sourceMaterial.glossinessFactor());

                if (!sourceMaterial.diffuseMap().isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sourceMaterial.diffuseMap(), -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; sg["diffuseTexture"] = t; }
                }
                if (!sourceMaterial.specularGlossinessMap().isEmpty())
                {
                    int ti = findOrCreateTexture(gltfJson, sourceMaterial.specularGlossinessMap(), -1, logCallback);
                    if (ti >= 0) { QJsonObject t; t["index"] = ti; sg["specularGlossinessTexture"] = t; }
                }

                extensions["KHR_materials_pbrSpecularGlossiness"] = sg;
                hasExtensions = true;
            }

            if (hasExtensions)
                mat["extensions"] = extensions;

            // --- Clearcoat texture index fix + transforms + samplers ---
            // Must run AFTER mat["extensions"] = extensions so the cc object
            // built above (with correct texture references) is already in place.
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject cc = exts.value("KHR_materials_clearcoat").toObject();
                if (!cc.isEmpty())
                {
                    fixTextureIndex(cc, "clearcoatTexture", sourceMaterial.texture(Material::TextureType::ClearcoatColor));
                    fixTextureIndex(cc, "clearcoatRoughnessTexture", sourceMaterial.texture(Material::TextureType::ClearcoatRoughness));
                    fixTextureIndex(cc, "clearcoatNormalTexture", sourceMaterial.texture(Material::TextureType::ClearcoatNormal));

                    writeTransform(cc, "clearcoatTexture", sourceMaterial.texture(Material::TextureType::ClearcoatColor));
                    writeTransform(cc, "clearcoatRoughnessTexture", sourceMaterial.texture(Material::TextureType::ClearcoatRoughness));
                    writeTransform(cc, "clearcoatNormalTexture", sourceMaterial.texture(Material::TextureType::ClearcoatNormal));

                    updateTextureSampler(cc, "clearcoatTexture", sourceMaterial.texture(Material::TextureType::ClearcoatColor));
                    updateTextureSampler(cc, "clearcoatRoughnessTexture", sourceMaterial.texture(Material::TextureType::ClearcoatRoughness));
                    updateTextureSampler(cc, "clearcoatNormalTexture", sourceMaterial.texture(Material::TextureType::ClearcoatNormal));

                    exts["KHR_materials_clearcoat"] = cc;
                    mat["extensions"] = exts;
                }
            }

            // --- Anisotropy texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject aniso = exts.value("KHR_materials_anisotropy").toObject();
                if (!aniso.isEmpty())
                {
                    fixTextureIndex(aniso, "anisotropyTexture", sourceMaterial.texture(Material::TextureType::Anisotropy));
                    writeTransform(aniso, "anisotropyTexture", sourceMaterial.texture(Material::TextureType::Anisotropy));
                    updateTextureSampler(aniso, "anisotropyTexture", sourceMaterial.texture(Material::TextureType::Anisotropy));

                    exts["KHR_materials_anisotropy"] = aniso;
                    mat["extensions"] = exts;
                }
            }

            // --- Sheen texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject sheen = exts.value("KHR_materials_sheen").toObject();
                if (!sheen.isEmpty())
                {
                    fixTextureIndex(sheen, "sheenColorTexture", sourceMaterial.texture(Material::TextureType::SheenColor));
                    fixTextureIndex(sheen, "sheenRoughnessTexture", sourceMaterial.texture(Material::TextureType::SheenRoughness));

                    writeTransform(sheen, "sheenColorTexture", sourceMaterial.texture(Material::TextureType::SheenColor));
                    writeTransform(sheen, "sheenRoughnessTexture", sourceMaterial.texture(Material::TextureType::SheenRoughness));

                    updateTextureSampler(sheen, "sheenColorTexture", sourceMaterial.texture(Material::TextureType::SheenColor));
                    updateTextureSampler(sheen, "sheenRoughnessTexture", sourceMaterial.texture(Material::TextureType::SheenRoughness));

                    exts["KHR_materials_sheen"] = sheen;
                    mat["extensions"] = exts;
                }
            }

            // --- Iridescence texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject irid = exts.value("KHR_materials_iridescence").toObject();
                if (!irid.isEmpty())
                {
                    fixTextureIndex(irid, "iridescenceTexture", sourceMaterial.texture(Material::TextureType::Iridescence));
                    fixTextureIndex(irid, "iridescenceThicknessTexture", sourceMaterial.texture(Material::TextureType::IridescenceThickness));

                    writeTransform(irid, "iridescenceTexture", sourceMaterial.texture(Material::TextureType::Iridescence));
                    writeTransform(irid, "iridescenceThicknessTexture", sourceMaterial.texture(Material::TextureType::IridescenceThickness));

                    updateTextureSampler(irid, "iridescenceTexture", sourceMaterial.texture(Material::TextureType::Iridescence));
                    updateTextureSampler(irid, "iridescenceThicknessTexture", sourceMaterial.texture(Material::TextureType::IridescenceThickness));

                    exts["KHR_materials_iridescence"] = irid;
                    mat["extensions"] = exts;
                }
            }

            // --- Specular texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject spec = exts.value("KHR_materials_specular").toObject();
                if (!spec.isEmpty())
                {
                    fixTextureIndex(spec, "specularTexture", sourceMaterial.texture(Material::TextureType::SpecularFactor));
                    fixTextureIndex(spec, "specularColorTexture", sourceMaterial.texture(Material::TextureType::SpecularColor));

                    writeTransform(spec, "specularTexture", sourceMaterial.texture(Material::TextureType::SpecularFactor));
                    writeTransform(spec, "specularColorTexture", sourceMaterial.texture(Material::TextureType::SpecularColor));

                    updateTextureSampler(spec, "specularTexture", sourceMaterial.texture(Material::TextureType::SpecularFactor));
                    updateTextureSampler(spec, "specularColorTexture", sourceMaterial.texture(Material::TextureType::SpecularColor));

                    exts["KHR_materials_specular"] = spec;
                    mat["extensions"] = exts;
                }
            }

            // --- Volume texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject vol = exts.value("KHR_materials_volume").toObject();
                if (!vol.isEmpty())
                {
                    fixTextureIndex(vol, "thicknessTexture", sourceMaterial.texture(Material::TextureType::Thickness));
                    writeTransform(vol, "thicknessTexture", sourceMaterial.texture(Material::TextureType::Thickness));
                    updateTextureSampler(vol, "thicknessTexture", sourceMaterial.texture(Material::TextureType::Thickness));

                    exts["KHR_materials_volume"] = vol;
                    mat["extensions"] = exts;
                }
            }

            // --- Transmission texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject trans = exts.value("KHR_materials_transmission").toObject();
                if (!trans.isEmpty())
                {
                    fixTextureIndex(trans, "transmissionTexture", sourceMaterial.texture(Material::TextureType::Transmission));
                    writeTransform(trans, "transmissionTexture", sourceMaterial.texture(Material::TextureType::Transmission));
                    updateTextureSampler(trans, "transmissionTexture", sourceMaterial.texture(Material::TextureType::Transmission));

                    exts["KHR_materials_transmission"] = trans;
                    mat["extensions"] = exts;
                }
            }

            // --- Diffuse transmission texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject dt = exts.value("KHR_materials_diffuse_transmission").toObject();
                if (!dt.isEmpty())
                {
                    fixTextureIndex(dt, "diffuseTransmissionTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmission));
                    fixTextureIndex(dt, "diffuseTransmissionColorTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmissionColor));

                    writeTransform(dt, "diffuseTransmissionTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmission));
                    writeTransform(dt, "diffuseTransmissionColorTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmissionColor));

                    updateTextureSampler(dt, "diffuseTransmissionTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmission));
                    updateTextureSampler(dt, "diffuseTransmissionColorTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmissionColor));

                    exts["KHR_materials_diffuse_transmission"] = dt;
                    mat["extensions"] = exts;
                }
            }

            // --- pbrSpecularGlossiness texture index fix + transforms + samplers ---
            {
                QJsonObject exts = mat.value("extensions").toObject();
                QJsonObject sg = exts.value("KHR_materials_pbrSpecularGlossiness").toObject();
                if (!sg.isEmpty())
                {
                    fixTextureIndex(sg, "diffuseTexture", sourceMaterial.texture(Material::TextureType::Diffuse));
                    fixTextureIndex(sg, "specularGlossinessTexture", sourceMaterial.texture(Material::TextureType::SpecularGlossiness));

                    writeTransform(sg, "diffuseTexture", sourceMaterial.texture(Material::TextureType::Diffuse));
                    writeTransform(sg, "specularGlossinessTexture", sourceMaterial.texture(Material::TextureType::SpecularGlossiness));

                    updateTextureSampler(sg, "diffuseTexture", sourceMaterial.texture(Material::TextureType::Diffuse));
                    updateTextureSampler(sg, "specularGlossinessTexture", sourceMaterial.texture(Material::TextureType::SpecularGlossiness));

                    exts["KHR_materials_pbrSpecularGlossiness"] = sg;
                    mat["extensions"] = exts;
                }
            }

            // Write correct material name from source Material
            QString correctName = sourceMaterial.name();
            if (!correctName.isEmpty())
                mat["name"] = correctName;

            materials[matIdx] = mat;
        }

        // ===== SECONDARY PASS: Patch variant materials (KHR_materials_variants) =====
        // Non-default variant materials were added as extra aiMaterials but have no
        // corresponding source SceneMesh, so they never enter patchedMaterials.
        // We patch them here using the Material stored in _variantMatByJsonIdx.
        if (!_variantMatByJsonIdx.isEmpty())
        {
            log("=== Patching Variant Materials (KHR_materials_variants) ===", logCallback);
            for (auto vit = _variantMatByJsonIdx.constBegin(); vit != _variantMatByJsonIdx.constEnd(); ++vit)
            {
                int matIdx = vit.key();
                const Material& sourceMaterial = vit.value();

                if (matIdx < 0 || matIdx >= materials.size())
                {
                    log(QString("  [VARIANT] mat[%1] out of range (%2 total), skipping")
                        .arg(matIdx).arg(materials.size()), logCallback);
                    continue;
                }
                if (patchedMaterials.contains(matIdx))
                {
                    log(QString("  [VARIANT] mat[%1] already patched by main pass, skipping").arg(matIdx), logCallback);
                    continue;
                }

                log(QString("  [VARIANT] Patching mat[%1] '%2'").arg(matIdx).arg(sourceMaterial.name()), logCallback);
                QJsonObject mat = materials[matIdx].toObject();

                // Fix texture indices for all PBR slots
                if (mat.contains("pbrMetallicRoughness"))
                {
                    QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                    fixTextureIndex(pbr, "baseColorTexture", sourceMaterial.texture(Material::TextureType::Albedo));
                    const auto& metallicTex  = sourceMaterial.texture(Material::TextureType::Metallic);
                    const auto& roughnessTex = sourceMaterial.texture(Material::TextureType::Roughness);
                    bool hasPackedMR    = (!metallicTex.path.empty() && !roughnessTex.path.empty()
                                           && metallicTex.path != roughnessTex.path);
                    bool isRoughnessOnly = (metallicTex.path.empty() && !roughnessTex.path.empty());
                    if (!hasPackedMR && !isRoughnessOnly)
                        fixTextureIndex(pbr, "metallicRoughnessTexture", metallicTex);
                    mat["pbrMetallicRoughness"] = pbr;
                }
                fixTextureIndex(mat, "normalTexture",    sourceMaterial.texture(Material::TextureType::Normal));
                fixTextureIndex(mat, "occlusionTexture", sourceMaterial.texture(Material::TextureType::AmbientOcclusion));
                fixTextureIndex(mat, "emissiveTexture",  sourceMaterial.texture(Material::TextureType::Emissive));

                // Write texture transforms
                if (mat.contains("pbrMetallicRoughness"))
                {
                    QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                    writeTransform(pbr, "baseColorTexture",        sourceMaterial.texture(Material::TextureType::Albedo));
                    writeTransform(pbr, "metallicRoughnessTexture", sourceMaterial.texture(Material::TextureType::Metallic));
                    mat["pbrMetallicRoughness"] = pbr;
                }
                writeTransform(mat, "normalTexture",    sourceMaterial.texture(Material::TextureType::Normal));
                writeTransform(mat, "occlusionTexture", sourceMaterial.texture(Material::TextureType::AmbientOcclusion));
                writeTransform(mat, "emissiveTexture",  sourceMaterial.texture(Material::TextureType::Emissive));

                // Fix samplers
                if (mat.contains("pbrMetallicRoughness"))
                {
                    QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                    updateTextureSampler(pbr, "baseColorTexture",        sourceMaterial.texture(Material::TextureType::Albedo));
                    updateTextureSampler(pbr, "metallicRoughnessTexture", sourceMaterial.texture(Material::TextureType::Metallic));
                    mat["pbrMetallicRoughness"] = pbr;
                }
                updateTextureSampler(mat, "normalTexture",    sourceMaterial.texture(Material::TextureType::Normal));
                updateTextureSampler(mat, "occlusionTexture", sourceMaterial.texture(Material::TextureType::AmbientOcclusion));
                updateTextureSampler(mat, "emissiveTexture",  sourceMaterial.texture(Material::TextureType::Emissive));

                // Build KHR extension objects from source material data.
                // We CANNOT rely on Assimp having written these extensions —
                // createMaterial() never serialises extension scalars to aiMaterial
                // properties, so Assimp's exporter produces no extension JSON for
                // variant materials.  We must build every block from scratch using
                // the same conditions as the main patching loop.
                {
                    QJsonObject exts = mat.value("extensions").toObject();

                    // --- KHR_materials_transmission ---
                    if (sourceMaterial.transmission() > 0.0f || !sourceMaterial.transmissionMapPath().isEmpty())
                    {
                        QJsonObject trans;
                        trans["transmissionFactor"] = static_cast<double>(sourceMaterial.transmission());
                        if (!sourceMaterial.transmissionMapPath().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.transmissionMapPath(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; trans["transmissionTexture"] = t; }
                        }
                        fixTextureIndex(trans, "transmissionTexture", sourceMaterial.texture(Material::TextureType::Transmission));
                        writeTransform(trans, "transmissionTexture", sourceMaterial.texture(Material::TextureType::Transmission));
                        updateTextureSampler(trans, "transmissionTexture", sourceMaterial.texture(Material::TextureType::Transmission));
                        exts["KHR_materials_transmission"] = trans;
                    }

                    // --- KHR_materials_ior ---
                    {
                        float ior = sourceMaterial.ior();
                        if (std::abs(ior - 1.5f) > 0.001f)
                        {
                            QJsonObject iorExt;
                            iorExt["ior"] = static_cast<double>(ior);
                            exts["KHR_materials_ior"] = iorExt;
                        }
                    }

                    // --- KHR_materials_clearcoat ---
                    if (sourceMaterial.clearcoat() > 0.0f || !sourceMaterial.clearcoatColorMapPath().isEmpty()
                        || !sourceMaterial.clearcoatRoughnessMapPath().isEmpty() || !sourceMaterial.clearcoatNormalMapPath().isEmpty())
                    {
                        QJsonObject cc;
                        cc["clearcoatFactor"] = static_cast<double>(sourceMaterial.clearcoat());
                        cc["clearcoatRoughnessFactor"] = static_cast<double>(sourceMaterial.clearcoatRoughness());
                        if (!sourceMaterial.clearcoatColorMapPath().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.clearcoatColorMapPath(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; cc["clearcoatTexture"] = t; }
                        }
                        if (!sourceMaterial.clearcoatRoughnessMapPath().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.clearcoatRoughnessMapPath(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; cc["clearcoatRoughnessTexture"] = t; }
                        }
                        if (!sourceMaterial.clearcoatNormalMapPath().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.clearcoatNormalMapPath(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; cc["clearcoatNormalTexture"] = t; }
                        }
                        fixTextureIndex(cc, "clearcoatTexture", sourceMaterial.texture(Material::TextureType::ClearcoatColor));
                        fixTextureIndex(cc, "clearcoatRoughnessTexture", sourceMaterial.texture(Material::TextureType::ClearcoatRoughness));
                        fixTextureIndex(cc, "clearcoatNormalTexture", sourceMaterial.texture(Material::TextureType::ClearcoatNormal));
                        writeTransform(cc, "clearcoatTexture", sourceMaterial.texture(Material::TextureType::ClearcoatColor));
                        writeTransform(cc, "clearcoatRoughnessTexture", sourceMaterial.texture(Material::TextureType::ClearcoatRoughness));
                        writeTransform(cc, "clearcoatNormalTexture", sourceMaterial.texture(Material::TextureType::ClearcoatNormal));
                        updateTextureSampler(cc, "clearcoatTexture", sourceMaterial.texture(Material::TextureType::ClearcoatColor));
                        updateTextureSampler(cc, "clearcoatRoughnessTexture", sourceMaterial.texture(Material::TextureType::ClearcoatRoughness));
                        updateTextureSampler(cc, "clearcoatNormalTexture", sourceMaterial.texture(Material::TextureType::ClearcoatNormal));
                        exts["KHR_materials_clearcoat"] = cc;
                    }

                    // --- KHR_materials_sheen ---
                    {
                        QVector3D sheenColor = sourceMaterial.sheenColor();
                        if (sheenColor.length() > 0.0f || !sourceMaterial.sheenColorMapPath().isEmpty()
                            || !sourceMaterial.sheenRoughnessMapPath().isEmpty())
                        {
                            QJsonObject sheen;
                            QJsonArray arr;
                            arr.append(static_cast<double>(sheenColor.x()));
                            arr.append(static_cast<double>(sheenColor.y()));
                            arr.append(static_cast<double>(sheenColor.z()));
                            sheen["sheenColorFactor"] = arr;
                            sheen["sheenRoughnessFactor"] = static_cast<double>(sourceMaterial.sheenRoughness());
                            if (!sourceMaterial.sheenColorMapPath().isEmpty())
                            {
                                int ti = findOrCreateTexture(gltfJson, sourceMaterial.sheenColorMapPath(), -1, logCallback);
                                if (ti >= 0) { QJsonObject t; t["index"] = ti; sheen["sheenColorTexture"] = t; }
                            }
                            if (!sourceMaterial.sheenRoughnessMapPath().isEmpty())
                            {
                                int ti = findOrCreateTexture(gltfJson, sourceMaterial.sheenRoughnessMapPath(), -1, logCallback);
                                if (ti >= 0) { QJsonObject t; t["index"] = ti; sheen["sheenRoughnessTexture"] = t; }
                            }
                            fixTextureIndex(sheen, "sheenColorTexture", sourceMaterial.texture(Material::TextureType::SheenColor));
                            fixTextureIndex(sheen, "sheenRoughnessTexture", sourceMaterial.texture(Material::TextureType::SheenRoughness));
                            writeTransform(sheen, "sheenColorTexture", sourceMaterial.texture(Material::TextureType::SheenColor));
                            writeTransform(sheen, "sheenRoughnessTexture", sourceMaterial.texture(Material::TextureType::SheenRoughness));
                            updateTextureSampler(sheen, "sheenColorTexture", sourceMaterial.texture(Material::TextureType::SheenColor));
                            updateTextureSampler(sheen, "sheenRoughnessTexture", sourceMaterial.texture(Material::TextureType::SheenRoughness));
                            exts["KHR_materials_sheen"] = sheen;
                        }
                    }

                    // --- KHR_materials_iridescence ---
                    if (sourceMaterial.iridescenceFactor() > 0.0f || !sourceMaterial.iridescenceMap().isEmpty()
                        || !sourceMaterial.iridescenceThicknessMap().isEmpty())
                    {
                        QJsonObject irid;
                        irid["iridescenceFactor"] = static_cast<double>(sourceMaterial.iridescenceFactor());
                        irid["iridescenceIor"] = static_cast<double>(sourceMaterial.iridescenceIor());
                        irid["iridescenceThicknessMinimum"] = static_cast<double>(sourceMaterial.iridescenceThicknessMin());
                        irid["iridescenceThicknessMaximum"] = static_cast<double>(sourceMaterial.iridescenceThicknessMax());
                        if (!sourceMaterial.iridescenceMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.iridescenceMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; irid["iridescenceTexture"] = t; }
                        }
                        if (!sourceMaterial.iridescenceThicknessMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.iridescenceThicknessMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; irid["iridescenceThicknessTexture"] = t; }
                        }
                        fixTextureIndex(irid, "iridescenceTexture", sourceMaterial.texture(Material::TextureType::Iridescence));
                        fixTextureIndex(irid, "iridescenceThicknessTexture", sourceMaterial.texture(Material::TextureType::IridescenceThickness));
                        writeTransform(irid, "iridescenceTexture", sourceMaterial.texture(Material::TextureType::Iridescence));
                        writeTransform(irid, "iridescenceThicknessTexture", sourceMaterial.texture(Material::TextureType::IridescenceThickness));
                        updateTextureSampler(irid, "iridescenceTexture", sourceMaterial.texture(Material::TextureType::Iridescence));
                        updateTextureSampler(irid, "iridescenceThicknessTexture", sourceMaterial.texture(Material::TextureType::IridescenceThickness));
                        exts["KHR_materials_iridescence"] = irid;
                    }

                    // --- KHR_materials_volume ---
                    if (sourceMaterial.thicknessFactor() > 0.0f)
                    {
                        QJsonObject vol;
                        vol["thicknessFactor"] = static_cast<double>(sourceMaterial.thicknessFactor());
                        vol["attenuationDistance"] = static_cast<double>(sourceMaterial.attenuationDistance());
                        QVector3D ac = sourceMaterial.attenuationColor();
                        QJsonArray arr;
                        arr.append(static_cast<double>(ac.x()));
                        arr.append(static_cast<double>(ac.y()));
                        arr.append(static_cast<double>(ac.z()));
                        vol["attenuationColor"] = arr;
                        if (!sourceMaterial.thicknessMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.thicknessMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; vol["thicknessTexture"] = t; }
                        }
                        fixTextureIndex(vol, "thicknessTexture", sourceMaterial.texture(Material::TextureType::Thickness));
                        writeTransform(vol, "thicknessTexture", sourceMaterial.texture(Material::TextureType::Thickness));
                        updateTextureSampler(vol, "thicknessTexture", sourceMaterial.texture(Material::TextureType::Thickness));
                        exts["KHR_materials_volume"] = vol;
                    }

                    // --- KHR_materials_specular ---
                    {
                        float specularFactor = sourceMaterial.specularFactor();
                        QVector3D specularColor = sourceMaterial.specularColorFactor();
                        QString specularFactorMap = sourceMaterial.specularFactorMap();
                        QString specularColorMap = sourceMaterial.specularColorMap();
                        bool hasNonDefaultSpecular = (std::abs(specularFactor - 1.0f) > 0.001f) ||
                            (specularColor - QVector3D(1, 1, 1)).length() > 0.001f ||
                            !specularFactorMap.isEmpty() || !specularColorMap.isEmpty();
                        bool shouldWriteSpecular = (hasNonDefaultSpecular || mat.contains("pbrMetallicRoughness")) &&
                                                   !sourceMaterial.getUseSpecularGlossiness();
                        if (shouldWriteSpecular)
                        {
                            QJsonObject spec;
                            spec["specularFactor"] = static_cast<double>(specularFactor);
                            QJsonArray arr;
                            arr.append(static_cast<double>(specularColor.x()));
                            arr.append(static_cast<double>(specularColor.y()));
                            arr.append(static_cast<double>(specularColor.z()));
                            spec["specularColorFactor"] = arr;
                            if (!specularFactorMap.isEmpty())
                            {
                                int ti = findOrCreateTexture(gltfJson, specularFactorMap, -1, logCallback);
                                if (ti >= 0) { QJsonObject t; t["index"] = ti; spec["specularTexture"] = t; }
                            }
                            if (!specularColorMap.isEmpty())
                            {
                                int ti = findOrCreateTexture(gltfJson, specularColorMap, -1, logCallback);
                                if (ti >= 0) { QJsonObject t; t["index"] = ti; spec["specularColorTexture"] = t; }
                            }
                            fixTextureIndex(spec, "specularTexture", sourceMaterial.texture(Material::TextureType::SpecularFactor));
                            fixTextureIndex(spec, "specularColorTexture", sourceMaterial.texture(Material::TextureType::SpecularColor));
                            writeTransform(spec, "specularTexture", sourceMaterial.texture(Material::TextureType::SpecularFactor));
                            writeTransform(spec, "specularColorTexture", sourceMaterial.texture(Material::TextureType::SpecularColor));
                            updateTextureSampler(spec, "specularTexture", sourceMaterial.texture(Material::TextureType::SpecularFactor));
                            updateTextureSampler(spec, "specularColorTexture", sourceMaterial.texture(Material::TextureType::SpecularColor));
                            exts["KHR_materials_specular"] = spec;
                        }
                    }

                    // --- KHR_materials_anisotropy ---
                    if (sourceMaterial.anisotropyStrength() > 0.0f || !sourceMaterial.anisotropyMap().isEmpty())
                    {
                        QJsonObject aniso;
                        aniso["anisotropyStrength"] = static_cast<double>(sourceMaterial.anisotropyStrength());
                        aniso["anisotropyRotation"] = static_cast<double>(sourceMaterial.anisotropyRotation());
                        if (!sourceMaterial.anisotropyMap().isEmpty())
                        {
                            const Material::Texture& anisoTex = sourceMaterial.texture(Material::TextureType::Anisotropy);
                            const int anisotropySamplerIdx = getOrCreateSampler(
                                anisoTex.magFilter, anisoTex.minFilter, anisoTex.wrapS, anisoTex.wrapT);
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.anisotropyMap(), anisotropySamplerIdx, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; aniso["anisotropyTexture"] = t; }
                        }
                        fixTextureIndex(aniso, "anisotropyTexture", sourceMaterial.texture(Material::TextureType::Anisotropy));
                        writeTransform(aniso, "anisotropyTexture", sourceMaterial.texture(Material::TextureType::Anisotropy));
                        updateTextureSampler(aniso, "anisotropyTexture", sourceMaterial.texture(Material::TextureType::Anisotropy));
                        exts["KHR_materials_anisotropy"] = aniso;
                    }

                    // --- KHR_materials_dispersion ---
                    if (sourceMaterial.dispersion() > 0.0f)
                    {
                        QJsonObject disp;
                        disp["dispersion"] = static_cast<double>(sourceMaterial.dispersion());
                        exts["KHR_materials_dispersion"] = disp;
                    }

                    // --- KHR_materials_emissive_strength ---
                    if (std::abs(sourceMaterial.emissiveStrength() - 1.0f) > 0.001f)
                    {
                        QJsonObject es;
                        es["emissiveStrength"] = static_cast<double>(sourceMaterial.emissiveStrength());
                        exts["KHR_materials_emissive_strength"] = es;
                    }

                    // --- KHR_materials_unlit ---
                    if (sourceMaterial.isUnlit())
                        exts["KHR_materials_unlit"] = QJsonObject();

                    // --- KHR_materials_volume_scatter ---
                    if (sourceMaterial.hasVolumeScattering())
                    {
                        QJsonObject scatter;
                        QVector3D msc = sourceMaterial.multiScatterColor();
                        QJsonArray mscArr;
                        mscArr.append(static_cast<double>(msc.x()));
                        mscArr.append(static_cast<double>(msc.y()));
                        mscArr.append(static_cast<double>(msc.z()));
                        scatter["multiscatterColor"] = mscArr;
                        exts["KHR_materials_volume_scatter"] = scatter;
                    }

                    // --- KHR_materials_diffuse_transmission ---
                    if (sourceMaterial.diffuseTransmissionFactor() > 0.0f
                        || !sourceMaterial.diffuseTransmissionMap().isEmpty()
                        || !sourceMaterial.diffuseTransmissionColorMap().isEmpty())
                    {
                        QJsonObject dt;
                        dt["diffuseTransmissionFactor"] = static_cast<double>(sourceMaterial.diffuseTransmissionFactor());
                        QVector3D dtc = sourceMaterial.diffuseTransmissionColorFactor();
                        QJsonArray dtcArr;
                        dtcArr.append(static_cast<double>(dtc.x()));
                        dtcArr.append(static_cast<double>(dtc.y()));
                        dtcArr.append(static_cast<double>(dtc.z()));
                        dt["diffuseTransmissionColorFactor"] = dtcArr;
                        if (!sourceMaterial.diffuseTransmissionMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.diffuseTransmissionMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; dt["diffuseTransmissionTexture"] = t; }
                        }
                        if (!sourceMaterial.diffuseTransmissionColorMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.diffuseTransmissionColorMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; dt["diffuseTransmissionColorTexture"] = t; }
                        }
                        fixTextureIndex(dt, "diffuseTransmissionTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmission));
                        fixTextureIndex(dt, "diffuseTransmissionColorTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmissionColor));
                        writeTransform(dt, "diffuseTransmissionTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmission));
                        writeTransform(dt, "diffuseTransmissionColorTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmissionColor));
                        updateTextureSampler(dt, "diffuseTransmissionTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmission));
                        updateTextureSampler(dt, "diffuseTransmissionColorTexture", sourceMaterial.texture(Material::TextureType::DiffuseTransmissionColor));
                        exts["KHR_materials_diffuse_transmission"] = dt;
                    }

                    // --- KHR_materials_pbrSpecularGlossiness ---
                    if (sourceMaterial.getUseSpecularGlossiness())
                    {
                        QJsonObject sg;
                        QVector3D diff = sourceMaterial.diffuseColor();
                        QJsonArray diffArr;
                        diffArr.append(static_cast<double>(diff.x()));
                        diffArr.append(static_cast<double>(diff.y()));
                        diffArr.append(static_cast<double>(diff.z()));
                        diffArr.append(static_cast<double>(sourceMaterial.opacity()));
                        sg["diffuseFactor"] = diffArr;
                        QVector3D spec = sourceMaterial.specularColor();
                        QJsonArray specArr;
                        specArr.append(static_cast<double>(spec.x()));
                        specArr.append(static_cast<double>(spec.y()));
                        specArr.append(static_cast<double>(spec.z()));
                        sg["specularFactor"] = specArr;
                        sg["glossinessFactor"] = static_cast<double>(sourceMaterial.glossinessFactor());
                        if (!sourceMaterial.diffuseMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.diffuseMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; sg["diffuseTexture"] = t; }
                        }
                        if (!sourceMaterial.specularGlossinessMap().isEmpty())
                        {
                            int ti = findOrCreateTexture(gltfJson, sourceMaterial.specularGlossinessMap(), -1, logCallback);
                            if (ti >= 0) { QJsonObject t; t["index"] = ti; sg["specularGlossinessTexture"] = t; }
                        }
                        fixTextureIndex(sg, "diffuseTexture", sourceMaterial.texture(Material::TextureType::Diffuse));
                        fixTextureIndex(sg, "specularGlossinessTexture", sourceMaterial.texture(Material::TextureType::SpecularGlossiness));
                        writeTransform(sg, "diffuseTexture", sourceMaterial.texture(Material::TextureType::Diffuse));
                        writeTransform(sg, "specularGlossinessTexture", sourceMaterial.texture(Material::TextureType::SpecularGlossiness));
                        updateTextureSampler(sg, "diffuseTexture", sourceMaterial.texture(Material::TextureType::Diffuse));
                        updateTextureSampler(sg, "specularGlossinessTexture", sourceMaterial.texture(Material::TextureType::SpecularGlossiness));
                        exts["KHR_materials_pbrSpecularGlossiness"] = sg;
                    }

                    if (!exts.isEmpty())
                        mat["extensions"] = exts;
                }

                // PBR scalar properties
                if (mat.contains("pbrMetallicRoughness"))
                {
                    QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
                    QVector3D albedo  = sourceMaterial.albedoColor();
                    QJsonArray bcf;
                    bcf.append(static_cast<double>(albedo.x()));
                    bcf.append(static_cast<double>(albedo.y()));
                    bcf.append(static_cast<double>(albedo.z()));
                    bcf.append(static_cast<double>(sourceMaterial.opacity()));
                    pbr["baseColorFactor"]  = bcf;
                    pbr["metallicFactor"]   = static_cast<double>(sourceMaterial.metalness());
                    pbr["roughnessFactor"]  = static_cast<double>(sourceMaterial.roughness());
                    mat["pbrMetallicRoughness"] = pbr;
                }

                // Alpha mode
                Material::BlendMode blendMode = sourceMaterial.blendMode();
                if (blendMode == Material::BlendMode::Opaque)
                    mat["alphaMode"] = "OPAQUE";
                else if (blendMode == Material::BlendMode::Masked)
                {
                    mat["alphaMode"] = "MASK";
                    mat["alphaCutoff"] = static_cast<double>(sourceMaterial.alphaThreshold());
                }
                else if (blendMode == Material::BlendMode::Alpha)
                    mat["alphaMode"] = "BLEND";

                if (sourceMaterial.twoSided())
                    mat["doubleSided"] = true;

                // Emissive factor
                QVector3D emissive = sourceMaterial.emissive();
                if (emissive.length() > 0.0f)
                {
                    QJsonArray arr;
                    arr.append(static_cast<double>(emissive.x()));
                    arr.append(static_cast<double>(emissive.y()));
                    arr.append(static_cast<double>(emissive.z()));
                    mat["emissiveFactor"] = arr;
                }

                // Material name
                QString correctName = sourceMaterial.name();
                if (!correctName.isEmpty())
                    mat["name"] = correctName;

                materials[matIdx] = mat;
                log(QString("  [VARIANT] mat[%1] '%2' patched").arg(matIdx).arg(correctName), logCallback);
            }
        }

        // Samplers have already been appended incrementally inside getOrCreateSampler().
        // Do NOT append createdSamplers again here, or we will duplicate sampler objects
        // and make the final sampler list unstable.
        log(QString("    Sampler state: %1 sampler(s) currently in JSON")
            .arg(samplers.size()), logCallback);

        // CRITICAL: Merge textures - include any added by findOrCreateTexture during material processing
        QJsonArray gltfTextures = gltfJson.value("textures").toArray();
        if (gltfTextures.size() > textures.size())
        {
            // New textures were added by findOrCreateTexture - append them
            for (int i = textures.size(); i < gltfTextures.size(); ++i)
            {
                textures.append(gltfTextures[i]);
                log(QString("  Merged extension texture[%1] from findOrCreateTexture").arg(i), logCallback);
            }
        }

        // CRITICAL: Write textures with sampler assignments to gltfJson first
        gltfJson["textures"] = textures;

        // CRITICAL: Reload textures array to include any created by findOrCreateTexture
        textures = gltfJson.value("textures").toArray();

        // NOTE: Since samplers are now merged (Assimp samplers + created samplers),
        // all sampler references remain valid. Assimp textures point to 0-N,
        // post-processor textures point to samplerBaseIndex+, and both sets are preserved.

        // MATERIAL DEDUPLICATION PASS
        // When the original model has multi-primitive meshes with shared material names
        // (e.g. 4 wheels each having "Rim2", "Tireside" etc.), Assimp creates separate
        // aiMaterial objects per aiMesh, resulting in duplicate material entries in the
        // exported JSON.  Deduplicate them so each unique name appears only once.
        // This prevents MaterialProcessor from hitting NAME AMBIGUOUS when reloading.
        // IMPORTANT: Only merge materials that are truly identical in content - same name
        // is not sufficient (a model may have distinct materials that share a name).
        {
            // Map from material JSON (as string) -> canonical index
            QMap<QString, int> contentToFirstIdx;
            QMap<int, int>     oldToCanonical;
            QJsonArray         dedupedMaterials;

            for (int i = 0; i < materials.size(); ++i)
            {
                QJsonObject matObj = materials[i].toObject();
                QString name = matObj.value("name").toString();

                // Serialize the full material JSON as the dedup key so that
                // two materials are only merged when their content is identical.
                QString contentKey = QJsonDocument(matObj).toJson(QJsonDocument::Compact);

                if (!name.isEmpty() && contentToFirstIdx.contains(contentKey))
                {
                    // Truly identical material: remap to the first occurrence
                    oldToCanonical[i] = contentToFirstIdx[contentKey];
                    log(QString("  Dedup material[%1] '%2' -> canonical[%3]")
                        .arg(i).arg(name).arg(contentToFirstIdx[contentKey]), logCallback);
                }
                else
                {
                    int newIdx = dedupedMaterials.size();
                    oldToCanonical[i] = newIdx;
                    if (!name.isEmpty())
                        contentToFirstIdx[contentKey] = newIdx;
                    dedupedMaterials.append(matObj);
                }
            }

            if (dedupedMaterials.size() < materials.size())
            {
                log(QString("  Material dedup: %1 -> %2 materials")
                    .arg(materials.size()).arg(dedupedMaterials.size()), logCallback);

                QMap<int, int> remappedMaterialToMesh;
                for (auto it = _materialToSourceMeshIndex.begin(); it != _materialToSourceMeshIndex.end(); ++it)
                {
                    if (!oldToCanonical.contains(it.key()))
                        continue;

                    int canonicalIdx = oldToCanonical[it.key()];
                    if (!remappedMaterialToMesh.contains(canonicalIdx))
                        remappedMaterialToMesh[canonicalIdx] = it.value();
                }

                materials = dedupedMaterials;
                _materialToSourceMeshIndex = remappedMaterialToMesh;

                // Update all mesh primitive material references
                QJsonArray meshesArr = gltfJson.value("meshes").toArray();
                for (int mi = 0; mi < meshesArr.size(); ++mi)
                {
                    QJsonObject mesh = meshesArr[mi].toObject();
                    QJsonArray prims = mesh["primitives"].toArray();
                    bool changed = false;
                    for (int pi = 0; pi < prims.size(); ++pi)
                    {
                        QJsonObject prim = prims[pi].toObject();
                        int oldIdx = prim.value("material").toInt(-1);
                        if (oldIdx >= 0 && oldToCanonical.contains(oldIdx))
                        {
                            int newIdx = oldToCanonical[oldIdx];
                            if (newIdx != oldIdx)
                            {
                                prim["material"] = newIdx;
                                prims[pi] = prim;
                                changed = true;
                            }
                        }
                    }
                    if (changed)
                    {
                        mesh["primitives"] = prims;
                        meshesArr[mi] = mesh;
                    }
                }
                gltfJson["meshes"] = meshesArr;

                // Remap _variantEntries and _variantMatByJsonIdx to post-dedup indices
                // so writeKhrMaterialsVariantsExtension writes correct material references.
                for (auto& entry : _variantEntries)
                {
                    QMap<int, int> remapped;
                    for (auto eit = entry.matKeyToJsonMatIdx.constBegin();
                         eit != entry.matKeyToJsonMatIdx.constEnd(); ++eit)
                    {
                        remapped[eit.key()] = oldToCanonical.value(eit.value(), eit.value());
                    }
                    entry.matKeyToJsonMatIdx = remapped;
                }
                {
                    QMap<int, Material> remappedMats;
                    for (auto mit = _variantMatByJsonIdx.constBegin();
                         mit != _variantMatByJsonIdx.constEnd(); ++mit)
                    {
                        int newIdx = oldToCanonical.value(mit.key(), mit.key());
                        remappedMats[newIdx] = mit.value();
                    }
                    _variantMatByJsonIdx = remappedMats;
                }
            }
        }

        gltfJson["materials"] = materials;
        gltfJson["samplers"] = samplers;
        textures = gltfJson.value("textures").toArray();
        gltfJson["textures"] = textures;

        // Verify what was actually written
        QJsonArray finalTextures = gltfJson.value("textures").toArray();
        for (int i = 0; i < finalTextures.size(); ++i)
        {
            QJsonObject tex = finalTextures[i].toObject();
            int samplerIdx = tex.value("sampler").toInt(-1);
            log(QString("  Final texture[%1] sampler: %2").arg(i).arg(samplerIdx), logCallback);
        }

        log(QString("  Final: %1 unique samplers created").arg(samplers.size()), logCallback);

        // THIRD-B PASS: Consolidate duplicate JSON materials
        // DISABLED: Material consolidation removed to preserve accuracy.
        // Deduplication (11->4) works correctly; consolidation caused index shifting bugs.
        // Slight redundancy in JSON materials is acceptable trade-off for correctness.
        // log("=== Consolidating Duplicate JSON Materials (DISABLED) ===", logCallback);

        // FOURTH PASS: Deduplicate images
        // Assimp may create duplicate image entries for the same file
        // We need to merge them and update texture references
        if (gltfJson.contains("images"))
        {
            QJsonArray images = gltfJson["images"].toArray();
            QMap<QString, int> uriToIndex;  // Maps URI to first occurrence index
            QMap<int, int> oldToNewIndex;   // Maps old image index to deduplicated index
            QJsonArray deduplicatedImages;

            // Build deduplication map
            for (int i = 0; i < images.size(); ++i)
            {
                QJsonObject img = images[i].toObject();
                QString uri = img.value("uri").toString();

                if (uriToIndex.contains(uri))
                {
                    // Duplicate - map to existing image
                    oldToNewIndex[i] = uriToIndex[uri];
                }
                else
                {
                    // New image - add to deduplicated array
                    int newIndex = deduplicatedImages.size();
                    uriToIndex[uri] = newIndex;
                    oldToNewIndex[i] = newIndex;
                    deduplicatedImages.append(img);
                }
            }

            // Update texture source references
            if (oldToNewIndex.size() != images.size())  // If we found duplicates
            {
                textures = gltfJson.value("textures").toArray();

                for (int i = 0; i < textures.size(); ++i)
                {
                    QJsonObject texture = textures[i].toObject();
                    int oldSource = texture.value("source").toInt(-1);

                    if (oldSource >= 0 && oldToNewIndex.contains(oldSource))
                    {
                        texture["source"] = oldToNewIndex[oldSource];
                        textures[i] = texture;
                    }
                }

                gltfJson["images"] = deduplicatedImages;
                gltfJson["textures"] = textures;

                log(QString("  Deduplicated images: %1 -> %2")
                    .arg(images.size())
                    .arg(deduplicatedImages.size()), logCallback);
            }
        }

        // ===== UPDATE extensionsUsed ARRAY =====
        // Scan all materials to see which extensions are actually used
        QSet<QString> usedExtensions;

        materials = gltfJson.value("materials").toArray();
        for (const QJsonValue& matVal : materials)
        {
            QJsonObject mat = matVal.toObject();
            if (mat.contains("extensions"))
            {
                QJsonObject exts = mat.value("extensions").toObject();
                for (const QString& extName : exts.keys())
                {
                    usedExtensions.insert(extName);
                }
            }
        }

        // Add to extensionsUsed array
        if (!usedExtensions.isEmpty())
        {
            QJsonArray extensionsUsed;
            if (gltfJson.contains("extensionsUsed"))
            {
                extensionsUsed = gltfJson["extensionsUsed"].toArray();
            }

            // Get existing extensions
            QSet<QString> existing;
            for (const QJsonValue& val : extensionsUsed)
            {
                existing.insert(val.toString());
            }

            // Add new ones
            for (const QString& ext : usedExtensions)
            {
                if (!existing.contains(ext))
                {
                    extensionsUsed.append(ext);
                    log(QString("  Added %1 to extensionsUsed").arg(ext), logCallback);
                }
            }

            gltfJson["extensionsUsed"] = extensionsUsed;
        }
    }

    // FIX: Correct normalTexture.scale (Assimp doesn't export this correctly)
    fixNormalTextureScale(gltfJson, meshes, logCallback);
    fixClearcoatNormalTextureScale(gltfJson, meshes, logCallback);

    // CRITICAL FIX: Correct KHR_materials_specular (or remove if default)
    fixSpecularExtension(gltfJson, meshes, logCallback);

    // CRITICAL FIX: Correct metallicFactor values (Assimp may set to 0 incorrectly)
    fixMetallicFactor(gltfJson, meshes, logCallback);

    if (gltfJson.contains("materials"))
    {
        QJsonArray mats = gltfJson["materials"].toArray();
        for (int i = 0; i < mats.size(); ++i)
        {
            QJsonObject mat = mats[i].toObject();
            log(QString("Material %1:").arg(i), logCallback);

            if (mat.contains("extensions") && mat["extensions"].toObject().contains("KHR_materials_specular"))
            {
                QJsonObject spec = mat["extensions"].toObject()["KHR_materials_specular"].toObject();
                if (spec.contains("specularTexture"))
                {
                    log(QString("  specularTexture index: %1").arg(spec["specularTexture"].toObject().value("index").toInt(-999)), logCallback);
                }
                if (spec.contains("specularColorTexture"))
                {
                    log(QString("  specularColorTexture index: %1").arg(spec["specularColorTexture"].toObject().value("index").toInt(-999)), logCallback);
                }
            }
        }
    }

    log("=== FINAL TEXTURE VALIDATION ===", logCallback);
    {
        QJsonArray finalTextures = gltfJson.value("textures").toArray();
        QJsonArray finalSamplers = gltfJson.value("samplers").toArray();
        int maxSamplerIndex = finalSamplers.size() - 1;
        bool anyFixed = false;

        log(QString("  Samplers available: %1 (indices 0-%2)").arg(finalSamplers.size()).arg(maxSamplerIndex), logCallback);
        log(QString("  Checking %1 textures...").arg(finalTextures.size()), logCallback);

        for (int i = 0; i < finalTextures.size(); ++i)
        {
            QJsonObject tex = finalTextures[i].toObject();
            bool needsFix = false;
            int samplerIdx = -1;

            if (tex.contains("sampler"))
            {
                samplerIdx = tex.value("sampler").toInt(-1);

                // Check if sampler index is invalid
                if (samplerIdx < 0 || samplerIdx > maxSamplerIndex)
                {
                    log(QString("  Texture[%1] has INVALID sampler index %2").arg(i).arg(samplerIdx), logCallback);
                    needsFix = true;
                }
                else
                {
                    log(QString("  Texture[%1] sampler: %2 (valid)").arg(i).arg(samplerIdx), logCallback);
                }
            }
            else
            {
                // Texture has NO sampler at all
                log(QString("  Texture[%1] has NO sampler reference").arg(i), logCallback);
                needsFix = true;
            }

            // Fix by assigning sampler 0
            if (needsFix)
            {
                if (finalSamplers.size() > 0)
                {
                    log(QString("    -> Setting sampler to 0"), logCallback);
                    tex["sampler"] = 0;
                    finalTextures[i] = tex;
                    anyFixed = true;
                }
                else
                {
                    log(QString("    -> WARNING: No samplers exist, cannot fix"), logCallback);
                }
            }
        }

        if (anyFixed)
        {
            log("  Writing fixed textures array back to gltfJson", logCallback);
            gltfJson["textures"] = finalTextures;

            // Verify the fix
            log("  Verification:", logCallback);
            QJsonArray verify = gltfJson.value("textures").toArray();
            for (int i = 0; i < verify.size(); ++i)
            {
                QJsonObject tex = verify[i].toObject();
                int samp = tex.value("sampler").toInt(-999);
                log(QString("    Texture[%1] sampler: %2").arg(i).arg(samp), logCallback);
            }
        }
        else
        {
            log("  All texture sampler references are valid", logCallback);
        }
    }


    // Write KHR_materials_variants extension if variant export data was set
    if (!_variantNames.isEmpty() && !_variantEntries.isEmpty())
        writeKhrMaterialsVariantsExtension(gltfJson, logCallback);

    if (!lights.empty())
        writePunctualLights(gltfJson, lights, logCallback);

    if (!_gltfCameras.isEmpty())
        writeGltfCameras(gltfJson, _gltfCameras, logCallback);

    if (_isGlbExport)
    {
        log("=== FIXING GLB IMAGE REFERENCES ===", logCallback);
        QJsonArray images = gltfJson.value("images").toArray();
        bool anyFixed = false;

        for (int i = 0; i < images.size(); ++i)
        {
            QJsonObject img = images[i].toObject();
            if (!img.contains("uri"))
                continue;

            const QString oldUri = img.value("uri").toString();
            if (oldUri.isEmpty())
                continue;

            // glTF requires an image to have EITHER uri OR bufferView, never
            // neither - only strip uri when a bufferView is already present
            // to take over (Assimp occasionally emits both for an image it
            // did manage to embed, which is the redundant-uri case this pass
            // is meant to clean up). An image Assimp DIDN'T manage to embed
            // has ONLY a uri and no bufferView at all - stripping it there
            // left the entry with neither, which is exactly what surfaced as
            // "images[N] should have either a URI or a bufferView and
            // mimetype" when reopening the exported file.
            if (!img.contains("bufferView"))
            {
                log(QString("  Image[%1] keeping uri '%2' - no bufferView to fall back to (not embedded by Assimp)").arg(i).arg(oldUri), logCallback);
                continue;
            }

            img.remove("uri");
            images[i] = img;
            anyFixed = true;
            log(QString("  Image[%1] cleared redundant uri '%2' (already has bufferView)").arg(i).arg(oldUri), logCallback);
        }

        if (anyFixed)
        {
            gltfJson["images"] = images;
            log("  Removed external image URIs from GLB JSON", logCallback);
        }
    }
    else
    {
        // FINAL PASS: Convert all image URIs to relative paths pointing to textures subfolder
        log("=== FIXING IMAGE URIS TO RELATIVE PATHS ===", logCallback);
        QJsonArray images = gltfJson.value("images").toArray();
        bool anyFixed = false;

        for (int i = 0; i < images.size(); ++i)
        {
            QJsonObject img = images[i].toObject();
            QString oldUri = img.value("uri").toString();

            // Skip already relative URIs that point to the correct subfolder
            if (oldUri.startsWith(_textureSubfolder + "/"))
            {
                log(QString("  Image[%1] already correct: %2").arg(i).arg(oldUri), logCallback);
                continue;
            }

            // Extract filename and rebuild URI as relative path
            QString filename = QFileInfo(oldUri).fileName();
            if (!filename.isEmpty())
            {
                QString newUri = _textureSubfolder + "/" + filename;
                img["uri"] = newUri;
                images[i] = img;
                anyFixed = true;
                log(QString("  Image[%1] fixed: '%2' -> '%3'").arg(i).arg(oldUri).arg(newUri), logCallback);
            }
        }

        if (anyFixed)
        {
            gltfJson["images"] = images;
            log("  Updated all image URIs in gltfJson", logCallback);
        }
    }

    // Then do standard post-processing (fills in missing properties with defaults)
    return postProcessGltfJson(gltfJson, logCallback);
}

QString GltfPostProcessor::normalisedGlbPath(const QString& path)
{
    if (path.startsWith("glb://") && path.contains("::"))
        return "glb://" + path.mid(path.lastIndexOf("::") + 2);
    return path;
}

bool GltfPostProcessor::fixTextureInfoWithTransforms(QJsonObject& parent, const QString& key)
{
    if (!parent.contains(key))
        return false;

    QJsonObject texInfo = parent[key].toObject();
    bool modified = false;

    if (!texInfo.contains("texCoord"))
    {
        texInfo["texCoord"] = 0;
        modified = true;
    }

    // Always ensure KHR_texture_transform exists with complete properties
    QJsonObject extensions;
    if (texInfo.contains("extensions"))
    {
        extensions = texInfo["extensions"].toObject();
    }

    QJsonObject transform;
    bool transformModified = false;

    if (extensions.contains("KHR_texture_transform"))
    {
        transform = extensions["KHR_texture_transform"].toObject();
    }
    else
    {
        // Create new transform with identity values
        transformModified = true;
    }

    // Add missing transform properties with identity defaults
    if (!transform.contains("offset"))
    {
        QJsonArray offset;
        offset.append(0.0);
        offset.append(0.0);
        transform["offset"] = offset;
        transformModified = true;
    }

    if (!transform.contains("rotation"))
    {
        transform["rotation"] = 0.0;
        transformModified = true;
    }

    if (!transform.contains("scale"))
    {
        QJsonArray scale;
        scale.append(1.0);
        scale.append(1.0);
        transform["scale"] = scale;
        transformModified = true;
    }

    // texCoord can also be specified inside the transform extension
    if (!transform.contains("texCoord") && texInfo.contains("texCoord"))
    {
        transform["texCoord"] = texInfo["texCoord"].toInt();
        transformModified = true;
    }

    if (transformModified)
    {
        extensions["KHR_texture_transform"] = transform;
        texInfo["extensions"] = extensions;
        modified = true;
    }

    if (modified)
    {
        parent[key] = texInfo;
    }

    return modified;
}

bool GltfPostProcessor::fixNormalTextureInfo(QJsonObject& parent, const QString& key)
{
    if (!parent.contains(key))
        return false;

    QJsonObject texInfo = parent[key].toObject();
    bool modified = false;

    // Add normal-specific scale property
    if (!texInfo.contains("scale"))
    {
        texInfo["scale"] = 1.0;
        modified = true;
    }

    // Store texInfo back if we modified it
    if (modified)
    {
        parent[key] = texInfo;
    }

    // Now handle common properties and transforms
    if (fixTextureInfoWithTransforms(parent, key))
    {
        modified = true;
    }

    return modified;
}

bool GltfPostProcessor::fixOcclusionTextureInfo(QJsonObject& parent, const QString& key, float occlusionStrength)
{
    if (!parent.contains(key))
        return false;

    QJsonObject texInfo = parent[key].toObject();
    bool modified = false;

    // Add occlusion-specific strength property using source material's value
    if (!texInfo.contains("strength"))
    {
        texInfo["strength"] = occlusionStrength;
        modified = true;
    }

    // Store texInfo back if we modified it
    if (modified)
    {
        parent[key] = texInfo;
    }

    // Now handle common properties and transforms
    if (fixTextureInfoWithTransforms(parent, key))
    {
        modified = true;
    }

    return modified;
}

bool GltfPostProcessor::fixNormalTextureScale(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    std::function<void(const QString&)> logCallback)
{
    bool modified = false;

    if (!gltfJson.contains("materials") || meshes.empty())
        return false;

    QJsonArray materials = gltfJson["materials"].toArray();

    for (int i = 0; i < materials.size(); ++i)
    {
        const SceneMesh* sourceMesh = sourceMeshForMaterial(i, meshes);
        if (!sourceMesh) continue;

        const Material glMat = sourceMaterialForJsonMaterial(i, meshes);
        QJsonObject mat = materials[i].toObject();

        // Fix normalTexture.scale if present
        if (mat.contains("normalTexture"))
        {
            QJsonObject normalTex = mat["normalTexture"].toObject();
            float normalScale = glMat.normalScale();

            // Write the scale (always write it, overriding Assimp's incorrect value)
            normalTex["scale"] = static_cast<double>(normalScale);
            mat["normalTexture"] = normalTex;
            materials[i] = mat;
            modified = true;

            log(QString("  -> Set normalTexture.scale to %1 for material %2")
                .arg(normalScale).arg(i), logCallback);
        }
    }

    if (modified)
    {
        gltfJson["materials"] = materials;  // CRITICAL: Write materials back!
        log("  -> normalTexture.scale values updated", logCallback);
    }

    return modified;
}

bool GltfPostProcessor::fixClearcoatNormalTextureScale(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    std::function<void(const QString&)> logCallback)
{
    bool modified = false;

    if (!gltfJson.contains("materials") || meshes.empty())
        return false;

    QJsonArray materials = gltfJson["materials"].toArray();

    for (int i = 0; i < materials.size(); ++i)
    {
        const SceneMesh* sourceMesh = sourceMeshForMaterial(i, meshes);
        if (!sourceMesh) continue;

        const Material glMat = sourceMaterialForJsonMaterial(i, meshes);
        QJsonObject mat = materials[i].toObject();
        QJsonObject extensions = mat.value("extensions").toObject();

        if (!extensions.contains("KHR_materials_clearcoat"))
            continue;

        QJsonObject clearcoat = extensions["KHR_materials_clearcoat"].toObject();
        if (!clearcoat.contains("clearcoatNormalTexture"))
            continue;

        QJsonObject clearcoatNormalTex = clearcoat["clearcoatNormalTexture"].toObject();
        const float clearcoatNormalScale = glMat.clearcoatNormalScale();
        clearcoatNormalTex["scale"] = static_cast<double>(clearcoatNormalScale);
        clearcoat["clearcoatNormalTexture"] = clearcoatNormalTex;
        extensions["KHR_materials_clearcoat"] = clearcoat;
        mat["extensions"] = extensions;
        materials[i] = mat;
        modified = true;

        log(QString("  -> Set clearcoatNormalTexture.scale to %1 for material %2")
            .arg(clearcoatNormalScale).arg(i), logCallback);
    }

    if (modified)
    {
        gltfJson["materials"] = materials;
        log("  -> clearcoatNormalTexture.scale values updated", logCallback);
    }

    return modified;
}

bool GltfPostProcessor::fixSpecularExtension(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    std::function<void(const QString&)> logCallback)
{
    bool modified = false;

    if (!gltfJson.contains("materials") || meshes.empty())
        return false;

    QJsonArray materials = gltfJson["materials"].toArray();

    for (int i = 0; i < materials.size(); ++i)
    {
        const SceneMesh* sourceMesh = sourceMeshForMaterial(i, meshes);
        if (!sourceMesh) continue;

        const Material glMat = sourceMaterialForJsonMaterial(i, meshes);
        QJsonObject mat = materials[i].toObject();

        // Check if material has KHR_materials_specular extension
        if (mat.contains("extensions"))
        {
            QJsonObject extensions = mat["extensions"].toObject();

            if (extensions.contains("KHR_materials_specular"))
            {
                // CRITICAL: Get the EXISTING specular object (preserves textures!)
                QJsonObject specular = extensions["KHR_materials_specular"].toObject();

                // Get the CORRECT values from Material
                float specularFactor = glMat.specularFactor();
                QVector3D specularColor = glMat.specularColorFactor();

                // Check if this extension should even be exported
                bool isDefaultValues = (std::abs(specularFactor - 1.0f) < 0.001f) &&
                    (std::abs(specularColor.x() - 1.0f) < 0.001f) &&
                    (std::abs(specularColor.y() - 1.0f) < 0.001f) &&
                    (std::abs(specularColor.z() - 1.0f) < 0.001f);

                // Check if there are textures
                bool hasTextures = specular.contains("specularTexture") ||
                    specular.contains("specularColorTexture");

                if (isDefaultValues && !hasTextures)
                {
                    // Remove the extension entirely if it's just defaults with no textures
                    extensions.remove("KHR_materials_specular");
                    mat["extensions"] = extensions;

                    // If no extensions left, remove the extensions object
                    if (extensions.isEmpty())
                    {
                        mat.remove("extensions");
                    }

                    materials[i] = mat;
                    modified = true;

                    log(QString("  -> Removed default KHR_materials_specular from material %1").arg(i), logCallback);
                }
                else
                {
                    // UPDATE the factor/color values while PRESERVING texture references
                    specular["specularFactor"] = static_cast<double>(specularFactor);

                    QJsonArray colorArray;
                    colorArray.append(static_cast<double>(specularColor.x()));
                    colorArray.append(static_cast<double>(specularColor.y()));
                    colorArray.append(static_cast<double>(specularColor.z()));
                    specular["specularColorFactor"] = colorArray;

                    // Write back the MODIFIED specular object (textures still intact!)
                    extensions["KHR_materials_specular"] = specular;
                    mat["extensions"] = extensions;
                    materials[i] = mat;
                    modified = true;

                    log(QString("  -> Fixed KHR_materials_specular for material %1: factor=%2, color=[%3, %4, %5]")
                        .arg(i).arg(specularFactor)
                        .arg(specularColor.x()).arg(specularColor.y()).arg(specularColor.z()),
                        logCallback);
                }
            }
        }
    }

    if (modified)
    {
        gltfJson["materials"] = materials;
    }

    return modified;
}

bool GltfPostProcessor::fixMetallicFactor(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    std::function<void(const QString&)> logCallback)
{
    bool modified = false;

    if (!gltfJson.contains("materials") || meshes.empty())
        return false;

    QJsonArray materials = gltfJson["materials"].toArray();

    for (int i = 0; i < materials.size(); ++i)
    {
        const SceneMesh* sourceMesh = sourceMeshForMaterial(i, meshes);
        if (!sourceMesh) continue;

        const Material glMat = sourceMaterialForJsonMaterial(i, meshes);
        QJsonObject mat = materials[i].toObject();

        if (mat.contains("pbrMetallicRoughness"))
        {
            QJsonObject pbr = mat["pbrMetallicRoughness"].toObject();
            float correctMetallic = glMat.metalness();  // Get the CORRECT value

            // Write the correct metallicFactor
            pbr["metallicFactor"] = static_cast<double>(correctMetallic);
            mat["pbrMetallicRoughness"] = pbr;
            materials[i] = mat;
            modified = true;

            log(QString("  -> Fixed metallicFactor to %1 for material %2")
                .arg(correctMetallic).arg(i), logCallback);
        }
    }

    if (modified)
    {
        gltfJson["materials"] = materials;
    }

    return modified;
}

bool GltfPostProcessor::fixSpecularTextures(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    std::function<void(const QString&)> logCallback)
{
    log("=== SPECULAR TEXTURE FIX START ===", logCallback);
    bool modified = false;

    if (!gltfJson.contains("materials"))
    {
        log("  -> No materials in gltfJson", logCallback);
        return false;
    }

    if (meshes.empty())
    {
        log("  -> No meshes provided", logCallback);
        return false;
    }

    QJsonArray materials = gltfJson["materials"].toArray();

    for (int i = 0; i < materials.size(); ++i)
    {
        const SceneMesh* sourceMesh = sourceMeshForMaterial(i, meshes);
        if (!sourceMesh)
        {
            log(QString("  -> No source mesh mapped for material %1, skipping").arg(i), logCallback);
            continue;
        }

        const Material glMat = sourceMaterialForJsonMaterial(i, meshes);

        // DEBUG: Check what maps are set
        QString specularFactorMap = glMat.specularFactorMap();
        QString specularColorMap = glMat.specularColorMap();

        log(QString("Material %1:").arg(i), logCallback);
        log(QString("  specularFactorMap: '%1'").arg(specularFactorMap), logCallback);
        log(QString("  specularColorMap: '%2'").arg(specularColorMap), logCallback);

        if (specularFactorMap.isEmpty() && specularColorMap.isEmpty())
        {
            log("  -> No specular maps, skipping", logCallback);
            continue;
        }

        QJsonObject mat = materials[i].toObject();

        // Check if material has KHR_materials_specular extension
        if (!mat.contains("extensions"))
        {
            log("  -> No extensions object, skipping", logCallback);
            continue;
        }

        QJsonObject extensions = mat["extensions"].toObject();

        if (!extensions.contains("KHR_materials_specular"))
        {
            log("  -> No KHR_materials_specular extension, skipping", logCallback);
            continue;
        }

        QJsonObject specular = extensions["KHR_materials_specular"].toObject();
        log("  -> Found KHR_materials_specular extension", logCallback);
        bool specularModified = false;

        // Add specularTexture if material has it
        if (!specularFactorMap.isEmpty())
        {
            log(QString("  -> Processing specularTexture: %1").arg(specularFactorMap), logCallback);

            int texIndex = findOrCreateTexture(gltfJson, specularFactorMap, -1, logCallback);
            log(QString("  -> findOrCreateTexture returned: %1").arg(texIndex), logCallback);

            if (texIndex >= 0)
            {
                QJsonObject texInfo;
                texInfo["index"] = texIndex;
                specular["specularTexture"] = texInfo;
                specularModified = true;

                log(QString("  -> Added specularTexture with index %1").arg(texIndex), logCallback);
            }
            else
            {
                log("  -> Failed to find/create specularTexture", logCallback);
            }
        }

        // Add specularColorTexture if material has it
        if (!specularColorMap.isEmpty())
        {
            log(QString("  -> Processing specularColorTexture: %1").arg(specularColorMap), logCallback);

            int texIndex = findOrCreateTexture(gltfJson, specularColorMap, -1, logCallback);
            log(QString("  -> findOrCreateTexture returned: %1").arg(texIndex), logCallback);

            if (texIndex >= 0)
            {
                QJsonObject texInfo;
                texInfo["index"] = texIndex;
                specular["specularColorTexture"] = texInfo;
                specularModified = true;

                log(QString("  -> Added specularColorTexture with index %1").arg(texIndex), logCallback);
            }
            else
            {
                log("  -> Failed to find/create specularColorTexture", logCallback);
            }
        }

        if (specularModified)
        {
            extensions["KHR_materials_specular"] = specular;
            mat["extensions"] = extensions;
            materials[i] = mat;
            modified = true;

            log(QString("  -> Modified material %1").arg(i), logCallback);
        }
    }

    if (modified)
    {
        gltfJson["materials"] = materials;
        log("  -> Updated materials array in gltfJson", logCallback);
    }

    log("=== SPECULAR TEXTURE FIX END ===", logCallback);
    return modified;
}

// Helper function with extensive logging
int GltfPostProcessor::findOrCreateTexture(
    QJsonObject& gltfJson,
    const QString& imagePath,
    int desiredSamplerIdx,
    std::function<void(const QString&)> logCallback)
{
    log(QString("findOrCreateTexture called with: '%1'").arg(imagePath), logCallback);

    // CRITICAL: Get fresh copies EVERY time
    QJsonArray images = gltfJson.value("images").toArray();
    QJsonArray textures = gltfJson.value("textures").toArray();

    log(QString("  Current images count: %1").arg(images.size()), logCallback);
    log(QString("  Current textures count: %1").arg(textures.size()), logCallback);

    // Extract just the filename from the path
    QString imageFileName = QFileInfo(normalisedGlbPath(imagePath)).fileName();
    log(QString("  Extracted filename: '%1'").arg(imageFileName), logCallback);

    // Look for EXISTING texture that already references this image
    // FIRST: Find the image index
    int imageIndex = -1;

    auto embeddedIt = _embeddedIndexMapping.find(imagePath);
    if (embeddedIt != _embeddedIndexMapping.end() &&
        embeddedIt.value() >= 0 &&
        embeddedIt.value() < images.size())
    {
        imageIndex = embeddedIt.value();
        log(QString("  -> Using authoritative embedded image index %1").arg(imageIndex), logCallback);
    }

    for (int i = 0; i < images.size(); ++i)
    {
        if (imageIndex >= 0)
            break;
        QJsonObject img = images[i].toObject();
        QString uri = img.value("uri").toString();
        log(QString("  Checking image %1: uri='%2'").arg(i).arg(uri), logCallback);

        if (uri.endsWith(imageFileName) ||
            QFileInfo(uri).completeBaseName().compare(imageFileName, Qt::CaseInsensitive) == 0)
        {
            imageIndex = i;
            log(QString("  -> Found matching image at index %1").arg(i), logCallback);
            break;
        }
    }

    // GLB fallback: match by "name" field (URI is empty for embedded images)
    if (imageIndex < 0 && !_pathMapping.isEmpty())
    {
        QString packagedRelPath = resolvePackagedPath(_pathMapping, imagePath);
        QString packagedName = QFileInfo(packagedRelPath).fileName();
        if (!packagedName.isEmpty())
        {
            for (int i = 0; i < images.size(); ++i)
            {
                QString imgName = images[i].toObject().value("name").toString();
                if (!imgName.isEmpty() &&
                    QFileInfo(imgName).fileName().compare(packagedName, Qt::CaseInsensitive) == 0)
                {
                    imageIndex = i;
                    log(QString("  -> Found embedded image at index %1 by name '%2'")
                        .arg(i).arg(packagedName), logCallback);
                    break;
                }
            }
        }
    }

    // GLB name fallback: match imageFileName against base name of each image's "name" field.
    // Handles cases like imagePath="glb://image_1" -> imageFileName="image_1",
    // which should match an image with name="image_1.png".
    if (imageIndex < 0)
    {
        QString imageBaseName = QFileInfo(imageFileName).completeBaseName();
        if (imageBaseName.isEmpty()) imageBaseName = imageFileName; // no extension case

        for (int i = 0; i < images.size(); ++i)
        {
            QString imgName = images[i].toObject().value("name").toString();
            if (!imgName.isEmpty())
            {
                QString imgBase = QFileInfo(imgName).completeBaseName();
                if (imgBase.compare(imageBaseName, Qt::CaseInsensitive) == 0)
                {
                    imageIndex = i;
                    log(QString("  -> Found embedded image at index %1 by base name '%2'")
                        .arg(i).arg(imageBaseName), logCallback);
                    break;
                }
            }
        }
    }

    // If image not found, create it
    if (imageIndex < 0)
    {
        if (_isGlbExport)
        {
            log(QString("  -> WARNING: Missing embedded GLB image for '%1'; not creating external uri")
                .arg(imagePath), logCallback);
            return -1;
        }

        QString newUri = _textureSubfolder + "/" + imageFileName;
        QJsonObject newImage;
        newImage["uri"] = newUri;
        images.append(newImage);
        imageIndex = images.size() - 1;

        // CRITICAL: Write back immediately
        gltfJson["images"] = images;

        log(QString("  -> Created new image at index %1 with uri='%2'").arg(imageIndex).arg(newUri), logCallback);
    }

    // NOW check if a texture already points to this image
    for (int i = 0; i < textures.size(); ++i)
    {
        QJsonObject tex = textures[i].toObject();
        int source = tex.value("source").toInt(-1);
        int sampler = tex.contains("sampler") ? tex.value("sampler").toInt(-1) : -1;

        if (source == imageIndex && (desiredSamplerIdx < 0 || sampler == desiredSamplerIdx))
        {
            log(QString("    -> Found existing texture at index %1 pointing to image %2 with sampler %3")
                .arg(i).arg(imageIndex).arg(sampler), logCallback);
            return i;
        }
    }

    // Create NEW texture entry pointing to this image
    QJsonObject newTexture;
    newTexture["source"] = imageIndex;

    if (desiredSamplerIdx >= 0)
    {
        newTexture["sampler"] = desiredSamplerIdx;
        log(QString("    -> Added sampler reference to texture (index %1)").arg(desiredSamplerIdx), logCallback);
    }
    else
    {
        log("    -> No explicit sampler requested, leaving texture without sampler for now", logCallback);
    }

    textures.append(newTexture);
    int textureIndex = textures.size() - 1;

    // CRITICAL: Write back immediately
    gltfJson["textures"] = textures;

    log(QString("  -> Created new texture at index %1 pointing to image %2").arg(textureIndex).arg(imageIndex), logCallback);

    return textureIndex;
}

bool GltfPostProcessor::mergeOpacityIntoBaseColorTextures(
    QJsonObject& gltfJson,
    const std::vector<SceneMesh*>& meshes,
    const QString& exportFilePath,
    QByteArray* glbBinaryChunk,
    std::function<void(const QString&)> logCallback)
{
    QJsonArray materials = gltfJson.value("materials").toArray();
    if (materials.isEmpty())
        return false;

    QJsonArray images = gltfJson.value("images").toArray();
    QJsonArray textures = gltfJson.value("textures").toArray();
    QJsonArray bufferViews = gltfJson.value("bufferViews").toArray();
    bool addedTextureTransform = false;

    QByteArray glbPayload;
    if (glbBinaryChunk && !glbBinaryChunk->isEmpty())
    {
        if (glbBinaryChunk->size() >= 8)
        {
            const char* data = glbBinaryChunk->constData();
            quint32 chunkLength = *reinterpret_cast<const quint32*>(data);
            quint32 chunkType = *reinterpret_cast<const quint32*>(data + 4);
            if (chunkType == 0x004E4942 && glbBinaryChunk->size() >= static_cast<int>(chunkLength + 8))
                glbPayload = glbBinaryChunk->mid(8, chunkLength);
        }
    }

    bool modified = false;
    QDir exportDir(QFileInfo(exportFilePath).dir());

    for (int matIdx = 0; matIdx < materials.size(); ++matIdx)
    {
        Material sourceMaterial = sourceMaterialForJsonMaterial(matIdx, meshes);
        if (sourceMaterial.opacityMapPath().isEmpty())
            continue;

        QJsonObject mat = materials[matIdx].toObject();
        QJsonObject pbr = mat.value("pbrMetallicRoughness").toObject();
        if (pbr.isEmpty())
            continue;

        const QString opacityPath = resolveTextureFileForExport(
            sourceMaterial.opacityMapPath(), exportFilePath, _pathMapping, _isGlbExport);
        if (opacityPath.isEmpty())
        {
            log(QString("  [OpacityMerge] No readable opacity source for material[%1]: %2")
                .arg(matIdx).arg(sourceMaterial.opacityMapPath()), logCallback);
            continue;
        }

        QString baseColorPath;
        const Material::Texture* bindingSource = nullptr;
        if (!sourceMaterial.albedoMapPath().isEmpty())
        {
            baseColorPath = resolveTextureFileForExport(
                sourceMaterial.albedoMapPath(), exportFilePath, _pathMapping, _isGlbExport);
            bindingSource = &sourceMaterial.texture(Material::TextureType::Albedo);
        }
        else if (!sourceMaterial.diffuseMapPath().isEmpty())
        {
            baseColorPath = resolveTextureFileForExport(
                sourceMaterial.diffuseMapPath(), exportFilePath, _pathMapping, _isGlbExport);
            bindingSource = &sourceMaterial.texture(Material::TextureType::Diffuse);
        }
        else
        {
            bindingSource = &sourceMaterial.texture(Material::TextureType::Opacity);
        }

        QImage merged = buildMergedBaseColorImage(sourceMaterial, baseColorPath, opacityPath, logCallback);
        if (merged.isNull())
            continue;

        QJsonObject texInfo;
        int newTextureIndex = -1;

        if (glbBinaryChunk)
        {
            QByteArray pngData;
            QBuffer buffer(&pngData);
            buffer.open(QIODevice::WriteOnly);
            if (!merged.save(&buffer, "PNG"))
            {
                log(QString("  [OpacityMerge] Failed to encode merged PNG for material[%1]").arg(matIdx), logCallback);
                continue;
            }

            while (glbPayload.size() % 4 != 0)
                glbPayload.append('\0');

            const int byteOffset = glbPayload.size();
            glbPayload.append(pngData);

            const int bufferViewIndex = bufferViews.size();
            QJsonObject newBufferView;
            newBufferView["buffer"] = 0;
            newBufferView["byteOffset"] = byteOffset;
            newBufferView["byteLength"] = pngData.size();
            bufferViews.append(newBufferView);

            const int imageIndex = images.size();
            QJsonObject newImage;
            newImage["bufferView"] = bufferViewIndex;
            newImage["mimeType"] = "image/png";
            newImage["name"] = QString("baseColorOpacity_%1.png").arg(matIdx);
            images.append(newImage);

            QJsonObject newTexture;
            newTexture["source"] = imageIndex;
            const int oldTextureIndex = pbr.value("baseColorTexture").toObject().value("index").toInt(-1);
            if (oldTextureIndex >= 0 && oldTextureIndex < textures.size())
            {
                QJsonObject oldTexture = textures[oldTextureIndex].toObject();
                if (oldTexture.contains("sampler"))
                    newTexture["sampler"] = oldTexture.value("sampler").toInt();
            }
            newTextureIndex = textures.size();
            textures.append(newTexture);
        }
        else
        {
            QString mergedDir = exportDir.filePath(_textureSubfolder);
            QDir().mkpath(mergedDir);
            QString mergedPath = QDir(mergedDir).filePath(QString("baseColorOpacity_%1.png").arg(matIdx));
            if (!merged.save(mergedPath, "PNG"))
            {
                log(QString("  [OpacityMerge] Failed to save merged image: %1").arg(mergedPath), logCallback);
                continue;
            }

            const int imageIndex = images.size();
            QJsonObject newImage;
            newImage["uri"] = _textureSubfolder + "/" + QFileInfo(mergedPath).fileName();
            images.append(newImage);

            QJsonObject newTexture;
            newTexture["source"] = imageIndex;
            const int oldTextureIndex = pbr.value("baseColorTexture").toObject().value("index").toInt(-1);
            if (oldTextureIndex >= 0 && oldTextureIndex < textures.size())
            {
                QJsonObject oldTexture = textures[oldTextureIndex].toObject();
                if (oldTexture.contains("sampler"))
                    newTexture["sampler"] = oldTexture.value("sampler").toInt();
            }
            newTextureIndex = textures.size();
            textures.append(newTexture);
        }

        if (newTextureIndex < 0)
            continue;

        texInfo["index"] = newTextureIndex;
        if (bindingSource)
        {
            applyTextureTransformToInfo(texInfo, *bindingSource);
            addedTextureTransform = addedTextureTransform || !isIdentityTextureTransform(*bindingSource);
        }

        pbr["baseColorTexture"] = texInfo;

        QJsonArray baseColorFactor = pbr.value("baseColorFactor").toArray();
        if (baseColorFactor.size() < 4)
        {
            QVector3D albedo = sourceMaterial.albedoColor();
            baseColorFactor = QJsonArray{
                static_cast<double>(albedo.x()),
                static_cast<double>(albedo.y()),
                static_cast<double>(albedo.z()),
                static_cast<double>(sourceMaterial.opacity())
            };
        }
        else
        {
            baseColorFactor[3] = static_cast<double>(sourceMaterial.opacity());
        }
        pbr["baseColorFactor"] = baseColorFactor;

        mat["pbrMetallicRoughness"] = pbr;
        materials[matIdx] = mat;
        modified = true;

        log(QString("  [OpacityMerge] Material[%1] merged opacity into baseColor alpha").arg(matIdx), logCallback);
    }

    if (!modified)
        return false;

    gltfJson["materials"] = materials;
    gltfJson["images"] = images;
    gltfJson["textures"] = textures;
    if (!bufferViews.isEmpty())
        gltfJson["bufferViews"] = bufferViews;

    // The bufferViews above (for the GLB path) reference bytes appended to
    // glbPayload past whatever buffers[0].byteLength currently says -
    // without this, the file gets written with bufferViews that overrun the
    // declared buffer length, which a spec-compliant loader rejects on
    // reopen (a GLB's embedded buffer has no "uri", so this surfaces as a
    // buffer/uri-related error even though the real defect is this length
    // field). Mirrors injectPointerAnimationChannels()/
    // injectMorphWeightAnimations() below, which already patch this the
    // same way after their own binary-chunk appends.
    if (glbBinaryChunk && !glbPayload.isEmpty())
    {
        QJsonArray buffers = gltfJson.value("buffers").toArray();
        if (!buffers.isEmpty())
        {
            QJsonObject buf0 = buffers[0].toObject();
            if (buf0.value("byteLength").toInt() < glbPayload.size())
            {
                buf0["byteLength"] = glbPayload.size();
                buffers[0] = buf0;
                gltfJson["buffers"] = buffers;
            }
        }
    }

    if (addedTextureTransform)
    {
        QJsonArray extensionsUsed = gltfJson.value("extensionsUsed").toArray();
        bool found = false;
        for (const auto& value : std::as_const(extensionsUsed))
        {
            if (value.toString() == "KHR_texture_transform")
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            extensionsUsed.append("KHR_texture_transform");
            gltfJson["extensionsUsed"] = extensionsUsed;
        }
    }

    if (glbBinaryChunk)
    {
        QByteArray rebuiltChunk;
        if (!glbPayload.isEmpty())
        {
            while (glbPayload.size() % 4 != 0)
                glbPayload.append('\0');

            const quint32 chunkLength = static_cast<quint32>(glbPayload.size());
            const quint32 chunkType = 0x004E4942; // BIN\0
            rebuiltChunk.append(reinterpret_cast<const char*>(&chunkLength), 4);
            rebuiltChunk.append(reinterpret_cast<const char*>(&chunkType), 4);
            rebuiltChunk.append(glbPayload);
        }
        *glbBinaryChunk = rebuiltChunk;
    }

    return true;
}

// ---------------------------------------------------------------------------
// injectPointerAnimationChannels
//
// Injects KHR_animation_pointer channels for Pointer-path animation data
// (material texture-transform, node visibility) that Assimp's aiAnimation
// cannot represent.  Called after the main postProcessGltfJsonWithMaterials()
// pass when _pointerAnimData is non-empty.
//
// Strategy:
//  - For each GltfAnimationClip that has hasPointerAnimations == true, find
//    the matching animation in gltfJson["animations"] by clip name.
//  - For each Pointer channel, write a float accessor for the time values and
//    a float/vec2 accessor for the channel values.
//  - Accessor data is accumulated in a single QByteArray; for glTF the buffer
//    is embedded as a data URI; for GLB it is appended to *glbBinaryChunk and
//    the existing buffer's byteLength is patched.
//  - Add the sampler (input/output accessors + "STEP" interpolation) and
//    channel (target with "pointer" path + KHR_animation_pointer extension)
//    to the matching animation object.
// ---------------------------------------------------------------------------
bool GltfPostProcessor::injectPointerAnimationChannels(
    QJsonObject& gltfJson,
    QByteArray*  glbBinaryChunk,
    std::function<void(const QString&)> logCallback)
{
    if (_pointerAnimData.isEmpty())
        return false;

    // Check if there are any Pointer channels at all.
    bool hasAny = false;
    for (const GltfAnimationData& ad : std::as_const(_pointerAnimData))
        for (const GltfAnimationClip& clip : ad.clips)
            if (clip.hasPointerAnimations)
                for (const GltfAnimationChannel& ch : clip.channels)
                    if (ch.targetPath == GltfAnimationTargetPath::Pointer)
                    { hasAny = true; break; }
    if (!hasAny)
        return false;

    // ------------------------------------------------------------------
    // Build the binary blob for all new accessors.
    // ------------------------------------------------------------------
    QByteArray newBinary;
    QJsonArray accessors  = gltfJson[QStringLiteral("accessors")].toArray();
    QJsonArray bufferViews = gltfJson[QStringLiteral("bufferViews")].toArray();
    QJsonArray animations = gltfJson[QStringLiteral("animations")].toArray();
    QJsonArray buffers    = gltfJson[QStringLiteral("buffers")].toArray();

    // Determine the buffer index we will append to / create.
    // For GLB we reuse buffer 0; for gltF we will create a new buffer at the end.
    const int bufferIndex = glbBinaryChunk ? 0
                                           : static_cast<int>(buffers.size());
    // Byte offset into *this* buffer where our data starts.
    // For GLB mode: use the actual binary chunk data size (excluding the 8-byte chunk header),
    // NOT buffers[0].byteLength - kept as a defensive fallback even though the two known
    // causes of byteLength lagging the real chunk size (patchGlbImageNames() in
    // AssImpMeshExporter.cpp and mergeOpacityIntoBaseColorTextures() above, both of which
    // append extra image bytes to the binary chunk) now patch byteLength themselves. If
    // either of those regresses again, trusting byteLength here would place new accessor
    // data in the middle of already-written image data rather than at the true end of the
    // binary chunk.
    int byteOffsetBase = glbBinaryChunk
                       ? (glbBinaryChunk->size() >= 8 ? glbBinaryChunk->size() - 8 : 0)
                       : 0;
    qDebug() << "[INJECT-POINTER] byteOffsetBase=" << byteOffsetBase
             << "buffers[0].byteLength=" << (buffers.size() > 0 ? buffers[0].toObject()["byteLength"].toInt() : -1);

    // Helper: append floats and return (bufferViewIndex, accessorIndex).
    auto appendAccessor = [&](const QVector<float>& data,
                              const QString& type,
                              int componentCount) -> int
    {
        if (data.isEmpty())
            return -1;

        // 4-byte align start of this view.
        while (newBinary.size() % 4 != 0)
            newBinary.append('\0');

        const int byteOffset  = byteOffsetBase + newBinary.size();
        const int byteLength  = static_cast<int>(data.size()) * 4;
        newBinary.append(reinterpret_cast<const char*>(data.constData()), byteLength);

        QJsonObject bv;
        bv.insert(QStringLiteral("buffer"),     bufferIndex);
        bv.insert(QStringLiteral("byteOffset"), byteOffset);
        bv.insert(QStringLiteral("byteLength"), byteLength);
        const int bvIdx = static_cast<int>(bufferViews.size());
        bufferViews.append(bv);

        float minVal = data[0], maxVal = data[0];
        for (float f : data) { minVal = std::min(minVal, f); maxVal = std::max(maxVal, f); }

        QJsonObject acc;
        acc.insert(QStringLiteral("bufferView"),    bvIdx);
        acc.insert(QStringLiteral("byteOffset"),    0);
        acc.insert(QStringLiteral("componentType"), 5126); // FLOAT
        acc.insert(QStringLiteral("count"),
                   static_cast<int>(data.size()) / componentCount);
        acc.insert(QStringLiteral("type"),          type);
        const int accIdx = static_cast<int>(accessors.size());
        accessors.append(acc);

        return accIdx;
    };

    // Helper: find animation by clip name.
    auto findAnimByName = [&](const QString& name) -> int {
        for (int i = 0; i < animations.size(); ++i)
            if (animations[i].toObject()[QStringLiteral("name")].toString() == name)
                return i;
        return -1;
    };

    // Helper: resolve glTF node index from name.
    auto nodeIndexInJson = [&](const QString& name) -> int {
        const QJsonArray nodes = gltfJson[QStringLiteral("nodes")].toArray();
        for (int i = 0; i < nodes.size(); ++i)
            if (nodes[i].toObject()[QStringLiteral("name")].toString() == name)
                return i;
        return -1;
    };

    // Helper: convert texture target enum to the glTF JSON path segment used
    // in KHR_animation_pointer pointer strings.
    // Pattern: /materials/{idx}/{texSlotPath}/extensions/KHR_texture_transform/{prop}
    auto textureTargetToJsonPath = [](GltfAnimationTextureTarget t) -> QString {
        switch (t)
        {
        case GltfAnimationTextureTarget::Albedo:
            return QStringLiteral("pbrMetallicRoughness/baseColorTexture");
        case GltfAnimationTextureTarget::MetallicRoughness:
            return QStringLiteral("pbrMetallicRoughness/metallicRoughnessTexture");
        case GltfAnimationTextureTarget::Normal:
            return QStringLiteral("normalTexture");
        case GltfAnimationTextureTarget::Occlusion:
            return QStringLiteral("occlusionTexture");
        case GltfAnimationTextureTarget::Emissive:
            return QStringLiteral("emissiveTexture");
        case GltfAnimationTextureTarget::Transmission:
            return QStringLiteral("extensions/KHR_materials_transmission/transmissionTexture");
        case GltfAnimationTextureTarget::Thickness:
            return QStringLiteral("extensions/KHR_materials_volume/thicknessTexture");
        case GltfAnimationTextureTarget::SheenColor:
            return QStringLiteral("extensions/KHR_materials_sheen/sheenColorTexture");
        case GltfAnimationTextureTarget::SheenRoughness:
            return QStringLiteral("extensions/KHR_materials_sheen/sheenRoughnessTexture");
        case GltfAnimationTextureTarget::Clearcoat:
            return QStringLiteral("extensions/KHR_materials_clearcoat/clearcoatTexture");
        case GltfAnimationTextureTarget::ClearcoatRoughness:
            return QStringLiteral("extensions/KHR_materials_clearcoat/clearcoatRoughnessTexture");
        case GltfAnimationTextureTarget::ClearcoatNormal:
            return QStringLiteral("extensions/KHR_materials_clearcoat/clearcoatNormalTexture");
        case GltfAnimationTextureTarget::Iridescence:
            return QStringLiteral("extensions/KHR_materials_iridescence/iridescenceTexture");
        case GltfAnimationTextureTarget::IridescenceThickness:
            return QStringLiteral("extensions/KHR_materials_iridescence/iridescenceThicknessTexture");
        case GltfAnimationTextureTarget::SpecularFactor:
            return QStringLiteral("extensions/KHR_materials_specular/specularTexture");
        case GltfAnimationTextureTarget::SpecularColor:
            return QStringLiteral("extensions/KHR_materials_specular/specularColorTexture");
        case GltfAnimationTextureTarget::Anisotropy:
            return QStringLiteral("extensions/KHR_materials_anisotropy/anisotropyTexture");
        case GltfAnimationTextureTarget::DiffuseTransmission:
            return QStringLiteral("extensions/KHR_materials_diffuse_transmission/diffuseTransmissionTexture");
        case GltfAnimationTextureTarget::DiffuseTransmissionColor:
            return QStringLiteral("extensions/KHR_materials_diffuse_transmission/diffuseTransmissionColorTexture");
        case GltfAnimationTextureTarget::Diffuse:
            return QStringLiteral("extensions/KHR_materials_pbrSpecularGlossiness/diffuseTexture");
        case GltfAnimationTextureTarget::SpecularGlossiness:
            return QStringLiteral("extensions/KHR_materials_pbrSpecularGlossiness/specularGlossinessTexture");
        default:
            return QString(); // Unknown — caller should skip
        }
    };

    bool modified = false;

    for (const GltfAnimationData& ad : std::as_const(_pointerAnimData))
    {
        for (const GltfAnimationClip& clip : ad.clips)
        {
            if (!clip.hasPointerAnimations)
                continue;

            int animIdx = findAnimByName(clip.name);
            QJsonObject animObj;
            if (animIdx >= 0)
                animObj = animations[animIdx].toObject();
            else
            {
                // No TRS channels were exported for this clip; create a new animation.
                animObj.insert(QStringLiteral("name"), clip.name);
                animIdx = static_cast<int>(animations.size());
                animations.append(animObj);
            }

            QJsonArray samplers = animObj[QStringLiteral("samplers")].toArray();
            QJsonArray channels = animObj[QStringLiteral("channels")].toArray();

            for (const GltfAnimationChannel& ch : clip.channels)
            {
                if (ch.targetPath != GltfAnimationTargetPath::Pointer)
                    continue;

                // Build time values accessor.
                QVector<float> times;

                // Determine the pointer path and value accessor based on property type.
                QString pointerPath;
                int inputAccIdx  = -1;
                int outputAccIdx = -1;

                switch (ch.pointerProperty)
                {
                case GltfAnimationPointerProperty::Visibility:
                {
                    // /nodes/{idx}/extensions/KHR_node_visibility/visible
                    const QString nodeName = ch.targetNodeName;
                    const int jsonNodeIdx  = nodeIndexInJson(nodeName);
                    if (jsonNodeIdx < 0)
                    {
                        log(QString("  [POINTER] Node not found: %1, skipping").arg(nodeName), logCallback);
                        continue;
                    }
                    pointerPath = QString("/nodes/%1/extensions/KHR_node_visibility/visible")
                                      .arg(jsonNodeIdx);

                    for (const GltfAnimationBoolKey& k : ch.boolKeys)
                        times.append(static_cast<float>(k.timeSeconds));
                    inputAccIdx = appendAccessor(times, QStringLiteral("SCALAR"), 1);

                    QVector<float> values;
                    values.reserve(ch.boolKeys.size());
                    for (const GltfAnimationBoolKey& k : ch.boolKeys)
                        values.append(k.value ? 1.0f : 0.0f);
                    outputAccIdx = appendAccessor(values, QStringLiteral("SCALAR"), 1);
                    break;
                }

                case GltfAnimationPointerProperty::Offset:
                {
                    // /materials/{matIdx}/{texSlot}/extensions/KHR_texture_transform/offset
                    if (ch.targetMaterialIndex < 0)
                        continue;
                    const QString texSlotOff = textureTargetToJsonPath(ch.textureTarget);
                    if (texSlotOff.isEmpty())
                        continue;
                    pointerPath = QString(
                        "/materials/%1/%2/extensions/KHR_texture_transform/offset")
                            .arg(ch.targetMaterialIndex).arg(texSlotOff);

                    for (const GltfAnimationVec2Key& k : ch.vec2Keys)
                        times.append(static_cast<float>(k.timeSeconds));
                    inputAccIdx = appendAccessor(times, QStringLiteral("SCALAR"), 1);

                    QVector<float> values;
                    values.reserve(ch.vec2Keys.size() * 2);
                    for (const GltfAnimationVec2Key& k : ch.vec2Keys)
                    { values.append(k.value.x()); values.append(k.value.y()); }
                    outputAccIdx = appendAccessor(values, QStringLiteral("VEC2"), 2);
                    break;
                }

                case GltfAnimationPointerProperty::Scale:
                {
                    if (ch.targetMaterialIndex < 0)
                        continue;
                    const QString texSlotScale = textureTargetToJsonPath(ch.textureTarget);
                    if (texSlotScale.isEmpty())
                        continue;
                    pointerPath = QString(
                        "/materials/%1/%2/extensions/KHR_texture_transform/scale")
                            .arg(ch.targetMaterialIndex).arg(texSlotScale);

                    for (const GltfAnimationVec2Key& k : ch.vec2Keys)
                        times.append(static_cast<float>(k.timeSeconds));
                    inputAccIdx = appendAccessor(times, QStringLiteral("SCALAR"), 1);

                    QVector<float> values;
                    values.reserve(ch.vec2Keys.size() * 2);
                    for (const GltfAnimationVec2Key& k : ch.vec2Keys)
                    { values.append(k.value.x()); values.append(k.value.y()); }
                    outputAccIdx = appendAccessor(values, QStringLiteral("VEC2"), 2);
                    break;
                }

                case GltfAnimationPointerProperty::Rotation:
                {
                    if (ch.targetMaterialIndex < 0)
                        continue;
                    const QString texSlotRot = textureTargetToJsonPath(ch.textureTarget);
                    if (texSlotRot.isEmpty())
                        continue;
                    pointerPath = QString(
                        "/materials/%1/%2/extensions/KHR_texture_transform/rotation")
                            .arg(ch.targetMaterialIndex).arg(texSlotRot);

                    for (const GltfAnimationFloatKey& k : ch.floatKeys)
                        times.append(static_cast<float>(k.timeSeconds));
                    inputAccIdx = appendAccessor(times, QStringLiteral("SCALAR"), 1);

                    QVector<float> values;
                    values.reserve(ch.floatKeys.size());
                    for (const GltfAnimationFloatKey& k : ch.floatKeys)
                        values.append(k.value);
                    outputAccIdx = appendAccessor(values, QStringLiteral("SCALAR"), 1);
                    break;
                }

                case GltfAnimationPointerProperty::BaseColorFactor:
                {
                    if (ch.targetMaterialIndex < 0)
                        continue;
                    pointerPath = QString(
                        "/materials/%1/pbrMetallicRoughness/baseColorFactor")
                            .arg(ch.targetMaterialIndex);

                    for (const GltfAnimationVec4Key& k : ch.vec4Keys)
                        times.append(static_cast<float>(k.timeSeconds));
                    inputAccIdx = appendAccessor(times, QStringLiteral("SCALAR"), 1);

                    QVector<float> values;
                    values.reserve(ch.vec4Keys.size() * 4);
                    for (const GltfAnimationVec4Key& k : ch.vec4Keys)
                    {
                        values.append(k.value.x()); values.append(k.value.y());
                        values.append(k.value.z()); values.append(k.value.w());
                    }
                    outputAccIdx = appendAccessor(values, QStringLiteral("VEC4"), 4);
                    break;
                }

                default:
                    continue; // Unsupported pointer property; skip.
                }

                if (inputAccIdx < 0 || outputAccIdx < 0 || pointerPath.isEmpty())
                    continue;

                // Add sampler (STEP interpolation for all pointer animations).
                QJsonObject sampler;
                sampler.insert(QStringLiteral("input"),         inputAccIdx);
                sampler.insert(QStringLiteral("output"),        outputAccIdx);
                sampler.insert(QStringLiteral("interpolation"), QStringLiteral("STEP"));
                const int samplerIdx = static_cast<int>(samplers.size());
                samplers.append(sampler);

                // Add channel with KHR_animation_pointer target extension.
                QJsonObject pointerExt;
                pointerExt.insert(QStringLiteral("pointer"), pointerPath);

                QJsonObject extensions;
                extensions.insert(QStringLiteral("KHR_animation_pointer"), pointerExt);

                QJsonObject target;
                target.insert(QStringLiteral("path"),       QStringLiteral("pointer"));
                target.insert(QStringLiteral("extensions"), extensions);

                QJsonObject channel;
                channel.insert(QStringLiteral("sampler"), samplerIdx);
                channel.insert(QStringLiteral("target"),  target);
                channels.append(channel);

                log(QString("  [POINTER] Injected channel: %1").arg(pointerPath), logCallback);
                modified = true;
            }

            if (modified)
            {
                animObj[QStringLiteral("samplers")] = samplers;
                animObj[QStringLiteral("channels")] = channels;
                animations[animIdx] = animObj;
            }
        }
    }

    qDebug() << "[INJECT-POINTER] modified=" << modified
             << "newBinary.size()=" << newBinary.size()
             << "glbBinaryChunk=" << (glbBinaryChunk ? "yes" : "null");
    if (!modified || newBinary.isEmpty())
        return modified;

    // Write the new binary data.
    if (glbBinaryChunk)
    {
        // Append to GLB binary chunk and update buffer 0 byteLength.
        // Set byteLength to byteOffsetBase + newBinary.size() (= actual end of all data)
        // rather than oldLen + newBinary.size(), because Assimp embeds image data in the
        // binary chunk beyond the declared byteLength, so the real data end is byteOffsetBase.
        QJsonObject buf0 = buffers[0].toObject();
        buf0[QStringLiteral("byteLength")] = byteOffsetBase + static_cast<int>(newBinary.size());
        buffers[0] = buf0;

        glbBinaryChunk->append(newBinary);
    }
    else
    {
        // Embed as a new buffer with a data URI.
        QJsonObject newBuf;
        newBuf.insert(QStringLiteral("byteLength"), newBinary.size());
        newBuf.insert(QStringLiteral("uri"),
                      QStringLiteral("data:application/octet-stream;base64,") +
                      QString::fromLatin1(newBinary.toBase64()));
        buffers.append(newBuf);
    }

    gltfJson[QStringLiteral("accessors")]   = accessors;
    gltfJson[QStringLiteral("bufferViews")] = bufferViews;
    gltfJson[QStringLiteral("animations")]  = animations;
    gltfJson[QStringLiteral("buffers")]     = buffers;

    // Ensure required extensions are listed.
    auto addExtension = [&](const QString& ext, bool required) {
        const QString key = required ? QStringLiteral("extensionsRequired")
                                     : QStringLiteral("extensionsUsed");
        QJsonArray arr = gltfJson[key].toArray();
        bool found = false;
        for (const QJsonValue& v : std::as_const(arr))
            if (v.toString() == ext) { found = true; break; }
        if (!found)
        {
            arr.append(ext);
            gltfJson[key] = arr;
        }
    };
    addExtension(QStringLiteral("KHR_animation_pointer"), false);
    addExtension(QStringLiteral("KHR_node_visibility"),   false);

    log(QStringLiteral("  [POINTER] Injected pointer animation channels"), logCallback);
    return true;
}

// ---------------------------------------------------------------------------
// injectMorphWeightAnimations
//
// Injects standard glTF2 "weights" animation channels for morph-target blend
// shapes.  Assimp's glTF2 exporter may not write aiMeshMorphAnim channels
// reliably across versions, so we inject them here instead.
//
// Strategy:
//  - For each GltfAnimationClip that has hasMorphAnimations == true, find
//    (or create) the matching animation in gltfJson["animations"] by clip name.
//  - For each Weights channel, write a float accessor for the time values and
//    a flat float accessor for the packed weight values
//    (all morph weights at t0, then all at t1, ...).
//  - Find the target node in the exported nodes array by name.
//  - Skip injection if a "weights" channel targeting the same node already
//    exists (prevents duplication when Assimp also wrote it).
// ---------------------------------------------------------------------------
bool GltfPostProcessor::injectMorphWeightAnimations(
    QJsonObject& gltfJson,
    QByteArray*  glbBinaryChunk,
    std::function<void(const QString&)> logCallback)
{
    // Check whether there are any morph weight channels to inject.
    bool hasAny = false;
    for (const GltfAnimationData& ad : std::as_const(_pointerAnimData))
        for (const GltfAnimationClip& clip : ad.clips)
            if (clip.hasMorphAnimations)
                for (const GltfAnimationChannel& ch : clip.channels)
                    if (ch.targetPath == GltfAnimationTargetPath::Weights && !ch.weightKeys.isEmpty())
                    { hasAny = true; break; }
    if (!hasAny)
        return false;

    QByteArray newBinary;
    QJsonArray accessors   = gltfJson[QStringLiteral("accessors")].toArray();
    QJsonArray bufferViews = gltfJson[QStringLiteral("bufferViews")].toArray();
    QJsonArray animations  = gltfJson[QStringLiteral("animations")].toArray();
    QJsonArray buffers     = gltfJson[QStringLiteral("buffers")].toArray();

    const int bufferIndex = glbBinaryChunk ? 0 : static_cast<int>(buffers.size());

    // For GLB: base offset = actual binary chunk data size (exclude 8-byte header).
    // Same approach as injectPointerAnimationChannels.
    int byteOffsetBase = glbBinaryChunk
                       ? (glbBinaryChunk->size() >= 8 ? glbBinaryChunk->size() - 8 : 0)
                       : 0;

    // Helper: append float array and return accessor index.
    auto appendFloatAccessor = [&](const QVector<float>& data,
                                   const QString& type,
                                   int componentCount) -> int
    {
        if (data.isEmpty())
            return -1;

        while (newBinary.size() % 4 != 0)
            newBinary.append('\0');

        const int byteOffset = byteOffsetBase + newBinary.size();
        const int byteLength = static_cast<int>(data.size()) * 4;
        newBinary.append(reinterpret_cast<const char*>(data.constData()), byteLength);

        QJsonObject bv;
        bv.insert(QStringLiteral("buffer"),     bufferIndex);
        bv.insert(QStringLiteral("byteOffset"), byteOffset);
        bv.insert(QStringLiteral("byteLength"), byteLength);
        const int bvIdx = static_cast<int>(bufferViews.size());
        bufferViews.append(bv);

        QJsonObject acc;
        acc.insert(QStringLiteral("bufferView"),    bvIdx);
        acc.insert(QStringLiteral("byteOffset"),    0);
        acc.insert(QStringLiteral("componentType"), 5126); // FLOAT
        acc.insert(QStringLiteral("count"),         static_cast<int>(data.size()) / componentCount);
        acc.insert(QStringLiteral("type"),          type);
        const int accIdx = static_cast<int>(accessors.size());
        accessors.append(acc);
        return accIdx;
    };

    // Helper: find animation by clip name.
    auto findAnimByName = [&](const QString& name) -> int {
        for (int i = 0; i < animations.size(); ++i)
            if (animations[i].toObject()[QStringLiteral("name")].toString() == name)
                return i;
        return -1;
    };

    // Helper: resolve glTF node index from name.
    auto nodeIndexInJson = [&](const QString& name) -> int {
        const QJsonArray nodes = gltfJson[QStringLiteral("nodes")].toArray();
        for (int i = 0; i < nodes.size(); ++i)
            if (nodes[i].toObject()[QStringLiteral("name")].toString() == name)
                return i;
        return -1;
    };

    // Helper: check if the animation already has a "weights" channel for nodeIdx.
    auto hasWeightsChannel = [&](const QJsonObject& animObj, int nodeIdx) -> bool {
        const QJsonArray channels = animObj[QStringLiteral("channels")].toArray();
        for (const QJsonValue& cv : channels)
        {
            const QJsonObject ch = cv.toObject();
            const QJsonObject tgt = ch[QStringLiteral("target")].toObject();
            if (tgt[QStringLiteral("path")].toString() == QLatin1String("weights") &&
                tgt[QStringLiteral("node")].toInt(-1) == nodeIdx)
                return true;
        }
        return false;
    };

    bool modified = false;

    for (const GltfAnimationData& ad : std::as_const(_pointerAnimData))
    {
        for (const GltfAnimationClip& clip : ad.clips)
        {
            if (!clip.hasMorphAnimations)
                continue;

            int animIdx = findAnimByName(clip.name);
            QJsonObject animObj;
            if (animIdx >= 0)
                animObj = animations[animIdx].toObject();
            else
            {
                // No animation yet for this clip — create one.
                animObj.insert(QStringLiteral("name"), clip.name);
                animObj.insert(QStringLiteral("samplers"), QJsonArray());
                animObj.insert(QStringLiteral("channels"), QJsonArray());
            }

            QJsonArray samplers = animObj[QStringLiteral("samplers")].toArray();
            QJsonArray channels = animObj[QStringLiteral("channels")].toArray();

            for (const GltfAnimationChannel& ch : clip.channels)
            {
                if (ch.targetPath != GltfAnimationTargetPath::Weights)
                    continue;
                if (ch.weightKeys.isEmpty())
                    continue;

                const int nodeIdx = nodeIndexInJson(ch.targetNodeName);
                if (nodeIdx < 0)
                {
                    log(QStringLiteral("  [MORPH] Node '%1' not found in exported JSON — skipping")
                        .arg(ch.targetNodeName), logCallback);
                    continue;
                }

                // Skip if Assimp already wrote a "weights" channel for this node.
                if (hasWeightsChannel(animObj, nodeIdx))
                {
                    log(QStringLiteral("  [MORPH] 'weights' channel for node '%1' already present — skipping injection")
                        .arg(ch.targetNodeName), logCallback);
                    continue;
                }

                // Build time values array.
                QVector<float> times;
                times.reserve(ch.weightKeys.size());
                for (const GltfAnimationWeightsKey& wk : ch.weightKeys)
                    times.append(static_cast<float>(wk.timeSeconds));

                // Build packed weights array: [w0t0, w1t0, ..., w0t1, w1t1, ...].
                const int numTargets = ch.weightKeys.isEmpty() ? 0 : ch.weightKeys[0].values.size();
                QVector<float> weights;
                weights.reserve(ch.weightKeys.size() * numTargets);
                for (const GltfAnimationWeightsKey& wk : ch.weightKeys)
                    for (float w : wk.values)
                        weights.append(w);

                if (times.isEmpty() || weights.isEmpty())
                    continue;

                // Compute min/max for input accessor.
                float tMin = times.front(), tMax = times.back();

                // Input accessor (time).
                while (newBinary.size() % 4 != 0)
                    newBinary.append('\0');
                const int tByteOffset = byteOffsetBase + newBinary.size();
                const int tByteLen    = times.size() * 4;
                newBinary.append(reinterpret_cast<const char*>(times.constData()), tByteLen);

                QJsonObject tBv;
                tBv.insert(QStringLiteral("buffer"),     bufferIndex);
                tBv.insert(QStringLiteral("byteOffset"), tByteOffset);
                tBv.insert(QStringLiteral("byteLength"), tByteLen);
                const int tBvIdx = static_cast<int>(bufferViews.size());
                bufferViews.append(tBv);

                QJsonObject tAcc;
                tAcc.insert(QStringLiteral("bufferView"),    tBvIdx);
                tAcc.insert(QStringLiteral("byteOffset"),    0);
                tAcc.insert(QStringLiteral("componentType"), 5126);
                tAcc.insert(QStringLiteral("count"),         times.size());
                tAcc.insert(QStringLiteral("type"),          QStringLiteral("SCALAR"));
                tAcc.insert(QStringLiteral("min"),           QJsonArray{static_cast<double>(tMin)});
                tAcc.insert(QStringLiteral("max"),           QJsonArray{static_cast<double>(tMax)});
                const int tAccIdx = static_cast<int>(accessors.size());
                accessors.append(tAcc);

                // Output accessor (packed weights).
                const int wAccIdx = appendFloatAccessor(weights, QStringLiteral("SCALAR"), 1);

                // Sampler.
                QJsonObject sampler;
                sampler.insert(QStringLiteral("input"),         tAccIdx);
                sampler.insert(QStringLiteral("output"),        wAccIdx);
                sampler.insert(QStringLiteral("interpolation"), QStringLiteral("LINEAR"));
                const int samplerIdx = static_cast<int>(samplers.size());
                samplers.append(sampler);

                // Channel.
                QJsonObject target;
                target.insert(QStringLiteral("node"), nodeIdx);
                target.insert(QStringLiteral("path"), QStringLiteral("weights"));
                QJsonObject channel;
                channel.insert(QStringLiteral("sampler"), samplerIdx);
                channel.insert(QStringLiteral("target"),  target);
                channels.append(channel);

                modified = true;
                log(QStringLiteral("  [MORPH] Injected 'weights' channel for node '%1' (%2 keys, %3 targets)")
                    .arg(ch.targetNodeName).arg(times.size()).arg(numTargets), logCallback);
            }

            animObj[QStringLiteral("samplers")] = samplers;
            animObj[QStringLiteral("channels")] = channels;

            if (animIdx >= 0)
                animations[animIdx] = animObj;
            else
                animations.append(animObj);
        }
    }

    if (!modified)
        return false;

    if (!modified || newBinary.isEmpty())
    {
        // Even if no new binary was written, still update the JSON arrays
        // so that any animation objects created above are written.
        gltfJson[QStringLiteral("animations")] = animations;
        return modified;
    }

    // Append new binary data to the GLB binary chunk or create a gltF buffer.
    // Same pattern as injectPointerAnimationChannels: simple append, no header rebuild.
    if (glbBinaryChunk)
    {
        // Update buffers[0].byteLength: base + new data size.
        if (!buffers.isEmpty())
        {
            QJsonObject buf0 = buffers[0].toObject();
            buf0[QStringLiteral("byteLength")] = byteOffsetBase + static_cast<int>(newBinary.size());
            buffers[0] = buf0;
        }
        glbBinaryChunk->append(newBinary);
    }
    else
    {
        // gltF: embed as data URI.
        QJsonObject buf;
        buf.insert(QStringLiteral("byteLength"), static_cast<int>(newBinary.size()));
        buf.insert(QStringLiteral("uri"),
                   QStringLiteral("data:application/octet-stream;base64,")
                   + QString::fromLatin1(newBinary.toBase64()));
        buffers.append(buf);
    }

    gltfJson[QStringLiteral("accessors")]   = accessors;
    gltfJson[QStringLiteral("bufferViews")] = bufferViews;
    gltfJson[QStringLiteral("animations")]  = animations;
    gltfJson[QStringLiteral("buffers")]     = buffers;

    log(QStringLiteral("  [MORPH] Injected morph weight animation channels"), logCallback);
    return true;
}
