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

// A closed loop of connected feature edges (see SceneMesh::
// getFeatureEdgeIndices()) that passed a roundness check at detection time -
// the non-CAD counterpart to OccEdgeCircleInfo for the EdgeRadius/
// Concentricity measurement tools. Deliberately stores ONLY the topology
// (rest-pose vertex indices, in walk order around the loop), not a baked
// center/axis/radius - those are re-fit fresh from CURRENT world-space
// positions on every measurement query (MeasurementController::
// resolveMeasurementEdgeCircle()), same "live geometry, not frozen at
// detection time" convention every other measurement resolver follows.
struct DetectedCircularLoop
{
	std::vector<uint32_t> vertexIndices;
};

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

	// Closed, round loops traced out of getFeatureEdgeIndices() - the non-CAD
	// fallback source for the EdgeRadius/Concentricity measurement tools
	// (see SelectionManager::pickEdgeCircleAnchor()/MeasurementController::
	// resolveMeasurementEdgeCircle(), both of which only consult this when
	// getOccEdgeCircles() is empty). Built lazily on first call, same
	// build-once-cache-forever convention as getTriangleAdjacency() - this is
	// opt-in data most meshes will never need. A loop only appears here if it
	// passed a roundness check at build time (see buildDetectedCircularLoops()'s
	// doc comment) - non-circular closed loops (a rectangular cutout, a hex-
	// bolt-head outline) are deliberately excluded rather than reported as
	// bad circles.
	const std::vector<DetectedCircularLoop>& getDetectedCircularLoops() const;

	// Resolves an edgeIndex in the same space SelectionManager::pickStraightEdgeAnchor()/
	// SeamEdgeMark use (CAD: getOccEdgeBoundaries() index; non-CAD: getFeatureEdgeIndices()
	// pair index) to its two endpoints in WORLD space - mirrors MeasurementController::
	// resolveMeasurementEdgeGeometry()'s dual branch, but as a SceneMesh-owned helper so
	// SeamMarkingController (no MeasurementAnchorRef involved) can use it directly for overlay
	// drawing. Returns false if edgeIndex doesn't resolve (out of range for the mesh's current
	// data - e.g. stale after a topology-changing edit within the same dialog session).
	bool resolveEdgeMarkWorldEndpoints(int edgeIndex, QVector3D& outStart, QVector3D& outEnd) const;

	// Same resolution, but LOCAL (model) space - i.e. skips combinedRenderTransform() entirely,
	// rather than applying it and having the caller invert it back. UVGenerator's seam-welding
	// needs BIT-EXACT equality against its own vertices array (also untransformed local space,
	// see SceneMesh::getMeshData()) - a transform-then-inverse round trip does NOT guarantee that
	// (QMatrix4x4::inverted() introduces floating-point error even for a nominally-identity
	// transform), which silently broke every marked seam's position-weld lookup (confirmed via
	// diagnostic logging: the welded topoIndex pair printed identically to the map's own key at
	// qDebug's rounded display precision, yet find() still missed - a classic near-but-not-exact
	// float mismatch). Use this one for anything feeding into UVGenerator; use the WORLD variant
	// above only for on-screen overlay drawing.
	bool resolveEdgeMarkLocalEndpoints(int edgeIndex, glm::vec3& outStart, glm::vec3& outEnd) const;

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

	// Upgrade of mergeMeshes() above for the SAME call sites/contract (same
	// signature, same nullptr-if-empty convention, same material/provenance-
	// from-meshes.first() convention) - tries a real CGAL boolean union
	// (CGAL::Polygon_mesh_processing::corefine_and_compute_union) folded
	// pairwise across all of meshes, repairing each input first (duplicate/
	// degenerate-polygon cleanup, border stitching, self-intersection
	// removal, orientation) since corefinement requires watertight,
	// self-intersection-free, consistently-oriented input. If repair or
	// corefinement fails for ANY pair in the fold, abandons the whole
	// attempt and falls back to mergeMeshes()'s plain concatenation - never
	// worse than today's "Merge Selected", better whenever the geometry
	// allows a real solid union. See the plan/[[project_cgal_capabilities_reference]]
	// for why this is all-or-nothing rather than per-pair partial fallback.
	// outUsedRealUnion, if non-null, is set to true when a real CGAL union
	// was produced and false whenever the mergeMeshes() fallback ran instead
	// (at any of this function's several fallback points) - lets a caller
	// tell the user which actually happened rather than reporting a generic
	// "merged" message regardless of which path ran.
	static SceneMesh* booleanUnionMeshes(const QVector<SceneMesh*>& meshes, const QString& mergedName,
	                                      bool* outUsedRealUnion = nullptr);

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

	enum class SubdivisionMethod { Loop, CatmullClark };

	// Subdivides ONE mesh's world-space geometry `iterations` times via
	// CGAL's Subdivision_method_3 (Loop for triangle-preserving output,
	// Catmull-Clark for the classic subdivision-surface look - CC always
	// produces quads, even from a triangle input, so its result is
	// triangulated back via triangulate_faces() before conversion to this
	// app's triangle-only Vertex/index-buffer convention). Unlike
	// mergeMeshes()/shrinkWrapMeshes() above, this is single-mesh in,
	// single-mesh out - subdivision is topology-preserving refinement, not
	// a combine. When preserveSharpFeatures is true, interior edges with a
	// >= 30 degree dihedral are treated as infinitely sharp creases by both
	// the remesher and subdivision stencil; their normals are split too.
	// Disabling it retains the conventional fully-smooth limit surface.
	// Returns
	// nullptr if mesh is null/empty or the input can't be repaired into a
	// valid polygon mesh (repair_polygon_soup + is_polygon_soup_a_polygon_mesh
	// check, same gate booleanUnionMeshes() uses, but without the
	// closed/watertight requirements boolean union needs - subdivision
	// handles open borders fine).
	static SceneMesh* subdivideMesh(SceneMesh* mesh, SubdivisionMethod method,
	                                 unsigned int iterations, const QString& newName,
	                                 bool preserveSharpFeatures = true);

	// Computes a suggested grid-simplification spacing for
	// reconstructSurfaceFromPoints() below's optional pre-simplify step, from
	// the combined world-space bounding-box diagonal of meshes (diagonal /
	// 500 - dense enough to preserve real detail while still meaningfully
	// thinning a noisy/oversampled scan). Same "starting point the user can
	// override" convention as suggestShrinkWrapTolerance(). Leaves outSpacing
	// at 0.0 if meshes is empty or carries no geometry.
	static void suggestReconstructionSpacing(const QVector<SceneMesh*>& meshes, double& outSpacing);

	// Reconstructs a single new triangulated surface from the world-space
	// POSITIONS of one or more meshes (their own faces/indices, if any, are
	// ignored - only vertex positions feed the algorithm) via CGAL's
	// advancing_front_surface_reconstruction. Each source point's own
	// Vertex::Color (e.g. real per-point RGB already read from a
	// photogrammetry/laser-scan PLY's vertex colors, or white if the source
	// had none) is preserved on the corresponding output vertex via an
	// exact-position lookup - see the .cpp for why that stays valid across
	// this function's own repair/simplify steps specifically. Unlike
	// shrinkWrapMeshes(), no normals are required on the input and the
	// result is NOT guaranteed watertight or even manifold - this is a local
	// greedy heuristic over the point set's own Delaunay triangulation, not
	// alpha_wrap_3's shell construction. simplifySpacing > 0 runs CGAL::grid_simplify_point_set()
	// first (see suggestReconstructionSpacing() above), merging points closer
	// together than that distance - recommended for large/noisy real scans,
	// where it also substantially speeds up the reconstruction itself.
	// radiusRatioBound/beta are advancing_front_surface_reconstruction's own
	// tunables (CGAL defaults 5.0/0.52 rad respectively): radiusRatioBound
	// controls how large a gap the reconstruction may bridge (higher closes
	// more holes but risks bridging unrelated surfaces); beta is half the
	// wedge angle used to judge candidate-triangle plausibility (lower =
	// smoother/rounder result, higher = sharper edges preserved). Returns
	// nullptr if meshes is empty, the combined point count is under 4 (no
	// non-degenerate 3D Delaunay triangulation possible), the parameters
	// aren't finite/positive, or reconstruction produced no faces.
	static SceneMesh* reconstructSurfaceFromPoints(const QVector<SceneMesh*>& meshes,
	                                                const QString& newName,
	                                                double radiusRatioBound = 5.0,
	                                                double beta = 0.52,
	                                                double simplifySpacing = 0.0);

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
	// Builds _detectedCircularLoopsCache (mutable, safe to call from const
	// context) - see getDetectedCircularLoops()'s doc comment. Traces closed
	// loops out of _featureEdgeIndices (every loop vertex must have exactly
	// 2 feature-edge neighbors - a junction/branch point breaks the walk and
	// the attempt is abandoned, not force-closed), fits a plane+circle to
	// each candidate loop's REST-POSE positions via MeasurementGeometry::
	// fitPitchCircle() (same math already used for the Pitch Circle tool),
	// and keeps only loops whose fit is valid AND round enough (max radial
	// residual within tolerance of the fitted radius) - deliberately
	// classified once here against rest-pose geometry, exactly like OccEdgeCircleInfo's
	// own isCircle flag is a fixed, import-time classification never
	// revisited against later transforms.
	void buildDetectedCircularLoops() const;
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

	mutable std::vector<DetectedCircularLoop> _detectedCircularLoopsCache;
	mutable bool _detectedCircularLoopsCacheBuilt = false;

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
