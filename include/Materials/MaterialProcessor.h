#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <assimp/scene.h>
#include <functional>
#include "Material.h"
#include "SceneMesh.h"
#include "PunctualLights.h"
#include "GltfLightData.h"
#include <QByteArray>
#include <QJsonDocument>
#include <QVector>


class MaterialProcessor
{
public:
	using ImageTextureUploadFn = std::function<unsigned int(Material::Texture&, const QImage&)>;
	using Ktx2TextureUploadFn = std::function<unsigned int(const QString&, const std::string&, Material::Texture&)>;

	MaterialProcessor();
	MaterialProcessor(std::string& folderPath);

	void setFolderPath(const std::string& folderPath) { _folderPath = folderPath; }

	void processAssimpColorAndMaterial(aiMaterial* material, Material& mat);
	void setDefaultMaterial(Material& mat);
	void processAssimpTextureMaps(aiMaterial* material, std::vector<Material::Texture>& textures, Material& mat);
	void setTextureTransforms(const std::vector<Material::Texture>& textures, Material& mat);
	void addExtensionMaps(Material& mat, std::vector<Material::Texture>& textures);
	void clearLoadedTextures() { _loadedTextures.clear(); }

	// GLB cache accessors used by AssImpModelLoader free functions that need
	// per-instance cache data without direct access to private members.
	bool fillAnimDataFromCache(const QString& path, QJsonDocument& outDoc,
	                           QVector<QByteArray>& outBufferData) const;
	void seedGlbCacheIfAbsent(const QString& path, const QJsonDocument& doc,
	                          std::vector<uint8_t> binary);

	/**
	 * Ensures Assimp scene has valid texture data before deep copying or merging.
	 * For GLB files: Populates mTextures from pre-loaded cached data
	 * For other formats: Validates existing texture data
	 *
	 * MUST be called with non-const scene pointer after processGltf2CoreAndExtensions()
	 *
	 * @param scene Mutable Assimp scene to validate/populate
	 * @param sourceFilePath Path to source file (determines handling strategy)
	 */
	void ensureAssimpSceneTexturesValid(aiScene* scene, const QString& sourceFilePath);


	// Checks all material textures of a given type and loads the textures if they're not loaded yet.
	// The required info is returned as a Texture struct.    
	std::vector<Material::Texture> loadMaterialTextures(
		aiMaterial* mat,
		aiTextureType type,
		const std::string& typeName,
		unsigned int slotIndex);

	bool checkImageForAlpha(const QImage& image);

	void synthesizeADSAliases(std::vector<Material::Texture>& textures);

	void processGltf2CoreAndExtensions(
		const QString& gltfPath,
		const aiScene* scene,
		const QString& nodeName,
		const aiMesh* currentMesh,
		int materialIndex,
		Material& outMaterial,
		std::vector<Material::Texture>& outTextures);

	static QString extractJsonFromGLB(const QString& glbPath, std::vector<uint8_t>& outBinaryBuffer);

	void clearGLBCaches();

	QString getGlbCacheDir(const QString& glbPath);

	std::tuple<int, glm::vec2, glm::vec2, float> extractKHRTextureTransform(const QJsonObject& texObj);
	
	GltfLightData parseKHRLightsPunctual(const QString& gltfPath);

	void setImageTextureUploader(ImageTextureUploadFn uploader) { _imageTextureUploader = std::move(uploader); }
	void setKtx2TextureUploader(Ktx2TextureUploadFn uploader) { _ktx2TextureUploader = std::move(uploader); }

private:
	void setShadingModel(Material& mat, aiShadingMode shadingModel);
	void setBlendMode(Material& mat, aiBlendMode blendMode);

	void validateMaterialConsistency(Material& mat);

	unsigned int createTextureOnGPU(Material::Texture& texture,
		const std::vector<uint8_t>* glbBinaryBuffer = nullptr,
		const QJsonArray* jsonBufferViews = nullptr,
		const QJsonArray* jsonImages = nullptr);

	void populateAssimpSceneFromGLBCache(aiScene* scene,
		const QString& glbPath);
	void validateAssimpSceneTextures(aiScene* scene);

	// Returns: pair<success, QImage>
	std::pair<bool, QImage> decodeDataUri(const QString& dataUri);
	bool decodeTextureImage(Material::Texture& texture,
		QImage& outImage,
		bool& outHasAlpha,
		const std::vector<uint8_t>* glbBinaryBuffer = nullptr,
		const QJsonArray* jsonBufferViews = nullptr,
		const QJsonArray* jsonImages = nullptr);

private:
	// Each entry: primary type + uniform name, and an optional fallback type+uniform name
	struct TextureSlotMapping
	{
		aiTextureType primaryType;
		std::string primaryName;
		unsigned int slotIndex;
		aiTextureType fallbackType;
		std::string fallbackName;
		unsigned int fallbackSlotIndex;

		// print for debugging
		friend std::ostream& operator<<(std::ostream& os, const TextureSlotMapping& mapping)
		{
			os << "Primary: " << mapping.primaryType << ", Name: " << mapping.primaryName
				<< ", Slot: " << mapping.slotIndex
				<< ", Fallback: " << mapping.fallbackType << ", Fallback Name: " << mapping.fallbackName
				<< ", Fallback Slot: " << mapping.fallbackSlotIndex;
			return os;
		}
	};

	const std::vector<TextureSlotMapping> textureMappings = {
		// PBR core 
		{ aiTextureType_BASE_COLOR,        "albedoMap",         0, aiTextureType_DIFFUSE,    "texture_diffuse", 0 },
		{ aiTextureType_METALNESS,         "metallicMap",       0, aiTextureType_NONE,       "",                0 },
		{ aiTextureType_DIFFUSE_ROUGHNESS, "roughnessMap",      0, aiTextureType_NONE,       "",                0 },
		{ aiTextureType_NORMALS,           "normalMap",         0, aiTextureType_HEIGHT,     "texture_normal",  0 },
		{ aiTextureType_LIGHTMAP,          "aoMap",             0, aiTextureType_AMBIENT_OCCLUSION, "aoMap",    0 },
		{ aiTextureType_EMISSIVE,          "emissiveMap",       0, aiTextureType_EMISSION_COLOR, "emissiveMap", 0 },
		{ aiTextureType_AMBIENT_OCCLUSION, "aoMap",             0, aiTextureType_LIGHTMAP,   "aoMap",           0 },
		{ aiTextureType_DISPLACEMENT,      "heightMap",         0, aiTextureType_DISPLACEMENT, "texture_height", 0 },
		{ aiTextureType_OPACITY,           "opacityMap",        0, aiTextureType_OPACITY,    "texture_opacity", 0 },
		{ aiTextureType_EMISSION_COLOR,    "emissiveMap",       0, aiTextureType_EMISSIVE,   "texture_emissive", 0 },

		// KHR_materials_clearcoat
		{ aiTextureType_CLEARCOAT,         "clearcoatColorMap",            0, aiTextureType_NONE, "", 0 },
		{ aiTextureType_CLEARCOAT,         "clearcoatRoughnessMap",   1, aiTextureType_NONE, "", 0 },
		{ aiTextureType_CLEARCOAT,         "clearcoatNormalMap",      2, aiTextureType_HEIGHT, "texture_normal", 0 },

		// KHR_materials_sheen
		{ aiTextureType_SHEEN,             "sheenColorMap",     0, aiTextureType_NONE, "", 0 },
		{ aiTextureType_SHEEN,             "sheenRoughnessMap", 1, aiTextureType_NONE, "", 0 },

		// KHR_materials_transmission
		{ aiTextureType_TRANSMISSION,      "transmissionMap",   0, aiTextureType_NONE, "", 0 },

		// KHR_materials_volume
		{ aiTextureType_TRANSMISSION,      "thicknessMap",      1, aiTextureType_NONE, "", 0 },

		// KHR_materials_ior (if mapped to a texture)
		{ aiTextureType_REFLECTION,        "iorMap",            0, aiTextureType_NONE, "", 0 },

		// KHR_materials_specular
		{ aiTextureType_SPECULAR,          "specularFactorMap", 0, aiTextureType_NONE, "", 0 },
		{ aiTextureType_SPECULAR,          "specularColorMap",  1, aiTextureType_NONE, "", 0 },

#ifdef MODELVIEWER_HAVE_ASSIMP_ANISOTROPY
		// KHR_materials_anisotropy
		{ aiTextureType_ANISOTROPY,        "anisotropyMap",     0, aiTextureType_NONE, "", 0 },
#endif

		// KHR_materials_iridescence
		{ aiTextureType_REFLECTION,        "iridescenceMap",    2, aiTextureType_NONE, "", 0 },
		{ aiTextureType_REFLECTION,        "iridescenceThicknessMap", 3, aiTextureType_NONE, "", 0 },

		// Legacy ADS types as fallbacks
		{ aiTextureType_DIFFUSE,           "texture_diffuse",   0, aiTextureType_NONE, "", 0 },
		{ aiTextureType_SPECULAR,          "texture_specular",  0, aiTextureType_NONE, "", 0 },
		{ aiTextureType_HEIGHT,            "texture_normal",    0, aiTextureType_NORMALS, "normalMap", 0 },
		{ aiTextureType_DISPLACEMENT,      "texture_height",    0, aiTextureType_NONE, "", 0 }
	};

	void debugMaterialTextures(aiMaterial* material, const std::string& materialName);

	void extractUVTransform(
		aiMaterial* mat,
		aiTextureType type,
		unsigned int slotIndex,
		Material::Texture& texture);

	void convertSpecularGlossinessToDielectric(Material& mat);


private:
	std::vector<Material::Texture> _loadedTextures;	// Stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
	std::string _folderPath; // Directory where textures are located

	// simple cached JSON per file
	// Caches for both .gltf and .glb
	QHash<QString, QJsonDocument> s_gltfJsonCache;
	QHash<QString, QJsonDocument> s_glbJsonCache;
	QHash<QString, std::vector<uint8_t>> s_glbBinaryCache;
	QHash<QString, bool> s_glbImagesLoaded;  // Track if images uploaded to GPU
	QHash<QString, bool> s_glbScenesSynced;  // Track which scenes have been synced
	QHash<QString, std::vector<size_t>> s_glbImageIndices; // Maps: glbPath -> list of indices in _loadedTextures

	ImageTextureUploadFn _imageTextureUploader;
	Ktx2TextureUploadFn _ktx2TextureUploader;


	QHash<QString, QString> s_glbCachedTexturePaths; // key: "glb://<file>::image_X" value: "C:/temp/cache/model/image_X.png"
};
