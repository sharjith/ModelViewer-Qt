#include "RtEmbreeScene.h"

#include <embree3/rtcore.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

RtEmbreeScene::RtEmbreeScene()
{
	_device = rtcNewDevice(nullptr);
}

RtEmbreeScene::~RtEmbreeScene()
{
	releaseScene();
	if (_device)
	{
		rtcReleaseDevice(_device);
		_device = nullptr;
	}
}

void RtEmbreeScene::releaseScene()
{
	for (RTCScene blas : _blasScenes)
	{
		if (blas) rtcReleaseScene(blas);
	}
	_blasScenes.clear();
	_geomIdToInstanceIndex.clear();

	if (_topScene)
	{
		rtcReleaseScene(_topScene);
		_topScene = nullptr;
	}
}

bool RtEmbreeScene::build(std::shared_ptr<const RtSceneSnapshot> snapshot)
{
	releaseScene();
	_snapshot = std::move(snapshot);
	if (!_snapshot || !_device) return false;

	// One BLAS scene per unique mesh (see class comment: v1 is 1:1 with
	// instances, dedup is a later perf optimization, not a correctness need).
	_blasScenes.reserve(_snapshot->meshes.size());
	for (const RtMeshGeometry& mesh : _snapshot->meshes)
	{
		RTCScene blas = rtcNewScene(_device);

		if (!mesh.vertices.empty() && mesh.indices.size() >= 3)
		{
			RTCGeometry geom = rtcNewGeometry(_device, RTC_GEOMETRY_TYPE_TRIANGLE);

			// Shared (zero-copy) buffers directly against the snapshot's own
			// vectors - valid as long as _snapshot outlives this scene, which
			// it does (held via shared_ptr above).
			rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
				mesh.vertices.data(), 0, sizeof(RtVertex), mesh.vertices.size());
			rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
				mesh.indices.data(), 0, 3 * sizeof(uint32_t), mesh.indices.size() / 3);

			rtcCommitGeometry(geom);
			rtcAttachGeometry(blas, geom);
			rtcReleaseGeometry(geom);
		}

		rtcCommitScene(blas);
		_blasScenes.push_back(blas);
	}

	// TLAS: one INSTANCE geometry per RtInstance, applying its world transform
	// to the corresponding BLAS scene.
	_topScene = rtcNewScene(_device);
	_geomIdToInstanceIndex.assign(_snapshot->instances.size(), 0);

	for (size_t i = 0; i < _snapshot->instances.size(); ++i)
	{
		const RtInstance& instance = _snapshot->instances[i];
		if (instance.meshIndex >= _blasScenes.size()) continue;

		RTCGeometry instGeom = rtcNewGeometry(_device, RTC_GEOMETRY_TYPE_INSTANCE);
		rtcSetGeometryInstancedScene(instGeom, _blasScenes[instance.meshIndex]);
		rtcSetGeometryTimeStepCount(instGeom, 1);
		rtcSetGeometryTransform(instGeom, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
			glm::value_ptr(instance.localToWorld));
		rtcCommitGeometry(instGeom);

		const unsigned int geomID = rtcAttachGeometry(_topScene, instGeom);
		rtcReleaseGeometry(instGeom);

		if (geomID >= _geomIdToInstanceIndex.size())
			_geomIdToInstanceIndex.resize(geomID + 1, 0);
		_geomIdToInstanceIndex[geomID] = static_cast<uint32_t>(i);
	}

	rtcCommitScene(_topScene);
	return true;
}

RtHit RtEmbreeScene::intersect(const RtRay& ray) const
{
	RtHit result;
	if (!_topScene) return result;

	RTCIntersectContext context;
	rtcInitIntersectContext(&context);

	RTCRayHit rayhit;
	rayhit.ray.org_x = ray.origin.x;
	rayhit.ray.org_y = ray.origin.y;
	rayhit.ray.org_z = ray.origin.z;
	rayhit.ray.dir_x = ray.direction.x;
	rayhit.ray.dir_y = ray.direction.y;
	rayhit.ray.dir_z = ray.direction.z;
	rayhit.ray.tnear = ray.tNear;
	rayhit.ray.tfar  = ray.tFar;
	rayhit.ray.mask  = static_cast<unsigned int>(-1);
	rayhit.ray.flags = 0;
	rayhit.ray.time  = 0.0f;
	rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
	rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

	rtcIntersect1(_topScene, &context, &rayhit);

	if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID || rayhit.hit.instID[0] == RTC_INVALID_GEOMETRY_ID)
		return result;

	const unsigned int instGeomID = rayhit.hit.instID[0];
	if (instGeomID >= _geomIdToInstanceIndex.size())
		return result;

	const uint32_t instanceIndex = _geomIdToInstanceIndex[instGeomID];
	const RtInstance& instance = _snapshot->instances[instanceIndex];
	const RtMeshGeometry& mesh = _snapshot->meshes[instance.meshIndex];

	const uint32_t triBase = rayhit.hit.primID * 3;
	if (triBase + 2 >= mesh.indices.size())
		return result;

	const RtVertex& v0 = mesh.vertices[mesh.indices[triBase + 0]];
	const RtVertex& v1 = mesh.vertices[mesh.indices[triBase + 1]];
	const RtVertex& v2 = mesh.vertices[mesh.indices[triBase + 2]];

	// Embree barycentric convention: u weights v1, v weights v2, (1-u-v) weights v0.
	const float u = rayhit.hit.u;
	const float v = rayhit.hit.v;
	const float w = 1.0f - u - v;

	const glm::vec3 localNormal    = w * v0.normal    + u * v1.normal    + v * v2.normal;
	const glm::vec3 localTangent   = w * v0.tangent   + u * v1.tangent   + v * v2.tangent;
	const glm::vec3 localBitangent = w * v0.bitangent + u * v1.bitangent + v * v2.bitangent;

	// Flat per-triangle (geometric) normal, distinct from the smooth
	// vertex-interpolated one above - CpuPathTracer uses this only for
	// numerically robust ray-offsetting.
	const glm::vec3 localGeometricNormal = glm::cross(v1.position - v0.position, v2.position - v0.position);

	// Instance geometries return hit data in the BLAS's local/object space
	// (only ray.org/dir stay in world space) - transform position/normal back
	// to world space using the instance's transform.
	const glm::mat3 modelMatrix3 = glm::mat3(instance.localToWorld);
	const glm::mat3 normalMatrix = glm::inverseTranspose(modelMatrix3);

	result.hit             = true;
	result.distance        = rayhit.ray.tfar;
	result.position        = ray.origin + ray.direction * rayhit.ray.tfar;
	result.normal          = glm::normalize(normalMatrix * localNormal);
	result.geometricNormal = glm::normalize(normalMatrix * localGeometricNormal);
	// Tangent/bitangent run along the surface, not perpendicular to it - they
	// transform with the plain model matrix, not its inverse-transpose (that
	// correction is specifically for normals). Left un-normalized when the
	// mesh has no tangent data (zero in, zero out) so the zero-length "no
	// tangent data" signal survives the transform - see RtVertex::tangent.
	result.tangent         = modelMatrix3 * localTangent;
	result.bitangent       = modelMatrix3 * localBitangent;
	for (int uvSet = 0; uvSet < 4; ++uvSet)
		result.texCoords[uvSet] = w * v0.texCoords[uvSet] + u * v1.texCoords[uvSet] + v * v2.texCoords[uvSet];
	result.vertexColor     = w * v0.color + u * v1.color + v * v2.color;
	result.instanceIndex   = instanceIndex;
	result.materialIndex   = instance.materialIndex;

	return result;
}

bool RtEmbreeScene::occluded(const RtRay& ray) const
{
	if (!_topScene) return false;

	RTCIntersectContext context;
	rtcInitIntersectContext(&context);

	RTCRay r;
	r.org_x = ray.origin.x;
	r.org_y = ray.origin.y;
	r.org_z = ray.origin.z;
	r.dir_x = ray.direction.x;
	r.dir_y = ray.direction.y;
	r.dir_z = ray.direction.z;
	r.tnear = ray.tNear;
	r.tfar  = ray.tFar;
	r.mask  = static_cast<unsigned int>(-1);
	r.flags = 0;
	r.time  = 0.0f;

	rtcOccluded1(_topScene, &context, &r);

	// Embree convention: occluded rays have tfar set to -inf.
	return r.tfar < 0.0f;
}
