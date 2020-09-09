#include "Cube.h"
#include <cstdio>

Cube::Cube(QOpenGLShaderProgram* prog, float size) : QuadMesh(prog, "Cube", 1, 1)
{
	float side = size / 2.0f;

	std::vector<float> p = {
		// Front
		-side, -side, side, -side, -side, -side, side, -side, -side, side, -side, side,
		// Right
		side, -side, side, side, -side, -side, side, side, -side, side, side, side,
		// Back
		-side, side, side, side, side, side, side, side, -side, -side, side, -side,
		// Left
		-side, -side, side, -side, side, side, -side, side, -side, -side, -side, -side,
		// Bottom
		-side, -side, -side, -side, side, -side, side, side, -side, side, -side, -side,
		// Top
		-side, -side, side, side, -side, side, side, side, side, -side, side, side };

	std::vector<float> n = {
		// Front
		0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f,
		// Right
		1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
		// Back
		0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
		// Left
		-1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
		// Bottom
		0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f,
		// Top
		0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };

	std::vector<float> tex = {
		// Front
		0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		// Right
		0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		// Back
		0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		// Left
		0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		// Bottom
		0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		// Top
		0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

	/*std::vector<unsigned int> el = {
		0,1,2,0,2,3,
		4,5,6,4,6,7,
		8,9,10,8,10,11,
		12,13,14,12,14,15,
		16,17,18,16,18,19,
		20,21,22,20,22,23
	};*/
	std::vector<unsigned int> el = {
		0, 1, 2, 3,
		4, 5, 6, 7,
		8, 9, 10, 11,
		12, 13, 14, 15,
		16, 17, 18, 19,
		20, 21, 22, 23 };

	initBuffers(&el, &p, &n, &tex);

	_boundingSphere.setCenter(0, 0, 0);
	_boundingSphere.setRadius(sqrt(3) * (size / 2));
    _boundingBox.setLimits(-side, side, -side, side, -side, side);
}

bool Cube::intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint)
{
	bool intersects = false;
	for (unsigned int i = 0; i < _indices.size(); i += 4)
	{
		QVector3D v0(_trsfpoints[_indices[i] + 0], _trsfpoints[_indices[i] + 1], _trsfpoints[_indices[i] + 2]);
		QVector3D v1(_trsfpoints[_indices[i + 1] + 0], _trsfpoints[_indices[i + 1] + 1], _trsfpoints[_indices[i + 1] + 2]);
		QVector3D v2(_trsfpoints[_indices[i + 2] + 0], _trsfpoints[_indices[i + 2] + 1], _trsfpoints[_indices[i + 2] + 2]);
		intersects = rayIntersectsTriangle(rayPos, rayDir, v0, v1, v2, outIntersectionPoint);
		if (intersects)
			break;
		QVector3D v3(_trsfpoints[_indices[i] + 0], _trsfpoints[_indices[i] + 1], _trsfpoints[_indices[i] + 2]);
		QVector3D v4(_trsfpoints[_indices[i + 1] + 0], _trsfpoints[_indices[i + 1] + 1], _trsfpoints[_indices[i + 1] + 2]);
		QVector3D v5(_trsfpoints[_indices[i + 3] + 0], _trsfpoints[_indices[i + 3] + 1], _trsfpoints[_indices[i + 3] + 2]);
		intersects = rayIntersectsTriangle(rayPos, rayDir, v3, v4, v5, outIntersectionPoint);
		if (intersects)
			break;
	}

	return intersects;
}
