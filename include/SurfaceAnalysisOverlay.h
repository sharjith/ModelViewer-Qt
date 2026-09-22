#pragma once

#include <QMatrix4x4>
#include <QVariantMap>
#include <QVector3D>
#include <QHash>
#include <QList>
#include <vector>

#include "AnalysisColorRamp.h"
#include "SubTriangleGrid.h"

class SceneMesh;

// Which analysis produced an overlay. Stored with each cached result so consumers (the hover readout's label)
// never have to guess it from HOW the result was uploaded - per-face vs per-vertex says nothing about the
// analysis: Draft Angle and Wall-Thickness are both per-face, and a readout that treated "per-face" as
// "Draft Angle" mislabelled every thickness value as degrees.
enum class AnalysisKind { Curvature, DraftAngle, WallThickness, Deviation };

// Tracks which meshes currently have an active Surface Analysis overlay
// (curvature/thickness/deviation heatmap - see RenderableMesh::
// setAnalysisOverlayColors()'s doc comment for the underlying rendering
// mechanism) and whether each one is still valid against the mesh's CURRENT
// state, so a caller can invalidate (clear) a stale result rather than ever
// showing one.
//
// Deliberately NOT a snapshot/restore mechanism - see
// RenderableMesh::clearAnalysisOverlay()'s doc comment for why the overlay
// itself never restores anything. This class only ever decides WHEN to call
// that clear, and holds the bookkeeping (cache key + scalar-field result)
// needed to make that decision and to support re-coloring an already-
// computed result (e.g. the dialog's legend range slider moving) without
// re-running the underlying geometry analysis.
//
// A caller that owns a live instance of this class (SurfaceAnalysisDialog) is
// responsible for actually invoking isValid()/clearOverlay() at the right
// moments. Deletion IS wired up (SurfaceAnalysisDialog connects to
// ViewportWidget::meshAboutToBeDeleted and calls clearOverlay() before the
// mesh is destroyed - see that signal's own doc comment). Geometry-edit/undo
// invalidation (via isValid()'s cache key) and rendering-mode-change
// invalidation are NOT yet wired to anything - isValid() currently has no
// caller, a known, disclosed gap (not a silent one): an in-flight overlay can
// go stale if the analyzed mesh's geometry changes, or its transform/
// reference-mesh state changes, while the dialog stays open. computeCurrentKey()/
// trackedMeshes()/storedKey() below exist specifically so a future idle-poll
// trigger (planned: comparing RenderableMesh::currentRuntimeBoundsRevision(),
// which already ticks on every transform change, against a last-seen value -
// see that revision counter's own doc comment - since no transform-changed
// signal exists anywhere on SceneMesh/RenderableMesh today) can be added as a
// pure caller-side addition, with no further changes needed here; that
// trigger is not wired up yet.
class SurfaceAnalysisOverlay
{
public:
	// Everything a cached result needs to answer "is this still valid" -
	// see the class doc comment. geometryRevision/transform come from
	// RenderableMesh::geometryRevision()/combinedRenderTransform();
	// parameters is analysis-mode-specific (ball radius, pull direction,
	// etc. - opaque to this class, just compared for equality).
	// referenceMesh/referenceGeometryRevision/referenceTransform are only
	// meaningful for a two-mesh analysis (deviation) - leave referenceMesh
	// null for every other mode.
	struct CacheKey
	{
		quint64 geometryRevision = 0;
		QMatrix4x4 transform;
		QVariantMap parameters;
		SceneMesh* referenceMesh = nullptr;
		quint64 referenceGeometryRevision = 0;
		QMatrix4x4 referenceTransform;

		bool operator==(const CacheKey& other) const
		{
			return geometryRevision == other.geometryRevision
				&& transform == other.transform
				&& parameters == other.parameters
				&& referenceMesh == other.referenceMesh
				&& referenceGeometryRevision == other.referenceGeometryRevision
				&& referenceTransform == other.referenceTransform;
		}
	};

	// Builds the CacheKey for `mesh`'s CURRENT live state (geometry revision +
	// transform, and the same for `referenceMesh` when given), with the
	// caller-supplied `parameters` copied straight through unexamined. This is
	// the one place that logic lives - extracted from what every
	// SurfaceAnalysisDialog Apply handler used to hand-write inline - reused
	// at three points: stamping a freshly-captured AnalysisMeshSnapshot's own
	// key (what a background worker's result is computed against), comparing
	// against that key once a worker result comes back (reject a stale
	// result rather than applying it), and periodically re-checking an
	// already-applied entry's key against isValid() while the dialog sits
	// idle (catches a later transform-only change - see isValid()'s own doc
	// comment on why that trigger doesn't exist yet without this).
	static CacheKey computeCurrentKey(SceneMesh* mesh, const QVariantMap& parameters, SceneMesh* referenceMesh = nullptr);

	// Applies a freshly-computed PER-VERTEX result (curvature/thickness/
	// deviation - one scalar per vertex, smoothly interpolated across each
	// triangle): see AnalysisColorRamp for the field -> color mapping this
	// then performs, uploaded via RenderableMesh::setAnalysisOverlayColors().
	// validPerSample (see AnalysisColorRamp::mapToRGBA()'s doc comment - pass
	// empty if every sample is valid) and the cache key this result was
	// computed against.
	void applyResult(
		SceneMesh* mesh,
		const std::vector<float>& scalarPerSample,
		const std::vector<bool>& validPerSample,
		const CacheKey& key,
		float rangeMin, float rangeMax,
		AnalysisColormap colormap,
		AnalysisKind kind,
		int discreteBands = 0);

	// Same as applyResult() above, but for a PER-FACE result (draft angle -
	// one scalar per TRIANGLE, uploaded via RenderableMesh::
	// setAnalysisOverlayFlatColors() instead - see that function's doc
	// comment for why draft angle specifically needs this representation).
	// scalarPerFace.size() must equal mesh's triangle count.
	void applyFlatResult(
		SceneMesh* mesh,
		const std::vector<float>& scalarPerFace,
		const std::vector<bool>& validPerFace,
		const CacheKey& key,
		float rangeMin, float rangeMax,
		AnalysisColormap colormap,
		AnalysisKind kind,
		int discreteBands = 0);

	// Same as applyFlatResult(), but each triangle is additionally drawn as an n x n grid of sub-triangles, each in
	// the colour of its OWN value (see SubTriangleGrid) - so a result measured at many points per triangle (wall
	// thickness) shows where within a large triangle the value changes, instead of one colour per triangle.
	// scalarPerFace/validPerFace are the per-triangle summary (used for statistics and as the fallback);
	// `refined` holds the per-sub-triangle values in the SAME units, NaN where a sample has no value, plus optional
	// reconciled corner values for a continuous display. Uploaded via RenderableMesh::setAnalysisOverlaySubTriangleColors().
	void applyRefinedResult(
		SceneMesh* mesh,
		const std::vector<float>& scalarPerFace,
		const std::vector<bool>& validPerFace,
		const SubTriangleField& refined,
		const CacheKey& key,
		float rangeMin, float rangeMax,
		AnalysisColormap colormap,
		AnalysisKind kind,
		int discreteBands = 0);

	// Re-runs ONLY the color mapping (not the underlying geometry analysis)
	// against the already-cached scalar field for `mesh`, via whichever
	// representation (per-vertex or flat-per-face) it was originally applied
	// with - e.g. the dialog's legend range slider or colormap picker
	// changed, nothing about the mesh or analysis parameters themselves did.
	// No-op if this mesh has no cached result.
	void recolor(SceneMesh* mesh, float rangeMin, float rangeMax, AnalysisColormap colormap, int discreteBands = 0);

	// True only if `mesh` currently has a cached result AND it was computed
	// against exactly `currentKey` (same geometry revision/transform/
	// parameters, and for deviation the same reference mesh state too). A
	// caller checks this before trusting a cached result is still worth
	// showing or reusing, rather than silently displaying a stale one.
	bool isValid(SceneMesh* mesh, const CacheKey& currentKey) const;

	// True if `mesh` currently has ANY cached overlay, regardless of
	// staleness - use isValid() instead when the question is "can I trust
	// this result", not just "is something currently showing".
	bool hasOverlay(SceneMesh* mesh) const;

	// Every mesh this instance currently has a cached overlay for - lets a
	// caller (the idle staleness poll) enumerate what to re-validate without
	// this class exposing its internal _entries storage directly.
	QList<SceneMesh*> trackedMeshes() const;

	// The CacheKey a mesh's cached overlay was originally computed against -
	// a default-constructed CacheKey if `mesh` has no cached entry. Lets a
	// caller re-derive computeCurrentKey()'s `parameters`/`referenceMesh`
	// arguments (the analysis-mode-specific parts isValid() alone can't
	// reconstruct) without needing to have kept its own copy around.
	CacheKey storedKey(SceneMesh* mesh) const;

	// Raw scalar value at one surface point, for a mouse-hover numeric
	// readout (see SurfaceAnalysisDialog::hoverReadoutText(), which owns
	// unit/mode formatting - this method only resolves the number). Draft
	// Angle (isFlat) indexes `scalarPerSample` directly by triangleIndex (one
	// value per triangle, in the SAME order as `mesh`'s index buffer, per
	// applyFlatResult()'s own doc comment); every other mode barycentric-
	// interpolates the triangle's 3 vertex samples (indices()[3*triangleIndex+0..2],
	// matching MeshSurfaceAnchor::triangleIndex/barycentric's own documented
	// convention). Returns false (leaving outValue/outIsFlat untouched) if
	// `mesh` has no cached overlay, triangleIndex is out of range, or any
	// sample involved is marked invalid.
	// outKind reports which analysis produced the value (see AnalysisKind) - use it, not the upload
	// representation, to label a readout.
	bool scalarAt(SceneMesh* mesh, int triangleIndex, const QVector3D& barycentric,
	              float& outValue, AnalysisKind& outKind) const;

	// The per-sub-triangle values of `mesh`'s cached overlay if it was applied via applyRefinedResult(); null
	// otherwise (no overlay, or a plain per-triangle/per-vertex one). Valid until the next apply/clear.
	const SubTriangleField* refinedFieldOf(SceneMesh* mesh) const;

	// The analysis kind of `mesh`'s cached overlay; false if it has none.
	bool kindOf(SceneMesh* mesh, AnalysisKind& outKind) const;

	// Copies of the cached scalar field (per triangle for a flat result, per vertex otherwise) and its validity
	// flags, so a caller can re-derive statistics or re-colour with different parameters without re-running the
	// analysis. False if `mesh` has no cached overlay.
	bool scalarField(SceneMesh* mesh, std::vector<float>& outValues, std::vector<bool>& outValid) const;

	// The exact displayed color at one surface point - scalarAt()'s raw
	// value, normalized against this mesh's own stored rangeMin/rangeMax and
	// mapped through its own stored colormap, the identical formula
	// AnalysisColorRamp::mapToRGBA() used to color the mesh in the first
	// place (so this always matches what the eye actually sees there, not
	// an approximation). For a mouse-hover readout that needs to pick a
	// legible text color against whatever the analysis heatmap painted
	// underneath it (see SurfaceAnalysisDialog::hoverReadoutText()). Same
	// false-on-no-overlay/out-of-range/invalid-sample conditions as
	// scalarAt().
	bool colorAt(SceneMesh* mesh, int triangleIndex, const QVector3D& barycentric,
	             QColor& outColor) const;

	// Definitive teardown for one mesh - calls SceneMesh::clearAnalysisOverlay()
	// and drops this class's own cached scalar-field data for it. MUST be
	// called before `mesh` is destroyed (see the Entry doc comment below) -
	// this class has no way to detect that on its own.
	void clearOverlay(SceneMesh* mesh);

	// Clears every mesh currently tracked, including actually turning off
	// each one's GL-side overlay (via SceneMesh::clearAnalysisOverlay(), not
	// just dropping this class's own bookkeeping) - e.g. when the whole
	// Surface Analysis dialog closes and any visible heatmap should
	// disappear immediately, not linger until some unrelated geometry
	// change happens to clear it. Safe to call even when the underlying
	// document/meshes are ALSO about to be torn down (e.g. switching
	// documents) - a mesh whose GL resources are being destroyed regardless
	// doesn't care whether this ran first.
	void clearAll();

private:
	struct Entry
	{
		// Also the map key (see _entries below) - kept here too purely for
		// readability at each use site. ONLY valid while this Entry exists:
		// a caller that deletes a SceneMesh must call clearOverlay(mesh)
		// first (the same "hook into whatever signal fires on mesh
		// deletion" responsibility this class's own doc comment already
		// places on whoever owns a live instance) - this class has no way
		// to detect a mesh being destroyed out from under it otherwise.
		SceneMesh* mesh = nullptr;
		CacheKey key;
		std::vector<float> scalarPerSample;
		std::vector<bool> validPerSample;
		float rangeMin = 0.0f;
		float rangeMax = 0.0f;
		AnalysisColormap colormap = AnalysisColormap::Sequential;
		int discreteBands = 0;
		// Which RenderableMesh upload path this result uses - see
		// applyResult() vs applyFlatResult()'s doc comments.
		bool isFlat = false;
		AnalysisKind kind = AnalysisKind::Curvature;
		// Non-empty only for a result applied via applyRefinedResult() (always also isFlat): the per-sub-triangle
		// values the display and the hover readout use in place of the per-triangle scalar.
		SubTriangleField refined;
	};

	// Keyed directly by SceneMesh* - unlike Scene States/Selection Sets
	// (which persist across sessions and genuinely need UUID identity to
	// survive serialization), this cache is purely in-memory and session-
	// only, and SceneMesh itself exposes no UUID of its own anyway (mesh
	// identity for lookup purposes lives on ViewportWidget - getUuidByIndex()/
	// getIndexByUuid()/getMeshByUuid() - not on SceneMesh). Pointer identity
	// is exactly as valid here as it already is for Entry::mesh above.
	QHash<SceneMesh*, Entry> _entries;
};
