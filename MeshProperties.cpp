#include "MeshProperties.h"
#include "TriangleMesh.h"
#include <iostream>

MeshProperties::MeshProperties(TriangleMesh* mesh, QObject* parent) : QObject(parent), _mesh(mesh)
{
	_meshPoints = _mesh->getTrsfPoints();
	calculateSurfaceAreaAndVolume();
}

TriangleMesh* MeshProperties::mesh() const
{
	return _mesh;
}

void MeshProperties::setMesh(TriangleMesh* mesh)
{
	_mesh = mesh;
	_meshPoints.clear();
	_meshPoints = _mesh->getTrsfPoints();
	calculateSurfaceAreaAndVolume();
}

std::vector<float> MeshProperties::meshPoints() const
{
	return _meshPoints;
}

float MeshProperties::surfaceArea() const
{
	return _surfaceArea;
}

float MeshProperties::volume() const
{
	return _volume;
}

BoundingBox MeshProperties::boundingBox() const
{
	return _mesh->getBoundingBox();
}

void MeshProperties::calculateSurfaceAreaAndVolume()
{
	_surfaceArea = 0;
	_volume = 0;
	try {		
		std::vector<unsigned int> indices = _mesh->getIndices();
		size_t offset = 3; // each index points to 3 floats
		for (size_t i = 0; i < indices.size();)
		{
			// Vertex 1
			QVector3D p1(_meshPoints[offset * indices[i] + 0], // x coordinate
				_meshPoints[offset * indices[i] + 1],          // y coordinate
				_meshPoints[offset * indices[i] + 2]);         // z coordinate
			i++;

			// Vertex 2
			QVector3D p2(_meshPoints[offset * indices[i] + 0], // x coordinate
				_meshPoints[offset * indices[i] + 1],          // y coordinate
				_meshPoints[offset * indices[i] + 2]);         // z coordinate
			i++;

			// Vertex 3
			QVector3D p3(_meshPoints[offset * indices[i] + 0], // x coordinate
				_meshPoints[offset * indices[i] + 1],          // y coordinate
				_meshPoints[offset * indices[i] + 2]);         // z coordinate
			i++;

			_volume += QVector3D::dotProduct(p1, (QVector3D::crossProduct(p2, p3))) / 6.0f;

			_surfaceArea += QVector3D::crossProduct(p2 - p1, p3 - p1).length() * 0.5;
		}
	}
	catch (const std::exception& ex) {
		std::cout << "Exception raised in MeshProperties::calculateSurfaceAreaAndVolume\n" << ex.what() << std::endl;
	}

	_volume = fabs(_volume);
}