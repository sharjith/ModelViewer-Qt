#include "GLMaterial.h"
#include <cmath>

GLMaterial::GLMaterial()
{
	*this = DEFAULT_MAT();
}

GLMaterial::GLMaterial(QVector3D ambient, QVector3D diffuse, QVector3D specular, QVector3D emissive, float shininess, bool metallic, float opacity)
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
}

GLMaterial::GLMaterial(QVector3D albedo, float metalness, float roughness, float opacity)
{
	_albedoColor.setX(std::clamp(albedo.x(), 0.0f, 1.0f));
	_albedoColor.setY(std::clamp(albedo.y(), 0.0f, 1.0f));
	_albedoColor.setZ(std::clamp(albedo.z(), 0.0f, 1.0f));

	_metalness = metalness;
	_roughness = roughness;
	_opacity = opacity;
}

QVector3D GLMaterial::ambient() const
{
	return _ambient;
}

void GLMaterial::setAmbient(const QVector3D& ambient)
{
	_ambient.setX(std::clamp(ambient.x(), 0.0f, 1.0f));
	_ambient.setY(std::clamp(ambient.y(), 0.0f, 1.0f));
	_ambient.setZ(std::clamp(ambient.z(), 0.0f, 1.0f));
}

QVector3D GLMaterial::diffuse() const
{
	return _diffuse;
}

void GLMaterial::setDiffuse(const QVector3D& diffuse)
{
	_diffuse.setX(std::clamp(diffuse.x(), 0.0f, 1.0f));
	_diffuse.setY(std::clamp(diffuse.y(), 0.0f, 1.0f));
	_diffuse.setZ(std::clamp(diffuse.z(), 0.0f, 1.0f));
}

QVector3D GLMaterial::specular() const
{
	return _specular;
}

void GLMaterial::setSpecular(const QVector3D& specular)
{
	_specular.setX(std::clamp(specular.x(), 0.0f, 1.0f));
	_specular.setY(std::clamp(specular.y(), 0.0f, 1.0f));
	_specular.setZ(std::clamp(specular.z(), 0.0f, 1.0f));
}

QVector3D GLMaterial::emissive() const
{
	return _emissive;
}

void GLMaterial::setEmissive(const QVector3D& emissive)
{
	_emissive.setX(std::clamp(emissive.x(), 0.0f, 1.0f));
	_emissive.setY(std::clamp(emissive.y(), 0.0f, 1.0f));
	_emissive.setZ(std::clamp(emissive.z(), 0.0f, 1.0f));
}

float GLMaterial::shininess() const
{
	return _shininess;
}

void GLMaterial::setShininess(float shininess)
{
	_shininess = shininess;
}

bool GLMaterial::metallic() const
{
	return _metallic;
}

void GLMaterial::setMetallic(bool metallic)
{
	_metallic = metallic;
}

QVector3D GLMaterial::albedoColor() const
{
	return _albedoColor;
}

void GLMaterial::setAlbedoColor(const QVector3D& albedoColor)
{
	_albedoColor.setX(std::clamp(albedoColor.x(), 0.0f, 1.0f));
	_albedoColor.setY(std::clamp(albedoColor.y(), 0.0f, 1.0f));
	_albedoColor.setZ(std::clamp(albedoColor.z(), 0.0f, 1.0f));
}

float GLMaterial::metalness() const
{
	return _metalness;
}

void GLMaterial::setMetalness(float metalness)
{
	_metalness = metalness;
}

float GLMaterial::roughness() const
{
	return _roughness;
}

void GLMaterial::setRoughness(float roughness)
{
	_roughness = roughness;
}

float GLMaterial::opacity() const
{
	return _opacity;
}

void GLMaterial::setOpacity(float opacity)
{
	_opacity = opacity;
}

GLMaterial GLMaterial::getPredefinedMaterial(GLMaterial::PredefinedMaterials type)
{
	switch (type)
	{
	case PredefinedMaterials::BRASS:
		return BRASS();
		break;
	case PredefinedMaterials::BRONZE:
		return BRONZE();
		break;
	case PredefinedMaterials::COPPER:
		return COPPER();
		break;
	case PredefinedMaterials::GOLD:
		return GOLD();
		break;
	case PredefinedMaterials::SILVER:
		return SILVER();
		break;
	case PredefinedMaterials::CHROME:
		return CHROME();
		break;
	case PredefinedMaterials::RUBY:
		return RUBY();
		break;
	case PredefinedMaterials::EMERALD:
		return EMERALD();
		break;
	case PredefinedMaterials::TURQUOISE:
		return TURQUOISE();
		break;
	case PredefinedMaterials::PEARL:
		return PEARL();
		break;
	case PredefinedMaterials::JADE:
		return JADE();
		break;
	case PredefinedMaterials::OBSIDIAN:
		return OBSIDIAN();
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

GLMaterial GLMaterial::BRASS()
{
	GLMaterial mat({ 0.329412f, 0.223529f, 0.027451f },
		{ 0.780392f, 0.568627f, 0.113725f },
		{ 0.992157f, 0.941176f, 0.807843f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.21794872),
		true,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.65f);

	return mat;
}

GLMaterial GLMaterial::BRONZE()
{
	GLMaterial mat({ 0.2125f, 0.1275f, 0.054f },
		{ 0.714f, 0.4284f, 0.18144f },
		{ 0.393548f, 0.271906f, 0.166721f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.2),
		true,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.65f);

	return mat;
}

GLMaterial GLMaterial::COPPER()
{
	GLMaterial mat({ 0.19125f, 0.0735f, 0.0225f },
		{ 0.7038f, 0.27048f, 0.0828f },
		{ 0.256777f, 0.137622f, 0.086014f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.1),
		true,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.65f);

	return mat;
}

GLMaterial GLMaterial::GOLD()
{
	GLMaterial mat({ 0.24725f, 0.1995f, 0.0745f },
		{ 0.75164f,      0.60648f,      0.22648f },
		{ 0.628281f,      0.555802f,      0.366065f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.4),
		true,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.65f);

	return mat;
}

GLMaterial GLMaterial::SILVER()
{
	GLMaterial mat({ 0.19225f, 0.19225f, 0.19225f },
		{ 0.50754f, 0.50654f, 0.50754f },
		{ 0.508273f, 0.508273f, 0.508273f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.4),
		true,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.65f);

	return mat;
}

GLMaterial GLMaterial::CHROME()
{
	GLMaterial mat({ 0.25f, 0.25f, 0.25f },
		{ 0.4f, 0.4f, 0.4f },
		{ 0.774597f, 0.774597f, 0.774597f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		true,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.65f);

	return mat;
}

GLMaterial GLMaterial::RUBY()
{
	GLMaterial mat({ 0.17450f, 0.01175f, 0.01175f },
		{ 0.61424f, 0.04136f, 0.04136f },
		{ 0.727811f, 0.626959f, 0.626959f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);

	return mat;
}

GLMaterial GLMaterial::EMERALD()
{
	GLMaterial mat({ 0.0215f, 0.1745f, 0.0215f },
		{ 0.07568f, 0.61424f, 0.07568f },
		{ 0.633000f, 0.727811f, 0.633000f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.6),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);

	return mat;
}

GLMaterial GLMaterial::TURQUOISE()
{
	GLMaterial mat({ 0.1f, 0.18725f, 0.1745f },
		{ 0.396f, 0.74151f, 0.69102f },
		{ 0.297254f, 0.30829f, 0.306678f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.1),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);

	return mat;
}

GLMaterial GLMaterial::PEARL()
{
	GLMaterial mat({ 0.25000f, 0.20725f, 0.20725f },
		{ 1.000f, 0.829f, 0.829f },
		{ 0.296648f, 0.296648f, 0.299948f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.088),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);

	return mat;
}

GLMaterial GLMaterial::JADE()
{
	GLMaterial mat({ 0.135f, 0.2225f, 0.1575f },
		{ 0.54f, 0.89f, 0.63f },
		{ 0.316228f, 0.316228f, 0.316228f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.1),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);

	return mat;
}

GLMaterial GLMaterial::OBSIDIAN()
{
	GLMaterial mat({ 0.05375f, 0.05f, 0.06625f },
		{ 0.18275f, 0.17f, 0.22525f },
		{ 0.332741f, 0.328634f, 0.346435f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.3),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.25f);

	return mat;
}

GLMaterial GLMaterial::RED_PLASTIC()
{
	GLMaterial mat({ 0.0f, 0.0f, 0.0f },
		{ 0.5f, 0.0f, 0.0f },
		{ 0.7f, 0.6f, 0.6f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);

	return mat;
}

GLMaterial GLMaterial::GREEN_PLASTIC()
{
	GLMaterial mat({ 0.0f, 0.0f, 0.0f },
		{ 0.1f, 0.35f, 0.1f },
		{ 0.45f, 0.55f, 0.45f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);

	return mat;
}

GLMaterial GLMaterial::CYAN_PLASTIC()
{
	GLMaterial mat({ 0.0f, 0.1f, 0.06f },
		{ 0.0f, 0.50980392f, 0.50980392f },
		{ 0.50196078f, 0.50196078f, 0.50196078f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);

	return mat;
}

GLMaterial GLMaterial::YELLOW_PLASTIC()
{
	GLMaterial mat({ 0.0f, 0.0f, 0.0f },
		{ 0.5f, 0.5f, 0.0f },
		{ 0.6f, 0.6f, 0.5f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);

	return mat;
}

GLMaterial GLMaterial::WHITE_PLASTIC()
{
	GLMaterial mat({ 0.0f, 0.0f, 0.0f },
		{ 0.55f, 0.55f, 0.55f },
		{ 0.7f, 0.7f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);

	return mat;
}

GLMaterial GLMaterial::BLACK_PLASTIC()
{
	GLMaterial mat({ 0.0f, 0.0f, 0.0f },
		{ 0.01f, 0.01f, 0.01f },
		{ 0.5f, 0.5f, 0.5f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.25),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.1f);

	return mat;
}

GLMaterial GLMaterial::RED_RUBBER()
{
	GLMaterial mat({ 0.05f, 0.0f, 0.0f },
		{ 0.7f, 0.4f, 0.4f },
		{ 0.7f, 0.04f, 0.04f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);

	return mat;
}

GLMaterial GLMaterial::GREEN_RUBBER()
{
	GLMaterial mat({ 0.0f, 0.05f, 0.0f },
		{ 0.4f, 0.5f, 0.4f },
		{ 0.04f, 0.7f, 0.04f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);

	return mat;
}

GLMaterial GLMaterial::CYAN_RUBBER()
{
	GLMaterial mat({ 0.0f, 0.05f, 0.05f },
		{ 0.4f, 0.5f, 0.5f },
		{ 0.04f, 0.7f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);

	return mat;
}

GLMaterial GLMaterial::YELLOW_RUBBER()
{
	GLMaterial mat({ 0.05f, 0.05f, 0.0f },
		{ 0.5f, 0.5f, 0.4f },
		{ 0.7f, 0.7f, 0.04f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);

	return mat;
}

GLMaterial GLMaterial::WHITE_RUBBER()
{
	GLMaterial mat({ 0.05f, 0.05f, 0.05f },
		{ 0.5f, 0.5f, 0.5f },
		{ 0.7f, 0.7f, 0.7f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);

	return mat;
}

GLMaterial GLMaterial::BLACK_RUBBER()
{
	GLMaterial mat({ 0.02f, 0.02f, 0.02f },
		{ 0.01f, 0.01f, 0.01f },
		{ 0.4f, 0.4f, 0.4f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.078125f),
		false,
		1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(0.0f);
	mat.setRoughness(0.7f);

	return mat;
}

GLMaterial GLMaterial::DEFAULT_MAT()
{
	/*GLMaterial mat({ 126 / 256.0f, 124 / 256.0f, 116 / 256.0f },
		{ 126 / 256.0f, 124 / 256.0f, 116 / 256.0f },
		{ 140 / 256.0f, 140 / 256.0f, 130 / 256.0f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.05f),
		false,
		1.0f);*/
    /*GLMaterial mat({ 25 / 255.0f, 25 / 255.0f, 25 / 255.0f },
		{ 230 / 255.0f, 230 / 255.0f, 230 / 255.0f },
		{ 128 / 255.0f, 128 / 255.0f, 128 / 255.0f },
		{ 0.0, 0.0, 0.0 },
		fabs(128.0 * 0.05f),
		false,
        1.0f);*/
    GLMaterial mat({ 90 / 255.0f, 98 / 255.0f, 115 / 255.0f },
        { 175 / 255.0f, 192 / 255.0f, 224 / 255.0f },
        { 26 / 255.0f, 26 / 255.0f, 26 / 255.0f },
        { 0.0, 0.0, 0.0 },
        fabs(128.0 * 0.05f),
        false,
        1.0f);
	mat.setAlbedoColor(mat.ambient() + mat.diffuse());
	mat.setMetalness(1.0f);
	mat.setRoughness(0.7f);

	return mat;
}

void GLMaterial::setAlbedoFromADS()
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
