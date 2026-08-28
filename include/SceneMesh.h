#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <initializer_list>

#include "RenderableMesh.h"
#include "MeshImportAdaptor.h"
#include "MeshAnimationState.h"
// Material, Vertex, MorphTargetData — transitive via RenderableMesh.h

class SceneMesh : public RenderableMesh
{
public:

	/*  Functions  */
	// Constructor
	SceneMesh(QOpenGLShaderProgram* shader, QString name, std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Material::Texture> textures, Material material, bool skipOptimization = false, GLenum primitiveMode = GL_TRIANGLES);
	~SceneMesh();
	virtual SceneMesh* clone();
	void setProg(QOpenGLShaderProgram* prog) override;
	void render();
	void renderWireframeFast(QOpenGLShaderProgram* wireProg) override;
	void renderFeatureEdgesFast(QOpenGLShaderProgram* wireProg) override;
	quint64 getRenderMaterialSortKey() const override;
	void markUniformsDirty() override;
	static void resetSharedUniformStateCache();

    std::vector<Vertex> vertices() const;

    // True unmorphed rest pose, snapshotted once at load (see applyMorphWeights()'s
    // own doc comment) - distinct from vertices(), which reflects the current
    // morph-blended (but not yet skinned) pose. Needed by RtSceneBuilder to feed
    // GPU morph-blending its bind-pose input.
    std::vector<Vertex> baseVertices() const;

    std::vector<unsigned int> indices() const;

    std::vector<Material::Texture> textures() const;

	void getMeshData(std::vector<Vertex>& vertices,
		std::vector<unsigned int>& indices) const;

	// Set new mesh data and upload to GPU (no optimization)
	void setMeshData(const std::vector<Vertex>& vertices,
		const std::vector<unsigned int>& indices,
		const std::vector<unsigned int>* sourceVertexMap = nullptr);
	void setMorphTargets(const QVector<MorphTargetData>& targets,
		const QVector<float>& defaultWeights);

	// Upload precomputed B-Rep edge segments from OCC (STEP/IGES/BREP).
	// edgeVerts is flat {x0,y0,z0, x1,y1,z1, ...}; bounds[i] = first vec3-index of
	// topological edge i (bounds.back() = total count, sentinel).
	// When set, renderFeatureEdgesFast() uses this buffer instead of the heuristic classifier.
	void setPrecomputedOccEdges(const std::vector<float>& edgeVerts,
	                            const std::vector<int>& bounds = {},
	                            const std::vector<OccEdgeCircleInfo>& circles = {},
	                            double vertexTolerance = 0.0);
	const std::vector<float>& getOccEdgeSegments()    const { return _importState.occEdgeSegments(); }
	const std::vector<int>&   getOccEdgeBoundaries()  const { return _importState.occEdgeBoundaries(); }
	// circles[i] describes the analytic circle for topological edge i (Edge
	// Radius measurement tool), 1:1 with getOccEdgeBoundaries()[i] - see
	// OccEdgeCircleInfo's doc comment (MeshImportAdaptor.h) for the frame it's in.
	const std::vector<OccEdgeCircleInfo>& getOccEdgeCircles() const { return _importState.occEdgeCircles(); }
	// Mesh-wide max BRep vertex tolerance (0.0 for non-OCC meshes) - see
	// MeshImportAdaptor::occEdgeVertexTolerance()'s doc comment.
	double getOccEdgeVertexTolerance() const { return _importState.occEdgeVertexTolerance(); }

	// Precomputed B-Rep per-face axis data (Cylindrical/Conical Diameter
	// measurement tool) - pure CPU data, no GL upload needed (unlike
	// setPrecomputedOccEdges()'s vertex buffer). Sparse (triangleIndices[i],
	// faceIndices[i]) parallel arrays, indices into THIS mesh's OWN current
	// triangle order - see MeshImportAdaptor::setOccFaceData()'s doc
	// comment for why a per-face contiguous range table doesn't work here
	// (optimizeMesh() reorders triangles). Invalidates the lazy triangle->
	// face lookup cache below so a later getOccTriangleFaceIndex() call
	// rebuilds it from the new data.
	void setPrecomputedOccFaceAxes(const std::vector<int>& triangleIndices,
	                               const std::vector<int>& faceIndices,
	                               const std::vector<OccFaceAxisInfo>& axes = {})
		{ _importState.setOccFaceData(triangleIndices, faceIndices, axes);
		  _occTriangleFaceLookupBuilt = false; _occTriangleFaceLookup.clear(); }
	// Raw sparse pass-through (MVF serialization, clone()'s own re-derivation
	// via remapOccFaceTriangleIndicesByPosition() below) - most callers
	// wanting "which face is triangle N on" should use
	// getOccTriangleFaceIndex() instead.
	const std::vector<int>& getOccFaceTriangleIndices() const { return _importState.occFaceTriangleIndices(); }
	const std::vector<int>& getOccFaceIndexPerTriangle() const { return _importState.occFaceIndexPerTriangle(); }
	// axes[i] describes the analytic axis for topological face i (see
	// OccFaceAxisInfo's doc comment) - look up i via getOccTriangleFaceIndex().
	const std::vector<OccFaceAxisInfo>& getOccFaceAxes() const { return _importState.occFaceAxes(); }
	// Returns the getOccFaceAxes() index for triangle `triangleIndex` in
	// THIS mesh's own current triangle order, or -1 if that triangle isn't
	// on a captured cylindrical/conical face. Lazily builds a hash-map from
	// the sparse arrays above on first call (same lazy-cache pattern as
	// getTriangleAdjacency()) rather than a linear scan per query, since
	// this is called from interactive picking/hover.
	int getOccTriangleFaceIndex(int triangleIndex) const;

	// Re-identifies which of THIS mesh's own CURRENT triangles correspond
	// to the (triangleIndices[i], faceIndices[i]) pairs captured against a
	// SOURCE mesh with the SAME vertex positions but possibly a DIFFERENT
	// triangle/vertex order - e.g. src is the pre-optimization geometry
	// this mesh was just built from (optimizeMesh() reorders during
	// construction), or a parent mesh being clone()'d (whose own
	// optimization pass may not reorder identically on the clone). Uses
	// exact position matching: optimizeMesh() only ever permutes/relabels
	// triangles and vertices, never recomputes their positions, so a
	// triangle's 3 vertex positions (sorted, so winding-order-independent)
	// are a reliable identity key across any such reordering. Static since
	// callers (AssImpMeshBuilder, SceneMesh::clone()) supply their own
	// "source" vertex/index arrays rather than an existing SceneMesh.
	static void remapOccFaceTriangleIndicesByPosition(
		const std::vector<Vertex>& srcVertices, const std::vector<unsigned int>& srcIndices,
		const std::vector<int>& srcTriangleIndices, const std::vector<int>& srcFaceIndices,
		SceneMesh* dst, std::vector<int>& outTriangleIndices, std::vector<int>& outFaceIndices);

	// Heuristic feature-edge list (dihedral-angle/seam-aware classifier,
	// see buildAndUploadFeatureEdges()) - flat pairs of indices into THIS
	// mesh's own vertex array (indices()/getTrsfPoints()), i.e. edge k runs
	// from vertex getFeatureEdgeIndices()[2k] to [2k+1]. Populated for every
	// mesh type (glTF/OBJ included, not just OCC-sourced ones) as part of
	// normal mesh setup - originally only consumed for GL-context-loss
	// recovery, now also the general-purpose (non-CAD) counterpart to
	// getOccEdgeSegments()/getOccEdgeBoundaries() for Edge Length/Edge-to-
	// Edge/Edge-to-Face/Edge-to-Vertex measurement picking (see
	// ViewportWidget::resolveMeasurementEdgeSegment()). Unlike the OCC
	// edges, there is no separate boundary/grouping table needed - each
	// pair already IS one discrete straight edge.
	const std::vector<uint32_t>& getFeatureEdgeIndices() const { return _featureEdgeIndices; }

	// Per-triangle neighbor list, one entry per triangle (indices()[3k..3k+2]),
	// giving the triangle index across each of its 3 edges in order
	// (edge e runs from local vertex e to (e+1)%3) - -1 where there's no
	// neighbor (a genuine mesh boundary) or the edge is non-manifold
	// (shared by more than 2 triangles - ignored past the first 2, same
	// tolerance-of-imperfect-input spirit as buildAndUploadFeatureEdges()).
	// Built lazily on first call (not eagerly like _featureEdgeIndices -
	// this is opt-in data, only needed by the Face Area measurement tool,
	// so building it for every loaded mesh whether or not that tool is
	// ever used would be wasted work) and cached forever after - safe
	// because mesh topology is static post-import (see this header's own
	// "v1: static meshes only" scope note elsewhere in this codebase).
	const std::vector<std::array<int, 3>>& getTriangleAdjacency() const;

	// Groups this mesh's triangles into spatially-connected islands via a
	// flood fill over getTriangleAdjacency()'s graph - e.g. a single OBJ/glTF
	// mesh containing several disjoint parts (a set of bolts merged into one
	// mesh on import) yields one group per part. Returns one entry per
	// island, each holding that island's triangle indices (into this mesh's
	// own current indices()/vertices() order); a fully-connected mesh
	// returns a single group covering every triangle. Used by
	// ModelViewer::splitSelectedMeshesByConnectivity() to decide whether
	// there's anything to split and to build each fragment's geometry.
	std::vector<std::vector<int>> findConnectedTriangleGroups() const;

	// Builds a new SceneMesh containing only the given triangles (deduped/
	// reindexed into a compact vertex list), with the same material,
	// textures, import provenance and full world transform as this mesh -
	// the split counterpart to clone(), used by ModelViewer::
	// splitSelectedMeshesByConnectivity() to build one fragment per island
	// from findConnectedTriangleGroups(). Deliberately does NOT copy morph
	// targets or precomputed OCC edge/face data (see the .cpp doc comment
	// for why - both are keyed to this mesh's FULL vertex/triangle index
	// space, which a triangle subset invalidates).
	SceneMesh* extractFragment(const std::vector<int>& triangleIndices, const QString& fragmentName) const;

	// The merge counterpart to extractFragment(): combines several separate
	// meshes into one, baking each input's CURRENT world-space position/
	// normal/tangent/bitangent (getTrsfPoints()/getTrsfNormals()/
	// getTrsfTangents()/getTrsfBitangents()) into the result, which is then
	// given an IDENTITY transform - the geometry itself already encodes
	// where each piece sat, so there's no single shared local frame to
	// reuse across inputs that may have had different transforms. Material/
	// textures/primitive mode are taken from meshes.first() - the caller
	// (ModelViewer::mergeSelectedMeshesByAdjacency()) is responsible for
	// only calling this on a set that's already been confirmed materially
	// compatible. Returns nullptr if meshes is empty.
	static SceneMesh* mergeMeshes(const QVector<SceneMesh*>& meshes, const QString& mergedName);

	// Computes a suggested alpha/offset pair for shrinkWrapMeshes() below,
	// from the combined world-space bounding-box diagonal of meshes (alpha
	// as 1/100 of the diagonal, offset as alpha/30 - middle of CGAL's own
	// documented 1/50-1/300 guidance for alpha). Callers (e.g. a dialog's
	// tolerance fields) can use this as a starting point and then let the
	// user override either value. Leaves outAlpha/outOffset at 0.0 if
	// meshes is empty or carries no geometry.
	static void suggestShrinkWrapTolerance(const QVector<SceneMesh*>& meshes, double& outAlpha, double& outOffset);

	// Combines the world-space geometry of one or more meshes into a single
	// new watertight, 2-manifold, intersection-free approximating shell via
	// CGAL's alpha_wrap_3, using the caller-supplied alpha/offset (see
	// suggestShrinkWrapTolerance() above for a reasonable starting point).
	// Unlike mergeMeshes(), the inputs need not share a material or even be
	// manifold/non-self-intersecting - alpha_wrap_3 tolerates arbitrary
	// input. The result carries no meaningful UVs or skinning (brand-new
	// geometry), so those Vertex fields are left at their defaults; only
	// material/texture/primitive-mode provenance is copied from
	// meshes.first(), same convention as mergeMeshes().
	// Returns nullptr if meshes is empty or the wrap produced no geometry.
	static SceneMesh* shrinkWrapMeshes(const QVector<SceneMesh*>& meshes, const QString& newName,
	                                    double alpha, double offset);

	// ---- Import provenance (moved from RenderableMesh) ----------------------
	MeshImportAdaptor&        importState()       { return _importState; }
	const MeshImportAdaptor&  importState() const { return _importState; }

	void setSceneIndex(int index)      { _importState.setSceneIndex(index); }
	int  getSceneIndex() const         { return _importState.sceneIndex(); }

	void setOriginalMaterialIndex(int index) { _importState.setOriginalMaterialIndex(index); }
	int  getOriginalMaterialIndex() const    { return _importState.originalMaterialIndex(); }

	void    setSourceFile(const QString& path)     { _importState.setSourceFile(path); }
	QString getSourceFile() const                  { return _importState.sourceFile(); }
	void    setSourceNodeName(const QString& name) { _importState.setSourceNodeName(name); }
	QString getSourceNodeName() const              { return _importState.sourceNodeName(); }

	void setSkinJoints(const QVector<GltfSkinJoint>& joints) { _importState.setSkinJoints(joints); }
	const QVector<GltfSkinJoint>& skinJoints() const         { return _importState.skinJoints(); }
	bool hasSkinning() const override                        { return _importState.hasSkinning(); }

	// ---- Animation state (moved from RenderableMesh) ------------------------
	MeshAnimationState&        animationState()       { return _animState; }
	const MeshAnimationState&  animationState() const { return _animState; }

	void setJointPalette(const QVector<QMatrix4x4>& palette) { _animState.setJointPalette(palette); }
	const QVector<QMatrix4x4>& jointPalette() const override  { return _animState.jointPalette(); }

	// ---- Morph-target overrides (fields live in SceneMesh until DeformableGeometry wiring) ----
	bool hasMorphTargets() const override { return !_morphTargets.isEmpty(); }
	QVector<float> defaultMorphWeights() const override { return _defaultMorphWeights; }
	const QVector<MorphTargetData>& getMorphTargets() const override { return _morphTargets; }

	void applyMorphWeights(const QVector<float>& weights) override;
	void resetMorphTargets() override;

	void setAlbedoPBRMap(unsigned int albedoMap) override;
	void setMetallicPBRMap(unsigned int metallicMap) override;
	void setEmissivePBRMap(unsigned int emissiveMap) override;
	void setRoughnessPBRMap(unsigned int roughnessMap) override;
	void setNormalPBRMap(unsigned int normalMap) override;
	void setAOPBRMap(unsigned int aoMap) override;
	void setHeightPBRMap(unsigned int heightMap) override;
	void setOpacityPBRMap(unsigned int opacityMap) override;
	void setIORPBRMap(unsigned int iorMap) override;
	void setClearcoatPBRMap(unsigned int clearcoatColorMap) override;
	void setClearcoatRoughnessPBRMap(unsigned int clearcoatRoughnessMap) override;
	void setClearcoatNormalPBRMap(unsigned int clearcoatNormalMap) override;
	void setSheenColorPBRMap(unsigned int sheenMap) override;
	void setSheenRoughnessPBRMap(unsigned int sheenRoughnessMap) override;
	void setTransmissionPBRMap(unsigned int transmissionMap) override;
	void clearAlbedoPBRMap() override;
	void clearMetallicPBRMap() override;
	void clearRoughnessPBRMap() override;
	void clearNormalPBRMap() override;
	void clearAOPBRMap() override;
	void clearHeightPBRMap() override;
	void clearOpacityPBRMap() override;
	void clearTransmissionPBRMap() override;
	void clearIORPBRMap() override;
	void clearSheenColorPBRMap() override;
	void clearSheenRoughnessPBRMap() override;
	void clearClearcoatPBRMap() override;
	void clearClearcoatRoughnessPBRMap() override;
	void clearClearcoatNormalPBRMap() override;
	void clearAllPBRMaps() override;

	// implementations for enabling/disabling textures
	void setDiffuseADSMap(unsigned int diffuseTex) override;
	void setSpecularADSMap(unsigned int specularTex) override;
	void setEmissiveADSMap(unsigned int emissiveTex) override;
	void setNormalADSMap(unsigned int normalTex) override;
	void setHeightADSMap(unsigned int heightTex) override;
	void setOpacityADSMap(unsigned int opacityTex) override;
	void clearDiffuseADSMap() override;
	void clearSpecularADSMap() override;
	void clearEmissiveADSMap() override;
	void clearNormalADSMap() override;
	void clearHeightADSMap() override;
	void clearOpacityADSMap() override;
	void clearAllADSMaps() override;

	virtual void setTextureMaps(const Material& material) override;
	void deleteTextures() override;
	void releaseContextBoundGpuResources() override;
	void restoreContextBoundGpuResources(QOpenGLShaderProgram* prog) override;
	void replaceOrAppendTexture(const std::string& type, GLuint id, bool hasAlpha);

	
private:
	void optimizeMesh();
	/*  Functions    */
	// Initializes all the buffer objects/arrays
	void setupMesh();
	void buildAndUploadFeatureEdges(float thresholdDegrees = 15.0f);
	// Quantizes each vertex position to a small epsilon and hash-maps it to
	// a canonical "welded" index - vertices at UV seams or hard-edge splits
	// share a 3D position but have different raw indices, so adjacency
	// (of edges OR triangles) has to be detected via this welded id, not
	// the raw one, or it'll miss real connectivity across those splits.
	// Shared by buildAndUploadFeatureEdges() (its own Step 1, extracted
	// here) and buildTriangleAdjacency() - the two need different per-edge
	// payloads (vertex normals vs. triangle indices) but the same weld.
	std::vector<uint32_t> buildPositionWeldMap() const;
	// Builds _triangleAdjacencyCache (mutable, safe to call from const
	// context) - see getTriangleAdjacency()'s doc comment. Split out from
	// the public accessor so the accessor itself stays a trivial "build
	// once, then return" wrapper.
	void buildTriangleAdjacency() const;
	// Uploads the coarse LOD1 index tier optimizeMesh() staged into
	// _pendingLod1Indices (if any) into RenderableMesh's _lodIndexBuffer.
	// Called from setupMesh() so it covers both the constructor path and
	// setMeshData()'s re-upload path - though setMeshData() doesn't call
	// optimizeMesh(), so _pendingLod1Indices is simply empty there and this
	// is a no-op (mesh stays LOD0-only, which is safe, just not optimized).
	void uploadLodTier();

	void cacheTextureBindings();
	void bindTexturesOptimized();
	void setRenderStateOptimized();
	void setupUniformsOptimized();
	void bindFeatureEdgeVertexState();
	quint64 uniformStateSignature() const;
	void removeTexturesByType(std::initializer_list<std::string> types);

	// sync texture path entries into _textures from the material's stored map (if _textures empty)
	void syncTexturesFromMaterialIfNeeded();
	// outImage receives the original, unflipped-as-loaded QImage (NOT the
	// vertically-mirrored copy used internally for the GL upload) so callers
	// can store it into Material::Texture::imageData in the same convention
	// the main import path (MaterialProcessor.cpp) uses - CPU-side consumers
	// (e.g. the path tracer) rely on that convention being consistent
	// regardless of which code path loaded a given texture.
	GLuint createGLTextureFromFile(const QString& path, bool& outHasAlpha, QImage& outImage);

protected:
	// ---- Import provenance + animation state (moved from RenderableMesh) --------
	MeshImportAdaptor  _importState;
	MeshAnimationState _animState;

	// ---- Interleaved CPU geometry (owned here until DeformableGeometry* composition) ---
	std::vector<Vertex> _vertices;
	std::vector<Vertex> _baseVertices;

	// ---- Interaction-time LOD1 hand-off ------------------------------------------
	// Staged by optimizeMesh() (CPU-side simplification, runs before GPU buffers
	// exist), consumed and cleared by uploadLodTier() (called from setupMesh(),
	// which creates GPU buffers). Empty whenever no LOD1 tier was generated
	// (skinned/morph/small/non-triangle meshes, or setMeshData()'s no-optimize path).
	std::vector<unsigned int> _pendingLod1Indices;
	std::vector<unsigned int> _lod1Indices;
	std::vector<uint32_t> _featureEdgeIndices;

	// ---- Lazy triangle-adjacency cache (see getTriangleAdjacency()) -------------
	mutable std::vector<std::array<int, 3>> _triangleAdjacencyCache;
	mutable bool _triangleAdjacencyCacheBuilt = false;

	// ---- Lazy triangle->face lookup cache (see getOccTriangleFaceIndex()) -------
	mutable std::unordered_map<int, int> _occTriangleFaceLookup;
	mutable bool _occTriangleFaceLookupBuilt = false;

	// ---- Morph-target data (static after load) ----------------------------------
	QVector<MorphTargetData> _morphTargets;
	QVector<float>           _defaultMorphWeights;

private:
	/*  Mesh Data  */
	// _indices → RenderableMesh (protected) — SceneMesh uses the inherited field directly
	// Reference alias into _materialState.textures() — same zero-churn pattern
	// as Material& _material in SceneMesh. Initialised in the constructor
	// init-list; all existing _textures.xxx call sites remain unchanged.
	std::vector<Material::Texture>& _textures;

	// Reference alias into _animState.currentMorphWeights() — avoids churn at call sites.
	QVector<float>& _currentMorphWeights;

	// State caching
	static QOpenGLShaderProgram* _currentUniformStateShader;
	static quint64 _currentUniformStateSignature;
	static bool _currentUniformStateHadDebugOverrides;
	static bool _currentBlendEnabled;
	static GLenum _currentFrontFace;
};
