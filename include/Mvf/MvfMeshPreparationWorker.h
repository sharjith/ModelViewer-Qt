#pragma once

#include "Material.h"
#include "GltfAnimationData.h"
#include "GltfVariantData.h"
#include "MeshImportAdaptor.h"
#include "MeshVertex.h"
#include "MvfDocument.h"

#include <QByteArray>
#include <QMap>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QUuid>
#include <QVector>
#include <QVector3D>

// Pre-computed mesh data produced by CPU-side MVF preparation.
// All fields are plain data; no GL resources are created here.
struct PreparedMvfMesh
{
	QString      name;
	QUuid        uuid;
	GLenum       primitiveMode = GL_TRIANGLES;
	int          sceneIndex    = -1;
	bool         hasNegativeScale = false;
	int          originalMaterialIndex = -1;
	QString      sourceFile;
	QString      sourceNodeName;
	QVector<GltfVariantMapping> variantMappings;
	QMap<int, Material> allVariantMaterials;
	std::vector<Vertex>       vertices;
	std::vector<unsigned int> indices;
	Material   material;
	QVector<GltfSkinJoint>   skinJoints;

	// Morph targets (blend shapes) - position/normal/tangent deltas.
	QVector<MorphTargetData> morphTargets;
	QVector<float>           defaultMorphWeights;

	// OCC B-Rep edge segments and per-topological-edge boundaries.
	std::vector<float> occEdgeSegments;
	std::vector<int>   occEdgeBoundaries;
	// Analytic circle data (Edge Radius measurement tool), 1:1 with
	// occEdgeBoundaries - see OccEdgeCircleInfo's doc comment.
	std::vector<OccEdgeCircleInfo> occEdgeCircles;
	// Mesh-wide max BRep vertex tolerance (Chain Length's connectivity
	// check) - see OccEdgeData::vertexTolerance's doc comment
	// (BRepToAssimpConverter.h).
	double occEdgeVertexTolerance = 0.0;

	// Sparse per-triangle face axis data (Cylindrical/Conical Diameter
	// measurement tool) - independent of the edge data above. Parallel
	// arrays (occFaceTriangleIndices[i], occFaceIndexPerTriangle[i]) into
	// the SAVE-TIME mesh's own triangle order - re-derived by position
	// against this reload's own construction (see
	// SceneMesh::remapOccFaceTriangleIndicesByPosition()) rather than
	// trusted directly, since MVF reload's own SceneMesh construction may
	// reorder triangles differently than the save-time instance did.
	std::vector<int> occFaceTriangleIndices;
	std::vector<int> occFaceIndexPerTriangle;
	std::vector<OccFaceAxisInfo> occFaceAxes;

	// Source-mesh provenance for CurvatureAnalyzer's cross-body edge-weld
	// advisory (see MeshImportAdaptor::sourceMeshIds()'s doc comment) - one
	// id per SAVE-TIME vertex, in save-time order. Unlike occFaceTriangleIndices
	// above, no position-based re-derivation is needed: it's handed to this
	// reload's own SceneMesh constructor, which keeps it correctly aligned
	// through that construction's own optimizeMesh() reorder like any other
	// creation path. Empty for the vast majority of meshes.
	std::vector<quint64> sourceMeshIds;

	// SceneMesh::topologyRepaired() - Repair Mesh's intentional non-manifold vertex split marker.
	bool topologyRepaired = false;

	// Per-mesh user transform (gizmo TRS) preserved across MVF save/load.
	QVector3D   meshTranslation  = QVector3D(0.0f, 0.0f, 0.0f);
	QVector3D   meshRotation     = QVector3D(0.0f, 0.0f, 0.0f);
	QQuaternion meshRotationQuat = QQuaternion();
	QVector3D   meshScale        = QVector3D(1.0f, 1.0f, 1.0f);
	QMatrix4x4  sceneRenderTransform;
	bool        hasSceneRenderTransform = false;
};

class MvfMeshPreparationWorker
{
public:
	static QVector<PreparedMvfMesh> prepare(const Mvf::Document& document,
	                                        const QByteArray& geometryChunk,
	                                        const QByteArray& imageChunk);
};
