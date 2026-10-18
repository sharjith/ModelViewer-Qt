#include "ViewCubeMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <QHash>

namespace
{
struct CanonicalVertex
{
	float x;
	float y;
	float z;
};

struct TriangleIndices
{
	unsigned int a;
	unsigned int b;
	unsigned int c;
};

QVector3D faceNormal(const QVector3D& a, const QVector3D& b, const QVector3D& c)
{
	const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
	if (normal.lengthSquared() <= 1.0e-8f)
		return QVector3D(0.0f, 0.0f, 1.0f);
	return normal.normalized();
}

QString normalKey(const QVector3D& normal)
{
	const auto rounded = [](float value) {
		return QString::number(std::round(value * 1000.0f) / 1000.0f, 'f', 3);
	};
	return rounded(normal.x()) + "," + rounded(normal.y()) + "," + rounded(normal.z());
}

bool rayTriangleIntersect(const QVector3D& origin,
                          const QVector3D& direction,
                          const QVector3D& v0,
                          const QVector3D& v1,
                          const QVector3D& v2,
                          float& outT)
{
	constexpr float kEpsilon = 1.0e-6f;
	const QVector3D edge1 = v1 - v0;
	const QVector3D edge2 = v2 - v0;
	const QVector3D pVec = QVector3D::crossProduct(direction, edge2);
	const float det = QVector3D::dotProduct(edge1, pVec);
	if (std::abs(det) < kEpsilon)
		return false;

	const float invDet = 1.0f / det;
	const QVector3D tVec = origin - v0;
	const float u = QVector3D::dotProduct(tVec, pVec) * invDet;
	if (u < 0.0f || u > 1.0f)
		return false;

	const QVector3D qVec = QVector3D::crossProduct(tVec, edge1);
	const float v = QVector3D::dotProduct(direction, qVec) * invDet;
	if (v < 0.0f || (u + v) > 1.0f)
		return false;

	const float t = QVector3D::dotProduct(edge2, qVec) * invDet;
	if (t <= kEpsilon)
		return false;

	outT = t;
	return true;
}
}

ViewCubeMesh::ViewCubeMesh(QOpenGLShaderProgram* prog, float size, float chamferRatio)
	: RenderableMesh(prog, "ViewCube"),
	  _size(size),
	  _chamferRatio(chamferRatio)
{
	setParameters(size, chamferRatio);
}

RenderableMesh* ViewCubeMesh::clone()
{
	return new ViewCubeMesh(_prog, _size, _chamferRatio);
}

void ViewCubeMesh::setParameters(float size, float chamferRatio)
{
	_size = size;
	_chamferRatio = std::clamp(chamferRatio, 0.01f, 0.49f);

	const float halfExtent = size * 0.5f;
	const float inset = halfExtent * (1.0f - _chamferRatio);

	const std::array<CanonicalVertex, 24> canonicalVerts = {{
		{ inset, -halfExtent, -inset },
		{ inset, -inset, -halfExtent },
		{ halfExtent, -inset, -inset },
		{ -inset, -inset, -halfExtent },
		{ -inset, -halfExtent, -inset },
		{ inset, -halfExtent, inset },
		{ -inset, -halfExtent, inset },
		{ -halfExtent, -inset, -inset },
		{ -inset, inset, -halfExtent },
		{ inset, inset, -halfExtent },
		{ halfExtent, inset, -inset },
		{ halfExtent, -inset, inset },
		{ inset, halfExtent, -inset },
		{ halfExtent, inset, inset },
		{ inset, -inset, halfExtent },
		{ -inset, -inset, halfExtent },
		{ -halfExtent, -inset, inset },
		{ -halfExtent, inset, -inset },
		{ -inset, halfExtent, -inset },
		{ inset, halfExtent, inset },
		{ inset, inset, halfExtent },
		{ -halfExtent, inset, inset },
		{ -inset, halfExtent, inset },
		{ -inset, inset, halfExtent }
	}};

	const std::array<TriangleIndices, 44> triangles = {{
		{ 0, 1, 2 }, { 3, 1, 0 }, { 0, 4, 3 }, { 5, 6, 4 }, { 4, 0, 5 }, { 3, 4, 7 },
		{ 1, 3, 8 }, { 8, 9, 1 }, { 10, 2, 1 }, { 1, 9, 10 }, { 5, 0, 2 }, { 2, 11, 5 },
		{ 9, 12, 10 }, { 13, 11, 2 }, { 2, 10, 13 }, { 5, 11, 14 }, { 14, 15, 6 }, { 6, 5, 14 },
		{ 16, 7, 4 }, { 4, 6, 16 }, { 8, 3, 7 }, { 7, 17, 8 }, { 18, 12, 9 }, { 9, 8, 18 },
		{ 13, 10, 12 }, { 12, 19, 13 }, { 20, 14, 11 }, { 11, 13, 20 }, { 6, 15, 16 }, { 21, 17, 7 },
		{ 7, 16, 21 }, { 8, 17, 18 }, { 18, 22, 19 }, { 19, 12, 18 }, { 15, 14, 20 }, { 20, 23, 15 },
		{ 13, 19, 20 }, { 21, 16, 15 }, { 15, 23, 21 }, { 22, 18, 17 }, { 17, 21, 22 }, { 23, 20, 19 },
		{ 19, 22, 23 }, { 21, 23, 22 }
	}};

	std::vector<unsigned int> indices;
	std::vector<float> points;
	std::vector<float> normals;
	_regionNormals.clear();
	_triangleRegions.clear();
	indices.reserve(triangles.size() * 3);
	points.reserve(triangles.size() * 9);
	normals.reserve(triangles.size() * 9);
	_triangleRegions.reserve(static_cast<int>(triangles.size()));
	QHash<QString, int> regionIdsByKey;

	for (const TriangleIndices& tri : triangles)
	{
		const CanonicalVertex& va = canonicalVerts[tri.a];
		const CanonicalVertex& vb = canonicalVerts[tri.b];
		const CanonicalVertex& vc = canonicalVerts[tri.c];
		const QVector3D a(va.x, va.y, va.z);
		const QVector3D b(vb.x, vb.y, vb.z);
		const QVector3D c(vc.x, vc.y, vc.z);
		const QVector3D n = faceNormal(a, b, c);
		const QString key = normalKey(n);
		int regionId = regionIdsByKey.value(key, -1);
		if (regionId < 0)
		{
			regionId = _regionNormals.size();
			regionIdsByKey.insert(key, regionId);
			_regionNormals.push_back(n);
		}
		const unsigned int baseIndex = static_cast<unsigned int>(points.size() / 3);
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);
		_triangleRegions.push_back({ a, b, c, regionId, n, static_cast<unsigned int>(indices.size() - 3) });

		const std::array<QVector3D, 3> triPoints = { a, b, c };
		for (const QVector3D& p : triPoints)
		{
			points.push_back(p.x());
			points.push_back(p.y());
			points.push_back(p.z());
			normals.push_back(n.x());
			normals.push_back(n.y());
			normals.push_back(n.z());
		}
	}

	initBuffers(&indices, &points, &normals, nullptr, nullptr, nullptr, nullptr);

	BoundingSphere bs; bs.setCenter(0.0f, 0.0f, 0.0f); bs.setRadius(std::sqrt(3.0f) * halfExtent);
	_instanceState.setBoundingSphere(bs);
	BoundingBox bb; bb.setLimits(-halfExtent, halfExtent, -halfExtent, halfExtent, -halfExtent, halfExtent);
	_instanceState.setBoundingBox(bb);
}

void ViewCubeMesh::renderRegion(int regionId)
{
	if (!_vertexArrayObject.isCreated() || regionId < 0)
		return;

	const QVariant globalModelVar = _prog->property("globalModelMatrix");
	const QMatrix4x4 globalModelMatrix = globalModelVar.isValid()
		? globalModelVar.value<QMatrix4x4>()
		: QMatrix4x4();
	const QMatrix4x4 modelMatrix = globalModelMatrix * combinedRenderTransform();
	const QVariant viewVar = _prog->property("viewMatrix");
	const QMatrix4x4 viewMatrix = viewVar.isValid() ? viewVar.value<QMatrix4x4>() : QMatrix4x4();
	const QMatrix4x4 modelViewMatrix = viewMatrix * modelMatrix;

	if (_prog->uniformLocation("modelMatrix") >= 0)
		_prog->setUniformValue("modelMatrix", modelMatrix);
	if (_prog->uniformLocation("modelViewMatrix") >= 0)
		_prog->setUniformValue("modelViewMatrix", modelViewMatrix);
	if (_prog->uniformLocation("normalMatrix") >= 0)
		_prog->setUniformValue("normalMatrix", modelViewMatrix.normalMatrix());

	_vertexArrayObject.bind();
	for (const TriangleRegion& tri : _triangleRegions)
	{
		if (tri.regionId != regionId)
			continue;
		glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT,
			reinterpret_cast<const void*>(static_cast<size_t>(tri.elementOffset) * sizeof(unsigned int)));
	}
	_vertexArrayObject.release();
}

QVector3D ViewCubeMesh::regionNormal(int regionId) const
{
	if (regionId < 0 || regionId >= _regionNormals.size())
		return QVector3D();
	return _regionNormals[regionId];
}

bool ViewCubeMesh::isPrimaryFaceRegion(int regionId) const
{
	const QVector3D normal = regionNormal(regionId);
	if (normal.lengthSquared() <= 1.0e-8f)
		return false;

	const auto near = [](float a, float b) {
		return std::abs(a - b) <= 1.0e-3f;
	};

	const bool xAxis = near(std::abs(normal.x()), 1.0f) && near(normal.y(), 0.0f) && near(normal.z(), 0.0f);
	const bool yAxis = near(std::abs(normal.y()), 1.0f) && near(normal.x(), 0.0f) && near(normal.z(), 0.0f);
	const bool zAxis = near(std::abs(normal.z()), 1.0f) && near(normal.x(), 0.0f) && near(normal.y(), 0.0f);
	return xAxis || yAxis || zAxis;
}

bool ViewCubeMesh::pickRegion(const QVector3D& rayOriginWorld,
                              const QVector3D& rayDirWorld,
                              const QMatrix4x4& modelMatrix,
                              RegionHit& outHit) const
{
	outHit = RegionHit{};
	if (_triangleRegions.isEmpty() || rayDirWorld.lengthSquared() <= 1.0e-8f)
		return false;

	const QMatrix4x4 invModel = modelMatrix.inverted();
	const QVector3D localOrigin = invModel.map(rayOriginWorld);
	const QVector3D localRayPoint = invModel.map(rayOriginWorld + rayDirWorld);
	QVector3D localDir = localRayPoint - localOrigin;
	if (localDir.lengthSquared() <= 1.0e-8f)
		return false;
	localDir.normalize();

	float nearestT = std::numeric_limits<float>::max();
	bool hit = false;
	for (const TriangleRegion& tri : _triangleRegions)
	{
		float t = 0.0f;
		if (!rayTriangleIntersect(localOrigin, localDir, tri.a, tri.b, tri.c, t))
			continue;
		if (t >= nearestT)
			continue;
		nearestT = t;
		outHit.regionId = tri.regionId;
		outHit.outwardNormal = tri.outwardNormal;
		hit = true;
	}

	return hit;
}
