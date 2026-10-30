# Draw batching for very large assemblies: design proposal

Status: phase 2 and part of phase 3 implemented 2026-10-29/30 (built, not yet run on production data; see section 8). Sections are labelled
**Observation** (measured or read in the source), **Estimate** (reasoned, unmeasured) and **Decision needed**.
Companion: [LARGE_ASSEMBLY_VIEWING_OPTIONS.md](LARGE_ASSEMBLY_VIEWING_OPTIONS.md) (option 5, draw-call reduction).

## 1. Why this is the next step

### Observations (frame profiler, synthetic 40k-part model, 15.96 M triangles, release build, 2026-10-29)

| Quantity | Value | Note |
|---|---|---|
| Meshes drawn | 40,000 | one `glDrawElements` each |
| CPU per frame | about 192 ms | culling 24 ms, `SceneMesh::render` 129 ms, remainder is the pass loop and per-mesh wrapper |
| GPU per frame | about 167-175 ms | GL timer query, read one frame late |
| CPU per mesh | about 4.8 us | no single dominant slice (see 1.1) |
| GPU per mesh | about 4.2 us | same whether the window is full size or a quarter of it |
| Zoomed-in frames | cost falls in proportion to meshes drawn | about 3.4-4.6 us GPU per mesh at 5-6 k meshes |

The quarter-size window test (owner, 2026-10-29) left GPU time unchanged, so the frame is not limited by shaded
pixels. Together with the flat per-mesh GPU cost this points at per-draw overhead (state validation, per-draw
uniforms), not triangle or fill load. Qualification: one synthetic model, one GPU, interaction fast path state not
recorded in the log line.

### 1.1 CPU slices of `SceneMesh::render` (ms per frame at 40,000 meshes, probe timers included)

| Slice | ms |
|---|---|
| wireframe scope, VAO bind, per-draw GL state | 36 |
| element-buffer rebind, draw, VAO release | 34 |
| transform uniforms (after the location-caching change) | 15.6 |
| render state | 8.1 |
| material signature check | 6.9 |
| matrix prep, program bind | 6.6 |
| analysis overlay, debug overrides | 4.4 |
| texture binding cache | 4.1 |
| texture binds | 2.5 |

Trimming these individually is worth perhaps 15-20 ms of 192 ms. The cost is the draw count, not one slow step.

## 2. What already helps batching (Observations, from the source)

- `SceneMesh::uniformStateSignature()` hashes the primitive mode, vertex-colour flag, negative scale, selection
  state, and every material value and texture id. Consecutive meshes with equal signatures already skip the
  material uniform upload (material reuse was about 99% in the logs). Meshes with equal signatures therefore need
  no per-mesh material state, which is exactly the grouping a batch needs.
- Per mesh, the values that still change between equal-signature meshes are the four transform uniforms
  (`modelMatrix`, `modelViewMatrix`, `normalMatrix`, `worldNormalMatrix`), `hasSkinning`/`jointCount`, and the
  selected/hovered flags.
- `RenderableMesh::setDrawRanges()` and the `glMultiDrawArrays` path already exist for one mesh, so multi-range
  drawing is not new to the renderer.
- The context is OpenGL 4.5 core. `glMultiDrawElements` and `glMultiDrawElementsIndirect` are available.
  `gl_DrawID` needs GL 4.6 or `ARB_shader_draw_parameters`; this design does not depend on it (section 3).
- Picking and measurement use CPU-side triangles per mesh, not an ID buffer, so they are independent of how
  meshes are drawn. This must be re-confirmed per tool during implementation.

## 3. Proposed approach: signature-grouped static batches

**Idea (hypothesis, not yet built):** after load, group display meshes by uniform-state signature. For each group
that is large enough, build one merged vertex buffer and one merged index buffer with each part's
`combinedRenderTransform` baked into its vertices. Draw a group with a single `glMultiDrawElements`, passing one
(count, offset) range per visible part. The shader needs no change: the batch uses the same program, the same
material uniforms (set once per group), and the global model matrix.

Why baked transforms: the per-mesh transform uniforms disappear, and the CPU culling result still selects
individual parts, because each part keeps its own index range. Hiding, frustum culling and small-part culling
remove ranges from the list instead of removing draws.

### 3.1 Which meshes are batched

Batched (static set): opaque, rigid, triangle meshes with no skinning or morph targets, no analysis overlay, no
material wireframe, no per-mesh debug overrides, not selected, not hovered, not part of an active animation,
exploded-view or gizmo edit.

Drawn individually as today (dynamic set): everything else. A part leaves its batch by being skipped in the range
list (cheap) and drawn from its own buffers.

### 3.2 Scenarios that must keep working (to be tested, one by one)

| Scenario | Handling |
|---|---|
| PBR and ADS modes | same program, same uniforms; batch is only a different way to supply geometry |
| Flat shading program (`fgFlatShader`) | batch must follow the mesh's program; group key includes the program |
| glTF/GLB, glTF extensions | unchanged material path; skinned and morphed meshes stay dynamic; texture ids are part of the signature |
| 3DXML, JT, STEP, IGES, BREP, OBJ, FBX | all arrive as `SceneMesh`; same rules; STEP-style edges and feature-edge overlays are separate draws and stay per mesh |
| Transparency and blending | transparent meshes stay dynamic (draw order matters); first version batches opaque only |
| Selection and hover highlight | the selected or hovered mesh drops out of the batch and is drawn alone with its highlight uniforms |
| Transform edits, undo/redo, animation, exploded view | affected meshes are marked dynamic while being edited; the batch is invalidated when a transform changes and rebuilt in the background |
| Hide/show, isolate, delete, purge, paste | range list rebuilt from visible parts; structural changes invalidate affected groups only |
| Clipping planes, box clip, section capping | first version draws individually whenever any clip mode is active; revisit later |
| Shadow pass | first version unchanged (per-mesh); a later phase can reuse the batches |
| Interaction LOD1 | merged LOD index buffer built per batch, or LOD parts left dynamic in version 1 |
| Surface-analysis overlays, zebra, debug texture panel | dynamic set while active |
| Path tracer (CPU, GPU, OptiX) | not affected: it builds its own scene data and does not call `SceneMesh::render()` (no matches under `src/RayTracing`); verify it never reads the merged buffers |
| Multi-viewport, compare, context loss | batches are per GL context; must be released and rebuilt on context restore (existing GPU registry pattern) |

### 3.3 Cost and risk

- **GPU memory (Estimate):** keeping both the per-mesh buffers and the merged buffers roughly doubles the vertex
  memory of batched parts. For the 16 M triangle synthetic model that is a few hundred MB. Mitigation: batch only
  above a mesh-count threshold (for example more than 5,000 draws), so small models see no change; optionally
  evict per-mesh buffers later and re-upload on demand.
- **Build time (Estimate):** a one-off pass over all vertices after load, on the prep thread; not on the first
  frame. It must not block loading, and it must be cancelable.
- **Correctness risks:** stale batches after a transform or material change; normal-matrix handling with negative
  scale (the front-face logic keys off per-mesh scale, so negative-scale parts stay dynamic); precision of
  baked transforms for very large coordinates (bake relative to the group origin if needed).
- **Material diversity (open):** the gain depends on how many distinct signatures the real vehicle has. Parts in a
  production EBOM may each carry a unique colour. If the signature count is close to the part count, batching
  by signature helps little and a different design (per-draw data in a buffer) would be needed.

## 4. Expected gain (Estimate, unmeasured)

If the production assembly collapses to a few hundred signature groups, the per-mesh CPU work (about 130 ms of the
192 ms) is replaced by a few hundred draws plus building the range list (low single-digit ms). GPU time should
move from per-draw dominated toward triangle dominated; 16 M triangles on this card is expected to take tens of
milliseconds, not 170. These numbers are reasoning, not measurements. The first gate below replaces them with
data.

## 5. Phases and gates

1. **Measure signature diversity.** Add a diagnostic that counts distinct uniform-state signatures among the
   displayed meshes (and how many are batch-eligible by section 3.1). Run on the synthetic model and the
   production vehicle. *Gate: owner reviews the counts; batching proceeds only if groups are far fewer than parts.*
2. **Prototype on the opaque shaded pass only**, behind a QSettings switch and the mesh-count threshold, with clip
   modes, transparency and shadows falling back to the current path. *Gate: owner builds and compares frame
   times and appearance on the synthetic model and a real glTF.*
3. **Dynamic-set handling** (selection, hover, edits, animation, hide/show) with the scenario table in 3.2 as the
   test list. *Gate: owner verification per scenario.*
4. **Extend** to LOD1, the shadow pass, and clipping if the numbers justify it.

Nothing is committed until the owner has tested; phases 2-4 do not start without sign-off on the previous gate.

## 6. Decisions needed

1. Proceed with phase 1 (the signature-count diagnostic), which changes no rendering?
2. Accept the memory trade-off in 3.3 (duplicate vertex data above a mesh-count threshold)?
3. Is a smaller first version acceptable (opaque, shaded, no clipping, no shadows) for the prototype?

## 7. Evidence and how to regenerate it

- Frame profiler output: Settings, profile rendering checkbox, then `[RenderDiagnostics]` lines (including
  `probesMs/frame`) in the newest `modelviewer_*.log` under the application log folder.
- Synthetic model:
  `python tools/perf/make_synthetic_assembly.py --meshes 40000 --tri-min 20 --tri-max 2000 --seed 2 --out out/perf/synthetic_40k_heavy_fixed.glb`
- Not yet measured on the production vehicle with the probe timers; production numbers so far come from the
  owner's earlier runs (about 12-34 k meshes drawn, 280-860 ms per frame).

## 8. Implementation status (2026-10-30)

Built (release, compiles and links); **not yet exercised in the running viewer**. The owner tests on production data.

### 8.1 What exists
- `DrawBatcher` (`include/Render/DrawBatcher.h`, `src/Render/DrawBatcher.cpp`), owned lazily by `ViewportWidget`.
  Scans the displayed meshes (4 ms per frame), groups them by `SceneMesh::drawGroupKey()` plus vertex layout, bakes each
  part's `combinedRenderTransform()` into merged VBOs (1 M vertices per chunk, 6 ms build budget per frame), and draws
  each group with `glMultiDrawElements`. The interaction LOD1 tier is read back from the per-mesh LOD index buffers
  and batched too, so the fast-path triangle reduction is preserved.
- Per frame every mesh is re-validated against what its batch was built from: same `SceneMesh*`, same
  `geometryRevision()` (now a process-wide counter, so a deleted mesh at the same address cannot match), same group
  key, same transform (bitwise), still a batch candidate, not hovered. Anything that fails is returned and drawn by the
  unchanged per-mesh loop.
- Rebuild: when more than max(200, 5%) of the drawn candidates have no valid entry and the scene's runtime-bounds
  revision has been unchanged for 20 frames (at least 1.5 s since the last build). Meshes that were deliberately left
  out are remembered, so they do not trigger rebuilds.
- Material, texture and render state for a group come from one representative mesh via
  `SceneMesh::prepareBatchDraw()` (a copy of `render()`'s pre-draw half, using the same uniform-state reuse).

### 8.2 Candidate rules (`SceneMesh::isStaticBatchCandidate()` plus the batcher)
Opaque triangle meshes with indices; not selected, not hovered, not skinned, no morph targets, no analysis overlay,
no zebra, no material wireframe, no debug uniform overrides, no volume scattering or transmission, no negative scale,
and a transform that is a similarity (rotation, translation, uniform scale - so baked normals/tangents equal what the
vertex shader would have produced). Single parts above 2 M vertices stay individual.

### 8.3 Where it is used / not used
Used: main opaque pass, display mode Shaded, no clipping plane or box clip active, no flat shading, model with at
least `batchMinMeshes` (default 3000) displayed parts, setting `batchedDrawing` on.
Not used (unchanged per-mesh path): transparent pass, shadow pass, SSS/transmission pre-passes, selection/picking
passes, clipping passes and capping, flat shading, wireframe/edge display modes, small models.

### 8.4 Settings and diagnostics
- Debug tab: Batched Drawing (key `batchedDrawing`, default on), Batching Minimum Model Size (`batchMinMeshes`, 3000).
  `batchMinGroupSize` (default 6) is a QSettings-only key.
- `[RenderDiagnostics]` lines now end with `batchedMeshes/frame`, `batchDraws/frame` and `batchMs/frame` (CPU spent in
  the batcher update plus validation and draw submission). `draws/frame` counts one per group multi-draw.
- `[DrawBatcher] built groups=... meshes=... vertices=... extraGpuMB=... in ... ms` is logged when a build completes.
- `[BatchDiagnostics]` (signature census, 100+ displayed parts) is unchanged.
- `[Teardown]` stage timings are logged when a viewport is destroyed (see 8.6).

### 8.5 Known gaps and follow-ups
- Shadow pass, clipping passes, transparent meshes and flat shading are not batched (phase 4 candidates).
- A mesh whose material changes without its signature changing is not noticed (the existing per-mesh uniform reuse has
  the same property).
- Extra GPU memory is the batched vertex data (reported as `extraGpuMB`); per-mesh buffers are kept.
- Per-frame work is still linear in the number of drawn meshes (culling about 24 ms and batch validation at 40k parts).
  Removing it needs GPU-driven culling or a spatial structure; not started.
- No GPU-driven or occlusion culling, no per-mesh LOD tiers beyond the existing LOD1.

### 8.6 Teardown (cause found 2026-10-29, fix built, not yet verified in the viewer)
After closing a very large model the process keeps releasing memory for minutes. The destructors look cheap in the
source, so the cause was not obvious. Stage timings were added (`[Teardown] gpu-resources-released`,
`gpu-owned-objects-deleted`, `meshes-deleted`, `scene-deleted`, plus the per-mesh GPU release time).

Measured (40,000-part synthetic model, release build, no debugger needed): releasing the per-mesh GPU resources took
29-35 s and deleting the mesh objects took 153 s. The time per 5,000-mesh chunk fell steadily as meshes went (13.3 s
for the first chunk, 0.4 s for the last), in either deletion order, so the cost per object grows with the number of
live objects. A standalone benchmark (scratch code, `out/perf/glbench/bench.cpp`, not tracked) with 20,000 objects of
1 VAO + 10 buffers each reproduced the same shape with plain `QOpenGLBuffer`/`QOpenGLVertexArrayObject` (about 6 s
to destroy) and showed the same GL objects deleted directly in 7 ms. Cause: Qt registers every wrapper with its
context, and searching that registry makes each create/destroy cost proportional to the live count. It is not the
debugger heap and not our code's loops.

Fix: `FastGlBuffer` / `FastGlVertexArray` (`include/Render/FastGlObjects.h`, `src/Render/FastGlObjects.cpp`) replace the
Qt wrappers in `RenderableMesh`, `SceneMesh` and `MeshVizAdaptor`. Meshes are now deleted while the viewport's context is
still current, buffers released without a context are queued and flushed at the start of the next frame, VAOs without
their context are dropped (the driver frees them with the context). Expected: a 40k-part close in well under a second
instead of about three minutes; to be confirmed by the `[Teardown]` lines.

### 8.7 Owner test list
1. Large synthetic model (`out/perf/synthetic_40k_heavy.glb`), orbit: expect `batchedMeshes/frame` near the part count,
   `batchDraws/frame` near the group count (about 40), `frameCpuMs` and `gpuMs` far below the 190/170 ms baseline.
2. Compare Batched Drawing on and off in the Debug tab: images must be identical (colours, lighting, back faces).
3. Select a part, hover parts, multi-select, deselect: highlights correct, no part vanishes or doubles.
4. Move a part with the gizmo, undo/redo, explode view, animations: moved parts follow immediately.
5. Hide/show, isolate, delete and undo delete, paste: no ghost geometry.
6. Change a material (colour, texture) on one part: only that part changes.
7. Enable a clipping plane, then flat shading, then a wireframe mode: they fall back to per-mesh drawing and look as before.
8. Production vehicle: `[BatchDiagnostics]` and `[DrawBatcher] built ...` lines, frame times, memory.

### 8.8 First measurements (2026-10-29, owner run, synthetic 40k heavy model, release build, install exe)

| | Before batching | With batching |
|---|---|---|
| Draws per frame | 40,000 | 40 (`batchDraws/frame`) |
| CPU per frame | about 182 ms | about 50 ms (culling about 25 ms, batcher validation about 16 ms) |
| GPU per frame | about 157 ms | about 30 ms |

The first build took 14.8 s from the first scan to the last group (`[DrawBatcher] built groups=40 meshes=40000
vertices=11493922 extraGpuMB=884.2`), spread over frames; frames in between fall back to per-mesh drawing and get
faster as groups complete. The first run showed no batching at all because the "scene is still" wait never passed:
the runtime-bounds revision changes on every frame (`stableFrames=0` in the state line), cause not yet found. The first
build now starts after 1.5 s regardless; a rebuild starts once many meshes have been missing for 30 frames, with a
wait that doubles (up to 30 s) while rebuilds keep following each other.

Follow-ups done in the same round: the opaque and transparent passes share one culling traversal per camera and frame
(`_cullCache`), and the per-mesh transform comparison no longer copies the matrix (`combinedRenderTransformRef()`).
Not yet measured.

### 8.9 Teardown measurements

With `FastGlBuffer`/`FastGlVertexArray`: releasing the per-mesh GPU resources of 40,000 meshes takes about 45 ms
(was 29-35 s) and the whole close took 28.9 s (was 186 s). The remaining time is deleting the mesh objects; the
per-5,000-mesh chunk times rise then fall (1.4, 2.9, 3.5, 4.4, 4.9, 4.7, 3.9, 2.9 s). Not GL (raw GL deletes and texture
deletes measured at about 0 ms in the standalone benchmark) and not the debugger heap (also slow without a debugger).
The process held about 15-17 GB of private memory on a 32 GB machine that already used about 21 GB, so memory
pressure while freeing is a candidate; reducing the CPU-side copies per mesh is the likely lever. Not investigated
further.

### 8.10 Transparent meshes (2026-10-29)

On a 3,625-part JT assembly 1,857 parts were classed transparent: all by blend mode "alpha" (the JT importer sets it when
a vertex colour has alpha below 1 or a texture has a texel with alpha below 255; opacity itself is 1.0), and the
owner confirmed they are visibly translucent in the viewer, so this is correct rendering. Transparent parts cannot join
the opaque batches because they must be drawn back to front.

`DrawBatcher::drawTransparentSequence()` keeps that order exactly: it walks the sorted list and merges *consecutive*
meshes of the same batch group into one `glMultiDrawElements` (the multi-draw executes its ranges in array order),
flushing the run whenever the group, the LOD tier or a non-batched mesh interrupts it. The result is pixel-identical to
drawing them one at a time. The gain depends on run length: it is large when transparent parts share one or two
materials (the yellow parts in the JT test), small when many different transparent materials interleave in depth order.
Used only where opaque batching is (plain shaded mode, no clipping, no flat shading, model above the part threshold).
Not yet measured.

### 8.11 Memory and load findings (2026-10-30, synthetic 40k heavy model, owner machine, release build)

Observations (log lines `[MemoryAudit]`, `[LoadPhases]`, `[Teardown]`; regenerate by loading `out/perf/synthetic_40k_heavy.glb` with
"Profile rendering" on and reading the newest files in the application log folder):

- Every `RenderableMesh` built its own 128x128 fallback texture image (about 200 KB each; 40,000 meshes about 5 GB).
  One shared 1x1 white image replaces them. Private memory 14.3 GB -> 9.05 GB, peak 16.4 GB -> about 11.1 GB, close
  29 s -> about 10 s (mesh deletion dominates what is left).
- The GPU driver keeps about 6 KB of system memory per buffer object; roughly 6 buffers per mesh is a further cost
  (idea, not done: one interleaved buffer per mesh).
- Load tail: after the worker thread had finished reading (about 36 s) the viewer thread needed another 3 min 22 s
  before it handled `loadingFinished`. Cause: `Logger::processQueue` sent each formatted log line to the console panel
  with its own queued call, and a 40,000-part import writes about 480,000 debug lines. Lines are now sent in one batched
  call per drain round (the newest 1,000 of a burst; the log file keeps every line). Worker-finished ->
  `loadingFinished` handled: 3 min 22 s -> 0.05 s. Launch -> first rendered frame: about 5 min -> about 78 s, of which
  mesh creation is about 18 s (about 0.45 ms per mesh).
- Smaller load-tail contributors, also fixed: the loader emitted `nodeMeshProgressUpdated` per mesh and per node and
  `verticesProcessed` per mesh (both now at most every 50 ms), and their slots called `makeCurrent()` every time.
- Show All after a large hide froze the application for minutes: `SceneTreeWidget::setVisibilityByUuids` collapses and
  re-expands every expanded assembly and tree animation was on, so each call grabbed a pixmap of the viewport. With
  animation off for the duration the tree sync takes about 160 ms (`[VisibilityDiagnostics]` lines).

### 8.12 Per-frame CPU left at 40,000 meshes (not changed; measured 2026-10-30)

Steady state, plain shaded view: frame CPU 32 ms, GPU 22.5 ms, 40 batched draws. Of the CPU time 10.2 ms is culling
(`collectVisibleMeshIdsForPass`) and 13.5 ms is the batch pass (`DrawBatcher::drawBatches` validating each visible mesh).
Both are bound by cache misses: a `SceneMesh` is 8,096 bytes with a 5,384-byte `Material` embedded in it, and the
checks read fields spread over it. Candidate fixes and why they were deferred:

- Skip per-mesh validation on frames where nothing changed: needs one change counter that every input of
  `isBatchCandidate()` / `drawGroupKey()` / the transform feeds. `markUniformsDirty()` is one funnel, but selection,
  zebra, analysis overlay, skin/morph state and the transform have their own paths, so a missed path would draw a stale
  state. Needs an audit of every input before it is safe.
- Software prefetch of the mesh state a few iterations ahead: semantics-neutral, gain unknown because the fields sit in
  many cache lines.
- A compact per-mesh culling record (bounds, flags) in the runtime-visibility nodes instead of reading `SceneMesh`;
  needs invalidation when a mesh's transparency changes.
- Expected gain of all three: the frame becomes GPU bound (about 22 ms), i.e. about 1.4x.

### 8.13 Per-frame CPU: what was fixed (2026-10-30, synthetic 40k heavy model, orbit, release build)

Regenerate with "Profile rendering" on: orbit for 15 s, then read `[RenderDiagnostics]`, `[DrawBatcher] state`, `[PaintDiagnostics]`
and `[SlowEvent]` in the newest log files.

| | before | after |
|---|---|---|
| batch pass (`batchMs`) | 16 ms | 4.5 ms |
| `render()` CPU | 38 ms | 24 ms |
| one `paintGL()` call | 62 ms | 25 ms |
| frames per second while orbiting | 14 | 25-34 |

- Fast validation (section 8.12, first option) is implemented: a process-wide render-state epoch
  (`MeshInstanceState::renderStateEpoch()`), bumped by every transform, uniform/material/texture, geometry, primitive-mode,
  negative-scale, debug-override, skin, morph and index-remap change; `DrawBatcher::drawBatches()` caches each mesh's answer
  against it and re-checks a rotating slice of 256 meshes per frame, repairing and eventually disabling itself if the slice
  ever disagrees (`verifyMismatches` in the `[DrawBatcher] state` line). QSettings key `batchFastValidation` (default on).
  It only paid off after the axis-triad cone, which is rebuilt as the view changes, was marked as a decoration
  (`setAffectsSceneRevision(false)`); until then it moved the epoch 15-50 times a second.
- The larger loss was outside `render()`: `renderSingleView()` ran `sceneHasVisibleSSSMaterials()` and
  `sceneHasVisibleTransmissionMaterials()` every frame over every mesh, and both called `getMaterial()`, which returns the
  5.4 KB `Material` by value (about 80,000 copies per frame). `RenderableMesh::materialRef()` returns a reference. This is
  what `[PaintDiagnostics]` exposed: the paint call took 62 ms while the render profiler reported 24 ms.
- Lesson for further profiling: the render profiler only covers `render()`. `[PaintDiagnostics]` (CPU per section of
  `paintGL()`) and `[SlowEvent]` (events of 8 ms or more) show the rest.
- Not changed: culling is still 12.5 ms per frame (hidden behind the GPU now); the frame is GPU bound at about 25 ms
  (13 million triangles). `getMaterial()` callers outside the per-frame path (export, dialogs, load) still copy.

### 8.14 Mesh creation and first frame (2026-10-30, synthetic 40k heavy model, release build)

Observations (log lines `[MeshBuildProfile]`, `[LoadPhases]`, `[SlowFrame]`; owner machine):

- Mesh creation on the GUI thread fell from 17.9 s to 10.6 s; the first frame from 12 s to 1.27 s.
- Per-mesh GL objects that most meshes never use are now created on first use: the feature-edge buffers, the 1x1 fallback
  texture and seven seldom-used buffer names (`FastGlBuffer::createLazily()`; `isCreated()` is true for a lazy object and
  `bind()` creates it). Feature-edge GPU upload waits for `ensureFeatureEdgesOnGpu()`, called when edges are drawn.
- `SceneMesh::wireframeFeaturesEnabled()` read QSettings once per mesh; it is cached for 1 s.
- `DrawBatcher::Settings::immediateInitialBuild`: once an import has finished (no pending scene uuids) the first batch build
  runs synchronously with the per-frame time budgets off, so the first frame draws batches rather than 40,000 individual
  meshes (the 12 s first frame was first-use state setup in the individual path).
- `include/Scene/MeshBuildProfile.h` times each stage of mesh creation (CtorBody, SetupArrays, GlUpload, LodAndBounds,
  FeatureEdges, BaseGlObjects, BaseAutoName, builder pre/post, attach, display-list update).
- `logMemoryAudit` walked the whole address space (about 4 s on a 9 GB process); the breakdown is now opt-in
  (QSettings `memoryAuditBreakdown`) and the audit logs its own cost when it exceeds 50 ms.

Still open (measured, not changed): GL upload 3.7 s, array setup 1.0 s, constructor body 0.8 s, worker tail
`optimize-meshes` about 6 s, per-mesh conversion about 17 s single-threaded, GLB parse about 10 s.
Smoke test by the owner (wireframe/edges, selection/hover, textured glTF, skinned/animated glTF): passed, 2026-10-30.
