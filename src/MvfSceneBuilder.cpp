#include "MvfSceneBuilder.h"

#include "SceneMesh.h"
#include "Material.h"
#include "SceneGraph.h"
#include "SceneNode.h"
#include "TextureLocationManager.h"
#include "GltfCameraData.h"
#include "RenderableMesh.h"

#include <QCryptographicHash>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector2D>
#include <QVector3D>

#include <cstring>
#include <limits>

namespace
{
constexpr quint32 ComponentTypeFloat = 5126;
constexpr quint32 ComponentTypeUnsignedInt = 5125;
constexpr quint32 BufferTargetArray = 34962;
constexpr quint32 BufferTargetElementArray = 34963;

QJsonArray vec3ToJson(const QVector3D& v)
{
    return QJsonArray{v.x(), v.y(), v.z()};
}

QJsonArray matrixToJson(const aiMatrix4x4& m)
{
    return QJsonArray{
        m.a1, m.a2, m.a3, m.a4,
        m.b1, m.b2, m.b3, m.b4,
        m.c1, m.c2, m.c3, m.c4,
        m.d1, m.d2, m.d3, m.d4
    };
}

QString shadingModelToString(Material::ShadingModel model)
{
    switch (model)
    {
    case Material::ShadingModel::Unlit: return QStringLiteral("Unlit");
    case Material::ShadingModel::BlinnPhong: return QStringLiteral("BlinnPhong");
    case Material::ShadingModel::PBR: return QStringLiteral("PBR");
    case Material::ShadingModel::Toon: return QStringLiteral("Toon");
    }
    return QStringLiteral("Unknown");
}

QString blendModeToString(Material::BlendMode mode)
{
    switch (mode)
    {
    case Material::BlendMode::Opaque: return QStringLiteral("Opaque");
    case Material::BlendMode::Masked: return QStringLiteral("Masked");
    case Material::BlendMode::Alpha: return QStringLiteral("Alpha");
    case Material::BlendMode::Additive: return QStringLiteral("Additive");
    case Material::BlendMode::Multiply: return QStringLiteral("Multiply");
    }
    return QStringLiteral("Unknown");
}

QString textureTypeKey(Material::TextureType type)
{
    switch (type)
    {
    case Material::TextureType::Albedo: return QStringLiteral("baseColorTexture");
    case Material::TextureType::Normal: return QStringLiteral("normalTexture");
    case Material::TextureType::AmbientOcclusion: return QStringLiteral("occlusionTexture");
    case Material::TextureType::Emissive: return QStringLiteral("emissiveTexture");
    case Material::TextureType::Metallic: return QStringLiteral("metallicTexture");
    case Material::TextureType::Roughness: return QStringLiteral("roughnessTexture");
    case Material::TextureType::Transmission: return QStringLiteral("transmissionTexture");
    case Material::TextureType::IOR: return QStringLiteral("iorTexture");
    case Material::TextureType::SheenColor: return QStringLiteral("sheenColorTexture");
    case Material::TextureType::SheenRoughness: return QStringLiteral("sheenRoughnessTexture");
    case Material::TextureType::ClearcoatColor: return QStringLiteral("clearcoatTexture");
    case Material::TextureType::ClearcoatRoughness: return QStringLiteral("clearcoatRoughnessTexture");
    case Material::TextureType::ClearcoatNormal: return QStringLiteral("clearcoatNormalTexture");
    case Material::TextureType::Iridescence: return QStringLiteral("iridescenceTexture");
    case Material::TextureType::IridescenceThickness: return QStringLiteral("iridescenceThicknessTexture");
    case Material::TextureType::SpecularFactor: return QStringLiteral("specularTexture");
    case Material::TextureType::SpecularColor: return QStringLiteral("specularColorTexture");
    case Material::TextureType::Anisotropy: return QStringLiteral("anisotropyTexture");
    case Material::TextureType::DiffuseTransmission: return QStringLiteral("diffuseTransmissionTexture");
    case Material::TextureType::DiffuseTransmissionColor: return QStringLiteral("diffuseTransmissionColorTexture");
    case Material::TextureType::Thickness: return QStringLiteral("thicknessTexture");
    case Material::TextureType::Diffuse: return QStringLiteral("diffuseTexture");
    case Material::TextureType::SpecularGlossiness: return QStringLiteral("specularGlossinessTexture");
    case Material::TextureType::Opacity: return QStringLiteral("opacityTexture");
    case Material::TextureType::Height: return QStringLiteral("heightTexture");
    case Material::TextureType::Count:
    default: return QString();
    }
}

QString texturePathForType(const Material& material, Material::TextureType type)
{
    switch (type)
    {
    case Material::TextureType::Albedo: return material.albedoMapPath();
    case Material::TextureType::Normal: return material.normalMapPath();
    case Material::TextureType::AmbientOcclusion: return material.aoMapPath();
    case Material::TextureType::Emissive: return material.emissiveMapPath();
    case Material::TextureType::Metallic: return material.metallicMapPath();
    case Material::TextureType::Roughness: return material.roughnessMapPath();
    case Material::TextureType::Transmission: return material.transmissionMapPath();
    case Material::TextureType::IOR: return material.iorMapPath();
    case Material::TextureType::SheenColor: return material.sheenColorMapPath();
    case Material::TextureType::SheenRoughness: return material.sheenRoughnessMapPath();
    case Material::TextureType::ClearcoatColor: return material.clearcoatColorMapPath();
    case Material::TextureType::ClearcoatRoughness: return material.clearcoatRoughnessMapPath();
    case Material::TextureType::ClearcoatNormal: return material.clearcoatNormalMapPath();
    case Material::TextureType::Iridescence: return material.iridescenceMap();
    case Material::TextureType::IridescenceThickness: return material.iridescenceThicknessMap();
    case Material::TextureType::SpecularFactor: return material.specularFactorMap();
    case Material::TextureType::SpecularColor: return material.specularColorMap();
    case Material::TextureType::Anisotropy: return material.anisotropyMap();
    case Material::TextureType::DiffuseTransmission: return material.diffuseTransmissionMap();
    case Material::TextureType::DiffuseTransmissionColor: return material.diffuseTransmissionColorMap();
    case Material::TextureType::Thickness: return material.thicknessMap();
    case Material::TextureType::Diffuse: return material.diffuseMap();
    case Material::TextureType::SpecularGlossiness: return material.specularGlossinessMap();
    case Material::TextureType::Opacity: return material.opacityMapPath();
    case Material::TextureType::Height: return material.heightMapPath();
    case Material::TextureType::Count:
    default: return QString();
    }
}

void populateTextureFallbackMetadata(const Material& material,
                                     Material::TextureType type,
                                     Material::Texture& texture)
{
    auto setTransform = [&](int texCoord, const QVector2D& scale,
                            const QVector2D& offset, float rotation)
    {
        texture.texCoordIndex = texCoord;
        texture.scale = glm::vec2(scale.x(), scale.y());
        texture.offset = glm::vec2(offset.x(), offset.y());
        texture.rotation = rotation;
    };

    switch (type)
    {
    case Material::TextureType::Albedo:
        setTransform(material.albedoTexCoord(), material.albedoTexScale(),
                     material.albedoTexOffset(), material.albedoTexRotation());
        break;
    case Material::TextureType::Normal:
        setTransform(material.normalTexCoord(), material.normalTexScale(),
                     material.normalTexOffset(), material.normalTexRotation());
        break;
    case Material::TextureType::AmbientOcclusion:
        setTransform(material.occlusionTexCoord(), material.occlusionTexScale(),
                     material.occlusionTexOffset(), material.occlusionTexRotation());
        break;
    case Material::TextureType::Emissive:
        setTransform(material.emissiveTexCoord(), material.emissiveTexScale(),
                     material.emissiveTexOffset(), material.emissiveTexRotation());
        break;
    case Material::TextureType::Metallic:
        setTransform(material.metallicTexCoord(), material.metallicTexScale(),
                     material.metallicTexOffset(), material.metallicTexRotation());
        break;
    case Material::TextureType::Roughness:
        setTransform(material.roughnessTexCoord(), material.roughnessTexScale(),
                     material.roughnessTexOffset(), material.roughnessTexRotation());
        break;
    case Material::TextureType::Transmission:
        setTransform(material.transmissionTexCoord(), material.transmissionTexScale(),
                     material.transmissionTexOffset(), material.transmissionTexRotation());
        break;
    case Material::TextureType::IOR:
        setTransform(material.iorTexCoord(), material.iorTexScale(),
                     material.iorTexOffset(), material.iorTexRotation());
        break;
    case Material::TextureType::SheenColor:
        setTransform(material.sheenColorTexCoord(), material.sheenColorTexScale(),
                     material.sheenColorTexOffset(), material.sheenColorTexRotation());
        break;
    case Material::TextureType::SheenRoughness:
        setTransform(material.sheenRoughnessTexCoord(), material.sheenRoughnessTexScale(),
                     material.sheenRoughnessTexOffset(), material.sheenRoughnessTexRotation());
        break;
    case Material::TextureType::ClearcoatColor:
        setTransform(material.clearcoatColorTexCoord(), material.clearcoatColorTexScale(),
                     material.clearcoatColorTexOffset(), material.clearcoatColorTexRotation());
        break;
    case Material::TextureType::ClearcoatRoughness:
        setTransform(material.clearcoatRoughnessTexCoord(), material.clearcoatRoughnessTexScale(),
                     material.clearcoatRoughnessTexOffset(), material.clearcoatRoughnessTexRotation());
        break;
    case Material::TextureType::ClearcoatNormal:
        setTransform(material.clearcoatNormalTexCoord(), material.clearcoatNormalTexScale(),
                     material.clearcoatNormalTexOffset(), material.clearcoatNormalTexRotation());
        break;
    case Material::TextureType::SpecularFactor:
        setTransform(material.specularFactorTexCoord(), material.specularFactorTexScale(),
                     material.specularFactorTexOffset(), material.specularFactorTexRotation());
        break;
    case Material::TextureType::SpecularColor:
        setTransform(material.specularColorTexCoord(), material.specularColorTexScale(),
                     material.specularColorTexOffset(), material.specularColorTexRotation());
        break;
    case Material::TextureType::Anisotropy:
        setTransform(material.anisotropyTexCoord(), material.anisotropyTexScale(),
                     material.anisotropyTexOffset(), material.anisotropyTexRotation());
        break;
    case Material::TextureType::Thickness:
        setTransform(material.thicknessTexCoord(), material.thicknessTexScale(),
                     material.thicknessTexOffset(), material.thicknessTexRotation());
        break;
    case Material::TextureType::DiffuseTransmission:
        setTransform(material.diffuseTransmissionTexCoord(), material.diffuseTransmissionTexScale(),
                     material.diffuseTransmissionTexOffset(), material.diffuseTransmissionTexRotation());
        break;
    case Material::TextureType::DiffuseTransmissionColor:
        setTransform(material.diffuseTransmissionColorTexCoord(), material.diffuseTransmissionColorTexScale(),
                     material.diffuseTransmissionColorTexOffset(), material.diffuseTransmissionColorTexRotation());
        break;
    case Material::TextureType::SpecularGlossiness:
        setTransform(material.specularGlossinessTexCoord(), material.specularGlossinessTexScale(),
                     material.specularGlossinessTexOffset(), material.specularGlossinessTexRotation());
        break;
    case Material::TextureType::Opacity:
        setTransform(material.opacityTexCoord(), material.opacityTexScale(),
                     material.opacityTexOffset(), material.opacityTexRotation());
        break;
    case Material::TextureType::Height:
        setTransform(material.heightTexCoord(), material.heightTexScale(),
                     material.heightTexOffset(), material.heightTexRotation());
        break;
    default:
        break;
    }
}

QJsonObject buildTextureInfoObject(int textureIndex, const Material::Texture& texture)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("index"), textureIndex);
    if (texture.texCoordIndex != 0)
        obj.insert(QStringLiteral("texCoord"), texture.texCoordIndex);

    QJsonObject transform;
    if (texture.scale.x != 1.0f || texture.scale.y != 1.0f)
        transform.insert(QStringLiteral("scale"), QJsonArray{texture.scale.x, texture.scale.y});
    if (texture.offset.x != 0.0f || texture.offset.y != 0.0f)
        transform.insert(QStringLiteral("offset"), QJsonArray{texture.offset.x, texture.offset.y});
    if (texture.rotation != 0.0f)
        transform.insert(QStringLiteral("rotation"), texture.rotation);
    if (!transform.isEmpty())
    {
        QJsonObject extensions;
        extensions.insert(QStringLiteral("KHR_texture_transform"), transform);
        obj.insert(QStringLiteral("extensions"), extensions);
    }
    return obj;
}

void alignBuffer(QByteArray& buffer, int alignment = 4)
{
    while (buffer.size() % alignment != 0)
        buffer.append(char(0));
}

template <typename T>
int appendBinary(QByteArray& buffer, const T* data, int count)
{
    if (count <= 0)
        return buffer.size();

    alignBuffer(buffer);
    const int offset = buffer.size();
    buffer.append(reinterpret_cast<const char*>(data), static_cast<int>(sizeof(T) * count));
    return offset;
}

QJsonObject makeBufferView(int bufferIndex,
                           quint64 byteOffset,
                           quint64 byteLength,
                           quint32 target,
                           const QString& name = {})
{
    QJsonObject obj;
    obj.insert(QStringLiteral("buffer"), bufferIndex);
    obj.insert(QStringLiteral("byteOffset"), static_cast<qint64>(byteOffset));
    obj.insert(QStringLiteral("byteLength"), static_cast<qint64>(byteLength));
    if (target != 0)
        obj.insert(QStringLiteral("target"), static_cast<int>(target));
    if (!name.isEmpty())
        obj.insert(QStringLiteral("name"), name);
    return obj;
}

QJsonObject makeAccessor(int bufferView,
                         quint64 byteOffset,
                         quint32 componentType,
                         quint64 count,
                         const QString& type,
                         const QString& name = {},
                         const QJsonArray& min = {},
                         const QJsonArray& max = {})
{
    QJsonObject obj;
    obj.insert(QStringLiteral("bufferView"), bufferView);
    obj.insert(QStringLiteral("byteOffset"), static_cast<qint64>(byteOffset));
    obj.insert(QStringLiteral("componentType"), static_cast<int>(componentType));
    obj.insert(QStringLiteral("count"), static_cast<qint64>(count));
    obj.insert(QStringLiteral("type"), type);
    if (!name.isEmpty())
        obj.insert(QStringLiteral("name"), name);
    if (!min.isEmpty())
        obj.insert(QStringLiteral("min"), min);
    if (!max.isEmpty())
        obj.insert(QStringLiteral("max"), max);
    return obj;
}

QJsonArray variantMappingsToJson(const QVector<GltfVariantMapping>& mappings)
{
    QJsonArray arr;
    for (const GltfVariantMapping& mapping : mappings)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("materialIndex"), mapping.materialIndex);
        QJsonArray variantIndices;
        for (int variantIndex : mapping.variantIndices)
            variantIndices.append(variantIndex);
        obj.insert(QStringLiteral("variantIndices"), variantIndices);
        arr.append(obj);
    }
    return arr;
}

QJsonObject gpuLightToJson(const GPULight& light)
{
    return QJsonObject{
        {QStringLiteral("type"), light.type},
        {QStringLiteral("range"), light.range},
        {QStringLiteral("intensity"), light.intensity},
        {QStringLiteral("direction"), QJsonArray{light.direction.x, light.direction.y, light.direction.z}},
        {QStringLiteral("color"), QJsonArray{light.color.x, light.color.y, light.color.z}},
        {QStringLiteral("position"), QJsonArray{light.position.x, light.position.y, light.position.z}},
        {QStringLiteral("innerConeCos"), light.innerConeCos},
        {QStringLiteral("outerConeCos"), light.outerConeCos}
    };
}

QJsonArray vec2ToJson(const QVector2D& v)
{
    return QJsonArray{v.x(), v.y()};
}

QJsonArray vec4ToJson(const QVector4D& v)
{
    return QJsonArray{v.x(), v.y(), v.z(), v.w()};
}

QJsonArray quatToJson(const QQuaternion& q)
{
    return QJsonArray{q.scalar(), q.x(), q.y(), q.z()};
}

QJsonObject animationClipToJson(const GltfAnimationClip& clip)
{
    QJsonObject clipObj;
    clipObj.insert(QStringLiteral("name"), clip.name);
    clipObj.insert(QStringLiteral("durationSeconds"), clip.durationSeconds);
    clipObj.insert(QStringLiteral("hasNodeTransforms"), clip.hasNodeTransforms);
    clipObj.insert(QStringLiteral("hasSkinning"), clip.hasSkinning);
    clipObj.insert(QStringLiteral("hasMorphAnimations"), clip.hasMorphAnimations);
    clipObj.insert(QStringLiteral("hasPointerAnimations"), clip.hasPointerAnimations);

    QJsonArray channelsArray;
    for (const GltfAnimationChannel& channel : clip.channels)
    {
        QJsonObject channelObj;
        channelObj.insert(QStringLiteral("targetKind"), static_cast<int>(channel.targetKind));
        channelObj.insert(QStringLiteral("targetNodeName"), channel.targetNodeName);
        channelObj.insert(QStringLiteral("targetNodeIndex"), channel.targetNodeIndex);
        channelObj.insert(QStringLiteral("targetMeshUuid"), channel.targetMeshUuid.toString(QUuid::WithoutBraces));
        channelObj.insert(QStringLiteral("targetPath"), static_cast<int>(channel.targetPath));
        channelObj.insert(QStringLiteral("targetPointer"), channel.targetPointer);
        channelObj.insert(QStringLiteral("pointerTargetKind"), static_cast<int>(channel.pointerTargetKind));
        channelObj.insert(QStringLiteral("targetMaterialIndex"), channel.targetMaterialIndex);
        channelObj.insert(QStringLiteral("textureTarget"), static_cast<int>(channel.textureTarget));
        channelObj.insert(QStringLiteral("pointerProperty"), static_cast<int>(channel.pointerProperty));

        QJsonArray vec3Keys;
        for (const GltfAnimationVec3Key& key : channel.vec3Keys)
        {
            vec3Keys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("value"), vec3ToJson(key.value)}
            });
        }
        channelObj.insert(QStringLiteral("vec3Keys"), vec3Keys);

        QJsonArray vec4Keys;
        for (const GltfAnimationVec4Key& key : channel.vec4Keys)
        {
            vec4Keys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("value"), vec4ToJson(key.value)}
            });
        }
        channelObj.insert(QStringLiteral("vec4Keys"), vec4Keys);

        QJsonArray quatKeys;
        for (const GltfAnimationQuatKey& key : channel.quatKeys)
        {
            quatKeys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("value"), quatToJson(key.value)}
            });
        }
        channelObj.insert(QStringLiteral("quatKeys"), quatKeys);

        QJsonArray vec2Keys;
        for (const GltfAnimationVec2Key& key : channel.vec2Keys)
        {
            vec2Keys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("value"), vec2ToJson(key.value)}
            });
        }
        channelObj.insert(QStringLiteral("vec2Keys"), vec2Keys);

        QJsonArray floatKeys;
        for (const GltfAnimationFloatKey& key : channel.floatKeys)
        {
            floatKeys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("value"), key.value}
            });
        }
        channelObj.insert(QStringLiteral("floatKeys"), floatKeys);

        QJsonArray boolKeys;
        for (const GltfAnimationBoolKey& key : channel.boolKeys)
        {
            boolKeys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("value"), key.value}
            });
        }
        channelObj.insert(QStringLiteral("boolKeys"), boolKeys);

        QJsonArray weightKeys;
        for (const GltfAnimationWeightsKey& key : channel.weightKeys)
        {
            QJsonArray values;
            for (float weight : key.values)
                values.append(weight);
            weightKeys.append(QJsonObject{
                {QStringLiteral("timeSeconds"), key.timeSeconds},
                {QStringLiteral("values"), values}
            });
        }
        channelObj.insert(QStringLiteral("weightKeys"), weightKeys);

        channelsArray.append(channelObj);
    }

    clipObj.insert(QStringLiteral("channels"), channelsArray);
    return clipObj;
}
}

namespace Mvf
{
MVFPackage buildMVFPackage(const SceneGraph& sceneGraph,
                           const std::vector<SceneMesh*>& meshStore,
                           const QSet<QUuid>& visibleMeshUuids,
                           const QSet<QUuid>& selectedMeshUuids,
                           const QVector<GltfCameraData>& cameraDataByFile)
{
    MVFPackage package;
    Document& document = package.document;

    QJsonObject geomBuffer;
    geomBuffer.insert(QStringLiteral("name"), QStringLiteral("geometry"));
    geomBuffer.insert(QStringLiteral("chunk"), QStringLiteral("GEOM"));
    geomBuffer.insert(QStringLiteral("byteLength"), 0);
    document.buffers.append(geomBuffer);

    QJsonObject imageBuffer;
    imageBuffer.insert(QStringLiteral("name"), QStringLiteral("images"));
    imageBuffer.insert(QStringLiteral("chunk"), QStringLiteral("IMGS"));
    imageBuffer.insert(QStringLiteral("byteLength"), 0);
    document.buffers.append(imageBuffer);

    QHash<QUuid, int> meshIndexByUuid;
    QHash<QUuid, int> materialIndexByUuid;
    QHash<QString, int> imageIndexByPath;
    QHash<QString, int> samplerIndexBySignature;
    QHash<QString, int> textureIndexBySignature;

    auto buildMaterialJsonObject =
        [&](const ::Material& material,
            const QString& id,
            const QString& nameForFallback) -> QJsonObject
    {
        QJsonObject materialObj;
        materialObj.insert(QStringLiteral("id"), id);
        materialObj.insert(QStringLiteral("name"), material.name().isEmpty() ? nameForFallback : material.name());
        materialObj.insert(QStringLiteral("shadingModel"), shadingModelToString(material.shadingModel()));
        materialObj.insert(QStringLiteral("blendMode"), blendModeToString(material.blendMode()));
        materialObj.insert(QStringLiteral("doubleSided"), material.twoSided());
        materialObj.insert(QStringLiteral("alphaCutoff"), material.alphaThreshold());
        materialObj.insert(QStringLiteral("opacity"), material.opacity());

        QJsonObject pbr;
        pbr.insert(QStringLiteral("baseColorFactor"),
                   QJsonArray{material.albedoColor().x(), material.albedoColor().y(),
                              material.albedoColor().z(), material.opacity()});
        pbr.insert(QStringLiteral("metallicFactor"), material.metalness());
        pbr.insert(QStringLiteral("roughnessFactor"), material.roughness());
        materialObj.insert(QStringLiteral("pbr"), pbr);

        QJsonObject extensions;
        extensions.insert(QStringLiteral("MVF_material_runtime"),
                          QJsonObject::fromVariantMap(material.toVariantMap()));

        QJsonObject ads;
        ads.insert(QStringLiteral("ambient"), vec3ToJson(material.ambient()));
        ads.insert(QStringLiteral("diffuse"), vec3ToJson(material.diffuse()));
        ads.insert(QStringLiteral("specular"), vec3ToJson(material.specular()));
        ads.insert(QStringLiteral("emissive"), vec3ToJson(material.emissive()));
        ads.insert(QStringLiteral("shininess"), material.shininess());
        extensions.insert(QStringLiteral("MVF_material_ads"), ads);

        QJsonObject mvfPbr;
        mvfPbr.insert(QStringLiteral("ior"), material.ior());
        mvfPbr.insert(QStringLiteral("transmission"), material.transmission());
        mvfPbr.insert(QStringLiteral("clearcoat"), material.clearcoat());
        mvfPbr.insert(QStringLiteral("clearcoatRoughness"), material.clearcoatRoughness());
        mvfPbr.insert(QStringLiteral("sheenColor"), vec3ToJson(material.sheenColor()));
        mvfPbr.insert(QStringLiteral("sheenRoughness"), material.sheenRoughness());

        for (int textureTypeIndex = 0;
             textureTypeIndex < static_cast<int>(::Material::TextureType::Count);
             ++textureTypeIndex)
        {
            const auto type = static_cast<::Material::TextureType>(textureTypeIndex);
            ::Material::Texture texture = material.texture(type);
            if (texture.path.empty())
            {
                const QString canonicalPath = texturePathForType(material, type);
                if (!canonicalPath.isEmpty())
                {
                    texture.path = canonicalPath.toStdString();
                    populateTextureFallbackMetadata(material, type, texture);
                }
            }

            if (texture.path.empty())
                continue;

            const QString imagePath = QString::fromStdString(texture.path);
            int imageIndex = imageIndexByPath.value(imagePath, -1);
            if (imageIndex < 0)
            {
                imageIndex = document.images.size();
                imageIndexByPath.insert(imagePath, imageIndex);

                QJsonObject imageObj;
                imageObj.insert(QStringLiteral("name"), QFileInfo(imagePath).fileName());
                imageObj.insert(QStringLiteral("originalUri"), imagePath);
                imageObj.insert(QStringLiteral("bufferView"), -1);
                imageObj.insert(QStringLiteral("mimeType"), QString());
                imageObj.insert(QStringLiteral("byteLength"), 0);
                document.images.append(imageObj);
            }

            const QString samplerSignature = QStringLiteral("%1|%2|%3|%4")
                .arg(static_cast<int>(texture.magFilter))
                .arg(static_cast<int>(texture.minFilter))
                .arg(static_cast<int>(texture.wrapS))
                .arg(static_cast<int>(texture.wrapT));

            int samplerIndex = samplerIndexBySignature.value(samplerSignature, -1);
            if (samplerIndex < 0)
            {
                samplerIndex = document.samplers.size();
                samplerIndexBySignature.insert(samplerSignature, samplerIndex);

                QJsonObject samplerObj;
                samplerObj.insert(QStringLiteral("magFilter"), static_cast<int>(texture.magFilter));
                samplerObj.insert(QStringLiteral("minFilter"), static_cast<int>(texture.minFilter));
                samplerObj.insert(QStringLiteral("wrapS"), static_cast<int>(texture.wrapS));
                samplerObj.insert(QStringLiteral("wrapT"), static_cast<int>(texture.wrapT));
                document.samplers.append(samplerObj);
            }

            const QString textureSignature = QStringLiteral("%1|%2")
                .arg(imageIndex)
                .arg(samplerIndex);

            int textureIndex = textureIndexBySignature.value(textureSignature, -1);
            if (textureIndex < 0)
            {
                textureIndex = document.textures.size();
                textureIndexBySignature.insert(textureSignature, textureIndex);

                QJsonObject textureObj;
                textureObj.insert(QStringLiteral("image"), imageIndex);
                textureObj.insert(QStringLiteral("sampler"), samplerIndex);
                document.textures.append(textureObj);
            }

            const QString key = textureTypeKey(type);
            if (!key.isEmpty())
            {
                mvfPbr.insert(key, buildTextureInfoObject(textureIndex, texture));
            }
        }

        extensions.insert(QStringLiteral("MVF_material_pbr"), mvfPbr);
        materialObj.insert(QStringLiteral("extensions"), extensions);
        return materialObj;
    };

    for (SceneMesh* mesh : meshStore)
    {
        if (!mesh)
            continue;

        const QUuid meshUuid = mesh->uuid();
        meshIndexByUuid.insert(meshUuid, document.meshes.size());

        QJsonObject meshObj;
        meshObj.insert(QStringLiteral("id"), meshUuid.toString(QUuid::WithoutBraces));
        meshObj.insert(QStringLiteral("name"), mesh->getName());

        QJsonObject primitive;
        primitive.insert(QStringLiteral("mode"), static_cast<int>(mesh->getPrimitiveMode()));

        QJsonObject primitiveExtras;
        primitiveExtras.insert(QStringLiteral("sceneIndex"), mesh->getSceneIndex());
        primitiveExtras.insert(QStringLiteral("meshUuid"), meshUuid.toString(QUuid::WithoutBraces));
        primitiveExtras.insert(QStringLiteral("hasGeometryChunk"), true);
        primitiveExtras.insert(QStringLiteral("hasNegativeScale"), mesh->hasNegativeScale());
        primitiveExtras.insert(QStringLiteral("originalMaterialIndex"), mesh->getOriginalMaterialIndex());
        if (!mesh->getSourceFile().isEmpty())
            primitiveExtras.insert(QStringLiteral("sourceFile"), mesh->getSourceFile());
        if (!mesh->getSourceNodeName().isEmpty())
            primitiveExtras.insert(QStringLiteral("sourceNodeName"), mesh->getSourceNodeName());
        if (mesh->hasVariants())
            primitiveExtras.insert(QStringLiteral("variantMappings"), variantMappingsToJson(mesh->variantMappings()));

        // Per-mesh user transform (gizmo TRS applied on top of scene hierarchy).
        // Stored as separate translation/rotation-quaternion/scale so that the load path
        // can call setTranslation / setRotationQuaternion / setScaling directly.
        {
            const QVector3D    t  = mesh->getTranslation();
            const QQuaternion  q  = mesh->getRotationQuaternion();
            const QVector3D    r  = mesh->getRotation();   // Euler display values
            const QVector3D    s  = mesh->getScaling();

            QJsonObject meshTrs;
            meshTrs.insert(QStringLiteral("tx"), t.x());
            meshTrs.insert(QStringLiteral("ty"), t.y());
            meshTrs.insert(QStringLiteral("tz"), t.z());
            meshTrs.insert(QStringLiteral("qx"), q.x());
            meshTrs.insert(QStringLiteral("qy"), q.y());
            meshTrs.insert(QStringLiteral("qz"), q.z());
            meshTrs.insert(QStringLiteral("qw"), q.scalar());
            meshTrs.insert(QStringLiteral("rx"), r.x());
            meshTrs.insert(QStringLiteral("ry"), r.y());
            meshTrs.insert(QStringLiteral("rz"), r.z());
            meshTrs.insert(QStringLiteral("sx"), s.x());
            meshTrs.insert(QStringLiteral("sy"), s.y());
            meshTrs.insert(QStringLiteral("sz"), s.z());
            primitiveExtras.insert(QStringLiteral("meshTrs"), meshTrs);
        }

        if (!mesh->allVariantMaterials().isEmpty())
        {
            QJsonArray variantMaterialsArray;
            for (auto it = mesh->allVariantMaterials().cbegin(); it != mesh->allVariantMaterials().cend(); ++it)
            {
                QJsonObject variantMaterialObj;
                variantMaterialObj.insert(QStringLiteral("key"), it.key());
                variantMaterialObj.insert(
                    QStringLiteral("material"),
                    buildMaterialJsonObject(
                        it.value(),
                        QStringLiteral("%1.variant.%2").arg(meshUuid.toString(QUuid::WithoutBraces)).arg(it.key()),
                        mesh->getName()));
                variantMaterialsArray.append(variantMaterialObj);
            }
            primitiveExtras.insert(QStringLiteral("variantMaterials"), variantMaterialsArray);
        }

        if (const auto* assImpMesh = dynamic_cast<const SceneMesh*>(mesh))
        {
            const std::vector<Vertex> vertices = assImpMesh->vertices();
            const std::vector<unsigned int> indices = assImpMesh->indices();

            primitiveExtras.insert(QStringLiteral("vertexCount"), static_cast<int>(vertices.size()));
            primitiveExtras.insert(QStringLiteral("indexCount"), static_cast<int>(indices.size()));

            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> tangents;
            std::vector<float> colors;
            std::vector<float> uv0;
            std::vector<float> uv1;
            std::vector<float> uv2;
            std::vector<float> uv3;
            positions.reserve(vertices.size() * 3);
            normals.reserve(vertices.size() * 3);
            tangents.reserve(vertices.size() * 3);
            colors.reserve(vertices.size() * 4);
            uv0.reserve(vertices.size() * 2);
            uv1.reserve(vertices.size() * 2);
            uv2.reserve(vertices.size() * 2);
            uv3.reserve(vertices.size() * 2);

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float minZ = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            float maxZ = std::numeric_limits<float>::lowest();

            bool hasColor = false;
            bool hasUv1 = false;
            bool hasUv2 = false;
            bool hasUv3 = false;
            bool hasSkinData = false;

            std::vector<float> jointIndices;
            std::vector<float> jointWeights;
            jointIndices.reserve(vertices.size() * 4);
            jointWeights.reserve(vertices.size() * 4);

            for (const Vertex& v : vertices)
            {
                positions.push_back(v.Position.x);
                positions.push_back(v.Position.y);
                positions.push_back(v.Position.z);

                normals.push_back(v.Normal.x);
                normals.push_back(v.Normal.y);
                normals.push_back(v.Normal.z);

                tangents.push_back(v.Tangent.x);
                tangents.push_back(v.Tangent.y);
                tangents.push_back(v.Tangent.z);

                colors.push_back(v.Color.r);
                colors.push_back(v.Color.g);
                colors.push_back(v.Color.b);
                colors.push_back(v.Color.a);

                uv0.push_back(v.TexCoords[0].x);
                uv0.push_back(v.TexCoords[0].y);
                uv1.push_back(v.TexCoords[1].x);
                uv1.push_back(v.TexCoords[1].y);
                uv2.push_back(v.TexCoords[2].x);
                uv2.push_back(v.TexCoords[2].y);
                uv3.push_back(v.TexCoords[3].x);
                uv3.push_back(v.TexCoords[3].y);

                jointIndices.push_back(v.JointIndices.x);
                jointIndices.push_back(v.JointIndices.y);
                jointIndices.push_back(v.JointIndices.z);
                jointIndices.push_back(v.JointIndices.w);
                jointWeights.push_back(v.JointWeights.x);
                jointWeights.push_back(v.JointWeights.y);
                jointWeights.push_back(v.JointWeights.z);
                jointWeights.push_back(v.JointWeights.w);

                if (!hasSkinData &&
                    (v.JointWeights.x > 0.0f || v.JointWeights.y > 0.0f ||
                     v.JointWeights.z > 0.0f || v.JointWeights.w > 0.0f))
                    hasSkinData = true;

                minX = std::min(minX, v.Position.x);
                minY = std::min(minY, v.Position.y);
                minZ = std::min(minZ, v.Position.z);
                maxX = std::max(maxX, v.Position.x);
                maxY = std::max(maxY, v.Position.y);
                maxZ = std::max(maxZ, v.Position.z);

                hasColor = hasColor || v.Color != glm::vec4(1.0f);
                hasUv1 = hasUv1 || v.TexCoords[1] != glm::vec2(0.0f);
                hasUv2 = hasUv2 || v.TexCoords[2] != glm::vec2(0.0f);
                hasUv3 = hasUv3 || v.TexCoords[3] != glm::vec2(0.0f);
            }
            // Also flag as skin data if the mesh has skin joints defined
            if (!hasSkinData && mesh->hasSkinning())
                hasSkinData = true;

            QJsonObject attributes;

            const int posOffset = appendBinary(package.geometryChunk, positions.data(), static_cast<int>(positions.size()));
            const int posView = document.bufferViews.size();
            document.bufferViews.append(makeBufferView(0, posOffset, positions.size() * sizeof(float), BufferTargetArray,
                                                       QStringLiteral("%1_POSITION").arg(mesh->getName())));
            const int posAccessor = document.accessors.size();
            document.accessors.append(makeAccessor(posView, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC3"),
                                                   QStringLiteral("%1_POSITION").arg(mesh->getName()),
                                                   QJsonArray{minX, minY, minZ},
                                                   QJsonArray{maxX, maxY, maxZ}));
            attributes.insert(QStringLiteral("POSITION"), posAccessor);

            const int normalOffset = appendBinary(package.geometryChunk, normals.data(), static_cast<int>(normals.size()));
            const int normalView = document.bufferViews.size();
            document.bufferViews.append(makeBufferView(0, normalOffset, normals.size() * sizeof(float), BufferTargetArray,
                                                       QStringLiteral("%1_NORMAL").arg(mesh->getName())));
            const int normalAccessor = document.accessors.size();
            document.accessors.append(makeAccessor(normalView, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC3"),
                                                   QStringLiteral("%1_NORMAL").arg(mesh->getName())));
            attributes.insert(QStringLiteral("NORMAL"), normalAccessor);

            const int tangentOffset = appendBinary(package.geometryChunk, tangents.data(), static_cast<int>(tangents.size()));
            const int tangentView = document.bufferViews.size();
            document.bufferViews.append(makeBufferView(0, tangentOffset, tangents.size() * sizeof(float), BufferTargetArray,
                                                       QStringLiteral("%1_TANGENT").arg(mesh->getName())));
            const int tangentAccessor = document.accessors.size();
            document.accessors.append(makeAccessor(tangentView, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC3"),
                                                   QStringLiteral("%1_TANGENT").arg(mesh->getName())));
            attributes.insert(QStringLiteral("TANGENT"), tangentAccessor);

            const int uv0Offset = appendBinary(package.geometryChunk, uv0.data(), static_cast<int>(uv0.size()));
            const int uv0View = document.bufferViews.size();
            document.bufferViews.append(makeBufferView(0, uv0Offset, uv0.size() * sizeof(float), BufferTargetArray,
                                                       QStringLiteral("%1_TEXCOORD_0").arg(mesh->getName())));
            const int uv0Accessor = document.accessors.size();
            document.accessors.append(makeAccessor(uv0View, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC2"),
                                                   QStringLiteral("%1_TEXCOORD_0").arg(mesh->getName())));
            attributes.insert(QStringLiteral("TEXCOORD_0"), uv0Accessor);

            if (hasUv1)
            {
                const int uv1Offset = appendBinary(package.geometryChunk, uv1.data(), static_cast<int>(uv1.size()));
                const int uv1View = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, uv1Offset, uv1.size() * sizeof(float), BufferTargetArray,
                                                           QStringLiteral("%1_TEXCOORD_1").arg(mesh->getName())));
                const int uv1Accessor = document.accessors.size();
                document.accessors.append(makeAccessor(uv1View, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC2"),
                                                       QStringLiteral("%1_TEXCOORD_1").arg(mesh->getName())));
                attributes.insert(QStringLiteral("TEXCOORD_1"), uv1Accessor);
            }

            if (hasUv2)
            {
                const int uv2Offset = appendBinary(package.geometryChunk, uv2.data(), static_cast<int>(uv2.size()));
                const int uv2View = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, uv2Offset, uv2.size() * sizeof(float), BufferTargetArray,
                                                           QStringLiteral("%1_TEXCOORD_2").arg(mesh->getName())));
                const int uv2Accessor = document.accessors.size();
                document.accessors.append(makeAccessor(uv2View, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC2"),
                                                       QStringLiteral("%1_TEXCOORD_2").arg(mesh->getName())));
                attributes.insert(QStringLiteral("TEXCOORD_2"), uv2Accessor);
            }

            if (hasUv3)
            {
                const int uv3Offset = appendBinary(package.geometryChunk, uv3.data(), static_cast<int>(uv3.size()));
                const int uv3View = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, uv3Offset, uv3.size() * sizeof(float), BufferTargetArray,
                                                           QStringLiteral("%1_TEXCOORD_3").arg(mesh->getName())));
                const int uv3Accessor = document.accessors.size();
                document.accessors.append(makeAccessor(uv3View, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC2"),
                                                       QStringLiteral("%1_TEXCOORD_3").arg(mesh->getName())));
                attributes.insert(QStringLiteral("TEXCOORD_3"), uv3Accessor);
            }

            if (hasColor)
            {
                const int colorOffset = appendBinary(package.geometryChunk, colors.data(), static_cast<int>(colors.size()));
                const int colorView = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, colorOffset, colors.size() * sizeof(float), BufferTargetArray,
                                                           QStringLiteral("%1_COLOR_0").arg(mesh->getName())));
                const int colorAccessor = document.accessors.size();
                document.accessors.append(makeAccessor(colorView, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC4"),
                                                       QStringLiteral("%1_COLOR_0").arg(mesh->getName())));
                attributes.insert(QStringLiteral("COLOR_0"), colorAccessor);
            }

            // --- Skinning: JOINTS_0 and WEIGHTS_0 ---
            if (hasSkinData)
            {
                const int jointsOffset = appendBinary(package.geometryChunk, jointIndices.data(), static_cast<int>(jointIndices.size()));
                const int jointsView = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, jointsOffset, jointIndices.size() * sizeof(float), BufferTargetArray,
                                                           QStringLiteral("%1_JOINTS_0").arg(mesh->getName())));
                const int jointsAccessor = document.accessors.size();
                document.accessors.append(makeAccessor(jointsView, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC4"),
                                                       QStringLiteral("%1_JOINTS_0").arg(mesh->getName())));
                attributes.insert(QStringLiteral("JOINTS_0"), jointsAccessor);

                const int weightsOffset = appendBinary(package.geometryChunk, jointWeights.data(), static_cast<int>(jointWeights.size()));
                const int weightsView = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, weightsOffset, jointWeights.size() * sizeof(float), BufferTargetArray,
                                                           QStringLiteral("%1_WEIGHTS_0").arg(mesh->getName())));
                const int weightsAccessor = document.accessors.size();
                document.accessors.append(makeAccessor(weightsView, 0, ComponentTypeFloat, vertices.size(), QStringLiteral("VEC4"),
                                                       QStringLiteral("%1_WEIGHTS_0").arg(mesh->getName())));
                attributes.insert(QStringLiteral("WEIGHTS_0"), weightsAccessor);
            }

            primitive.insert(QStringLiteral("attributes"), attributes);

            const int indexOffset = appendBinary(package.geometryChunk, indices.data(), static_cast<int>(indices.size()));
            const int indexView = document.bufferViews.size();
            document.bufferViews.append(makeBufferView(0, indexOffset, indices.size() * sizeof(unsigned int), BufferTargetElementArray,
                                                       QStringLiteral("%1_INDICES").arg(mesh->getName())));
            const int indexAccessor = document.accessors.size();
            document.accessors.append(makeAccessor(indexView, 0, ComponentTypeUnsignedInt, indices.size(), QStringLiteral("SCALAR"),
                                                   QStringLiteral("%1_INDICES").arg(mesh->getName())));
            primitive.insert(QStringLiteral("indices"), indexAccessor);

            // --- Morph targets (blend shapes) ---
            // Write each morph target's position/normal/tangent delta arrays as
            // VEC3 float accessors into the geometryChunk, then record them in
            // the primitive "targets" array (glTF-compatible layout).  The default
            // weights are stored in primitiveExtras so the read path can restore
            // the mesh's initial weight state.
            const QVector<MorphTargetData>& morphTargets = assImpMesh->getMorphTargets();
            if (!morphTargets.isEmpty())
            {
                QJsonArray targetsArray;
                const QString meshName = mesh->getName();
                for (int ti = 0; ti < morphTargets.size(); ++ti)
                {
                    const MorphTargetData& mt = morphTargets[ti];
                    QJsonObject targetAttribs;
                    const QString tiStr = QString::number(ti);

                    // POSITION deltas
                    if (!mt.positionDeltas.empty())
                    {
                        std::vector<float> posD;
                        posD.reserve(mt.positionDeltas.size() * 3);
                        for (const auto& p : mt.positionDeltas) { posD.push_back(p.x); posD.push_back(p.y); posD.push_back(p.z); }
                        const int offset = appendBinary(package.geometryChunk, posD.data(), static_cast<int>(posD.size()));
                        const int bv = document.bufferViews.size();
                        document.bufferViews.append(makeBufferView(0, offset, posD.size() * sizeof(float), BufferTargetArray,
                                                                   QStringLiteral("%1_MORPH%2_POSITION").arg(meshName, tiStr)));
                        const int acc = document.accessors.size();
                        document.accessors.append(makeAccessor(bv, 0, ComponentTypeFloat,
                                                               static_cast<int>(mt.positionDeltas.size()),
                                                               QStringLiteral("VEC3"),
                                                               QStringLiteral("%1_MORPH%2_POSITION").arg(meshName, tiStr)));
                        targetAttribs.insert(QStringLiteral("POSITION"), acc);
                    }

                    // NORMAL deltas
                    if (!mt.normalDeltas.empty())
                    {
                        std::vector<float> norD;
                        norD.reserve(mt.normalDeltas.size() * 3);
                        for (const auto& n : mt.normalDeltas) { norD.push_back(n.x); norD.push_back(n.y); norD.push_back(n.z); }
                        const int offset = appendBinary(package.geometryChunk, norD.data(), static_cast<int>(norD.size()));
                        const int bv = document.bufferViews.size();
                        document.bufferViews.append(makeBufferView(0, offset, norD.size() * sizeof(float), BufferTargetArray,
                                                                   QStringLiteral("%1_MORPH%2_NORMAL").arg(meshName, tiStr)));
                        const int acc = document.accessors.size();
                        document.accessors.append(makeAccessor(bv, 0, ComponentTypeFloat,
                                                               static_cast<int>(mt.normalDeltas.size()),
                                                               QStringLiteral("VEC3"),
                                                               QStringLiteral("%1_MORPH%2_NORMAL").arg(meshName, tiStr)));
                        targetAttribs.insert(QStringLiteral("NORMAL"), acc);
                    }

                    // TANGENT deltas
                    if (!mt.tangentDeltas.empty())
                    {
                        std::vector<float> tanD;
                        tanD.reserve(mt.tangentDeltas.size() * 3);
                        for (const auto& t : mt.tangentDeltas) { tanD.push_back(t.x); tanD.push_back(t.y); tanD.push_back(t.z); }
                        const int offset = appendBinary(package.geometryChunk, tanD.data(), static_cast<int>(tanD.size()));
                        const int bv = document.bufferViews.size();
                        document.bufferViews.append(makeBufferView(0, offset, tanD.size() * sizeof(float), BufferTargetArray,
                                                                   QStringLiteral("%1_MORPH%2_TANGENT").arg(meshName, tiStr)));
                        const int acc = document.accessors.size();
                        document.accessors.append(makeAccessor(bv, 0, ComponentTypeFloat,
                                                               static_cast<int>(mt.tangentDeltas.size()),
                                                               QStringLiteral("VEC3"),
                                                               QStringLiteral("%1_MORPH%2_TANGENT").arg(meshName, tiStr)));
                        targetAttribs.insert(QStringLiteral("TANGENT"), acc);
                    }

                    targetsArray.append(targetAttribs);
                }
                primitive.insert(QStringLiteral("targets"), targetsArray);

                // Save default weights in extras for read path restoration.
                const QVector<float> defWeights = assImpMesh->defaultMorphWeights();
                QJsonArray defWeightsJson;
                for (float w : defWeights)
                    defWeightsJson.append(static_cast<double>(w));
                primitiveExtras.insert(QStringLiteral("defaultMorphWeights"), defWeightsJson);
            }
        }

        const ::Material material = mesh->getMaterial();
        QJsonObject materialObj = buildMaterialJsonObject(
            material,
            meshUuid.toString(QUuid::WithoutBraces),
            mesh->getName());
        materialObj.insert(QStringLiteral("extras"), QJsonObject{
            {QStringLiteral("meshUuid"), meshUuid.toString(QUuid::WithoutBraces)}
        });
        document.materials.append(materialObj);
        const int materialIndex = document.materials.size() - 1;
        materialIndexByUuid.insert(meshUuid, materialIndex);
        primitive.insert(QStringLiteral("material"), materialIndex);

        // Save skin joints (inverse bind matrices + node names) so MVF reload can
        // restore per-vertex skinning and drive skeletal animation.
        {
            const QVector<GltfSkinJoint>& skinJoints = mesh->skinJoints();
            if (!skinJoints.isEmpty())
            {
                QJsonArray skinJointsArray;
                for (const GltfSkinJoint& joint : skinJoints)
                {
                    skinJointsArray.append(QJsonObject{
                        {QStringLiteral("nodeName"), joint.nodeName},
                        {QStringLiteral("inverseBindMatrix"), matrixToJson(joint.inverseBindMatrix)}
                    });
                }
                primitiveExtras.insert(QStringLiteral("skinJoints"), skinJointsArray);
            }
        }

        // Serialize OCC B-Rep edge segments into the binary chunk so true analytical
        // wireframe edges are preserved across MVF save/load for STEP/IGES/BREP meshes.
        // The per-topological-edge boundary table is written as a compact JSON int array
        // in extras (it's small: one int per topological edge).
        if (const auto* assImpMeshForEdges = dynamic_cast<const SceneMesh*>(mesh))
        {
            const std::vector<float>& occEdges  = assImpMeshForEdges->getOccEdgeSegments();
            const std::vector<int>&   occBounds = assImpMeshForEdges->getOccEdgeBoundaries();
            if (!occEdges.empty())
            {
                const int edgeOffset = appendBinary(package.geometryChunk, occEdges.data(),
                                                    static_cast<int>(occEdges.size()));
                const int edgeView = document.bufferViews.size();
                document.bufferViews.append(makeBufferView(0, edgeOffset,
                                                           occEdges.size() * sizeof(float), 0,
                                                           QStringLiteral("%1_OCC_EDGES").arg(mesh->getName())));
                const int edgeAccessor = document.accessors.size();
                document.accessors.append(makeAccessor(edgeView, 0, ComponentTypeFloat,
                                                       static_cast<int>(occEdges.size() / 3),
                                                       QStringLiteral("VEC3"),
                                                       QStringLiteral("%1_OCC_EDGES").arg(mesh->getName())));
                primitiveExtras.insert(QStringLiteral("occEdgeAccessor"), edgeAccessor);

                if (!occBounds.empty())
                {
                    QJsonArray boundsJson;
                    for (int b : occBounds)
                        boundsJson.append(b);
                    primitiveExtras.insert(QStringLiteral("occEdgeBounds"), boundsJson);
                }

                // Analytic circle data (Edge Radius measurement tool) - one
                // entry per topological edge, positionally 1:1 with
                // occEdgeBounds; null for any edge that isn't a circle
                // (line, spline, ...) so position stays meaningful.
                const std::vector<OccEdgeCircleInfo>& occCircles = assImpMeshForEdges->getOccEdgeCircles();
                if (!occCircles.empty())
                {
                    QJsonArray circlesJson;
                    for (const OccEdgeCircleInfo& c : occCircles)
                    {
                        if (!c.isCircle)
                        {
                            circlesJson.append(QJsonValue());
                            continue;
                        }
                        QJsonObject co;
                        co.insert(QStringLiteral("cx"), c.centerX);
                        co.insert(QStringLiteral("cy"), c.centerY);
                        co.insert(QStringLiteral("cz"), c.centerZ);
                        co.insert(QStringLiteral("ax"), c.axisX);
                        co.insert(QStringLiteral("ay"), c.axisY);
                        co.insert(QStringLiteral("az"), c.axisZ);
                        co.insert(QStringLiteral("r"), c.radius);
                        circlesJson.append(co);
                    }
                    primitiveExtras.insert(QStringLiteral("occEdgeCircles"), circlesJson);
                }

                // Mesh-wide max BRep vertex tolerance (Chain Length's
                // connectivity check) - see OccEdgeData::vertexTolerance's
                // doc comment (BRepToAssimpConverter.h).
                const double vertexTolerance = assImpMeshForEdges->getOccEdgeVertexTolerance();
                if (vertexTolerance > 0.0)
                    primitiveExtras.insert(QStringLiteral("occEdgeVertexTolerance"), vertexTolerance);
            }
        }

        primitive.insert(QStringLiteral("extras"), primitiveExtras);

        QJsonArray primitives;
        primitives.append(primitive);
        meshObj.insert(QStringLiteral("primitives"), primitives);
        document.meshes.append(meshObj);
    }

    QHash<const SceneNode*, int> nodeIndexByPtr;
    std::function<int(const SceneNode*)> appendNode = [&](const SceneNode* node) -> int {
        QJsonObject nodeObj;
        nodeObj.insert(QStringLiteral("id"), node->nodeUuid.toString(QUuid::WithoutBraces));
        nodeObj.insert(QStringLiteral("name"), node->name);
        nodeObj.insert(QStringLiteral("isSynthetic"), node->isSynthetic);
        if (!node->sourceFile.isEmpty())
            nodeObj.insert(QStringLiteral("sourceFile"), node->sourceFile);
        nodeObj.insert(QStringLiteral("matrix"), matrixToJson(node->localTransform));
        // Persist the autoOrient+autoScale correction so the exporter can factor it
        // out on the next export even after a save/load round-trip via MVF.
        if (!node->importCorrection.IsIdentity())
            nodeObj.insert(QStringLiteral("importCorrection"), matrixToJson(node->importCorrection));
        if (node->autoOrientApplied)
            nodeObj.insert(QStringLiteral("autoOrientApplied"), true);
        if (node->autoScaleApplied)
            nodeObj.insert(QStringLiteral("autoScaleApplied"), true);

        if (!node->meshUuids.isEmpty())
        {
            QJsonArray meshBindings;
            for (const QUuid& meshUuid : node->meshUuids)
            {
                QJsonObject bindingObj;
                bindingObj.insert(QStringLiteral("uuid"), meshUuid.toString(QUuid::WithoutBraces));
                bindingObj.insert(QStringLiteral("visible"), visibleMeshUuids.contains(meshUuid));
                bindingObj.insert(QStringLiteral("mesh"), meshIndexByUuid.value(meshUuid, -1));
                bindingObj.insert(QStringLiteral("materialOverride"), materialIndexByUuid.value(meshUuid, -1));
                meshBindings.append(bindingObj);
            }
            nodeObj.insert(QStringLiteral("meshBindings"), meshBindings);
        }

        document.nodes.append(nodeObj);
        const int nodeIndex = document.nodes.size() - 1;
        nodeIndexByPtr.insert(node, nodeIndex);

        QJsonArray children;
        for (const SceneNode* child : node->children)
            children.append(appendNode(child));

        QJsonObject updatedNode = document.nodes.at(nodeIndex).toObject();
        if (!children.isEmpty())
            updatedNode.insert(QStringLiteral("children"), children);
        document.nodes.replace(nodeIndex, updatedNode);
        return nodeIndex;
    };

    QJsonArray sceneRoots;
    for (const SceneNode* child : sceneGraph.root()->children)
        sceneRoots.append(appendNode(child));

    QJsonObject sceneObj;
    sceneObj.insert(QStringLiteral("name"), QStringLiteral("Default Scene"));
    sceneObj.insert(QStringLiteral("nodes"), sceneRoots);
    document.scenes.append(sceneObj);
    document.scene = 0;

    QJsonArray visibleArray;
    for (const QUuid& uuid : visibleMeshUuids)
        visibleArray.append(uuid.toString(QUuid::WithoutBraces));

    QJsonArray selectedArray;
    for (const QUuid& uuid : selectedMeshUuids)
        selectedArray.append(uuid.toString(QUuid::WithoutBraces));

    document.mvfSession.insert(QStringLiteral("visibleMeshUuids"), visibleArray);
    document.mvfSession.insert(QStringLiteral("selectedMeshUuids"), selectedArray);
    document.mvfSession.insert(QStringLiteral("geometryChunkPresent"), !package.geometryChunk.isEmpty());

    // Per-file punctual light data (names, enabled flags, repositioned positions)
    // is written by ModelViewer::buildMVFPackage() which has access to ViewportWidget's
    // repositioned state.  This builder only sees the raw SceneGraph positions and
    // has no way to apply the user's slider / model-rotation repositioning.

    const QStringList variantFiles = sceneGraph.filesWithVariants();
    if (!variantFiles.isEmpty())
    {
        QJsonArray variantFilesArray;
        for (const QString& sourceFile : variantFiles)
        {
            const GltfVariantData variantData = sceneGraph.variantDataForFile(sourceFile);
            if (variantData.isEmpty())
                continue;

            QJsonObject fileObj;
            fileObj.insert(QStringLiteral("sourceFile"), sourceFile);

            QJsonArray variantNames;
            for (const QString& variantName : variantData.variantNames)
                variantNames.append(variantName);
            fileObj.insert(QStringLiteral("variantNames"), variantNames);
            const int activeVariant = sceneGraph.activeVariantForFile(sourceFile);
            fileObj.insert(QStringLiteral("activeVariant"), activeVariant);

            QJsonArray meshVariantMappingsArray;
            for (auto it = variantData.meshVariantMappings.cbegin();
                 it != variantData.meshVariantMappings.cend();
                 ++it)
            {
                QJsonObject mappingObj;
                mappingObj.insert(QStringLiteral("sceneIndex"), it.key());
                mappingObj.insert(QStringLiteral("variantMappings"), variantMappingsToJson(it.value()));
                meshVariantMappingsArray.append(mappingObj);
            }
            fileObj.insert(QStringLiteral("meshVariantMappings"), meshVariantMappingsArray);
            variantFilesArray.append(fileObj);
        }

        if (!variantFilesArray.isEmpty())
            document.mvfSession.insert(QStringLiteral("variantFiles"), variantFilesArray);
    }

    QVector<GltfCameraData> camerasToSave = cameraDataByFile;
    if (camerasToSave.isEmpty())
    {
        const QStringList cameraFiles = sceneGraph.filesWithGltfCameras();
        for (const QString& sourceFile : cameraFiles)
        {
            const GltfCameraData cameraData = sceneGraph.gltfCameraDataForFile(sourceFile);
            if (!cameraData.isEmpty())
                camerasToSave.append(cameraData);
        }
    }

    if (!camerasToSave.isEmpty())
    {
        QJsonArray cameraFilesArray;
        for (const GltfCameraData& cameraData : camerasToSave)
        {
            if (cameraData.isEmpty() || cameraData.sourceFile.isEmpty())
                continue;

            QJsonObject fileObj;
            fileObj.insert(QStringLiteral("sourceFile"), cameraData.sourceFile);

            QJsonArray camerasArray;
            for (const GltfCameraEntry& camera : cameraData.cameras)
            {
                QJsonObject cameraObj;
                cameraObj.insert(QStringLiteral("name"), camera.name);
                cameraObj.insert(QStringLiteral("nodeName"), camera.nodeName);
                cameraObj.insert(QStringLiteral("nodeIndex"), camera.nodeIndex);
                cameraObj.insert(QStringLiteral("hasAiChildPath"), camera.hasAiChildPath);
                QJsonArray aiChildPath;
                for (int childIndex : camera.aiChildPath)
                    aiChildPath.append(childIndex);
                cameraObj.insert(QStringLiteral("aiChildPath"), aiChildPath);
                cameraObj.insert(QStringLiteral("type"),
                                 camera.type == GltfCameraType::Orthographic
                                     ? QStringLiteral("orthographic")
                                     : QStringLiteral("perspective"));
                cameraObj.insert(QStringLiteral("fovYRadians"), camera.fovYRadians);
                cameraObj.insert(QStringLiteral("zNear"), camera.zNear);
                cameraObj.insert(QStringLiteral("zFar"), camera.zFar);
                cameraObj.insert(QStringLiteral("xMag"), camera.xMag);
                cameraObj.insert(QStringLiteral("yMag"), camera.yMag);
                cameraObj.insert(QStringLiteral("worldPosition"),
                                 QJsonArray{camera.worldPosition.x(), camera.worldPosition.y(), camera.worldPosition.z()});
                cameraObj.insert(QStringLiteral("worldDirection"),
                                 QJsonArray{camera.worldDirection.x(), camera.worldDirection.y(), camera.worldDirection.z()});
                cameraObj.insert(QStringLiteral("worldUp"),
                                 QJsonArray{camera.worldUp.x(), camera.worldUp.y(), camera.worldUp.z()});
                cameraObj.insert(QStringLiteral("needsModelTransformCompensation"),
                                 camera.needsModelTransformCompensation);
                cameraObj.insert(QStringLiteral("capturedViewRange"), camera.capturedViewRange);
                camerasArray.append(cameraObj);
            }

            fileObj.insert(QStringLiteral("cameras"), camerasArray);
            cameraFilesArray.append(fileObj);
        }

        if (!cameraFilesArray.isEmpty())
            document.mvfSession.insert(QStringLiteral("cameraFiles"), cameraFilesArray);
    }

    const QStringList animationFiles = sceneGraph.filesWithAnimations();
    if (!animationFiles.isEmpty())
    {
        QJsonArray animationFilesArray;
        for (const QString& sourceFile : animationFiles)
        {
            const GltfAnimationData animationData = sceneGraph.animationDataForFile(sourceFile);
            if (animationData.isEmpty() && !animationData.hasSkinning)
                continue;

            QJsonObject fileObj;
            fileObj.insert(QStringLiteral("sourceFile"), sourceFile);
            fileObj.insert(QStringLiteral("hasNodeAnimations"), animationData.hasNodeAnimations);
            fileObj.insert(QStringLiteral("hasSkinning"), animationData.hasSkinning);
            fileObj.insert(QStringLiteral("hasMorphAnimations"), animationData.hasMorphAnimations);
            fileObj.insert(QStringLiteral("hasPointerAnimations"), animationData.hasPointerAnimations);
            fileObj.insert(QStringLiteral("activeClip"), sceneGraph.activeAnimationClipForFile(sourceFile));
            fileObj.insert(QStringLiteral("rootInverseTransform"), matrixToJson(animationData.rootInverseTransform));

            QJsonArray nodeBindingsArray;
            for (const GltfAnimationNodeBinding& binding : animationData.nodeBindings)
            {
                QJsonObject bindingObj;
                bindingObj.insert(QStringLiteral("nodeIndex"), binding.nodeIndex);
                bindingObj.insert(QStringLiteral("nodeName"), binding.nodeName);
                bindingObj.insert(QStringLiteral("hasAiChildPath"), binding.hasAiChildPath);
                QJsonArray childPath;
                for (int pathIndex : binding.aiChildPath)
                    childPath.append(pathIndex);
                bindingObj.insert(QStringLiteral("aiChildPath"), childPath);
                nodeBindingsArray.append(bindingObj);
            }
            fileObj.insert(QStringLiteral("nodeBindings"), nodeBindingsArray);

            QJsonArray visibilityStatesArray;
            for (const GltfAnimationNodeVisibilityState& state : animationData.nodeVisibilityStates)
            {
                visibilityStatesArray.append(QJsonObject{
                    {QStringLiteral("nodeIndex"), state.nodeIndex},
                    {QStringLiteral("parentNodeIndex"), state.parentNodeIndex},
                    {QStringLiteral("nodeName"), state.nodeName},
                    {QStringLiteral("defaultVisible"), state.defaultVisible}
                });
            }
            fileObj.insert(QStringLiteral("nodeVisibilityStates"), visibilityStatesArray);

            QJsonArray lightBindingsArray;
            for (const GltfAnimationLightBinding& binding : animationData.lightBindings)
            {
                lightBindingsArray.append(QJsonObject{
                    {QStringLiteral("parsedLightIndex"), binding.parsedLightIndex},
                    {QStringLiteral("lightDefinitionIndex"), binding.lightDefinitionIndex},
                    {QStringLiteral("nodeIndex"), binding.nodeIndex},
                    {QStringLiteral("nodeName"), binding.nodeName}
                });
            }
            fileObj.insert(QStringLiteral("lightBindings"), lightBindingsArray);

            QJsonArray clipsArray;
            for (const GltfAnimationClip& clip : animationData.clips)
                clipsArray.append(animationClipToJson(clip));
            fileObj.insert(QStringLiteral("clips"), clipsArray);

            animationFilesArray.append(fileObj);
        }

        if (!animationFilesArray.isEmpty())
            document.mvfSession.insert(QStringLiteral("animationFiles"), animationFilesArray);
    }

    document.extensionsUsed.append(QStringLiteral("MVF_material_ads"));
    document.extensionsUsed.append(QStringLiteral("MVF_material_pbr"));
    document.extensionsUsed.append(QStringLiteral("MVF_material_runtime"));

    QJsonObject updatedGeomBuffer = document.buffers.at(0).toObject();
    updatedGeomBuffer.insert(QStringLiteral("byteLength"), package.geometryChunk.size());
    document.buffers.replace(0, updatedGeomBuffer);

    // --- Image embedding ---
    // Try to read each image's source file and append it to the IMGS chunk.
    // glb:// URIs are resolved via TextureLocationManager (uses its cache).
    // KTX2 files are skipped (complex GPU-compressed format; path-only fallback).
    {
        TextureLocationManager resolver;

        for (int imgIdx = 0; imgIdx < document.images.size(); ++imgIdx)
        {
            QJsonObject imageObj = document.images[imgIdx].toObject();
            const QString origUri = imageObj[QStringLiteral("originalUri")].toString();
            if (origUri.isEmpty())
                continue;

            // Resolve the path
            QString resolvedPath = origUri;
            if (origUri.startsWith(QStringLiteral("glb://")))
            {
                const TextureMetadata meta = resolver.resolveTexture(origUri);
                if (meta.resolvedPath.isEmpty())
                    continue;
                resolvedPath = meta.resolvedPath;
            }

            const QString ext = QFileInfo(resolvedPath).suffix().toLower();
            if (ext == QLatin1String("ktx2"))
                continue;   // skip GPU-compressed textures

            QFile f(resolvedPath);
            if (!f.exists() || !f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray fileData = f.readAll();
            if (fileData.isEmpty())
                continue;

            QString mimeType;
            if      (ext == QLatin1String("png"))  mimeType = QStringLiteral("image/png");
            else if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
                                                   mimeType = QStringLiteral("image/jpeg");
            else if (ext == QLatin1String("webp")) mimeType = QStringLiteral("image/webp");
            else if (ext == QLatin1String("bmp"))  mimeType = QStringLiteral("image/bmp");
            else                                   mimeType = QStringLiteral("application/octet-stream");

            alignBuffer(package.imageChunk);
            const quint64 imgByteOffset = static_cast<quint64>(package.imageChunk.size());
            package.imageChunk.append(fileData);

            // Create a bufferView in the IMGS buffer (buffer index 1)
            QJsonObject imgBv;
            imgBv.insert(QStringLiteral("buffer"),     1);
            imgBv.insert(QStringLiteral("byteOffset"), static_cast<qint64>(imgByteOffset));
            imgBv.insert(QStringLiteral("byteLength"), static_cast<qint64>(fileData.size()));
            const int imgBvIndex = document.bufferViews.size();
            document.bufferViews.append(imgBv);

            imageObj.insert(QStringLiteral("bufferView"), imgBvIndex);
            imageObj.insert(QStringLiteral("byteLength"), static_cast<qint64>(fileData.size()));
            imageObj.insert(QStringLiteral("mimeType"),   mimeType);
            document.images.replace(imgIdx, imageObj);
        }

        if (!package.imageChunk.isEmpty())
        {
            QJsonObject updatedImgBuffer = document.buffers.at(1).toObject();
            updatedImgBuffer.insert(QStringLiteral("byteLength"),
                                    static_cast<qint64>(package.imageChunk.size()));
            document.buffers.replace(1, updatedImgBuffer);
        }
    }

    document.mvfSession.insert(QStringLiteral("imageChunkPresent"),
                                !package.imageChunk.isEmpty());

    return package;
}
}
