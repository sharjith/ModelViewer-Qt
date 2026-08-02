#pragma once

#include <string>
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
	                            const std::vector<int>& bounds = {});
	const std::vector<float>& getOccEdgeSegments()    const { return _importState.occEdgeSegments(); }
	const std::vector<int>&   getOccEdgeBoundaries()  const { return _importState.occEdgeBoundaries(); }

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
	void replaceOrAppendTexture(const std::string& type, GLuint id, bool hasAlpha);

	
private:
	void optimizeMesh();
	/*  Functions    */
	// Initializes all the buffer objects/arrays
	void setupMesh();
	void buildAndUploadFeatureEdges(float thresholdDegrees = 15.0f);
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
