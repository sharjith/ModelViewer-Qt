#pragma once

#include <vector>
#include "Drawable.h"
#include "BoundingSphere.h"
#include "BoundingBox.h"
#include "GLMaterial.h"

class TriangleMesh : public Drawable
{
    Q_OBJECT
public:
	TriangleMesh(QOpenGLShaderProgram* prog, const QString name);

	virtual ~TriangleMesh();

	virtual void setProg(QOpenGLShaderProgram* prog);

	virtual TriangleMesh* clone() = 0;

	virtual void render();
	virtual void select()
	{
		_selected = true;
	}
	virtual void deselect()
	{
		_selected = false;
	}

	virtual BoundingSphere getBoundingSphere() const { return _boundingSphere; }
	virtual BoundingBox getBoundingBox() const { return _boundingBox; }

	virtual QOpenGLVertexArrayObject& getVAO();
	virtual QString getName() const
	{
		return _name;
	}

	virtual unsigned long long memorySize() const { return _memorySize; }

	QVector3D ambientMaterial() const;
	void setAmbientMaterial(const QVector3D& ambient);

	QVector3D diffuseMaterial() const;
	void setDiffuseMaterial(const QVector3D& diffuse);

	QVector3D specularMaterial() const;
	void setSpecularMaterial(const QVector3D& specular);

	QVector3D emmissiveMaterial() const;
	void setEmmissiveMaterial(const QVector3D& emissive);

	float opacity() const;
	void setOpacity(const float& opacity);

	float shininess() const;
	void setShininess(const float& shininess);

	bool isMetallic() const;
	void setMetallic(bool metallic);

	bool hasTexture() const;
	void enableTexture(const bool& bHasTexture);

	void setTexureImage(const QImage& texImage);

	void setPBRAlbedoColor(const float& r, const float& g, const float& b);
	void setPBRMetallic(const float& val);
	void setPBRRoughness(const float& val);

	float getHighestXValue() const;
	float getLowestXValue() const;
	float getHighestYValue() const;
	float getLowestYValue() const;
	float getHighestZValue() const;
	float getLowestZValue() const;
	QRect projectedRect(const QMatrix4x4& modelView, const QMatrix4x4& projection, const QRect& viewport, const QRect& window) const;

	QVector3D getTranslation() const;
	void setTranslation(const QVector3D& trans);

	QVector3D getRotation() const;
	void setRotation(const QVector3D& rota);

	QVector3D getScaling() const;
	void setScaling(const QVector3D& scale);

	QMatrix4x4 getTransformation() const;

	std::vector<unsigned int> getIndices() const;
	std::vector<float> getPoints() const;
	std::vector<float> getNormals() const;
	std::vector<float> getTexCoords() const;
	std::vector<float> getTrsfPoints() const;

	void resetTransformations();

	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

	void setAlbedoPBRMap(unsigned int albedoMap);
	void setNormalPBRMap(unsigned int normalMap);
	void setMetallicPBRMap(unsigned int metallicMap);
	void setRoughnessPBRMap(unsigned int roughnessMap);
	void setAOPBRMap(unsigned int aoMap);
	void setHeightPBRMap(unsigned int heightMap);
	void setOpacityPBRMap(unsigned int opacityMap);
	void invertOpacityPBRMap(bool invert);

	bool hasAlbedoPBRMap() const;
	void enableAlbedoPBRMap(bool hasAlbedoMap);

	bool hasMetallicPBRMap() const;
	void enableMetallicPBRMap(bool hasMetallicMap);

	bool hasRoughnessPBRMap() const;
	void enableRoughnessPBRMap(bool hasRoughnessMap);

	bool hasNormalPBRMap() const;
	void enableNormalPBRMap(bool hasNormalMap);

	bool hasAOPBRMap() const;
	void enableAOPBRMap(bool hasAOMap);

	bool hasHeightPBRMap() const;
	void enableHeightPBRMap(bool hasHeightMap);

	float getHeightPBRMapScale() const;
	void setHeightPBRMapScale(float heightScale);

	bool hasOpacityPBRMap() const;
	void enableOpacityPBRMap(bool hasHeightMap);

	void clearAlbedoPBRMap();
	void clearMetallicPBRMap();
	void clearRoughnessPBRMap();
	void clearNormalPBRMap();
	void clearAOPBRMap();
	void clearHeightPBRMap();
	void clearOpacityPBRMap();
	void clearAllPBRMaps();

	GLMaterial getMaterial() const;
	void setMaterial(const GLMaterial& material);

	void enableDiffuseADSMap(bool enable);
	void setDiffuseADSMap(unsigned int diffuseTex);
	void enableSpecularADSMap(bool enable);
	void setSpecularADSMap(unsigned int specularTex);
	void enableEmissiveADSMap(bool enable);
	void setEmissiveADSMap(unsigned int emissiveTex);
	void enableNormalADSMap(bool enable);
	void setNormalADSMap(unsigned int normalTex);
	void enableHeightADSMap(bool enable);
	void setHeightADSMap(unsigned int heightTex);
	void enableOpacityADSMap(bool enable);
	void invertOpacityADSMap(bool invert);
	void setOpacityADSMap(unsigned int opacityTex);

	void clearDiffuseADSMap();
	void clearSpecularADSMap();
	void clearEmissiveADSMap();
	void clearNormalADSMap();
	void clearHeightADSMap();
	void clearOpacityADSMap();
	void clearAllADSMaps();

protected: // methods
	virtual void initBuffers(
		std::vector<unsigned int>* indices,
		std::vector<float>* points,
		std::vector<float>* normals,
		std::vector<float>* texCoords = nullptr,
		std::vector<float>* tangents = nullptr,
		std::vector<float>* bitangents = nullptr
	);

	virtual void deleteBuffers();
	virtual void setupTransformation();
	virtual void computeBounds();
	virtual bool rayIntersectsTriangle(const QVector3D& rayOrigin,
		const QVector3D& rayVector,
		const QVector3D& vertex0,
		const QVector3D& vertex1,
		const QVector3D& vertex2,
		QVector3D& outIntersectionPoint);

	virtual void setupTextures();
	virtual void setupUniforms();

protected:

	QOpenGLBuffer _indexBuffer;
	QOpenGLBuffer _positionBuffer;
	QOpenGLBuffer _normalBuffer;
	QOpenGLBuffer _texCoordBuffer;
	QOpenGLBuffer _tangentBuf;
	QOpenGLBuffer _bitangentBuf;

	QOpenGLBuffer _coordBuf;

	unsigned int _nVerts;     // Number of vertices
	QOpenGLVertexArrayObject _vertexArrayObject;        // The Vertex Array Object

	// Vertex buffers
	std::vector<QOpenGLBuffer> _buffers;

	BoundingSphere _boundingSphere;
	BoundingBox    _boundingBox;

	GLMaterial _material;

	QImage _texImage, _texBuffer;
	// ADS texture light maps
	unsigned int _texture;
	unsigned int _diffuseADSMap;
	unsigned int _specularADSMap;
	unsigned int _emissiveADSMap;
	unsigned int _normalADSMap;
	unsigned int _heightADSMap;
	unsigned int _opacityADSMap;
	bool _hasTexture;
	bool _hasDiffuseADSMap;
	bool _hasSpecularADSMap;
	bool _hasEmissiveADSMap;
	bool _hasNormalADSMap;
	bool _hasHeightADSMap;
	bool _hasOpacityADSMap;
	bool _opacityADSMapInverted;

	unsigned int _sMax;
	unsigned int _tMax;

	// PBR texture maps
	unsigned int _albedoPBRMap;
	unsigned int _metallicPBRMap;
	unsigned int _roughnessPBRMap;
	unsigned int _normalPBRMap;
	unsigned int _aoPBRMap;
	unsigned int _heightPBRMap;
	unsigned int _opacityPBRMap;
	bool _hasAlbedoPBRMap;
	bool _hasMetallicPBRMap;
	bool _hasRoughnessPBRMap;
	bool _hasNormalPBRMap;
	bool _hasAOPBRMap;
	bool _hasHeightPBRMap;
	float _heightPBRMapScale;
	bool _hasOpacityPBRMap;
	bool _opacityPBRMapInverted;

	std::vector<unsigned int> _indices;
	std::vector<float> _points;
	std::vector<float> _normals;
	std::vector<float> _tangents;
	std::vector<float> _bitangents;
	std::vector<float> _texCoords;
	std::vector<float> _trsfpoints;
	std::vector<float> _trsfnormals;

	// Individual transformation components
	float _transX;
	float _transY;
	float _transZ;

	float _rotateX;
	float _rotateY;
	float _rotateZ;

	float _scaleX;
	float _scaleY;
	float _scaleZ;

	QMatrix4x4 _transformation;

	unsigned long long _memorySize;
};
