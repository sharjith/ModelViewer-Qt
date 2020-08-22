#pragma once

#include <vector>
#include "Drawable.h"
#include "BoundingSphere.h"

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

	virtual QOpenGLVertexArrayObject& getVAO();
	virtual QString getName() const
	{
		return _name;
	}

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

	bool hasTexture() const;
	void enableTexture(const bool& bHasTexture);

	void setTexureImage(const QImage& texImage);

	float getLowestZValue() const;

	QMatrix4x4 getTransformation() const;
	void setTransformation(const QMatrix4x4& transformation);

	std::vector<float> getPoints() const;
	std::vector<float> getNormals() const;

	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

protected: // methods
	virtual void initBuffers(
		std::vector<unsigned int>* indices,
		std::vector<float>* points,
		std::vector<float>* normals,
		std::vector<float>* texCoords = nullptr,
		std::vector<float>* tangents = nullptr
	);

	virtual void deleteBuffers();

	void computeBoundingSphere(std::vector<float> points);

	bool rayIntersectsTriangle(const QVector3D& rayOrigin,
		const QVector3D& rayVector,
		const QVector3D& vertex0,
		const QVector3D& vertex1,
		const QVector3D& vertex2,
		QVector3D& outIntersectionPoint);

protected:

	QOpenGLBuffer _indexBuffer;
	QOpenGLBuffer _positionBuffer;
	QOpenGLBuffer _normalBuffer;
	QOpenGLBuffer _texCoordBuffer;
	QOpenGLBuffer _tangentBuf;

	QOpenGLBuffer _coordBuf;

	unsigned int _nVerts;     // Number of vertices
	QOpenGLVertexArrayObject _vertexArrayObject;        // The Vertex Array Object

	// Vertex buffers
	std::vector<QOpenGLBuffer> _buffers;

	BoundingSphere _boundingSphere;

	QString _name;

	QVector4D _ambientMaterial;
	QVector4D _diffuseMaterial;
	QVector4D _specularMaterial;
	QVector4D _emmissiveMaterial;
	QVector4D _specularReflectivity;
	float _opacity;
	float _shininess;

	QImage _texImage, _texBuffer;
	unsigned int _texture;
	bool _bHasTexture;

	std::vector<unsigned int> _indices;
	std::vector<float> _points;
	std::vector<float> _normals;
	std::vector<float> _trsfpoints;
	std::vector<float> _trsfnormals;
	QMatrix4x4 _transformation;
};
