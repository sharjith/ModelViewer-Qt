#pragma once

#include <vector>
#include "Drawable.h"
#include "BoundingSphere.h"
#include "BoundingBox.h"

struct GLMaterialProps
{
	QVector4D ambientMaterial;
	QVector4D diffuseMaterial;
	QVector4D specularMaterial;
	QVector4D specularReflectivity;
	QVector4D emmissiveMaterial;
	float   shininess;
	float   opacity;
	bool bMetallic;
    float pbrMetallic;
    float pbrRoughness;
	bool bHasTexture;
};

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

	QVector4D ambientMaterial() const;
	void setAmbientMaterial(const QVector4D& ambientMaterial);

	QVector4D diffuseMaterial() const;
	void setDiffuseMaterial(const QVector4D& diffuseMaterial);

	QVector4D specularMaterial() const;
	void setSpecularMaterial(const QVector4D& specularMaterial);

	QVector4D emmissiveMaterial() const;
	void setEmmissiveMaterial(const QVector4D& emmissiveMaterial);

	QVector4D specularReflectivity() const;
	void setSpecularReflectivity(const QVector4D& specularReflectivity);

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

	void resetTransformations();

	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

    void setAlbedoMap(unsigned int albedoMap);
    void setNormalMap(unsigned int normalMap);
    void setMetallicMap(unsigned int metallicMap);
    void setRoughnessMap(unsigned int roughnessMap);
    void setAOMap(unsigned int aoMap);
    void setHeightMap(unsigned int heightMap);

    bool hasNormalMap() const;
    void enableNormalMap(bool hasNormalMap);

    bool hasAOMap() const;
    void enableAOMap(bool hasAOMap);

    bool hasHeightMap() const;
    void enableHeightMap(bool hasHeightMap);

    float getHeightScale() const;
    void setHeightScale(float heightScale);

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
	virtual void computeBounds(std::vector<float> points);
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

	QVector4D _ambientMaterial;
	QVector4D _diffuseMaterial;
	QVector4D _specularMaterial;
	QVector4D _emmissiveMaterial;
	QVector4D _specularReflectivity;
	float _opacity;
	float _shininess;
	bool _metallic;

	QVector3D _PBRAlbedoColor;
	float     _PBRMetallic;
	float     _PBRRoughness;

	QImage _texImage, _texBuffer;
	unsigned int _texture;
	bool _bHasTexture;
	bool _bHasDiffuseTexture;
	bool _bHasSpecularTexture;
	unsigned int _sMax;
	unsigned int _tMax;

    unsigned int _albedoMap;
    unsigned int _metallicMap;
    unsigned int _roughnessMap;
    unsigned int _normalMap;
    unsigned int _aoMap;
    unsigned int _heightMap;
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
