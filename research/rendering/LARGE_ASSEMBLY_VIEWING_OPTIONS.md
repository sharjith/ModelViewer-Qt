# Viewing performance of large assemblies: current state and options

Status: first pass, 2026-10-28. The first section is **Code** (read in the source, not measured). The options
are **Hypotheses** until a frame-time breakdown exists. Sources are web search results; the original papers
were not read in full.

## 1. What the viewer does today (Code)

| Area | Present behaviour | Where |
|---|---|---|
| Frustum culling | Hierarchical on the assembly tree (`RuntimeVisibilityNode::subtreeBounds`, subtree pruned when outside), then a per-mesh AABB test in `isMeshVisible()`. Rerun for every pass (main, shadow, clipping). | `ViewportWidget.cpp` `collectVisibleMeshIdsForPass`, `isMeshVisible`; `VisibilityComputationHelper.cpp` |
| Spatial structure | None of its own: the culling tree is the assembly tree. A sub-assembly's bounds can span the whole vehicle, so pruning is weak where assemblies interleave in space. | same |
| Occlusion culling | None. | no matches in `src/` |
| LOD | One precomputed coarse tier (`LOD1`). Used only while the user interacts **and** the displayed model is over 50 MB (`MAX_MODEL_SIZE_BYTES`), applied to every mesh at once, not by screen size or distance. Snaps back to full detail when interaction stops. | `ViewportWidget.cpp` ~9104-9119, `RenderableMesh.cpp` ~1752 |
| Draw submission | One `glDrawElements` per mesh with per-mesh state; no instancing, multi-draw or indirect draw. | `RenderableMesh.cpp` ~1757 |
| Small-feature culling | None (no projected-size threshold). | no matches |
| Shadows | Skipped during interaction on oversized models; a separate per-mesh test against the shadow volume. | `ViewportWidget.cpp` ~12730 |

Not yet known (needs measurement): whether a frame on a ~35k-part assembly is limited by CPU (culling, state
changes, driver submission of tens of thousands of draws) or by the GPU (vertex load, fill rate, transparency,
shadow pass). Every option below helps only one of these.

## 2. Options, cheapest first

| # | Option | Helps | Cost / risk | Evidence |
|---|---|---|---|---|
| 1 | **Frame breakdown first** (done 2026-10-28, untested): the existing `[RenderDiagnostics]` log line (Settings > profile-rendering checkbox, written once per second to the app log) now also reports `cullMs`, `meshesAfterCull`, `meshesDisplayed`, `triangles` and `gpuMs` (GL timer query, read one frame late) next to the existing CPU stages and draw counts. Run on the synthetic 35k model and the production vehicle. | decides everything else | small, no behaviour change | n/a |
| 2 | **Screen-size (small-feature) culling** (implemented 2026-10-29, untested): skip a mesh, and any subtree, whose bounding sphere projects under N pixels (default 4, registry key `smallPartCullPixels`, 0 = off). Active only while the camera is moving (`lowResEnabled`) on a model over the 50 MB fast-path size, so small models and resting views are unchanged. Compare `meshesAfterCull` and frame times in `[RenderDiagnostics]`. Baseline on the synthetic 40k heavy model (full view, orbiting): about 285 ms CPU, 250 ms GPU, 40,000 draws. | CPU and GPU, large on screws/clips at vehicle zoom | low; tiny parts vanish (gaps), so keep N small and off at rest if needed | Open CASCADE ships it as "size culling", off by default ([forum](https://www.opencascade.com/content/render-fewer-shapes-while-zoom-or-rotate-large-model)); C3D Vision lists pixel culling ([C3D Vision](https://c3dlabs.com/products/c3d-toolkit/vision/)); a patent describes the gap artefact ([patent](https://patents.justia.com/patent/20170330371)). |
| 3 | **Per-mesh LOD selection by screen size/distance**, not a global switch; coarser tiers (LOD2) for very small projections. | GPU vertex load, memory bandwidth | medium: needs LOD generation for more tiers (existing meshopt LOD is the base) | JT's LOD scheme is coarse-to-fine by view ([cadinterop](https://cadinterop.com/en/formats/neutral-format/jt.html)) |
| 4 | **Spatial BVH over mesh AABBs** (built once per load/structure change, refit on transform), replacing the assembly-tree prune. One traversal per frame feeds all passes. | CPU culling, and later occlusion queries | medium: must handle moving/exploded/animated meshes (refit) and skinned meshes (already excluded from culling) | A spatial-partitioning patent notes assembly and part hierarchies have no implicit spatial relation, hence the need ([patent](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9424370)). |
| 5 | **Draw-call reduction**: sort/batch by material and program, upload per-mesh data once (UBO/SSBO), then multi-draw. | CPU submission cost, usually the limit at 30k+ draws | medium-high: touches the renderer's per-mesh uniform path | nvpro-samples notes multi-draw indirect pays off for meshes with thousands of triangles, less for tiny draws ([nvpro gl_occlusion_culling](https://gitee.com/mirrors_nvpro-samples/gl_occlusion_culling)) |
| 6 | **Occlusion culling**: hierarchical, using last frame's visibility (CHC++, hardware queries) or a GPU Hi-Z two-pass scheme. | GPU/CPU where interiors are hidden (engine bay, cabin) | high: latency/popping, transparency, and cost can exceed the saving; a BIM viewer's GPU-driven attempt ended net-flat and was reverted ([IfcOpenShell log](https://dev.glassvision.dedyn.io/Glassvision/IfcOpenShell/src/commit/a34c36d22e8357c126ad5a20a02f5afce5314ced)) | CHC++ ([TU Wien](https://www.cg.tuwien.ac.at/research/publications/2008/mattausch-2008-CHC/)): up to an order of magnitude over earlier occlusion-query culling, no worse than frustum culling in its worst case (as reported); two-pass Hi-Z pattern ([article](https://hn.jakechampion.name/reader/https://krupitskas.com/posts/modern_culling_techniques/)) |
| 7 | **Out-of-core / on-demand geometry**: load simplified shells first, refine by visibility. | memory and load time, not frame rate | high; already planned as its own branch | Teamcenter's per-level simplified shells ([Siemens PDF](https://www.mayahtt.com/wp-content/uploads/2013/03/Teamcenter%20Visual%20Navigation%20and%20Reporting.pdf)); Fraunhofer loads the largest visible parts first ([slides](https://old.web3d.org/sites/default/files/attachment/node/1870/edit/WebGLBOF_largeCADVis.pdf)) |

## 3. Suggested order
1. Frame breakdown (option 1). It is the only way to know whether to start with CPU or GPU work.
2. Screen-size culling (2): cheap and likely the biggest win on a vehicle full of fasteners.
3. If CPU-bound: BVH (4) then draw-call reduction (5). If GPU-bound: per-mesh LOD (3), then occlusion (6).
4. Occlusion last: highest complexity and the only option with a documented net-zero outcome in a comparable viewer.

## 4. Gaps
- No vendor source found for how VisMockup, Navisworks, Creo View or 3DEXPERIENCE cull or batch. The survey in
  `research/assembly-loading/LARGE_MODEL_PRACTICE_SURVEY.md` has the same gap.
- The numbers quoted from the IfcOpenShell commit log are anecdotal.
- The interaction-time LOD behaviour already in the viewer was not found documented elsewhere; no external
  reference was found for or against it.

## 5. Test data
- Synthetic: `tools/perf/make_synthetic_assembly.py` (35k and 10k meshes, nested assemblies), output in `out/perf/`.
- Flat: Khronos NodePerformanceTest (10k nodes). It exercises mesh count but not tree depth or assembly culling.
- Production vehicle assembly: owner-run only.
