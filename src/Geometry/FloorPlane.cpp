#include "FloorPlane.h"

FloorPlane::FloorPlane(QOpenGLShaderProgram* prog,
                       QVector3D center, float xsize, float ysize, int xdivs, int ydivs,
                       float zlevel, float smax, float tmax, Plane::Orientation orientation)
    : PlaneRenderable(prog, center, xsize, ysize, xdivs, ydivs, zlevel, smax, tmax, orientation)
{
}

RenderableMesh* FloorPlane::clone()
{
    return new FloorPlane(_prog,
                          _plane.center(), _plane.xSize(), _plane.ySize(),
                          _plane.xDivs(), _plane.yDivs(),
                          _plane.zLevel(), _plane.sMax(), _plane.tMax(),
                          _plane.orientation());
}

void FloorPlane::render()
{
	if (!_vertexArrayObject.isCreated())
		return;

	const QMatrix4x4 modelMatrix = currentGlobalModelMatrix() * combinedRenderTransform();
	const QMatrix4x4 viewMatrix = currentViewMatrix();
	const QMatrix4x4 modelViewMatrix = viewMatrix * modelMatrix;

	if (_prog->uniformLocation("modelMatrix") >= 0)
		_prog->setUniformValue("modelMatrix", modelMatrix);
	if (_prog->uniformLocation("modelViewMatrix") >= 0)
		_prog->setUniformValue("modelViewMatrix", modelViewMatrix);
	if (_prog->uniformLocation("normalMatrix") >= 0)
		_prog->setUniformValue("normalMatrix", modelViewMatrix.normalMatrix());
	if (_prog->uniformLocation("hasSkinning") >= 0)
		_prog->setUniformValue("hasSkinning", false);
	if (_prog->uniformLocation("jointCount") >= 0)
		_prog->setUniformValue("jointCount", 0);

	setupTextures();
	setupUniforms();

	if (_material.opacity() < 1.0f ||
		_material.hasOpacityMap() || _material.hasTransmissionMap() ||
		_material.transmission() > 0.0f ||
		_material.blendMode() == Material::BlendMode::Alpha ||
		_material.alphaThreshold() > 0.0f || _materialState.hasTextureAlpha())
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_POLYGON_SMOOTH);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	glFrontFace(hasNegativeScale() ? GL_CW : GL_CCW);

	_vertexArrayObject.bind();
	glDrawElements(GL_TRIANGLES, _nVerts, GL_UNSIGNED_INT, 0);
	_vertexArrayObject.release();

	glDisable(GL_BLEND);
}

void FloorPlane::setupTextures()
{
	bindTextureUnitCached(GL_TEXTURE0, _fallbackTexture);
	if (_textureBindingsDirty && !_fallbackTextureImage.isNull())
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _fallbackTextureImage.width(), _fallbackTextureImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _fallbackTextureImage.bits());
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	const GLuint diffuseTex = _material.hasAlbedoMap()
		? static_cast<GLuint>(_material.albedoTextureId())
		: (_material.hasDiffuseMap() ? static_cast<GLuint>(_material.diffuseTextureId()) : 0U);

	bindTextureUnitCached(GL_TEXTURE10, diffuseTex);

	_textureBindingsDirty = false;
}

void FloorPlane::setupUniforms()
{
	if (!_uniformsDirty)
		return;

	_prog->bind();

	GLint modeValue = 0;
	switch (_primitiveMode)
	{
	case GL_POINTS:         modeValue = 0; break;
	case GL_LINES:          modeValue = 1; break;
	case GL_LINE_LOOP:      modeValue = 2; break;
	case GL_LINE_STRIP:     modeValue = 3; break;
	case GL_TRIANGLES:      modeValue = 4; break;
	case GL_TRIANGLE_STRIP: modeValue = 5; break;
	case GL_TRIANGLE_FAN:   modeValue = 6; break;
	default:                modeValue = 4; break;
	}

	const bool hasDiffuseTexture = _material.hasAlbedoMap() || _material.hasDiffuseMap();
	const QVector3D adsDiffuse = _material.getUseSpecularGlossiness()
		? (hasDiffuseTexture ? QVector3D(1.0f, 1.0f, 1.0f) : _material.diffuseColor())
		: _material.diffuse();

	_prog->setUniformValue("primitiveMode", modeValue);
	_prog->setUniformValue("hasVertexColors", _hasVertexColors);
	_prog->setUniformValue("hasNegativeScale", hasNegativeScale());

	_prog->setUniformValue("material.ambient", _material.ambient());
	_prog->setUniformValue("material.diffuse", adsDiffuse);
	_prog->setUniformValue("material.specular", _material.specular());
	_prog->setUniformValue("material.emission", _material.emissive());
	_prog->setUniformValue("material.shininess", _material.shininess());
	_prog->setUniformValue("material.metallic", _material.metallic());
	_prog->setUniformValue("opacity", _material.opacity());

	_prog->setUniformValue("hasDiffuseTexture", hasDiffuseTexture);
	_prog->setUniformValue("hasSpecularTexture", false);
	_prog->setUniformValue("hasEmissiveTexture", false);
	_prog->setUniformValue("hasNormalTexture", false);
	_prog->setUniformValue("hasHeightTexture", false);
	_prog->setUniformValue("hasOpacityTexture", false);
	_prog->setUniformValue("opacityTextureInverted", false);

	_prog->setUniformValue("texture_diffuse", 10);
	_prog->setUniformValue("texture_specular", 11);
	_prog->setUniformValue("texture_emissive", 12);
	_prog->setUniformValue("texture_normal", 13);
	_prog->setUniformValue("texture_height", 14);
	_prog->setUniformValue("texture_opacity", 15);

	_prog->setUniformValue("alphaThreshold", _material.alphaThreshold());
	_prog->setUniformValue("blendMode", static_cast<int>(_material.blendMode()));
	_prog->setUniformValue("twoSided", _material.twoSided());

	_prog->setUniformValue("diffuseTextureTransform.texCoordIndex", _material.albedoTexCoord());
	_prog->setUniformValue("diffuseTextureTransform.offset", _material.albedoTexOffset());
	_prog->setUniformValue("diffuseTextureTransform.scale", _material.albedoTexScale());
	_prog->setUniformValue("diffuseTextureTransform.rotation", _material.albedoTexRotation());

	_prog->setUniformValue("selected", isSelected());

	_uniformsDirty = false;
}
