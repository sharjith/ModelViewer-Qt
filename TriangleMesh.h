#pragma once

#include <vector>
#include "Drawable.h"
#include "BoundingSphere.h"
#include "BoundingBox.h"
#include "GLMaterial.h"

class TriangleMesh : public Drawable
{
public:
	TriangleMesh(QOpenGLShaderProgram* prog, const QString name);

	virtual ~TriangleMesh();

	virtual void setProg(QOpenGLShaderProgram* prog);

	virtual void setName(const QString& name)
	{
		_name = name;
	}

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

	void setAlbedoMap(unsigned int albedoMap);
	void setNormalMap(unsigned int normalMap);
	void setMetallicMap(unsigned int metallicMap);
	void setRoughnessMap(unsigned int roughnessMap);
	void setAOMap(unsigned int aoMap);
	void setHeightMap(unsigned int heightMap);

	bool hasAlbedoMap() const;
	void enableAlbedoMap(bool hasAlbedoMap);

	bool hasMetallicMap() const;
	void enableMetallicMap(bool hasMetallicMap);

	bool hasRoughnessMap() const;
	void enableRoughnessMap(bool hasRoughnessMap);

	bool hasNormalMap() const;
	void enableNormalMap(bool hasNormalMap);

	bool hasAOMap() const;
	void enableAOMap(bool hasAOMap);

	bool hasHeightMap() const;
	void enableHeightMap(bool hasHeightMap);

	float getHeightScale() const;
	void setHeightScale(float heightScale);

	void clearAlbedoMap();
	void clearMetallicMap();
	void clearRoughnessMap();
	void clearNormalMap();
	void clearAOMap();
	void clearHeightMap();
	void clearPBRTextures();

	GLMaterial getMaterial() const;
	void setMaterial(const GLMaterial& material);

    void enableDiffuseTex(bool enable);
    void setDiffuseTex(unsigned int diffuseTex);
    void enableSpecularTex(bool enable);
    void setSpecularTex(unsigned int specularTex);
    void enableNormalTex(bool enable);
    void setNormalTex(unsigned int normalTex);
    void enableHeightTex(bool enable);
    void setHeightTex(unsigned int heightTex);

    void clearDiffuseTex();
    void clearSpecularTex();
    void clearNormalTex();
    void clearHeightTex();
	void clearADSTextures();

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

	QString _name;

	GLMaterial _material;

	QImage _texImage, _texBuffer;
    // ADS texture light maps
    unsigned int _texture;
    unsigned int _diffuseTex;
    unsigned int _specularTex;
    unsigned int _normalTex;
    unsigned int _heightTex;
    bool _hasTexture;
    bool _hasDiffuseTexture;
    bool _hasSpecularTexture;
    bool _hasNormalTexture;
    bool _hasHeightTexture;

    unsigned int _sMax;
	unsigned int _tMax;

    // PBR texture maps
	unsigned int _albedoMap;
	unsigned int _metallicMap;
	unsigned int _roughnessMap;
	unsigned int _normalMap;
	unsigned int _aoMap;
	unsigned int _heightMap;
	bool _hasAlbedoMap;
	bool _hasMetallicMap;
	bool _hasRoughnessMap;
	bool _hasNormalMap;
	bool _hasAOMap;
	bool _hasHeightMap;
	float _heightScale;

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
