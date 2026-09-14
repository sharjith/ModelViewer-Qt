#pragma once

#include <QMatrix4x4>
#include <QVariantMap>
#include <QHash>
#include <QList>
#include <vector>

#include "AnalysisColorRamp.h"

class SceneMesh;

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
		AnalysisColormap colormap);

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
		AnalysisColormap colormap);

	// Re-runs ONLY the color mapping (not the underlying geometry analysis)
	// against the already-cached scalar field for `mesh`, via whichever
	// representation (per-vertex or flat-per-face) it was originally applied
	// with - e.g. the dialog's legend range slider or colormap picker
	// changed, nothing about the mesh or analysis parameters themselves did.
	// No-op if this mesh has no cached result.
	void recolor(SceneMesh* mesh, float rangeMin, float rangeMax, AnalysisColormap colormap);

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
		// Which RenderableMesh upload path this result uses - see
		// applyResult() vs applyFlatResult()'s doc comments.
		bool isFlat = false;
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
