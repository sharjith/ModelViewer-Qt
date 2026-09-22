#pragma once

#include <atomic>
#include <vector>
#include "Drawable.h"
#include "BoundingSphere.h"
#include "BoundingBox.h"
#include "Material.h"
#include "GltfAnimationData.h"
#include "GltfVariantData.h"
#include "MeshInstanceState.h"
#include "MaterialVizState.h"
#include "MeshVertex.h"

#include "MeshGeometry.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QMap>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLVertexArrayObject>
#include <QQuaternion>
#include <QVariant>
#include <QVector3D>

class Triangle;

// Precomputed texture binding entry — built once per material change by
// SceneMesh::cacheTextureBindings(), consumed every frame by
// bindTexturesOptimized() without re-scanning the texture list.
struct PrecomputedTexture
{
    unsigned int textureId;
    unsigned int textureUnit;
    int          uniformLocation;
    bool         isValid;
};

class RenderableMesh : public Drawable
{
	Q_OBJECT
public:
	RenderableMesh(QOpenGLShaderProgram* prog, const QString name);

	virtual ~RenderableMesh();

	virtual void setProg(QOpenGLShaderProgram* prog);

	virtual RenderableMesh* clone() = 0;

	static void setCurrentRenderContext(const QMatrix4x4& globalModelMatrix,
	                                   const QMatrix4x4& viewMatrix);
	static void clearCurrentRenderContext();

	// Interaction-time geometry LOD - set once per frame (mirrors
	// setCurrentRenderContext's pattern) from ViewportWidget::setCommonUniforms(),
	// gated on the SAME lowResEnabled()+oversized-model condition that already
	// drives interactionFastPath's shader-cost cutback. render() reads this via
	// lodPolicyActive() at its glDrawElements call site to pick between the
	// full-resolution index buffer and a mesh's precomputed coarse LOD1 tier
	// (present only on meshes where the eligibility gate in SceneMesh::
	// optimizeMesh() allowed generating one - see _hasLod1 below).
	static void setLodPolicy(bool useLod1);
	static bool lodPolicyActive();
	static void bindTextureUnitCached(GLenum textureUnit, GLuint textureId);
	static void resetTextureBindingCacheForCurrentContext();
	static void bindProgramCached(QOpenGLShaderProgram* prog);
	static void notifyProgramBound(QOpenGLShaderProgram* prog);
	static void resetBoundProgramCacheForCurrentContext();
	static bool renderDiagnosticsEnabled();
	static void beginRenderDiagnosticsFrame(bool enabled);
	static void recordFrameCpuMs(double ms);
	static void recordOpaquePassCpuMs(double ms);
	static void recordTransparentPassCpuMs(double ms);
	static void recordFloorPassCpuMs(double ms);
	static void recordRenderMeshWithDisplayModeCpuMs(double ms);
	static void recordAssImpRenderCpuMs(double ms);
	static void recordProgramBindCall(bool actualBind);
	static void recordTextureBindCall(bool actualBind);
	static void recordVaoProgramReconfigure();
	static void recordMaterialUniformRefresh(bool explicitDirty);
	static void recordMaterialUniformReuse();
	static void recordMaterialRefreshReason(bool explicitDirty,
		bool shaderSwitch,
		bool signatureMismatch,
		bool debugOverridesBlockedReuse);
	static void recordMaterialDirtyBySetProg();
	static void recordTransformUniformUploads(int count);
	static void recordJointUniformUploads(int count);
	static void recordDrawCall(bool indexed, bool transparent);
	static void recordTextureCacheCpuMs(double ms);
	static void recordTransformUniformCpuMs(double ms);
	static void recordMaterialUniformCpuMs(double ms);
	static void recordTextureBindCpuMs(double ms);
	static void recordRenderStateCpuMs(double ms);
	static void recordDrawCpuMs(double ms);
	static void flushRenderDiagnostics();
	static quint64 currentRuntimeBoundsRevision();

	// Setter for primitive mode (from glTF)
	void setPrimitiveMode(GLenum mode)
	{
		_primitiveMode = mode;
		_instanceState.setBuildPickingTriangles(mode == GL_TRIANGLES);
	}

	GLenum getPrimitiveMode() const
	{
		return _primitiveMode;
	}

	virtual void render();
	virtual void renderShadow(); // Lightweight render for shadow mapping
	virtual void renderWireframeFast(QOpenGLShaderProgram* wireProg); // Lightweight render for wireframe/wireshaded passes
	virtual void renderFeatureEdgesFast(QOpenGLShaderProgram* wireProg) { Q_UNUSED(wireProg) } // Feature-edge render (subclass provides implementation)

	virtual void select()
	{
		_selected = true;
		_instanceState.setSelected(true);
		markUniformsDirty();
	}
	virtual void deselect()
	{
		_selected = false;
		_instanceState.setSelected(false);
		markUniformsDirty();
	}

	virtual BoundingSphere getBoundingSphere() const { return _instanceState.getBoundingSphere(); }
	virtual BoundingBox getBoundingBox() const { return _instanceState.getBoundingBox(); }
	QVector3D getStableTransformCenter() const;
	float getStableTransformRadius() const;

	virtual QOpenGLVertexArrayObject& getVAO();
	virtual QString getName() const
	{
		return _name;
	}

	virtual unsigned long long memorySize() const;

	// ---- Sub-object accessors (Phase 5b) ------------------------------------
	// Provide direct access to the component sub-objects so callers that need
	// more than the forwarding methods can reach the component without casting.
	// These are the building blocks for SceneMeshRecord (Phase 7).
	MeshInstanceState&        instanceState()        { return _instanceState; }
	const MeshInstanceState&  instanceState()  const { return _instanceState; }

	MaterialVizState&         materialState()        { return _materialState; }
	const MaterialVizState&   materialState()  const { return _materialState; }

	// -------------------------------------------------------------------------

	QVector3D ambientMaterial() const;
	void setAmbientMaterial(const QVector3D& ambient);

	QVector3D diffuseMaterial() const;
	void setDiffuseMaterial(const QVector3D& diffuse);

	QVector3D specularMaterial() const;
	void setSpecularMaterial(const QVector3D& specular);

	QVector3D emmissiveMaterial() const;
	void setEmmissiveMaterial(const QVector3D& emissive);

	float opacity() const;
	void setOpacity(const float& opacity);

	float shininess() const;
	void setShininess(const float& shininess);

	bool isMetallic() const;
	void setMetallic(bool metallic);

	void setPBRAlbedoColor(const float& r, const float& g, const float& b);
	void setPBRMetallic(const float& val);
	void setPBRRoughness(const float& val);

	float getHighestXValue() const;
	float getLowestXValue() const;
	float getHighestYValue() const;
	float getLowestYValue() const;
	float getHighestZValue() const;
	float getLowestZValue() const;
	QRect projectedRect(const QMatrix4x4& modelView, const QMatrix4x4& projection, const QRect& viewport, const QRect& window) const;

	QVector3D getTranslation() const;
	void setTranslation(const QVector3D& trans);

	QVector3D getRotation() const;
	void setRotation(const QVector3D& rota);
	QQuaternion getRotationQuaternion() const;
	void setRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler);

	QVector3D getScaling() const;
	void setScaling(const QVector3D& scale);

	QVector3D getExplodedViewTranslation() const;
	void setExplodedViewTranslation(const QVector3D& trans);

	QVector3D getExplodedViewRotation() const;
	void setExplodedViewRotation(const QVector3D& rota);
	QQuaternion getExplodedViewRotationQuaternion() const;
	void setExplodedViewRotationQuaternion(const QQuaternion& quat, const QVector3D& displayEuler);

	QVector3D getExplodedViewScaling() const;
	void setExplodedViewScaling(const QVector3D& scale);

	// Fast variants — update TRS and recompute the world-space bounding box from
	// the 8 local AABB corners only (O(1)), skipping the full O(N) vertex transform
	// that setTranslation/setRotation/setScaling do.  Use during interactive gizmo
	// drag; call fullUpdateRuntimeBounds() once on drag-commit to resync _trsfPoints.
	void setTranslationFast(const QVector3D& trans);
	void setRotationFast(const QVector3D& rota);
	void setRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler);
	void setScalingFast(const QVector3D& scale);
	void setExplodedViewTranslationFast(const QVector3D& trans);
	void setExplodedViewRotationFast(const QVector3D& rota);
	void setExplodedViewRotationQuaternionFast(const QQuaternion& quat, const QVector3D& displayEuler);
	void setExplodedViewScalingFast(const QVector3D& scale);
	void fullUpdateRuntimeBounds();  // force full O(N) rebuild after drag ends

	QMatrix4x4 getTransformation() const;
	QMatrix4x4 getExplodedViewTransformation() const;
	QMatrix4x4 getSceneRenderTransform() const;
	QMatrix4x4 combinedRenderTransform() const;

	// Explosion offset — world-space translation added by ExplodedViewManager.
	// Baked into combinedRenderTransform() so every render path (main + selection)
	// automatically draws and picks the mesh at the correct exploded position.
	void setExplosionOffset(const QVector3D& offset) { _instanceState.setExplosionOffset(offset); }
	QVector3D explosionOffset() const { return _instanceState.explosionOffset(); }

	std::vector<unsigned int> getIndices() const;
	std::vector<float> getPoints() const;
	std::vector<float> getNormals() const;
	std::vector<float> getTexCoords() const;
	const std::vector<float>& getTrsfPoints() const;
	const std::vector<float>& getTrsfNormals() const;
	const std::vector<float>& getTrsfTangents() const;
	const std::vector<float>& getTrsfBitangents() const;

	void resetTransformations();
	void resetExplodedViewTransformations();

	void    setSceneRenderTransform(const QMatrix4x4& trsf);
	void    setSceneRenderTransformFast(const QMatrix4x4& trsf);

	// -----------------------------------------------------------------------
	// KHR_materials_variants support
	// -----------------------------------------------------------------------

	// Per-primitive variant->material mappings (empty for non-variant meshes).
	void setVariantMappings(const QVector<GltfVariantMapping>& m) { _materialState.setVariantMappings(m); }
	const QVector<GltfVariantMapping>& variantMappings() const    { return _materialState.variantMappings(); }

	// Pre-built Material for every material index referenced by variants.
	// Includes the default material keyed by originalMaterialIndex.
	void setAllVariantMaterials(const QMap<int, Material>& mats) { _materialState.setAllVariantMaterials(mats); }
	const QMap<int, Material>& allVariantMaterials() const       { return _materialState.allVariantMaterials(); }

	bool hasVariants() const { return _materialState.hasVariants(); }

	// Return a pointer to the prebuilt Material for the given variant index,
	// or nullptr when this mesh has no mapping for that variant (keep default).
	// Pass variantIndex = -1 to retrieve the original/default material.
	const Material* materialForVariant(int variantIndex) const;
	virtual int getOriginalMaterialIndex() const { return -1; }

	void setActiveVariantIndex(int idx) { _instanceState.setActiveVariantIndex(idx); }
	int  activeVariantIndex() const     { return _instanceState.activeVariantIndex(); }

	virtual bool hasSkinning() const { return false; }
	virtual const QVector<QMatrix4x4>& jointPalette() const {
		static const QVector<QMatrix4x4> empty;
		return empty;
	}
	virtual bool hasMorphTargets() const { return false; }
	virtual QVector<float> defaultMorphWeights() const { return {}; }
	virtual const QVector<MorphTargetData>& getMorphTargets() const {
		static const QVector<MorphTargetData> empty;
		return empty;
	}
	virtual void applyMorphWeights(const QVector<float>&) { }
	virtual void resetMorphTargets() { }

	void setHasNegativeScale(bool hasNegativeScale)
	{
		_instanceState.setHasNegativeScale(hasNegativeScale);
	}
	bool hasNegativeScale() const { return _instanceState.hasNegativeScale(); }

	// Whether this mesh actually has a COLOR_0-style vertex color attribute
	// (feeds the "hasVertexColors" raster shader uniform - see
	// RenderableMesh.cpp:513/538/785). _colors/Vertex::Color otherwise holds
	// unspecified/garbage data, not an implicit white, so callers must gate
	// on this rather than assuming an absent attribute reads as (1,1,1,1).
	bool hasVertexColors() const { return _hasVertexColors; }

	// Returns a sort key based on primary texture IDs to minimise GPU texture
	// state changes when opaque meshes are sorted before drawing.
	uint64_t getTextureSortKey() const
	{
		// Combine albedo + normal as the two most commonly varying maps.
		// XOR-shift spreads bits to reduce collisions on small IDs.
		uint64_t a = _material.hasAlbedoMap() ? static_cast<uint64_t>(_material.albedoTextureId())
			: (_material.hasDiffuseMap() ? static_cast<uint64_t>(_material.diffuseTextureId()) : 0ULL);
		uint64_t n = _material.hasNormalMap() ? static_cast<uint64_t>(_material.normalTextureId()) : 0ULL;
		return (a << 32) ^ (n * 2654435761ULL);
	}

	virtual quint64 getRenderMaterialSortKey() const
	{
		return getTextureSortKey();
	}

	virtual bool intersectsWithRay(const QVector3D& rayPos, const QVector3D& rayDir, QVector3D& outIntersectionPoint);

	// Same closest-hit search, but also reports which triangle won and its
	// barycentric weights there - see MeshInstanceState::intersectsWithRayDetailed().
	bool intersectsWithRayDetailed(const QVector3D& rayPos, const QVector3D& rayDir,
		QVector3D& outIntersectionPoint, int& outTriangleIndex, QVector3D& outBarycentric)
	{
		return _instanceState.intersectsWithRayDetailed(rayPos, rayDir, outIntersectionPoint, outTriangleIndex, outBarycentric);
	}

	virtual void setAlbedoPBRMap(unsigned int albedoMap);
	virtual void setNormalPBRMap(unsigned int normalMap);
	virtual void setMetallicPBRMap(unsigned int metallicMap);
	virtual void setEmissivePBRMap(unsigned int emissiveMap);
	virtual void setRoughnessPBRMap(unsigned int roughnessMap);
	virtual void setAOPBRMap(unsigned int aoMap);
	virtual void setHeightPBRMap(unsigned int heightMap);
	virtual void setOpacityPBRMap(unsigned int opacityMap);
	virtual void invertOpacityPBRMap(bool invert);
	virtual void setSpecularGlossinessMap(unsigned int sgMap);

	virtual unsigned int getAlbedoPBRMap() const { return static_cast<unsigned int>(_material.albedoTextureId()); }
	virtual unsigned int getNormalPBRMap() const { return static_cast<unsigned int>(_material.normalTextureId()); }
	virtual unsigned int getMetallicPBRMap() const { return static_cast<unsigned int>(_material.metallicTextureId()); }
	virtual unsigned int getEmissivePBRMap() const { return static_cast<unsigned int>(_material.emissiveTextureId()); }
	virtual unsigned int getRoughnessPBRMap() const { return static_cast<unsigned int>(_material.roughnessTextureId()); }
	virtual unsigned int getAOPBRMap() const { return static_cast<unsigned int>(_material.occlusionTextureId()); }
	virtual unsigned int getHeightPBRMap() const { return static_cast<unsigned int>(_material.heightTextureId()); }
	virtual unsigned int getOpacityPBRMap() const { return static_cast<unsigned int>(_material.opacityTextureId()); }
	virtual unsigned int getTransmissionPBRMap() const { return static_cast<unsigned int>(_material.transmissionTextureId()); }
	virtual unsigned int getIORPBRMap() const { return static_cast<unsigned int>(_material.iorTextureId()); }
	virtual unsigned int getSheenColorPBRMap() const { return static_cast<unsigned int>(_material.sheenColorTextureId()); }
	virtual unsigned int getSheenRoughnessPBRMap() const { return static_cast<unsigned int>(_material.sheenRoughnessTextureId()); }
	virtual unsigned int getClearcoatPBRMap() const { return static_cast<unsigned int>(_material.clearcoatColorTextureId()); }
	virtual unsigned int getClearcoatRoughnessPBRMap() const { return static_cast<unsigned int>(_material.clearcoatRoughnessTextureId()); }
	virtual unsigned int getClearcoatNormalPBRMap() const { return static_cast<unsigned int>(_material.clearcoatNormalTextureId()); }


	virtual bool hasAlbedoPBRMap() const;
	virtual void enableAlbedoPBRMap(bool hasAlbedoMap);

	virtual bool hasMetallicPBRMap() const;
	virtual void enableMetallicPBRMap(bool hasMetallicMap);

	virtual bool hasEmissivePBRMap() const;
	virtual void enableEmissivePBRMap(bool hasEmissiveMap);
	 
	virtual bool hasRoughnessPBRMap() const;
	virtual void enableRoughnessPBRMap(bool hasRoughnessMap);
	
	virtual bool hasNormalPBRMap() const;
	virtual void enableNormalPBRMap(bool hasNormalMap);
	
	virtual bool hasAOPBRMap() const;
	virtual void enableAOPBRMap(bool hasAOMap);
	
	virtual bool hasHeightPBRMap() const;
	virtual void enableHeightPBRMap(bool hasHeightMap);
	
	virtual float getHeightPBRMapScale() const;
	virtual void setHeightPBRMapScale(float heightScale);
	
	virtual bool hasOpacityPBRMap() const;
	virtual void enableOpacityPBRMap(bool hasHeightMap);

	virtual bool hasTransmissionPBRMap() const;
	virtual void enableTransmissionPBRMap(bool hasTransmissionMap);
	virtual void setTransmissionPBRMap(unsigned int transmissionMap);

	virtual bool hasIORPBRMap() const;
	virtual void enableIORPBRMap(bool hasIORMap);
	virtual void setIORPBRMap(unsigned int iorMap);

	virtual bool hasSheenColorPBRMap() const;
	virtual void enableSheenColorPBRMap(bool hasSheenColorMap);
	virtual void setSheenColorPBRMap(unsigned int sheenColorMap);

	virtual bool hasSheenRoughnessPBRMap() const;
	virtual void enableSheenRoughnessPBRMap(bool hasSheenRoughnessMap);
	virtual void setSheenRoughnessPBRMap(unsigned int sheenRoughnessMap);

	virtual bool hasClearcoatPBRMap() const;
	virtual void enableClearcoatPBRMap(bool hasClearcoatMap);
	virtual void setClearcoatPBRMap(unsigned int clearcoatColorMap);

	virtual bool hasClearcoatRoughnessPBRMap() const;
	virtual void enableClearcoatRoughnessPBRMap(bool hasClearcoatRoughnessMap);
	virtual void setClearcoatRoughnessPBRMap(unsigned int clearcoatRoughnessMap);

	virtual bool hasClearcoatNormalPBRMap() const;
	virtual void enableClearcoatNormalPBRMap(bool hasClearcoatNormalMap);
	virtual void setClearcoatNormalPBRMap(unsigned int clearcoatNormalMap);

	virtual bool isTransparent() const;
	virtual bool needsDepthMaskOff() const ;
	
	virtual void clearAlbedoPBRMap();
	virtual void clearMetallicPBRMap();
	virtual void clearRoughnessPBRMap();
	virtual void clearNormalPBRMap();
	virtual void clearAOPBRMap();
	virtual void clearHeightPBRMap();
	virtual void clearOpacityPBRMap();
	virtual void clearTransmissionPBRMap();
	virtual void clearIORPBRMap();
	virtual void clearSheenColorPBRMap();
	virtual void clearSheenRoughnessPBRMap();
	virtual void clearClearcoatPBRMap();
	virtual void clearClearcoatRoughnessPBRMap();
	virtual void clearClearcoatNormalPBRMap();
	virtual void clearSpecularGlossinessMap();

	virtual void clearAllPBRMaps();
	
	virtual Material getMaterial() const;
	virtual void setMaterial(const Material& material);

	virtual void setTextureMaps(const Material& material);

	void cacheBaseVolumeProperties();
	void applyScaledVolumeProperties();
	
	virtual void setDiffuseADSMap(unsigned int diffuseTex);
	virtual void setSpecularADSMap(unsigned int specularTex);
	virtual void setEmissiveADSMap(unsigned int emissiveTex);
	virtual void setNormalADSMap(unsigned int normalTex);
	virtual void setHeightADSMap(unsigned int heightTex);
	virtual void invertOpacityADSMap(bool invert);
	virtual void setOpacityADSMap(unsigned int opacityTex);
	
	virtual void clearDiffuseADSMap();
	virtual void clearSpecularADSMap();
	virtual void clearEmissiveADSMap();
	virtual void clearNormalADSMap();
	virtual void clearHeightADSMap();
	virtual void clearOpacityADSMap();
	virtual void clearAllADSMaps();
	

	virtual void markTexturesDirty() { _textureBindingsDirty = true; }
	virtual void markUniformsDirty() { _uniformsDirty = true; }
	virtual void updateRuntimeBounds();

	virtual void deleteTextures();
	virtual void releaseContextBoundGpuResources();
	virtual void restoreContextBoundGpuResources(QOpenGLShaderProgram* prog);

	// ---- Debug texture overrides (TextureDebugPanel) -------------------------
	// Replace a texture unit with an alternative texture for the next draw call.
	// Pass replaceTex = 0 to bind "no texture" (black), or pass the ID of a
	// neutral 1×1 placeholder created by ViewportWidget.
	void setDebugTextureOverride(int unit, GLuint replaceTex);
	void clearDebugTextureOverride(int unit);
	void clearAllDebugTextureOverrides();

	// ---- Debug uniform overrides (TextureDebugPanel extension toggles) --------
	// Override a named scalar/vec3 uniform after setupUniforms() for one frame.
	// Supports float and QVector3D values.  Cleared automatically when the panel
	// re-enables the extension (which also sets markUniformsDirty so the shader
	// restores the original value on the very next frame).
	void setDebugUniformOverride(const QString& name, const QVariant& value);
	void clearDebugUniformOverride(const QString& name);
	void clearAllDebugUniformOverrides();

	// ---- Surface Analysis overlay (curvature/thickness/deviation heatmaps) ---
	// A TRANSIENT render override, deliberately separate from the real vertex-
	// color pipeline above (_colors/_colorBuffer/_hasVertexColors, which is
	// authored mesh data - e.g. genuine Point Set Reconstruction colors) and
	// from the debug-uniform-override map above (a general-purpose mechanism
	// other, unrelated code may also use). Writing analysis results into
	// either would violate the overlay's own contract: it never touches
	// authored data, and clearing it is a pure "stop overriding" with nothing
	// to restore - see SurfaceAnalysisOverlay's own doc comment for the full
	// lifecycle rationale (why this is NOT a snapshot/restore mechanism).
	//
	// rgba is one RGBA quadruplet per ORIGINAL vertex (same indexing as
	// _points/_colors) - a plain, shared-topology per-vertex layout, smoothly
	// interpolated across each triangle by the shader like any other vertex
	// attribute. Reused by every analysis mode that computes a genuinely
	// per-vertex (not per-face) scalar - curvature, thickness, deviation.
	// Mutually exclusive with setAnalysisOverlayFlatColors() below - calling
	// this clears any active flat overlay, and vice versa; a mesh shows at
	// most one Surface Analysis overlay, in one representation, at a time.
	//
	// Calling this uploads the buffer AND marks the overlay active (shown) -
	// there is no reason to upload data nobody is about to look at.
	// setAnalysisOverlayActive(false) is the cheap way to hide it again
	// without discarding the uploaded data (e.g. a dialog's own "Preview"
	// toggle); setAnalysisOverlayActive(true) re-shows it (whichever
	// representation - per-vertex or flat-per-face - currently has data
	// uploaded). clearAnalysisOverlay() is the definitive teardown - hides
	// AND releases both representations' buffers, used whenever the
	// analysis result itself is no longer valid (mesh edited, deleted, or
	// the analysis parameters/mode changed).
	void setAnalysisOverlayColors(const std::vector<float>& rgba);

	// Flat-per-face variant (Step 4's draft-angle mode is the first
	// consumer): a shared-vertex layout can only hold ONE color per vertex
	// slot, but that slot is incident to multiple triangles that may each
	// want a genuinely DIFFERENT flat value - averaging across them (what
	// setAnalysisOverlayColors() above would do) reintroduces exactly the
	// misleading-corner-reading problem a flat-shaded analysis exists to
	// avoid. rgbaPerFace is one RGBA quadruplet per TRIANGLE (indices.size()/3
	// entries, same face order as the mesh's own index buffer). Internally
	// duplicates position/normal/color into 3 unique vertices per triangle -
	// a small, separate, RENDER-ONLY VAO/buffer set, never written back into
	// _points/_normals/_indices or any authored/persisted geometry - so
	// ordinary smooth interpolation of 3 IDENTICAL corner colors naturally
	// produces a flat look, with no shader changes needed beyond what
	// setAnalysisOverlayColors() already established (same analysisColor
	// attribute/v_analysisColor varying/analysisOverlayActive uniform,
	// fed from a different, separate GPU buffer and draw call).
	void setAnalysisOverlayFlatColors(const std::vector<float>& rgbaPerFace);

	// Sub-triangle variant of the flat overlay, for an analysis that measures many points per triangle (wall
	// thickness): triangle t is drawn as a gridN[t] x gridN[t] grid of sub-triangles (see SubTriangleGrid.h for the
	// numbering and barycentric convention). `rgba` contains either one colour per sub-triangle, or three colours
	// per sub-triangle (one per corner) for a continuously interpolated field. gridN/offset must have one entry per
	// triangle of this mesh; a triangle with gridN 0 is not
	// overdrawn. Render-only, in mesh-local coordinates, sharing the flat overlay's VAO/buffers - mutually
	// exclusive with the other two overlay representations exactly as setAnalysisOverlayFlatColors() is.
	void setAnalysisOverlaySubTriangleColors(
		const std::vector<unsigned char>& gridN, const std::vector<unsigned int>& offset, const std::vector<float>& rgba);
	// Enables true fragment-level contour bands for a scalar-encoded overlay
	// buffer (R = normalized scalar, A = validity). Pass bands < 2 for the
	// ordinary pre-colored RGBA path. `colormap` uses AnalysisColormap's
	// integer values without introducing that UI-oriented dependency here.
	void setAnalysisOverlayBanding(int bands, int colormap);

	void setAnalysisOverlayActive(bool active);
	void clearAnalysisOverlay();
	bool hasAnalysisOverlay() const { return _hasAnalysisOverlay || _hasAnalysisFlatOverlay; }

	// Zebra-stripe reflection-line overlay (Surface Analysis's "Curvature"
	// panel, "Zebra Stripe" sub-mode - the curvature COLORMAP sub-mode is
	// separate, not-yet-built, see Step 6 of the plan). Unlike the analysis-
	// overlay paths above, this needs no per-vertex/per-face data at all -
	// it's a purely procedural, view-dependent shader effect computed live
	// from the mesh's own existing smoothly-interpolated normal
	// (reflect(viewDir, normal) sampled against a periodic pattern - see
	// main_scene.frag), so just a uniform toggle + frequency, no GPU buffer
	// upload. Deliberately its own first-class uniform state, same reasoning
	// as the analysis overlay above (never routed through the generic
	// debug-uniform-override map).
	void setZebraStripeActive(bool active, float frequencyStripesPerUnit = 10.0f);
	bool isZebraStripeActive() const { return _zebraStripeActive; }

	// Monotonically increasing, bumped by initBuffers()/uploadGeometry() -
	// i.e. any time this mesh's actual geometry (not just its transform or
	// material) changes. No equivalent counter existed anywhere else in this
	// codebase to reuse (checked RenderableMesh.h/SceneMesh.h/
	// MeshInstanceState.h - only a transient, already-cleared-after-use
	// _combinedRenderTransformDirty flag, not a comparable "has this changed
	// since I last looked" counter). Exists specifically for
	// SurfaceAnalysisOverlay's cache-key/invalidation check - see that
	// class's doc comment.
	quint64 geometryRevision() const { return _geometryRevision; }

protected: // methods
	// Upload geometry from a MeshGeometry object: copies vectors into _points/_normals/etc.
	// and calls initBuffers() to create GL buffers. Bounds are recomputed automatically.
	// Also syncs _sMax/_tMax/_hasVertexColors from the geometry.
	void uploadGeometry(const MeshGeometry& geom);

	virtual void initBuffers(
		std::vector<unsigned int>* indices,
		std::vector<float>* points,
		std::vector<float>* normals,
		std::vector<float>* colors = nullptr,
		std::vector<float>* texCoords = nullptr,
		std::vector<float>* tangents = nullptr,
		std::vector<float>* bitangents = nullptr,
		std::vector<float>* jointIndices = nullptr,
		std::vector<float>* jointWeights = nullptr
	);

    void computeBounds();
    void deleteBuffers();
	void recreateContextBoundBufferObjects();
	void recreateFallbackTexture();
	void fastUpdateWorldBounds();   // O(1) AABB update from 8 local corners
	void invalidateCombinedRenderTransformCache() const;
	static void markRuntimeBoundsChanged();

	virtual void setupTransformation();
	virtual void setupTextures();
	virtual void setupUniforms();
	int uniformLocationCached(const char* name) const;
	int uniformLocationCached(const QByteArray& name) const;
	int uniformLocationCached(const QString& name) const;
	void clearUniformLocationCache();
	static const QMatrix4x4& currentGlobalModelMatrix();
	static const QMatrix4x4& currentViewMatrix();

	// Rebinds debug-override textures after the normal texture setup.
	// Call at the end of any render() path that binds textures.
	void applyDebugTextureOverrides();

	// Re-sets debug-override uniforms after setupUniforms().
	// Must be called unconditionally every frame (not gated by _uniformsDirty).
	void applyDebugUniformOverrides();

protected:

	// Per-instance scene state: all transform layers, bounds, picking geometry.
	// Public API on SceneMesh forwards to this object.
	MeshInstanceState _instanceState;

	// ---- GL buffer objects (direct members, MeshVizAdaptor dissolved) -------
	QOpenGLBuffer _indexBuffer;
	QOpenGLBuffer _positionBuffer;
	QOpenGLBuffer _normalBuffer;
	QOpenGLBuffer _colorBuffer;
	// Dedicated Surface Analysis overlay buffer - see setAnalysisOverlayColors()'s
	// doc comment. NEVER aliased with _colorBuffer above.
	QOpenGLBuffer _analysisOverlayColorBuffer;

	// Flat-per-face Surface Analysis overlay - a small, entirely SEPARATE
	// VAO/buffer set (own position/normal/color, duplicated 3 unique
	// vertices per triangle) from _vertexArrayObject/_positionBuffer/
	// _normalBuffer/_analysisOverlayColorBuffer above - see
	// setAnalysisOverlayFlatColors()'s doc comment for why a shared-vertex
	// layout can't represent this. Drawn via glDrawArrays (no index buffer -
	// the duplicated layout has no sharing left to index), REPLACING the
	// normal draw call for a frame where it's active, not an extra pass on
	// top of it (same surface, same depth - drawing both would just
	// z-fight, and the overlay is meant to override normal appearance, not
	// layer under/over it).
	QOpenGLVertexArrayObject _analysisFlatVAO;
	QOpenGLBuffer _analysisFlatPositionBuffer { QOpenGLBuffer::VertexBuffer };
	QOpenGLBuffer _analysisFlatNormalBuffer   { QOpenGLBuffer::VertexBuffer };
	QOpenGLBuffer _analysisFlatColorBuffer    { QOpenGLBuffer::VertexBuffer };
	unsigned int  _analysisFlatVertexCount = 0;
	// Uploads already-expanded (3 vertices per drawn triangle) position/normal/colour arrays into the flat overlay's
	// buffers and switches it on - the shared tail of setAnalysisOverlayFlatColors()/...SubTriangleColors().
	void uploadAnalysisFlatBuffers(const std::vector<float>& positions, const std::vector<float>& normals, const std::vector<float>& colors);
	bool          _hasAnalysisFlatOverlay  = false;

	// See setZebraStripeActive()'s doc comment (public section).
	bool  _zebraStripeActive = false;
	float _zebraStripeFrequency = 10.0f;
	QOpenGLBuffer _texCoord0Buffer;
	QOpenGLBuffer _texCoord1Buffer;
	QOpenGLBuffer _texCoord2Buffer;
	QOpenGLBuffer _texCoord3Buffer;
	QOpenGLBuffer _tangentBuf;
	QOpenGLBuffer _bitangentBuf;
	QOpenGLBuffer _jointIndexBuffer;
	QOpenGLBuffer _jointWeightBuffer;
	QOpenGLBuffer _coordBuf;

	unsigned int               _nVerts = 0;
	QOpenGLVertexArrayObject   _vertexArrayObject;
	std::vector<QOpenGLBuffer> _buffers;

	MaterialVizState _materialState;
	Material& _material;   // reference alias into _materialState

	// ---- Fallback texture --------------------------------------------------
	QImage       _fallbackTextureImage;
	QImage       _fallbackTextureBuffer;
	unsigned int _fallbackTexture = 0;

	float _sMax = 1.0f;
	float _tMax = 1.0f;

	// ---- Per-frame dirty flags + uniform cache ----------------------------
	bool                           _textureBindingsDirty = true;
	bool                           _uniformsDirty        = true;
	mutable QHash<QByteArray, int> _uniformLocationCache;
	mutable QOpenGLShaderProgram*  _vaoConfiguredProgram = nullptr;

	static QMatrix4x4 _currentGlobalModelMatrix;
	static QMatrix4x4 _currentViewMatrix;
	static bool       _lodPolicyUseLod1;

	// ---- Debug override maps -----------------------------------------------
	QMap<int, GLuint>       _debugTextureOverrides;
	QMap<QString, QVariant> _debugUniformOverrides;

	// ---- Feature-edge VAO/VBO (heuristic silhouette edges) ----------------
	QOpenGLBuffer            _featureEdgeIndexBuffer { QOpenGLBuffer::IndexBuffer };
	QOpenGLVertexArrayObject _featureEdgeVAO;
	int                      _featureEdgeCount = 0;

	// ---- Interaction-time LOD1 (coarse) index buffer -----------------------
	// A second, independent EBO drawn against the SAME vertex buffer/VAO as
	// _indexBuffer - kept entirely separate from _indices/_indexBuffer (rather
	// than concatenated into one buffer) so every other consumer of _indices
	// (RtSceneBuilder::convertGeometry(), picking-triangle construction,
	// setMeshData()'s round trip) keeps seeing full-resolution geometry
	// unmodified. Only populated for eligible rigid meshes - see
	// SceneMesh::optimizeMesh()'s eligibility gate and SceneMesh::uploadLodTier().
	QOpenGLBuffer _lodIndexBuffer { QOpenGLBuffer::IndexBuffer };
	unsigned int  _nVertsLod1 = 0;
	bool          _hasLod1    = false;

	// ---- OCC B-Rep edge VAO/VBO (STEP/IGES/BREP exact topology) -----------
	QOpenGLBuffer            _occEdgeVertexBuffer { QOpenGLBuffer::VertexBuffer };
	QOpenGLVertexArrayObject _occEdgeVAO;
	int                      _occEdgeCount = 0;

	// ---- Precomputed texture binding table --------------------------------
	std::vector<PrecomputedTexture> _textureBindings;

	// ---- Uniform-state signature cache ------------------------------------
	mutable bool    _uniformStateSignatureDirty  = true;
	mutable quint64 _cachedUniformStateSignature = 0;

	std::vector<unsigned int> _indices;
	std::vector<float> _points;
	std::vector<float> _normals;
	std::vector<float> _colors;
	std::vector<float> _tangents;
	std::vector<float> _bitangents;
	std::vector<float> _texCoords;
	std::vector<float> _jointIndices;
	std::vector<float> _jointWeights;

	bool _hasVertexColors;

	// See geometryRevision()'s doc comment above (public section).
	quint64 _geometryRevision = 0;

	// CPU-side cache for the Surface Analysis overlay buffer above - kept
	// (rather than write-only to the GPU) so recreateContextBoundBufferObjects()
	// can re-upload it after a context loss, same reason _colors is kept
	// alongside _colorBuffer.
	std::vector<float> _analysisOverlayColors;
	bool _hasAnalysisOverlay = false;
	int _analysisOverlayBands = 0;
	int _analysisOverlayColormap = 0;

	// Primitive mode from glTF (GL_POINTS=0, GL_LINES=1, GL_LINE_STRIP=3, GL_TRIANGLE_STRIP=5, GL_TRIANGLES=4)
	GLenum _primitiveMode = GL_TRIANGLES;  // Default to triangles for backward compatibility

	unsigned long long _memorySize;
};

