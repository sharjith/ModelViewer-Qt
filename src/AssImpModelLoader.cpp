#include "AssImpModelLoader.h"

#include "BRepToAssimpConverter.h"
#include "IXCAFDocProcessor.hxx"
#include "XCAFDocProcessorFactory.hxx"
#include "XCAFSTEPProcessor.hxx"
#include "XCAFIGESProcessor.hxx"
#include "XCAFBREPProcessor.hxx"

#include "MainWindow.h"
#include "ModelViewer.h"
#include "TangentGenerator.h"
#include "Utils.h"
#include "UVGenerator.h"
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QCheckBox>
#include <QLayout>
#include <Quantity_ColorRGBA.hxx>
#include <cmath>
#include <limits>
#include <unordered_set>

using namespace std;

namespace
{
QMatrix4x4 aiToQMatrix4x4(const aiMatrix4x4& matrix)
{
	QMatrix4x4 result;
	result.setRow(0, QVector4D(matrix.a1, matrix.a2, matrix.a3, matrix.a4));
	result.setRow(1, QVector4D(matrix.b1, matrix.b2, matrix.b3, matrix.b4));
	result.setRow(2, QVector4D(matrix.c1, matrix.c2, matrix.c3, matrix.c4));
	result.setRow(3, QVector4D(matrix.d1, matrix.d2, matrix.d3, matrix.d4));
	return result;
}

glm::vec3 computeFallbackTangent(const glm::vec3& normal)
{
	const glm::vec3 safeNormal = glm::length(normal) > 0.0001f
		? glm::normalize(normal)
		: glm::vec3(0.0f, 1.0f, 0.0f);

	glm::vec3 referenceAxis = std::abs(safeNormal.y) < 0.999f
		? glm::vec3(0.0f, 1.0f, 0.0f)
		: glm::vec3(1.0f, 0.0f, 0.0f);

	glm::vec3 tangent = glm::cross(referenceAxis, safeNormal);
	if (glm::length(tangent) <= 0.0001f)
	{
		referenceAxis = glm::vec3(0.0f, 0.0f, 1.0f);
		tangent = glm::cross(referenceAxis, safeNormal);
	}

	return glm::normalize(tangent);
}

void assignFallbackTexCoords(Vertex& vertex)
{
	for (glm::vec2& texCoord : vertex.TexCoords)
	{
		texCoord = glm::vec2(0.0f);
	}
}

void assignFallbackTangentBasis(Vertex& vertex)
{
	const glm::vec3 safeNormal = glm::length(vertex.Normal) > 0.0001f
		? glm::normalize(vertex.Normal)
		: glm::vec3(0.0f, 1.0f, 0.0f);

	vertex.Tangent = computeFallbackTangent(safeNormal);
	vertex.Bitangent = glm::normalize(glm::cross(safeNormal, vertex.Tangent));
}

struct GltfPrimitiveVertexBasis
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec3> tangents;
};

struct GltfPrimitiveReference
{
	int meshIndex = -1;
	int primitiveIndex = -1;
};

QVector<GltfPrimitiveReference> parsePrimitiveReferencesFromMeshName(const QString& meshName)
{
	static const QRegularExpression primitiveRefPattern(
		QStringLiteral(R"(meshes\[(\d+)\]-(\d+))"));

	QVector<GltfPrimitiveReference> refs;
	QSet<QString> seenRefs;
	auto it = primitiveRefPattern.globalMatch(meshName);
	while (it.hasNext())
	{
		const QRegularExpressionMatch match = it.next();
		if (!match.hasMatch())
			continue;

		const QString key = match.captured(0);
		if (seenRefs.contains(key))
			continue;

		seenRefs.insert(key);
		refs.append({
			match.captured(1).toInt(),
			match.captured(2).toInt()
		});
	}

	return refs;
}

bool resolveSingleGltfMaterialFromPrimitiveRefs(const QVector<GltfPrimitiveReference>& refs,
	const QJsonArray& jsonMeshes,
	int& outMaterialIndex)
{
	bool materialAssigned = false;
	int resolvedMaterial = -1;

	for (const GltfPrimitiveReference& ref : refs)
	{
		if (ref.meshIndex < 0 || ref.meshIndex >= jsonMeshes.size())
			return false;

		const QJsonArray primitives =
			jsonMeshes.at(ref.meshIndex).toObject().value("primitives").toArray();
		if (ref.primitiveIndex < 0 || ref.primitiveIndex >= primitives.size())
			return false;

		const QJsonObject primObj = primitives.at(ref.primitiveIndex).toObject();
		const int materialIndex = primObj.contains("material")
			? primObj.value("material").toInt(-1)
			: -1;

		if (!materialAssigned)
		{
			resolvedMaterial = materialIndex;
			materialAssigned = true;
		}
		else if (resolvedMaterial != materialIndex)
		{
			return false;
		}
	}

	if (!materialAssigned)
		return false;

	outMaterialIndex = resolvedMaterial;
	return resolvedMaterial >= 0;
}

bool resolveSingleGltfMaterialFromMeshName(const QString& aiMeshName,
	const QJsonArray& jsonMeshes,
	const QMap<QString, int>& meshNameToUniqueIndex,
	int& outMaterialIndex)
{
	auto resolveFromMeshIndex = [&](int meshIndex, int primitiveIndex) -> bool
	{
		if (meshIndex < 0 || meshIndex >= jsonMeshes.size())
			return false;

		const QJsonArray primitives =
			jsonMeshes.at(meshIndex).toObject().value("primitives").toArray();
		if (primitives.isEmpty())
			return false;

		if (primitiveIndex >= 0)
		{
			if (primitiveIndex >= primitives.size())
				return false;

			const QJsonObject primObj = primitives.at(primitiveIndex).toObject();
			outMaterialIndex = primObj.contains("material")
				? primObj.value("material").toInt(-1)
				: -1;
			return outMaterialIndex >= 0;
		}

		int resolvedMaterial = -1;
		for (const QJsonValue& primVal : primitives)
		{
			const QJsonObject primObj = primVal.toObject();
			const int materialIndex = primObj.contains("material")
				? primObj.value("material").toInt(-1)
				: -1;

			if (resolvedMaterial < 0)
				resolvedMaterial = materialIndex;
			else if (resolvedMaterial != materialIndex)
				return false;
		}

		outMaterialIndex = resolvedMaterial;
		return resolvedMaterial >= 0;
	};

	const auto exactIt = meshNameToUniqueIndex.find(aiMeshName);
	if (exactIt != meshNameToUniqueIndex.end() &&
		resolveFromMeshIndex(exactIt.value(), -1))
	{
		return true;
	}

	static const QRegularExpression primitiveSuffixPattern(
		QStringLiteral(R"(^(.*)-(\d+)$)"));
	const QRegularExpressionMatch match = primitiveSuffixPattern.match(aiMeshName);
	if (!match.hasMatch())
		return false;

	const QString baseName = match.captured(1);
	const int primitiveIndex = match.captured(2).toInt();
	const auto baseIt = meshNameToUniqueIndex.find(baseName);
	if (baseIt == meshNameToUniqueIndex.end())
		return false;

	return resolveFromMeshIndex(baseIt.value(), primitiveIndex);
}

std::vector<unsigned int> buildMorphVertexRemap(const std::vector<Vertex>& importedVertices,
	const GltfPrimitiveVertexBasis& sourceBasis)
{
	auto squaredLength = [](const auto& value)
	{
		return glm::dot(value, value);
	};

	if (importedVertices.size() != sourceBasis.positions.size())
		return {};

	std::vector<unsigned int> remap(importedVertices.size(), 0);
	std::vector<bool> used(sourceBasis.positions.size(), false);

	for (size_t importedIndex = 0; importedIndex < importedVertices.size(); ++importedIndex)
	{
		const Vertex& imported = importedVertices[importedIndex];
		float bestScore = std::numeric_limits<float>::max();
		int bestIndex = -1;

		for (int sourceIndex = 0; sourceIndex < static_cast<int>(sourceBasis.positions.size()); ++sourceIndex)
		{
			if (used[sourceIndex])
				continue;

			const glm::vec3 sourcePosition = sourceBasis.positions[sourceIndex];
			const glm::vec3 sourceNormal = sourceIndex < static_cast<int>(sourceBasis.normals.size())
				? sourceBasis.normals[sourceIndex]
				: glm::vec3(0.0f);
			const glm::vec3 sourceTangent = sourceIndex < static_cast<int>(sourceBasis.tangents.size())
				? sourceBasis.tangents[sourceIndex]
				: glm::vec3(0.0f);

			const float positionScore = squaredLength(imported.Position - sourcePosition);
			const float normalScore = squaredLength(imported.Normal - sourceNormal);
			const float tangentScore = squaredLength(imported.Tangent - sourceTangent);
			const float texScore = squaredLength(imported.TexCoords[0]);
			const float score = positionScore * 1000.0f + normalScore * 100.0f + tangentScore * 10.0f + texScore;

			if (score < bestScore)
			{
				bestScore = score;
				bestIndex = sourceIndex;
			}
		}

		if (bestIndex < 0)
			return {};

		used[bestIndex] = true;
		remap[importedIndex] = static_cast<unsigned int>(bestIndex);
	}

	return remap;
}

void reorderMorphTargetsToImportedVertexOrder(QVector<MorphTargetData>& morphTargets,
	const std::vector<unsigned int>& remap)
{
	if (morphTargets.isEmpty() || remap.empty())
		return;

	for (MorphTargetData& morphTarget : morphTargets)
	{
		if (!morphTarget.positionDeltas.empty())
		{
			std::vector<glm::vec3> reordered(remap.size());
			for (size_t importedIndex = 0; importedIndex < remap.size(); ++importedIndex)
				reordered[importedIndex] = morphTarget.positionDeltas[remap[importedIndex]];
			morphTarget.positionDeltas = std::move(reordered);
		}

		if (!morphTarget.normalDeltas.empty())
		{
			std::vector<glm::vec3> reordered(remap.size());
			for (size_t importedIndex = 0; importedIndex < remap.size(); ++importedIndex)
				reordered[importedIndex] = morphTarget.normalDeltas[remap[importedIndex]];
			morphTarget.normalDeltas = std::move(reordered);
		}

		if (!morphTarget.tangentDeltas.empty())
		{
			std::vector<glm::vec3> reordered(remap.size());
			for (size_t importedIndex = 0; importedIndex < remap.size(); ++importedIndex)
				reordered[importedIndex] = morphTarget.tangentDeltas[remap[importedIndex]];
			morphTarget.tangentDeltas = std::move(reordered);
		}
	}
}

// gltfPath  – path to the .glb / .gltf file
// matProc   – optional pointer to the loader's MaterialProcessor instance; when
//             provided its per-instance GLB caches are checked first so the file
//             is not re-read on every mesh.  Pass nullptr to force a cold read
//             (used only when no loader context is available).
bool loadAnimationJsonAndBuffer(const QString& gltfPath, QJsonDocument& doc,
                                QVector<QByteArray>& bufferData,
                                MaterialProcessor* matProc = nullptr)
{
	const bool isGlb = gltfPath.endsWith(".glb", Qt::CaseInsensitive);
	if (isGlb)
	{
		// Use the per-loader-instance cache populated by updateAiSceneWithGltfMaterials
		// and processGltf2CoreAndExtensions to avoid re-reading large binaries per mesh.
		if (matProc && matProc->fillAnimDataFromCache(gltfPath, doc, bufferData))
			return true;

		std::vector<uint8_t> glbBinaryBuffer;
		const QString jsonString = MaterialProcessor::extractJsonFromGLB(gltfPath, glbBinaryBuffer);
		if (jsonString.isEmpty())
			return false;

		doc = QJsonDocument::fromJson(jsonString.toUtf8());
		if (!doc.isObject())
			return false;

		bufferData.clear();
		bufferData.append(QByteArray(reinterpret_cast<const char*>(glbBinaryBuffer.data()),
			static_cast<int>(glbBinaryBuffer.size())));
		qDebug() << "[ANIM-GLB-LOAD] loaded GLB binary buffer size=" << (int)glbBinaryBuffer.size()
		         << "path=" << gltfPath;
		return true;
	}

	QFile file(gltfPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	doc = QJsonDocument::fromJson(file.readAll());
	file.close();
	if (!doc.isObject())
		return false;

	const QJsonArray buffers = doc.object().value("buffers").toArray();
	bufferData.clear();
	if (buffers.isEmpty())
		return true;

	for (const QJsonValue& bufferValue : buffers)
	{
		const QString uri = bufferValue.toObject().value("uri").toString();
		if (uri.isEmpty())
			return false;

		if (uri.startsWith("data:", Qt::CaseInsensitive))
		{
			const QString dataUriContent = uri.mid(5);
			const int commaIdx = dataUriContent.indexOf(',');
			if (commaIdx < 0)
				return false;

			const QString metadata = dataUriContent.left(commaIdx);
			if (!metadata.endsWith("base64", Qt::CaseInsensitive))
				return false;

			const QByteArray decoded = QByteArray::fromBase64(dataUriContent.mid(commaIdx + 1).toUtf8());
			if (decoded.isEmpty())
				return false;

			bufferData.append(decoded);
			continue;
		}

		QFile bufferFile(QFileInfo(gltfPath).dir().filePath(uri));
		if (!bufferFile.open(QIODevice::ReadOnly))
			return false;
		bufferData.append(bufferFile.readAll());
	}
	return true;
}

bool readFloatAccessorData(const QJsonArray& accessors,
	const QJsonArray& bufferViews,
	const QVector<QByteArray>& bufferData,
	int accessorIndex,
	int expectedComponents,
	QVector<float>& outValues,
	int* outElementCount = nullptr)
{
	if (accessorIndex < 0 || accessorIndex >= accessors.size())
		return false;

	const QJsonObject accessor = accessors.at(accessorIndex).toObject();
	if (accessor.value("componentType").toInt() != 5126 || accessor.contains("sparse"))
		return false;

	const QString type = accessor.value("type").toString();
	int components = 0;
	if (type == "SCALAR") components = 1;
	else if (type == "VEC2") components = 2;
	else if (type == "VEC3") components = 3;
	else if (type == "VEC4") components = 4;
	else return false;

	if (expectedComponents > 0 && components != expectedComponents)
		return false;

	const int bufferViewIndex = accessor.value("bufferView").toInt(-1);
	if (bufferViewIndex < 0 || bufferViewIndex >= bufferViews.size())
		return false;

	const QJsonObject bufferView = bufferViews.at(bufferViewIndex).toObject();
	const int bufferIndex = bufferView.value("buffer").toInt(0);
	if (bufferIndex < 0 || bufferIndex >= bufferData.size())
		return false;
	const QByteArray& activeBuffer = bufferData[bufferIndex];
	const int count = accessor.value("count").toInt(0);
	const int accessorByteOffset = accessor.value("byteOffset").toInt(0);
	const int bufferViewByteOffset = bufferView.value("byteOffset").toInt(0);
	const int byteStride = bufferView.value("byteStride").toInt(components * static_cast<int>(sizeof(float)));
	const int baseOffset = bufferViewByteOffset + accessorByteOffset;

	if (count <= 0 || byteStride < components * static_cast<int>(sizeof(float)))
		return false;

	outValues.resize(count * components);
	for (int elementIndex = 0; elementIndex < count; ++elementIndex)
	{
		const int offset = baseOffset + elementIndex * byteStride;
		const int requiredBytes = offset + components * static_cast<int>(sizeof(float));
		if (requiredBytes > activeBuffer.size())
			return false;

		const float* src = reinterpret_cast<const float*>(activeBuffer.constData() + offset);
		for (int componentIndex = 0; componentIndex < components; ++componentIndex)
			outValues[elementIndex * components + componentIndex] = src[componentIndex];
	}

	if (outElementCount)
		*outElementCount = count;
	return true;
}

bool readScalarAccessorDataAsFloats(const QJsonArray& accessors,
	const QJsonArray& bufferViews,
	const QVector<QByteArray>& bufferData,
	int accessorIndex,
	QVector<float>& outValues,
	int* outElementCount = nullptr)
{
	if (accessorIndex < 0 || accessorIndex >= accessors.size())
		return false;

	const QJsonObject accessor = accessors.at(accessorIndex).toObject();
	if (accessor.contains("sparse") || accessor.value("type").toString() != "SCALAR")
		return false;

	const int componentType = accessor.value("componentType").toInt();
	const int bufferViewIndex = accessor.value("bufferView").toInt(-1);
	if (bufferViewIndex < 0 || bufferViewIndex >= bufferViews.size())
		return false;

	int componentSize = 0;
	switch (componentType)
	{
	case 5120: componentSize = 1; break; // BYTE
	case 5121: componentSize = 1; break; // UNSIGNED_BYTE
	case 5122: componentSize = 2; break; // SHORT
	case 5123: componentSize = 2; break; // UNSIGNED_SHORT
	case 5125: componentSize = 4; break; // UNSIGNED_INT
	case 5126: componentSize = 4; break; // FLOAT
	default:
		return false;
	}

	const QJsonObject bufferView = bufferViews.at(bufferViewIndex).toObject();
	const int bufferIndex = bufferView.value("buffer").toInt(0);
	if (bufferIndex < 0 || bufferIndex >= bufferData.size())
		return false;
	const QByteArray& activeBuffer = bufferData[bufferIndex];
	const int count = accessor.value("count").toInt(0);
	const int accessorByteOffset = accessor.value("byteOffset").toInt(0);
	const int bufferViewByteOffset = bufferView.value("byteOffset").toInt(0);
	const int byteStride = bufferView.value("byteStride").toInt(componentSize);
	const int baseOffset = bufferViewByteOffset + accessorByteOffset;
	if (count <= 0 || byteStride < componentSize)
		return false;

	outValues.resize(count);
	for (int elementIndex = 0; elementIndex < count; ++elementIndex)
	{
		const int offset = baseOffset + elementIndex * byteStride;
		const int requiredBytes = offset + componentSize;
		if (requiredBytes > activeBuffer.size())
			return false;

		const char* src = activeBuffer.constData() + offset;
		switch (componentType)
		{
		case 5120: outValues[elementIndex] = *reinterpret_cast<const qint8*>(src); break;
		case 5121: outValues[elementIndex] = *reinterpret_cast<const quint8*>(src); break;
		case 5122: outValues[elementIndex] = *reinterpret_cast<const qint16*>(src); break;
		case 5123: outValues[elementIndex] = *reinterpret_cast<const quint16*>(src); break;
		case 5125: outValues[elementIndex] = static_cast<float>(*reinterpret_cast<const quint32*>(src)); break;
		case 5126: outValues[elementIndex] = *reinterpret_cast<const float*>(src); break;
		default: return false;
		}
	}

	if (outElementCount)
		*outElementCount = count;
	return true;
}

bool decodeAnimationPointerTarget(const QString& pointer,
	int& materialIndex,
	GltfAnimationTextureTarget& textureTarget,
	GltfAnimationPointerProperty& pointerProperty)
{
	static const QRegularExpression propertyRegex(
		QStringLiteral("^/materials/(\\d+)/(.*)/extensions/KHR_texture_transform/(offset|scale|rotation)$"));
	const QRegularExpressionMatch match = propertyRegex.match(pointer);
	if (!match.hasMatch())
		return false;

	materialIndex = match.captured(1).toInt();
	const QString texturePath = match.captured(2);
	const QString property = match.captured(3);

	if (property == "offset")
		pointerProperty = GltfAnimationPointerProperty::Offset;
	else if (property == "scale")
		pointerProperty = GltfAnimationPointerProperty::Scale;
	else if (property == "rotation")
		pointerProperty = GltfAnimationPointerProperty::Rotation;
	else
		return false;

	if (texturePath == "pbrMetallicRoughness/baseColorTexture")
		textureTarget = GltfAnimationTextureTarget::Albedo;
	else if (texturePath == "pbrMetallicRoughness/metallicRoughnessTexture")
		textureTarget = GltfAnimationTextureTarget::MetallicRoughness;
	else if (texturePath == "normalTexture")
		textureTarget = GltfAnimationTextureTarget::Normal;
	else if (texturePath == "occlusionTexture")
		textureTarget = GltfAnimationTextureTarget::Occlusion;
	else if (texturePath == "emissiveTexture")
		textureTarget = GltfAnimationTextureTarget::Emissive;
	else if (texturePath == "extensions/KHR_materials_transmission/transmissionTexture")
		textureTarget = GltfAnimationTextureTarget::Transmission;
	else if (texturePath == "extensions/KHR_materials_volume/thicknessTexture")
		textureTarget = GltfAnimationTextureTarget::Thickness;
	else if (texturePath == "extensions/KHR_materials_volume/attenuationTexture")
		textureTarget = GltfAnimationTextureTarget::Thickness;
	else if (texturePath == "extensions/KHR_materials_sheen/sheenColorTexture")
		textureTarget = GltfAnimationTextureTarget::SheenColor;
	else if (texturePath == "extensions/KHR_materials_sheen/sheenRoughnessTexture")
		textureTarget = GltfAnimationTextureTarget::SheenRoughness;
	else if (texturePath == "extensions/KHR_materials_clearcoat/clearcoatTexture")
		textureTarget = GltfAnimationTextureTarget::Clearcoat;
	else if (texturePath == "extensions/KHR_materials_clearcoat/clearcoatRoughnessTexture")
		textureTarget = GltfAnimationTextureTarget::ClearcoatRoughness;
	else if (texturePath == "extensions/KHR_materials_clearcoat/clearcoatNormalTexture")
		textureTarget = GltfAnimationTextureTarget::ClearcoatNormal;
	else if (texturePath == "extensions/KHR_materials_iridescence/iridescenceTexture")
		textureTarget = GltfAnimationTextureTarget::Iridescence;
	else if (texturePath == "extensions/KHR_materials_iridescence/iridescenceThicknessTexture")
		textureTarget = GltfAnimationTextureTarget::IridescenceThickness;
	else if (texturePath == "extensions/KHR_materials_specular/specularTexture")
		textureTarget = GltfAnimationTextureTarget::SpecularFactor;
	else if (texturePath == "extensions/KHR_materials_specular/specularColorTexture")
		textureTarget = GltfAnimationTextureTarget::SpecularColor;
	else if (texturePath == "extensions/KHR_materials_anisotropy/anisotropyTexture")
		textureTarget = GltfAnimationTextureTarget::Anisotropy;
	else if (texturePath == "extensions/KHR_materials_diffuse_transmission/diffuseTransmissionTexture")
		textureTarget = GltfAnimationTextureTarget::DiffuseTransmission;
	else if (texturePath == "extensions/KHR_materials_diffuse_transmission/diffuseTransmissionColorTexture")
		textureTarget = GltfAnimationTextureTarget::DiffuseTransmissionColor;
	else if (texturePath == "extensions/KHR_materials_pbrSpecularGlossiness/diffuseTexture")
		textureTarget = GltfAnimationTextureTarget::Diffuse;
	else if (texturePath == "extensions/KHR_materials_pbrSpecularGlossiness/specularGlossinessTexture")
		textureTarget = GltfAnimationTextureTarget::SpecularGlossiness;
	else
		textureTarget = GltfAnimationTextureTarget::Unknown;

	return textureTarget != GltfAnimationTextureTarget::Unknown;
}

bool decodeMaterialFactorPointerTarget(const QString& pointer,
	int& materialIndex,
	GltfAnimationPointerProperty& pointerProperty)
{
	static const QRegularExpression baseColorRegex(
		QStringLiteral("^/materials/(\\d+)/pbrMetallicRoughness/baseColorFactor$"));
	const QRegularExpressionMatch match = baseColorRegex.match(pointer);
	if (!match.hasMatch())
		return false;

	materialIndex = match.captured(1).toInt();
	pointerProperty = GltfAnimationPointerProperty::BaseColorFactor;
	return materialIndex >= 0;
}

bool decodeNodeVisibilityPointerTarget(const QString& pointer, int& nodeIndex)
{
	static const QRegularExpression visibilityRegex(
		QStringLiteral("^/nodes/(\\d+)/extensions/KHR_node_visibility/visible$"));
	const QRegularExpressionMatch match = visibilityRegex.match(pointer);
	if (!match.hasMatch())
		return false;

	nodeIndex = match.captured(1).toInt();
	return nodeIndex >= 0;
}

bool loadMorphTargetsForAiMesh(const QString& gltfPath,
	const aiScene* scene,
	unsigned int aiMeshIndex,
	unsigned int expectedVertexCount,
	QVector<MorphTargetData>& outTargets,
	QVector<float>& outDefaultWeights,
	GltfPrimitiveVertexBasis* outBaseBasis = nullptr,
	MaterialProcessor* matProc = nullptr)
{
	QJsonDocument doc;
	QVector<QByteArray> bufferData;
	if (!loadAnimationJsonAndBuffer(gltfPath, doc, bufferData, matProc) || !doc.isObject() || !scene || !scene->mRootNode)
		return false;

	const QJsonObject root = doc.object();
	const QJsonArray jsonNodes = root.value("nodes").toArray();
	const QJsonArray jsonMeshes = root.value("meshes").toArray();
	const QJsonArray jsonScenes = root.value("scenes").toArray();
	const QJsonArray accessors = root.value("accessors").toArray();
	const QJsonArray bufferViews = root.value("bufferViews").toArray();

	const int sceneIdx = root.value("scene").toInt(0);
	QJsonArray rootNodeIndices;
	if (sceneIdx >= 0 && sceneIdx < jsonScenes.size())
		rootNodeIndices = jsonScenes[sceneIdx].toObject().value("nodes").toArray();
	if (rootNodeIndices.isEmpty())
		return false;

	struct Frame { aiNode* aiNodePtr; int gltfNodeIdx; };
	QVector<Frame> stack;
	aiNode* aiSceneRoot = scene->mRootNode;
	if (rootNodeIndices.size() == 1)
	{
		const int rootGltfIdx = rootNodeIndices[0].toInt();
		const QString gltfRootName = (rootGltfIdx >= 0 && rootGltfIdx < jsonNodes.size())
			? jsonNodes[rootGltfIdx].toObject().value("name").toString() : QString();
		const QString aiRootName = QString::fromUtf8(aiSceneRoot->mName.C_Str());
		if (aiRootName == gltfRootName || aiSceneRoot->mNumMeshes > 0)
			stack.append({ aiSceneRoot, rootGltfIdx });
		else if (aiSceneRoot->mNumMeshes == 0 && aiSceneRoot->mNumChildren == 1)
			stack.append({ aiSceneRoot->mChildren[0], rootGltfIdx });
	}
	else
	{
		for (int i = static_cast<int>(rootNodeIndices.size()) - 1; i >= 0; --i)
			if (i < static_cast<int>(aiSceneRoot->mNumChildren))
				stack.append({ aiSceneRoot->mChildren[i], rootNodeIndices[i].toInt() });
	}

	while (!stack.isEmpty())
	{
		const Frame frame = stack.takeLast();
		aiNode* aiNodePtr = frame.aiNodePtr;
		const int nodeIdx = frame.gltfNodeIdx;
		if (!aiNodePtr || nodeIdx < 0 || nodeIdx >= jsonNodes.size())
			continue;

		const QJsonObject nodeObj = jsonNodes[nodeIdx].toObject();
		if (nodeObj.contains("mesh") && aiNodePtr->mNumMeshes > 0)
		{
			const int gltfMeshIndex = nodeObj.value("mesh").toInt(-1);
			if (gltfMeshIndex >= 0 && gltfMeshIndex < jsonMeshes.size())
			{
				const QJsonObject meshObj = jsonMeshes[gltfMeshIndex].toObject();
				const QJsonArray primitives = meshObj.value("primitives").toArray();
				for (int primitiveIndex = 0; primitiveIndex < primitives.size() && primitiveIndex < static_cast<int>(aiNodePtr->mNumMeshes); ++primitiveIndex)
				{
					const unsigned int candidateAiMeshIndex = aiNodePtr->mMeshes[primitiveIndex];
					if (candidateAiMeshIndex != aiMeshIndex)
						continue;

					const QJsonObject primitiveObj = primitives[primitiveIndex].toObject();

					// KHR_draco_mesh_compression: this primitive's base
					// attributes (and indices) live only inside the
					// compressed blob referenced by primitiveObj.extensions.
					// KHR_draco_mesh_compression.bufferView - their "regular"
					// accessor entries (POSITION/NORMAL/TEXCOORD_0 etc.,
					// referenced from primitiveObj.attributes) intentionally
					// omit bufferView/byteOffset, since the real data isn't
					// there. readBasisAccessor() below reads those regular
					// accessor entries directly via readFloatAccessorData(),
					// which has no Draco decoder and simply fails (no
					// bufferView to read), leaving outBaseBasis empty - which
					// silently skips buildMorphVertexRemap()/
					// reorderMorphTargetsToImportedVertexOrder() at the call
					// site below, the exact step that exists to reconcile
					// Assimp's decoded vertex order against this function's
					// own raw-glTF-order reads. Draco compression reorders
					// vertices as part of its encoding, so skipping that
					// remap silently misapplies every morph delta read below
					// to the wrong vertex - confirmed via glTF-Sample-Assets'
					// MorphPrimitivesTest (glTF-Draco variant only; the
					// uncompressed glTF/glb variants of the same asset render
					// correctly, isolating this to Draco specifically).
					//
					// Bailing out here (before touching outTargets/
					// outDefaultWeights/outBaseBasis at all) leaves the
					// caller's ALREADY-POPULATED native-Assimp result (built
					// from mesh->mAnimMeshes just before this function is
					// called) standing uncontested instead of being
					// overwritten with data this function cannot correctly
					// order. That native path is reliable for exactly this
					// case: Assimp's own glTF2 importer (glTF2Importer.cpp's
					// target.position[0]->ExtractData(positionDiff,
					// vertexRemappingTable) call) extracts morph deltas using
					// the SAME vertexRemappingTable it uses for every other
					// per-vertex attribute (positions, colors, texcoords),
					// so it's internally consistent with whatever reordering
					// Draco decompression performed - unlike this function,
					// which has no visibility into that table at all.
					const QJsonObject primitiveExtensions = primitiveObj.value("extensions").toObject();
					if (primitiveExtensions.contains("KHR_draco_mesh_compression"))
						return false;

					const QJsonArray targets = primitiveObj.value("targets").toArray();
					if (targets.isEmpty())
						return false;

					if (outBaseBasis)
					{
						outBaseBasis->positions.clear();
						outBaseBasis->normals.clear();
						outBaseBasis->tangents.clear();

						const QJsonObject attributesObj = primitiveObj.value("attributes").toObject();
						auto readBasisAccessor = [&](const char* key, std::vector<glm::vec3>& destination)
						{
							const int accessorIndex = attributesObj.value(QString::fromUtf8(key)).toInt(-1);
							if (accessorIndex < 0)
								return;

							QVector<float> values;
							int count = 0;
							if (!readFloatAccessorData(accessors, bufferViews, bufferData, accessorIndex, 3, values, &count))
								return;
							if (count != static_cast<int>(expectedVertexCount))
								return;

							destination.reserve(count);
							for (int i = 0; i < count; ++i)
							{
								destination.emplace_back(
									values[i * 3 + 0],
									values[i * 3 + 1],
									values[i * 3 + 2]);
							}
						};

						readBasisAccessor("POSITION", outBaseBasis->positions);
						readBasisAccessor("NORMAL", outBaseBasis->normals);
						readBasisAccessor("TANGENT", outBaseBasis->tangents);
					}

					outDefaultWeights.clear();
					const QJsonArray weights = meshObj.value("weights").toArray();
					outDefaultWeights.reserve(weights.size());
					for (const QJsonValue& value : weights)
						outDefaultWeights.append(static_cast<float>(value.toDouble(0.0)));

					outTargets.clear();
					outTargets.reserve(targets.size());
					for (const QJsonValue& targetValue : targets)
					{
						const QJsonObject targetObj = targetValue.toObject();
						MorphTargetData morphTarget;

						auto readVec3Accessor = [&](const char* key, std::vector<glm::vec3>& destination)
						{
							const int accessorIndex = targetObj.value(QString::fromUtf8(key)).toInt(-1);
							if (accessorIndex < 0)
								return;

							QVector<float> values;
							int count = 0;
							if (!readFloatAccessorData(accessors, bufferViews, bufferData, accessorIndex, 3, values, &count))
								return;
							if (count != static_cast<int>(expectedVertexCount))
								return;

							destination.reserve(count);
							for (int i = 0; i < count; ++i)
							{
								destination.emplace_back(
									values[i * 3 + 0],
									values[i * 3 + 1],
									values[i * 3 + 2]);
							}
						};

						readVec3Accessor("POSITION", morphTarget.positionDeltas);
						readVec3Accessor("NORMAL", morphTarget.normalDeltas);
						readVec3Accessor("TANGENT", morphTarget.tangentDeltas);
						outTargets.append(std::move(morphTarget));
					}

					if (outDefaultWeights.size() < outTargets.size())
						outDefaultWeights.resize(outTargets.size());
					return !outTargets.isEmpty();
				}
			}
		}

		const QJsonArray gltfChildren = nodeObj.value("children").toArray();
		for (int i = static_cast<int>(gltfChildren.size()) - 1; i >= 0; --i)
			if (i < static_cast<int>(aiNodePtr->mNumChildren))
				stack.append({ aiNodePtr->mChildren[i], gltfChildren[i].toInt() });
	}

	return false;
}
}


bool AssImpModelProgressHandler::Update(float percentage)
{
	emit fileReadProcessed(percentage);
	return !(_cancelFlag && *_cancelFlag);
}

/*  Functions   */
// Constructor, expects a filepath to a 3D model.
AssImpModelLoader::AssImpModelLoader() : QObject(),
	_importer(),
	_scene(nullptr),
	_errorMessage(""),
	_loadingCancelled(false),
	_selectedUVMethod(UVMethod::None),
	_autoScale(true),
	_autoOrient(true)
{
	_loadingCancelled = false;
	_progHandler = new AssImpModelProgressHandler();
	_progHandler->setCancelFlag(&_loadingCancelled);
	_importer.SetProgressHandler(_progHandler);
	connect(_progHandler, SIGNAL(fileReadProcessed(float)), this, SLOT(processFileReadProgress(float)));

	_autoOrient = QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName())
		.value("assimpAutoOrientCheckBox", true).toBool();
}

AssImpModelLoader::~AssImpModelLoader()
{
	disconnect(_progHandler, SIGNAL(fileReadProcessed(float)), this, SLOT(processFileReadProgress(float)));
	//delete _progHandler; // causes crash
	_progHandler = nullptr;
}

void AssImpModelLoader::setImageTextureUploader(MaterialProcessor::ImageTextureUploadFn uploader)
{
	_materialProcessor.setImageTextureUploader(std::move(uploader));
}

void AssImpModelLoader::setKtx2TextureUploader(MaterialProcessor::Ktx2TextureUploadFn uploader)
{
	_materialProcessor.setKtx2TextureUploader(std::move(uploader));
}

void AssImpModelLoader::processFileReadProgress(float percentage)
{
	emit fileReadProcessed(percentage);
}

void AssImpModelLoader::cancelLoading()
{
	_loadingCancelled = true;
}

AssImpMeshDataBatch AssImpModelLoader::getMeshes() const
{
	return _meshes;
}

/*  Functions   */
// Loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
void AssImpModelLoader::loadModel(string path, const bool& progressiveLoading)
{	
	_progressiveLoading = progressiveLoading;
	_loadingCancelled = false;
	_errorMessage.clear();
	_path = std::string(path);
	_meshes.clear();	
	_totalNodeCount = 0;
	_processedNodeCount = 0;
	_processedMeshCount = 0;

	_materialProcessor.clearGLBCaches();
	_materialProcessor.clearLoadedTextures();

	_importer.FreeScene(); // Free any previously loaded scene
	_scene = nullptr; // Reset scene pointer
		
	QFileInfo fi(path.c_str());

#ifdef __DEBUG__
	std::cout << "\n--------------------------------------------------" << std::endl;
	std::cout << "Starting to load model: " << path.c_str() << std::endl;
	std::cout << "File size: " << fi.size() / 1024.0f << " KB" << std::endl;
#endif

	// Check if the file is a supported XCAF document type
	std::unique_ptr<IXCAFDocProcessor> processor = XCAFDocProcessorFactory::createProcessor(fi.suffix().toLower().toStdString());
	if (processor)
	{
		_scene = processor->processFile(path);
	}
	else // all Assimp models
	{
		// Read file via ASSIMP
		// For glTF/GLB: normals come from the file spec; don't auto-generate smooth normals
		// so that positions-only meshes keep zero normals and the shader can derive
		// flat face normals via screen-space derivatives (dFdx/dFdy).
		QString qPathCheck = QString::fromStdString(path);
		bool isGltfFile = qPathCheck.endsWith(".gltf", Qt::CaseInsensitive) ||
		                  qPathCheck.endsWith(".glb",  Qt::CaseInsensitive);

		QSettings importSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		const bool wantGenNormals       = importSettings.value("assimpGenNormalsCheckBox", true).toBool();
		const bool wantSmoothNormals    = importSettings.value("assimpSmoothNormalsCheckBox", true).toBool();
		const bool wantCalcTangents     = importSettings.value("assimpCalcTangentsCheckBox", true).toBool();
		const bool wantOptimizeMesh     = importSettings.value("assimpOptimizeMeshCheckBox", true).toBool();
		const bool wantRemoveDuplicates = importSettings.value("assimpRemoveDuplicatesCheckBox", true).toBool();

		unsigned int importFlags = aiProcess_ImproveCacheLocality |
			aiProcess_Triangulate |
			aiProcess_GenUVCoords |
			aiProcess_SortByPType;

		if (wantCalcTangents)
			importFlags |= aiProcess_CalcTangentSpace;
		if (wantOptimizeMesh)
			importFlags |= aiProcess_OptimizeMeshes;
		if (wantRemoveDuplicates)
			importFlags |= aiProcess_JoinIdenticalVertices;

		if (!isGltfFile)
		{
			// glTF models have correct normals by design; FixInfacingNormals would flip
			// inward-facing normals on interior geometry (e.g. sky domes) and break them.
			importFlags |= aiProcess_FixInfacingNormals;
			if (wantGenNormals)
			{
				if (wantSmoothNormals)
				{
					_importer.SetPropertyFloat("PP_GSN_MAX_SMOOTHING_ANGLE", 15);
					importFlags |= aiProcess_GenSmoothNormals;
				}
				else
				{
					importFlags |= aiProcess_GenNormals;
				}
			}
		}

		_scene = _importer.ReadFile(path, importFlags);
	}

	// Check for errors
	if (!_scene || _scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE || !_scene->mRootNode) // if is Not Zero
	{
		if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
		{
			_loadingCancelled = true;
			_errorMessage = "Model loading cancelled by user.";
			emit loadingCancelled();
			emit loadingFinished(false, nullptr);
			return;
		}

		_errorMessage = _importer.GetErrorString();
		cout << "ERROR::ASSIMP:: " << _importer.GetErrorString() << endl;
		emit loadingFinished(false, nullptr);
		return;
	}

	// If cancellation arrived during a long importer read, stop before any
	// post-read work (UV prompts, scene analysis, traversal) continues.
	if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
	{
		_loadingCancelled = true;
		_errorMessage = "Model loading cancelled by user.";
		emit loadingCancelled();
		emit loadingFinished(false, nullptr);
		return;
	}

	// Route every imported aiScene through runtime node/world transforms. Formats
	// with authored aiNode transforms are positioned by model matrices, while
	// identity-transform scenes continue to render from their vertex data as-is.
	QString qPath = QString::fromStdString(path);
	const bool isGltfLike = qPath.endsWith(".gltf", Qt::CaseInsensitive) ||
		qPath.endsWith(".glb", Qt::CaseInsensitive);
	_sceneUpAxis = detectSceneUpAxis(_scene, path);
	emit sceneUpAxisDetected(_sceneUpAxis, _autoOrient);

	// Capture original glTF material indices before deduplication so import,
	// variants, and export continue to share the authored material numbering.
	_meshIndexToOriginalMaterialIndex.clear();
	_aiMatToGltfMat.clear();
	if (isGltfLike)
	{
		// Save the original material indices before they get remapped
		for (unsigned int i = 0; i < _scene->mNumMeshes; ++i)
		{
			_meshIndexToOriginalMaterialIndex[i] = _scene->mMeshes[i]->mMaterialIndex;
		}

		// Update aiScene materials to match glTF structure (deduplicate, fix references)
		// This must happen BEFORE processing nodes so meshes get correct material assignments
		updateAiSceneWithGltfMaterials(qPath, const_cast<aiScene*>(_scene));

		// Parse primitive modes for rendering
		parseGltfPrimitiveModes(qPath);

		// Parse KHR_materials_variants (after material dedup so indices are stable)
		_variantData = GltfVariantData();
		parseGltfVariants(qPath);
		_animationData = GltfAnimationData();
		parseSceneAnimations();
		// parseSceneCameras() is deferred until after the import-time scale pass
		// so that findNodeWorldTransform() sees the same scaled root-node matrix
		// that the runtime meshes use.
		_cameraData = GltfCameraData();
	}
	else
	{
		_animationData = GltfAnimationData();
		_cameraData    = GltfCameraData();
	}

	_sceneStats = collectSceneMeshInfo(_scene);
	_totalNodeCount = countNodes(_scene->mRootNode);

	if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
	{
		_loadingCancelled = true;
		_errorMessage = "Model loading cancelled by user.";
		emit loadingCancelled();
		emit loadingFinished(false, nullptr);
		return;
	}

	// Apply any import-time scale normalization. Orientation compensation now
	// lives in camera space rather than mutating the imported scene graph.
	applyCoordinateSystemTransformations(path);

	// Parse cameras AFTER the scale correction so that findNodeWorldTransform()
	// accumulates the same scaled root-node matrix that mesh vertices occupy.
	parseSceneCameras();

	if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
	{
		_loadingCancelled = true;
		_errorMessage = "Model loading cancelled by user.";
		emit loadingCancelled();
		emit loadingFinished(false, nullptr);
		return;
	}

	bool modelHasMissingUVs = false;
	for (unsigned int i = 0; i < _scene->mNumMeshes; ++i)
	{
		if (_scene->mMeshes[i]->mTextureCoords[0] == nullptr)
		{
			modelHasMissingUVs = true;
			break;
		}
	}
	
	if (modelHasMissingUVs)
	{								
		if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
		{
			_loadingCancelled = true;
			_errorMessage = "Model loading cancelled by user.";
			emit loadingCancelled();
			emit loadingFinished(false, nullptr);
			return;
		}

		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());				
		bool remember = settings.value("RememberUVMethod", false).toBool();
		if (_sceneStats.totalTriangles > 100000 &&
			(_selectedUVMethod == UVMethod::AngleBasedSmartUV || _selectedUVMethod == UVMethod::SmartProject) &&
			remember)
		{
			if (_uvDecisionCallback)
			{
				_selectedUVMethod = _uvDecisionCallback(_sceneStats.totalTriangles, _selectedUVMethod);
			}
			else
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle(tr("Performance Warning!"));
				msgBox.setText(tr("The model contains more than 100000 triangles and the current method of UV generation is \"Smart UV\" which is time consuming.\nDo you want to continue generating the UV?"));
				msgBox.setIcon(QMessageBox::Question);

				// Add custom buttons
				QPushButton* yesButton = msgBox.addButton(QMessageBox::Yes);
				QPushButton* noButton = msgBox.addButton(QMessageBox::No);
				QPushButton* changeSettingsButton = msgBox.addButton(tr("Change Settings"), QMessageBox::ActionRole);

				// Set default button
				msgBox.setDefaultButton(QMessageBox::Yes);

				// Execute and check result
				msgBox.exec();

				if (msgBox.clickedButton() == noButton)
				{				
					qDebug() << "User chose not to generate UVs, using None method.";
					_selectedUVMethod = UVMethod::None;
				}
				else if (msgBox.clickedButton() == changeSettingsButton)
				{				
					_selectedUVMethod = ModelViewer::askUserForUVMethod(qApp->activeWindow()).method;
				}
			}
		}			
	}
	else
	{		
		_selectedUVMethod = UVMethod::None; // No UVs needed, reset to None
	}

	// Retrieve the directory path of the filepath
	this->_texturePath = path.substr(0, path.find_last_of('/'));

	// Set batch size based on number of meshes;
	int batchSize = std::clamp(_sceneStats.meshCount / 10, 5, 100);
	_batchSize = batchSize;

	if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
	{
		_loadingCancelled = true;
		_errorMessage = "Model loading cancelled by user.";
		emit loadingCancelled();
		emit loadingFinished(false, nullptr);
		return;
	}

	// Process ASSIMP's root node recursively	
	this->processNode(0, _scene->mRootNode, _scene, aiMatrix4x4());

	if (_loadingCancelled || MainWindow::isFileLoadCancelRequested())
	{
		_loadingCancelled = true;
		_errorMessage = "Model loading cancelled by user.";
		emit loadingFinished(false, _scene);
		return;
	}
	
	// Flush any remaining meshes in batch
	if (!_currentBatch.empty())
	{
		emit meshBatchReady(std::move(_currentBatch));
		_currentBatch.clear();
	}

	// === Parse KHR_lights_punctual extension ===
	GltfLightData parsedLights;
	QString gltfPath = QString::fromStdString(path);

	if (gltfPath.endsWith(".gltf", Qt::CaseInsensitive) || gltfPath.endsWith(".glb", Qt::CaseInsensitive))
	{
		parsedLights = _materialProcessor.parseKHRLightsPunctual(gltfPath);
		if (!parsedLights.isEmpty())
		{
			qDebug() << "AssImpModelLoader: Loaded" << parsedLights.lights.size() << "KHR lights with transforms";
		}
	}

	// Apply the import-time scale correction to each light so it stays
	// consistent with the scaled geometry. Orientation compensation is now
	// handled in camera space, so punctual lights remain in authored space.
	if (!parsedLights.isEmpty() && _autoScale)
	{
		for (auto& entry : parsedLights.lights)
		{
			GPULight& light = entry.gpuLight;

			// Transform position (with translation)
			glm::vec4 transformedPos = _appliedTransform * glm::vec4(light.position, 1.0f);
			light.position = glm::vec3(transformedPos);

			// Transform direction (no translation)
			glm::vec4 transformedDir = _appliedTransform * glm::vec4(light.direction, 0.0f);
			light.direction = glm::normalize(glm::vec3(transformedDir));

			// Scale range by the uniform scale factor
			glm::vec3 scale(
				glm::length(glm::vec3(_appliedTransform[0])),
				glm::length(glm::vec3(_appliedTransform[1])),
				glm::length(glm::vec3(_appliedTransform[2]))
			);
			float avgScale = (scale.x + scale.y + scale.z) / 3.0f;
			light.range *= avgScale;
		}
	}

	// Emit lights for ViewportWidget to handle
	emit lightsLoaded(parsedLights);
	emit loadingFinished(true, _scene);
}


// Processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
void AssImpModelLoader::processNode(int nodeCounter, aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform)
{
	if (_loadingCancelled)
	{
		emit loadingCancelled();
		return;
	}

	// Compute global transformation matrix for the current node
	aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;
	++_processedNodeCount;

	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];		
		AssImpMeshData myMesh = processMesh(mesh, scene, node->mMeshes[i], scene->mNumMeshes, globalTransform, node->mName.C_Str());

		_meshes.push_back(myMesh);            // full mesh store
		++_processedMeshCount;
		emit nodeMeshProgressUpdated(
			_processedNodeCount,
			_totalNodeCount,
			_processedMeshCount,
			_sceneStats.meshCount,
			_needsUVGeneration && _selectedUVMethod != UVMethod::None);


		if (_progressiveLoading)
		{
			_currentBatch.push_back(myMesh);      // batch collection
			if (_currentBatch.size() >= _batchSize)
			{
				emit meshBatchReady(std::move(_currentBatch));
				_currentBatch.clear();
			}
		}
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		if (_loadingCancelled)
		{
			emit loadingCancelled();
			return;
		}

		++nodeCounter;
		processNode(nodeCounter, node->mChildren[i], scene, globalTransform);
	}

	const bool uvProcessed = _needsUVGeneration && _selectedUVMethod != UVMethod::None;
	emit nodeMeshProgressUpdated(_processedNodeCount, _totalNodeCount, _processedMeshCount, _sceneStats.meshCount, uvProcessed);
}

int AssImpModelLoader::countNodes(const aiNode* node) const
{
	if (!node)
		return 0;

	int count = 1;
	for (unsigned int i = 0; i < node->mNumChildren; ++i)
	{
		count += countNodes(node->mChildren[i]);
	}

	return count;
}


AssImpMeshData AssImpModelLoader::processMesh(aiMesh* mesh, const aiScene* scene, const int& meshIndex, const int& totalMeshes, const aiMatrix4x4& transform, const char* nodeName)
{
	// Data to fill
	vector<Vertex> vertices;
	vector<unsigned int> indices;
	vector<Material::Texture> textures;
	
	_needsUVGeneration = false;

	bool isNonTrianglePrimitive = false;
	if (_gltfMeshPrimitiveModes.find(meshIndex) != _gltfMeshPrimitiveModes.end())
	{
		GLenum mode = _gltfMeshPrimitiveModes[meshIndex];
		isNonTrianglePrimitive = (mode != GL_TRIANGLES &&
			mode != GL_TRIANGLE_STRIP &&
			mode != GL_TRIANGLE_FAN);
	}

	// Walk through each of the mesh's vertices
	int step = 0;
	unsigned int nbVertices = mesh->mNumVertices;

	// Check if we need to generate normals
	bool hasNormals = mesh->mNormals != nullptr;
	bool canGenerateNormals = HasSurfaceGeometry(mesh);
	std::vector<glm::vec3> generatedNormals;

	// Positions-only meshes have no normals and no UV coordinates.
	// Keep their normals at zero so the fragment shader can derive flat face
	// normals via dFdx/dFdy derivatives, giving correct flat shading.
	bool isPositionsOnly = !hasNormals && !mesh->HasTextureCoords(0);

	if (!hasNormals && canGenerateNormals && !isPositionsOnly)
	{
		GenerateFaceNormals(mesh, generatedNormals);
		printf("Generated normals for mesh with %u vertices and %u faces\n",
			mesh->mNumVertices, mesh->mNumFaces);
	}

	bool hasNegativeScale = false;
	aiMatrix3x3 normalMatrix = aiMatrix3x3(transform);
	normalMatrix = normalMatrix.Inverse().Transpose();
	for (unsigned int i = 0; i < nbVertices; i++)
	{
		step++;
		Vertex vertex{};
		assignFallbackTexCoords(vertex);

		// Detect negative scale by computing determinant of the 3x3 transform
		glm::mat3 glmTransform = glm::mat3(
			transform.a1, transform.a2, transform.a3,
			transform.b1, transform.b2, transform.b3,
			transform.c1, transform.c2, transform.c3
		);
		float determinant = glm::determinant(glmTransform);
		hasNegativeScale = determinant < 0.0f;

		aiVector3D pos = mesh->mVertices[i];
		vertex.Position = glm::vec3(pos.x, pos.y, pos.z);

		if (hasNormals)
		{
			aiVector3D normal = mesh->mNormals[i];
			vertex.Normal = glm::vec3(normal.x, normal.y, normal.z);
		}
		else if (!generatedNormals.empty())
		{
			glm::vec3 normal = generatedNormals[i];
			vertex.Normal = normal;
		}
		else
		{
			if (isNonTrianglePrimitive || isPositionsOnly)
			{
				// Points/lines and positions-only triangles: zero normal so the
				// fragment shader computes flat face normals via dFdx/dFdy.
				vertex.Normal = glm::vec3(0.0f, 0.0f, 0.0f);
			}
			else
			{
				vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
			}
		}

		// Extract up to four UV sets used by the runtime vertex layout.
		bool hasAnyTexCoords = false;
		for (int texCoordSet = 0; texCoordSet < 4; texCoordSet++)
		{
			if (mesh->mTextureCoords[texCoordSet])
			{
				glm::vec2 vec;
				vec.x = mesh->mTextureCoords[texCoordSet][i].x;
				vec.y = mesh->mTextureCoords[texCoordSet][i].y;
				vertex.TexCoords[texCoordSet] = vec;
				hasAnyTexCoords = true;
			}
			else
			{
				vertex.TexCoords[texCoordSet] = glm::vec2(0.0f);
			}
		}

		if (hasAnyTexCoords)
		{
			assignFallbackTangentBasis(vertex);

			if (mesh->mTangents)
			{
				aiVector3D tangent = mesh->mTangents[i];
				vertex.Tangent = glm::vec3(tangent.x, tangent.y, tangent.z);
			}

			if (mesh->mBitangents)
			{
				aiVector3D bitangent = mesh->mBitangents[i];
				vertex.Bitangent = glm::vec3(bitangent.x, bitangent.y, bitangent.z);
			}
		}
		else
		{
			// Keep a deterministic fallback UV/tangent basis so later texture
			// application samples a constant texel instead of reading garbage.
			assignFallbackTangentBasis(vertex);
			_needsUVGeneration = true;
		}

		// Vertex Color
		if (mesh->HasVertexColors(0))
		{
			aiColor4D color = mesh->mColors[0][i];
			vertex.Color = glm::vec4(color.r, color.g, color.b, color.a);
		}
		else
		{
			vertex.Color = glm::vec4(1.0f);
		}

		vertices.push_back(vertex);

		if (i % 100000 == 0)
		{
			emit verticesProcessed(static_cast<float>(i) / nbVertices * 100.0f);
		}
	}

	QVector<GltfSkinJoint> skinJoints;
	if (mesh->HasBones())
	{
		skinJoints.reserve(mesh->mNumBones);
		for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			const aiBone* bone = mesh->mBones[boneIndex];
			if (!bone)
				continue;

			GltfSkinJoint joint;
			joint.nodeName = QString::fromUtf8(bone->mName.C_Str());
			joint.inverseBindMatrix = bone->mOffsetMatrix;
			skinJoints.append(joint);

			for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi)
			{
				const aiVertexWeight& vw = bone->mWeights[wi];
				if (vw.mVertexId >= vertices.size())
					continue;

				Vertex& vertex = vertices[vw.mVertexId];
				for (int slot = 0; slot < 4; ++slot)
				{
					if (vertex.JointWeights[slot] <= 0.0f)
					{
						vertex.JointIndices[slot] = static_cast<float>(boneIndex);
						vertex.JointWeights[slot] = vw.mWeight;
						break;
					}
				}
			}
		}

		for (Vertex& vertex : vertices)
		{
			const float totalWeight =
				vertex.JointWeights.x + vertex.JointWeights.y +
				vertex.JointWeights.z + vertex.JointWeights.w;
			if (totalWeight > 0.0001f)
				vertex.JointWeights /= totalWeight;
		}
	}

	QVector<MorphTargetData> morphTargets;
	QVector<float> defaultMorphWeights;
	GltfPrimitiveVertexBasis morphBaseBasis;
	if (mesh->mNumAnimMeshes > 0 && mesh->mAnimMeshes)
	{
		morphTargets.reserve(mesh->mNumAnimMeshes);
		defaultMorphWeights.reserve(mesh->mNumAnimMeshes);
		for (unsigned int morphIndex = 0; morphIndex < mesh->mNumAnimMeshes; ++morphIndex)
		{
			const aiAnimMesh* animMesh = mesh->mAnimMeshes[morphIndex];
			if (!animMesh || animMesh->mNumVertices != mesh->mNumVertices)
				continue;

			MorphTargetData morphTarget;
			defaultMorphWeights.append(animMesh->mWeight);

			if (animMesh->mVertices)
			{
				morphTarget.positionDeltas.reserve(animMesh->mNumVertices);
				for (unsigned int vertexIndex = 0; vertexIndex < animMesh->mNumVertices; ++vertexIndex)
				{
					const aiVector3D& value = animMesh->mVertices[vertexIndex];
					morphTarget.positionDeltas.emplace_back(value.x, value.y, value.z);
				}
			}

			if (animMesh->mNormals)
			{
				morphTarget.normalDeltas.reserve(animMesh->mNumVertices);
				for (unsigned int vertexIndex = 0; vertexIndex < animMesh->mNumVertices; ++vertexIndex)
				{
					const aiVector3D& value = animMesh->mNormals[vertexIndex];
					morphTarget.normalDeltas.emplace_back(value.x, value.y, value.z);
				}
			}

			if (animMesh->mTangents)
			{
				morphTarget.tangentDeltas.reserve(animMesh->mNumVertices);
				for (unsigned int vertexIndex = 0; vertexIndex < animMesh->mNumVertices; ++vertexIndex)
				{
					const aiVector3D& value = animMesh->mTangents[vertexIndex];
					morphTarget.tangentDeltas.emplace_back(value.x, value.y, value.z);
				}
			}

			morphTargets.append(std::move(morphTarget));
		}
	}

	const QString importPath = QString::fromStdString(_path);
	if (importPath.endsWith(".gltf", Qt::CaseInsensitive) || importPath.endsWith(".glb", Qt::CaseInsensitive))
	{
		loadMorphTargetsForAiMesh(
			importPath, scene, meshIndex, mesh->mNumVertices, morphTargets, defaultMorphWeights, &morphBaseBasis,
			&_materialProcessor);
	}

	if (!morphTargets.isEmpty() &&
		morphBaseBasis.positions.size() == vertices.size())
	{
		GltfPrimitiveVertexBasis importedBasis = morphBaseBasis;
		const std::vector<unsigned int> morphRemap = buildMorphVertexRemap(vertices, importedBasis);
		if (!morphRemap.empty())
			reorderMorphTargetsToImportedVertexOrder(morphTargets, morphRemap);
	}

	if (!morphTargets.isEmpty())
	{
		for (MorphTargetData& morphTarget : morphTargets)
		{
			for (glm::vec3& delta : morphTarget.normalDeltas)
			{
				aiVector3D transformed =
					normalMatrix * aiVector3D(delta.x, delta.y, delta.z);
				if (transformed.Length() > 0.0001f)
					transformed.Normalize();
				if (hasNegativeScale)
					transformed = -transformed;
				delta = glm::vec3(transformed.x, transformed.y, transformed.z);
			}

			for (glm::vec3& delta : morphTarget.tangentDeltas)
			{
				aiVector3D transformed =
					normalMatrix * aiVector3D(delta.x, delta.y, delta.z);
				if (transformed.Length() > 0.0001f)
					transformed.Normalize();
				if (hasNegativeScale)
					transformed = -transformed;
				delta = glm::vec3(transformed.x, transformed.y, transformed.z);
			}
		}
	}

	// Walk each face and record its vertex indices.
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		// Retrieve all indices of the face and store them in the indices vector
		for (unsigned int j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	// For positions-only triangle meshes, expand to non-indexed triangles with per-face
	// flat normals. This is more robust than screen-space dFdx/dFdy derivatives which
	// produce incorrect smooth normals when the mesh is small on screen (zoomed out).
	if (!isNonTrianglePrimitive && isPositionsOnly && !vertices.empty() && !indices.empty())
	{
		std::vector<Vertex> flatVertices;
		std::vector<unsigned int> flatIndices;
		flatVertices.reserve(indices.size());
		flatIndices.reserve(indices.size());

		for (size_t idx = 0; idx + 2 < indices.size(); idx += 3)
		{
			Vertex v0 = vertices[indices[idx]];
			Vertex v1 = vertices[indices[idx + 1]];
			Vertex v2 = vertices[indices[idx + 2]];

			glm::vec3 edge1 = v1.Position - v0.Position;
			glm::vec3 edge2 = v2.Position - v0.Position;
			glm::vec3 faceNormal = glm::cross(edge1, edge2);
			float len = glm::length(faceNormal);
			if (len > 0.0001f)
				faceNormal /= len;
			// degenerate face: leave normal zero — shader dFdx/dFdy fallback handles it

			v0.Normal = faceNormal;
			v1.Normal = faceNormal;
			v2.Normal = faceNormal;

			unsigned int base = static_cast<unsigned int>(flatVertices.size());
			flatVertices.push_back(v0);
			flatVertices.push_back(v1);
			flatVertices.push_back(v2);
			flatIndices.push_back(base);
			flatIndices.push_back(base + 1);
			flatIndices.push_back(base + 2);
		}

		vertices = std::move(flatVertices);
		indices  = std::move(flatIndices);
	}

	if (!morphTargets.isEmpty() && _needsUVGeneration && textures.empty())
	{
		_needsUVGeneration = false;
	}

	// If the mesh has no texture coordinates, we generate them now.
	if (_needsUVGeneration && _selectedUVMethod != UVMethod::None)
	{		
		// Generate UVs for the mesh
		MeshAnalysis::SamplingConfig config;
		config.maxSamples = 200;
		config.sphericalAspectRatio = 0.85f;
		auto analysis = MeshAnalyzer::analyzeMesh(mesh, config);
		generateUVsForMesh(analysis, mesh, vertices, indices);
	}

	// Determine the correct material index for this mesh.
	// For glTF files: use the PRE-DEDUPLICATION index saved before updateAiSceneWithGltfMaterials()
	// This is critical because deduplication remaps mesh->mMaterialIndex
	// For other formats: use the current index (not remapped)
	int originalMaterialIndex = mesh->mMaterialIndex;

	qDebug() << "[IMPORT] processMesh[" << meshIndex << "] nodeName=" << nodeName
	         << "- mesh->mMaterialIndex (raw)=" << mesh->mMaterialIndex;

	// If this is from a glTF file, look up the compact pre-dedup index then convert
	// to the glTF material index using the reindex map built during material loading.
	for (unsigned int meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
	{
		if (scene->mMeshes[meshIdx] == mesh)
		{
			auto savedIt = _meshIndexToOriginalMaterialIndex.find(meshIdx);
			if (savedIt != _meshIndexToOriginalMaterialIndex.end())
			{
				int compactIdx = savedIt->second;
				// Convert compact aiScene index → glTF material index.
				// updateAiSceneWithGltfMaterials() reindexed the array so that
				// scene->mMaterials[i] == glTF material[i]; we need the same space.
				auto gltfIt = _aiMatToGltfMat.find(compactIdx);
				originalMaterialIndex = (gltfIt != _aiMatToGltfMat.end())
					? gltfIt->second
					: compactIdx;
				qDebug() << "  [IMPORT-glTF] compact" << compactIdx
				         << "-> glTF materialIndex" << originalMaterialIndex
				         << "for aiMesh[" << meshIdx << "]";
			}
			break;
		}
	}

	// Process materials
	Material mat;
	_materialProcessor.setDefaultMaterial(mat);
	//if (mesh->mMaterialIndex != 0)

	// DEBUG: Log the material index assignment
	qDebug() << "[IMPORT] processMesh[" << meshIndex << "] FINAL"
	         << "nodeName=" << nodeName
	         << "mNumVertices=" << mesh->mNumVertices
	         << "mNumFaces=" << mesh->mNumFaces
	         << "originalMaterialIndex=" << originalMaterialIndex
	         << "scene->mNumMaterials=" << scene->mNumMaterials;

	// CRITICAL FIX: Use originalMaterialIndex to load material properties, not mesh->mMaterialIndex
	// When Assimp remaps materials, mesh->mMaterialIndex may point to wrong material slot.
	// We must use the pre-dedup originalMaterialIndex to access the correct material properties.
	int materialIndexToUse = originalMaterialIndex;
	if (materialIndexToUse < scene->mNumMaterials)
	{
		aiMaterial* material = scene->mMaterials[materialIndexToUse];
		// We assume a convention for sampler names in the shaders. Each diffuse texture should be named
		// as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER.
		// Same applies to other texture as the following list summarizes:
		// Diffuse: texture_diffuseN
		// Specular: texture_specularN
		// Normal: texture_normalN

		_materialProcessor.setFolderPath(this->_texturePath);

		// Determine file type
		bool isGlb = (_path.find(".glb") != std::string::npos);
		bool isGltf = (_path.find(".gltf") != std::string::npos && !isGlb);  // exclude .glb

		if (isGltf || isGlb)
		{
			_materialProcessor.processGltf2CoreAndExtensions(
				QString::fromStdString(_path),
				scene,
				QString::fromUtf8(nodeName),
				mesh,
				originalMaterialIndex,
				mat,
				textures);
			
			// Ensure scene textures are valid for deep copy / merging
			_materialProcessor.ensureAssimpSceneTexturesValid(
				const_cast<aiScene*>(scene),
				QString::fromStdString(_path));
			
			mat.setIsGLTFMaterial(true);
			qDebug() << "GLTF Material Loaded";
		}
		else
		{
			//Set color and material
			_materialProcessor.processAssimpColorAndMaterial(material, mat);
			// ADS and PBR Maps from Assimp
			_materialProcessor.processAssimpTextureMaps(material, textures, mat);
		}
	}

	// Return a mesh object created from the extracted mesh data
	QString meshName = QString::fromStdString(mesh->mName.C_Str());
	if(meshName.isEmpty())
	{
		meshName = QFileInfo(QString(_path.data())).baseName() + " (Unnamed Mesh)";
	}
	else
	{
		meshName = QFileInfo(QString(_path.data())).baseName() + " (" + mesh->mName.C_Str() + ")";
	}

	// Material and textures details
	qDebug() << "Mesh with material: " << meshName << " processed.";

	AssImpMeshData meshData;
	meshData.name = meshName;
	meshData.vertices = std::move(vertices);
	meshData.indices = std::move(indices);
	meshData.textures = std::move(textures);
	meshData.material = mat;
	meshData.hasNegativeScale = hasNegativeScale;
	meshData.sceneIndex = meshIndex;
	meshData.originalMaterialIndex = originalMaterialIndex;
	meshData.sourceFile = QString::fromStdString(_path);
	meshData.sourceNodeName = QString::fromUtf8(nodeName);
	meshData.preserveNodeTransform = true;
	meshData.nodeWorldTransform = aiToQMatrix4x4(transform);
	meshData.skinJoints = skinJoints;
	meshData.morphTargets = morphTargets;
	meshData.defaultMorphWeights = defaultMorphWeights;

	qDebug() << "[IMPORT-STORED] MeshData for" << meshName
	         << "sceneIndex=" << meshIndex
	         << "originalMaterialIndex=" << originalMaterialIndex
	         << "materialName=" << mat.name();

	auto derivePrimitiveModeFromAssimp = [&](unsigned int primitiveTypes) -> GLenum
	{
		if (primitiveTypes & aiPrimitiveType_POINT)
			return GL_POINTS;
		if (primitiveTypes & aiPrimitiveType_LINE)
			return GL_LINES;
		if (primitiveTypes & aiPrimitiveType_TRIANGLE)
			return GL_TRIANGLES;
		if (primitiveTypes & aiPrimitiveType_POLYGON)
			return GL_TRIANGLES;
		return GL_TRIANGLES;
	};

	meshData.primitiveMode = derivePrimitiveModeFromAssimp(mesh->mPrimitiveTypes);

	if (_gltfMeshPrimitiveModes.find(meshIndex) != _gltfMeshPrimitiveModes.end())
	{
		meshData.primitiveMode = _gltfMeshPrimitiveModes[meshIndex];
		qDebug() << "Set primitive mode for mesh" << meshIndex << "to" << meshData.primitiveMode;
	}

	// -----------------------------------------------------------------------
	// KHR_materials_variants: pre-build Material for every variant material
	// referenced by this primitive.  We do this here, while the scene + JSON
	// caches are warm, so variant switching at runtime requires no I/O.
	// -----------------------------------------------------------------------
	if (!_variantData.isEmpty() && _variantData.meshVariantMappings.contains(meshIndex))
	{
		const QString qPath = QString::fromStdString(_path);
		const bool isGltf   = qPath.endsWith(".gltf", Qt::CaseInsensitive);
		const bool isGlb    = qPath.endsWith(".glb",  Qt::CaseInsensitive);

		meshData.variantMappings = _variantData.meshVariantMappings[meshIndex];

		// Collect every unique material index this primitive can ever use.
		QSet<int> matIndices;
		matIndices.insert(originalMaterialIndex);  // the current default
		for (const GltfVariantMapping& vm : std::as_const(meshData.variantMappings))
			matIndices.insert(vm.materialIndex);

		for (int matIdx : std::as_const(matIndices))
		{
			if (matIdx < 0 || matIdx >= static_cast<int>(scene->mNumMaterials))
				continue;

			Material varMat;
			std::vector<Material::Texture> varTextures;
			_materialProcessor.processAssimpColorAndMaterial(scene->mMaterials[matIdx], varMat);

			if (isGltf || isGlb)
			{
				_materialProcessor.processGltf2CoreAndExtensions(
					qPath, scene,
					QString::fromUtf8(nodeName),
					nullptr,     // no specific aiMesh — materialIndex is authoritative
					matIdx,
					varMat, varTextures);
				// glTF/GLB variant materials must have isGLTFMaterial=true so
				// the shader uses PBR blending (blendFactor=1) and texture
				// scaling works correctly for metallic, roughness, AO, and
				// specular — identical to the default mesh material path at
				// line 777.
				varMat.setIsGLTFMaterial(true);

			}
			else
			{
				_materialProcessor.processAssimpTextureMaps(
					scene->mMaterials[matIdx], varTextures, varMat);
			}

			meshData.allVariantMaterials[matIdx] = varMat;
		}

		qDebug() << "[VARIANTS] Mesh" << meshName << "has" << meshData.variantMappings.size()
		         << "variant mappings," << meshData.allVariantMaterials.size() << "prebuilt materials";
	}

	// Attach precomputed B-Rep edges if this mesh was produced by BRepToAssimpConverter
	// (STEP/IGES/BREP). For OBJ/glTF the lookup returns nullptr and the fields stay empty.
	if (const auto* occData = BRepToAssimpConverter::getPrecomputedEdges(mesh))
	{
		meshData.precomputedOccEdges           = occData->segments;
		meshData.precomputedOccEdgeBoundaries  = occData->bounds;
		meshData.precomputedOccEdgeVertexTolerance = occData->vertexTolerance;

		meshData.precomputedOccEdgeCircles.reserve(occData->circles.size());
		for (const BRepToAssimpConverter::OccEdgeCircle& c : occData->circles)
		{
			AssImpMeshData::PrecomputedEdgeCircle pc;
			pc.isCircle = c.isCircle;
			pc.centerX = c.centerX; pc.centerY = c.centerY; pc.centerZ = c.centerZ;
			pc.axisX = c.axisX;     pc.axisY = c.axisY;     pc.axisZ = c.axisZ;
			pc.radius = c.radius;
			meshData.precomputedOccEdgeCircles.push_back(pc);
		}
	}

	// Attach precomputed B-Rep per-face axis data (Cylindrical/Conical
	// Diameter measurement tool) - same nullptr-for-non-OCC convention.
	if (const auto* occFaces = BRepToAssimpConverter::getPrecomputedFaces(mesh))
	{
		meshData.precomputedOccFaceTriangleBounds = occFaces->bounds;

		meshData.precomputedOccFaceAxes.reserve(occFaces->axes.size());
		for (const BRepToAssimpConverter::OccFaceAxis& a : occFaces->axes)
		{
			AssImpMeshData::PrecomputedFaceAxis pa;
			pa.isCylinder = a.isCylinder;
			pa.isCone     = a.isCone;
			pa.originX = a.originX; pa.originY = a.originY; pa.originZ = a.originZ;
			pa.axisX = a.axisX;     pa.axisY = a.axisY;     pa.axisZ = a.axisZ;
			meshData.precomputedOccFaceAxes.push_back(pa);
		}
	}

	return meshData;
}

void AssImpModelLoader::generateUVsForMesh(MeshAnalysis::AnalysisResult& analysis, aiMesh* mesh, std::vector<Vertex>& vertices, std::vector<std::seed_seq::result_type>& indices)
{
	// Choose UV config based on surface type
	UVConfig uvconfig;

	switch (analysis.surfaceType)
	{
	case MeshAnalysis::SurfaceType::SPHERICAL:
		uvconfig.sphericalScale = 1.0f;
		uvconfig.seamlessSpherical = true;
		break;

	case MeshAnalysis::SurfaceType::CYLINDRICAL:
		uvconfig.cylindricalScale = 1.0f;
		uvconfig.cylindricalOffset = 0.0f;
		break;

	case MeshAnalysis::SurfaceType::PLANAR:
		uvconfig.planarScale = glm::vec2(1.0f);
		break;

	case MeshAnalysis::SurfaceType::MIXED:
		break;
	}

	uvconfig.angleThreshold = 66.0f; // Similar to Blender's default
	uvconfig.enableRelaxation = true;

	// Generate UVs and tangents
	switch (_selectedUVMethod)
	{
	case UVMethod::Planar:
		UVGenerator::generatePlanar(vertices, indices, uvconfig);
		break;
	case UVMethod::Cylindrical:
		UVGenerator::generateCylindrical(vertices, indices, uvconfig);
		break;
	case UVMethod::Spherical:
		UVGenerator::generateSpherical(vertices, indices, uvconfig);
		break;
	case UVMethod::AngleBased:
		UVGenerator::generateAngleBased(vertices, indices, uvconfig);
		break;
	case UVMethod::Hybrid:
		UVGenerator::generateHybrid(vertices, indices);
		break;
	case UVMethod::AngleBasedSmartUV:
		UVGenerator::generateAngleBasedSmartUV(vertices, indices, uvconfig);
		break;
	case UVMethod::SmartProject:
		UVGenerator::generateSmartProject(vertices, indices, uvconfig);
		break;
	case UVMethod::ARAP:
		// Backfilled: this switch previously had no case for ARAP at all (fell through to
		// default and silently skipped UV generation) - a pre-existing gap unrelated to Torus,
		// fixed here while adding the case immediately below since it's the same switch.
		UVGenerator::generateARAP(vertices, indices, uvconfig);
		break;
	case UVMethod::Torus:
		UVGenerator::generateTorus(vertices, indices, uvconfig);
		break;
	case UVMethod::None: // fall through
	default:
		break; // skip UV generation
	}

	// MikkTSpace tangents
	TangentGenerator::generateMikkTSpaceTangentsForMesh(vertices, indices);
}

bool AssImpModelLoader::HasSurfaceGeometry(aiMesh* mesh)
{
	if (!mesh || mesh->mNumFaces == 0)
	{
		return false;
	}

	// Check if mesh has triangular faces
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		if (mesh->mFaces[i].mNumIndices >= 3)
		{
			return true;
		}
	}
	return false;
}

void AssImpModelLoader::GenerateFaceNormals(aiMesh* mesh, std::vector<glm::vec3>& generatedNormals)
{
	generatedNormals.clear();
	generatedNormals.resize(mesh->mNumVertices, glm::vec3(0.0f));

	// Only generate normals for meshes with triangular faces
	if (mesh->mNumFaces == 0)
	{
		// No faces - fill with default up normals
		std::fill(generatedNormals.begin(), generatedNormals.end(), glm::vec3(0.0f, 1.0f, 0.0f));
		return;
	}

	// Calculate normals from faces
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		const aiFace& face = mesh->mFaces[i];

		if (face.mNumIndices >= 3)
		{ 
			// Only process triangles/polygons
			// Get three vertices of the face
			aiVector3D v0 = mesh->mVertices[face.mIndices[0]];
			aiVector3D v1 = mesh->mVertices[face.mIndices[1]];
			aiVector3D v2 = mesh->mVertices[face.mIndices[2]];

			// Calculate face normal using cross product
			aiVector3D edge1 = v1 - v0;
			aiVector3D edge2 = v2 - v0;
			aiVector3D faceNormal = edge1 ^ edge2; // Cross product in Assimp

			// Check if normal is valid (non-zero length)
			float length = faceNormal.Length();
			if (length > 0.0001f)
			{
				faceNormal.Normalize();

				// Add this face normal to all vertices of the face (for smooth shading)
				for (unsigned int j = 0; j < face.mNumIndices; j++)
				{
					unsigned int vertexIndex = face.mIndices[j];
					if (vertexIndex < generatedNormals.size())
					{
						generatedNormals[vertexIndex] += glm::vec3(faceNormal.x, faceNormal.y, faceNormal.z);
					}
				}
			}
		}
	}

	// Normalize all accumulated normals
	for (auto& normal : generatedNormals)
	{
		float length = glm::length(normal);
		if (length > 0.0001f)
		{
			normal = glm::normalize(normal);
		}
		else
		{
			// Fallback for vertices not part of any valid face
			normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up vector
		}
	}
}


QString AssImpModelLoader::getErrorMessage() const
{
	return _errorMessage;
}

bool AssImpModelLoader::regenerateUVs(SceneMesh* mesh,
	UVMethod method,
	const UVConfig& config,
	const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges)
{
	if (!mesh) return false;

	// Get current mesh data
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	std::vector<unsigned int> sourceVertexMap;
	mesh->getMeshData(vertices, indices);

	// Generate UVs and tangents
	switch (method)
	{
	case UVMethod::Planar:
		UVGenerator::generatePlanar(vertices, indices, config, &sourceVertexMap);
		break;
	case UVMethod::Cylindrical:
		UVGenerator::generateCylindrical(vertices, indices, config, &sourceVertexMap);
		break;
	case UVMethod::Spherical:
		UVGenerator::generateSpherical(vertices, indices, config, &sourceVertexMap);
		break;
	case UVMethod::AngleBased:
		UVGenerator::generateAngleBased(vertices, indices, config, &sourceVertexMap,
			config.useMarkedSeams ? userSeamEdges : nullptr);
		break;
	case UVMethod::Hybrid:
		UVGenerator::generateHybrid(vertices, indices, config, &sourceVertexMap);
		break;
	case UVMethod::AngleBasedSmartUV:
		UVGenerator::generateAngleBasedSmartUV(vertices, indices, config, &sourceVertexMap,
			config.useMarkedSeams ? userSeamEdges : nullptr);
		break;
	case UVMethod::SmartProject:
		UVGenerator::generateSmartProject(vertices, indices, config, &sourceVertexMap);
		break;
	case UVMethod::ARAP:
		UVGenerator::generateARAP(vertices, indices, config, &sourceVertexMap,
			config.useMarkedSeams ? userSeamEdges : nullptr);
		break;
	case UVMethod::Torus:
		UVGenerator::generateTorus(vertices, indices, config, &sourceVertexMap);
		break;
	case UVMethod::None: // fall through
	default:
		break; // skip UV generation
	}

	// MikkTSpace tangents
	TangentGenerator::generateMikkTSpaceTangentsForMesh(vertices, indices);

	// Set data back to mesh (will call setupMesh internally)
	mesh->setMeshData(vertices, indices, &sourceVertexMap);

	return true;
}


void AssImpModelLoader::freeScene()
{
	_importer.FreeScene();
	_scene = nullptr;
}

SceneMeshInfo AssImpModelLoader::collectSceneMeshInfo(const aiScene* scene)
{
	SceneMeshInfo info;
	if (!scene || !scene->HasMeshes() || !scene->mRootNode)
		return info;

	bool firstVertex = true;
	double minX = DBL_MAX, maxX = -DBL_MAX;
	double minY = DBL_MAX, maxY = -DBL_MAX;
	double minZ = DBL_MAX, maxZ = -DBL_MAX;

	float minMeshDimension = std::numeric_limits<float>::max();
	float maxMeshDimension = 0.0f;

	std::unordered_set<unsigned int> processedMeshes;

	std::function<void(const aiNode*, const glm::mat4&)> collectFromNode;
	collectFromNode = [&](const aiNode* node, const glm::mat4& parentTransform) {
		glm::mat4 nodeTransform = parentTransform * SceneUtils::aiMatrixToGlm(node->mTransformation);

		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
		{
			unsigned int meshIndex = node->mMeshes[i];
			if (!processedMeshes.insert(meshIndex).second)
				continue;

			const aiMesh* mesh = scene->mMeshes[meshIndex];
			int numFaces = static_cast<int>(mesh->mNumFaces);
			int numVerts = static_cast<int>(mesh->mNumVertices);

			info.totalVertices += numVerts;
			info.totalTriangles += numFaces;
			info.meshCount++;

			if (numFaces > info.largestMeshTriangles)
			{
				info.largestMeshTriangles = numFaces;
				info.largestMeshName = mesh->mName.C_Str();
			}

			// Track per-mesh bounding box
			double meshMinX = DBL_MAX, meshMaxX = -DBL_MAX;
			double meshMinY = DBL_MAX, meshMaxY = -DBL_MAX;
			double meshMinZ = DBL_MAX, meshMaxZ = -DBL_MAX;

			for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
			{
				glm::vec4 vertex = nodeTransform * glm::vec4(
					mesh->mVertices[j].x,
					mesh->mVertices[j].y,
					mesh->mVertices[j].z,
					1.0f
				);

				double x = static_cast<double>(vertex.x);
				double y = static_cast<double>(vertex.y);
				double z = static_cast<double>(vertex.z);

				// Update scene bounding box
				if (firstVertex)
				{
					minX = maxX = x;
					minY = maxY = y;
					minZ = maxZ = z;
					firstVertex = false;
				}
				else
				{
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
					minZ = std::min(minZ, z);
					maxZ = std::max(maxZ, z);
				}

				// Update mesh bounding box
				meshMinX = std::min(meshMinX, x);
				meshMaxX = std::max(meshMaxX, x);
				meshMinY = std::min(meshMinY, y);
				meshMaxY = std::max(meshMaxY, y);
				meshMinZ = std::min(meshMinZ, z);
				meshMaxZ = std::max(meshMaxZ, z);
			}

			// Calculate this mesh's dimensions
			double meshSizeX = meshMaxX - meshMinX;
			double meshSizeY = meshMaxY - meshMinY;
			double meshSizeZ = meshMaxZ - meshMinZ;
			float meshMaxDim = static_cast<float>(std::max({ meshSizeX, meshSizeY, meshSizeZ }));

			// Track smallest and largest across all meshes
			minMeshDimension = std::min(minMeshDimension, meshMaxDim);
			maxMeshDimension = std::max(maxMeshDimension, meshMaxDim);
		}

		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			collectFromNode(node->mChildren[i], nodeTransform);
		}
		};

	collectFromNode(scene->mRootNode, glm::mat4(1.0f));

	if (!firstVertex)
	{
		info.boundingBox.setLimits(minX, minY, minZ, maxX, maxY, maxZ);
		info.minDimension = minMeshDimension;
		info.maxDimension = maxMeshDimension;
	}
	else
	{
		info.boundingBox.setLimits(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
		info.minDimension = 0.0f;
		info.maxDimension = 0.0f;
	}

	return info;
}

void AssImpModelLoader::applyCoordinateSystemTransformations(const std::string& path)
{
	Q_UNUSED(path);
	_autoOrientWasApplied = false;

	if (_autoScale)
	{
		_appliedTransform = glm::mat4(1.0f);
		_appliedScale = calculateConditionalScale(_sceneStats.minDimension, _sceneStats.maxDimension);
		_appliedTransform = glm::scale(_appliedTransform, glm::vec3(_appliedScale));

		applyTransformToNode(_scene->mRootNode, _appliedTransform);
	}
}

void AssImpModelLoader::applyTransformToNode(aiNode* node, const glm::mat4& transform)
{
	if (!node) return;

	// Convert glm::mat4 to aiMatrix4x4
	aiMatrix4x4 aiTransform = SceneUtils::glmToAiMatrix(transform);
	
	// Apply transformation to the node
	node->mTransformation = aiTransform * node->mTransformation;
}

SceneUpAxis AssImpModelLoader::detectSceneUpAxis(const aiScene* scene, const std::string& filePath) const
{
	bool foundMetadata = false;

	if (scene && scene->mMetaData)
	{
		aiString upAxis;
		int upAxisInt = 0;

		if (scene->mMetaData->Get("UpAxis", upAxis) ||
			scene->mMetaData->Get("up_axis", upAxis) ||
			scene->mMetaData->Get("UP_AXIS", upAxis) ||
			scene->mMetaData->Get("CoordinateSystem", upAxis))
		{
			std::string upStr = upAxis.C_Str();
			std::transform(upStr.begin(), upStr.end(), upStr.begin(), ::tolower);

			if (upStr.find("y") != std::string::npos || upStr == "y_up")
				return SceneUpAxis::YUp;
			if (upStr.find("z") != std::string::npos || upStr == "z_up")
				return SceneUpAxis::ZUp;

			foundMetadata = true;
		}
		else if (scene->mMetaData->Get("UpAxis", upAxisInt) ||
			scene->mMetaData->Get("up_axis", upAxisInt))
		{
			foundMetadata = true;
			switch (upAxisInt)
			{
			case 1:
				return SceneUpAxis::YUp;
			case 2:
				return SceneUpAxis::ZUp;
			default:
				break;
			}
		}
	}

	if (foundMetadata)
		return SceneUpAxis::ZUp;

	std::string extension = filePath.substr(filePath.find_last_of("."));
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
	const glm::mat4 transform = getCoordinateSystemFromFileType(extension);
	return transform == glm::mat4(1.0f) ? SceneUpAxis::ZUp : SceneUpAxis::YUp;
}

glm::mat4 AssImpModelLoader::getCoordinateSystemTransform(const aiScene* scene, const std::string& filePath)
{
	Q_UNUSED(scene);
	Q_UNUSED(filePath);
	return glm::mat4(1.0f);
}

glm::mat4 AssImpModelLoader::getCoordinateSystemFromFileType(const std::string& fileExtension) const
{
	glm::mat4 transform = glm::mat4(1.0f);

	if (fileExtension == ".gltf" || fileExtension == ".glb")
	{
		// GLTF: Always Y-up by specification
		transform = glm::rotate(glm::mat4(1.0f),
			glm::radians(90.0f),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else if (fileExtension == ".obj")
	{
		// OBJ: Usually Y-up (Wavefront OBJ spec)
		transform = glm::rotate(glm::mat4(1.0f),
			glm::radians(90.0f),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else if (fileExtension == ".fbx")
	{
		// FBX: Usually Y-up (but can vary based on export settings)
		transform = glm::rotate(glm::mat4(1.0f),
			glm::radians(90.0f),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else if (fileExtension == ".dae")
	{
		// Collada: Y-up by default
		transform = glm::rotate(glm::mat4(1.0f),
			glm::radians(90.0f),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else if (fileExtension == ".blend")
	{
		// Blender: Y-up
		transform = glm::rotate(glm::mat4(1.0f),
			glm::radians(90.0f),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else if (fileExtension == ".3ds" || fileExtension == ".max")
	{
		// 3ds Max files: Z-up (no conversion needed)
		transform = glm::mat4(1.0f);
	}
	else if (fileExtension == ".x3d")
	{
		// X3D: Y-up by specification
		transform = glm::rotate(glm::mat4(1.0f),
			glm::radians(90.0f),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else if (fileExtension == ".ply")
	{
		// PLY: No standard, but commonly Z-up from scanning
		transform = glm::mat4(1.0f);
	}
	else if (fileExtension == ".stl")
	{
		// STL: No standard, varies by source
		// Default to no conversion, let user override
		transform = glm::mat4(1.0f);
	}
	else
	{
		// Unknown format: assume no conversion needed
		// Log this for debugging
#ifdef DEBUG
		printf("Unknown file format %s, assuming Z-up\n", fileExtension.c_str());
#endif
		transform = glm::mat4(1.0f);
	}

	return transform;
}

float AssImpModelLoader::calculateConditionalScale(const float& minDimension, const float& maxDimension)
{
	if (maxDimension < 1e-6f)
	{
		return 1.0f; // Avoid division by zero
	}

	constexpr float MIN_DIMENSION = 0.01f;
	constexpr float MAX_DIMENSION = 100000.0f;

	// Always scale up if minimum dimension is below threshold
	if (minDimension < MIN_DIMENSION)
	{
		double scale = static_cast<double>(MIN_DIMENSION) / static_cast<double>(minDimension);
		return static_cast<float>(scale);
	}

	// Scale down only if it doesn't push minimum dimension below threshold
	if (maxDimension > MAX_DIMENSION)
	{
		double proposedScale = static_cast<double>(MAX_DIMENSION) / static_cast<double>(maxDimension);
		double scaledMinDimension = static_cast<double>(minDimension) * proposedScale;

		// Only apply scale if minimum dimension stays within bounds
		if (scaledMinDimension >= MIN_DIMENSION)
		{
			return static_cast<float>(proposedScale);
		}
	}

	return 1.0f;
}

void AssImpModelLoader::parseGltfPrimitiveModes(const QString& gltfPath)
{
	_gltfMeshPrimitiveModes.clear();

	bool isGLB = gltfPath.endsWith(".glb", Qt::CaseInsensitive);
	bool isGLTF = gltfPath.endsWith(".gltf", Qt::CaseInsensitive);

	if (!isGLB && !isGLTF)
	{
		qWarning() << "parseGltfPrimitiveModes: Not a glTF file:" << gltfPath;
		return;
	}

	QJsonDocument doc;

	// ===== HANDLE GLB FILES =====
	if (isGLB)
	{
		std::vector<uint8_t> glbBinaryBuffer;
		QString jsonString = MaterialProcessor::extractJsonFromGLB(gltfPath, glbBinaryBuffer);

		if (jsonString.isEmpty())
		{
			qWarning() << "parseGltfPrimitiveModes: Failed to extract JSON from GLB:" << gltfPath;
			return;
		}

		QJsonParseError perr;
		doc = QJsonDocument::fromJson(jsonString.toUtf8(), &perr);
		if (perr.error != QJsonParseError::NoError)
		{
			qWarning() << "parseGltfPrimitiveModes: JSON parse error in GLB:" << perr.errorString();
			return;
		}
	}
	// ===== HANDLE GLTF FILES =====
	else
	{
		QFile file(gltfPath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			qWarning() << "Failed to open glTF file for primitive mode parsing:" << gltfPath;
			return;
		}

		doc = QJsonDocument::fromJson(file.readAll());
		file.close();

		if (!doc.isObject())
		{
			qWarning() << "Invalid glTF JSON structure";
			return;
		}
	}

	// ===== PARSE PRIMITIVE MODES (parallel DFS over aiScene + glTF node trees) =====
	// We walk both trees simultaneously so aiNode->mMeshes[] provides the authoritative
	// aiMesh indices instead of a manual counter. This correctly handles Assimp's
	// primitive merging: when multiple glTF primitives share a material, Assimp merges
	// them into one aiMesh — a counting-only DFS would produce wrong indices thereafter.
	QJsonObject root = doc.object();
	const QJsonArray jsonNodes  = root["nodes"].toArray();
	const QJsonArray jsonMeshes = root["meshes"].toArray();
	const QJsonArray jsonScenes = root["scenes"].toArray();

	int sceneIdx = root.value("scene").toInt(0);
	QJsonArray rootNodeIndices;
	if (sceneIdx >= 0 && sceneIdx < jsonScenes.size())
		rootNodeIndices = jsonScenes[sceneIdx].toObject().value("nodes").toArray();

	if (!_scene || !_scene->mRootNode || rootNodeIndices.isEmpty())
		return;

	struct PrimModeFrame { aiNode* aiNodePtr; int gltfNodeIdx; };
	QVector<PrimModeFrame> stack;
	stack.reserve(jsonNodes.size());

	aiNode* aiSceneRoot = _scene->mRootNode;
	if (rootNodeIndices.size() == 1)
	{
		int rootGltfIdx = rootNodeIndices[0].toInt();
		QString gltfRootName = (rootGltfIdx >= 0 && rootGltfIdx < jsonNodes.size())
		    ? jsonNodes[rootGltfIdx].toObject().value("name").toString() : QString();
		QString aiRootName   = QString::fromUtf8(aiSceneRoot->mName.C_Str());

		if (aiRootName == gltfRootName || aiSceneRoot->mNumMeshes > 0)
			stack.append({aiSceneRoot, rootGltfIdx});            // direct 1:1 match
		else if (aiSceneRoot->mNumMeshes == 0 && aiSceneRoot->mNumChildren == 1)
			stack.append({aiSceneRoot->mChildren[0], rootGltfIdx}); // virtual wrapper
	}
	else
	{
		for (int i = (int)rootNodeIndices.size() - 1; i >= 0; --i)
		{
			if (i < (int)aiSceneRoot->mNumChildren)
				stack.append({aiSceneRoot->mChildren[i], rootNodeIndices[i].toInt()});
		}
	}

	while (!stack.isEmpty())
	{
		const auto frame    = stack.takeLast();
		aiNode*    aiNodePtr = frame.aiNodePtr;
		const int  nodeIdx   = frame.gltfNodeIdx;

		if (!aiNodePtr || nodeIdx < 0 || nodeIdx >= jsonNodes.size())
			continue;

		const QJsonObject nodeObj = jsonNodes[nodeIdx].toObject();

		if (nodeObj.contains("mesh") && aiNodePtr->mNumMeshes > 0)
		{
			const int meshIdx = nodeObj.value("mesh").toInt(-1);
			if (meshIdx >= 0 && meshIdx < jsonMeshes.size())
			{
				const QJsonArray primitives = jsonMeshes[meshIdx].toObject()["primitives"].toArray();
				for (int primIndex = 0; primIndex < primitives.size(); ++primIndex)
				{
					const QJsonObject primObj = primitives[primIndex].toObject();
					const int primMat = primObj.value("material").toInt(0);
					const int mode    = primObj.value("mode").toInt(4);

					GLenum glMode = GL_TRIANGLES;
					switch (mode)
					{
					case 0: glMode = GL_POINTS;         break;
					case 1: glMode = GL_LINES;           break;
					case 2: glMode = GL_LINE_LOOP;       break;
					case 3: glMode = GL_LINE_STRIP;      break;
					case 4: glMode = GL_TRIANGLES;       break;
					case 5: glMode = GL_TRIANGLE_STRIP;  break;
					case 6: glMode = GL_TRIANGLE_FAN;    break;
					default: glMode = GL_TRIANGLES;      break;
					}

					// Prefer positional pairing when the node's aiMesh list mirrors the
					// glTF primitive list. This is common and avoids ambiguous matching
					// for conformance samples where primitives have no material or share
					// the same material index.
					if (primitives.size() == static_cast<int>(aiNodePtr->mNumMeshes) &&
						primIndex < static_cast<int>(aiNodePtr->mNumMeshes))
					{
						const unsigned int candidate = aiNodePtr->mMeshes[primIndex];
						_gltfMeshPrimitiveModes[candidate] = glMode;
						continue;
					}

					// Fallback: match this glTF primitive to the aiMesh in this node by
					// material after updateAiSceneWithGltfMaterials() has aligned both
					// sides to glTF-index space.
					for (unsigned int mi = 0; mi < aiNodePtr->mNumMeshes; ++mi)
					{
						unsigned int candidate = aiNodePtr->mMeshes[mi];
						if ((int)_scene->mMeshes[candidate]->mMaterialIndex == primMat)
						{
							_gltfMeshPrimitiveModes[candidate] = glMode;
							break;
						}
					}
				}
			}
		}

		// Push children in reverse order (pre-order DFS).
		// aiNode children correspond 1:1 to glTF node children in the same order.
		const QJsonArray gltfChildren = nodeObj.value("children").toArray();
		for (int i = (int)gltfChildren.size() - 1; i >= 0; --i)
		{
			if (i < (int)aiNodePtr->mNumChildren)
				stack.append({aiNodePtr->mChildren[i], gltfChildren[i].toInt()});
		}
	}

	const QRegularExpression nodeIndexPattern(QStringLiteral("^nodes\\[(\\d+)\\]$"));
	std::function<void(aiNode*)> applyNameBasedFallback = [&](aiNode* aiNodePtr)
	{
		if (!aiNodePtr)
			return;

		const QString aiNodeName = QString::fromUtf8(aiNodePtr->mName.C_Str());
		const QRegularExpressionMatch match = nodeIndexPattern.match(aiNodeName);
		if (match.hasMatch())
		{
			bool ok = false;
			const int gltfNodeIdx = match.captured(1).toInt(&ok);
			if (ok && gltfNodeIdx >= 0 && gltfNodeIdx < jsonNodes.size())
			{
				const QJsonObject nodeObj = jsonNodes[gltfNodeIdx].toObject();
				const int meshIdx = nodeObj.value("mesh").toInt(-1);
				if (meshIdx >= 0 && meshIdx < jsonMeshes.size())
				{
					const QJsonArray primitives = jsonMeshes[meshIdx].toObject().value("primitives").toArray();
					if (primitives.size() == static_cast<int>(aiNodePtr->mNumMeshes))
					{
						for (int primIndex = 0; primIndex < primitives.size(); ++primIndex)
						{
							const QJsonObject primObj = primitives[primIndex].toObject();
							const int mode = primObj.value("mode").toInt(4);

							GLenum glMode = GL_TRIANGLES;
							switch (mode)
							{
							case 0: glMode = GL_POINTS;         break;
							case 1: glMode = GL_LINES;          break;
							case 2: glMode = GL_LINE_LOOP;      break;
							case 3: glMode = GL_LINE_STRIP;     break;
							case 4: glMode = GL_TRIANGLES;      break;
							case 5: glMode = GL_TRIANGLE_STRIP; break;
							case 6: glMode = GL_TRIANGLE_FAN;   break;
							default: glMode = GL_TRIANGLES;     break;
							}

							const unsigned int aiMeshIndex = aiNodePtr->mMeshes[primIndex];
							_gltfMeshPrimitiveModes[aiMeshIndex] = glMode;
						}
					}
				}
			}
		}

		for (unsigned int childIndex = 0; childIndex < aiNodePtr->mNumChildren; ++childIndex)
			applyNameBasedFallback(aiNodePtr->mChildren[childIndex]);
	};

	applyNameBasedFallback(_scene->mRootNode);
}

void AssImpModelLoader::parseGltfVariants(const QString& gltfPath)
{
	_variantData = GltfVariantData();

	const bool isGLB  = gltfPath.endsWith(".glb",  Qt::CaseInsensitive);
	const bool isGLTF = gltfPath.endsWith(".gltf", Qt::CaseInsensitive);
	if (!isGLB && !isGLTF)
		return;

	// ---- Obtain JSON -------------------------------------------------------
	QJsonDocument doc;
	if (isGLB)
	{
		std::vector<uint8_t> glbBinaryBuffer;
		const QString jsonString = MaterialProcessor::extractJsonFromGLB(gltfPath, glbBinaryBuffer);
		if (jsonString.isEmpty())
		{
			qWarning() << "parseGltfVariants: failed to extract JSON from GLB:" << gltfPath;
			return;
		}
		QJsonParseError perr;
		doc = QJsonDocument::fromJson(jsonString.toUtf8(), &perr);
		if (perr.error != QJsonParseError::NoError)
		{
			qWarning() << "parseGltfVariants: JSON parse error:" << perr.errorString();
			return;
		}
	}
	else
	{
		QFile file(gltfPath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			qWarning() << "parseGltfVariants: cannot open" << gltfPath;
			return;
		}
		doc = QJsonDocument::fromJson(file.readAll());
		file.close();
	}

	if (!doc.isObject())
		return;

	const QJsonObject root = doc.object();

	// ---- Root-level variant names -----------------------------------------
	const QJsonObject rootExts    = root.value("extensions").toObject();
	const QJsonObject khrVariants = rootExts.value("KHR_materials_variants").toObject();
	const QJsonArray  variantArr  = khrVariants.value("variants").toArray();

	if (variantArr.isEmpty())
		return;  // extension not present or no variants declared

	_variantData.sourceFile = gltfPath;
	for (const QJsonValue& v : variantArr)
		_variantData.variantNames.append(v.toObject().value("name").toString());

	qDebug() << "parseGltfVariants: found" << _variantData.variantNames.size()
	         << "variants in" << gltfPath << ":" << _variantData.variantNames;

	// ---- Per-mesh-primitive mappings (parallel DFS over aiScene + glTF node trees) ---
	// We walk both trees simultaneously so aiNode->mMeshes[] provides the authoritative
	// aiMesh indices. When Assimp merges multiple glTF primitives that share a material
	// into a single aiMesh (e.g. CarConcept mesh 85 with two prims, both mat 28), a
	// counting-only DFS would over-count and map all subsequent variants to wrong meshes.
	const QJsonArray jsonNodes  = root.value("nodes").toArray();
	const QJsonArray jsonMeshes = root.value("meshes").toArray();
	const QJsonArray jsonScenes = root.value("scenes").toArray();

	int sceneIdx = root.value("scene").toInt(0);
	QJsonArray rootNodeIndices;
	if (sceneIdx >= 0 && sceneIdx < jsonScenes.size())
		rootNodeIndices = jsonScenes[sceneIdx].toObject().value("nodes").toArray();

	if (!_scene || !_scene->mRootNode || rootNodeIndices.isEmpty())
		return;

	struct VariantFrame { aiNode* aiNodePtr; int gltfNodeIdx; };
	QVector<VariantFrame> stack;
	stack.reserve(jsonNodes.size());

	aiNode* aiSceneRoot = _scene->mRootNode;
	if (rootNodeIndices.size() == 1)
	{
		int rootGltfIdx = rootNodeIndices[0].toInt();
		QString gltfRootName = (rootGltfIdx >= 0 && rootGltfIdx < jsonNodes.size())
		    ? jsonNodes[rootGltfIdx].toObject().value("name").toString() : QString();
		QString aiRootName   = QString::fromUtf8(aiSceneRoot->mName.C_Str());

		if (aiRootName == gltfRootName || aiSceneRoot->mNumMeshes > 0)
			stack.append({aiSceneRoot, rootGltfIdx});
		else if (aiSceneRoot->mNumMeshes == 0 && aiSceneRoot->mNumChildren == 1)
			stack.append({aiSceneRoot->mChildren[0], rootGltfIdx});
	}
	else
	{
		for (int i = (int)rootNodeIndices.size() - 1; i >= 0; --i)
		{
			if (i < (int)aiSceneRoot->mNumChildren)
				stack.append({aiSceneRoot->mChildren[i], rootNodeIndices[i].toInt()});
		}
	}

	int aiMeshCount = 0; // for logging only

	while (!stack.isEmpty())
	{
		const auto frame     = stack.takeLast();
		aiNode*    aiNodePtr = frame.aiNodePtr;
		const int  nodeIdx   = frame.gltfNodeIdx;

		if (!aiNodePtr || nodeIdx < 0 || nodeIdx >= jsonNodes.size())
			continue;

		const QJsonObject nodeObj = jsonNodes[nodeIdx].toObject();

		// Process this node's mesh primitives before recursing into children.
		if (nodeObj.contains("mesh") && aiNodePtr->mNumMeshes > 0)
		{
			const int meshIdx = nodeObj.value("mesh").toInt(-1);
			if (meshIdx >= 0 && meshIdx < jsonMeshes.size())
			{
				const QJsonArray primitives = jsonMeshes[meshIdx].toObject().value("primitives").toArray();
				aiMeshCount += (int)aiNodePtr->mNumMeshes;

				for (const QJsonValue& primVal : primitives)
				{
					const QJsonObject primObj  = primVal.toObject();
					const QJsonObject primExts = primObj.value("extensions").toObject();
					const QJsonObject khrPrim  = primExts.value("KHR_materials_variants").toObject();
					const QJsonArray  mappings = khrPrim.value("mappings").toArray();

					if (mappings.isEmpty())
						continue;

					const int primMat = primObj.value("material").toInt(0);

					// Find the aiMesh in this node that has primMat as its material.
					// After updateAiSceneWithGltfMaterials() both indices are in glTF space.
					unsigned int targetAiMeshIdx = ~0u;
					for (unsigned int mi = 0; mi < aiNodePtr->mNumMeshes; ++mi)
					{
						unsigned int candidate = aiNodePtr->mMeshes[mi];
						if ((int)_scene->mMeshes[candidate]->mMaterialIndex == primMat)
						{
							targetAiMeshIdx = candidate;
							break;
						}
					}

					if (targetAiMeshIdx == ~0u)
						continue;

					QVector<GltfVariantMapping> primMappings;
					primMappings.reserve(mappings.size());

					for (const QJsonValue& mapVal : mappings)
					{
						const QJsonObject mapObj = mapVal.toObject();
						GltfVariantMapping gvm;
						gvm.materialIndex = mapObj.value("material").toInt(-1);
						const QJsonArray varIdxArr = mapObj.value("variants").toArray();
						gvm.variantIndices.reserve(varIdxArr.size());
						for (const QJsonValue& vi : varIdxArr)
							gvm.variantIndices.append(vi.toInt());
						if (gvm.materialIndex >= 0 && !gvm.variantIndices.isEmpty())
							primMappings.append(gvm);
					}

					if (!primMappings.isEmpty())
						_variantData.meshVariantMappings[targetAiMeshIdx] = primMappings;
				}
			}
		}

		// Push children in reverse order so first child is processed first (pre-order DFS).
		// aiNode children correspond 1:1 with glTF node children in the same order.
		const QJsonArray gltfChildren = nodeObj.value("children").toArray();
		for (int i = (int)gltfChildren.size() - 1; i >= 0; --i)
		{
			if (i < (int)aiNodePtr->mNumChildren)
				stack.append({aiNodePtr->mChildren[i], gltfChildren[i].toInt()});
		}
	}

	qDebug() << "parseGltfVariants: mapped" << _variantData.meshVariantMappings.size()
	         << "primitives with variant overrides (parallel DFS, total aiMesh count:" << aiMeshCount << ")";
}

void AssImpModelLoader::parseSceneAnimations()
{
	_animationData = GltfAnimationData();
	if (!_scene)
		return;

	_animationData.sourceFile = QString::fromStdString(_path);
	if (_scene->mRootNode)
	{
		aiMatrix4x4 rootInverse = _scene->mRootNode->mTransformation;
		rootInverse.Inverse();
		_animationData.rootInverseTransform = rootInverse;
	}

	for (unsigned int meshIndex = 0; meshIndex < _scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = _scene->mMeshes[meshIndex];
		if (mesh && mesh->HasBones())
		{
			_animationData.hasSkinning = true;
			break;
		}
	}

	for (unsigned int animIndex = 0; animIndex < _scene->mNumAnimations; ++animIndex)
	{
		const aiAnimation* animation = _scene->mAnimations[animIndex];
		if (!animation)
			continue;

		GltfAnimationClip clip;
		clip.name = QString::fromUtf8(animation->mName.C_Str());
		if (clip.name.isEmpty())
			clip.name = QStringLiteral("Animation %1").arg(animIndex + 1);

		const double ticksPerSecond =
			animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
		clip.durationSeconds = animation->mDuration > 0.0
			? animation->mDuration / ticksPerSecond
			: 0.0;
		clip.hasSkinning = _animationData.hasSkinning;

		for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
		{
			const aiNodeAnim* channel = animation->mChannels[channelIndex];
			if (!channel)
				continue;

			if (channel->mNumPositionKeys > 0)
			{
				GltfAnimationChannel animChannel;
				animChannel.targetNodeName = QString::fromUtf8(channel->mNodeName.C_Str());
				animChannel.targetPath = GltfAnimationTargetPath::Translation;
				animChannel.vec3Keys.reserve(channel->mNumPositionKeys);
				for (unsigned int keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex)
				{
					const aiVectorKey& key = channel->mPositionKeys[keyIndex];
					animChannel.vec3Keys.append({
						key.mTime / ticksPerSecond,
						QVector3D(key.mValue.x, key.mValue.y, key.mValue.z)
					});
				}
				clip.channels.append(animChannel);
				clip.hasNodeTransforms = true;
			}

			if (channel->mNumRotationKeys > 0)
			{
				GltfAnimationChannel animChannel;
				animChannel.targetNodeName = QString::fromUtf8(channel->mNodeName.C_Str());
				animChannel.targetPath = GltfAnimationTargetPath::Rotation;
				animChannel.quatKeys.reserve(channel->mNumRotationKeys);
				for (unsigned int keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex)
				{
					const aiQuatKey& key = channel->mRotationKeys[keyIndex];
					animChannel.quatKeys.append({
						key.mTime / ticksPerSecond,
						QQuaternion(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)
					});
				}
				clip.channels.append(animChannel);
				clip.hasNodeTransforms = true;
			}

			if (channel->mNumScalingKeys > 0)
			{
				GltfAnimationChannel animChannel;
				animChannel.targetNodeName = QString::fromUtf8(channel->mNodeName.C_Str());
				animChannel.targetPath = GltfAnimationTargetPath::Scale;
				animChannel.vec3Keys.reserve(channel->mNumScalingKeys);
				for (unsigned int keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex)
				{
					const aiVectorKey& key = channel->mScalingKeys[keyIndex];
					animChannel.vec3Keys.append({
						key.mTime / ticksPerSecond,
						QVector3D(key.mValue.x, key.mValue.y, key.mValue.z)
					});
				}
				clip.channels.append(animChannel);
				clip.hasNodeTransforms = true;
			}
		}

		_animationData.clips.append(clip);
	}

	QJsonDocument doc;
	QVector<QByteArray> bufferData;
	if (!loadAnimationJsonAndBuffer(QString::fromStdString(_path), doc, bufferData, &_materialProcessor) || !doc.isObject())
		return;

	const QJsonObject root = doc.object();
	const QJsonArray animations = root.value("animations").toArray();
	const QJsonArray accessors = root.value("accessors").toArray();
	const QJsonArray bufferViews = root.value("bufferViews").toArray();
	const QJsonArray nodes = root.value("nodes").toArray();
	const QJsonArray scenes = root.value("scenes").toArray();

	_animationData.nodeVisibilityStates.clear();
	_animationData.lightBindings.clear();
	_animationData.nodeBindings.clear();

	QVector<int> parentIndices(nodes.size(), -1);
	for (int nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const QJsonArray children = nodes.at(nodeIndex).toObject().value("children").toArray();
		for (const QJsonValue& childValue : children)
		{
			const int childIndex = childValue.toInt(-1);
			if (childIndex >= 0 && childIndex < parentIndices.size())
				parentIndices[childIndex] = nodeIndex;
		}
	}

	for (int nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const QJsonObject nodeObj = nodes.at(nodeIndex).toObject();
		const QJsonObject extensions = nodeObj.value("extensions").toObject();

		GltfAnimationNodeVisibilityState nodeState;
		nodeState.nodeIndex = nodeIndex;
		nodeState.parentNodeIndex = parentIndices.value(nodeIndex, -1);
		nodeState.nodeName = nodeObj.value("name").toString();
		nodeState.defaultVisible = extensions.value("KHR_node_visibility").toObject().value("visible").toBool(true);
		_animationData.nodeVisibilityStates.append(nodeState);

		GltfAnimationNodeBinding binding;
		binding.nodeIndex = nodeIndex;
		binding.nodeName = nodeState.nodeName;
		_animationData.nodeBindings.append(binding);

		const QJsonObject lightRef = extensions.value("KHR_lights_punctual").toObject();
		if (lightRef.contains("light"))
		{
			GltfAnimationLightBinding binding;
			binding.parsedLightIndex = _animationData.lightBindings.size();
			binding.lightDefinitionIndex = lightRef.value("light").toInt(-1);
			binding.nodeIndex = nodeIndex;
			binding.nodeName = nodeState.nodeName;
			_animationData.lightBindings.append(binding);
		}
	}

	if (_scene && _scene->mRootNode && !scenes.isEmpty())
	{
		const int sceneIdx = root.value("scene").toInt(0);
		QJsonArray rootNodeIndices;
		if (sceneIdx >= 0 && sceneIdx < scenes.size())
			rootNodeIndices = scenes[sceneIdx].toObject().value("nodes").toArray();

		struct NodeFrame
		{
			aiNode* aiNodePtr;
			int gltfNodeIdx;
			QVector<int> aiChildPath;
		};
		QVector<NodeFrame> stack;
		aiNode* aiRoot = _scene->mRootNode;
		if (rootNodeIndices.size() == 1)
		{
			const int rootGltfIdx = rootNodeIndices[0].toInt();
			const QString gltfRootName = (rootGltfIdx >= 0 && rootGltfIdx < nodes.size())
				? nodes[rootGltfIdx].toObject().value("name").toString() : QString();
			const QString aiRootName = QString::fromUtf8(aiRoot->mName.C_Str());

			if (aiRootName == gltfRootName || aiRoot->mNumMeshes > 0 || aiRoot->mNumChildren == 0)
				stack.append({ aiRoot, rootGltfIdx, {} });
			else if (aiRoot->mNumMeshes == 0 && aiRoot->mNumChildren == 1)
				stack.append({ aiRoot->mChildren[0], rootGltfIdx, { 0 } });
		}
		else
		{
			for (int i = rootNodeIndices.size() - 1; i >= 0; --i)
			{
				if (i < static_cast<int>(aiRoot->mNumChildren))
					stack.append({ aiRoot->mChildren[i], rootNodeIndices[i].toInt(), { i } });
			}
		}

		while (!stack.isEmpty())
		{
			const NodeFrame frame = stack.takeLast();
			if (!frame.aiNodePtr || frame.gltfNodeIdx < 0 || frame.gltfNodeIdx >= nodes.size())
				continue;

			QString aiNodeName = QString::fromUtf8(frame.aiNodePtr->mName.C_Str());
			if (aiNodeName.isEmpty())
			{
				aiNodeName = QStringLiteral("__gltfNode_%1").arg(frame.gltfNodeIdx);
				frame.aiNodePtr->mName = aiString(aiNodeName.toUtf8().constData());
			}
			if (frame.gltfNodeIdx < _animationData.nodeBindings.size())
			{
				_animationData.nodeBindings[frame.gltfNodeIdx].nodeName = aiNodeName;
				_animationData.nodeBindings[frame.gltfNodeIdx].hasAiChildPath = true;
				_animationData.nodeBindings[frame.gltfNodeIdx].aiChildPath = frame.aiChildPath;
			}
			if (frame.gltfNodeIdx < _animationData.nodeVisibilityStates.size())
				_animationData.nodeVisibilityStates[frame.gltfNodeIdx].nodeName = aiNodeName;
			for (GltfAnimationLightBinding& binding : _animationData.lightBindings)
			{
				if (binding.nodeIndex == frame.gltfNodeIdx)
					binding.nodeName = aiNodeName;
			}

			const QJsonArray childIndices = nodes[frame.gltfNodeIdx].toObject().value("children").toArray();
			for (int childOffset = childIndices.size() - 1; childOffset >= 0; --childOffset)
			{
				if (childOffset < static_cast<int>(frame.aiNodePtr->mNumChildren))
				{
					QVector<int> childPath = frame.aiChildPath;
					childPath.append(childOffset);
					stack.append({ frame.aiNodePtr->mChildren[childOffset], childIndices[childOffset].toInt(), childPath });
				}
			}
		}
	}

	for (int animIndex = 0; animIndex < animations.size(); ++animIndex)
	{
		const QJsonObject animationObj = animations.at(animIndex).toObject();
		if (_animationData.clips.size() <= animIndex)
		{
			GltfAnimationClip clip;
			clip.name = animationObj.value("name").toString();
			if (clip.name.isEmpty())
				clip.name = QStringLiteral("Animation %1").arg(animIndex + 1);
			clip.hasSkinning = _animationData.hasSkinning;
			_animationData.clips.append(clip);
		}

		GltfAnimationClip& clip = _animationData.clips[animIndex];
		const QJsonArray samplers = animationObj.value("samplers").toArray();
		const QJsonArray channels = animationObj.value("channels").toArray();
		bool jsonHasPointerTargets = false;
		bool jsonHasNodeTransformTargets = false;

		for (const QJsonValue& channelValue : channels)
		{
			const QJsonObject targetObj = channelValue.toObject().value("target").toObject();
			const QString path = targetObj.value("path").toString();
			if (path == "pointer")
			{
				jsonHasPointerTargets = true;
			}
			else if (path == "translation" || path == "rotation" || path == "scale")
			{
				jsonHasNodeTransformTargets = true;
			}
		}

		if (jsonHasPointerTargets && !jsonHasNodeTransformTargets)
		{
			QVector<GltfAnimationChannel> pointerOnlyChannels;
			pointerOnlyChannels.reserve(clip.channels.size());
			for (const GltfAnimationChannel& channel : std::as_const(clip.channels))
			{
				if (channel.targetPath == GltfAnimationTargetPath::Pointer)
					pointerOnlyChannels.append(channel);
			}
			clip.channels = pointerOnlyChannels;
			clip.hasNodeTransforms = false;
		}

		for (const QJsonValue& channelValue : channels)
		{
			const QJsonObject channelObj = channelValue.toObject();
			const QJsonObject targetObj = channelObj.value("target").toObject();
			const QString targetPath = targetObj.value("path").toString();
			if (targetPath == "translation" || targetPath == "rotation" || targetPath == "scale")
			{
				const int samplerIndex = channelObj.value("sampler").toInt(-1);
				const int nodeIndex = targetObj.value("node").toInt(-1);
				if (samplerIndex < 0 || samplerIndex >= samplers.size() || nodeIndex < 0 || nodeIndex >= nodes.size())
					continue;

				const QJsonObject samplerObj = samplers.at(samplerIndex).toObject();
				const int inputAccessorIndex = samplerObj.value("input").toInt(-1);
				const int outputAccessorIndex = samplerObj.value("output").toInt(-1);
				if (inputAccessorIndex < 0 || outputAccessorIndex < 0)
					continue;

				QVector<float> inputTimes;
				int keyCount = 0;
				if (!readFloatAccessorData(accessors, bufferViews, bufferData, inputAccessorIndex, 1, inputTimes, &keyCount) || keyCount <= 0)
					continue;

				GltfAnimationChannel transformChannel;
				transformChannel.targetNodeIndex = nodeIndex;
				transformChannel.targetNodeName = nodeIndex < _animationData.nodeBindings.size()
					? _animationData.nodeBindings[nodeIndex].nodeName
					: nodes.at(nodeIndex).toObject().value("name").toString();

				if (targetPath == "translation" || targetPath == "scale")
				{
					QVector<float> outputValues;
					int outputCount = 0;
					if (!readFloatAccessorData(accessors, bufferViews, bufferData, outputAccessorIndex, 3, outputValues, &outputCount) ||
						outputCount != keyCount)
					{
						continue;
					}

					transformChannel.targetPath = targetPath == "translation"
						? GltfAnimationTargetPath::Translation
						: GltfAnimationTargetPath::Scale;
					transformChannel.vec3Keys.reserve(keyCount);
					for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						transformChannel.vec3Keys.append({
							inputTimes[keyIndex],
							QVector3D(
								outputValues[keyIndex * 3],
								outputValues[keyIndex * 3 + 1],
								outputValues[keyIndex * 3 + 2])
						});
					}
				}
				else
				{
					QVector<float> outputValues;
					int outputCount = 0;
					if (!readFloatAccessorData(accessors, bufferViews, bufferData, outputAccessorIndex, 4, outputValues, &outputCount) ||
						outputCount != keyCount)
					{
						continue;
					}

					transformChannel.targetPath = GltfAnimationTargetPath::Rotation;
					transformChannel.quatKeys.reserve(keyCount);
					for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						transformChannel.quatKeys.append({
							inputTimes[keyIndex],
							QQuaternion(
								outputValues[keyIndex * 4 + 3],
								outputValues[keyIndex * 4],
								outputValues[keyIndex * 4 + 1],
								outputValues[keyIndex * 4 + 2]).normalized()
						});
					}
				}

				if (!inputTimes.isEmpty())
					clip.durationSeconds = std::max(clip.durationSeconds, static_cast<double>(inputTimes.back()));
				clip.hasNodeTransforms = true;
				clip.channels.append(transformChannel);
				continue;
			}
			if (targetPath == "weights")
			{
				const int samplerIndex = channelObj.value("sampler").toInt(-1);
				if (samplerIndex < 0 || samplerIndex >= samplers.size())
					continue;

				const int nodeIndex = targetObj.value("node").toInt(-1);
				if (nodeIndex < 0 || nodeIndex >= nodes.size())
					continue;

				const QJsonObject samplerObj = samplers.at(samplerIndex).toObject();
				const int inputAccessorIndex = samplerObj.value("input").toInt(-1);
				const int outputAccessorIndex = samplerObj.value("output").toInt(-1);
				if (inputAccessorIndex < 0 || outputAccessorIndex < 0)
					continue;

				QVector<float> inputTimes;
				int keyCount = 0;
				if (!readFloatAccessorData(accessors, bufferViews, bufferData, inputAccessorIndex, 1, inputTimes, &keyCount) || keyCount <= 0)
					continue;

				QVector<float> outputValues;
				int outputCount = 0;
				if (!readFloatAccessorData(accessors, bufferViews, bufferData, outputAccessorIndex, 1, outputValues, &outputCount) || outputCount <= 0)
					continue;

				if (outputCount % keyCount != 0)
					continue;

				const int weightsPerKey = outputCount / keyCount;
				GltfAnimationChannel weightChannel;
				weightChannel.targetPath = GltfAnimationTargetPath::Weights;
				weightChannel.targetNodeIndex = nodeIndex;
				weightChannel.targetNodeName = nodeIndex < _animationData.nodeBindings.size()
					? _animationData.nodeBindings[nodeIndex].nodeName
					: nodes.at(nodeIndex).toObject().value("name").toString();
				weightChannel.weightKeys.reserve(keyCount);

				for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
				{
					GltfAnimationWeightsKey key;
					key.timeSeconds = inputTimes[keyIndex];
					key.values.reserve(weightsPerKey);
					for (int weightIndex = 0; weightIndex < weightsPerKey; ++weightIndex)
						key.values.append(outputValues[keyIndex * weightsPerKey + weightIndex]);
					weightChannel.weightKeys.append(std::move(key));
				}

				if (!inputTimes.isEmpty())
					clip.durationSeconds = std::max(clip.durationSeconds, static_cast<double>(inputTimes.back()));
				clip.hasMorphAnimations = true;
				clip.channels.append(weightChannel);
				continue;
			}
			if (targetPath != "pointer")
				continue;

			const int samplerIndex = channelObj.value("sampler").toInt(-1);
			if (samplerIndex < 0 || samplerIndex >= samplers.size())
				continue;

			const QString pointer = targetObj.value("extensions").toObject()
				.value("KHR_animation_pointer").toObject()
				.value("pointer").toString();
			if (pointer.isEmpty())
				continue;

			int materialIndex = -1;
			GltfAnimationTextureTarget textureTarget = GltfAnimationTextureTarget::Unknown;
			GltfAnimationPointerProperty pointerProperty = GltfAnimationPointerProperty::None;
			const QJsonObject samplerObj = samplers.at(samplerIndex).toObject();
			const int inputAccessorIndex = samplerObj.value("input").toInt(-1);
			const int outputAccessorIndex = samplerObj.value("output").toInt(-1);
			if (inputAccessorIndex < 0 || outputAccessorIndex < 0)
				continue;

			QVector<float> inputTimes;
			int keyCount = 0;
			if (!readFloatAccessorData(accessors, bufferViews, bufferData, inputAccessorIndex, 1, inputTimes, &keyCount) || keyCount <= 0)
				continue;

			GltfAnimationChannel pointerChannel;
			pointerChannel.targetPath = GltfAnimationTargetPath::Pointer;
			pointerChannel.targetPointer = pointer;
			if (decodeAnimationPointerTarget(pointer, materialIndex, textureTarget, pointerProperty))
			{
				pointerChannel.pointerTargetKind = GltfAnimationPointerTargetKind::MaterialTextureTransform;
				pointerChannel.targetMaterialIndex = materialIndex;
				pointerChannel.textureTarget = textureTarget;
				pointerChannel.pointerProperty = pointerProperty;

				if (pointerProperty == GltfAnimationPointerProperty::Rotation)
				{
					QVector<float> outputValues;
					int outputCount = 0;
					if (!readFloatAccessorData(accessors, bufferViews, bufferData, outputAccessorIndex, 1, outputValues, &outputCount) ||
						outputCount != keyCount)
					{
						continue;
					}

					pointerChannel.floatKeys.reserve(keyCount);
					for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						pointerChannel.floatKeys.append({
							inputTimes[keyIndex],
							outputValues[keyIndex]
						});
					}
				}
				else
				{
					QVector<float> outputValues;
					int outputCount = 0;
					if (!readFloatAccessorData(accessors, bufferViews, bufferData, outputAccessorIndex, 2, outputValues, &outputCount) ||
						outputCount != keyCount)
					{
						continue;
					}

					pointerChannel.vec2Keys.reserve(keyCount);
					for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						pointerChannel.vec2Keys.append({
							inputTimes[keyIndex],
							QVector2D(outputValues[keyIndex * 2], outputValues[keyIndex * 2 + 1])
						});
					}
				}
			}
			else if (decodeMaterialFactorPointerTarget(pointer, materialIndex, pointerProperty))
			{
				QVector<float> outputValues;
				int outputCount = 0;
				if (!readFloatAccessorData(accessors, bufferViews, bufferData, outputAccessorIndex, 4, outputValues, &outputCount) ||
					outputCount != keyCount)
				{
					continue;
				}

				pointerChannel.pointerTargetKind = GltfAnimationPointerTargetKind::MaterialTextureTransform;
				pointerChannel.targetMaterialIndex = materialIndex;
				pointerChannel.pointerProperty = pointerProperty;
				pointerChannel.vec4Keys.reserve(keyCount);
				for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
				{
					pointerChannel.vec4Keys.append({
						inputTimes[keyIndex],
						QVector4D(
							outputValues[keyIndex * 4],
							outputValues[keyIndex * 4 + 1],
							outputValues[keyIndex * 4 + 2],
							outputValues[keyIndex * 4 + 3])
					});
				}
			}
			else
			{
				int nodeIndex = -1;
				if (!decodeNodeVisibilityPointerTarget(pointer, nodeIndex) || nodeIndex >= nodes.size())
					continue;

				QVector<float> outputValues;
				int outputCount = 0;
				if (!readScalarAccessorDataAsFloats(accessors, bufferViews, bufferData, outputAccessorIndex, outputValues, &outputCount) ||
					outputCount != keyCount)
				{
					continue;
				}

				pointerChannel.pointerTargetKind = GltfAnimationPointerTargetKind::NodeVisibility;
				pointerChannel.pointerProperty = GltfAnimationPointerProperty::Visibility;
				pointerChannel.targetNodeIndex = nodeIndex;
				pointerChannel.targetNodeName = nodes.at(nodeIndex).toObject().value("name").toString();
				pointerChannel.boolKeys.reserve(keyCount);
				for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
				{
					pointerChannel.boolKeys.append({
						inputTimes[keyIndex],
						outputValues[keyIndex] >= 0.5f
					});
				}
			}

			if (!inputTimes.isEmpty())
				clip.durationSeconds = std::max(clip.durationSeconds, static_cast<double>(inputTimes.back()));
			clip.hasPointerAnimations = true;
			clip.channels.append(pointerChannel);
		}

	}

	_animationData.hasNodeAnimations = false;
	_animationData.hasMorphAnimations = false;
	_animationData.hasPointerAnimations = false;
	for (const GltfAnimationClip& clip : std::as_const(_animationData.clips))
	{
		_animationData.hasNodeAnimations |= clip.hasNodeTransforms;
		_animationData.hasMorphAnimations |= clip.hasMorphAnimations;
		_animationData.hasPointerAnimations |= clip.hasPointerAnimations;
	}
}

// ---------------------------------------------------------------------------
// parseSceneCameras
//
// Populates _cameraData from aiScene::mCameras[].  Each aiCamera entry gives
// us the projection parameters (FOV, clip planes, ortho extents) and a node
// name.  The world-space position and orientation are derived by accumulating
// the transform chain from the scene root down to the matching node.
//
// glTF cameras look along the node's local -Z axis with +Y as up.
// ---------------------------------------------------------------------------

namespace
{
// Recursively find the node with the given name and accumulate its world
// transform.  Returns true and sets 'result' when found.
bool findNodeWorldTransform(const aiNode*       node,
                            const aiString&     targetName,
                            const aiMatrix4x4&  parentTransform,
                            aiMatrix4x4&        result)
{
    if (!node)
        return false;

    const aiMatrix4x4 worldTransform = parentTransform * node->mTransformation;

    if (node->mName == targetName)
    {
        result = worldTransform;
        return true;
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        if (findNodeWorldTransform(node->mChildren[i], targetName, worldTransform, result))
            return true;
    }
    return false;
}
} // anonymous namespace

void AssImpModelLoader::parseSceneCameras()
{
    _cameraData = GltfCameraData();

    if (!_scene || _scene->mNumCameras == 0)
        return;

    _cameraData.sourceFile = QString::fromStdString(_path);

    struct CameraNodeBinding
    {
        int nodeIndex = -1;
        int cameraIndex = -1;
        QVector<int> aiChildPath;
        aiMatrix4x4 worldTransform;
        bool hasWorldTransform = false;
    };
    QVector<CameraNodeBinding> cameraBindingsByOrdinal;
    QJsonArray cameraDefs;
    if (_scene->mRootNode)
    {
        QJsonDocument doc;
        QVector<QByteArray> bufferData;
        if (loadAnimationJsonAndBuffer(QString::fromStdString(_path), doc, bufferData, &_materialProcessor) && doc.isObject())
        {
            const QJsonObject root = doc.object();
            const QJsonArray nodes = root.value("nodes").toArray();
            const QJsonArray scenes = root.value("scenes").toArray();
            cameraDefs = root.value("cameras").toArray();

            const int sceneIdx = root.value("scene").toInt(0);
            QJsonArray rootNodeIndices;
            if (sceneIdx >= 0 && sceneIdx < scenes.size())
                rootNodeIndices = scenes[sceneIdx].toObject().value("nodes").toArray();

            struct NodeFrame
            {
                aiNode* aiNodePtr;
                int gltfNodeIdx;
                QVector<int> aiChildPath;
                aiMatrix4x4 worldTransform;
            };
            QVector<NodeFrame> stack;
            aiNode* aiRoot = _scene->mRootNode;

            if (rootNodeIndices.size() == 1)
            {
                const int rootGltfIdx = rootNodeIndices[0].toInt();
                const QString gltfRootName = (rootGltfIdx >= 0 && rootGltfIdx < nodes.size())
                    ? nodes[rootGltfIdx].toObject().value("name").toString() : QString();
                const QString aiRootName = QString::fromUtf8(aiRoot->mName.C_Str());

                if (aiRootName == gltfRootName || aiRoot->mNumMeshes > 0 || aiRoot->mNumChildren == 0)
                    stack.append({ aiRoot, rootGltfIdx, {}, aiRoot->mTransformation });
                else if (aiRoot->mNumMeshes == 0 && aiRoot->mNumChildren == 1)
                    stack.append({ aiRoot->mChildren[0], rootGltfIdx, { 0 }, aiRoot->mTransformation * aiRoot->mChildren[0]->mTransformation });
            }
            else
            {
                for (int i = rootNodeIndices.size() - 1; i >= 0; --i)
                {
                    if (i < static_cast<int>(aiRoot->mNumChildren))
                        stack.append({ aiRoot->mChildren[i], rootNodeIndices[i].toInt(), { i }, aiRoot->mTransformation * aiRoot->mChildren[i]->mTransformation });
                }
            }

            while (!stack.isEmpty())
            {
                const NodeFrame frame = stack.takeLast();
                if (!frame.aiNodePtr || frame.gltfNodeIdx < 0 || frame.gltfNodeIdx >= nodes.size())
                    continue;

                const QJsonObject nodeObj = nodes[frame.gltfNodeIdx].toObject();
                if (nodeObj.contains("camera"))
                {
                    CameraNodeBinding binding;
                    binding.nodeIndex = frame.gltfNodeIdx;
                    binding.cameraIndex = nodeObj.value("camera").toInt(-1);
                    binding.aiChildPath = frame.aiChildPath;
                    binding.worldTransform = frame.worldTransform;
                    binding.hasWorldTransform = true;
                    cameraBindingsByOrdinal.append(binding);
                }

                const QJsonArray childIndices = nodeObj.value("children").toArray();
                for (int childOffset = childIndices.size() - 1; childOffset >= 0; --childOffset)
                {
                    if (childOffset < static_cast<int>(frame.aiNodePtr->mNumChildren))
                    {
                        QVector<int> childPath = frame.aiChildPath;
                        childPath.append(childOffset);
                        aiNode* childNode = frame.aiNodePtr->mChildren[childOffset];
                        stack.append({ childNode,
                                       childIndices[childOffset].toInt(),
                                       childPath,
                                       frame.worldTransform * childNode->mTransformation });
                    }
                }
            }
        }
    }

    for (unsigned int i = 0; i < _scene->mNumCameras; ++i)
    {
        const aiCamera* cam = _scene->mCameras[i];
        if (!cam)
            continue;

        GltfCameraEntry entry;
        entry.name     = QString::fromUtf8(cam->mName.C_Str());
        // Imported glTF camera poses are already authored in the same scene
        // space as the preserved node-transform hierarchy, so they should be
        // applied directly.  Model-transform compensation is reserved for
        // MVF-restored cameras that were saved after user-driven global
        // model transforms.
        entry.needsModelTransformCompensation = false;
        // Assimp's glTF2 importer sets aiCamera::mName to the scene-node name
        // that references the camera.  That node name is the key used by the
        // animation runtime's worldTransforms map, so store it directly.
        entry.nodeName = entry.name;
        if (static_cast<int>(i) < cameraBindingsByOrdinal.size())
        {
            const CameraNodeBinding& binding = cameraBindingsByOrdinal.at(static_cast<int>(i));
            entry.nodeIndex = binding.nodeIndex;
            entry.aiChildPath = binding.aiChildPath;
            entry.hasAiChildPath = !entry.aiChildPath.isEmpty();

            if (binding.cameraIndex >= 0 && binding.cameraIndex < cameraDefs.size())
            {
                const QJsonObject cameraObj = cameraDefs.at(binding.cameraIndex).toObject();
                const QString type = cameraObj.value("type").toString();
                if (type == QLatin1String("orthographic"))
                {
                    entry.type = GltfCameraType::Orthographic;
                    const QJsonObject orthoObj = cameraObj.value("orthographic").toObject();
                    entry.xMag = static_cast<float>(orthoObj.value("xmag").toDouble(entry.xMag));
                    entry.yMag = static_cast<float>(orthoObj.value("ymag").toDouble(entry.yMag));
                    entry.zNear = static_cast<float>(orthoObj.value("znear").toDouble(entry.zNear));
                    entry.zFar = static_cast<float>(orthoObj.value("zfar").toDouble(entry.zFar));
                }
                else
                {
                    entry.type = GltfCameraType::Perspective;
                    const QJsonObject perspObj = cameraObj.value("perspective").toObject();
                    entry.fovYRadians = static_cast<float>(perspObj.value("yfov").toDouble(entry.fovYRadians));
                    entry.zNear = static_cast<float>(perspObj.value("znear").toDouble(entry.zNear));
                    entry.zFar = static_cast<float>(perspObj.value("zfar").toDouble(entry.zFar));
                }
            }
            else if (cam->mOrthographicWidth > 0.0f)
            {
                entry.type  = GltfCameraType::Orthographic;
                entry.xMag  = cam->mOrthographicWidth;
                entry.yMag  = (cam->mAspect > 0.0f)
                            ? cam->mOrthographicWidth / cam->mAspect
                            : cam->mOrthographicWidth;
                entry.zNear = cam->mClipPlaneNear;
                entry.zFar  = cam->mClipPlaneFar;
            }
            else
            {
                entry.type = GltfCameraType::Perspective;
                entry.fovYRadians = cam->mHorizontalFOV;
                entry.zNear = cam->mClipPlaneNear;
                entry.zFar  = cam->mClipPlaneFar;
            }
        }
        else
        {
            if (cam->mOrthographicWidth > 0.0f)
            {
                entry.type  = GltfCameraType::Orthographic;
                entry.xMag  = cam->mOrthographicWidth;
                entry.yMag  = (cam->mAspect > 0.0f)
                            ? cam->mOrthographicWidth / cam->mAspect
                            : cam->mOrthographicWidth;
            }
            else
            {
                entry.type = GltfCameraType::Perspective;
                entry.fovYRadians = cam->mHorizontalFOV;
            }
            entry.zNear = cam->mClipPlaneNear;
            entry.zFar  = cam->mClipPlaneFar;
        }

        aiMatrix4x4 worldMat;
        bool hasWorldMat = false;
        if (static_cast<int>(i) < cameraBindingsByOrdinal.size())
        {
            const CameraNodeBinding& binding = cameraBindingsByOrdinal.at(static_cast<int>(i));
            if (binding.hasWorldTransform)
            {
                worldMat = binding.worldTransform;
                hasWorldMat = true;
            }
        }

        // Fallback for non-glTF or malformed bindings.
        if (!hasWorldMat)
        {
            aiMatrix4x4 identity;
            hasWorldMat = findNodeWorldTransform(_scene->mRootNode, cam->mName, identity, worldMat);
        }

        if (hasWorldMat)
        {
            // World position = translation component of the 4×4 matrix
            entry.worldPosition = QVector3D(worldMat.a4, worldMat.b4, worldMat.c4);

            // glTF cameras look along -Z in local space; +Y is up.
            // Rotate these local vectors by the world rotation (3×3 submatrix).
            aiMatrix3x3 rot(worldMat);
            aiVector3D localForward(0.0f, 0.0f, -1.0f);
            aiVector3D localUp(0.0f, 1.0f, 0.0f);
            aiVector3D worldForward = (rot * localForward).Normalize();
            aiVector3D worldUp      = (rot * localUp).Normalize();

            entry.worldDirection = QVector3D(worldForward.x, worldForward.y, worldForward.z);
            entry.worldUp        = QVector3D(worldUp.x,      worldUp.y,      worldUp.z);
        }
        else
        {
            qWarning() << "parseSceneCameras: node not found for camera" << entry.name;
            // Use defaults: looking along -Z from the origin
        }

        _cameraData.cameras.append(entry);
        qDebug() << "parseSceneCameras: camera" << entry.name
                 << "pos" << entry.worldPosition
                 << "dir" << entry.worldDirection
                 << (entry.type == GltfCameraType::Perspective
                     ? QString("fovY=%1°").arg(qRadiansToDegrees(entry.fovYRadians), 0, 'f', 1)
                     : QString("ortho xMag=%1").arg(entry.xMag));
    }

    qDebug() << "parseSceneCameras: found" << _cameraData.cameras.size()
             << "camera(s) in" << _cameraData.sourceFile;
}

void AssImpModelLoader::updateAiSceneWithGltfMaterials(const QString& gltfPath, aiScene* scene)
{
	if (!scene || scene->mNumMaterials == 0)
		return;

	bool isGLB = gltfPath.endsWith(".glb", Qt::CaseInsensitive);
	bool isGLTF = gltfPath.endsWith(".gltf", Qt::CaseInsensitive);

	if (!isGLB && !isGLTF)
		return;

	QJsonDocument doc;

	// ===== READ GLTF JSON =====
	if (isGLB)
	{
		std::vector<uint8_t> glbBinaryBuffer;
		QString jsonString = MaterialProcessor::extractJsonFromGLB(gltfPath, glbBinaryBuffer);
		if (jsonString.isEmpty())
			return;

		doc = QJsonDocument::fromJson(jsonString.toUtf8());

		// Seed the per-instance cache so loadAnimationJsonAndBuffer and
		// processGltf2CoreAndExtensions can skip redundant file reads for every mesh.
		_materialProcessor.seedGlbCacheIfAbsent(gltfPath, doc, glbBinaryBuffer);
	}
	else
	{
		QFile file(gltfPath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;

		doc = QJsonDocument::fromJson(file.readAll());
		file.close();
	}

	if (!doc.isObject())
		return;

	QJsonObject root = doc.object();
	QJsonArray gltfMaterials = root["materials"].toArray();
	const QJsonArray jsonNodes = root["nodes"].toArray();
	const QJsonArray jsonMeshes = root["meshes"].toArray();
	const QJsonArray jsonScenes = root["scenes"].toArray();

	if (gltfMaterials.isEmpty())
		return;

	int gltfMatCount = static_cast<int>(gltfMaterials.size());
	int aiMatCount   = static_cast<int>(scene->mNumMaterials);

	// ===== BUILD NAME→GLTF INDEX MAP =====
	// Unique glTF material names are safe fallbacks when primitive provenance is
	// unavailable. They must not override authoritative primitive-derived data.
	QMap<QString, int> nameToGltfIdx;
	QMap<QString, int> nameUseCount;
	for (int i = 0; i < gltfMatCount; ++i)
	{
		QString name = gltfMaterials[i].toObject()["name"].toString();
		if (name.isEmpty())
			continue;

		nameUseCount[name] += 1;
		if (!nameToGltfIdx.contains(name))
			nameToGltfIdx.insert(name, i);
	}

	// Unique glTF mesh names provide a second provenance layer for assets where
	// aiNode/glTF-node traversal cannot be aligned directly, but aiMesh names
	// still preserve source mesh identity (for example "tower00" or "car-12-1").
	QMap<QString, int> meshNameUseCount;
	QMap<QString, int> meshNameToUniqueIndex;
	for (int i = 0; i < jsonMeshes.size(); ++i)
	{
		const QString name = jsonMeshes.at(i).toObject().value("name").toString();
		if (name.isEmpty())
			continue;

		meshNameUseCount[name] += 1;
		if (!meshNameToUniqueIndex.contains(name))
			meshNameToUniqueIndex.insert(name, i);
	}
	for (auto it = meshNameToUniqueIndex.begin(); it != meshNameToUniqueIndex.end(); )
	{
		if (meshNameUseCount.value(it.key()) != 1)
			it = meshNameToUniqueIndex.erase(it);
		else
			++it;
	}

	// ===== BUILD AI-MESH → GLTF MATERIAL PROVENANCE =====
	// Follow the Khronos viewer's correctness model: the glTF primitive owns the
	// default material, and variants only override that primitive-local index.
	// Our job is to reconstruct that primitive→material identity before Assimp's
	// compact material slots are treated as authoritative.
	std::map<int, int> aiMeshToGltfMaterial;

	struct MaterialFrame { aiNode* aiNodePtr; int gltfNodeIdx; };
	QVector<MaterialFrame> stack;
	stack.reserve(jsonNodes.size());

	int rootSceneIndex = root.value("scene").toInt(0);
	QJsonArray rootNodeIndices;
	if (rootSceneIndex >= 0 && rootSceneIndex < jsonScenes.size())
		rootNodeIndices = jsonScenes[rootSceneIndex].toObject().value("nodes").toArray();

	if (_scene && _scene->mRootNode && !rootNodeIndices.isEmpty())
	{
		aiNode* aiSceneRoot = _scene->mRootNode;
		if (rootNodeIndices.size() == 1)
		{
			const int rootGltfIdx = rootNodeIndices[0].toInt();
			const QString gltfRootName = (rootGltfIdx >= 0 && rootGltfIdx < jsonNodes.size())
				? jsonNodes[rootGltfIdx].toObject().value("name").toString()
				: QString();
			const QString aiRootName = QString::fromUtf8(aiSceneRoot->mName.C_Str());

			if (aiRootName == gltfRootName || aiSceneRoot->mNumMeshes > 0)
				stack.append({ aiSceneRoot, rootGltfIdx });
			else if (aiSceneRoot->mNumMeshes == 0 && aiSceneRoot->mNumChildren == 1)
				stack.append({ aiSceneRoot->mChildren[0], rootGltfIdx });
		}
		else
		{
			for (int i = static_cast<int>(rootNodeIndices.size()) - 1; i >= 0; --i)
			{
				if (i < static_cast<int>(aiSceneRoot->mNumChildren))
					stack.append({ aiSceneRoot->mChildren[i], rootNodeIndices[i].toInt() });
			}
		}
	}

	while (!stack.isEmpty())
	{
		const MaterialFrame frame = stack.takeLast();
		aiNode* aiNodePtr = frame.aiNodePtr;
		const int nodeIdx = frame.gltfNodeIdx;

		if (!aiNodePtr || nodeIdx < 0 || nodeIdx >= jsonNodes.size())
			continue;

		const QJsonObject nodeObj = jsonNodes[nodeIdx].toObject();
		if (nodeObj.contains("mesh") && aiNodePtr->mNumMeshes > 0)
		{
			const int gltfMeshIdx = nodeObj.value("mesh").toInt(-1);
			if (gltfMeshIdx >= 0 && gltfMeshIdx < jsonMeshes.size())
			{
				const QJsonArray primitives =
					jsonMeshes[gltfMeshIdx].toObject().value("primitives").toArray();

				// Exact positional pairing is the safest path when Assimp preserved
				// the primitive list length for this node.
				if (primitives.size() == static_cast<int>(aiNodePtr->mNumMeshes))
				{
					for (int primIndex = 0; primIndex < primitives.size(); ++primIndex)
					{
						const QJsonObject primObj = primitives.at(primIndex).toObject();
						const int primMat = primObj.contains("material")
							? primObj.value("material").toInt(-1)
							: -1;
						const unsigned int aiMeshIdx = aiNodePtr->mMeshes[primIndex];
						aiMeshToGltfMaterial[static_cast<int>(aiMeshIdx)] = primMat;
					}
				}
			}
		}

		const QJsonArray gltfChildren = nodeObj.value("children").toArray();
		for (int i = static_cast<int>(gltfChildren.size()) - 1; i >= 0; --i)
		{
			if (i < static_cast<int>(aiNodePtr->mNumChildren))
				stack.append({ aiNodePtr->mChildren[i], gltfChildren[i].toInt() });
		}
	}

	// Assimp often preserves merged primitive provenance in aiMesh::mName
	// (for example "meshes[0]-81.meshes[0]-83..."). Use it to recover the
	// authoritative glTF material for merged meshes.
	for (unsigned int meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
	{
		if (aiMeshToGltfMaterial.find(static_cast<int>(meshIdx)) != aiMeshToGltfMaterial.end())
			continue;

		const QString directMeshName = QString::fromUtf8(scene->mMeshes[meshIdx]->mName.C_Str());
		int resolvedMaterial = -1;
		if (resolveSingleGltfMaterialFromMeshName(
			directMeshName,
			jsonMeshes,
			meshNameToUniqueIndex,
			resolvedMaterial))
		{
			aiMeshToGltfMaterial[static_cast<int>(meshIdx)] = resolvedMaterial;
			continue;
		}

		const QVector<GltfPrimitiveReference> refs = parsePrimitiveReferencesFromMeshName(directMeshName);
		if (refs.isEmpty())
			continue;

		resolvedMaterial = -1;
		if (resolveSingleGltfMaterialFromPrimitiveRefs(refs, jsonMeshes, resolvedMaterial))
		{
			aiMeshToGltfMaterial[static_cast<int>(meshIdx)] = resolvedMaterial;
		}
	}

	// ===== BUILD COMPACT→GLTF INDEX MAPPING =====
	// Derive compact aiMaterial slots from the meshes that referenced them before
	// reindexing. This preserves the glTF material identity used by variants and export.
	std::map<int, QSet<int>> compactMatCandidates;
	for (unsigned int meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
	{
		const auto meshMatIt = aiMeshToGltfMaterial.find(static_cast<int>(meshIdx));
		if (meshMatIt == aiMeshToGltfMaterial.end())
			continue;

		int compactMatIdx = static_cast<int>(scene->mMeshes[meshIdx]->mMaterialIndex);
		const auto savedIt = _meshIndexToOriginalMaterialIndex.find(static_cast<int>(meshIdx));
		if (savedIt != _meshIndexToOriginalMaterialIndex.end())
			compactMatIdx = savedIt->second;

		compactMatCandidates[compactMatIdx].insert(meshMatIt->second);
	}

	_aiMatToGltfMat.clear();
	for (int aiIdx = 0; aiIdx < aiMatCount; ++aiIdx)
	{
		int resolvedGltfIdx = -1;

		const auto compactIt = compactMatCandidates.find(aiIdx);
		if (compactIt != compactMatCandidates.end())
		{
			const QSet<int>& candidates = compactIt->second;
			if (candidates.size() == 1)
			{
				resolvedGltfIdx = *candidates.constBegin();
			}
			else if (!candidates.isEmpty())
			{
				qWarning() << "updateAiSceneWithGltfMaterials: compact aiMaterial" << aiIdx
				           << "maps to multiple glTF materials (count =" << candidates.size() << ")"
				           << "- keeping fallback resolution";
			}
		}

		// Fallback 1: unique material-name match.
		if (resolvedGltfIdx < 0)
		{
			aiString aiName;
			scene->mMaterials[aiIdx]->Get(AI_MATKEY_NAME, aiName);
			const QString name = QString::fromUtf8(aiName.C_Str());
			const auto nameIt = nameToGltfIdx.find(name);
			if (!name.isEmpty() &&
				nameIt != nameToGltfIdx.end() &&
				nameUseCount.value(name) == 1)
			{
				resolvedGltfIdx = nameIt.value();
			}
		}

		// Fallback 2: identity only when it stays inside glTF material space.
		if (resolvedGltfIdx < 0 && aiIdx < gltfMatCount)
			resolvedGltfIdx = aiIdx;

		_aiMatToGltfMat[aiIdx] = resolvedGltfIdx;
	}

	// ===== REINDEX MATERIAL ARRAY TO GLTF ORDER =====
	// Create an array of size gltfMatCount where reindexed[i] = glTF material[i].
	// Start by creating stub aiMaterials (name only) for all gltfMatCount slots,
	// then overwrite each slot with the actual Assimp-loaded aiMaterial.
	aiMaterial** reindexed = new aiMaterial*[gltfMatCount];
	std::vector<bool> slotFilled(gltfMatCount, false);

	for (int i = 0; i < gltfMatCount; ++i)
	{
		aiMaterial* stub = new aiMaterial();
		QString matName = gltfMaterials[i].toObject()["name"].toString(
			QString("material_%1").arg(i));
		aiString sn(matName.toStdString());
		stub->AddProperty(&sn, AI_MATKEY_NAME);
		reindexed[i] = stub;
	}

	// Place each compact aiMaterial at its correct glTF index position.
	for (int aiIdx = 0; aiIdx < aiMatCount; ++aiIdx)
	{
		int gltfIdx = _aiMatToGltfMat[aiIdx];
		if (gltfIdx >= 0 && gltfIdx < gltfMatCount && !slotFilled[gltfIdx])
		{
			delete reindexed[gltfIdx];                  // free the stub
			reindexed[gltfIdx] = scene->mMaterials[aiIdx];
			slotFilled[gltfIdx] = true;
		}
		else
		{
			// Duplicate name or out-of-range — the original aiMaterial is no longer needed.
			delete scene->mMaterials[aiIdx];
		}
	}

	// ===== UPDATE MESH MATERIAL REFERENCES TO GLTF INDICES =====
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
	{
		aiMesh* mesh = scene->mMeshes[m];
		auto it = _aiMatToGltfMat.find(static_cast<int>(mesh->mMaterialIndex));
		if (it != _aiMatToGltfMat.end())
			mesh->mMaterialIndex = static_cast<unsigned int>(it->second);
	}

	// ===== REPLACE SCENE MATERIAL ARRAY =====
	delete[] scene->mMaterials;
	scene->mMaterials    = reindexed;
	scene->mNumMaterials = static_cast<unsigned int>(gltfMatCount);

	qDebug() << "updateAiSceneWithGltfMaterials: Reindexed" << aiMatCount
	         << "compact materials into" << gltfMatCount << "glTF-indexed slots"
	         << "(provenance mapped meshes:" << aiMeshToGltfMaterial.size() << ")";
}
