#include "CurvatureAnalyzer.h"
#include "SceneMesh.h"
#include "MeshRepair.h"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/interpolated_corrected_curvatures.h>
#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/locate.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <QObject>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace
{
	using CvKernel = MeshRepair::Kernel;
	using CvPoint3 = MeshRepair::Point_3;
	using CvMesh = MeshRepair::Mesh;
	namespace PMP = CGAL::Polygon_mesh_processing;

	using VertexDescriptor = boost::graph_traits<CvMesh>::vertex_descriptor;
	using FaceDescriptor = boost::graph_traits<CvMesh>::face_descriptor;
	using VPM = boost::property_map<CvMesh, boost::vertex_point_t>::const_type;
	using AABBPrimitive = CGAL::AABB_face_graph_triangle_primitive<CvMesh, VPM>;
	using AABBTraits = CGAL::AABB_traits_3<CvKernel, AABBPrimitive>;
	using AABBTree = CGAL::AABB_tree<AABBTraits>;

	bool isFinitePoint(const std::vector<float>& pts, size_t vertexIndex)
	{
		for (int c = 0; c < 3; ++c)
		{
			const float v = pts[vertexIndex * 3 + c];
			if (!std::isfinite(v))
				return false;
		}
		return true;
	}

	// Simple union-find over the ORIGINAL mesh's own raw index buffer -
	// two triangles are connected if they share a vertex index. Used only
	// to answer "are these two original vertices on the same connected
	// piece of the original surface", the reference point the constrained-
	// matching validation below checks a repaired-mesh match against.
	class UnionFind
	{
	public:
		explicit UnionFind(size_t n) : _parent(n)
		{
			std::iota(_parent.begin(), _parent.end(), 0);
		}
		size_t find(size_t x)
		{
			while (_parent[x] != x)
			{
				_parent[x] = _parent[_parent[x]];
				x = _parent[x];
			}
			return x;
		}
		void unite(size_t a, size_t b)
		{
			a = find(a); b = find(b);
			if (a != b) _parent[a] = b;
		}
	private:
		std::vector<size_t> _parent;
	};
}

CurvatureResult CurvatureAnalyzer::computeMeanCurvature(SceneMesh* mesh, double ballRadius)
{
	CurvatureResult result;
	if (!mesh)
		return result;

	const std::vector<float>& origPoints = mesh->getTrsfPoints();
	const std::vector<float>& origNormals = mesh->getTrsfNormals();
	const std::vector<unsigned int> origIndices = mesh->getIndices();
	const size_t origVertexCount = origPoints.size() / 3;
	if (origPoints.empty() || origIndices.size() < 3 || origNormals.size() != origPoints.size())
		return result;

	// ---- Vertex validity + a filtered face list, shared by soup-building,
	// the original mesh's own connected-components union-find, and the
	// geometric-normal fallback below. A face referencing an out-of-bounds
	// index (malformed input - origIndices is never trusted blindly here)
	// or a non-finite position is excluded entirely, rather than fabricated
	// against a (0,0,0) stand-in position: a fabricated triangle would still
	// get fed into repair and could corrupt a NEIGHBORING, genuinely valid
	// vertex's curvature - the earlier "unreachable by any valid face"
	// comment on the old (0,0,0)-substitution code was simply wrong, since
	// nothing actually excluded that vertex's incident faces before. ----
	std::vector<bool> vertexFinite(origVertexCount, false);
	for (size_t v = 0; v < origVertexCount; ++v)
		vertexFinite[v] = isFinitePoint(origPoints, v);

	const size_t rawFaceCount = origIndices.size() / 3;
	std::vector<std::array<unsigned int, 3>> validFaces;
	validFaces.reserve(rawFaceCount);
	for (size_t f = 0; f < rawFaceCount; ++f)
	{
		const unsigned int ia = origIndices[f * 3 + 0];
		const unsigned int ib = origIndices[f * 3 + 1];
		const unsigned int ic = origIndices[f * 3 + 2];
		if (ia >= origVertexCount || ib >= origVertexCount || ic >= origVertexCount)
			continue;
		if (!vertexFinite[ia] || !vertexFinite[ib] || !vertexFinite[ic])
			continue;
		validFaces.push_back({ ia, ib, ic });
	}
	if (validFaces.empty())
		return result;

	// ---- Geometric (position-only) per-vertex normal fallback, used only
	// for the correspondence normal check in Pass 2 below - NOT a
	// replacement for the mesh's real shading normal anywhere else.
	// Correspondence cannot simply require the imported normal be non-zero:
	// this app's importer deliberately preserves all-zero normals for
	// positions-only triangle meshes (a valid input), which would otherwise
	// fail every single vertex's correspondence check even on perfectly
	// valid, successfully-repaired geometry. Unweighted face-normal average
	// per vertex, same cross-product convention DraftAngleAnalyzer already
	// uses for its own per-face geometric normal. ----
	std::vector<QVector3D> geomNormalAccum(origVertexCount, QVector3D(0.0f, 0.0f, 0.0f));
	for (const auto& f : validFaces)
	{
		const QVector3D p0(origPoints[f[0] * 3 + 0], origPoints[f[0] * 3 + 1], origPoints[f[0] * 3 + 2]);
		const QVector3D p1(origPoints[f[1] * 3 + 0], origPoints[f[1] * 3 + 1], origPoints[f[1] * 3 + 2]);
		const QVector3D p2(origPoints[f[2] * 3 + 0], origPoints[f[2] * 3 + 1], origPoints[f[2] * 3 + 2]);
		const QVector3D faceNormal = QVector3D::crossProduct(p1 - p0, p2 - p0);
		geomNormalAccum[f[0]] += faceNormal;
		geomNormalAccum[f[1]] += faceNormal;
		geomNormalAccum[f[2]] += faceNormal;
	}

	// Scale-aware correspondence distance limit (Pass 2 below) - a fixed
	// world-unit threshold would be wrong for both a tiny part and a large
	// assembly, same reasoning this app's other analyzers already apply to
	// their own tolerances (see MeshProperties.cpp).
	QVector3D bboxMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	QVector3D bboxMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
	for (size_t v = 0; v < origVertexCount; ++v)
	{
		if (!vertexFinite[v])
			continue;
		const QVector3D p(origPoints[v * 3 + 0], origPoints[v * 3 + 1], origPoints[v * 3 + 2]);
		bboxMin.setX(std::min(bboxMin.x(), p.x())); bboxMin.setY(std::min(bboxMin.y(), p.y())); bboxMin.setZ(std::min(bboxMin.z(), p.z()));
		bboxMax.setX(std::max(bboxMax.x(), p.x())); bboxMax.setY(std::max(bboxMax.y(), p.y())); bboxMax.setZ(std::max(bboxMax.z(), p.z()));
	}
	const double bboxDiagonal = static_cast<double>((bboxMax - bboxMin).length());
	const double maxCorrespondenceDistance = std::max(bboxDiagonal * 0.02, 1.0e-6);

	// ---- Build the soup and repair it (see this class's doc comment for why) ----
	std::vector<CvPoint3> soupPoints;
	soupPoints.reserve(origVertexCount);
	for (size_t v = 0; v < origVertexCount; ++v)
	{
		// Non-finite entries are never referenced by validFaces below, so
		// their exact placeholder value genuinely is unreachable now.
		soupPoints.emplace_back(
			vertexFinite[v] ? static_cast<double>(origPoints[v * 3 + 0]) : 0.0,
			vertexFinite[v] ? static_cast<double>(origPoints[v * 3 + 1]) : 0.0,
			vertexFinite[v] ? static_cast<double>(origPoints[v * 3 + 2]) : 0.0);
	}
	std::vector<std::array<std::size_t, 3>> soupFaces;
	soupFaces.reserve(validFaces.size());
	for (const auto& f : validFaces)
		soupFaces.push_back({ f[0], f[1], f[2] });

	CvMesh workingMesh;
	MeshRepairReport report;
	const bool repaired = MeshRepair::repairSoupToMesh(std::move(soupPoints), std::move(soupFaces), workingMesh, &report);
	if (!repaired || workingMesh.number_of_vertices() == 0 || workingMesh.number_of_faces() == 0)
		return result;

	result.repairSummary = report.wasAlreadyValid
		? QObject::tr("Mesh was already valid - no repair needed.")
		: QObject::tr("Repair adjusted the mesh before analysis: %1 soup point(s) removed, "
		              "%2 soup face(s) removed, %3 non-manifold vertex/vertices fixed%4.")
			.arg(static_cast<qulonglong>(report.soupPointsRemoved))
			.arg(static_cast<qulonglong>(report.soupFacesRemoved))
			.arg(static_cast<qulonglong>(report.nonManifoldVerticesFixed))
			.arg(report.hadSelfIntersections
				? (report.selfIntersectionsResolved
					? QObject::tr(", self-intersections resolved")
					: QObject::tr(", self-intersections could NOT be fully resolved"))
				: QString());

	// ---- Mean curvature on the repaired mesh ----
	auto meanCurvatureMap = workingMesh.add_property_map<VertexDescriptor, double>("v:mean_curvature", 0.0).first;
	PMP::interpolated_corrected_curvatures(workingMesh,
		CGAL::parameters::vertex_mean_curvature_map(meanCurvatureMap).ball_radius(ballRadius));

	auto vertexNormalMap = workingMesh.add_property_map<VertexDescriptor, CvKernel::Vector_3>("v:normal", CvKernel::Vector_3(0, 0, 0)).first;
	PMP::compute_vertex_normals(workingMesh, vertexNormalMap);

	auto faceComponentMap = workingMesh.add_property_map<FaceDescriptor, std::size_t>("f:component", 0).first;
	PMP::connected_components(workingMesh, faceComponentMap);

	AABBTree aabbTree;
	PMP::build_AABB_tree(workingMesh, aabbTree);

	// ---- Original mesh's own connected components (see UnionFind's doc
	// comment) - built from validFaces, not raw origIndices, so an
	// out-of-bounds or non-finite-referencing face can't reach unite() with
	// an invalid index either. ----
	UnionFind origUnion(origVertexCount);
	for (const auto& f : validFaces)
	{
		origUnion.unite(f[0], f[1]);
		origUnion.unite(f[1], f[2]);
	}

	// ---- Pass 1: locate every original vertex on the repaired mesh, tally
	// which repaired connected component each original component's
	// vertices actually land in (majority vote), before deciding validity. ----
	struct Located
	{
		bool ok = false;
		double curvature = 0.0;
		CvKernel::Vector_3 normal { 0, 0, 0 };
		std::size_t repairedComponent = 0;
		// Squared distance from the original query point to the matched
		// point on the repaired mesh - Pass 2 rejects a match that's
		// suspiciously far away (see maxCorrespondenceDistance above),
		// since "nearest point CGAL could find" is not the same claim as
		// "the right point" once distance, not just normal/component
		// agreement, gets checked too.
		double distanceSq = 0.0;
	};
	std::vector<Located> located(origVertexCount);
	std::unordered_map<size_t, std::unordered_map<std::size_t, size_t>> votesByOrigComponent;

	for (size_t v = 0; v < origVertexCount; ++v)
	{
		if (!isFinitePoint(origPoints, v))
			continue;

		const CvPoint3 query(origPoints[v * 3 + 0], origPoints[v * 3 + 1], origPoints[v * 3 + 2]);
		const auto loc = PMP::locate_with_AABB_tree(query, aabbTree, workingMesh);
		const FaceDescriptor f = loc.first;
		const auto& bary = loc.second;

		// CGAL's barycentric weights correspond to (source(h), target(h),
		// target(next(h))) for h = halfedge(f, mesh) - NOT the order
		// CGAL::vertices_around_face(halfedge(f, mesh), mesh) iterates in,
		// which starts at the halfedge's TARGET. Using the iterator order
		// here silently paired each weight with the wrong vertex - a cyclic
		// mismatch that still produced a plausible-looking (but wrong)
		// interpolated curvature/normal/position on every single query.
		const auto h = halfedge(f, workingMesh);
		const std::array<VertexDescriptor, 3> faceVerts {
			source(h, workingMesh),
			target(h, workingMesh),
			target(next(h, workingMesh), workingMesh)
		};

		Located& l = located[v];
		l.ok = true;
		l.curvature = bary[0] * meanCurvatureMap[faceVerts[0]]
		            + bary[1] * meanCurvatureMap[faceVerts[1]]
		            + bary[2] * meanCurvatureMap[faceVerts[2]];
		l.normal = bary[0] * vertexNormalMap[faceVerts[0]]
		         + bary[1] * vertexNormalMap[faceVerts[1]]
		         + bary[2] * vertexNormalMap[faceVerts[2]];
		l.repairedComponent = faceComponentMap[f];

		// PMP::construct_point() applies CGAL's own convention internally -
		// reusing it here (rather than hand-interpolating from faceVerts)
		// means this can never drift out of sync with it again.
		const CvPoint3 matchedPoint = PMP::construct_point(loc, workingMesh);
		const double dx = CGAL::to_double(matchedPoint.x()) - static_cast<double>(origPoints[v * 3 + 0]);
		const double dy = CGAL::to_double(matchedPoint.y()) - static_cast<double>(origPoints[v * 3 + 1]);
		const double dz = CGAL::to_double(matchedPoint.z()) - static_cast<double>(origPoints[v * 3 + 2]);
		l.distanceSq = dx * dx + dy * dy + dz * dz;

		// Only a match that's actually close votes on which repaired
		// component its original component maps to - a distant, wrong
		// match must not be allowed to skew (or dominate) the majority
		// vote itself, on top of being rejected individually in Pass 2.
		if (l.distanceSq <= maxCorrespondenceDistance * maxCorrespondenceDistance)
			votesByOrigComponent[origUnion.find(v)][l.repairedComponent]++;
	}

	// Majority repaired-component per original component.
	std::unordered_map<size_t, std::size_t> majorityRepairedComponent;
	for (const auto& [origComp, votes] : votesByOrigComponent)
	{
		std::size_t bestComp = 0;
		size_t bestCount = 0;
		for (const auto& [comp, count] : votes)
		{
			if (count > bestCount)
			{
				bestCount = count;
				bestComp = comp;
			}
		}
		majorityRepairedComponent[origComp] = bestComp;
	}

	// ---- Pass 2: validate distance+normal+component jointly - all three,
	// not distance alone (misses a thin-wall opposite-side jump), not
	// normal+component alone (misses a distant match on a similarly-
	// oriented, same-voted-component but geometrically unrelated patch of
	// surface, e.g. when repair removed/reshaped the true nearby region). ----
	result.meanCurvaturePerVertex.assign(origVertexCount, 0.0f);
	result.validPerVertex.assign(origVertexCount, false);

	constexpr double kNormalSimilarityThreshold = 0.3; // ~72 degrees - generous, but rejects a genuine opposite-side match
	const double maxCorrespondenceDistanceSq = maxCorrespondenceDistance * maxCorrespondenceDistance;
	for (size_t v = 0; v < origVertexCount; ++v)
	{
		const Located& l = located[v];
		if (!l.ok)
			continue;

		if (l.distanceSq > maxCorrespondenceDistanceSq)
			continue; // matched point too far from the original - ambiguous/lost correspondence

		// Prefer the mesh's real imported (shading) normal; fall back to
		// the position-derived geometric one ONLY when the imported normal
		// is degenerate/absent (see geomNormalAccum's doc comment) - a
		// valid positions-only import must not fail every vertex here.
		QVector3D origNormalQ(origNormals[v * 3 + 0], origNormals[v * 3 + 1], origNormals[v * 3 + 2]);
		if (origNormalQ.lengthSquared() < 1.0e-12f)
			origNormalQ = geomNormalAccum[v];
		const CvKernel::Vector_3 origNormal(origNormalQ.x(), origNormalQ.y(), origNormalQ.z());
		const double origLen = std::sqrt(CGAL::to_double(origNormal.squared_length()));
		const double matchLen = std::sqrt(CGAL::to_double(l.normal.squared_length()));
		if (origLen < 1e-9 || matchLen < 1e-9)
			continue;
		const double cosAngle = CGAL::to_double(origNormal * l.normal) / (origLen * matchLen);
		if (cosAngle < kNormalSimilarityThreshold)
			continue; // opposite-side / thin-wall guard

		const size_t origComp = origUnion.find(v);
		const auto majIt = majorityRepairedComponent.find(origComp);
		if (majIt == majorityRepairedComponent.end() || majIt->second != l.repairedComponent)
			continue; // disconnected-piece guard

		result.meanCurvaturePerVertex[v] = static_cast<float>(l.curvature);
		result.validPerVertex[v] = true;
	}

	result.succeeded = true;
	return result;
}
