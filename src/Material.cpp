#include "Material.h"
#include <cmath>
#include <QVector>
#include <iostream>
#include <iomanip>
#include <string>
#include <limits>

Material::Material()
{
	*this = DEFAULT_MAT();
	// Sensible defaults (e.g., glTF ORM: O=R, R=G, M=B)
	_metallicPacking = { 2, false, 1.0f, 0.0f }; // B
	_roughnessPacking = { 1, false, 1.0f, 0.0f }; // G
	_aoPacking = { 0, false, 1.0f, 0.0f }; // R
	_opacityPacking = { 3, false, 1.0f, 0.0f }; // A (common), tweak if assets prefer R
}

Material::Material(QVector3D ambient, QVector3D diffuse, QVector3D specular, QVector3D emissive, float shininess, bool metallic, float opacity)
{
	_ambient.setX(std::clamp(ambient.x(), 0.0f, 1.0f));
	_ambient.setY(std::clamp(ambient.y(), 0.0f, 1.0f));
	_ambient.setZ(std::clamp(ambient.z(), 0.0f, 1.0f));

	_diffuse.setX(std::clamp(diffuse.x(), 0.0f, 1.0f));
	_diffuse.setY(std::clamp(diffuse.y(), 0.0f, 1.0f));
	_diffuse.setZ(std::clamp(diffuse.z(), 0.0f, 1.0f));

	_specular.setX(std::clamp(specular.x(), 0.0f, 1.0f));
	_specular.setY(std::clamp(specular.y(), 0.0f, 1.0f));
	_specular.setZ(std::clamp(specular.z(), 0.0f, 1.0f));

	_emissive.setX(std::clamp(emissive.x(), 0.0f, 1.0f));
	_emissive.setY(std::clamp(emissive.y(), 0.0f, 1.0f));
	_emissive.setZ(std::clamp(emissive.z(), 0.0f, 1.0f));

	_shininess = shininess;
	_metallic = metallic;
	_opacity = opacity;
	_metalness = metallic ? 1.0f : 0.0f;
	_roughness = 0.5f;
	_albedoColor = diffuse; // Use diffuse as albedo color
	_emissiveStrength = 1.0f; // Default emissive strength
	_ior = 1.5f; // Default index of refraction
	_clearcoat = 0.0f; // Default clearcoat
	_clearcoatRoughness = 0.0f; // Default clearcoat roughness
	_sheenColor = QVector3D(0.0f, 0.0f, 0.0f); // Default sheen color
	_sheenRoughness = 0.0f; // Default
	_transmission = 0.0f; // Default transmission
	_shadingModel = ShadingModel::BlinnPhong; // Default shading model
	_blendMode = BlendMode::Opaque; // Default blend mode
	_twoSided = true; // Default two-sided rendering
	_wireframe = false; // Default wireframe rendering
	_alphaThreshold = 0.5f; // Default alpha threshold for masked blend mode

	// Sensible defaults (e.g., glTF ORM: O=R, R=G, M=B)
	_metallicPacking = { 2, false, 1.0f, 0.0f }; // B
	_roughnessPacking = { 1, false, 1.0f, 0.0f }; // G
	_aoPacking = { 0, false, 1.0f, 0.0f }; // R
	_opacityPacking = { 3, false, 1.0f, 0.0f }; // A (common), tweak if assets prefer R

	// Ensure all values are within valid ranges
	clampValues();
	// Fix shininess (0 to 128 for OpenGL)
	_shininess = std::clamp(_shininess, 0.0f, 128.0f);
	// Set albedo from diffuse
	setAlbedoFromADS();
	// Update consistency between legacy and PBR properties
	updateConsistency();
}

Material::Material(QVector3D albedo, float metalness, float roughness, float opacity)
{
	_albedoColor.setX(std::clamp(albedo.x(), 0.0f, 1.0f));
	_albedoColor.setY(std::clamp(albedo.y(), 0.0f, 1.0f));
	_albedoColor.setZ(std::clamp(albedo.z(), 0.0f, 1.0f));

	_metalness = metalness;
	_roughness = roughness;
	_opacity = opacity;
	_metallic = _metalness > 0.5f ? true : false;
	_shininess = 125 * _metalness;
	_ambient = _albedoColor * 0.2f; // Set ambient to a fraction of albedo
	_diffuse = _albedoColor; // Use albedo as diffuse color
	_specular = QVector3D(0.04f, 0.04f, 0.04f); // Default specular for dielectrics
	_emissive = QVector3D(0.0f, 0.0f, 0.0f); // Default emissive
	_emissiveStrength = 1.0f; // Default emissive strength
	_ior = 1.5f; // Default index of refraction
	_clearcoat = 0.0f; // Default clearcoat
	_clearcoatRoughness = 0.0f; // Default clearcoat roughness
	_sheenColor = QVector3D(0.0f, 0.0f, 0.0f); // Default sheen color
	_sheenRoughness = 0.0f; // Default
	_transmission = 0.0f; // Default transmission
	_shadingModel = ShadingModel::PBR; // Default shading model
	_blendMode = BlendMode::Opaque; // Default blend mode
	_twoSided = true; // Default two-sided rendering
	_wireframe = false; // Default wireframe rendering

	// Sensible defaults (e.g., glTF ORM: O=R, R=G, M=B)
	_metallicPacking = { 2, false, 1.0f, 0.0f }; // B
	_roughnessPacking = { 1, false, 1.0f, 0.0f }; // G
	_aoPacking = { 0, false, 1.0f, 0.0f }; // R
	_opacityPacking = { 3, false, 1.0f, 0.0f }; // A (common), tweak if assets prefer R

	clampValues();
	// Fix shininess (0 to 128 for OpenGL)
	_shininess = std::clamp(_shininess, 0.0f, 128.0f);
	// Set albedo from diffuse
	setAlbedoFromADS();
	updateConsistency();
}

Material::~Material()
{
}

// ============================================================================
// Unified Texture Setters/Getters
// ============================================================================

void Material::setTexture(TextureType type, const Texture& texture)
{
	Q_ASSERT(static_cast<size_t>(type) < static_cast<size_t>(TextureType::Count));
	const std::string previousPath = _textures[static_cast<size_t>(type)].path;
	_textures[static_cast<size_t>(type)] = texture;
	syncTextureParameters();

	// Keep packable single-channel maps consistent even when callers use the
	// newer typed texture API instead of the older setXxxMap(path) helpers.
	// Without this, standalone opacity maps can keep the default alpha-channel
	// packing and appear to have no effect when authored as grayscale RGB.
	switch (type)
	{
	case TextureType::AmbientOcclusion:
	case TextureType::Roughness:
	case TextureType::Metallic:
	case TextureType::Opacity:
		if (texture.path != previousPath)
			assignAutoPackingForPath(QString::fromStdString(texture.path));
		break;
	default:
		break;
	}
}

const Material::Texture& Material::texture(TextureType type) const
{
	Q_ASSERT(static_cast<size_t>(type) < static_cast<size_t>(TextureType::Count));
	return _textures[static_cast<size_t>(type)];
}

Material::Texture& Material::texture(TextureType type)
{
	Q_ASSERT(static_cast<size_t>(type) < static_cast<size_t>(TextureType::Count));
	return _textures[static_cast<size_t>(type)];
}

Material::TextureTransform Material::textureTransform(TextureType type) const
{
	switch (type)
	{
	case TextureType::Albedo:                   return _albedoTexTransform;
	case TextureType::Metallic:                 return _metallicTexTransform;
	case TextureType::Roughness:                return _roughnessTexTransform;
	case TextureType::Normal:                   return _normalTexTransform;
	case TextureType::AmbientOcclusion:         return _occlusionTexTransform;
	case TextureType::Opacity:                  return _opacityTexTransform;
	case TextureType::Emissive:                 return _emissiveTexTransform;
	case TextureType::Height:                   return _heightTexTransform;
	case TextureType::Transmission:              return _transmissionTexTransform;
	case TextureType::IOR:                      return _iorTexTransform;
	case TextureType::SheenColor:                return _sheenColorTexTransform;
	case TextureType::SheenRoughness:            return _sheenRoughnessTexTransform;
	case TextureType::ClearcoatColor:            return _clearcoatColorTexTransform;
	case TextureType::ClearcoatRoughness:        return _clearcoatRoughnessTexTransform;
	case TextureType::ClearcoatNormal:           return _clearcoatNormalTexTransform;
	case TextureType::Iridescence:                return _iridescenceTexTransform;
	case TextureType::IridescenceThickness:      return _iridescenceThicknessTexTransform;
	case TextureType::SpecularFactor:            return _specularFactorTexTransform;
	case TextureType::SpecularColor:             return _specularColorTexTransform;
	case TextureType::Anisotropy:                return _anisotropyTexTransform;
	case TextureType::DiffuseTransmission:       return _diffuseTransmissionTexTransform;
	case TextureType::DiffuseTransmissionColor:  return _diffuseTransmissionColorTexTransform;
	case TextureType::Thickness:                 return _thicknessTexTransform;
	case TextureType::Diffuse:                   return _diffuseTexTransform;
	case TextureType::SpecularGlossiness:        return _specularGlossinessTexTransform;
	case TextureType::Count:
	default:                                     return TextureTransform();
	}
}

// ============================================================================
// TextureType to String Helper
// ============================================================================
QString Material::textureTypeToString(TextureType type)
{
	switch (type)
	{
	case TextureType::Albedo:                   return "Albedo";
	case TextureType::Metallic:                 return "Metallic";
	case TextureType::Roughness:                return "Roughness";
	case TextureType::Normal:                   return "Normal";
	case TextureType::AmbientOcclusion:         return "AmbientOcclusion";
	case TextureType::Opacity:                  return "Opacity";
	case TextureType::Emissive:                 return "Emissive";
	case TextureType::Height:                   return "Height";
	case TextureType::Transmission:             return "Transmission";
	case TextureType::IOR:                      return "IOR";
	case TextureType::SheenColor:               return "SheenColor";
	case TextureType::SheenRoughness:           return "SheenRoughness";
	case TextureType::ClearcoatColor:           return "ClearcoatColor";
	case TextureType::ClearcoatRoughness:       return "ClearcoatRoughness";
	case TextureType::ClearcoatNormal:          return "ClearcoatNormal";
	case TextureType::Iridescence:              return "Iridescence";
	case TextureType::IridescenceThickness:     return "IridescenceThickness";
	case TextureType::SpecularFactor:           return "SpecularFactor";
	case TextureType::SpecularColor:            return "SpecularColor";
	case TextureType::Anisotropy:               return "Anisotropy";
	case TextureType::DiffuseTransmission:      return "DiffuseTransmission";
	case TextureType::DiffuseTransmissionColor: return "DiffuseTransmissionColor";
	case TextureType::Thickness:                return "Thickness";
	case TextureType::Diffuse:                  return "Diffuse";
	case TextureType::SpecularGlossiness:       return "SpecularGlossiness";
	case TextureType::Count:
	default:                                    return "Unknown";
	}
}

// ============================================================================
// String to TextureType Helper
// ============================================================================
Material::TextureType Material::stringToTextureType(const QString& name)
{
	if (name == "Albedo")                   return TextureType::Albedo;
	if (name == "Metallic")                 return TextureType::Metallic;
	if (name == "Roughness")                return TextureType::Roughness;
	if (name == "Normal")                   return TextureType::Normal;
	if (name == "AmbientOcclusion")         return TextureType::AmbientOcclusion;
	if (name == "Opacity")                  return TextureType::Opacity;
	if (name == "Emissive")                 return TextureType::Emissive;
	if (name == "Height")                   return TextureType::Height;
	if (name == "Transmission")             return TextureType::Transmission;
	if (name == "IOR")                      return TextureType::IOR;
	if (name == "SheenColor")               return TextureType::SheenColor;
	if (name == "SheenRoughness")           return TextureType::SheenRoughness;
	if (name == "ClearcoatColor")           return TextureType::ClearcoatColor;
	if (name == "ClearcoatRoughness")       return TextureType::ClearcoatRoughness;
	if (name == "ClearcoatNormal")          return TextureType::ClearcoatNormal;
	if (name == "Iridescence")              return TextureType::Iridescence;
	if (name == "IridescenceThickness")     return TextureType::IridescenceThickness;
	if (name == "SpecularFactor")           return TextureType::SpecularFactor;
	if (name == "SpecularColor")            return TextureType::SpecularColor;
	if (name == "Anisotropy")               return TextureType::Anisotropy;
	if (name == "DiffuseTransmission")      return TextureType::DiffuseTransmission;
	if (name == "DiffuseTransmissionColor") return TextureType::DiffuseTransmissionColor;
	if (name == "Thickness")                return TextureType::Thickness;
	if (name == "Diffuse")                  return TextureType::Diffuse;
	if (name == "SpecularGlossiness")       return TextureType::SpecularGlossiness;

	return TextureType::Count;
}


void Material::setAmbient(const QVector3D& ambient)
{
	_ambient.setX(std::clamp(ambient.x(), 0.0f, 1.0f));
	_ambient.setY(std::clamp(ambient.y(), 0.0f, 1.0f));
	_ambient.setZ(std::clamp(ambient.z(), 0.0f, 1.0f));
}


void Material::setDiffuse(const QVector3D& diffuse)
{
	_diffuse.setX(std::clamp(diffuse.x(), 0.0f, 1.0f));
	_diffuse.setY(std::clamp(diffuse.y(), 0.0f, 1.0f));
	_diffuse.setZ(std::clamp(diffuse.z(), 0.0f, 1.0f));
}


void Material::setSpecular(const QVector3D& specular)
{
	_specular.setX(std::clamp(specular.x(), 0.0f, 1.0f));
	_specular.setY(std::clamp(specular.y(), 0.0f, 1.0f));
	_specular.setZ(std::clamp(specular.z(), 0.0f, 1.0f));
}


void Material::setEmissive(const QVector3D& emissive)
{
	_emissive.setX(std::clamp(emissive.x(), 0.0f, 1.0f));
	_emissive.setY(std::clamp(emissive.y(), 0.0f, 1.0f));
	_emissive.setZ(std::clamp(emissive.z(), 0.0f, 1.0f));
}


void Material::setShininess(float shininess)
{
	_shininess = shininess;
}



void Material::setMetallic(bool metallic)
{
	_metallic = metallic;
}

void Material::setAlbedoColor(const QVector3D& albedoColor)
{
	_albedoColor.setX(std::clamp(albedoColor.x(), 0.0f, 1.0f));
	_albedoColor.setY(std::clamp(albedoColor.y(), 0.0f, 1.0f));
	_albedoColor.setZ(std::clamp(albedoColor.z(), 0.0f, 1.0f));
}



void Material::setMetalness(float metalness)
{
	_metalness = metalness;
}


void Material::setRoughness(float roughness)
{
	_roughness = roughness;
}


void Material::setOpacity(float opacity)
{
	_opacity = opacity;	
}

Material Material::getPredefinedMaterial(Material::PredefinedMaterials type)
{
	switch (type)
	{
	case PredefinedMaterials::BRASS:
		return METAL_BRASS();
		break;
	case PredefinedMaterials::BRONZE:
		return METAL_BRONZE();
		break;
	case PredefinedMaterials::COPPER:
		return METAL_COPPER();
		break;
	case PredefinedMaterials::GOLD:
		return METAL_GOLD();
		break;
	case PredefinedMaterials::SILVER:
		return METAL_SILVER();
		break;
	case PredefinedMaterials::CHROME:
		return METAL_CHROME();
		break;
	case PredefinedMaterials::RUBY:
		return STONE_RUBY();
		break;
	case PredefinedMaterials::EMERALD:
		return STONE_EMERALD();
		break;
	case PredefinedMaterials::TURQUOISE:
		return STONE_TURQUOISE();
		break;
	case PredefinedMaterials::PEARL:
		return STONE_PEARL();
		break;
	case PredefinedMaterials::JADE:
		return STONE_JADE();
		break;
	case PredefinedMaterials::OBSIDIAN:
		return STONE_OBSIDIAN();
		break;
	case PredefinedMaterials::RED_PLASTIC:
		return RED_PLASTIC();
		break;
	case PredefinedMaterials::GREEN_PLASTIC:
		return GREEN_PLASTIC();
		break;
	case PredefinedMaterials::CYAN_PLASTIC:
		return CYAN_PLASTIC();
		break;
	case PredefinedMaterials::YELLOW_PLASTIC:
		return YELLOW_PLASTIC();
		break;
	case PredefinedMaterials::WHITE_PLASTIC:
		return WHITE_PLASTIC();
		break;
	case PredefinedMaterials::BLACK_PLASTIC:
		return BLACK_PLASTIC();
		break;
	case PredefinedMaterials::RED_RUBBER:
		return RED_RUBBER();
		break;
	case PredefinedMaterials::GREEN_RUBBER:
		return GREEN_RUBBER();
		break;
	case PredefinedMaterials::CYAN_RUBBER:
		return CYAN_RUBBER();
		break;
	case PredefinedMaterials::YELLOW_RUBBER:
		return YELLOW_RUBBER();
		break;
	case PredefinedMaterials::WHITE_RUBBER:
		return WHITE_RUBBER();
		break;
	case PredefinedMaterials::BLACK_RUBBER:
		return BLACK_RUBBER();
		break;
	default:
		return DEFAULT_MAT();
		break;
	}
}

Material Material::METAL_BRASS()
{
	Material mat({ 0.329412f, 0.223529f, 0.027451f },
		{ 0.780392f, 0.568627f, 0.113725f },
		{ 0.992157f, 0.941176f, 0.807843f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.21794872),
		true,
		1.0f);

	// Corrected PBR properties for brass
	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.3f); // Brass is typically smoother than 0.65
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(0.47f); // Brass complex IOR (real part)

	return mat;
}

Material Material::METAL_BRONZE()
{
	Material mat({ 0.2125f, 0.1275f, 0.054f },
		{ 0.714f, 0.4284f, 0.18144f },
		{ 0.393548f, 0.271906f, 0.166721f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.2),
		true,
		1.0f);

	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.4f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.18f);

	return mat;
}

Material Material::METAL_COPPER()
{
	Material mat({ 0.19125f, 0.0735f, 0.0225f },
		{ 0.7038f, 0.27048f, 0.0828f },
		{ 0.256777f, 0.137622f, 0.086014f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.1),
		true,
		1.0f);

	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.25f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(0.617f);

	return mat;
}

Material Material::METAL_GOLD()
{
	Material mat({ 0.24725f, 0.1995f, 0.0745f },
		{ 0.75164f, 0.60648f, 0.22648f },
		{ 0.628281f, 0.555802f, 0.366065f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.4),
		true,
		1.0f);

	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.1f); // Gold is very smooth when polished
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(0.47f);

	return mat;
}

Material Material::METAL_SILVER()
{
	Material mat({ 0.19225f, 0.19225f, 0.19225f },
		{ 0.50754f, 0.50654f, 0.50754f },
		{ 0.508273f, 0.508273f, 0.508273f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.4),
		true,
		1.0f);

	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.05f); // Silver is very reflective
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(0.155f);

	return mat;
}

Material Material::METAL_CHROME()
{
	Material mat({ 0.25f, 0.25f, 0.25f },
		{ 0.4f, 0.4f, 0.4f },
		{ 0.774597f, 0.774597f, 0.774597f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		true,
		1.0f);

	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.02f); // Chrome is extremely smooth
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(4.1f);

	return mat;
}

Material Material::METAL_STEEL()
{
	Material mat({ 0.25f, 0.25f, 0.25f },
		{ 0.4f, 0.4f, 0.4f },
		{ 0.774597f, 0.774597f, 0.774597f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		true,
		1.0f);

	mat.setAlbedoColor(mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.25f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setOpacity(1.0f);
	mat.setTransmission(0.0f);

	return mat;
}

// ----------------- Brushed Metals -----------------
Material Material::BRUSHED_ALUMINUM()
{
	Material mat({ 0.06f, 0.06f, 0.06f },      // ambient
		{ 0.7f, 0.7f, 0.7f },         // diffuse
		{ 0.5f, 0.5f, 0.5f },         // specular
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.6f),
		true, 1.0f);

	mat.setAlbedoColor(QVector3D(0.7f, 0.7f, 0.7f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.35f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::BRUSHED_STEEL()
{
	Material mat({ 0.05f, 0.05f, 0.05f },
		{ 0.55f, 0.55f, 0.55f },
		{ 0.45f, 0.45f, 0.45f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.55f),
		true, 1.0f);

	mat.setAlbedoColor(QVector3D(0.55f, 0.55f, 0.55f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.45f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// --------------------- Stone Materials ---------------------
Material Material::STONE_RUBY()
{
	Material mat({ 0.17450f, 0.01175f, 0.01175f },
		{ 0.61424f, 0.04136f, 0.04136f },
		{ 0.727811f, 0.626959f, 0.626959f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.7f, 0.1f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f); // Gemstones are very smooth
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.77f); // Ruby IOR
	mat.setTransmission(0.1f); // Slight translucency

	return mat;
}

Material Material::STONE_EMERALD()
{
	Material mat({ 0.0215f, 0.1745f, 0.0215f },
		{ 0.07568f, 0.61424f, 0.07568f },
		{ 0.633000f, 0.727811f, 0.633000f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.1f, 0.7f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.57f); // Emerald IOR
	mat.setTransmission(0.15f);

	return mat;
}

Material Material::STONE_TURQUOISE()
{
	Material mat({ 0.1f, 0.18725f, 0.1745f },
		{ 0.396f, 0.74151f, 0.69102f },
		{ 0.297254f, 0.30829f, 0.306678f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.1),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.4f, 0.8f, 0.7f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.3f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.65f);

	return mat;
}

Material Material::STONE_PEARL()
{
	Material mat({ 0.25000f, 0.20725f, 0.20725f },
		{ 1.000f, 0.829f, 0.829f },
		{ 0.296648f, 0.296648f, 0.299948f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.088),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(1.0f, 0.95f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.53f);
	mat.setTransmission(0.05f); // Slight translucency
	mat.setSheenColor(QVector3D(0.8f, 0.8f, 0.9f)); // Pearl luster
	mat.setSheenRoughness(0.1f);

	return mat;
}

Material Material::STONE_JADE()
{
	Material mat({ 0.135f, 0.2225f, 0.1575f },
		{ 0.54f, 0.89f, 0.63f },
		{ 0.316228f, 0.316228f, 0.316228f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.1),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.4f, 0.8f, 0.4f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.15f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.66f);
	mat.setTransmission(0.2f);

	return mat;
}

Material Material::STONE_OBSIDIAN()
{
	Material mat({ 0.05375f, 0.05f, 0.06625f },
		{ 0.18275f, 0.17f, 0.22525f },
		{ 0.332741f, 0.328634f, 0.346435f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.3),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.1f, 0.1f, 0.12f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.05f); // Obsidian is very smooth
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.48f);

	return mat;
}

// Plastic materials - corrected roughness values
Material Material::RED_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.5f, 0.0f, 0.0f },
		{ 0.7f, 0.6f, 0.6f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.8f, 0.1f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f); // Plastics are typically rougher
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);

	return mat;
}

Material Material::GREEN_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.1f, 0.35f, 0.1f },
		{ 0.45f, 0.55f, 0.45f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.1f, 0.8f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);

	return mat;
}

Material Material::BLUE_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.5f },
		{ 0.6f, 0.6f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(QVector3D(0.1f, 0.1f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);
	return mat;
}

Material Material::CYAN_PLASTIC()
{
	Material mat({ 0.0f, 0.1f, 0.06f },
		{ 0.0f, 0.50980392f, 0.50980392f },
		{ 0.50196078f, 0.50196078f, 0.50196078f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.1f, 0.8f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);

	return mat;
}

Material Material::YELLOW_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.5f, 0.5f, 0.0f },
		{ 0.6f, 0.6f, 0.5f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.8f, 0.8f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);

	return mat;
}

Material Material::MAGENTA_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.5f, 0.0f, 0.5f },
		{ 0.6f, 0.5f, 0.6f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(QVector3D(0.8f, 0.1f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);
	return mat;
}

Material Material::WHITE_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.55f, 0.55f, 0.55f },
		{ 0.7f, 0.7f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);

	return mat;
}

Material Material::BLACK_PLASTIC()
{
	Material mat({ 0.0f, 0.0f, 0.0f },
		{ 0.01f, 0.01f, 0.01f },
		{ 0.5f, 0.5f, 0.5f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.05f, 0.05f, 0.05f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.46f);

	return mat;
}

// Rubber materials - keep high roughness
Material Material::RED_RUBBER()
{
	Material mat({ 0.05f, 0.0f, 0.0f },
		{ 0.7f, 0.4f, 0.4f },
		{ 0.7f, 0.04f, 0.04f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.8f, 0.1f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f); // Rubber is very rough
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);

	return mat;
}

Material Material::GREEN_RUBBER()
{
	Material mat({ 0.0f, 0.05f, 0.0f },
		{ 0.4f, 0.5f, 0.4f },
		{ 0.04f, 0.7f, 0.04f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.1f, 0.8f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);

	return mat;
}

Material Material::BLUE_RUBBER()
{
	Material mat({ 0.0f, 0.0f, 0.05f },
		{ 0.4f, 0.4f, 0.5f },
		{ 0.04f, 0.04f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(QVector3D(0.1f, 0.1f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);
	return mat;
}

Material Material::CYAN_RUBBER()
{
	Material mat({ 0.0f, 0.05f, 0.05f },
		{ 0.4f, 0.5f, 0.5f },
		{ 0.04f, 0.7f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.1f, 0.8f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);

	return mat;
}

Material Material::YELLOW_RUBBER()
{
	Material mat({ 0.05f, 0.05f, 0.0f },
		{ 0.5f, 0.5f, 0.4f },
		{ 0.7f, 0.7f, 0.04f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.8f, 0.8f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);

	return mat;
}

Material Material::MAGENTA_RUBBER()
{
	Material mat({ 0.05f, 0.0f, 0.05f },
		{ 0.5f, 0.4f, 0.5f },
		{ 0.7f, 0.04f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(QVector3D(0.8f, 0.1f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);
	return mat;
}

Material Material::WHITE_RUBBER()
{
	Material mat({ 0.05f, 0.05f, 0.05f },
		{ 0.5f, 0.5f, 0.5f },
		{ 0.7f, 0.7f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);

	return mat;
}

Material Material::BLACK_RUBBER()
{
	Material mat({ 0.02f, 0.02f, 0.02f },
		{ 0.01f, 0.01f, 0.01f },
		{ 0.4f, 0.4f, 0.4f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);

	mat.setAlbedoColor(QVector3D(0.05f, 0.05f, 0.05f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setIOR(1.52f);

	return mat;
}

// === NEW ADDITIONAL MATERIALS ===

Material Material::GLASS()
{
	Material mat({ 0.02f, 0.02f, 0.02f },         // ambient - very low
		{ 0.1f, 0.1f, 0.1f },             // diffuse - low for transparency
		{ 0.9f, 0.9f, 0.9f },             // specular - high reflectivity
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.8f),              // shininess - very shiny
		false,                            // metallic
		0.1f);                           // opacity - transparent

	mat.setAlbedoColor(QVector3D(0.95f, 0.95f, 0.95f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.05f);
	mat.setTransmission(0.9f);
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();
	return mat;
}

Material Material::WATER()
{
	Material mat({ 0.01f, 0.02f, 0.04f },         // ambient - very low, slight blue tint
		{ 0.1f, 0.3f, 0.5f },             // diffuse - blue tint
		{ 0.8f, 0.9f, 1.0f },             // specular - high with blue tint
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.9f),              // shininess - very reflective
		false,                            // metallic
		0.2f);                           // opacity - mostly transparent

	mat.setAlbedoColor(QVector3D(0.3f, 0.7f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.02f);
	mat.setTransmission(0.8f);
	mat.setIOR(1.33f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();
	return mat;
}

Material Material::DIAMOND()
{
	Material mat({ 0.02f, 0.02f, 0.02f },         // ambient - very low
		{ 0.9f, 0.9f, 0.9f },             // diffuse - high reflectivity
		{ 1.0f, 1.0f, 1.0f },             // specular - maximum reflectivity
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.95f),             // shininess - extremely shiny
		false,                            // metallic
		0.8f);                           // opacity - semi-transparent

	mat.setAlbedoColor(QVector3D(0.98f, 0.98f, 0.98f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.01f);
	mat.setTransmission(0.2f);
	mat.setIOR(2.42f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();
	return mat;
}

Material Material::CERAMIC()
{
	Material mat({ 0.16f, 0.16f, 0.16f },         // ambient
		{ 0.8f, 0.8f, 0.8f },             // diffuse
		{ 0.6f, 0.6f, 0.6f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.3f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setIOR(1.62f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::FABRIC()
{
	Material mat({ 0.03f, 0.02f, 0.016f },        // ambient
		{ 0.6f, 0.4f, 0.3f },             // diffuse
		{ 0.1f, 0.1f, 0.1f },             // specular - very low for matte fabric
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.05f),             // shininess - very low
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.7f, 0.5f, 0.4f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.8f);
	mat.setIOR(1.46f);
	mat.setSheenColor(QVector3D(0.8f, 0.8f, 0.8f));
	mat.setSheenRoughness(0.2f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::SKIN()
{
	Material mat({ 0.16f, 0.12f, 0.08f },         // ambient - warm tones
		{ 0.8f, 0.6f, 0.4f },             // diffuse - skin tone
		{ 0.3f, 0.3f, 0.3f },             // specular - moderate reflectivity
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.15f),             // shininess - slight sheen
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.9f, 0.7f, 0.5f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setTransmission(0.1f);
	mat.setIOR(1.4f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::PAPER()
{
	Material mat({ 0.27f, 0.27f, 0.255f },        // ambient - high for diffuse material
		{ 0.9f, 0.9f, 0.85f },            // diffuse - off-white
		{ 0.05f, 0.05f, 0.05f },          // specular - very low for matte
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.063f),            // shininess - very low
		false,                            // metallic
		0.85f);                          // opacity - slightly translucent

	mat.setAlbedoColor(QVector3D(0.95f, 0.95f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setTransmission(0.05f);
	mat.setIOR(1.3f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD()
{
	Material mat({ 0.12f, 0.08f, 0.04f },         // ambient - brown wood tones
		{ 0.6f, 0.4f, 0.2f },             // diffuse - wood color
		{ 0.1f, 0.1f, 0.1f },             // specular - low reflectivity
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.25f),             // shininess - moderate
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.6f, 0.4f, 0.2f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);
	mat.setIOR(1.4f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_BAMBOO()
{
	Material mat({ 0.10f, 0.09f, 0.06f },   // ambient - pale yellow tint
		{ 0.85f, 0.78f, 0.55f },              // diffuse - light yellowish
		{ 0.12f, 0.12f, 0.10f },              // specular - low
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.20f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.85f, 0.78f, 0.55f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setIOR(1.4f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_CEDAR()
{
	Material mat({ 0.10f, 0.05f, 0.04f },   // reddish tint
		{ 0.65f, 0.28f, 0.20f },              // diffuse - warm red-brown
		{ 0.15f, 0.12f, 0.10f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.25f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.65f, 0.28f, 0.20f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.65f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_REDWOOD()
{
	Material mat({ 0.12f, 0.05f, 0.03f },
		{ 0.72f, 0.25f, 0.18f },              // redder
		{ 0.15f, 0.12f, 0.10f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.30f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.72f, 0.25f, 0.18f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.65f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_OAK()
{
	Material mat({ 0.12f, 0.10f, 0.07f },
		{ 0.65f, 0.52f, 0.35f },              // warm golden brown
		{ 0.14f, 0.14f, 0.12f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.22f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.65f, 0.52f, 0.35f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);
	mat.setIOR(1.42f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_PINE()
{
	Material mat({ 0.11f, 0.10f, 0.08f },
		{ 0.90f, 0.80f, 0.55f },              // pale yellowish
		{ 0.12f, 0.12f, 0.10f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.18f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.90f, 0.80f, 0.55f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setIOR(1.39f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_BIRCH()
{
	Material mat({ 0.12f, 0.12f, 0.11f },
		{ 0.95f, 0.87f, 0.70f },              // creamy white
		{ 0.14f, 0.14f, 0.12f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.20f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.95f, 0.87f, 0.70f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.65f);
	mat.setIOR(1.39f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_WALNUT()
{
	Material mat({ 0.08f, 0.06f, 0.05f },
		{ 0.35f, 0.22f, 0.12f },              // dark brown
		{ 0.12f, 0.12f, 0.10f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.28f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.35f, 0.22f, 0.12f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_CHERRY()
{
	Material mat({ 0.10f, 0.06f, 0.05f },
		{ 0.70f, 0.30f, 0.25f },              // reddish brown
		{ 0.15f, 0.12f, 0.10f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.26f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.70f, 0.30f, 0.25f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.65f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_TEAK()
{
	Material mat({ 0.10f, 0.08f, 0.05f },
		{ 0.55f, 0.38f, 0.20f },              // golden medium brown
		{ 0.14f, 0.12f, 0.10f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.24f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.55f, 0.38f, 0.20f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.65f);
	mat.setIOR(1.44f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::WOOD_MAPLE()
{
	Material mat({ 0.12f, 0.11f, 0.09f },
		{ 0.88f, 0.77f, 0.58f },              // light creamy brown
		{ 0.14f, 0.14f, 0.12f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.22f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.88f, 0.77f, 0.58f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);
	mat.setIOR(1.41f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}


Material Material::METAL()
{
	Material mat({ 0.04f, 0.04f, 0.04f },         // ambient - low for metal
		{ 0.2f, 0.2f, 0.2f },             // diffuse - low for metal
		{ 0.8f, 0.8f, 0.8f },             // specular - high reflectivity
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 1.0f),              // shininess - maximum
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.7f, 0.7f, 0.7f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.1f);
	mat.setIOR(2.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::PLASTIC()
{
	Material mat({ 0.12f, 0.12f, 0.16f },         // ambient
		{ 0.6f, 0.6f, 0.8f },             // diffuse
		{ 0.5f, 0.5f, 0.5f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 1.0f),              // shininess - high for glossy plastic
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.6f, 0.6f, 0.8f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.3f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE()
{
	Material mat({ 0.1f, 0.1f, 0.1f },    // ambient
		{ 0.5f, 0.5f, 0.5f },				// diffuse
		{ 0.05f, 0.05f, 0.05f },			// specular - very low
		{ 0.0f, 0.0f, 0.0f },				// emissive
		fabs(128.0f * 0.063f),				// shininess - very dull
		false,								// metallic
		1.0f);								// opacity

	mat.setAlbedoColor(QVector3D(0.5f, 0.5f, 0.5f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::MIRROR_SILVER()
{
	Material mat(
		QVector3D(0.06f, 0.06f, 0.06f),     // ambient (small - scene/IBL will dominate)
		QVector3D(0.02f, 0.02f, 0.02f),     // diffuse (metals have ~no diffuse; tiny placeholder)
		QVector3D(0.95f, 0.94f, 0.90f),     // specular (F0-like for silver)
		QVector3D(0.0f, 0.0f, 0.0f),        // emissive
		fabs(128.0f),                       // shininess (maxed; legacy ADS)
		true,                               // metallic
		1.0f);                              // opacity

	// PBR canonical
	mat.setAlbedoColor(QVector3D(0.95f, 0.94f, 0.90f));  // silver F0-ish color
	mat.setMetalness(1.0f);
	mat.setRoughness(0.02f);                             // very polished
	mat.setIOR(2.50f);                                   // not critical for metals, reasonable value
	mat.setTransmission(0.0f);

	// optional extras (off by default)
	mat.setClearcoat(0.0f);
	mat.setClearcoatRoughness(0.03f);
	mat.setSheenColor(QVector3D(0.0f, 0.0f, 0.0f));
	mat.setSheenRoughness(0.0f);

	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Opaque);
	mat.setTwoSided(false);

	mat.updateConsistency();
	return mat;
}

// === CLEARCOAT MATERIALS ===
Material Material::CAR_PAINT_RED()
{
	Material mat({ 0.1f, 0.02f, 0.02f },	// ambient
		{ 0.8f, 0.1f, 0.1f },				// diffuse - bright red base
		{ 0.9f, 0.9f, 0.9f },				// specular - high for clearcoat
		{ 0.0f, 0.0f, 0.0f },				// emissive
		fabs(128.0f * 0.9f),				// shininess - very high
		false,								// metallic
		1.0f);								// opacity

	// PBR properties with clearcoat
	mat.setAlbedoColor(QVector3D(0.8f, 0.1f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.4f);                          // Base material roughness
	mat.setClearcoat(1.0f);                          // Full clearcoat layer
	mat.setClearcoatRoughness(0.05f);                // Very smooth clearcoat
	mat.setIOR(1.5f);                               // Standard automotive clearcoat
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::CAR_PAINT_METALLIC_BLUE()
{
	Material mat({ 0.02f, 0.05f, 0.15f },         // ambient
		{ 0.1f, 0.3f, 0.8f },             // diffuse - metallic blue
		{ 0.9f, 0.9f, 0.9f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.85f),             // shininess
		false,                            // metallic base, but clearcoated
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.2f, 0.4f, 0.9f));
	mat.setMetalness(0.3f);                          // Slightly metallic base
	mat.setRoughness(0.2f);
	mat.setClearcoat(0.8f);                          // Strong clearcoat
	mat.setClearcoatRoughness(0.02f);                // Very smooth clearcoat
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::CAR_PAINT_WHITE()
{
	Material mat({ 0.15f, 0.15f, 0.15f },          // ambient - neutral gray
		{ 0.95f, 0.95f, 0.95f },                     // diffuse - bright white
		{ 0.45f, 0.45f, 0.45f },                     // specular - strong gloss
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.9f),                         // shiny
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.95f, 0.95f, 0.95f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);
	mat.setClearcoat(1.0f);                          // full clearcoat
	mat.setClearcoatRoughness(0.1f);                 // smooth glossy clearcoat
	mat.setIOR(1.5f);                                // typical for car paints
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_GREEN()
{
	Material mat({ 0.08f, 0.10f, 0.08f },
		{ 0.05f, 0.55f, 0.20f },                     // diffuse - metallic green tint
		{ 0.40f, 0.45f, 0.40f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.85f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.05f, 0.55f, 0.20f));
	mat.setMetalness(0.3f);                          // metallic flakes effect
	mat.setRoughness(0.35f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.12f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_PEARL()
{
	Material mat({ 0.15f, 0.14f, 0.13f },
		{ 0.92f, 0.92f, 0.85f },                     // diffuse - off-white pearl
		{ 0.50f, 0.48f, 0.45f },                     // strong gloss
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.9f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.92f, 0.92f, 0.85f));
	mat.setMetalness(0.2f);                          // pearlescent layer acts semi-metallic
	mat.setRoughness(0.25f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.08f);                // very glossy
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::MATTE_GREY()
{
	Material mat({ 0.12f, 0.12f, 0.12f },
		{ 0.45f, 0.45f, 0.45f },                     // diffuse - medium grey
		{ 0.15f, 0.15f, 0.15f },                     // weak specular
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.25f),                        // not shiny
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.45f, 0.45f, 0.45f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.85f);                         // matte
	mat.setClearcoat(1.0f);                          // matte clearcoat still exists
	mat.setClearcoatRoughness(0.8f);                 // very rough clearcoat -> matte look
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}


Material Material::PIANO_BLACK()
{
	Material mat({ 0.01f, 0.01f, 0.01f },         // ambient - very dark
		{ 0.05f, 0.05f, 0.05f },          // diffuse - very dark base
		{ 0.95f, 0.95f, 0.95f },          // specular - high reflectivity
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.95f),             // shininess - mirror-like
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.08f, 0.08f, 0.08f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.6f);                          // Rough base material
	mat.setClearcoat(1.0f);                          // Full clearcoat for piano finish
	mat.setClearcoatRoughness(0.01f);                // Extremely smooth clearcoat
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}
// METALLIC CAR PAINTS

Material Material::CAR_PAINT_METALLIC_SILVER()
{
	Material mat({ 0.12f, 0.12f, 0.12f },
		{ 0.75f, 0.76f, 0.78f },                     // diffuse - bright silver
		{ 0.85f, 0.85f, 0.85f },                     // high reflectivity
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.9f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.75f, 0.76f, 0.78f));
	mat.setMetalness(0.85f);                         // highly metallic
	mat.setRoughness(0.15f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.05f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_DEEP_METALLIC_BLUE()
{
	Material mat({ 0.05f, 0.08f, 0.15f },
		{ 0.15f, 0.35f, 0.75f },                     // diffuse - deep metallic blue
		{ 0.45f, 0.50f, 0.65f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.85f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.15f, 0.35f, 0.75f));
	mat.setMetalness(0.7f);
	mat.setRoughness(0.25f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.08f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_RED()
{
	Material mat({ 0.15f, 0.05f, 0.05f },
		{ 0.78f, 0.15f, 0.12f },                     // diffuse - bright metallic red
		{ 0.60f, 0.40f, 0.40f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.88f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.78f, 0.15f, 0.12f));
	mat.setMetalness(0.45f);
	mat.setRoughness(0.3f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.1f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_COPPER()
{
	Material mat({ 0.18f, 0.12f, 0.08f },
		{ 0.85f, 0.45f, 0.25f },                     // diffuse - warm copper
		{ 0.70f, 0.55f, 0.45f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.82f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.85f, 0.45f, 0.25f));
	mat.setMetalness(0.8f);
	mat.setRoughness(0.28f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.12f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_GOLD()
{
	Material mat({ 0.20f, 0.18f, 0.10f },
		{ 0.90f, 0.75f, 0.35f },                     // diffuse - rich gold
		{ 0.75f, 0.68f, 0.50f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.88f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.90f, 0.75f, 0.35f));
	mat.setMetalness(0.9f);
	mat.setRoughness(0.22f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.06f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_PURPLE()
{
	Material mat({ 0.12f, 0.08f, 0.15f },
		{ 0.55f, 0.25f, 0.70f },                     // diffuse - deep metallic purple
		{ 0.55f, 0.45f, 0.60f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.85f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.55f, 0.25f, 0.70f));
	mat.setMetalness(0.6f);
	mat.setRoughness(0.32f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.09f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// NON-METALLIC CAR PAINTS

Material Material::CAR_PAINT_GLOSSY_BLACK()
{
	Material mat({ 0.05f, 0.05f, 0.05f },
		{ 0.08f, 0.08f, 0.08f },                     // diffuse - deep black
		{ 0.95f, 0.95f, 0.95f },                     // high gloss reflection
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.95f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.08f, 0.08f, 0.08f));
	mat.setMetalness(0.0f);                          // non-metallic
	mat.setRoughness(0.1f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.02f);                // mirror-like clearcoat
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_GLOSSY_WHITE()
{
	Material mat({ 0.85f, 0.85f, 0.85f },
		{ 0.95f, 0.95f, 0.95f },                     // diffuse - pure white
		{ 0.90f, 0.90f, 0.90f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.92f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.95f, 0.95f, 0.95f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.12f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.04f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_MATTE_RED()
{
	Material mat({ 0.45f, 0.08f, 0.08f },
		{ 0.75f, 0.12f, 0.12f },                     // diffuse - vibrant red
		{ 0.15f, 0.15f, 0.15f },                     // low reflectivity for matte
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.3f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.75f, 0.12f, 0.12f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.85f);                         // high roughness for matte
	mat.setClearcoat(0.0f);                          // no clearcoat for matte finish
	mat.setClearcoatRoughness(1.0f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_GLOSSY_YELLOW()
{
	Material mat({ 0.65f, 0.60f, 0.15f },
		{ 0.95f, 0.85f, 0.15f },                     // diffuse - bright yellow
		{ 0.85f, 0.85f, 0.75f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.88f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.95f, 0.85f, 0.15f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.18f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.06f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_GLOSSY_ORANGE()
{
	Material mat({ 0.55f, 0.35f, 0.12f },
		{ 0.90f, 0.45f, 0.15f },                     // diffuse - vibrant orange
		{ 0.80f, 0.70f, 0.60f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.85f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.90f, 0.45f, 0.15f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.07f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_SATIN_GRAY()
{
	Material mat({ 0.35f, 0.35f, 0.35f },
		{ 0.55f, 0.55f, 0.55f },                     // diffuse - medium gray
		{ 0.45f, 0.45f, 0.45f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.6f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.55f, 0.55f, 0.55f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.55f);                         // satin finish - between matte and gloss
	mat.setClearcoat(0.3f);                          // light clearcoat
	mat.setClearcoatRoughness(0.4f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// DARK SHADE VARIATIONS

Material Material::CAR_PAINT_MIDNIGHT_BLUE()
{
	Material mat({ 0.03f, 0.05f, 0.12f },
		{ 0.08f, 0.15f, 0.35f },                     // diffuse - very dark blue
		{ 0.85f, 0.85f, 0.90f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.92f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.08f, 0.15f, 0.35f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.15f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.04f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_FOREST_GREEN()
{
	Material mat({ 0.05f, 0.12f, 0.06f },
		{ 0.12f, 0.35f, 0.15f },                     // diffuse - deep forest green
		{ 0.75f, 0.85f, 0.75f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.88f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.12f, 0.35f, 0.15f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.18f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.06f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_CHARCOAL_GRAY()
{
	Material mat({ 0.15f, 0.15f, 0.15f },
		{ 0.25f, 0.25f, 0.25f },                     // diffuse - dark charcoal
		{ 0.80f, 0.80f, 0.80f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.85f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.25f, 0.25f, 0.25f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.22f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.08f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_BURGUNDY()
{
	Material mat({ 0.18f, 0.06f, 0.08f },
		{ 0.45f, 0.12f, 0.18f },                     // diffuse - deep burgundy
		{ 0.75f, 0.65f, 0.68f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.82f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.45f, 0.12f, 0.18f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.1f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// LIGHT SHADE VARIATIONS

Material Material::CAR_PAINT_POWDER_BLUE()
{
	Material mat({ 0.65f, 0.75f, 0.85f },
		{ 0.75f, 0.85f, 0.95f },                     // diffuse - soft powder blue
		{ 0.85f, 0.88f, 0.92f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.78f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.75f, 0.85f, 0.95f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.28f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.12f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_MINT_GREEN()
{
	Material mat({ 0.70f, 0.85f, 0.75f },
		{ 0.80f, 0.95f, 0.85f },                     // diffuse - light mint green
		{ 0.82f, 0.92f, 0.85f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.75f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.80f, 0.95f, 0.85f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.32f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.15f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_CREAM_YELLOW()
{
	Material mat({ 0.88f, 0.85f, 0.70f },
		{ 0.95f, 0.92f, 0.78f },                     // diffuse - soft cream yellow
		{ 0.90f, 0.88f, 0.82f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.72f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.95f, 0.92f, 0.78f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.35f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.18f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_LAVENDER()
{
	Material mat({ 0.75f, 0.70f, 0.85f },
		{ 0.85f, 0.78f, 0.95f },                     // diffuse - soft lavender
		{ 0.88f, 0.82f, 0.92f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.75f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.85f, 0.78f, 0.95f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.3f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.14f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// MEDIUM TONE VARIATIONS

Material Material::CAR_PAINT_TEAL()
{
	Material mat({ 0.25f, 0.45f, 0.42f },
		{ 0.35f, 0.65f, 0.58f },                     // diffuse - medium teal
		{ 0.70f, 0.85f, 0.82f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.8f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.35f, 0.65f, 0.58f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.1f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_CORAL()
{
	Material mat({ 0.65f, 0.45f, 0.35f },
		{ 0.85f, 0.58f, 0.45f },                     // diffuse - warm coral
		{ 0.88f, 0.75f, 0.68f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.78f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.85f, 0.58f, 0.45f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.28f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.12f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_SLATE_BLUE()
{
	Material mat({ 0.35f, 0.38f, 0.55f },
		{ 0.48f, 0.52f, 0.75f },                     // diffuse - medium slate blue
		{ 0.75f, 0.78f, 0.88f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.82f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.48f, 0.52f, 0.75f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.22f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.08f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// METALLIC VARIATIONS WITH DIFFERENT SHADES

Material Material::CAR_PAINT_METALLIC_CHAMPAGNE()
{
	Material mat({ 0.45f, 0.42f, 0.35f },
		{ 0.75f, 0.68f, 0.55f },                     // diffuse - warm champagne
		{ 0.82f, 0.78f, 0.68f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.85f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.75f, 0.68f, 0.55f));
	mat.setMetalness(0.65f);
	mat.setRoughness(0.25f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.08f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_GUNMETAL()
{
	Material mat({ 0.18f, 0.20f, 0.22f },
		{ 0.35f, 0.38f, 0.42f },                     // diffuse - dark gunmetal
		{ 0.65f, 0.68f, 0.72f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.88f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.35f, 0.38f, 0.42f));
	mat.setMetalness(0.75f);
	mat.setRoughness(0.2f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.06f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_METALLIC_BRONZE()
{
	Material mat({ 0.35f, 0.25f, 0.15f },
		{ 0.68f, 0.45f, 0.28f },                     // diffuse - rich bronze
		{ 0.78f, 0.65f, 0.48f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.83f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.68f, 0.45f, 0.28f));
	mat.setMetalness(0.8f);
	mat.setRoughness(0.3f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.1f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// SPECIAL FINISHES

Material Material::CAR_PAINT_PEARLESCENT_BLUE()
{
	Material mat({ 0.10f, 0.15f, 0.25f },
		{ 0.35f, 0.55f, 0.85f },                     // diffuse - pearl blue
		{ 0.60f, 0.65f, 0.70f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.9f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.35f, 0.55f, 0.85f));
	mat.setMetalness(0.25f);                         // pearlescent semi-metallic
	mat.setRoughness(0.2f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.05f);
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_CANDY_APPLE_RED()
{
	Material mat({ 0.25f, 0.05f, 0.05f },
		{ 0.85f, 0.08f, 0.08f },                     // diffuse - deep candy red
		{ 0.90f, 0.70f, 0.70f },                     // high gloss with red tint
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.95f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.85f, 0.08f, 0.08f));
	mat.setMetalness(0.1f);                          // slight metallic base
	mat.setRoughness(0.08f);                         // very glossy
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.02f);                // mirror finish
	mat.setIOR(1.6f);                                // higher IOR for candy effect
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CAR_PAINT_IRIDESCENT_GREEN()
{
	Material mat({ 0.15f, 0.25f, 0.18f },
		{ 0.28f, 0.65f, 0.45f },                     // diffuse - iridescent green
		{ 0.55f, 0.75f, 0.65f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.92f),
		false, 1.0f);
	mat.setAlbedoColor(QVector3D(0.28f, 0.65f, 0.45f));
	mat.setMetalness(0.35f);                         // color-shifting effect
	mat.setRoughness(0.15f);
	mat.setClearcoat(1.0f);
	mat.setClearcoatRoughness(0.04f);
	mat.setIOR(1.55f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

// === SHEEN MATERIALS ===

Material Material::VELVET_RED()
{
	Material mat({ 0.15f, 0.03f, 0.03f },         // ambient
		{ 0.6f, 0.1f, 0.1f },             // diffuse - deep red
		{ 0.05f, 0.05f, 0.05f },          // specular - very low
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.02f),             // shininess - very low
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.7f, 0.15f, 0.15f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);                          // Very rough base
	mat.setSheenColor(QVector3D(1.0f, 0.8f, 0.8f)); // Bright sheen with slight red tint
	mat.setSheenRoughness(0.3f);                     // Moderate sheen roughness
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::SATIN_FABRIC()
{
	Material mat({ 0.18f, 0.18f, 0.12f },         // ambient - cream color
		{ 0.9f, 0.9f, 0.6f },             // diffuse - silk-like cream
		{ 0.1f, 0.1f, 0.1f },             // specular - low base specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.08f),             // shininess - low
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.7f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);                          // Moderately rough
	mat.setSheenColor(QVector3D(1.0f, 1.0f, 0.9f)); // Bright sheen
	mat.setSheenRoughness(0.1f);                     // Sharp sheen highlight
	mat.setIOR(1.54f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::MICROFIBER_CLOTH()
{
	Material mat({ 0.08f, 0.1f, 0.12f },          // ambient - blue-gray
		{ 0.4f, 0.5f, 0.6f },             // diffuse - blue-gray fabric
		{ 0.02f, 0.02f, 0.02f },          // specular - very low
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.01f),             // shininess - almost none
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.4f, 0.5f, 0.6f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.95f);                         // Very rough base
	mat.setSheenColor(QVector3D(0.6f, 0.7f, 0.8f)); // Subtle blue-tinted sheen
	mat.setSheenRoughness(0.8f);                     // Diffuse sheen
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

// ----------------- Leather Materials -----------------
Material Material::LEATHER_BLACK()
{
	Material mat({ 0.03f, 0.03f, 0.03f },
		{ 0.12f, 0.12f, 0.12f },
		{ 0.2f, 0.2f, 0.2f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.3f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.12f, 0.12f, 0.12f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.55f);
	mat.setIOR(1.48f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LEATHER_BROWN()
{
	Material mat({ 0.05f, 0.03f, 0.02f },
		{ 0.35f, 0.22f, 0.15f },
		{ 0.2f, 0.2f, 0.2f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.28f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.35f, 0.22f, 0.15f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.50f);
	mat.setIOR(1.48f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LEATHER_RED()
{
	Material mat({ 0.08f, 0.02f, 0.02f },
		{ 0.55f, 0.18f, 0.18f },
		{ 0.2f, 0.2f, 0.2f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.28f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.55f, 0.18f, 0.18f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.52f);
	mat.setIOR(1.48f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LEATHER_WHITE()
{
	Material mat({ 0.10f, 0.10f, 0.10f },
		{ 0.85f, 0.85f, 0.85f },
		{ 0.2f, 0.2f, 0.2f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.3f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.85f, 0.85f, 0.85f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.50f);
	mat.setIOR(1.48f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LEATHER_OXBLOOD()
{
	Material mat({ 0.07f, 0.02f, 0.03f },
		{ 0.45f, 0.10f, 0.12f },
		{ 0.2f, 0.2f, 0.2f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.28f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.45f, 0.10f, 0.12f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.50f);
	mat.setIOR(1.48f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LEATHER_TAN()
{
	Material mat({ 0.06f, 0.04f, 0.02f },          // ambient - warm brown
		{ 0.60f, 0.40f, 0.25f },          // diffuse - tan color
		{ 0.2f, 0.2f, 0.2f },             // specular - soft highlights
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.28f),           // moderate shininess
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.60f, 0.40f, 0.25f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.50f);                        // slightly smoother than black/brown leather
	mat.setIOR(1.48f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}


// === TRANSMISSION MATERIALS ===
Material Material::FROSTED_GLASS()
{
	Material mat({ 0.05f, 0.05f, 0.05f },         // ambient
		{ 0.1f, 0.1f, 0.1f },             // diffuse - low for transparency
		{ 0.8f, 0.8f, 0.8f },             // specular - high
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.3f),              // shininess - moderate due to frosting
		false,                            // metallic
		0.2f);                           // opacity - mostly transparent

	mat.setAlbedoColor(QVector3D(0.95f, 0.95f, 0.95f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.3f);                          // Roughness creates frosted effect
	mat.setTransmission(0.8f);                       // High transmission
	mat.setIOR(1.52f);                              // Glass IOR
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();

	return mat;
}

Material Material::COLORED_GLASS_GREEN()
{
	Material mat({ 0.02f, 0.05f, 0.02f },         // ambient - slight green tint
		{ 0.1f, 0.3f, 0.1f },             // diffuse - green tint
		{ 0.9f, 0.9f, 0.9f },             // specular - high
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.9f),              // shininess - very smooth
		false,                            // metallic
		0.15f);                          // opacity - very transparent

	mat.setAlbedoColor(QVector3D(0.7f, 0.9f, 0.7f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.02f);                         // Very smooth
	mat.setTransmission(0.85f);                      // High transmission with color filtering
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();

	return mat;
}

Material Material::CRYSTAL_QUARTZ()
{
	Material mat({ 0.03f, 0.03f, 0.03f },         // ambient
		{ 0.9f, 0.9f, 0.95f },            // diffuse - slightly blue-white
		{ 0.95f, 0.95f, 1.0f },           // specular - very high
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.95f),             // shininess - very high
		false,                            // metallic
		0.7f);                           // opacity - semi-transparent

	mat.setAlbedoColor(QVector3D(0.98f, 0.98f, 1.0f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.01f);                         // Extremely smooth
	mat.setTransmission(0.3f);                       // Moderate transmission
	mat.setIOR(1.544f);                             // Quartz IOR
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();

	return mat;
}

// === EMISSIVE MATERIALS ===

Material Material::NEON_BLUE()
{
	Material mat({ 0.05f, 0.15f, 0.4f },          // ambient - blue glow
		{ 0.2f, 0.6f, 1.0f },             // diffuse - bright blue
		{ 0.8f, 0.9f, 1.0f },             // specular
		{ 0.1f, 0.4f, 1.0f },             // emissive - bright blue emission
		fabs(128.0f * 0.7f),              // shininess
		false,                            // metallic
		0.9f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.3f, 0.7f, 1.0f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setEmissiveStrength(3.0f);                   // Strong emission for HDR
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::NEON_GREEN()
{
	Material mat({ 0.05f, 0.4f, 0.05f },          // ambient - green glow
		{ 0.2f, 0.8f, 0.2f },             // diffuse - bright green
		{ 0.8f, 1.0f, 0.8f },             // specular
		{ 0.1f, 1.0f, 0.1f },             // emissive - bright green emission
		fabs(128.0f * 0.7f),              // shininess
		false,                            // metallic
		0.9f);                           // opacity
	mat.setAlbedoColor(QVector3D(0.3f, 1.0f, 0.3f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setEmissiveStrength(3.0f);                   // Strong emission for HDR
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::NEON_RED()
{
	Material mat({ 0.4f, 0.05f, 0.05f },          // ambient - red glow
		{ 0.8f, 0.2f, 0.2f },             // diffuse - bright red
		{ 1.0f, 0.8f, 0.8f },             // specular
		{ 1.0f, 0.1f, 0.1f },             // emissive - bright red emission
		fabs(128.0f * 0.7f),              // shininess
		false,                            // metallic
		0.9f);                           // opacity
	mat.setAlbedoColor(QVector3D(1.0f, 0.3f, 0.3f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setEmissiveStrength(3.0f);                   // Strong emission for HDR
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::NEON_YELLOW()
{
	Material mat({ 0.4f, 0.4f, 0.05f },          // ambient - yellow glow
		{ 0.8f, 0.8f, 0.2f },             // diffuse - bright yellow
		{ 1.0f, 1.0f, 0.8f },             // specular
		{ 1.0f, 1.0f, 0.1f },             // emissive - bright yellow emission
		fabs(128.0f * 0.7f),              // shininess
		false,                            // metallic
		0.9f);                           // opacity
	mat.setAlbedoColor(QVector3D(1.0f, 1.0f, 0.3f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.2f);
	mat.setEmissiveStrength(3.0f);                   // Strong emission for HDR
	mat.setIOR(1.46f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LED_BLUE()
{
	Material mat({ 0.2f, 0.2f, 0.2f },            // ambient
		{ 0.8f, 0.8f, 1.0f },             // diffuse - bright blue
		{ 0.9f, 0.9f, 1.0f },             // specular
		{ 0.1f, 0.1f, 1.0f },             // emissive - bright blue emission
		fabs(128.0f * 0.8f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity
	mat.setAlbedoColor(QVector3D(0.3f, 0.3f, 1.0f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);
	mat.setEmissiveStrength(5.0f);                   // Very bright for LED
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LED_GREEN()
{
	Material mat({ 0.2f, 0.2f, 0.2f },            // ambient
		{ 0.8f, 1.0f, 0.8f },             // diffuse - bright green
		{ 0.9f, 1.0f, 0.9f },             // specular
		{ 0.1f, 1.0f, 0.1f },             // emissive - bright green emission
		fabs(128.0f * 0.8f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity
	mat.setAlbedoColor(QVector3D(0.3f, 1.0f, 0.3f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);
	mat.setEmissiveStrength(5.0f);                   // Very bright for LED
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LED_RED()
{
	Material mat({ 0.2f, 0.2f, 0.2f },            // ambient
		{ 0.8f, 0.4f, 0.4f },             // diffuse - bright red
		{ 1.0f, 0.6f, 0.6f },             // specular
		{ 1.0f, 0.1f, 0.1f },             // emissive - bright red emission
		fabs(128.0f * 0.8f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity
	mat.setAlbedoColor(QVector3D(1.0f, 0.3f, 0.3f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);
	mat.setEmissiveStrength(5.0f);                   // Very bright for LED
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LED_YELLOW()
{
	Material mat({ 0.2f, 0.2f, 0.2f },            // ambient
		{ 0.8f, 0.8f, 0.4f },             // diffuse - bright yellow
		{ 0.9f, 0.9f, 0.6f },             // specular
		{ 1.0f, 1.0f, 0.1f },             // emissive - bright yellow emission
		fabs(128.0f * 0.8f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity
	mat.setAlbedoColor(QVector3D(1.0f, 1.0f, 0.3f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);
	mat.setEmissiveStrength(5.0f);                   // Very bright for LED
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::LED_WHITE()
{
	Material mat({ 0.2f, 0.2f, 0.2f },            // ambient
		{ 0.8f, 0.8f, 0.8f },             // diffuse
		{ 0.9f, 0.9f, 0.9f },             // specular
		{ 1.0f, 1.0f, 0.95f },            // emissive - warm white
		fabs(128.0f * 0.8f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);
	mat.setEmissiveStrength(5.0f);                   // Very bright for LED
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

// === COMPLEX MATERIALS (Multiple Properties) ===

Material Material::IRIDESCENT_SOAP_BUBBLE()
{
	Material mat({ 0.01f, 0.01f, 0.01f },         // ambient - very low
		{ 0.05f, 0.05f, 0.05f },          // diffuse - very low for transparency
		{ 0.98f, 0.98f, 0.98f },          // specular - very high
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.98f),             // shininess - extremely high
		false,                            // metallic
		0.05f);                          // opacity - nearly transparent

	mat.setAlbedoColor(QVector3D(0.9f, 0.95f, 1.0f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.005f);                        // Extremely smooth
	mat.setTransmission(0.95f);                      // Nearly full transmission
	mat.setIOR(1.33f);                              // Soap film IOR
	mat.setSheenColor(QVector3D(1.0f, 0.8f, 1.0f)); // Iridescent sheen
	mat.setSheenRoughness(0.01f);                    // Sharp iridescent highlight
	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Alpha);
	mat.updateConsistency();

	return mat;
}

Material Material::CARBON_FIBER()
{
	Material mat({ 0.02f, 0.02f, 0.02f },         // ambient - very dark
		{ 0.1f, 0.1f, 0.1f },             // diffuse - dark base
		{ 0.6f, 0.6f, 0.6f },             // specular - moderate
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.7f),              // shininess
		false,                            // metallic (composite material)
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.15f, 0.15f, 0.15f));
	mat.setMetalness(0.2f);                          // Slightly metallic
	mat.setRoughness(0.3f);                          // Moderate roughness
	mat.setClearcoat(0.6f);                          // Resin clearcoat layer
	mat.setClearcoatRoughness(0.1f);                 // Smooth clearcoat
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::WET_ASPHALT()
{
	Material mat({ 0.01f, 0.01f, 0.01f },         // ambient - very dark
		{ 0.08f, 0.08f, 0.08f },          // diffuse - dark asphalt
		{ 0.4f, 0.4f, 0.4f },             // specular - wet surface
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.6f),              // shininess - wet reflections
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.12f, 0.12f, 0.12f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.8f);                          // Rough base material
	mat.setClearcoat(0.3f);                          // Water layer on top
	mat.setClearcoatRoughness(0.05f);                // Smooth water surface
	mat.setIOR(1.33f);                              // Water IOR for clearcoat
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

Material Material::CONCRETE()
{
	Material mat({ 0.08f, 0.08f, 0.08f },   // ambient - neutral gray
		{ 0.55f, 0.55f, 0.55f },              // diffuse - medium gray
		{ 0.10f, 0.10f, 0.10f },              // specular - low reflectivity
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.20f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.55f, 0.55f, 0.55f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.85f);
	mat.setIOR(1.52f);                        // typical cement IOR
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CONCRETE_LIGHT()
{
	Material mat({ 0.09f, 0.09f, 0.09f },
		{ 0.75f, 0.75f, 0.75f },              // lighter gray
		{ 0.12f, 0.12f, 0.12f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.18f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.75f, 0.75f, 0.75f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.8f);
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CONCRETE_DARK()
{
	Material mat({ 0.06f, 0.06f, 0.06f },
		{ 0.30f, 0.30f, 0.30f },              // darker gray
		{ 0.08f, 0.08f, 0.08f },
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.22f),
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.30f, 0.30f, 0.30f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::CONCRETE_POLISHED()
{
	Material mat({ 0.08f, 0.08f, 0.08f },
		{ 0.60f, 0.60f, 0.60f },              // medium-light gray
		{ 0.25f, 0.25f, 0.25f },              // stronger highlights
		{ 0.0f, 0.0f, 0.0f },
		fabs(128.0f * 0.60f),                 // much shinier
		false, 1.0f);

	mat.setAlbedoColor(QVector3D(0.60f, 0.60f, 0.60f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.4f);                   // smoother due to polish
	mat.setIOR(1.52f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}


Material Material::STONE_GRANITE()
{
	Material mat({ 0.12f, 0.11f, 0.11f },        // ambient
		{ 0.6f, 0.55f, 0.55f },          // diffuse  
		{ 0.05f, 0.05f, 0.05f },         // specular
		{ 0.0f, 0.0f, 0.0f },            // emissive
		fabs(128.0f * 0.09f),             // shininess
		false,                            // metallic
		1.0f);                           // opacity

	// PBR properties
	mat.setAlbedoColor(QVector3D(0.6f, 0.55f, 0.55f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.8f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_LIMESTONE()
{
	Material mat({ 0.15f, 0.146f, 0.136f },       // ambient
		{ 0.75f, 0.73f, 0.68f },          // diffuse
		{ 0.02f, 0.02f, 0.02f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.047f),            // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.75f, 0.73f, 0.68f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_MARBLE()
{
	Material mat({ 0.18f, 0.18f, 0.18f },         // ambient
		{ 0.9f, 0.9f, 0.9f },             // diffuse
		{ 0.2f, 0.2f, 0.2f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.25f),             // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.5f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_SLATE()
{
	Material mat({ 0.03f, 0.036f, 0.044f },       // ambient
		{ 0.15f, 0.18f, 0.22f },          // diffuse
		{ 0.01f, 0.01f, 0.01f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.031f),            // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.15f, 0.18f, 0.22f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.95f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_SANDSTONE()
{
	Material mat({ 0.152f, 0.128f, 0.096f },      // ambient
		{ 0.76f, 0.64f, 0.48f },          // diffuse
		{ 0.03f, 0.03f, 0.03f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.078f),            // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.76f, 0.64f, 0.48f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.85f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_BASALT()
{
	Material mat({ 0.02f, 0.02f, 0.02f },         // ambient
		{ 0.1f, 0.1f, 0.1f },             // diffuse
		{ 0.02f, 0.02f, 0.02f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.047f),            // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.1f, 0.1f, 0.1f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.9f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_TRAVERTINE()
{
	Material mat({ 0.17f, 0.16f, 0.14f },         // ambient
		{ 0.85f, 0.8f, 0.7f },            // diffuse
		{ 0.02f, 0.02f, 0.02f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.063f),            // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.85f, 0.8f, 0.7f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.85f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_QUARTZITE()
{
	Material mat({ 0.16f, 0.17f, 0.18f },         // ambient
		{ 0.8f, 0.85f, 0.9f },            // diffuse
		{ 0.3f, 0.3f, 0.3f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.5f),              // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.8f, 0.85f, 0.9f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.4f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::STONE_SOAPSTONE()
{
	Material mat({ 0.05f, 0.06f, 0.056f },        // ambient
		{ 0.25f, 0.3f, 0.28f },           // diffuse
		{ 0.01f, 0.01f, 0.01f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.031f),            // shininess
		false,                            // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.25f, 0.3f, 0.28f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.95f);
	mat.setIOR(1.45f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_TITANIUM()
{
	Material mat({ 0.11f, 0.116f, 0.124f },       // ambient
		{ 0.55f, 0.58f, 0.62f },          // diffuse
		{ 0.7f, 0.7f, 0.7f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.5f),              // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.55f, 0.58f, 0.62f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.3f);
	mat.setIOR(2.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_PLATINUM()
{
	Material mat({ 0.164f, 0.164f, 0.17f },       // ambient
		{ 0.82f, 0.82f, 0.85f },          // diffuse
		{ 0.9f, 0.9f, 0.9f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.75f),             // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.82f, 0.82f, 0.85f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.2f);
	mat.setIOR(2.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_MAGNESIUM()
{
	Material mat({ 0.18f, 0.18f, 0.19f },         // ambient
		{ 0.9f, 0.9f, 0.95f },            // diffuse
		{ 0.7f, 0.7f, 0.75f },            // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.31f),             // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.9f, 0.9f, 0.95f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.4f);
	mat.setIOR(1.6f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_ZINC()
{
	Material mat({ 0.13f, 0.14f, 0.15f },         // ambient
		{ 0.65f, 0.7f, 0.75f },           // diffuse
		{ 0.7f, 0.7f, 0.7f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.375f),            // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.65f, 0.7f, 0.75f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.35f);
	mat.setIOR(1.9f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_NICKEL()
{
	Material mat({ 0.144f, 0.144f, 0.148f },      // ambient
		{ 0.72f, 0.72f, 0.74f },          // diffuse
		{ 0.85f, 0.85f, 0.85f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.625f),            // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.72f, 0.72f, 0.74f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.25f);
	mat.setIOR(2.0f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_ALUMINUM()
{
	Material mat({ 0.182f, 0.184f, 0.184f },      // ambient
		{ 0.91f, 0.92f, 0.92f },          // diffuse
		{ 0.95f, 0.95f, 0.95f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.563f),            // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.91f, 0.92f, 0.92f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.2f);
	mat.setIOR(1.44f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_IRON_RAW()
{
	Material mat({ 0.09f, 0.09f, 0.094f },        // ambient
		{ 0.45f, 0.45f, 0.47f },          // diffuse
		{ 0.6f, 0.6f, 0.6f },             // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.25f),             // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.45f, 0.45f, 0.47f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.5f);
	mat.setIOR(2.25f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_COBALT()
{
	Material mat({ 0.08f, 0.09f, 0.12f },         // ambient
		{ 0.4f, 0.45f, 0.6f },            // diffuse
		{ 0.7f, 0.7f, 0.75f },            // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.406f),            // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.4f, 0.45f, 0.6f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.3f);
	mat.setIOR(2.3f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_PEWTER()
{
	Material mat({ 0.12f, 0.12f, 0.124f },        // ambient
		{ 0.6f, 0.6f, 0.62f },            // diffuse
		{ 0.55f, 0.55f, 0.55f },          // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.156f),            // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.6f, 0.6f, 0.62f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.6f);
	mat.setIOR(2.1f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}

Material Material::METAL_TUNGSTEN()
{
	Material mat({ 0.06f, 0.06f, 0.066f },        // ambient
		{ 0.3f, 0.3f, 0.33f },            // diffuse
		{ 0.85f, 0.85f, 0.9f },           // specular
		{ 0.0f, 0.0f, 0.0f },             // emissive
		fabs(128.0f * 0.75f),             // shininess
		true,                             // metallic
		1.0f);                           // opacity

	mat.setAlbedoColor(QVector3D(0.3f, 0.3f, 0.33f));
	mat.setMetalness(1.0f);
	mat.setRoughness(0.15f);
	mat.setIOR(3.0f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();
	return mat;
}


Material Material::DEFAULT_MAT()
{
	Material mat(
		QVector3D(0.108f, 0.108f, 0.108f),   // ambient (~ albedo * 0.12)
		QVector3D(0.90f, 0.90f, 0.90f),      // diffuse
		QVector3D(0.04f, 0.04f, 0.04f),      // specular (dielectric F0)
		QVector3D(0.0f, 0.0f, 0.0f),         // emissive
		fabs(77.0f),                         // shininess (~roughness 0.45)
		false,                               // metallic
		1.0f);                               // opacity

	// PBR canonical values
	mat.setAlbedoColor(QVector3D(0.90f, 0.90f, 0.90f));
	mat.setMetalness(0.0f);
	mat.setRoughness(0.45f);
	mat.setIOR(1.5f);
	mat.setTransmission(0.0f);
	mat.setClearcoat(0.0f);
	mat.setClearcoatRoughness(0.0f);
	mat.setSheenColor(QVector3D(0.0f, 0.0f, 0.0f));
	mat.setSheenRoughness(0.0f);

	mat.setShadingModel(ShadingModel::PBR);
	mat.setBlendMode(BlendMode::Opaque);
	mat.setTwoSided(true);

	mat.updateConsistency();
	return mat;
}

void Material::setAlbedoFromADS()
{
	QVector3D col;
	if (_metallic)
		col = _ambient + _diffuse;
	else
		col = _ambient + _diffuse;
	_albedoColor.setX(std::clamp(col.x(), 0.0f, 1.0f));
	_albedoColor.setY(std::clamp(col.y(), 0.0f, 1.0f));
	_albedoColor.setZ(std::clamp(col.z(), 0.0f, 1.0f));
}

// ---------------------- BEGIN: Robust updateConsistency + helper ----------------------

// internal helper: convert canonical PBR fields into legacy ADS fields
// - This is intentionally one-way: it writes ADS fields computed from PBR canonical fields
// - It does NOT change canonical PBR fields (albedo, metalness, roughness, ior, etc.)
void Material::convertPBRtoADS()
{
	// Local copies for clarity
	const QVector3D albedo = _albedoColor;
	const float metalness = qBound(0.0f, _metalness, 1.0f);
	const float roughness = qBound(0.0f, _roughness, 1.0f);
	const float ior = qBound(1.0f, _ior, 10.0f);

	// Ambient: small fraction of albedo (studio friendly)
	_ambient = albedo * 0.12f;

	// Diffuse: dielectrics keep albedo as diffuse; metals have strongly reduced diffuse contribution.
	// We'll set diffuse = albedo * (1 - metalness/4.0f) so shader / final lighting multiply is consistent.
	_diffuse = albedo * (1.0f - metalness/4.0f);

	// Compute dielectric F0 from IOR: F0 = ((ior-1)/(ior+1))^2
	float f0scalar = (ior - 1.0f) / (ior + 1.0f);
	f0scalar = f0scalar * f0scalar;
	QVector3D dielectricF0 = QVector3D(f0scalar, f0scalar, f0scalar);

	// Metallic specular uses albedo as F0
	QVector3D metallicF0 = albedo;

	// Final specular = mix(dielectricF0, metallicF0, metalness)
	_specular = dielectricF0 * (1.0f - metalness) + metallicF0 * metalness;

	// Shininess mapping: use a perceptual mapping. Keep in 1..128 range for legacy shaders.
	// Use squared smoothness mapping: shininess ~ pow(1-roughness,2) * 255 -> clamp to [1,128]
	float smoothness = 1.0f - roughness;
	float shin = qBound(1.0f, std::pow(smoothness, 2.0f) * 255.0f, 128.0f);
	_shininess = shin;

}


void Material::updateConsistency()
{
	// 1) First, ensure basic ranges are respected
	clampValues();

	// 2) Make sure metal boolean and float are consistent (boolean is legacy short-hand)
	// Keep _metalness as the canonical float. If boolean was used, gently bias float.
	if (_metallic && _metalness < 0.5f)
	{
		// If legacy boolean is set but float is low, bias toward metallic but don't force
		_metalness = qBound(0.5f, _metalness, 1.0f);
	}
	else if (!_metallic && _metalness > 0.99f)
	{
		// if float says fully-metallic but boolean false, keep float as source of truth
		// (we will not flip boolean here)
	}
	_metallic = (_metalness > 0.5f); // keep boolean consistent with canonical float

	// 3) ALWAYS derive ADS from PBR for rendering mode switching
	// PBR values are canonical for all materials (regardless of shading model display)
	// This ensures ADS shader can always work correctly
	convertPBRtoADS();

	// Keep PBR fields unchanged. Don't multiply emissive by strength in the model.
	// (renderer uses _emissive * _emissiveStrength when shading)

	// Some additional housekeeping: if clearcoat is set but clearcoatRoughness is zero,
	// set a small default to avoid singular appearance.
	if (_clearcoat > 0.0f && _clearcoatRoughness <= 0.0f)
		_clearcoatRoughness = qBound(0.02f, _clearcoatRoughness, 1.0f);

	// clamp again after conversions
	clampValues();
}



void Material::clampValues()
{
	// Clamp legacy properties
	_ambient = QVector3D(
		qBound(0.0f, _ambient.x(), 1.0f),
		qBound(0.0f, _ambient.y(), 1.0f),
		qBound(0.0f, _ambient.z(), 1.0f)
	);

	_diffuse = QVector3D(
		qBound(0.0f, _diffuse.x(), 1.0f),
		qBound(0.0f, _diffuse.y(), 1.0f),
		qBound(0.0f, _diffuse.z(), 1.0f)
	);

	_specular = QVector3D(
		qBound(0.0f, _specular.x(), 1.0f),
		qBound(0.0f, _specular.y(), 1.0f),
		qBound(0.0f, _specular.z(), 1.0f)
	);

	// Emissive can exceed 1.0 for HDR, but should be non-negative
	_emissive = QVector3D(
		qMax(0.0f, _emissive.x()),
		qMax(0.0f, _emissive.y()),
		qMax(0.0f, _emissive.z())
	);

	_shininess = qBound(0.0f, _shininess, 1000.0f);

	// Clamp PBR properties
	_albedoColor = QVector3D(
		qBound(0.0f, _albedoColor.x(), 1.0f),
		qBound(0.0f, _albedoColor.y(), 1.0f),
		qBound(0.0f, _albedoColor.z(), 1.0f)
	);

	_metalness = qBound(0.0f, _metalness, 1.0f);
	_roughness = qBound(0.0f, _roughness, 1.0f);
	_opacity = qBound(0.0f, _opacity, 1.0f);

	// Emissive strength can be > 1.0 for HDR
	_emissiveStrength = qMax(0.0f, _emissiveStrength);

	// Advanced PBR properties
	_ior = qBound(1.0f, _ior, 3.0f); // Typical range for common materials
	_clearcoat = qBound(0.0f, _clearcoat, 1.0f);
	_clearcoatRoughness = qBound(0.0f, _clearcoatRoughness, 1.0f);

	_sheenColor = QVector3D(
		qBound(0.0f, _sheenColor.x(), 1.0f),
		qBound(0.0f, _sheenColor.y(), 1.0f),
		qBound(0.0f, _sheenColor.z(), 1.0f)
	);

	_sheenRoughness = qBound(0.0f, _sheenRoughness, 1.0f);
	_transmission = qBound(0.0f, _transmission, 1.0f);

	// Rendering properties
	_alphaThreshold = qBound(0.0f, _alphaThreshold, 1.0f);

	// Texture coordinate indices should be non-negative
	_albedoTexCoord = qMax(0, _albedoTexCoord);
	_normalTexCoord = qMax(0, _normalTexCoord);
	_metallicRoughnessTexCoord = qMax(0, _metallicRoughnessTexCoord);
}

void Material::ensureADSConsistency()
{
	// This function can be called to update any material that might have
	// incomplete ADS values based on its PBR properties

	if (_shadingModel == ShadingModel::PBR)
	{
		// Update legacy ADS from PBR values
		_diffuse = _albedoColor;
		_ambient = _albedoColor * 0.2f; // Standard ambient factor

		if (_metalness > 0.5f)
		{
			// Metallic materials
			_specular = _albedoColor; // Metals use albedo as specular
			_shininess = (1.0f - _roughness) * 128.0f;
		}
		else
		{
			// Dielectric materials
			float f0 = (_ior - 1.0f) / (_ior + 1.0f);
			f0 = f0 * f0;
			_specular = QVector3D(f0, f0, f0);
			_shininess = (1.0f - _roughness) * 128.0f;
		}
	}
	else
	{
		// Update PBR from legacy ADS values
		_albedoColor = _diffuse;
		_roughness = 1.0f - (_shininess / 128.0f);
		_metalness = _metallic ? 1.0f : 0.0f;

		// Estimate IOR from specular
		float specGray = (_specular.x() + _specular.y() + _specular.z()) / 3.0f;
		if (specGray > 0.01f)
		{
			float sqrtF0 = sqrt(specGray);
			_ior = (1.0f + sqrtF0) / (1.0f - sqrtF0);
		}
	}

	// Ensure all values are properly clamped
	clampValues();
}


void Material::assignAutoPackingForPath(const QString& path)
{
	if (path.isEmpty()) return;

	// collect which packable slots reference this path
	// We'll check AO, Roughness, Metallic, Opacity
	struct Ref { QString key; ChannelPacking* pack; QString* pathPtr; };
	QVector<Ref> refs;

	auto pushIfMatches = [&](QString& slotPath, ChannelPacking& pack, const QString& keyName) {
		if (!slotPath.isEmpty() && slotPath == path) refs.append({ keyName, &pack, &slotPath });
		};

	pushIfMatches(_aoMapPath, _aoPacking, "ao");
	pushIfMatches(_roughnessMapPath, _roughnessPacking, "roughness");
	pushIfMatches(_metallicMapPath, _metallicPacking, "metallic");
	pushIfMatches(_opacityMapPath, _opacityPacking, "opacity");

	if (refs.isEmpty()) return;

	if (refs.size() >= 2)
	{
		// multiple references -> assume packed ORM/AORM; assign sensible defaults:
		for (const Ref& r : refs)
		{
			if (r.key == "ao")
			{
				r.pack->channel = 0; r.pack->invert = false; r.pack->scale = 1.0f; r.pack->bias = 0.0f;
			}
			else if (r.key == "roughness")
			{
				r.pack->channel = 1; r.pack->invert = false; r.pack->scale = 1.0f; r.pack->bias = 0.0f;
			}
			else if (r.key == "metallic")
			{
				r.pack->channel = 2; r.pack->invert = false; r.pack->scale = 1.0f; r.pack->bias = 0.0f;
			}
			else if (r.key == "opacity")
			{
				// prefer alpha for opacity in packed images
				r.pack->channel = 3; r.pack->invert = false; r.pack->scale = 1.0f; r.pack->bias = 0.0f;
			}
		}
		return;
	}

	// Single reference only: default to single-channel (R)
	Ref single = refs.first();
	single.pack->channel = 0;
	single.pack->invert = false;
	single.pack->scale = 1.0f;
	single.pack->bias = 0.0f;
}

// ---------------------- END: Robust updateConsistency + helper ----------------------


#include <QVariant>
#include <QVariantMap>
#include <QVector3D>
#include <QJsonObject>

// Helper: read QVector3D from variant (list or map)
static QVector3D readVec3(const QVariant& v, const QVector3D& fallback = QVector3D(0.8f, 0.8f, 0.8f))
{
	if (!v.isValid()) return fallback;
	if (v.typeId() == QMetaType::QVariantList)
	{
		QVariantList l = v.toList();
		if (l.size() >= 3)
		{
			return QVector3D(l[0].toFloat(), l[1].toFloat(), l[2].toFloat());
		}
	}
	else if (v.canConvert<QVector3D>())
	{
		return v.value<QVector3D>();
	}
	else if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Int)
	{
		float f = v.toFloat();
		return QVector3D(f, f, f);
	}
	else if (v.typeId() == QMetaType::QVariantMap)
	{
		QVariantMap mm = v.toMap();
		return QVector3D(mm.value("x", fallback.x()).toFloat(),
			mm.value("y", fallback.y()).toFloat(),
			mm.value("z", fallback.z()).toFloat());
	}
	return fallback;
}

// Read float with fallback
static float readFloat(const QVariant& v, float fallback)
{
	if (!v.isValid()) return fallback;
	bool ok = false;
	float f = v.toFloat(&ok);
	if (!ok) return fallback;
	return f;
}

// ============================================================================
// GLenum <-> String Conversion (for texture sampler serialization)
// ============================================================================

static QString glEnumToString(GLenum e)
{
	// Wrap modes
	if (e == GL_REPEAT) return "repeat";
	if (e == GL_CLAMP_TO_EDGE) return "clamp";
	if (e == GL_MIRRORED_REPEAT) return "mirror";

	// Texture filters
	if (e == GL_LINEAR) return "linear";
	if (e == GL_NEAREST) return "nearest";
	if (e == GL_LINEAR_MIPMAP_LINEAR) return "linear_mipmap_linear";
	if (e == GL_LINEAR_MIPMAP_NEAREST) return "linear_mipmap_nearest";
	if (e == GL_NEAREST_MIPMAP_LINEAR) return "nearest_mipmap_linear";
	if (e == GL_NEAREST_MIPMAP_NEAREST) return "nearest_mipmap_nearest";

	return "unknown";
}

static GLenum stringToGLEnum(const QString& s)
{
	// Wrap modes
	if (s == "repeat") return GL_REPEAT;
	if (s == "clamp") return GL_CLAMP_TO_EDGE;
	if (s == "mirror") return GL_MIRRORED_REPEAT;

	// Texture filters
	if (s == "linear") return GL_LINEAR;
	if (s == "nearest") return GL_NEAREST;
	if (s == "linear_mipmap_linear") return GL_LINEAR_MIPMAP_LINEAR;
	if (s == "linear_mipmap_nearest") return GL_LINEAR_MIPMAP_NEAREST;
	if (s == "nearest_mipmap_linear") return GL_NEAREST_MIPMAP_LINEAR;
	if (s == "nearest_mipmap_nearest") return GL_NEAREST_MIPMAP_NEAREST;

	return GL_REPEAT; // Default fallback
}

// ============================================================================
// TextureType <-> String Conversion (for texture metadata serialization)
// ============================================================================

static QString textureTypeToString(Material::TextureType type)
{
	switch (type) {
		case Material::TextureType::Albedo: return "albedo";
		case Material::TextureType::Metallic: return "metallic";
		case Material::TextureType::Roughness: return "roughness";
		case Material::TextureType::Normal: return "normal";
		case Material::TextureType::AmbientOcclusion: return "ao";
		case Material::TextureType::Opacity: return "opacity";
		case Material::TextureType::Emissive: return "emissive";
		case Material::TextureType::Height: return "height";
		case Material::TextureType::Transmission: return "transmission";
		case Material::TextureType::IOR: return "ior";
		case Material::TextureType::SheenColor: return "sheenColor";
		case Material::TextureType::SheenRoughness: return "sheenRoughness";
		case Material::TextureType::ClearcoatColor: return "clearcoatColor";
		case Material::TextureType::ClearcoatRoughness: return "clearcoatRoughness";
		case Material::TextureType::ClearcoatNormal: return "clearcoatNormal";
		case Material::TextureType::Iridescence: return "iridescence";
		case Material::TextureType::IridescenceThickness: return "iridescenceThickness";
		case Material::TextureType::SpecularFactor: return "specularFactor";
		case Material::TextureType::SpecularColor: return "specularColor";
		case Material::TextureType::Anisotropy: return "anisotropy";
		case Material::TextureType::DiffuseTransmission: return "diffuseTransmission";
		case Material::TextureType::DiffuseTransmissionColor: return "diffuseTransmissionColor";
		case Material::TextureType::Thickness: return "thickness";
		case Material::TextureType::Diffuse: return "diffuse";
		case Material::TextureType::SpecularGlossiness: return "specularGlossiness";
		default: return "";
	}
}

static Material::TextureType stringToTextureType(const QString& key)
{
	if (key == "albedo") return Material::TextureType::Albedo;
	if (key == "metallic") return Material::TextureType::Metallic;
	if (key == "roughness") return Material::TextureType::Roughness;
	if (key == "normal") return Material::TextureType::Normal;
	if (key == "ao") return Material::TextureType::AmbientOcclusion;
	if (key == "opacity") return Material::TextureType::Opacity;
	if (key == "emissive") return Material::TextureType::Emissive;
	if (key == "height") return Material::TextureType::Height;
	if (key == "transmission") return Material::TextureType::Transmission;
	if (key == "ior") return Material::TextureType::IOR;
	if (key == "sheenColor") return Material::TextureType::SheenColor;
	if (key == "sheenRoughness") return Material::TextureType::SheenRoughness;
	if (key == "clearcoatColor") return Material::TextureType::ClearcoatColor;
	if (key == "clearcoatRoughness") return Material::TextureType::ClearcoatRoughness;
	if (key == "clearcoatNormal") return Material::TextureType::ClearcoatNormal;
	if (key == "iridescence") return Material::TextureType::Iridescence;
	if (key == "iridescenceThickness") return Material::TextureType::IridescenceThickness;
	if (key == "specularFactor") return Material::TextureType::SpecularFactor;
	if (key == "specularColor") return Material::TextureType::SpecularColor;
	if (key == "anisotropy") return Material::TextureType::Anisotropy;
	if (key == "diffuseTransmission") return Material::TextureType::DiffuseTransmission;
	if (key == "diffuseTransmissionColor") return Material::TextureType::DiffuseTransmissionColor;
	if (key == "thickness") return Material::TextureType::Thickness;
	if (key == "diffuse") return Material::TextureType::Diffuse;
	if (key == "specularGlossiness") return Material::TextureType::SpecularGlossiness;

	return Material::TextureType::Albedo; // Default fallback
}

Material Material::fromVariantMap(const QVariantMap& m)
{
	// small local readers
	auto readVec3 = [](const QVariant& v, const QVector3D& fallback) -> QVector3D {
		if (!v.isValid()) return fallback;
		if (v.typeId() == QMetaType::QVariantList)
		{
			const QVariantList l = v.toList();
			if (l.size() >= 3) return QVector3D(l[0].toFloat(), l[1].toFloat(), l[2].toFloat());
		}
		else if (v.typeId() == QMetaType::QVariantMap)
		{
			const QVariantMap mm = v.toMap();
			return QVector3D(mm.value("x", fallback.x()).toFloat(),
				mm.value("y", fallback.y()).toFloat(),
				mm.value("z", fallback.z()).toFloat());
		}
		else if (v.canConvert<QVector3D>())
		{
			return v.value<QVector3D>();
		}
		return fallback;
		};

	auto readFloat = [](const QVariant& v, float fallback) -> float {
		if (!v.isValid()) return fallback;
		bool ok = false;
		float f = v.toFloat(&ok);
		return ok ? f : fallback;
		};

	auto readBool = [](const QVariant& v, bool fallback) -> bool {
		if (!v.isValid()) return fallback;
		return v.toBool();
		};

	Material mat; // default constructed material

	// Note: Legacy ADS fields may exist in old JSON files but will be overwritten by
	// updateConsistency() which always derives ADS from PBR values. We read them for
	// backward compatibility but don't use them.
	if (m.contains("ambient"))          { mat._ambient = readVec3(m.value("ambient"), mat._ambient); }
	if (m.contains("diffuse"))          { mat._diffuse = readVec3(m.value("diffuse"), mat._diffuse); }
	if (m.contains("specular"))         { mat._specular = readVec3(m.value("specular"), mat._specular); }
	if (m.contains("emissive"))         mat._emissive = readVec3(m.value("emissive"), mat._emissive);
	if (m.contains("shininess"))        { mat._shininess = readFloat(m.value("shininess"), mat._shininess); }
	if (m.contains("emissiveStrength")) mat._emissiveStrength = readFloat(m.value("emissiveStrength"), mat._emissiveStrength);
	if (m.contains("opacity"))          mat._opacity = readFloat(m.value("opacity"), mat._opacity);
	if (m.contains("occlusionStrength")) mat._occlusionStrength = readFloat(m.value("occlusionStrength"), mat._occlusionStrength);

	// ---------------- PBR core (assign internal members directly) ----------------
	if (m.contains("albedo"))           mat._albedoColor = readVec3(m.value("albedo"), mat._albedoColor);

	// metalness/metallic: prefer float "metalness", fall back to boolean "metallic"
	if (m.contains("metalness"))
	{
		mat._metalness = qBound(0.0f, readFloat(m.value("metalness"), mat._metalness), 1.0f);
		mat._metallic = (mat._metalness >= 0.5f);
	}
	else if (m.contains("metallic"))
	{
		mat._metallic = readBool(m.value("metallic"), mat._metallic);
		mat._metalness = mat._metallic ? 1.0f : 0.0f;
	}

	if (m.contains("roughness"))        mat._roughness = qBound(0.0f, readFloat(m.value("roughness"), mat._roughness), 1.0f);
	if (m.contains("normalScale"))      mat._normalScale = readFloat(m.value("normalScale"), mat._normalScale);
	if (m.contains("heightScale"))      mat._heightScale = readFloat(m.value("heightScale"), mat._heightScale);
	if (m.contains("clearcoatNormalScale")) mat._clearcoatNormalScale = readFloat(m.value("clearcoatNormalScale"), mat._clearcoatNormalScale);
	// KHR_materials_transmission
	if (m.contains("ior"))              mat._ior = readFloat(m.value("ior"), mat._ior);
	if (m.contains("transmission"))     mat._transmission = readFloat(m.value("transmission"), mat._transmission);
	if (m.contains("thickness")) mat._thicknessFactor = readFloat(m.value("thickness"), mat._thicknessFactor);
	if (m.contains("attenuationDistance")) mat._attenuationDistance = readFloat(m.value("attenuationDistance"), mat._attenuationDistance);
	if (m.contains("attenuationColor"))  mat._attenuationColor = readVec3(m.value("attenuationColor"), mat._attenuationColor);
	if (m.contains("dispersion")) 	  mat._dispersion = readFloat(m.value("dispersion"), mat._dispersion);
	if (m.contains("specularFactor"))   mat._specularFactor = qBound(0.0f, readFloat(m.value("specularFactor"), mat._specularFactor), 1.0f);
	if (m.contains("specularColorFactor")) mat._specularColorFactor = readVec3(m.value("specularColorFactor"), mat._specularColorFactor);
	if (m.contains("anisotropyStrength")) mat._anisotropyStrength = qBound(0.0f, readFloat(m.value("anisotropyStrength"), mat._anisotropyStrength), 1.0f);
	if (m.contains("anisotropyRotation")) mat._anisotropyRotation = readFloat(m.value("anisotropyRotation"), mat._anisotropyRotation);
	if (m.contains("diffuseTransmissionFactor")) mat._diffuseTransmissionFactor = qBound(0.0f, readFloat(m.value("diffuseTransmissionFactor"), mat._diffuseTransmissionFactor), 1.0f);
	if (m.contains("diffuseTransmissionColorFactor")) mat._diffuseTransmissionColorFactor = readVec3(m.value("diffuseTransmissionColorFactor"), mat._diffuseTransmissionColorFactor);
	if (m.contains("unlit"))            mat._unlit = readBool(m.value("unlit"), mat._unlit);
	if (m.contains("multiScatterColor")) mat._multiScatterColor = readVec3(m.value("multiScatterColor"), mat._multiScatterColor);
	if (m.contains("hasVolumeScattering")) mat._hasVolumeScattering = readBool(m.value("hasVolumeScattering"), mat._hasVolumeScattering);

	// KHR_materials_clearcoat
	if (m.contains("clearcoat"))        mat._clearcoat = readFloat(m.value("clearcoat"), mat._clearcoat);
	if (m.contains("clearcoatRoughness")) mat._clearcoatRoughness = readFloat(m.value("clearcoatRoughness"), mat._clearcoatRoughness);
	// KHR_materials_sheen
	if (m.contains("sheenColor"))       mat._sheenColor = readVec3(m.value("sheenColor"), mat._sheenColor);
	if (m.contains("sheenRoughness"))   mat._sheenRoughness = readFloat(m.value("sheenRoughness"), mat._sheenRoughness);
	// KHR_materials_iridescence
	if (m.contains("iridescence"))      mat._iridescenceFactor = qBound(0.0f, readFloat(m.value("iridescence"), mat._iridescenceFactor), 1.0f);
	if (m.contains("iridescenceFactor")) mat._iridescenceFactor = qBound(0.0f, readFloat(m.value("iridescenceFactor"), mat._iridescenceFactor), 1.0f);
	if (m.contains("iridescenceIor"))   mat._iridescenceIor = readFloat(m.value("iridescenceIor"), mat._iridescenceIor);
	if (m.contains("iridescenceThicknessMin")) mat._iridescenceThicknessMin = readFloat(m.value("iridescenceThicknessMin"), mat._iridescenceThicknessMin);
	if (m.contains("iridescenceThicknessMax")) mat._iridescenceThicknessMax = readFloat(m.value("iridescenceThicknessMax"), mat._iridescenceThicknessMax);


	// energy-conserving factor / extra PBR attributes if present
	if (m.contains("metallicFactor"))   mat._metalness = qBound(0.0f, readFloat(m.value("metallicFactor"), mat._metalness), 1.0f); // alternate key

	// ===== BACKWARD COMPATIBILITY: Initialize PBR from ADS if needed =====
	// For old ADS-only materials without explicit PBR values, use ADS to initialize PBR
	const float EPS = 1e-3f;
	bool albedoLooksDefault = (mat._albedoColor.lengthSquared() < (EPS * EPS));
	bool hasDiffuse = m.contains("diffuse");
	if (albedoLooksDefault && hasDiffuse)
	{
		// No explicit albedo, but have diffuse -> use diffuse as albedo
		mat._albedoColor = mat._diffuse;
	}

	// If no explicit metalness/roughness but have shininess, derive PBR from ADS
	bool hasShininessOnly = m.contains("shininess") && !m.contains("roughness");
	if (hasShininessOnly)
	{
		// Reverse mapping: shininess -> roughness
		mat._roughness = qBound(0.04f, 1.0f - (mat._shininess / 128.0f), 1.0f);
	}

	// Derive IOR from specular if not explicitly set
	bool needsIORFromSpecular = !m.contains("ior") && m.contains("specular");
	if (needsIORFromSpecular)
	{
		float specGray = (mat._specular.x() + mat._specular.y() + mat._specular.z()) / 3.0f;
		if (specGray > 1e-4f)
		{
			float s = qBound(1e-4f, specGray, 0.999f);
			float sqrtF0 = qSqrt(s);
			float n = 1.5f;
			if (sqrtF0 < 0.999f)
				n = (1.0f + sqrtF0) / (1.0f - sqrtF0);
			mat._ior = qBound(1.0f, n, 3.0f);
		}
	}

	// ---------------- Texture paths (strings) and support for combined maps ----------------
	// (value-only materials can ignore these; we still populate so later textured materials work)
	if (m.contains("albedoMapPath"))              mat._albedoMapPath = m.value("albedoMapPath").toString();
	if (m.contains("normalMapPath"))              mat._normalMapPath = m.value("normalMapPath").toString();
	if (m.contains("metallicMapPath"))            mat._metallicMapPath = m.value("metallicMapPath").toString();
	if (m.contains("roughnessMapPath"))           mat._roughnessMapPath = m.value("roughnessMapPath").toString();
	//if (m.contains("metallicRoughnessMapPath"))
	//{ // combined map (GLTF style)
	//	mat._metallicRoughnessMapPath = m.value("metallicRoughnessMapPath").toString();
	//	// optionally mirror combined into separate slots so older code can use them:
	//	if (mat._metallicMapPath.isEmpty()) mat._metallicMapPath = mat._metallicRoughnessMapPath;
	//	if (mat._roughnessMapPath.isEmpty()) mat._roughnessMapPath = mat._metallicRoughnessMapPath;
	//}
	if (m.contains("aoMapPath"))                  mat._aoMapPath = m.value("aoMapPath").toString();
	if (m.contains("opacityMapPath"))             mat._opacityMapPath = m.value("opacityMapPath").toString();
	if (m.contains("heightMapPath"))              mat._heightMapPath = m.value("heightMapPath").toString();
	if (m.contains("transmissionMapPath"))        mat._transmissionMapPath = m.value("transmissionMapPath").toString();
	if (m.contains("iorMapPath"))                 mat._iorMapPath = m.value("iorMapPath").toString();
	if (m.contains("sheenColorMapPath"))          mat._sheenColorMapPath = m.value("sheenColorMapPath").toString();
	if (m.contains("sheenRoughnessMapPath"))      mat._sheenRoughnessMapPath = m.value("sheenRoughnessMapPath").toString();
	if (m.contains("clearcoatColorMapPath"))      mat._clearcoatColorMapPath = m.value("clearcoatColorMapPath").toString();
	if (m.contains("clearcoatRoughnessMapPath"))  mat._clearcoatRoughnessMapPath = m.value("clearcoatRoughnessMapPath").toString();
	if (m.contains("clearcoatNormalMapPath"))     mat._clearcoatNormalMapPath = m.value("clearcoatNormalMapPath").toString();
	// KHR_materials_iridescence texture maps
	if (m.contains("iridescenceMapPath"))         mat._iridescenceMap = m.value("iridescenceMapPath").toString();
	if (m.contains("iridescenceThicknessMapPath")) mat._iridescenceThicknessMap = m.value("iridescenceThicknessMapPath").toString();

	// ---------------- Texture IDs (numeric slot IDs if texture manager uses them) ----------------
	if (m.contains("albedoTextureId"))            mat._albedoTextureId = m.value("albedoTextureId").toInt();
	if (m.contains("normalTextureId"))            mat._normalTextureId = m.value("normalTextureId").toInt();
	if (m.contains("metallicTextureId"))          mat._metallicTextureId = m.value("metallicTextureId").toInt();
	if (m.contains("roughnessTextureId"))         mat._roughnessTextureId = m.value("roughnessTextureId").toInt();
	if (m.contains("occlusionTextureId"))         mat._occlusionTextureId = m.value("occlusionTextureId").toInt();
	if (m.contains("opacityTextureId"))           mat._opacityTextureId = m.value("opacityTextureId").toInt();
	if (m.contains("heightTextureId"))            mat._heightTextureId = m.value("heightTextureId").toInt();
	if (m.contains("transmissionTextureId"))      mat._transmissionTextureId = m.value("transmissionTextureId").toInt();
	if (m.contains("iorTextureId"))               mat._iorTextureId = m.value("iorTextureId").toInt();
	// KHR_materials_iridescence texture IDs
	if (m.contains("iridescenceTextureId"))       mat._iridescenceTextureId = m.value("iridescenceTextureId").toInt();
	if (m.contains("iridescenceThicknessTextureId")) mat._iridescenceThicknessTextureId = m.value("iridescenceThicknessTextureId").toInt();

	// ---------------- Texcoord indices ----------------
	if (m.contains("albedoTexCoord"))             mat._albedoTexCoord = m.value("albedoTexCoord").toInt();
	if (m.contains("normalTexCoord"))             mat._normalTexCoord = m.value("normalTexCoord").toInt();
	if (m.contains("metallicRoughnessTexCoord"))  mat._metallicRoughnessTexCoord = m.value("metallicRoughnessTexCoord").toInt();
	//if (m.contains("aoTexCoord"))                 mat._aoTexCoord = m.value("aoTexCoord").toInt();

	// Support combined UV keys if present
	if (m.contains("uvTiling"))
	{
		QVariantList l = m.value("uvTiling").toList();
		if (l.size() >= 2)
		{
			mat._uvTilingU = l[0].toFloat();
			mat._uvTilingV = l[1].toFloat();
		}
	}
	if (m.contains("uvOffset"))
	{
		QVariantList l = m.value("uvOffset").toList();
		if (l.size() >= 2)
		{
			mat._uvOffsetU = l[0].toFloat();
			mat._uvOffsetV = l[1].toFloat();
		}
	}

	// ---------------- UV tiling & offset ----------------
	if (m.contains("uvTilingU"))                  mat._uvTilingU = readFloat(m.value("uvTilingU"), mat._uvTilingU);
	if (m.contains("uvTilingV"))                  mat._uvTilingV = readFloat(m.value("uvTilingV"), mat._uvTilingV);
	if (m.contains("uvOffsetU"))                  mat._uvOffsetU = readFloat(m.value("uvOffsetU"), mat._uvOffsetU);
	if (m.contains("uvOffsetV"))                  mat._uvOffsetV = readFloat(m.value("uvOffsetV"), mat._uvOffsetV);
	if (m.contains("diffuseColor"))               mat._diffuseColor = readVec3(m.value("diffuseColor"), mat._diffuseColor);
	if (m.contains("glossinessFactor"))           mat._glossinessFactor = qBound(0.0f, readFloat(m.value("glossinessFactor"), mat._glossinessFactor), 1.0f);
	if (m.contains("useSpecularGlossiness"))      mat._useSpecularGlossiness = readBool(m.value("useSpecularGlossiness"), mat._useSpecularGlossiness);
	if (m.contains("isGLTFMaterial"))             mat._isGLTFMaterial = readBool(m.value("isGLTFMaterial"), mat._isGLTFMaterial);

	// ---------------- Rendering flags & enums ----------------
	if (m.contains("shadingModel"))
	{
		QString sm = m.value("shadingModel").toString().toLower();
		if (sm == "pbr")       mat._shadingModel = ShadingModel::PBR;
		else if (sm == "unlit") mat._shadingModel = ShadingModel::Unlit;
		else if (sm == "toon")  mat._shadingModel = ShadingModel::Toon;
		else                    mat._shadingModel = ShadingModel::BlinnPhong;
	}

	if (m.contains("blendMode"))
	{
		QString bm = m.value("blendMode").toString().toLower();
		if (bm == "alpha")      mat._blendMode = BlendMode::Alpha;
		else if (bm == "additive") mat._blendMode = BlendMode::Additive;
		else if (bm == "multiply") mat._blendMode = BlendMode::Multiply;
		else if (bm == "masked")   mat._blendMode = BlendMode::Masked;
		else                       mat._blendMode = BlendMode::Opaque;
	}

	if (m.contains("twoSided"))         mat._twoSided = readBool(m.value("twoSided"), mat._twoSided);
	if (m.contains("wireframe"))        mat._wireframe = readBool(m.value("wireframe"), mat._wireframe);
	if (m.contains("alphaThreshold"))   mat._alphaThreshold = readFloat(m.value("alphaThreshold"), mat._alphaThreshold);
	if (m.contains("hasTextureAlpha"))  mat._hasTextureAlpha = readBool(m.value("hasTextureAlpha"), mat._hasTextureAlpha);

	// ---------------- Optional: hints and alternate keys ----------------
	// support alternate keys sometimes used by other exporters
	if (m.contains("diffuseColor") && !m.contains("diffuse"))
	{
		mat._diffuse = readVec3(m.value("diffuseColor"), mat._diffuse);
	}
	if (m.contains("baseColor") && !m.contains("albedo"))
	{
		mat._albedoColor = readVec3(m.value("baseColor"), mat._albedoColor);
	}
	/*if (m.contains("metallicRoughnessMap") && mat._metallicRoughnessMapPath.isEmpty())
	{
		mat._metallicRoughnessMapPath = m.value("metallicRoughnessMap").toString();
		if (mat._metallicMapPath.isEmpty()) mat._metallicMapPath = mat._metallicRoughnessMapPath;
		if (mat._roughnessMapPath.isEmpty()) mat._roughnessMapPath = mat._metallicRoughnessMapPath;
	}*/

	// ============================================================================
	// Load Texture Metadata (transforms, sampler settings, channel packing)
	// ============================================================================
	if (m.contains("textureMetadata"))
	{
		QVariantMap textureMetadataMap = m.value("textureMetadata").toMap();

		for (auto it = textureMetadataMap.begin(); it != textureMetadataMap.end(); ++it)
		{
			QString typeKey = it.key();
			Material::TextureType type = stringToTextureType(typeKey);
			QVariantMap texMetadata = it.value().toMap();

			Material::Texture tex;
			tex.path = texMetadata.value("path").toString().toStdString();

			// Reconstruct scale (2D vector)
			QVariantList scaleList = texMetadata.value("scale").toList();
			if (scaleList.size() >= 2) {
				tex.scale = glm::vec2(scaleList[0].toFloat(), scaleList[1].toFloat());
			}

			// Reconstruct offset (2D vector)
			QVariantList offsetList = texMetadata.value("offset").toList();
			if (offsetList.size() >= 2) {
				tex.offset = glm::vec2(offsetList[0].toFloat(), offsetList[1].toFloat());
			}

			// Reconstruct rotation and texCoordIndex
			tex.rotation = readFloat(texMetadata.value("rotation"), tex.rotation);
			tex.texCoordIndex = texMetadata.value("texCoordIndex").toInt();

			// Reconstruct sampler settings (wrap modes and filters)
			tex.wrapS = stringToGLEnum(texMetadata.value("wrapS").toString());
			tex.wrapT = stringToGLEnum(texMetadata.value("wrapT").toString());
			tex.magFilter = stringToGLEnum(texMetadata.value("magFilter").toString());
			tex.minFilter = stringToGLEnum(texMetadata.value("minFilter").toString());

			// Set texture with all metadata
			mat.setTexture(type, tex);

			// Load channel packing if present (for metallic, roughness, ao, opacity)
			if (texMetadata.contains("packing"))
			{
				QVariantMap packingMap = texMetadata.value("packing").toMap();
				Material::ChannelPacking packing;
				packing.channel = packingMap.value("channel").toInt();
				packing.invert = packingMap.value("invert").toBool();
				packing.scale = readFloat(packingMap.value("scale"), packing.scale);
				packing.bias = readFloat(packingMap.value("bias"), packing.bias);
				mat.setPackingFor(typeKey, packing);
			}
		}
	}

	// ---------------- Finalization: clamp values and compute ADS from PBR ----------------
	mat.clampValues();        // clamp ranges
	mat.updateConsistency();  // compute ADS from PBR (ensures ADS is always synced with PBR)

	// NOTE: for value-only predefined materials we intentionally don't do texture loading here.
	// For later direct texture registration, do it after updateConsistency():
	// e.g.
	// if (!mat._albedoMapPath.isEmpty() && mat._albedoTextureId < 0) {
	//     mat._albedoTextureId = TextureManager::instance()->load(mat._albedoMapPath);
	// }

	return mat;
}


QVariantMap Material::toVariantMap() const
{
	QVariantMap m;

	// helper to convert QVector3D -> QVariantList
	auto vec3ToList = [](const QVector3D& v) -> QVariantList {
		return QVariantList{ QVariant(v.x()), QVariant(v.y()), QVariant(v.z()) };
		};

	// --- Legacy ADS (never save; always derived from PBR in-memory for rendering mode switching) ---
	// ADS values are always computed from PBR via convertPBRtoADS() when needed for rendering
	m.insert("emissive", vec3ToList(emissive()));  // emissive is independent of shading model
	m.insert("emissiveStrength", QVariant(emissiveStrength()));
	m.insert("opacity", QVariant(opacity()));
	m.insert("occlusionStrength", QVariant(occlusionStrength()));

	// --- PBR core ---
	m.insert("albedo", vec3ToList(albedoColor()));
	m.insert("metalness", QVariant(metalness()));
	// Keep legacy boolean for compatibility
	m.insert("metallic", QVariant(metallic())); // boolean
	m.insert("roughness", QVariant(roughness()));
	m.insert("normalScale", QVariant(normalScale()));
	m.insert("heightScale", QVariant(heightScale()));
	m.insert("clearcoatNormalScale", QVariant(clearcoatNormalScale()));
	m.insert("ior", QVariant(ior()));
	m.insert("transmission", QVariant(transmission()));
	m.insert("thickness", QVariant(thicknessFactor()));
	m.insert("attenuationDistance", QVariant(attenuationDistance()));
	m.insert("attenuationColor", vec3ToList(attenuationColor()));
	m.insert("dispersion", QVariant(dispersion()));
	m.insert("specularFactor", QVariant(specularFactor()));
	m.insert("specularColorFactor", vec3ToList(specularColorFactor()));
	m.insert("anisotropyStrength", QVariant(anisotropyStrength()));
	m.insert("anisotropyRotation", QVariant(anisotropyRotation()));
	m.insert("diffuseTransmissionFactor", QVariant(diffuseTransmissionFactor()));
	m.insert("diffuseTransmissionColorFactor", vec3ToList(diffuseTransmissionColorFactor()));
	m.insert("unlit", QVariant(isUnlit()));
	m.insert("multiScatterColor", vec3ToList(multiScatterColor()));
	m.insert("hasVolumeScattering", QVariant(hasVolumeScattering()));
	m.insert("clearcoat", QVariant(clearcoat()));
	m.insert("clearcoatRoughness", QVariant(clearcoatRoughness()));
	m.insert("sheenColor", vec3ToList(sheenColor()));
	m.insert("sheenRoughness", QVariant(sheenRoughness()));
	// KHR_materials_iridescence
	m.insert("iridescence", QVariant(iridescenceFactor()));
	m.insert("iridescenceFactor", QVariant(iridescenceFactor()));
	m.insert("iridescenceIor", QVariant(iridescenceIor()));
	m.insert("iridescenceThicknessMin", QVariant(iridescenceThicknessMin()));
	m.insert("iridescenceThicknessMax", QVariant(iridescenceThicknessMax()));

	// --- Texture path slots (strings) ---
	m.insert("albedoMapPath", QVariant(albedoMapPath()));
	m.insert("normalMapPath", QVariant(normalMapPath()));
	m.insert("metallicMapPath", QVariant(metallicMapPath()));
	m.insert("roughnessMapPath", QVariant(roughnessMapPath()));
	m.insert("aoMapPath", QVariant(aoMapPath()));
	m.insert("opacityMapPath", QVariant(opacityMapPath()));
	m.insert("heightMapPath", QVariant(heightMapPath()));
	m.insert("transmissionMapPath", QVariant(transmissionMapPath()));
	m.insert("iorMapPath", QVariant(iorMapPath()));
	m.insert("sheenColorMapPath", QVariant(sheenColorMapPath()));
	m.insert("sheenRoughnessMapPath", QVariant(sheenRoughnessMapPath()));
	m.insert("clearcoatColorMapPath", QVariant(clearcoatColorMapPath()));
	m.insert("clearcoatRoughnessMapPath", QVariant(clearcoatRoughnessMapPath()));
	m.insert("clearcoatNormalMapPath", QVariant(clearcoatNormalMapPath()));
	// KHR_materials_iridescence texture maps
	m.insert("iridescenceMapPath", QVariant(iridescenceMap()));
	m.insert("iridescenceThicknessMapPath", QVariant(iridescenceThicknessMap()));

	// --- Texture ids (numeric) ---
	m.insert("albedoTextureId", QVariant(albedoTextureId()));
	m.insert("normalTextureId", QVariant(normalTextureId()));
	m.insert("metallicTextureId", QVariant(metallicTextureId()));
	m.insert("roughnessTextureId", QVariant(roughnessTextureId()));
	m.insert("aoTextureId", QVariant(occlusionTextureId()));
	m.insert("opacityTextureId", QVariant(opacityTextureId()));
	m.insert("heightTextureId", QVariant(heightTextureId()));
	m.insert("transmissionTextureId", QVariant(transmissionTextureId()));
	// KHR_materials_iridescence texture IDs
	m.insert("iridescenceTextureId", QVariant(iridescenceTextureId()));
	m.insert("iridescenceThicknessTextureId", QVariant(iridescenceThicknessTextureId()));

	// --- Texcoord indices ---
	m.insert("albedoTexCoord", QVariant(albedoTexCoord()));
	m.insert("normalTexCoord", QVariant(normalTexCoord()));
	m.insert("metallicRoughnessTexCoord", QVariant(metallicRoughnessTexCoord()));
	//m.insert("aoTexCoord", QVariant(aoTexCoord()));

	// Also emit combined UV keys for compatibility
	m.insert("uvTiling", QVariantList{ QVariant(uvTilingU()), QVariant(uvTilingV()) });
	m.insert("uvOffset", QVariantList{ QVariant(uvOffsetU()), QVariant(uvOffsetV()) });

	// --- UV transform ---
	m.insert("uvTilingU", QVariant(uvTilingU()));
	m.insert("uvTilingV", QVariant(uvTilingV()));
	m.insert("uvOffsetU", QVariant(uvOffsetU()));
	m.insert("uvOffsetV", QVariant(uvOffsetV()));
	m.insert("diffuseColor", vec3ToList(diffuseColor()));
	m.insert("glossinessFactor", QVariant(glossinessFactor()));
	m.insert("useSpecularGlossiness", QVariant(getUseSpecularGlossiness()));
	m.insert("isGLTFMaterial", QVariant(isGLTFMaterial()));

	// --- Rendering flags / shading model / blend ---
	// shadingModel -> string (we emit lowercase to match fromVariantMap checks)
	QString sm;
	switch (shadingModel())
	{
	case ShadingModel::PBR: sm = "pbr"; break;
	case ShadingModel::Unlit: sm = "unlit"; break;
	default: sm = "blinnphong"; break;
	}
	m.insert("shadingModel", QVariant(sm));

	// blendMode -> string
	QString bm;
	switch (blendMode())
	{
	case BlendMode::Alpha: bm = "Alpha"; break;
	case BlendMode::Additive: bm = "Add"; break;
	default: bm = "Opaque"; break;
	}
	m.insert("blendMode", QVariant(bm));

	m.insert("twoSided", QVariant(twoSided()));
	m.insert("alphaThreshold", QVariant(alphaThreshold()));

	// ============================================================================
	// Serialize Texture Metadata (transforms, sampler settings, channel packing)
	// ============================================================================
	QVariantMap textureMetadataMap;

	for (int i = 0; i < static_cast<int>(TextureType::Count); ++i)
	{
		TextureType type = static_cast<TextureType>(i);
		const Texture& tex = texture(type);

		// Skip unloaded textures (empty path)
		if (tex.path.empty()) continue;

		QString typeKey = textureTypeToString(type);
		if (typeKey.isEmpty()) continue; // Skip unknown types

		QVariantMap texMetadata;
		texMetadata.insert("path", QString::fromStdString(tex.path));
		texMetadata.insert("scale", QVariantList{ QVariant(tex.scale.x), QVariant(tex.scale.y) });
		texMetadata.insert("offset", QVariantList{ QVariant(tex.offset.x), QVariant(tex.offset.y) });
		texMetadata.insert("rotation", QVariant(tex.rotation));
		texMetadata.insert("texCoordIndex", QVariant(tex.texCoordIndex));
		texMetadata.insert("wrapS", glEnumToString(tex.wrapS));
		texMetadata.insert("wrapT", glEnumToString(tex.wrapT));
		texMetadata.insert("magFilter", glEnumToString(tex.magFilter));
		texMetadata.insert("minFilter", glEnumToString(tex.minFilter));

		// Add channel packing for supported types (metallic, roughness, ao, opacity)
		if (typeKey == "metallic" || typeKey == "roughness" ||
		    typeKey == "ao" || typeKey == "opacity")
		{
			ChannelPacking packing = packingFor(typeKey);
			QVariantMap packingMap;
			packingMap.insert("channel", packing.channel);
			packingMap.insert("invert", packing.invert);
			packingMap.insert("scale", packing.scale);
			packingMap.insert("bias", packing.bias);
			texMetadata.insert("packing", packingMap);
		}

		textureMetadataMap.insert(typeKey, texMetadata);
	}

	if (!textureMetadataMap.isEmpty()) {
		m.insert("textureMetadata", textureMetadataMap);
	}

	return m;
}



// === HELPER FUNCTIONS FOR ADVANCED MATERIALS ===
// Function to create a material with time-varying properties (for animations)
Material Material::createAnimatedEmissive(const QVector3D& baseColor,
	const QVector3D& emissiveColor,
	float emissiveStrength,
	float time)
{
	Material mat;

	mat.setAmbient(baseColor * 0.1f);
	mat.setDiffuse(baseColor);
	mat.setSpecular(QVector3D(0.2f, 0.2f, 0.2f));

	// Animate emissive strength with time
	float animatedStrength = emissiveStrength * (0.5f + 0.5f * sin(time));
	mat.setEmissive(emissiveColor);
	mat.setEmissiveStrength(animatedStrength);

	mat.setShininess(64.0f);
	mat.setMetallic(false);
	mat.setOpacity(1.0f);

	// PBR properties
	mat.setAlbedoColor(baseColor);
	mat.setMetalness(0.0f);
	mat.setRoughness(0.3f);
	mat.setIOR(1.5f);
	mat.setShadingModel(ShadingModel::PBR);
	mat.updateConsistency();

	return mat;
}

// Function to blend two materials based on a factor (useful for layered materials)
Material Material::blendMaterials(const Material& mat1,
	const Material& mat2,
	float factor)
{
	factor = std::clamp(factor, 0.0f, 1.0f);
	float invFactor = 1.0f - factor;

	Material result;

	// Blend ADS properties
	result.setAmbient(mat1.ambient() * invFactor + mat2.ambient() * factor);
	result.setDiffuse(mat1.diffuse() * invFactor + mat2.diffuse() * factor);
	result.setSpecular(mat1.specular() * invFactor + mat2.specular() * factor);
	result.setEmissive(mat1.emissive() * invFactor + mat2.emissive() * factor);
	result.setShininess(mat1.shininess() * invFactor + mat2.shininess() * factor);
	result.setOpacity(mat1.opacity() * invFactor + mat2.opacity() * factor);

	// Blend PBR properties
	result.setAlbedoColor(mat1.albedoColor() * invFactor + mat2.albedoColor() * factor);
	result.setMetalness(mat1.metalness() * invFactor + mat2.metalness() * factor);
	result.setRoughness(mat1.roughness() * invFactor + mat2.roughness() * factor);
	result.setEmissiveStrength(mat1.emissiveStrength() * invFactor + mat2.emissiveStrength() * factor);
	result.setIOR(mat1.ior() * invFactor + mat2.ior() * factor);
	result.setClearcoat(mat1.clearcoat() * invFactor + mat2.clearcoat() * factor);
	result.setClearcoatRoughness(mat1.clearcoatRoughness() * invFactor + mat2.clearcoatRoughness() * factor);
	result.setSheenColor(mat1.sheenColor() * invFactor + mat2.sheenColor() * factor);
	result.setSheenRoughness(mat1.sheenRoughness() * invFactor + mat2.sheenRoughness() * factor);
	result.setTransmission(mat1.transmission() * invFactor + mat2.transmission() * factor);

	// Choose the primary material's shading model and blend mode
	result.setShadingModel(factor > 0.5f ? mat2.shadingModel() : mat1.shadingModel());
	result.setBlendMode(factor > 0.5f ? mat2.blendMode() : mat1.blendMode());

	result.updateConsistency();
	return result;
}

Material::ChannelPacking Material::packingFor(const QString& key) const
{
	if (key == "metallic")   return _metallicPacking;
	if (key == "roughness")  return _roughnessPacking;
	if (key == "ao")         return _aoPacking;
	if (key == "opacity")    return _opacityPacking;
	// Fallback: return a neutral pack (R, no invert, scale=1,bias=0)
	return ChannelPacking{};
}

void Material::setPackingFor(const QString& key, const ChannelPacking& p)
{
	if (key == "metallic") { _metallicPacking = p; return; }
	if (key == "roughness") { _roughnessPacking = p; return; }
	if (key == "ao") { _aoPacking = p; return; }
	if (key == "opacity") { _opacityPacking = p; return; }
	// Unknown key: ignore (or assert)
}

// Sync internal texture parameters to UI-exposed properties
void Material::syncTextureParameters()
{
	// Albedo / Base Color Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Albedo)];
		_albedoTextureId = static_cast<int>(tex.id);
		_albedoMapPath = QString::fromStdString(tex.path);
		_albedoTexCoord = tex.texCoordIndex;
		_albedoTexTransform.texCoord = tex.texCoordIndex;
		_albedoTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_albedoTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_albedoTexTransform.texRotation = tex.rotation;
	}

	// Metallic Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Metallic)];
		_metallicTextureId = static_cast<int>(tex.id);
		_metallicMapPath = QString::fromStdString(tex.path);
		_metallicTexTransform.texCoord = tex.texCoordIndex;
		_metallicTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_metallicTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_metallicTexTransform.texRotation = tex.rotation;
	}

	// Roughness Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Roughness)];
		_roughnessTextureId = static_cast<int>(tex.id);
		_roughnessMapPath = QString::fromStdString(tex.path);
		_roughnessTexTransform.texCoord = tex.texCoordIndex;
		_roughnessTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_roughnessTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_roughnessTexTransform.texRotation = tex.rotation;
	}

	// Normal Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Normal)];
		_normalTextureId = static_cast<int>(tex.id);
		_normalMapPath = QString::fromStdString(tex.path);
		_normalTexCoord = tex.texCoordIndex;
		_normalTexTransform.texCoord = tex.texCoordIndex;
		_normalTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_normalTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_normalTexTransform.texRotation = tex.rotation;
	}

	// Ambient Occlusion (AO) Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::AmbientOcclusion)];
		_occlusionTextureId = static_cast<int>(tex.id);
		_aoMapPath = QString::fromStdString(tex.path);
		_occlusionTexTransform.texCoord = tex.texCoordIndex;
		_occlusionTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_occlusionTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_occlusionTexTransform.texRotation = tex.rotation;
	}

	// Opacity Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Opacity)];
		_opacityTextureId = static_cast<int>(tex.id);
		_opacityMapPath = QString::fromStdString(tex.path);
		_opacityTexTransform.texCoord = tex.texCoordIndex;
		_opacityTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_opacityTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_opacityTexTransform.texRotation = tex.rotation;
	}

	// Emissive Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Emissive)];
		_emissiveTextureId = static_cast<int>(tex.id);
		_emissiveMapPath = QString::fromStdString(tex.path);
		_emissiveTexTransform.texCoord = tex.texCoordIndex;
		_emissiveTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_emissiveTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_emissiveTexTransform.texRotation = tex.rotation;
	}

	// Height Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Height)];
		_heightTextureId = static_cast<int>(tex.id);
		_heightMapPath = QString::fromStdString(tex.path);
		_heightTexTransform.texCoord = tex.texCoordIndex;
		_heightTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_heightTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_heightTexTransform.texRotation = tex.rotation;
	}

	// Transmission Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Transmission)];
		_transmissionTextureId = static_cast<int>(tex.id);
		_transmissionMapPath = QString::fromStdString(tex.path);
		_transmissionTexTransform.texCoord = tex.texCoordIndex;
		_transmissionTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_transmissionTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_transmissionTexTransform.texRotation = tex.rotation;
	}

	// IOR Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::IOR)];
		_iorTextureId = static_cast<int>(tex.id);
		_iorMapPath = QString::fromStdString(tex.path);
		_iorTexTransform.texCoord = tex.texCoordIndex;
		_iorTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_iorTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_iorTexTransform.texRotation = tex.rotation;
	}

	// Sheen Color Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::SheenColor)];
		_sheenColorTextureId = static_cast<int>(tex.id);
		_sheenColorMapPath = QString::fromStdString(tex.path);
		_sheenColorTexTransform.texCoord = tex.texCoordIndex;
		_sheenColorTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_sheenColorTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_sheenColorTexTransform.texRotation = tex.rotation;
	}

	// Sheen Roughness Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::SheenRoughness)];
		_sheenRoughnessTextureId = static_cast<int>(tex.id);
		_sheenRoughnessMapPath = QString::fromStdString(tex.path);
		_sheenRoughnessTexTransform.texCoord = tex.texCoordIndex;
		_sheenRoughnessTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_sheenRoughnessTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_sheenRoughnessTexTransform.texRotation = tex.rotation;
	}

	// Clearcoat Color Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::ClearcoatColor)];
		_clearcoatColorTextureId = static_cast<int>(tex.id);
		_clearcoatColorMapPath = QString::fromStdString(tex.path);
		_clearcoatColorTexTransform.texCoord = tex.texCoordIndex;
		_clearcoatColorTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_clearcoatColorTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_clearcoatColorTexTransform.texRotation = tex.rotation;
	}

	// Clearcoat Roughness Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::ClearcoatRoughness)];
		_clearcoatRoughnessTextureId = static_cast<int>(tex.id);
		_clearcoatRoughnessMapPath = QString::fromStdString(tex.path);
		_clearcoatRoughnessTexTransform.texCoord = tex.texCoordIndex;
		_clearcoatRoughnessTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_clearcoatRoughnessTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_clearcoatRoughnessTexTransform.texRotation = tex.rotation;
	}

	// Clearcoat Normal Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::ClearcoatNormal)];
		_clearcoatNormalTextureId = static_cast<int>(tex.id);
		_clearcoatNormalMapPath = QString::fromStdString(tex.path);
		_clearcoatNormalTexTransform.texCoord = tex.texCoordIndex;
		_clearcoatNormalTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_clearcoatNormalTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_clearcoatNormalTexTransform.texRotation = tex.rotation;
	}

	// Iridescence Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Iridescence)];
		_iridescenceTextureId = static_cast<unsigned int>(tex.id);
		_iridescenceMap = QString::fromStdString(tex.path);
		_iridescenceTexTransform.texCoord = tex.texCoordIndex;
		_iridescenceTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_iridescenceTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_iridescenceTexTransform.texRotation = tex.rotation;
	}

	// Iridescence Thickness Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::IridescenceThickness)];
		_iridescenceThicknessTextureId = static_cast<unsigned int>(tex.id);
		_iridescenceThicknessMap = QString::fromStdString(tex.path);
		_iridescenceThicknessTexTransform.texCoord = tex.texCoordIndex;
		_iridescenceThicknessTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_iridescenceThicknessTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_iridescenceThicknessTexTransform.texRotation = tex.rotation;
	}

	// Specular Factor Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::SpecularFactor)];
		_specularFactorTextureId = static_cast<unsigned int>(tex.id);
		_specularFactorMap = QString::fromStdString(tex.path);
		_specularFactorTexTransform.texCoord = tex.texCoordIndex;
		_specularFactorTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_specularFactorTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_specularFactorTexTransform.texRotation = tex.rotation;
	}

	// Specular Color Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::SpecularColor)];
		_specularColorTextureId = static_cast<unsigned int>(tex.id);
		_specularColorMap = QString::fromStdString(tex.path);
		_specularColorTexTransform.texCoord = tex.texCoordIndex;
		_specularColorTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_specularColorTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_specularColorTexTransform.texRotation = tex.rotation;
	}

	// Anisotropy Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Anisotropy)];
		_anisotropyTextureId = static_cast<unsigned int>(tex.id);
		_anisotropyMap = QString::fromStdString(tex.path);
		_anisotropyTexTransform.texCoord = tex.texCoordIndex;
		_anisotropyTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_anisotropyTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_anisotropyTexTransform.texRotation = tex.rotation;
	}

	// Diffuse Transmission Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::DiffuseTransmission)];
		_diffuseTransmissionTextureId = static_cast<unsigned int>(tex.id);
		_diffuseTransmissionMap = QString::fromStdString(tex.path);
		_diffuseTransmissionTexTransform.texCoord = tex.texCoordIndex;
		_diffuseTransmissionTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_diffuseTransmissionTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_diffuseTransmissionTexTransform.texRotation = tex.rotation;
	}

	// Diffuse Transmission Color Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::DiffuseTransmissionColor)];
		_diffuseTransmissionColorTextureId = static_cast<unsigned int>(tex.id);
		_diffuseTransmissionColorMap = QString::fromStdString(tex.path);
		_diffuseTransmissionColorTexTransform.texCoord = tex.texCoordIndex;
		_diffuseTransmissionColorTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_diffuseTransmissionColorTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_diffuseTransmissionColorTexTransform.texRotation = tex.rotation;
	}

	// Thickness Texture
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Thickness)];
		_thicknessTextureId = static_cast<unsigned int>(tex.id);
		_thicknessMap = QString::fromStdString(tex.path);
		_thicknessTexTransform.texCoord = tex.texCoordIndex;
		_thicknessTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_thicknessTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_thicknessTexTransform.texRotation = tex.rotation;
	}

	// Diffuse Texture (PBR Specular Glossiness)
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::Diffuse)];
		_diffuseTextureId = static_cast<unsigned int>(tex.id);
		_diffuseMap = QString::fromStdString(tex.path);
		_diffuseTexTransform.texCoord = tex.texCoordIndex;
		_diffuseTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_diffuseTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_diffuseTexTransform.texRotation = tex.rotation;
	}

	// Specular Glossiness Texture (PBR Specular Glossiness)
	{
		const auto& tex = _textures[static_cast<size_t>(TextureType::SpecularGlossiness)];
		_specularGlossinessTextureId = static_cast<unsigned int>(tex.id);
		_specularGlossinessMap = QString::fromStdString(tex.path);
		_specularGlossinessTexTransform.texCoord = tex.texCoordIndex;
		_specularGlossinessTexTransform.texScale = QVector2D(tex.scale.x, tex.scale.y);
		_specularGlossinessTexTransform.texOffset = QVector2D(tex.offset.x, tex.offset.y);
		_specularGlossinessTexTransform.texRotation = tex.rotation;
	}
}

// Material validation
bool Material::isValid() const
{
	// Check if all material properties are within valid ranges

	// Check color components (should be 0.0 to 1.0)
	auto isColorValid = [](const QVector3D& color) {
		return color.x() >= 0.0f && color.x() <= 1.0f &&
			color.y() >= 0.0f && color.y() <= 1.0f &&
			color.z() >= 0.0f && color.z() <= 1.0f;
		};

	// Traditional Phong/Blinn-Phong checks
	if (!isColorValid(_ambient)) return false;
	if (!isColorValid(_diffuse)) return false;
	if (!isColorValid(_specular)) return false;
	if (!isColorValid(_emissive)) return false;

	// Shininess should be positive
	if (_shininess < 0.0f || _shininess > 128.0f) return false;

	// PBR material checks
	if (!isColorValid(_albedoColor)) return false;
	if (!isColorValid(_emissive)) return false;

	// PBR parameters should be in valid ranges
	if (_metalness < 0.0f || _metalness > 1.0f) return false;
	if (_roughness < 0.0f || _roughness > 1.0f) return false;
	if (_opacity < 0.0f || _opacity > 1.0f) return false;
	if (_transmission < 0.0f || _transmission > 1.0f) return false;
	if (_ior < 1.0f || _ior > 3.0f) return false; // Reasonable IOR range

	return true;
}
void Material::validateAndFix()
{
	// Clamp color components to valid range [0.0, 1.0]
	auto clampColor = [](float& component) {
		component = qBound(0.0f, component, 1.0f);
		};

	// Fix traditional material properties (assuming they are QVector3D members)
	_ambient.setX(qBound(0.0f, _ambient.x(), 1.0f));
	_ambient.setY(qBound(0.0f, _ambient.y(), 1.0f));
	_ambient.setZ(qBound(0.0f, _ambient.z(), 1.0f));

	_diffuse.setX(qBound(0.0f, _diffuse.x(), 1.0f));
	_diffuse.setY(qBound(0.0f, _diffuse.y(), 1.0f));
	_diffuse.setZ(qBound(0.0f, _diffuse.z(), 1.0f));

	_specular.setX(qBound(0.0f, _specular.x(), 1.0f));
	_specular.setY(qBound(0.0f, _specular.y(), 1.0f));
	_specular.setZ(qBound(0.0f, _specular.z(), 1.0f));

	_emissive.setX(qBound(0.0f, _emissive.x(), 1.0f));
	_emissive.setY(qBound(0.0f, _emissive.y(), 1.0f));
	_emissive.setZ(qBound(0.0f, _emissive.z(), 1.0f));

	// Fix shininess (0 to 128 for OpenGL)
	_shininess = qBound(0.0f, _shininess, 128.0f);

	// Fix PBR properties
	_albedoColor.setZ(qBound(0.0f, _albedoColor.z(), 1.0f));
	_albedoColor.setX(qBound(0.0f, _albedoColor.x(), 1.0f));
	_albedoColor.setY(qBound(0.0f, _albedoColor.y(), 1.0f));

	_emissive.setX(qBound(0.0f, _emissive.x(), 1.0f));
	_emissive.setY(qBound(0.0f, _emissive.y(), 1.0f));
	_emissive.setZ(qBound(0.0f, _emissive.z(), 1.0f));

	// Clamp PBR parameters
	_metalness = qBound(0.0f, _metalness, 1.0f);
	_roughness = qBound(0.04f, _roughness, 1.0f); // Minimum 0.04 to prevent artifacts
	_opacity = qBound(0.0f, _opacity, 1.0f);
	_transmission = qBound(0.0f, _transmission, 1.0f);
	_ior = qBound(1.0f, _ior, 3.0f);
}

// Conversion utilities
void Material::convertToBlinnPhong()
{
	// Convert PBR material to Blinn-Phong approximation

	// Use albedo as diffuse color
	_diffuse = _albedoColor;

	// Set ambient to a fraction of diffuse (typically 0.1-0.3)
	float ambientFactor = 0.2f;
	_ambient = _diffuse * ambientFactor;

	// Convert metalness and roughness to specular properties
	if (_metalness > 0.5f)
	{
		// Metallic: use albedo as specular color, reduce diffuse
		_specular = _albedoColor * 0.9f;

		// Metals have very little diffuse reflection
		_diffuse *= (1.0f - _metalness) * 0.1f;
	}
	else
	{
		// Dielectric: white/gray specular, keep diffuse
		float specularIntensity = 0.04f + (1.0f - _roughness) * 0.96f;
		_specular = QVector3D(specularIntensity, specularIntensity, specularIntensity);
	}

	// Convert roughness to shininess (inverse relationship)
	// Roughness 0.0 = shininess 128, roughness 1.0 = shininess 1
	_shininess = (1.0f - _roughness) * 127.0f + 1.0f;

	// Set shading model to traditional
	_shadingModel = ShadingModel::BlinnPhong;
}

void Material::convertToPBR()
{
	// Convert Blinn-Phong material to PBR approximation

	// Use diffuse as base albedo
	_albedoColor = _diffuse;

	// Determine if material is metallic based on specular color
	float specularLuminance = 0.299f * _specular.x() + 0.587f * _specular.y() + 0.114f * _specular.z();
	float diffuseLuminance = 0.299f * _diffuse.x() + 0.587f * _diffuse.y() + 0.114f * _diffuse.z();

	if (specularLuminance > 0.9f && diffuseLuminance < 0.1f)
	{
		// Likely metallic - high specular, low diffuse
		_metalness = 1.0f;
		_albedoColor = _specular;
	}
	else if (specularLuminance > 0.5f)
	{
		// Partially metallic
		_metalness = qBound(0.0f, (specularLuminance - 0.04f) / 0.96f, 1.0f);
		// Blend between diffuse and specular for albedo
		_albedoColor = _diffuse * (1.0f - _metalness) + _specular * _metalness;
	}
	else
	{
		// Dielectric material
		_metalness = 0.0f;
		_albedoColor = _diffuse;
	}

	// Convert shininess to roughness (inverse relationship)
	// Shininess 128 = roughness 0.04, shininess 1 = roughness 1.0
	_roughness = 1.0f - ((_shininess - 1.0f) / 127.0f);
	_roughness = qBound(0.04f, _roughness, 1.0f);


	// Set reasonable defaults for other PBR properties
	_transmission = 0.0f;
	_ior = 1.5f; // Common default IOR

	// Set shading model to PBR
	_shadingModel = ShadingModel::PBR;
}




// Helper to print QVector2D / QVector3D concisely
static inline void printVec2(std::ostream& os, const QVector2D& v)
{
	os << "(" << v.x() << ", " << v.y() << ")";
}
static inline void printVec3(std::ostream& os, const QVector3D& v)
{
	os << "(" << v.x() << ", " << v.y() << ", " << v.z() << ")";
}

std::ostream& operator<<(std::ostream& os, const Material& m)
{
	// Formatting
	os << std::boolalpha;
	os << std::fixed << std::setprecision(4);

	os << "Material {\n";

	// --- Legacy/primary colors and params
	os << "  _ambient: "; printVec3(os, m._ambient); os << "\n";
	os << "  _diffuse: "; printVec3(os, m._diffuse); os << "\n";
	os << "  _specular: "; printVec3(os, m._specular); os << "\n";
	os << "  _emissive: "; printVec3(os, m._emissive); os << "  (emissiveStrength=" << m._emissiveStrength << ")\n";
	os << "  _shininess: " << m._shininess << "\n";
	os << "  _metallic (legacy flag): " << m._metallic << "\n";

	// --- PBR properties
	os << "  _albedoColor: "; printVec3(os, m._albedoColor); os << "\n";
	os << "  _metalness: " << m._metalness << "\n";
	os << "  _roughness: " << m._roughness << "\n";
	os << "  _opacity: " << m._opacity << "\n";
	os << "  _normalScale: " << m._normalScale << "  _heightScale: " << m._heightScale
		<< "  _clearcoatNormalScale: " << m._clearcoatNormalScale << "\n";

	// --- Advanced PBR / extras
	os << "  _ior: " << m._ior << "\n";
	os << "  _clearcoat: " << m._clearcoat << "  _clearcoatRoughness: " << m._clearcoatRoughness << "\n";
	os << "  _sheenColor: "; printVec3(os, m._sheenColor); os << "  _sheenRoughness: " << m._sheenRoughness << "\n";
	os << "  _transmission: " << m._transmission << "\n";
	os << "  _thicknessFactor: " << m._thicknessFactor << "\n";
	os << "  _attenuationDistance: " << m._attenuationDistance << "  _attenuationColor: "; printVec3(os, m._attenuationColor); os << "\n";
	os << "  _dispersion: " << m._dispersion << "\n";
	// KHR_materials_iridescence
	os << "  _iridescenceFactor: " << m._iridescenceFactor << "  _iridescenceIor: " << m._iridescenceIor << "\n";
	os << "  _iridescenceThicknessMin: " << m._iridescenceThicknessMin << "  _iridescenceThicknessMax: " << m._iridescenceThicknessMax << "\n";



	// --- Rendering flags
	os << "  _shadingModel: " << static_cast<int>(m._shadingModel) << "\n";
	os << "  _blendMode: " << static_cast<int>(m._blendMode) << "\n";
	os << "  _twoSided: " << m._twoSided << "\n";
	os << "  _wireframe: " << m._wireframe << "\n";
	os << "  _alphaThreshold: " << m._alphaThreshold << "\n";
	os << "  _hasTextureAlpha: " << m._hasTextureAlpha << "\n";

	// --- Texture IDs (ints)
	os << "  Texture IDs (logical slots):\n";
	os << "    _albedoTextureId: " << m._albedoTextureId << "\n";
	os << "    _metallicTextureId: " << m._metallicTextureId << "\n";
	os << "    _roughnessTextureId: " << m._roughnessTextureId << "\n";
	os << "    _normalTextureId: " << m._normalTextureId << "\n";
	os << "    _occlusionTextureId: " << m._occlusionTextureId << "\n";
	os << "    _emissiveTextureId: " << m._emissiveTextureId << "\n";
	os << "    _opacityTextureId: " << m._opacityTextureId << "\n";
	os << "    _heightTextureId: " << m._heightTextureId << "\n";
	os << "    _sheenColorTextureId: " << m._sheenColorTextureId << "\n";
	os << "    _sheenRoughnessTextureId: " << m._sheenRoughnessTextureId << "\n";
	os << "    _clearcoatColorTextureId: " << m._clearcoatColorTextureId << "\n";
	os << "    _clearcoatRoughnessTextureId: " << m._clearcoatRoughnessTextureId << "\n";
	os << "    _clearcoatNormalTextureId: " << m._clearcoatNormalTextureId << "\n";
	os << "    _iorTextureId: " << m._iorTextureId << "\n";
	os << "    _transmissionTextureId: " << m._transmissionTextureId << "\n";
	os << "    _invertOpacityTexture: " << m._invertOpacityTexture << "\n";
	os << "    _iridescenceTextureId: " << m._iridescenceTextureId << "\n";
	os << "    _anisotropyTextureId: " << m._anisotropyTextureId << "\n";
	os << "    _iridescenceThicknessTextureId: " << m._iridescenceThicknessTextureId << "\n";
	
	// --- Texture coordinate sets / transforms
	auto printTT = [&os](const Material::TextureTransform& t) {
		os << "{texCoord=" << t.texCoord << ", texScale=";
		printVec2(os, t.texScale);
		os << ", texOffset="; printVec2(os, t.texOffset);
		os << ", texRotation=" << t.texRotation << "}";
		};

	os << "  _albedoTexTransform: "; printTT(m._albedoTexTransform); os << "\n";
	os << "  _metallicTexTransform: "; printTT(m._metallicTexTransform); os << "\n";
	os << "  _normalTexTransform: "; printTT(m._normalTexTransform); os << "\n";
	os << "  _metallicRoughnessTexTransform: "; printTT(m._metallicRoughnessTexTransform); os << "\n";
	os << "  _roughnessTexTransform: "; printTT(m._roughnessTexTransform); os << "\n";
	os << "  _occlusionTexTransform: "; printTT(m._occlusionTexTransform); os << "\n";
	os << "  _emissiveTexTransform: "; printTT(m._emissiveTexTransform); os << "\n";
	os << "  _opacityTexTransform: "; printTT(m._opacityTexTransform); os << "\n";
	os << "  _heightTexTransform: "; printTT(m._heightTexTransform); os << "\n";
	os << "  _clearcoatColorTexTransform: "; printTT(m._clearcoatColorTexTransform); os << "\n";
	os << "  _clearcoatRoughnessTexTransform: "; printTT(m._clearcoatRoughnessTexTransform); os << "\n";
	os << "  _clearcoatNormalTexTransform: "; printTT(m._clearcoatNormalTexTransform); os << "\n";
	os << "  _sheenColorTexTransform: "; printTT(m._sheenColorTexTransform); os << "\n";
	os << "  _sheenRoughnessTexTransform: "; printTT(m._sheenRoughnessTexTransform); os << "\n";
	os << "  _iorTexTransform: "; printTT(m._iorTexTransform); os << "\n";
	os << "  _transmissionTexTransform: "; printTT(m._transmissionTexTransform); os << "\n";
	os << "  _anisotropyTexTransform: "; printTT(m._anisotropyTexTransform); os << "\n";
	os << "  _iridescenceTexTransform: "; printTT(m._iridescenceTexTransform); os << "\n";

	// --- Map paths (QString -> std::string)
	auto q = [](const QString& s)->std::string { return s.toStdString(); };
	os << "  Map paths:\n";
	os << "    _albedoMapPath: " << q(m._albedoMapPath) << "\n";
	os << "    _normalMapPath: " << q(m._normalMapPath) << "\n";
	os << "    _emissiveMapPath: " << q(m._emissiveMapPath) << "\n";
	os << "    _metallicMapPath: " << q(m._metallicMapPath) << "\n";
	os << "    _roughnessMapPath: " << q(m._roughnessMapPath) << "\n";
	os << "    _aoMapPath: " << q(m._aoMapPath) << "\n";
	os << "    _opacityMapPath: " << q(m._opacityMapPath) << "\n";
	os << "    _heightMapPath: " << q(m._heightMapPath) << "\n";
	os << "    _transmissionMapPath: " << q(m._transmissionMapPath) << "\n";
	os << "    _iorMapPath: " << q(m._iorMapPath) << "\n";
	os << "    _sheenColorMapPath: " << q(m._sheenColorMapPath) << "\n";
	os << "    _sheenRoughnessMapPath: " << q(m._sheenRoughnessMapPath) << "\n";
	os << "    _clearcoatColorMapPath: " << q(m._clearcoatColorMapPath) << "\n";
	os << "    _clearcoatRoughnessMapPath: " << q(m._clearcoatRoughnessMapPath) << "\n";
	os << "    _clearcoatNormalMapPath: " << q(m._clearcoatNormalMapPath) << "\n";
	os << "    _anisotropyMapPath: " << q(m._anisotropyMap) << "\n";
	os << "    _iridescenceMapPath: " << q(m._iridescenceMap) << "\n";
	os << "    _iridescenceThicknessMapPath: " << q(m._iridescenceThicknessMap) << "\n";
	os << "    _diffuseMapPath: " << q(m._diffuseMap) << "\n";
	os << "    _specularGlossinessMapPath: " << q(m._specularGlossinessMap) << "\n";

	// --- KHR extension fields (specular, anisotropy, iridescence, volume, etc.)
	os << "  KHR_specular: _specularFactor: " << m._specularFactor
		<< " _specularColorFactor: "; printVec3(os, m._specularColorFactor); os << "\n";
	os << "    _specularFactorMap: " << q(m._specularFactorMap)
		<< " _specularFactorTextureId: " << m._specularFactorTextureId << "\n";
	os << "    _specularColorMap: " << q(m._specularColorMap)
		<< " _specularColorTextureId: " << m._specularColorTextureId << "\n";

	// KHR_pbr_specular_glossiness
	os << "  KHR_specular_glossiness: _diffuseFactor: "; printVec3(os, m._diffuseColor);
	os << " _glossinessFactor: " << m._glossinessFactor << "\n";
	os << "    _diffuseMap: " << q(m._diffuseMap)
		<< " _diffuseTextureId: " << m._diffuseTextureId << "\n";
	os << "    _specularGlossinessMap: " << q(m._specularGlossinessMap)
		<< " _specularGlossinessTextureId: " << m._specularGlossinessTextureId << "\n";
	os << "    _diffuseColor: "; printVec3(os, m._diffuseColor); os << "\n";
	os << "    _specularColor: "; printVec3(os, m._specularColor); os << "\n";
	os << "    _useSpecularGlossiness: " << m._useSpecularGlossiness << "\n";
	os << "    _hasSpecularGlossinessMap: " << m.hasSpecularGlossinessMap() << "\n";
	

	os << "  KHR_anisotropy: strength=" << m._anisotropyStrength
		<< " rotation=" << m._anisotropyRotation
		<< " map=" << q(m._anisotropyMap) << " texId=" << m._anisotropyTextureId << "\n";

	os << "  KHR_iridescence: factor=" << m._iridescenceFactor
		<< " ior=" << m._iridescenceIor
		<< " thicknessMin=" << m._iridescenceThicknessMin
		<< " thicknessMax=" << m._iridescenceThicknessMax
		<< " map=" << q(m._iridescenceMap)
		<< " texId=" << m._iridescenceTextureId << "\n";
	os << "    thicknessMap=" << q(m._iridescenceThicknessMap)
		<< " thicknessTexId=" << m._iridescenceThicknessTextureId << "\n";

	os << "  KHR_volume: thicknessFactor=" << m._thicknessFactor
		<< " attenuationDistance=";
	if (std::isfinite(m._attenuationDistance)) os << m._attenuationDistance;
	else os << "inf";
	os << " attenuationColor="; printVec3(os, m._attenuationColor);
	os << " thicknessMap=" << q(m._thicknessMap) << " texId=" << m._thicknessTextureId << "\n";

	os << "  KHR_emissive_strength: " << m._emissiveStrength << "\n";
	os << "  KHR_dispersion: " << m._dispersion << "\n";
	os << "  KHR_unlit: " << m._unlit << "\n";

	// KHR_materials_diffuse_transmission
	os << "  KHR_diffuse_transmission: "
		<< "diffuseTransmissionFactor=" << m._diffuseTransmissionFactor
		<< " diffuseTransmissionColorFactor="; printVec3(os, m._diffuseTransmissionColorFactor);
	os << " diffuseTransmissionMap=" << q(m._diffuseTransmissionMap) << "\n"
		<< " diffuseTransmissionTexId=" << m._diffuseTransmissionTextureId << "\n";
	os << "diffuseTransmissionColorMap=" << q(m._diffuseTransmissionColorMap) << "\n"
		<< " diffuseTransmissionColorTexId=" << m._diffuseTransmissionColorTextureId << "\n";

	// KHR_materials_scattering
	os << "  KHR_scattering: "		
		<< " scatteringColor="; printVec3(os, m._multiScatterColor);

	// --- UV tiling/offset
	os << "  UV tiling: (" << m._uvTilingU << ", " << m._uvTilingV << ")"
		<< " UV offset: (" << m._uvOffsetU << ", " << m._uvOffsetV << ")\n";

	// --- Channel packings (inspect fields)
	auto printPacking = [&os](const Material::ChannelPacking& p) {
		os << "{channel=" << p.channel << ", invert=" << p.invert
			<< ", scale=" << p.scale << ", bias=" << p.bias << "}";
		};
	os << "  _metallicPacking: "; printPacking(m._metallicPacking); os << "\n";
	os << "  _roughnessPacking: "; printPacking(m._roughnessPacking); os << "\n";
	os << "  _aoPacking: "; printPacking(m._aoPacking); os << "\n";
	os << "  _opacityPacking: "; printPacking(m._opacityPacking); os << "\n";

	os << "}\n";

	// Restore floatfield? (optional)
	// os.unsetf(std::ios_base::floatfield);

	return os;
}

