#pragma once

#include <fstream>
#include <iostream>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "SceneMesh.h"
#include "BoundingBox.h"
#include "MaterialProcessor.h"
#include "MeshAnalyzer.h"
#include "RenderableMesh.h"
#include "UVGenerator.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gp_Trsf.hxx>
#include <QFileInfo>
#include <QImage>
#include <QMatrix4x4>
#include <QString>
#include <Quantity_Color.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Tool.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <tuple>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_Location.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include "PunctualLights.h"
#include "GltfAnimationData.h"
#include "GltfCameraData.h"
#include "ThreeDXml/Document.h"
#include "Jt/JtImporter.h"
#include "GltfVariantData.h"


class AssImpModelProgressHandler : public QObject, public Assimp::ProgressHandler
{
	Q_OBJECT
public:
	void setCancelFlag(const bool* cancelFlag) { _cancelFlag = cancelFlag; }
	virtual bool Update(float percentage);

	// Required by Qt system. TODO: Make sure it is fine
	inline void* operator new(size_t, void* ptr) noexcept
	{
		return ptr;
	}

	using Assimp::ProgressHandler::operator new;

private:
	const bool* _cancelFlag = nullptr;

signals:
	void fileReadProcessed(float percent);
};

enum class UVMethod
{
	None,
	Planar,
	Cylindrical,
	Spherical,
	AngleBased,
	Hybrid,
	AngleBasedSmartUV,
	SmartProject,
	ARAP,
	Torus,
	LSCM
};

enum class SceneUpAxis
{
	ZUp,
	YUp
};
Q_DECLARE_METATYPE(SceneUpAxis)

struct SceneMeshInfo
{
	int totalVertices = 0;
	int totalTriangles = 0;
	int meshCount = 0;
	std::string largestMeshName;
	int largestMeshTriangles = 0;
	BoundingBox boundingBox;
	float maxDimension = 0.0f;
	float minDimension = 0.0f;
};


struct AssImpMeshData
{
	bool initiallyVisible = true;
	QString name;
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	std::vector<Material::Texture> textures;
	Material material;
	GLenum primitiveMode = GL_TRIANGLES;
	bool hasNegativeScale = false;
	// Index of this mesh in aiScene::mMeshes[] at load time.
	// Preserved so the exporter can match surviving meshes back to aiMesh
	// entries without relying on name strings.
	int sceneIndex = -1;
	// Material index from aiMesh::mMaterialIndex at import time.
	// This is the authoritative mapping of which material applies to this mesh,
	// avoiding fragile name-based matching during export.
	int originalMaterialIndex = -1;

	// Absolute path of the file this mesh was loaded from.
	QString sourceFile;
	QString sourceNodeName;
	bool preserveNodeTransform = false;
	QMatrix4x4 nodeWorldTransform;

	// KHR_materials_variants: which material index maps to which variants.
	// Empty when the source file has no variant extension.
	QVector<GltfVariantMapping> variantMappings;

	// Pre-built Material for every material index referenced by variants
	// (including the default material at key originalMaterialIndex).
	// Populated during processMesh() so variant switching requires no I/O.
	QMap<int, Material> allVariantMaterials;

	// Skinning support for animated glTF meshes.
	QVector<GltfSkinJoint> skinJoints;
	QVector<MorphTargetData> morphTargets;
	QVector<float> defaultMorphWeights;

	// Pre-computed B-Rep edge segments from OCC, flat {x0,y0,z0, x1,y1,z1, ...}
	// in model space.  Empty for OBJ/glTF; populated for STEP/IGES/BREP.
	// Importer-supplied per-vertex body ids (one per vertex, same order as `vertices`); empty when the importer
	// merged nothing. Passed to SceneMesh as its initial source-mesh ids - see MeshImportAdaptor::sourceMeshIds().
	std::vector<quint64> sourceMeshIds;

	// Set by AssImpModelLoader::optimizeBatch(): the vertices/indices above already went through SceneMesh::optimizeGeometry() on a
	// worker thread (sourceMeshIds permuted to match), so the viewer's SceneMesh constructor skips that pass and only uploads.
	bool geometryPrepared = false;
	std::vector<unsigned int> preparedLod1Indices;
	// Feature edges computed on a worker thread against the final vertices/indices (see SceneMesh::computeFeatureEdges()).
	bool featureEdgesPrepared = false;
	std::vector<uint32_t> preparedFeatureEdges;
	// OCC meshes: the cylinder/cone face table remapped onto the final (optimised) triangle order, computed on a worker thread
	// against the pre-optimisation positions (see SceneMesh::remapOccFaceTriangleIndicesFromArrays()).
	bool occFaceRemapPrepared = false;
	std::vector<int> preparedOccTriangleIndices, preparedOccFaceIndices;

	std::vector<float> precomputedOccEdges;
	// Boundary table: bounds[i] = first vec3-index of topological edge i; bounds.back() = total.
	std::vector<int>   precomputedOccEdgeBoundaries;
	// Analytic circle data for topological edge i (Edge Radius measurement
	// tool) - circles[i] corresponds 1:1 to precomputedOccEdgeBoundaries[i],
	// same order. Plain mirror of BRepToAssimpConverter::OccEdgeCircle so
	// this header doesn't need OCC's gp_Circ/BRepAdaptor_Curve includes.
	struct PrecomputedEdgeCircle
	{
		bool   isCircle = false;
		double centerX = 0.0, centerY = 0.0, centerZ = 0.0;
		double axisX = 0.0, axisY = 0.0, axisZ = 1.0;
		double radius = 0.0;
	};
	std::vector<PrecomputedEdgeCircle> precomputedOccEdgeCircles;
	// Mesh-wide max BRep vertex tolerance - mirrors
	// BRepToAssimpConverter::OccEdgeData::vertexTolerance's doc comment.
	double precomputedOccEdgeVertexTolerance = 0.0;

	// Boundary table: bounds[i] = first TRIANGLE-index of topological face
	// i; bounds.back() = total triangle count. Mirrors
	// precomputedOccEdgeBoundaries but at triangle-range granularity - see
	// BRepToAssimpConverter::OccFaceData's doc comment.
	std::vector<int> precomputedOccFaceTriangleBounds;
	// Analytic axis data for topological face i (Cylindrical/Conical
	// Diameter measurement tool) - axes[i] corresponds 1:1 to
	// precomputedOccFaceTriangleBounds[i]. Plain mirror of
	// BRepToAssimpConverter::OccFaceAxis so this header doesn't need OCC's
	// BRepAdaptor_Surface/gp_Cylinder/gp_Cone includes.
	struct PrecomputedFaceAxis
	{
		bool   isCylinder = false;
		bool   isCone = false;
		double originX = 0.0, originY = 0.0, originZ = 0.0;
		double axisX = 0.0, axisY = 0.0, axisZ = 1.0;
	};
	std::vector<PrecomputedFaceAxis> precomputedOccFaceAxes;
};

using AssImpMeshDataBatch = std::vector<AssImpMeshData>;

Q_DECLARE_METATYPE(AssImpMeshData)
Q_DECLARE_METATYPE(AssImpMeshDataBatch)

class AssImpModelLoader : public QObject
{
	Q_OBJECT
public:
	/*  Functions   */
	using UVDecisionFn = std::function<UVMethod(int totalTriangles, UVMethod currentMethod)>;

	// Constructor, expects a filepath to a 3D model.
	AssImpModelLoader();

	~AssImpModelLoader();

	/*  Functions   */
	// Loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
	void loadModel(std::string path, const bool& progressiveLoading = false);

	// Transfer after the worker has stopped; avoid copying every geometry buffer.
	AssImpMeshDataBatch takeMeshes() { return std::move(_meshes); }
	// Lightweight import results remain available even when progressive batches
	// have transferred their geometry directly to the viewer.
	int processedMeshCount() const { return _processedMeshCount; }
	bool preservesNodeTransforms() const { return _preservesNodeTransforms; }

	glm::mat4 getGlobalSceneTransform() const { return _appliedTransform; }

	QString getErrorMessage() const;

	void setUVGenerationMethod(const UVMethod& uvMethod) { _selectedUVMethod = uvMethod; }
	UVMethod getUVGenerationMethod() const { return _selectedUVMethod; }

	// userSeamEdges: optional local-space seam-edge position pairs (SeamMarkingController's marks,
	// resolved and filtered by ViewportWidget::generateUVsForMeshes()) - forwarded to
	// UVGenerator::generateAngleBased()/generateAngleBasedSmartUV()/generateARAP() only, the 3
	// methods that support it. Ignored (nullptr default) for every other method.
	bool regenerateUVs(SceneMesh* mesh, UVMethod method, const UVConfig& config,
		const std::vector<std::pair<glm::vec3, glm::vec3>>* userSeamEdges = nullptr);

	// Auto scale and orient the model to fit the scene's coordinate system
	void setAutoScaleActive(bool autoScale) { _autoScale = autoScale; }
	void setAutoOrientActive(bool autoOrient) { _autoOrient = autoOrient; }
	bool isAutoScaleActive()  const { return _autoScale; }
	bool isAutoOrientActive() const { return _autoOrient; }
	// True if a coordinate-system rotation was actually performed at load time.
	bool wasAutoOrientApplied() const { return _autoOrient && _autoOrientWasApplied; }
	// True if an auto-scale was actually performed at load time.
	bool wasAutoScaleApplied()  const { return _autoScale && std::abs(_appliedScale - 1.0f) > 0.001f; }
	SceneUpAxis sceneUpAxis() const { return _sceneUpAxis; }

	const aiScene* getScene() const { return _scene; }
	bool hasThreeDXmlScene() const { return bool(_threeDXml.scene); }
	// Warnings the native importers (3DXML, JT) reported for the file just read; empty for other formats.
	QStringList importWarnings() const
	{
		if (_sceneReleased)
			return _releasedWarnings;
		return _threeDXml.scene ? _threeDXml.warnings : _jt.scene ? QStringList(_jt.warnings + _jt.notes) : QStringList();
	}
	// Runs the vertex-cache/overdraw/LOD1/vertex-fetch optimisation (SceneMesh::optimizeGeometry) for every eligible mesh of the batch
	// on a pool of threads. It is pure CPU work on the meshes' own arrays, and was done one mesh at a time on the GUI thread inside
	// the SceneMesh constructor. Meshes that are skinned, morphed, not triangle lists or carry OCC face tables are left to the
	// constructor as before.
	static void optimizeBatch(std::vector<AssImpMeshData>& batch);

	// GUI side of the progressive hand-off: moves the oldest waiting batch out of the queue. Returns false when none is waiting.
	// The worker keeps converting the next batch while the viewer creates meshes for this one; it only waits when more than
	// kMaxQueuedBatches are already waiting, so memory stays bounded.
	bool takeReadyBatch(AssImpMeshDataBatch& out);

	// Hands the imported scene to the caller, so the viewer can adopt it instead of deep-copying every mesh: the native JT and
	// 3DXML scenes, the scenes built from OCC documents (STEP, IGES, BREP, VRML) and the scene Assimp's importer read (orphaned
	// from the importer, which then no longer frees it). Null when there is no scene to hand over, or after
	// MODELVIEWER_COPY_IMPORT_SCENE=1, which restores the deep copy. importWarnings() keeps working.
	aiScene* releaseNativeScene()
	{
		aiScene* scene = nullptr;
		if (_threeDXml.scene || _jt.scene)
		{
			_releasedWarnings = importWarnings();
			_sceneReleased = true;
			scene = _threeDXml.scene ? _threeDXml.scene.release() : _jt.scene.release();
			_scene = nullptr;
		}
		else if (_scene && qEnvironmentVariableIntValue("MODELVIEWER_COPY_IMPORT_SCENE") == 0)
		{
			if (_xcafScene && _scene == _xcafScene.get())
				scene = _xcafScene.release();
			else if (_scene == _importer.GetScene())
				scene = _importer.GetOrphanedScene();
			if (scene)
				_scene = nullptr;
		}
		return scene;
	}

	// Returns variant data parsed from KHR_materials_variants.
	// Empty when the loaded file has no variant extension.
	const GltfVariantData&    getVariantData()  const { return _variantData; }
	const GltfAnimationData&  getAnimationData() const { return _animationData; }
	const GltfCameraData&     getCameraData()    const { return _cameraData; }

	void freeScene();
	void setImageTextureUploader(MaterialProcessor::ImageTextureUploadFn uploader);
	void setKtx2TextureUploader(MaterialProcessor::Ktx2TextureUploadFn uploader);
	void setUVDecisionCallback(UVDecisionFn callback) { _uvDecisionCallback = std::move(callback); }

signals:
	void fileReadProcessed(float percent);
	void verticesProcessed(float percent);
	void nodeMeshProgressUpdated(int processedNodes, int totalNodes, int processedMeshes, int totalMeshes, bool uvProcessed);
	// A converted batch is waiting in the hand-off queue (see takeReadyBatch()). Carries no payload: queued connections copy their
	// arguments, and a batch is megabytes of vertices.
	void meshBatchAvailable();
	void sceneUpAxisDetected(SceneUpAxis sceneUpAxis, bool autoOrientCameraEnabled);
	void loadingFinished(bool successFlag, const aiScene* scene);
	void loadingCancelled();
	void lightsLoaded(const GltfLightData& lights);

public slots:
	void processFileReadProgress(float percentage);
	void cancelLoading();

private:
	void failProcessing(const char* reason);
	const char* _importStage = "idle";
	int _importSceneIndex = -1;
	// Processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
	void processNode(int nodeNum, aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform);

	AssImpMeshData processMesh(aiMesh* mesh, const aiScene* scene, const int& meshIndex, const int& totalMeshes, const aiMatrix4x4& transform, const char* nodeName);

	int countNodes(const aiNode* node) const;

	void generateUVsForMesh(MeshAnalysis::AnalysisResult& analysis, aiMesh* mesh, std::vector<Vertex>& vertices, std::vector<std::seed_seq::result_type>& indices);

	void GenerateFaceNormals(aiMesh* mesh, std::vector<glm::vec3>& generatedNormals);
	bool HasSurfaceGeometry(aiMesh* mesh);


	static SceneMeshInfo collectSceneMeshInfo(const aiScene* scene);
	
	// Automatic orientation and scaling of the model to fit the scene's coordinate system.
	void applyCoordinateSystemTransformations(const std::string& filePath);
	void applyTransformToNode(aiNode* node, const glm::mat4& transform);
	SceneUpAxis detectSceneUpAxis(const aiScene* scene, const std::string& filePath) const;
	glm::mat4 getCoordinateSystemTransform(const aiScene* scene, const std::string& filePath);
	glm::mat4 getCoordinateSystemFromFileType(const std::string& fileExtension) const;
	float calculateConditionalScale(const float& minDimension, const float& maxDimension);
	// Parse glTF primitive modes and store them in the map
	void parseGltfPrimitiveModes(const QString& gltfPath);

	// Parse KHR_materials_variants extension and populate _variantData.
	// Must be called after updateAiSceneWithGltfMaterials() so material indices
	// in the JSON correspond 1:1 to aiScene::mMaterials[] entries.
	void parseGltfVariants(const QString& gltfPath);
	void parseSceneAnimations();
	void parseSceneCameras();

	// Update aiScene materials to match glTF structure (deduplicate, fix material assignments)
	void updateAiSceneWithGltfMaterials(const QString& gltfPath, aiScene* scene);

private:
	std::string _path;
	/*  Model Data  */
	AssImpMeshDataBatch _meshes;
	std::string _texturePath;

	Assimp::Importer _importer;
	AssImpModelProgressHandler* _progHandler;
	QString _errorMessage;
	bool _loadingCancelled;
	
	AssImpMeshDataBatch _currentBatch;
	int _batchSize = 20;

	const aiScene* _scene = nullptr; // Holds the loaded scene
	void publishBatch(AssImpMeshDataBatch&& batch);
	// Progressive pipeline, in three stages on three threads: this (conversion) thread builds a batch and submitBatch()es it; a
	// preparation thread optimises it (optimizeBatch) and publishBatch()es it; the GUI thread takes it (takeReadyBatch). Each
	// hand-off is bounded, so memory stays bounded and a slow stage holds back the one before it. flushBatches() waits until
	// everything submitted has been published and stops the preparation thread; it must run before loadingFinished is emitted.
	void submitBatch(AssImpMeshDataBatch&& batch);
	void flushBatches();
	void prepareLoop();
	static constexpr size_t kMaxPrepBatches = 2;
	std::thread _prepThread;
	std::mutex _prepMutex;
	std::condition_variable _prepWake, _prepIdle;
	std::deque<AssImpMeshDataBatch> _prepQueue;
	bool _prepBusy = false, _prepStop = false;
	static constexpr size_t kMaxQueuedBatches = 2;
	std::mutex _batchMutex;
	std::condition_variable _batchSpace;
	std::deque<AssImpMeshDataBatch> _readyBatches;
	bool _sceneReleased = false;     // the native scene was handed to the viewer (releaseNativeScene())
	QStringList _releasedWarnings;
	ThreeDXml::ImportResult _threeDXml; // Owns custom aiScene independently of Assimp::Importer.
	std::unique_ptr<aiScene> _xcafScene; // Owns the scene an OCC document processor built (STEP, IGES, BREP, VRML).
	Jt::ImportResult _jt;
	
	SceneMeshInfo _sceneStats; // Holds statistics about the scene
	int _totalNodeCount = 0;
	int _totalMeshOccurrences = 0;
	int _processedNodeCount = 0;
	int _processedMeshCount = 0;
	bool _preservesNodeTransforms = false;

	bool _progressiveLoading = false; // If true, emit progress signals during loading

	MaterialProcessor _materialProcessor; 

	UVMethod _selectedUVMethod = UVMethod::Hybrid;
	bool _needsUVGeneration = false;

	bool _autoScale = true;            // Automatically scale the model to fit the scene's coordinate system
	bool _autoOrient = true;           // Automatically orient the model to match the scene's coordinate system
	bool _autoOrientWasApplied = false; // Set true when a non-identity orientation rotation was performed
	float _appliedScale = 1.0f;
	glm::mat4 _appliedTransform = glm::mat4(1.0f);
	SceneUpAxis _sceneUpAxis = SceneUpAxis::ZUp;

	// Map from mesh index to primitive mode (from glTF)
	std::unordered_map<unsigned int, GLenum> _gltfMeshPrimitiveModes;
	// -1 = not checked yet for this import, 0 = the glTF has no morph targets, 1 = it has some.
	int _gltfHasMorphTargets = -1;
	UVDecisionFn _uvDecisionCallback;

	// Map from aiMesh index to original material index (before deduplication)
	// Used to preserve true glTF material indices for correct export matching
	std::map<int, int> _meshIndexToOriginalMaterialIndex;

	// Map from Assimp compact material index → glTF material index.
	// Assimp loads materials in DFS traversal order (not glTF index order).
	// updateAiSceneWithGltfMaterials() builds this so processMesh() can convert
	// _meshIndexToOriginalMaterialIndex values (compact) into glTF-space indices
	// that are consistent with variant mapping materialIndex values.
	// Map from Assimp compact material index to glTF material index.
	// updateAiSceneWithGltfMaterials() rebuilds this from glTF primitive
	// provenance so processMesh() can convert pre-remap compact material slots
	// into authoritative glTF material indices. This keeps default materials,
	// variants, and export logic in the same index space even when Assimp merges
	// or reorders primitives.
	std::map<int, int> _aiMatToGltfMat;

	// Variant data parsed from KHR_materials_variants (empty for non-glTF files
	// or glTF files that do not carry the extension).
	GltfVariantData   _variantData;
	GltfAnimationData _animationData;
	GltfCameraData    _cameraData;
};
