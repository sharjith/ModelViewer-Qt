#include "MvfMeshPreparationWorker.h"

#include "TangentGenerator.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstring>

namespace
{
static std::vector<float> readFloatStream(const QByteArray& chunk,
                                          const QJsonArray& accessors,
                                          const QJsonArray& bufferViews,
                                          int accessorIndex)
{
	if (accessorIndex < 0 || accessorIndex >= accessors.size())
		return {};
	const QJsonObject acc = accessors[accessorIndex].toObject();
	const int bvIdx = acc[QStringLiteral("bufferView")].toInt(-1);
	if (bvIdx < 0 || bvIdx >= bufferViews.size())
		return {};
	const QJsonObject bv = bufferViews[bvIdx].toObject();
	if (bv[QStringLiteral("buffer")].toInt(-1) != 0)
		return {};

	const int bvOffset   = static_cast<int>(bv[QStringLiteral("byteOffset")].toDouble(0));
	const int accOffset  = static_cast<int>(acc[QStringLiteral("byteOffset")].toDouble(0));
	const int byteOffset = bvOffset + accOffset;
	const int count      = static_cast<int>(acc[QStringLiteral("count")].toDouble(0));

	const QString type = acc[QStringLiteral("type")].toString();
	int components = 1;
	if      (type == QLatin1String("VEC2")) components = 2;
	else if (type == QLatin1String("VEC3")) components = 3;
	else if (type == QLatin1String("VEC4")) components = 4;

	const int totalFloats = count * components;
	if (byteOffset + totalFloats * static_cast<int>(sizeof(float)) > chunk.size())
		return {};

	std::vector<float> result(totalFloats);
	std::memcpy(result.data(), chunk.constData() + byteOffset, totalFloats * sizeof(float));
	return result;
}

static std::vector<unsigned int> readUIntStream(const QByteArray& chunk,
                                                const QJsonArray& accessors,
                                                const QJsonArray& bufferViews,
                                                int accessorIndex)
{
	if (accessorIndex < 0 || accessorIndex >= accessors.size())
		return {};
	const QJsonObject acc = accessors[accessorIndex].toObject();
	const int bvIdx = acc[QStringLiteral("bufferView")].toInt(-1);
	if (bvIdx < 0 || bvIdx >= bufferViews.size())
		return {};
	const QJsonObject bv = bufferViews[bvIdx].toObject();
	if (bv[QStringLiteral("buffer")].toInt(-1) != 0)
		return {};

	const int bvOffset   = static_cast<int>(bv[QStringLiteral("byteOffset")].toDouble(0));
	const int accOffset  = static_cast<int>(acc[QStringLiteral("byteOffset")].toDouble(0));
	const int byteOffset = bvOffset + accOffset;
	const int count      = static_cast<int>(acc[QStringLiteral("count")].toDouble(0));

	if (byteOffset + count * static_cast<int>(sizeof(unsigned int)) > chunk.size())
		return {};

	std::vector<unsigned int> result(count);
	std::memcpy(result.data(), chunk.constData() + byteOffset, count * sizeof(unsigned int));
	return result;
}

static void applyTextureRef(Material& mat,
                            Material::TextureType type,
                            const QJsonObject& texInfo,
                            const QHash<int, QString>& imagePaths,
                            const QJsonArray& textures,
                            const QJsonArray& samplers)
{
	const int texIndex = texInfo[QStringLiteral("index")].toInt(-1);
	if (texIndex < 0 || texIndex >= textures.size())
		return;
	const QJsonObject texObj   = textures[texIndex].toObject();
	const int imgIndex         = texObj[QStringLiteral("image")].toInt(-1);
	const int samplerIndex     = texObj[QStringLiteral("sampler")].toInt(-1);
	const QString path         = imagePaths.value(imgIndex);
	if (path.isEmpty())
		return;

	switch (type)
	{
	case Material::TextureType::Albedo:                   mat.setAlbedoMap(path); break;
	case Material::TextureType::Normal:                   mat.setNormalMap(path); break;
	case Material::TextureType::AmbientOcclusion:         mat.setAOMap(path); break;
	case Material::TextureType::Emissive:                 mat.setEmissiveMap(path); break;
	case Material::TextureType::Metallic:                 mat.setMetallicMap(path); break;
	case Material::TextureType::Roughness:                mat.setRoughnessMap(path); break;
	case Material::TextureType::Transmission:             mat.setTransmissionMap(path); break;
	case Material::TextureType::IOR:                      mat.setIORMap(path); break;
	case Material::TextureType::SheenColor:               mat.setSheenColorMap(path); break;
	case Material::TextureType::SheenRoughness:           mat.setSheenRoughnessMap(path); break;
	case Material::TextureType::ClearcoatColor:           mat.setClearcoatColorMap(path); break;
	case Material::TextureType::ClearcoatRoughness:       mat.setClearcoatRoughnessMap(path); break;
	case Material::TextureType::ClearcoatNormal:          mat.setClearcoatNormalMap(path); break;
	case Material::TextureType::Iridescence:              mat.setIridescenceMap(path); break;
	case Material::TextureType::IridescenceThickness:     mat.setIridescenceThicknessMap(path); break;
	case Material::TextureType::SpecularFactor:           mat.setSpecularFactorMap(path); break;
	case Material::TextureType::SpecularColor:            mat.setSpecularColorMap(path); break;
	case Material::TextureType::Anisotropy:               mat.setAnisotropyMap(path); break;
	case Material::TextureType::Thickness:                mat.setThicknessMap(path); break;
	case Material::TextureType::Diffuse:                  mat.setDiffuseMap(path); break;
	case Material::TextureType::DiffuseTransmission:      mat.setDiffuseTransmissionMap(path); break;
	case Material::TextureType::DiffuseTransmissionColor: mat.setDiffuseTransmissionColorMap(path); break;
	case Material::TextureType::SpecularGlossiness:       mat.setSpecularGlossinessMap(path); break;
	case Material::TextureType::Opacity:                  mat.setOpacityMap(path); break;
	case Material::TextureType::Height:                   mat.setHeightMap(path); break;
	default: return;
	}

	Material::Texture tex = mat.texture(type);
	tex.path = path.toStdString();
	tex.texCoordIndex = texInfo[QStringLiteral("texCoord")].toInt(0);

	if (samplerIndex >= 0 && samplerIndex < samplers.size())
	{
		const QJsonObject samp = samplers[samplerIndex].toObject();
		tex.magFilter = static_cast<GLenum>(samp[QStringLiteral("magFilter")].toInt(GL_LINEAR));
		tex.minFilter = static_cast<GLenum>(samp[QStringLiteral("minFilter")].toInt(GL_LINEAR_MIPMAP_LINEAR));
		tex.wrapS     = static_cast<GLenum>(samp[QStringLiteral("wrapS")].toInt(GL_REPEAT));
		tex.wrapT     = static_cast<GLenum>(samp[QStringLiteral("wrapT")].toInt(GL_REPEAT));
	}

	const QJsonObject extensions = texInfo[QStringLiteral("extensions")].toObject();
	const QJsonObject transform = extensions[QStringLiteral("KHR_texture_transform")].toObject();
	if (!transform.isEmpty())
	{
		const QJsonArray scale = transform[QStringLiteral("scale")].toArray();
		if (scale.size() >= 2)
			tex.scale = glm::vec2(static_cast<float>(scale[0].toDouble(1.0)),
			                      static_cast<float>(scale[1].toDouble(1.0)));

		const QJsonArray offset = transform[QStringLiteral("offset")].toArray();
		if (offset.size() >= 2)
			tex.offset = glm::vec2(static_cast<float>(offset[0].toDouble(0.0)),
			                       static_cast<float>(offset[1].toDouble(0.0)));

		tex.rotation = static_cast<float>(transform[QStringLiteral("rotation")].toDouble(0.0));
	}

	mat.setTexture(type, tex);
}

static Material reconstructMvfMaterial(const QJsonObject& matObj,
                                         const QHash<int, QString>& imagePaths,
                                         const QJsonArray& textures,
                                         const QJsonArray& samplers)
{
	const QString materialName = matObj[QStringLiteral("name")].toString();
	const QJsonObject exts = matObj[QStringLiteral("extensions")].toObject();
	const bool hasRuntimeMaterial =
		!exts[QStringLiteral("MVF_material_runtime")].toObject().isEmpty();

	Material mat = hasRuntimeMaterial
		? Material::fromVariantMap(exts[QStringLiteral("MVF_material_runtime")].toObject().toVariantMap())
		: Material();
	mat.setName(materialName);

	if (!hasRuntimeMaterial)
	{
		const QString shadingModel = matObj[QStringLiteral("shadingModel")].toString();
		if      (shadingModel == QLatin1String("PBR"))        mat.setShadingModel(Material::ShadingModel::PBR);
		else if (shadingModel == QLatin1String("BlinnPhong")) mat.setShadingModel(Material::ShadingModel::BlinnPhong);
		else if (shadingModel == QLatin1String("Unlit"))      mat.setShadingModel(Material::ShadingModel::Unlit);
		else if (shadingModel == QLatin1String("Toon"))       mat.setShadingModel(Material::ShadingModel::Toon);

		const QString blendMode = matObj[QStringLiteral("blendMode")].toString();
		if      (blendMode == QLatin1String("Opaque"))   mat.setBlendMode(Material::BlendMode::Opaque);
		else if (blendMode == QLatin1String("Masked"))   mat.setBlendMode(Material::BlendMode::Masked);
		else if (blendMode == QLatin1String("Alpha"))    mat.setBlendMode(Material::BlendMode::Alpha);
		else if (blendMode == QLatin1String("Additive")) mat.setBlendMode(Material::BlendMode::Additive);
		else if (blendMode == QLatin1String("Multiply")) mat.setBlendMode(Material::BlendMode::Multiply);

		mat.setTwoSided(matObj[QStringLiteral("doubleSided")].toBool(false));
		mat.setAlphaThreshold(static_cast<float>(matObj[QStringLiteral("alphaCutoff")].toDouble(0.5)));
		mat.setOpacity(static_cast<float>(matObj[QStringLiteral("opacity")].toDouble(1.0)));

		const QJsonObject pbr = matObj[QStringLiteral("pbr")].toObject();
		const QJsonArray bc = pbr[QStringLiteral("baseColorFactor")].toArray();
		if (bc.size() >= 3)
			mat.setAlbedoColor(QVector3D(static_cast<float>(bc[0].toDouble()),
			                             static_cast<float>(bc[1].toDouble()),
			                             static_cast<float>(bc[2].toDouble())));
		mat.setMetalness(static_cast<float>(pbr[QStringLiteral("metallicFactor")].toDouble(0.0)));
		mat.setRoughness(static_cast<float>(pbr[QStringLiteral("roughnessFactor")].toDouble(1.0)));
	}

	if (!hasRuntimeMaterial && exts.contains(QStringLiteral("MVF_material_ads")))
	{
		const QJsonObject ads = exts[QStringLiteral("MVF_material_ads")].toObject();
		auto v3 = [](const QJsonArray& a, const QVector3D& def = {}) -> QVector3D {
			return a.size() >= 3
				? QVector3D(static_cast<float>(a[0].toDouble()),
				            static_cast<float>(a[1].toDouble()),
				            static_cast<float>(a[2].toDouble()))
				: def;
		};
		mat.setAmbient(v3(ads[QStringLiteral("ambient")].toArray()));
		mat.setDiffuse(v3(ads[QStringLiteral("diffuse")].toArray()));
		mat.setSpecular(v3(ads[QStringLiteral("specular")].toArray()));
		mat.setEmissive(v3(ads[QStringLiteral("emissive")].toArray()));
		mat.setShininess(static_cast<float>(ads[QStringLiteral("shininess")].toDouble(32.0)));
	}

	if (exts.contains(QStringLiteral("MVF_material_pbr")))
	{
		const QJsonObject mvfPbr = exts[QStringLiteral("MVF_material_pbr")].toObject();

		if (!hasRuntimeMaterial)
		{
			mat.setIOR(static_cast<float>(mvfPbr[QStringLiteral("ior")].toDouble(1.5)));
			mat.setTransmission(static_cast<float>(mvfPbr[QStringLiteral("transmission")].toDouble(0.0)));
			mat.setClearcoat(static_cast<float>(mvfPbr[QStringLiteral("clearcoat")].toDouble(0.0)));
			mat.setClearcoatRoughness(static_cast<float>(mvfPbr[QStringLiteral("clearcoatRoughness")].toDouble(0.0)));
			const QJsonArray sc = mvfPbr[QStringLiteral("sheenColor")].toArray();
			if (sc.size() >= 3)
			{
				mat.setSheenColor(QVector3D(static_cast<float>(sc[0].toDouble()),
				                            static_cast<float>(sc[1].toDouble()),
				                            static_cast<float>(sc[2].toDouble())));
			}
			mat.setSheenRoughness(static_cast<float>(mvfPbr[QStringLiteral("sheenRoughness")].toDouble(0.0)));
		}

		static const struct { const char* key; Material::TextureType type; } kTexKeys[] = {
			{"baseColorTexture",                Material::TextureType::Albedo},
			{"normalTexture",                   Material::TextureType::Normal},
			{"occlusionTexture",                Material::TextureType::AmbientOcclusion},
			{"emissiveTexture",                 Material::TextureType::Emissive},
			{"metallicTexture",                 Material::TextureType::Metallic},
			{"roughnessTexture",                Material::TextureType::Roughness},
			{"transmissionTexture",             Material::TextureType::Transmission},
			{"iorTexture",                      Material::TextureType::IOR},
			{"sheenColorTexture",               Material::TextureType::SheenColor},
			{"sheenRoughnessTexture",           Material::TextureType::SheenRoughness},
			{"clearcoatTexture",                Material::TextureType::ClearcoatColor},
			{"clearcoatRoughnessTexture",       Material::TextureType::ClearcoatRoughness},
			{"clearcoatNormalTexture",          Material::TextureType::ClearcoatNormal},
			{"iridescenceTexture",              Material::TextureType::Iridescence},
			{"iridescenceThicknessTexture",     Material::TextureType::IridescenceThickness},
			{"specularTexture",                 Material::TextureType::SpecularFactor},
			{"specularColorTexture",            Material::TextureType::SpecularColor},
			{"anisotropyTexture",               Material::TextureType::Anisotropy},
			{"thicknessTexture",                Material::TextureType::Thickness},
			{"diffuseTexture",                  Material::TextureType::Diffuse},
			{"diffuseTransmissionTexture",      Material::TextureType::DiffuseTransmission},
			{"diffuseTransmissionColorTexture", Material::TextureType::DiffuseTransmissionColor},
			{"specularGlossinessTexture",       Material::TextureType::SpecularGlossiness},
			{"opacityTexture",                  Material::TextureType::Opacity},
			{"heightTexture",                   Material::TextureType::Height},
		};

		for (const auto& entry : kTexKeys)
		{
			const QString key = QLatin1String(entry.key);
			if (mvfPbr.contains(key))
				applyTextureRef(mat, entry.type, mvfPbr[key].toObject(), imagePaths, textures, samplers);
		}
	}

	mat.syncTextureParameters();
	mat.updateConsistency();
	return mat;
}

static QVector<int> jsonArrayToIntVector(const QJsonArray& array)
{
	QVector<int> values;
	values.reserve(array.size());
	for (const QJsonValue& value : array)
		values.append(value.toInt(-1));
	return values;
}

static QVector<GltfVariantMapping> parseVariantMappings(const QJsonArray& array)
{
	QVector<GltfVariantMapping> mappings;
	mappings.reserve(array.size());
	for (const QJsonValue& value : array)
	{
		const QJsonObject obj = value.toObject();
		GltfVariantMapping mapping;
		mapping.materialIndex = obj[QStringLiteral("materialIndex")].toInt(-1);
		mapping.variantIndices = jsonArrayToIntVector(obj[QStringLiteral("variantIndices")].toArray());
		mappings.append(mapping);
	}
	return mappings;
}
} // namespace

QVector<PreparedMvfMesh> MvfMeshPreparationWorker::prepare(const Mvf::Document& document,
                                                           const QByteArray& geometryChunk,
                                                           const QByteArray& imageChunk)
{
	QHash<int, QString> imagePaths;
	static QTemporaryDir s_embeddedImageDir;

	for (int i = 0; i < document.images.size(); ++i)
	{
		const QJsonObject imgObj  = document.images[i].toObject();
		const int bvIndex         = imgObj[QStringLiteral("bufferView")].toInt(-1);
		const QString origUri     = imgObj[QStringLiteral("originalUri")].toString();
		const QString mimeType    = imgObj[QStringLiteral("mimeType")].toString();

		if (bvIndex >= 0 && bvIndex < document.bufferViews.size() && !imageChunk.isEmpty())
		{
			const QJsonObject bv = document.bufferViews[bvIndex].toObject();
			if (bv[QStringLiteral("buffer")].toInt(-1) == 1)
			{
				const int offset = static_cast<int>(bv[QStringLiteral("byteOffset")].toDouble(0));
				const int length = static_cast<int>(bv[QStringLiteral("byteLength")].toDouble(0));
				if (length > 0 && offset + length <= imageChunk.size() && s_embeddedImageDir.isValid())
				{
					const QByteArray embeddedBytes = QByteArray(imageChunk.constData() + offset, length);

					QString ext = QStringLiteral(".bin");
					if      (mimeType == QLatin1String("image/png"))  ext = QStringLiteral(".png");
					else if (mimeType == QLatin1String("image/jpeg")) ext = QStringLiteral(".jpg");
					else if (mimeType == QLatin1String("image/webp")) ext = QStringLiteral(".webp");
					else if (mimeType == QLatin1String("image/bmp"))  ext = QStringLiteral(".bmp");

					const QString tempPath = s_embeddedImageDir.filePath(
						QStringLiteral("img_%1_%2%3")
							.arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
							.arg(i)
							.arg(ext));
					QFile f(tempPath);
					if (f.open(QIODevice::WriteOnly))
					{
						f.write(embeddedBytes);
						f.close();
						imagePaths[i] = tempPath;
						continue;
					}
				}
			}
		}

		if (!origUri.isEmpty())
			imagePaths[i] = origUri;
	}

	QVector<PreparedMvfMesh> result;
	result.reserve(document.meshes.size());

	for (int meshIdx = 0; meshIdx < document.meshes.size(); ++meshIdx)
	{
		const QJsonObject meshObj  = document.meshes[meshIdx].toObject();
		const QJsonArray primitives = meshObj[QStringLiteral("primitives")].toArray();
		if (primitives.isEmpty())
			continue;

		const QJsonObject prim    = primitives[0].toObject();
		const QJsonObject attribs = prim[QStringLiteral("attributes")].toObject();
		const QJsonObject extras  = prim[QStringLiteral("extras")].toObject();

		const std::vector<float> positions = readFloatStream(
			geometryChunk, document.accessors, document.bufferViews,
			attribs[QStringLiteral("POSITION")].toInt(-1));
		if (positions.empty())
			continue;

		const std::vector<unsigned int> indices = readUIntStream(
			geometryChunk, document.accessors, document.bufferViews,
			prim[QStringLiteral("indices")].toInt(-1));
		if (indices.empty())
			continue;

		const std::vector<float> normals  = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("NORMAL")].toInt(-1));
		const int tangentAccessorIndex = attribs[QStringLiteral("TANGENT")].toInt(-1);
		const std::vector<float> tangents = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, tangentAccessorIndex);
		const std::vector<float> uv0      = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("TEXCOORD_0")].toInt(-1));
		const std::vector<float> uv1      = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("TEXCOORD_1")].toInt(-1));
		const std::vector<float> uv2      = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("TEXCOORD_2")].toInt(-1));
		const std::vector<float> uv3      = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("TEXCOORD_3")].toInt(-1));
		const std::vector<float> colors   = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("COLOR_0")].toInt(-1));
		const std::vector<float> joints0  = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("JOINTS_0")].toInt(-1));
		const std::vector<float> weights0 = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, attribs[QStringLiteral("WEIGHTS_0")].toInt(-1));

		const size_t vertexCount = positions.size() / 3;
		std::vector<Vertex> vertices(vertexCount);
		const bool tangentAccessorIsVec4 =
			tangentAccessorIndex >= 0 &&
			tangentAccessorIndex < document.accessors.size() &&
			document.accessors[tangentAccessorIndex].toObject()[QStringLiteral("type")].toString() == QLatin1String("VEC4");
		const int tangentStride = tangentAccessorIsVec4 ? 4 : 3;
		for (size_t vi = 0; vi < vertexCount; ++vi)
		{
			Vertex& v = vertices[vi];
			v.Position = glm::vec3(positions[vi*3], positions[vi*3+1], positions[vi*3+2]);

			if (normals.size() >= vi * 3 + 3)
				v.Normal = glm::vec3(normals[vi*3], normals[vi*3+1], normals[vi*3+2]);
			if (tangents.size() >= static_cast<size_t>(vi * tangentStride + 3))
			{
				v.Tangent = glm::vec3(
					tangents[vi * tangentStride],
					tangents[vi * tangentStride + 1],
					tangents[vi * tangentStride + 2]);

				if (glm::length(v.Normal) > 0.0001f && glm::length(v.Tangent) > 0.0001f)
				{
					float handedness = 1.0f;
					if (tangentAccessorIsVec4 && tangents.size() > static_cast<size_t>(vi * tangentStride + 3))
						handedness = tangents[vi * tangentStride + 3] >= 0.0f ? 1.0f : -1.0f;
					v.Bitangent = glm::normalize(glm::cross(v.Normal, v.Tangent)) * handedness;
				}
			}

			if (uv0.size() >= vi*2+2) v.TexCoords[0] = glm::vec2(uv0[vi*2], uv0[vi*2+1]);
			if (uv1.size() >= vi*2+2) v.TexCoords[1] = glm::vec2(uv1[vi*2], uv1[vi*2+1]);
			if (uv2.size() >= vi*2+2) v.TexCoords[2] = glm::vec2(uv2[vi*2], uv2[vi*2+1]);
			if (uv3.size() >= vi*2+2) v.TexCoords[3] = glm::vec2(uv3[vi*2], uv3[vi*2+1]);

			v.Color = colors.size() >= vi*4+4
				? glm::vec4(colors[vi*4], colors[vi*4+1], colors[vi*4+2], colors[vi*4+3])
				: glm::vec4(1.0f);

			if (joints0.size() >= vi*4+4)
				v.JointIndices = glm::vec4(joints0[vi*4], joints0[vi*4+1], joints0[vi*4+2], joints0[vi*4+3]);
			if (weights0.size() >= vi*4+4)
				v.JointWeights = glm::vec4(weights0[vi*4], weights0[vi*4+1], weights0[vi*4+2], weights0[vi*4+3]);
		}

		const bool hasNormals = normals.size() >= vertexCount * 3;
		const bool hasUv0 = uv0.size() >= vertexCount * 2;
		const bool hasTangents = tangents.size() >= vertexCount * tangentStride;
		if (!hasTangents && hasNormals && hasUv0 && prim[QStringLiteral("mode")].toInt(GL_TRIANGLES) == GL_TRIANGLES)
			TangentGenerator::generateMikkTSpaceTangentsForMesh(vertices, indices);

		const int materialIndex = prim[QStringLiteral("material")].toInt(-1);
		Material material;
		if (materialIndex >= 0 && materialIndex < document.materials.size())
		{
			material = reconstructMvfMaterial(document.materials[materialIndex].toObject(),
			                                  imagePaths, document.textures, document.samplers);
		}

		PreparedMvfMesh prepared;
		prepared.name = meshObj[QStringLiteral("name")].toString();
		prepared.primitiveMode = static_cast<GLenum>(prim[QStringLiteral("mode")].toInt(GL_TRIANGLES));
		prepared.sceneIndex = extras[QStringLiteral("sceneIndex")].toInt(-1);
		prepared.hasNegativeScale = extras[QStringLiteral("hasNegativeScale")].toBool(false);
		prepared.originalMaterialIndex = extras[QStringLiteral("originalMaterialIndex")].toInt(-1);
		prepared.sourceFile = extras[QStringLiteral("sourceFile")].toString();
		prepared.sourceNodeName = extras[QStringLiteral("sourceNodeName")].toString();
		prepared.variantMappings = parseVariantMappings(extras[QStringLiteral("variantMappings")].toArray());
		const QString uuidStr = extras[QStringLiteral("meshUuid")].toString();
		prepared.uuid = uuidStr.isEmpty()
			? QUuid::fromString(meshObj[QStringLiteral("id")].toString())
			: QUuid::fromString(uuidStr);
		prepared.vertices = std::move(vertices);
		prepared.indices = std::move(indices);
		prepared.material = std::move(material);

		prepared.occEdgeSegments = readFloatStream(geometryChunk, document.accessors,
			document.bufferViews, extras[QStringLiteral("occEdgeAccessor")].toInt(-1));
		for (const QJsonValue& bv : extras[QStringLiteral("occEdgeBounds")].toArray())
			prepared.occEdgeBoundaries.push_back(bv.toInt());
		for (const QJsonValue& cv : extras[QStringLiteral("occEdgeCircles")].toArray())
		{
			OccEdgeCircleInfo info;
			if (cv.isObject())
			{
				const QJsonObject co = cv.toObject();
				info.isCircle = true;
				info.centerX = co[QStringLiteral("cx")].toDouble();
				info.centerY = co[QStringLiteral("cy")].toDouble();
				info.centerZ = co[QStringLiteral("cz")].toDouble();
				info.axisX   = co[QStringLiteral("ax")].toDouble();
				info.axisY   = co[QStringLiteral("ay")].toDouble();
				info.axisZ   = co[QStringLiteral("az")].toDouble();
				info.radius  = co[QStringLiteral("r")].toDouble();
			}
			// else: JSON null placeholder for a non-circle edge - info stays
			// default-constructed (isCircle = false).
			prepared.occEdgeCircles.push_back(info);
		}
		prepared.occEdgeVertexTolerance = extras[QStringLiteral("occEdgeVertexTolerance")].toDouble();

		for (const QJsonValue& jointValue : extras[QStringLiteral("skinJoints")].toArray())
		{
			const QJsonObject jointObj = jointValue.toObject();
			GltfSkinJoint joint;
			joint.nodeName = jointObj[QStringLiteral("nodeName")].toString();
			const QJsonArray matArr = jointObj[QStringLiteral("inverseBindMatrix")].toArray();
			if (matArr.size() == 16)
			{
				aiMatrix4x4& m = joint.inverseBindMatrix;
				m.a1 = static_cast<float>(matArr[0].toDouble());  m.a2 = static_cast<float>(matArr[1].toDouble());
				m.a3 = static_cast<float>(matArr[2].toDouble());  m.a4 = static_cast<float>(matArr[3].toDouble());
				m.b1 = static_cast<float>(matArr[4].toDouble());  m.b2 = static_cast<float>(matArr[5].toDouble());
				m.b3 = static_cast<float>(matArr[6].toDouble());  m.b4 = static_cast<float>(matArr[7].toDouble());
				m.c1 = static_cast<float>(matArr[8].toDouble());  m.c2 = static_cast<float>(matArr[9].toDouble());
				m.c3 = static_cast<float>(matArr[10].toDouble()); m.c4 = static_cast<float>(matArr[11].toDouble());
				m.d1 = static_cast<float>(matArr[12].toDouble()); m.d2 = static_cast<float>(matArr[13].toDouble());
				m.d3 = static_cast<float>(matArr[14].toDouble()); m.d4 = static_cast<float>(matArr[15].toDouble());
			}
			prepared.skinJoints.append(joint);
		}

		const QJsonObject meshTrs = extras[QStringLiteral("meshTrs")].toObject();
		if (!meshTrs.isEmpty())
		{
			prepared.meshTranslation = QVector3D(
				static_cast<float>(meshTrs[QStringLiteral("tx")].toDouble(0.0)),
				static_cast<float>(meshTrs[QStringLiteral("ty")].toDouble(0.0)),
				static_cast<float>(meshTrs[QStringLiteral("tz")].toDouble(0.0)));
			prepared.meshRotationQuat = QQuaternion(
				static_cast<float>(meshTrs[QStringLiteral("qw")].toDouble(1.0)),
				static_cast<float>(meshTrs[QStringLiteral("qx")].toDouble(0.0)),
				static_cast<float>(meshTrs[QStringLiteral("qy")].toDouble(0.0)),
				static_cast<float>(meshTrs[QStringLiteral("qz")].toDouble(0.0)));
			prepared.meshRotation = QVector3D(
				static_cast<float>(meshTrs[QStringLiteral("rx")].toDouble(0.0)),
				static_cast<float>(meshTrs[QStringLiteral("ry")].toDouble(0.0)),
				static_cast<float>(meshTrs[QStringLiteral("rz")].toDouble(0.0)));
			prepared.meshScale = QVector3D(
				static_cast<float>(meshTrs[QStringLiteral("sx")].toDouble(1.0)),
				static_cast<float>(meshTrs[QStringLiteral("sy")].toDouble(1.0)),
				static_cast<float>(meshTrs[QStringLiteral("sz")].toDouble(1.0)));
		}

		const QJsonArray variantMaterialsArray = extras[QStringLiteral("variantMaterials")].toArray();
		for (const QJsonValue& variantMaterialValue : variantMaterialsArray)
		{
			const QJsonObject variantMaterialObj = variantMaterialValue.toObject();
			const int key = variantMaterialObj[QStringLiteral("key")].toInt(-1);
			const QJsonObject materialObj = variantMaterialObj[QStringLiteral("material")].toObject();
			if (key < 0 || materialObj.isEmpty())
				continue;

			prepared.allVariantMaterials.insert(
				key,
				reconstructMvfMaterial(materialObj, imagePaths, document.textures, document.samplers));
		}

		const QJsonArray targetsArray = prim[QStringLiteral("targets")].toArray();
		if (!targetsArray.isEmpty())
		{
			QVector<MorphTargetData> morphTargets;
			morphTargets.reserve(targetsArray.size());
			for (const QJsonValue& targetVal : targetsArray)
			{
				const QJsonObject targetObj = targetVal.toObject();
				MorphTargetData mt;

				const std::vector<float> posD = readFloatStream(
					geometryChunk, document.accessors, document.bufferViews,
					targetObj[QStringLiteral("POSITION")].toInt(-1));
				if (!posD.empty())
				{
					mt.positionDeltas.reserve(posD.size() / 3);
					for (size_t i = 0; i + 2 < posD.size(); i += 3)
						mt.positionDeltas.push_back(glm::vec3(posD[i], posD[i+1], posD[i+2]));
				}

				const std::vector<float> norD = readFloatStream(
					geometryChunk, document.accessors, document.bufferViews,
					targetObj[QStringLiteral("NORMAL")].toInt(-1));
				if (!norD.empty())
				{
					mt.normalDeltas.reserve(norD.size() / 3);
					for (size_t i = 0; i + 2 < norD.size(); i += 3)
						mt.normalDeltas.push_back(glm::vec3(norD[i], norD[i+1], norD[i+2]));
				}

				const std::vector<float> tanD = readFloatStream(
					geometryChunk, document.accessors, document.bufferViews,
					targetObj[QStringLiteral("TANGENT")].toInt(-1));
				if (!tanD.empty())
				{
					mt.tangentDeltas.reserve(tanD.size() / 3);
					for (size_t i = 0; i + 2 < tanD.size(); i += 3)
						mt.tangentDeltas.push_back(glm::vec3(tanD[i], tanD[i+1], tanD[i+2]));
				}

				morphTargets.append(std::move(mt));
			}
			prepared.morphTargets = std::move(morphTargets);

			const QJsonArray defWeightsJson = extras[QStringLiteral("defaultMorphWeights")].toArray();
			QVector<float> defWeights;
			defWeights.reserve(defWeightsJson.size());
			for (const QJsonValue& wv : defWeightsJson)
				defWeights.append(static_cast<float>(wv.toDouble(0.0)));
			prepared.defaultMorphWeights = std::move(defWeights);
		}

		result.append(std::move(prepared));
	}

	return result;
}
