#include "MeshProperties.h"
#include "TriangleMesh.h"
#include <iostream>

MeshProperties::MeshProperties(TriangleMesh* mesh, QObject* parent) : QObject(parent), _mesh(mesh)
{
	_meshPoints = _mesh->getPoints();
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
	_meshPoints = _mesh->getPoints();
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

void MeshProperties::calculateSurfaceAreaAndVolume()
{
	_surfaceArea = 0;
	_volume = 0;
	try {
		for (size_t i = 0; i < _meshPoints.size(); i += 9)
		{
			QVector3D p1(_meshPoints.at(i + 0), _meshPoints.at(i + 1), _meshPoints.at(i + 2));
			QVector3D p2(_meshPoints.at(i + 3), _meshPoints.at(i + 4), _meshPoints.at(i + 5));
			QVector3D p3(_meshPoints.at(i + 6), _meshPoints.at(i + 7), _meshPoints.at(i + 8));

			_volume += QVector3D::dotProduct(p1, (QVector3D::crossProduct(p2, p3))) / 6.0f;

			_surfaceArea += QVector3D::crossProduct(p2 - p1, p3 - p1).length() * 0.5;
		}
	}
	catch (const std::exception& ex) {
		std::cout << "Exception raised in MeshProperties::calculateSurfaceAreaAndVolume\n" << ex.what() << std::endl;
	}

	_volume = fabs(_volume);
}