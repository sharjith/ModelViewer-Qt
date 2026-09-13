#include "DeviationAnalyzer.h"
#include "SceneMesh.h"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_triangle_primitive_3.h>

#include <cmath>

namespace
{
	using DevKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
	using DevPoint3 = DevKernel::Point_3;
	using DevTriangle3 = DevKernel::Triangle_3;
	using DevTriangleIterator = std::vector<DevTriangle3>::const_iterator;
	using DevPrimitive = CGAL::AABB_triangle_primitive_3<DevKernel, DevTriangleIterator>;
	using DevTraits = CGAL::AABB_traits_3<DevKernel, DevPrimitive>;
	using DevTree = CGAL::AABB_tree<DevTraits>;
}

std::vector<float> DeviationAnalyzer::computeDeviation(SceneMesh* sampledMesh, SceneMesh* referenceMesh)
{
	std::vector<float> result;
	if (!sampledMesh || !referenceMesh)
		return result;

	const std::vector<float>& refPoints = referenceMesh->getTrsfPoints();
	const std::vector<unsigned int> refIndices = referenceMesh->getIndices();
	if (refPoints.empty() || refIndices.size() < 3)
		return result;

	// Original (unrepaired) triangles only - see this class's doc comment
	// for why repair is deliberately not run here.
	std::vector<DevTriangle3> triangles;
	triangles.reserve(refIndices.size() / 3);
	for (size_t f = 0; f + 2 < refIndices.size(); f += 3)
	{
		const unsigned int ia = refIndices[f + 0];
		const unsigned int ib = refIndices[f + 1];
		const unsigned int ic = refIndices[f + 2];

		const DevPoint3 pa(refPoints[ia * 3 + 0], refPoints[ia * 3 + 1], refPoints[ia * 3 + 2]);
		const DevPoint3 pb(refPoints[ib * 3 + 0], refPoints[ib * 3 + 1], refPoints[ib * 3 + 2]);
		const DevPoint3 pc(refPoints[ic * 3 + 0], refPoints[ic * 3 + 1], refPoints[ic * 3 + 2]);

		const DevTriangle3 tri(pa, pb, pc);
		if (tri.is_degenerate())
			continue;

		triangles.push_back(tri);
	}
	if (triangles.empty())
		return result;

	DevTree tree(triangles.cbegin(), triangles.cend());
	tree.accelerate_distance_queries();

	const std::vector<float>& sampledPoints = sampledMesh->getTrsfPoints();
	const size_t vertexCount = sampledPoints.size() / 3;
	result.reserve(vertexCount);
	for (size_t v = 0; v < vertexCount; ++v)
	{
		const DevPoint3 query(sampledPoints[v * 3 + 0], sampledPoints[v * 3 + 1], sampledPoints[v * 3 + 2]);
		const double squaredDist = CGAL::to_double(tree.squared_distance(query));
		result.push_back(static_cast<float>(std::sqrt(std::max(0.0, squaredDist))));
	}

	return result;
}
