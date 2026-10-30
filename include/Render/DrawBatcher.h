#pragma once

// ---------------------------------------------------------------------------
// DrawBatcher
//
// Merges many small static meshes into a few large GPU buffers so the main opaque pass can draw them with one
// glMultiDrawElements per material group instead of one glDrawElements (plus state setup) per mesh. A very large
// CAD assembly (tens of thousands of parts) is bound by per-draw cost, not by triangles - see
// research/rendering/DRAW_BATCHING_DESIGN.md.
//
// Meshes are grouped by SceneMesh::drawGroupKey() (identical program-independent material, texture and render
// state) plus the vertex layout. Each part's own transform is baked into the group's vertices, so a group needs no
// per-mesh uniforms. Every frame each mesh is re-validated against what the batch was built from (geometry
// revision, group key, transform, selection, hover); anything that no longer matches is simply returned to the
// caller to draw individually exactly as before. A batch that has fallen behind is rebuilt in the background once
// the scene has stopped changing.
//
// Not batched (always drawn individually by the caller): selected or hovered meshes, transparent, skinned,
// morph-target and overlay meshes, negative or non-uniform scale, clipping passes, flat shading, non-shaded
// display modes. Everything GL-related must run on the thread that owns the viewport's current GL context.
// ---------------------------------------------------------------------------

#include <QOpenGLFunctions_4_5_Core>

#include <QElapsedTimer>
#include <QtGlobal>

#include <functional>
#include <unordered_map>
#include <vector>

class QOpenGLShaderProgram;
class SceneMesh;

class DrawBatcher : protected QOpenGLFunctions_4_5_Core
{
public:
	struct Settings
	{
		bool enabled = true;
		int  minDisplayedMeshes = 3000; // below this, batching is never used, so ordinary models are untouched
		int  minGroupSize = 6;          // smaller groups are not worth a buffer of their own
		bool fastValidation = true;     // skip re-checking meshes while nothing that affects batching has changed
		// Set by the caller, not read from QSettings: build the first batches in one go, before this frame is drawn, instead of
		// spread over many frames after it. Without it the first frame of a 40,000-part model draws every mesh individually
		// (12 s, mostly first-use state setup) and the batches only take over a few seconds later. For callers that know the
		// scene is complete (no import in progress).
		bool immediateInitialBuild = false;
	};
	using MeshLookup = std::function<SceneMesh*(int)>;
	using DrawOne = std::function<void(int)>;

	DrawBatcher();
	~DrawBatcher();

	// QSettings keys: batchedDrawing (bool), batchMinMeshes (int), batchMinGroupSize (int), batchFastValidation (bool).
	// Cached for one second.
	static Settings readSettings();

	// Frees every GL object. Needs the owning context to be current. Safe to call repeatedly.
	void releaseGl();

	// Drops all batches (GL objects included) and starts over on the next update().
	void invalidate();

	// Advances scanning and building by a small time slice. `visibleIds` are all displayed meshes, not only the
	// ones that survived culling this frame.
	void update(const std::vector<int>& visibleIds, const MeshLookup& meshAt, const Settings& settings);

	// Draws every mesh of `ids` that is currently covered by a valid batch and appends the rest, in order, to
	// `individual`. Returns the number of meshes drawn from batches. `prog` must be the pass's program.
	int drawBatches(QOpenGLShaderProgram* prog, const std::vector<int>& ids, int hoveredId,
	                const MeshLookup& meshAt, std::vector<int>& individual);

	// Transparent pass. `orderedIds` is the back-to-front draw order. Consecutive meshes of one batch group are drawn
	// with a single multi-draw in exactly that order, so the picture is identical to drawing them one by one; any mesh
	// the batches do not cover is passed to `drawIndividual` at its place in the order.
	void drawTransparentSequence(QOpenGLShaderProgram* prog, const std::vector<int>& orderedIds, int hoveredId,
	                             const MeshLookup& meshAt, const DrawOne& drawIndividual);

	bool hasBatches() const { return !_groups.empty(); }
	// Video memory taken by the merged buffers of all groups (diagnostics).
	qint64 gpuBytes() const { qint64 total = 0; for (const Group& g : _groups) total += g.bytes; return total; }

private:
	struct Entry
	{
		SceneMesh* mesh = nullptr;
		quint64 geometryRevision = 0;
		quint64 key = 0;
		float transform[16] = {};
		int group = -1;
		quint32 firstIndex = 0;
		quint32 indexCount = 0;
		quint32 lodFirst = 0;
		quint32 lodCount = 0;
		bool transparent = false;
		// Result of drawBatches() per-mesh check, valid while RenderableMesh::renderStateEpoch() == checkedEpoch.
		quint64 checkedEpoch = 0;
		bool cachedOk = false;        // batched, ignoring the hovered mesh
		bool cachedCandidate = false;
		bool cachedMissing = false;   // a candidate that is not (or no longer) in a batch
		bool cachedSelected = false;  // drawn in the group's selected pass
	};

	struct Group
	{
		unsigned vao = 0;
		unsigned positionVbo = 0;
		unsigned normalVbo = 0;
		unsigned colorVbo = 0;
		unsigned texCoordVbo = 0;
		unsigned tangentVbo = 0;
		unsigned bitangentVbo = 0;
		unsigned indexBuffer = 0;
		unsigned lodIndexBuffer = 0;
		QOpenGLShaderProgram* configuredProgram = nullptr;
		bool hasColors = false, hasTexCoords = false, hasTangents = false, hasBitangents = false;
		qint64 bytes = 0;

		// Per-frame scratch (reused, so drawing allocates nothing after the first frames).
		std::vector<int> counts, lodCounts;
		std::vector<const void*> offsets, lodOffsets;
		SceneMesh* frameRep = nullptr;
		qint64 frameTriangles = 0;
		// The selected meshes of the group, drawn after the others under their own uniforms (selected = true).
		std::vector<int> selCounts, selLodCounts;
		std::vector<const void*> selOffsets, selLodOffsets;
		SceneMesh* frameRepSelected = nullptr;
		qint64 frameTrianglesSelected = 0;
	};

	struct Spec
	{
		std::vector<int> ids;
		bool transparent = false;
		bool hasColors = false, hasTexCoords = false, hasTangents = false, hasBitangents = false;
	};

	void ensureGlFunctions();
	void startScan(const std::vector<int>& visibleIds);
	bool advanceScan(const MeshLookup& meshAt, const Settings& settings, qint64 budgetMicros);
	bool buildSpec(const Spec& spec, const MeshLookup& meshAt);
	void configureVao(Group& group, QOpenGLShaderProgram* prog);
	void destroyGroup(Group& group);

	enum class Phase { Idle, Scanning, Building };

	bool _glReady = false;
	Phase _phase = Phase::Idle;
	std::vector<Entry> _entries;   // indexed by mesh id
	std::vector<Group> _groups;

	// Scan / build state
	std::vector<int> _scanIds;
	size_t _scanPos = 0;
	std::unordered_map<quint64, Spec> _scanSpecs;
	std::vector<Spec> _buildQueue;
	size_t _buildPos = 0;
	qint64 _buildVertices = 0;
	qint64 _buildMeshes = 0;
	qint64 _buildBytes = 0;
	QElapsedTimer _buildClock;

	// Staleness tracking
	int _missingLastFrame = 0;
	int _candidatesLastFrame = 0;
	// Fast validation (see drawBatches()): meshes re-checked from scratch vs answered from the cache last frame, a rotating
	// slice that is re-checked anyway to catch a state change that did not bump the epoch, and what that found.
	int _fastHitsLastFrame = 0;
	int _slowChecksLastFrame = 0;
	// Why meshes were drawn individually in the last drawBatches() call (diagnostics, [DrawBatcher] state line).
	int _indHovered = 0, _indNotCandidate = 0, _indMissing = 0, _indLeftOut = 0;
	size_t _verifyCursor = 0;
	int _verifyMismatches = 0;
	bool _fastDisabled = false;
	bool _buildSynchronously = false; // the build in progress is the immediate initial one: no per-frame time budgets
	quint64 _lastRuntimeRevision = 0;
	int _stableFrames = 0;
	QElapsedTimer _sinceBuild;
	bool _enabledLastFrame = false;
	bool _needsInitialBuild = true;
	int _missingStreak = 0;          // consecutive frames with many meshes missing from the batches
	qint64 _cooldownMs = 1500;       // minimum time between builds; doubles while rebuilds keep following each other
	std::vector<int> _runCounts;
	std::vector<const void*> _runOffsets;
	QElapsedTimer _wantedSince;   // how long the model has been large enough to batch
	QElapsedTimer _updateClock;   // time between update() calls: the frame interval, which sizes the build slices
	QElapsedTimer _missingSince;  // how long many meshes have been continuously missing from the batches
	QElapsedTimer _stateLogClock;
};
