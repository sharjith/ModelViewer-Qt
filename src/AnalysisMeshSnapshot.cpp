#include "AnalysisMeshSnapshot.h"
#include "SceneMesh.h"

AnalysisMeshSnapshot captureAnalysisMeshSnapshot(SceneMesh* mesh, const QVariantMap& parameters,
	SceneMesh* referenceMesh)
{
	AnalysisMeshSnapshot snapshot;
	if (!mesh)
		return snapshot;

	snapshot.meshHandle = mesh;
	snapshot.points = mesh->getTrsfPoints();
	snapshot.indices = mesh->getIndices();
	snapshot.normals = mesh->getTrsfNormals();
	snapshot.sourceMeshIds = mesh->getSourceMeshIds();
	snapshot.boundingBox = mesh->getBoundingBox();
	snapshot.key = SurfaceAnalysisOverlay::computeCurrentKey(mesh, parameters, referenceMesh);

	return snapshot;
}
