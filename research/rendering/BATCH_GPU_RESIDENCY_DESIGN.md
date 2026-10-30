# Batch GPU residency: freeing per-mesh buffers of batched meshes

Status: design for owner review (2026-10-30). Nothing in this document is implemented. Follows
[DRAW_BATCHING_DESIGN.md](DRAW_BATCHING_DESIGN.md) section 8.15, which records the measurements cited here.

## 1. Problem (observations)

Measured on the synthetic very heavy model (40,000 meshes, 147 M triangles, 101 M vertices; owner machine, release build,
2026-10-30):

- A mesh that is in a draw batch still keeps its own GPU buffers. Every batched vertex therefore exists twice in video memory.
- Video memory in use: about 12.4 of 16.4 GB (`[MemoryAudit] gpuUsedMB`/`gpuTotalMB`). The batch set alone is 5.2 GB
  for 63.85 M vertices, about 81 bytes per vertex.
- `kMaxTotalVertices` (64 M vertices, `DrawBatcher.cpp`) stops the batch set at 26,151 of 40,000 meshes.
  It is a deliberate guard: raising it without freeing something else would exceed the card.
- The 13,849 meshes outside the batch set draw one by one every frame. Orbit is then CPU bound: about 76 ms per frame
  against about 60 ms of GPU time (`[RenderDiagnostics]` `frameCpuMs/frame`, `gpuMs/frame`, `batchedMeshes/frame`).
- The per-mesh buffers of the batched meshes are idle in the normal shaded view: selection and hover are the only reasons
  a batched mesh draws individually, and selection no longer does (commit e8d71e2).

Hypothesis (not measured): per-mesh buffers are the largest single user of video memory on this model. From the
`[MemoryAudit]` CPU-side figures the per-mesh vertex data is roughly 8 GB (points 1.2, normals 1.2, colours 1.5,
tangents and bitangents 2.3, indices 1.7, LOD indices 0.6 GB, plus texture coordinates); the GPU copy has the same
layout. This needs one measurement before implementation (phase 0).

## 2. Options

| Option | Effect | Verdict |
|---|---|---|
| A. Raise `kMaxTotalVertices` | all 101 M vertices batched | Rejected: 12.4 GB in use plus about 3 GB more exceeds the practical budget and the driver starts paging. |
| B. Evict the per-mesh buffers of batched meshes; restore from the CPU arrays when a mesh must draw individually | frees roughly the per-mesh share of 12.4 GB; lets the whole model batch | Recommended. |
| C. Make the batch buffers the only storage; give each mesh a VAO that points into them | no duplicate at all | Rejected for now: batch vertices have the mesh transform baked in, so every individual draw, edit and animation would need a different matrix path and sub-range rewrites. |

## 3. Design of option B

### 3.1 Mesh states

`RenderableMesh` gets a residency flag: **Resident** (today's state) or **Evicted** (position, normal, colour, tangent,
bitangent, texture-coordinate, joint and index buffers and the LOD index buffer deleted; the VAO object and the CPU arrays
`_points`, `_normals`, `_colors`, `_tangents`, `_bitangents`, `_texCoords`, `_indices`, `_lod1Indices` kept). The CPU arrays
are object space, so a restore re-runs the body of `initBuffers()` from them and rebuilds the VAO bindings, with no
dependence on any transform state.

### 3.2 Restore points (the correctness-critical part)

Every place that binds a mesh's own VAO or buffers must call `ensureResident()` first. Found by search
(`_vertexArrayObject.bind()`, `getVAO()`):

| Site | Used by |
|---|---|
| `SceneMesh::render()` | individual draws: hover, meshes outside the batches, passes that do not batch |
| `SceneMesh::renderWireframeFast()` | wireframe display mode |
| `RenderableMesh::render()`, `renderShadow()`, and the shadow loop in `ViewportWidget` (`renderToShadowBuffer`) | shadow pass (draws every visible mesh) |
| `SelectionManager::processSelection()` | colour pick (only the meshes near the pixel since e8d71e2) |
| `RenderableMesh::setProg()` VAO reconfigure | program change |
| feature-edge draws | own VAO and buffers; not touched by eviction |

`ensureResident()` is one inline check of the flag; the restore itself runs only after an eviction. A debug build asserts
that no draw reaches a deleted buffer.

### 3.3 When to evict

Eviction is allowed only while all of these hold, evaluated once per frame by the `DrawBatcher`:

1. the mesh is in a built, valid group (`Entry::group >= 0`) and was drawn from it for N consecutive frames (N about 120);
2. the pass configuration is batch-only: shaded display mode, no shadow pass, no clip plane or section capping, no
   analysis overlay, not the path-traced view;
3. the mesh is not hovered, not being edited (transform gizmo, exploded-view placement), not animated.

Eviction runs in slices of a few milliseconds per frame (like the batch build) after the build has finished, never
during interaction frames. When condition 2 stops holding (the user switches to wireframe, turns shadows on), nothing is
restored eagerly: each mesh restores itself on its first individual draw. Restoring all 40,000 costs about one second
of upload per 1,000 meshes at 3,700 triangles each (Estimate, unmeasured), so a one-off pause of tens of seconds is
possible on the largest models; the progress is spread over frames.

### 3.4 Raising the cap

`kMaxTotalVertices` becomes a budget derived from the video memory query already used by `[MemoryAudit]`
(`gpuTotalMB`): batches may take up to a fixed share of the card (initial proposal 45%) while eviction runs, and stay at
64 M while eviction is off. Setting `batchEvictMeshBuffers` (default off in the first release of the feature) switches
the whole feature.

### 3.5 Interaction with other planned work

- The CPU arrays must stay. Dropping `vertexStructs` (12 GB of CPU memory on this model) is compatible only if the
  object-space arrays survive; any CPU-memory reduction must be checked against this design.
- Instancing is blocked by per-occurrence baked transforms (see the memory note on instancing); eviction does not change that.
- Path tracing builds its scene from CPU data and never reads these buffers (no matches under `src/RayTracing` for the
  draw path); verify again when implementing.

## 4. Expected gain (Estimate, unmeasured)

- Video memory: roughly 6-8 GB freed on this model, enough to batch all 40,000 meshes (about 8 GB of batch data).
- Orbit frame CPU time: from about 76 ms toward the batched floor (culling 12.7 ms plus about 5 ms of batch draw), so the
  frame becomes GPU bound at about 60 ms: from about 13 to about 16 frames per second. The larger benefit is headroom on
  cards with less memory and on models above 101 M vertices.
- Cost: a restore pause when a pass that needs per-mesh buffers is switched on; extra code at six sites.

## 5. Phases and gates

0. **Measure** (no behaviour change): log per-mesh GL memory by buffer kind, and the number of meshes drawn individually per
   frame by reason. Gate: the per-mesh share of video memory is confirmed.
1. **Residency API** behind `batchEvictMeshBuffers` (default off): `evict()`, `ensureResident()`, assertions, restore at
   the six sites. Gate: owner build and test with the setting on but eviction never triggered by policy (forced restore
   path tested by a debug command).
2. **Policy**: eviction after build under the conditions in 3.3, slices per frame. Gate: owner runs the test matrix below.
3. **Cap**: video-memory-derived batch budget. Gate: very heavy model batches fully; memory audit shows the freed amount.

Owner test matrix after phase 2 (each with eviction having run, then switching back): hover and select parts; move a part
with the gizmo and undo; play the planetary gear animation; wireframe display mode; shadows on and off; clip plane and
section capping; surface-analysis overlay; switch to the path tracers and back; hide, show, isolate, delete, paste;
exploded view; context loss (change the monitor or resume from sleep); save and reload an MVF; measurement and
annotation tools; export to glTF.

## 6. CPU memory: the cause of the slow first interaction

### 6.1 Observations (owner run, 2026-10-30, 31.9 GB RAM machine, very heavy model)

- The first frames after load, and the first frames after about 45 minutes of idle, are slow: 300-570 ms per frame for roughly
  10 s, then about 75 ms (`[RenderDiagnostics]` at 20:40:06-20:40:11 and 21:25:15-21:25:24). In those frames `assImpMs/frame` is
  190-300 ms and `materialUniformMs/frame` 20-60 ms, against 45 ms and 2 ms once warm. The owner also reports the first mouse
  interaction after load as a little sluggish.
- `[MemoryAudit]`: `privateMB` about 57,800, `workingSetMB` about 8,300, `meshArraysMB` 22,218, `otherMB` about 35,600,
  `gpuUsedMB` 15,787 of 16,380. The machine has 31.9 GB of RAM and about 3.7 GB free. So about 49 GB of committed private memory
  is not resident.
- CPU mesh arrays (MB): `vertexStructs` 12,334 (128 bytes per vertex, `std::vector<Vertex>`), points 1,156, normals 1,156,
  colours 1,541, tangents 2,311, indices 1,690, `worldPoints` 1,156, `lodIndices` 672, edges 203.
- The audit accounts for 22 GB. The other 35.6 GB (`otherMB`) is not attributed to anything.

### 6.2 Hypotheses (not yet tested)

- H1. Most of the non-resident memory is paged out; the first frames touch each mesh's data (draw loop, uniforms, culling) and
  fault it back in. Supported by the shape of the warm-up (slow, then a sharp drop) and by its reappearance after idle.
  Not yet shown: a page-fault count per frame.
- H2. Part of `otherMB` is the graphics driver's system-memory mirror of the 15.8 GB of video memory (WDDM commits it to the
  process). That portion cannot be reduced except by using less video memory, which is what sections 3-4 do.
- H3. Part of `otherMB` is memory freed by the loader (file buffer 3.4 GB, parse arrays) but still held by the heap, not
  returned to the system. Retained but never touched again, it is paged out silently and only costs commit and disk traffic.

### 6.3 Measurements before any change (phase C0, diagnostics only)

1. Log process page faults (`PageFaultCount` from `GetProcessMemoryInfo`) in `[RenderDiagnostics]`, so a warm-up frame shows its
   fault count. Confirms or rejects H1.
2. After load, log `privateMB` before and after `_heapmin()` and `HeapCompact` on the process heap. A large drop confirms H3.
3. Log the video-memory commit estimate beside `otherMB` (`gpuUsedMB` is already there). A rough one-to-one match supports H2.
4. Break `otherMB` down by owner where cheap: SceneMesh objects (310 MB, known), `MeshInstanceState`, tree widgets, texture
   caches, per-mesh `QOpenGLBuffer`/VAO wrappers.

Gate: the owner confirms which of H1-H3 explains the 35.6 GB before C1.

### 6.3a Phase C0 results (owner run, 2026-10-30 21:34-21:51, same machine and model)

Observations:
- Heap trim (`_heapmin` plus `HeapCompact` on all heaps) released 0 MB. No free heap memory is being held back.
- Address-space walk: process heaps 12; allocated 23,447 MB, committed 26,737 MB; private committed 47,272 MB; mapped 975 MB;
  images 626 MB. Heap-allocated memory matches the mesh arrays (22,218 MB) plus object overhead. The remaining
  about 20,500 MB of private commit sits outside the heaps, in large regions (>=16 MB: about 20,100 MB in 358 regions);
  video memory in use was 15.8 GB.
- Page faults per frame, warm-up after load (frame CPU time in brackets): 237,000 (521 ms), then about 2,300-2,500 per frame
  (390-400 ms) for several frames; after the 45-minute-style idle gap: 47,000 (459 ms), 5,300, 1,900-2,000 (250-520 ms),
  500 (256 ms), 179 (98 ms), then 8-16 per frame (77 ms) in steady state.
- The first frame after load took 186 s in this run (56 s in the earlier run) and took 35.2 million page faults; the frame before
  it took 5.1 million faults (1.4 s). The batch build reads every mesh's arrays in that frame.

Interpretation (hypotheses, supported not proven):
- H3 (free heap held by the loader) is rejected: the trim released nothing.
- H1 (paging) is supported: slow frames coincide with thousands of page faults per frame and the frame time falls in step with
  the fault count to 77 ms at under 20 faults. The first-frame build thrashes (tens of millions of faults), which explains why its
  time varies between runs (56 s and 186 s) with the memory other programs use.
- H2 (driver mirror) is supported: private commit outside the heaps (about 20 GB) is of the size of the 15.8 GB of video memory
  plus a few GB. Cutting video memory (sections 3-4) should cut it.
- Not measured: whether those faults are hard (disk) or soft (zero or standby pages); the counter includes both.

Consequence for the plan: C1 (heap trim) is dropped. C2 (drop `vertexStructs`, 12.3 GB of the 23 GB heap) and C3 are the CPU levers;
the driver mirror is addressed by the residency work. The batch build's whole-model read in the first frame should also be
considered separately (build from a background thread, or in slices, to avoid a single multi-minute frame).

### 6.4 Reductions, in order of benefit and risk (Estimate, unmeasured)

| Step | Frees | Notes |
|---|---|---|
| C1. ~~Return loader memory to the system (heap trim)~~ | 0 MB | Dropped: measured in C0 (6.3a). |
| C2. Drop `vertexStructs` after the separate object-space arrays exist | 12.3 GB | 155 matches in 31 files use `Vertex`/`_vertices`: UV and tangent generators, morph targets (`_baseVertices`), deformable geometry, simulation, Plot3D, exporter, ray tracing. Not a drop-in: those users must read the separate arrays, or the structs must be rebuilt on demand from them. Large meshes only, behind a threshold, so small models are untouched. |
| C3. Drop `worldPoints` (the transformed-points cache) | 1.2 GB | Rebuilt lazily on use (selection, measurement). Check callers in `SelectionManager`, `MeasurementController`. |
| C4. Keep the arrays the residency design needs | none | Section 3.5: `_points`, `_normals`, `_colors`, `_tangents`, `_bitangents`, `_texCoords`, `_indices`, `_lod1Indices` must survive. C2 and eviction are compatible only if `vertexStructs` is the one that goes. |

Expected effect if H1 and H3 hold: resident memory falls below RAM (22 GB of arrays minus C2 and C3 is about 9 GB, plus
textures and driver mirror), so the warm-up page-in disappears. If only H2 holds, the warm-up is driven by the arrays
alone and C2 is the lever.

### 6.5 Interaction with the residency work

Eviction (sections 3-4) lowers video memory, which lowers the driver mirror (H2) and removes the draw loop's reason to touch
13,849 meshes' buffers. C2 lowers CPU memory. They are independent and can ship in either order; C0 and C1 first, since they
are diagnostics and a no-behaviour-change fix.

## 7. Open questions for the owner

1. Is a restore pause of up to tens of seconds acceptable when switching the very heavy model to wireframe or shadows, or
   should those modes stay batch-capable first?
2. Should the feature be on by default for models above the interaction fast-path size, or opt-in through Settings?
3. Is 45% of video memory the right ceiling for the batch set on the owner's target cards?

4. Phase C2 changes who owns vertex data on very large models. Is a size threshold (apply only above the interaction fast-path
   size) acceptable, so small models keep today's code path?

## 8. Provenance

All figures come from the owner's runs of the synthetic very heavy model on 2026-10-30. The model is produced by a local
generator under the ignored `out/perf/` folder (`regen_all.sh`); the logs are the application's own log files
(`[MemoryAudit]`, `[RenderDiagnostics]`, `[DrawBatcher]`, `[SelectDiagnostics]`). Neither is tracked, per the
research-folder rules in [AGENTS.md](../../AGENTS.md).
