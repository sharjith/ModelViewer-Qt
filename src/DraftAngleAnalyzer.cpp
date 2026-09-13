#include "DraftAngleAnalyzer.h"
#include "SceneMesh.h"

#include <QtMath>
#include <algorithm>

std::vector<float> DraftAngleAnalyzer::computeDraftAnglesDegrees(SceneMesh* mesh, const QVector3D& pullDirection)
{
	std::vector<float> result;
	if (!mesh)
		return result;

	const QVector3D pull = pullDirection.length() > 1.0e-6f ? pullDirection.normalized() : QVector3D(0.0f, 1.0f, 0.0f);

	const std::vector<float> points = mesh->getTrsfPoints();
	const std::vector<unsigned int> indices = mesh->getIndices();
	const size_t faceCount = indices.size() / 3;
	result.reserve(faceCount);

	for (size_t f = 0; f < faceCount; ++f)
	{
		const unsigned int ia = indices[f * 3 + 0];
		const unsigned int ib = indices[f * 3 + 1];
		const unsigned int ic = indices[f * 3 + 2];

		const QVector3D p1(points[ia * 3 + 0], points[ia * 3 + 1], points[ia * 3 + 2]);
		const QVector3D p2(points[ib * 3 + 0], points[ib * 3 + 1], points[ib * 3 + 2]);
		const QVector3D p3(points[ic * 3 + 0], points[ic * 3 + 1], points[ic * 3 + 2]);

		// Real per-face (flat) normal from the triangle's own current
		// geometry, NOT an averaged imported vertex normal - see this
		// class's doc comment.
		QVector3D faceNormal = QVector3D::crossProduct(p2 - p1, p3 - p1);
		const float len = faceNormal.length();
		if (len < 1.0e-9f)
		{
			// Degenerate triangle - no meaningful normal, report a neutral
			// 0.0 rather than a value derived from an undefined direction.
			result.push_back(0.0f);
			continue;
		}
		faceNormal /= len;

		// theta = angle between the face normal and the pull direction.
		// draftAngle = 90 - theta: 0 at theta=90 (vertical wall, normal
		// perpendicular to pull), +90 at theta=0 (flat top, normal parallel
		// to pull), -90 at theta=180 (normal pointing back opposite the
		// pull direction - the worst-case undercut). See this class's doc
		// comment for the physical reasoning.
		const float cosTheta = std::clamp(QVector3D::dotProduct(faceNormal, pull), -1.0f, 1.0f);
		const float thetaDegrees = qRadiansToDegrees(std::acos(cosTheta));
		result.push_back(90.0f - thetaDegrees);
	}

	return result;
}
