# C5 runtime detail (LOD): evidence and design

3 November 2026, `feature/JT-importer`. Research and design record. Sections 1 to 8 were written before any code; section 9 records what C5a changed. Nothing was built or run beyond the read-only tool named below, and nothing here is owner-accepted. It starts the C5 stage of the [feature completion plan](JT_FEATURE_COMPLETION_PLAN.md) ("retain/reload qualified LOD alternatives and switch them at runtime: camera/range criteria, resource sharing, stable scene identity, selection, cancellation, bounds and no duplicated LOD display").

## 1. What the importer does today

Observed in `src/Import/Jt/JtSceneBuilder.cpp` (the group branch of `visit()`), recorded earlier in [display selection](JT_DISPLAY_SELECTION_VALIDATION.md) and the [assembly attribute validation](JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md):

- A LOD or Range LOD node keeps exactly one child. Without an eye point it is the first child; with `Options::lodEyePoint` it is chosen by the Range Limits against the distance from the (world-transformed) Centre; a rejected child is rolled back and the others are tried in source order.
- Every other alternative is never decoded and never reaches the scene: there is nothing to switch to later.
- The node records `JTSelectedChild` (and `JTRangeLODDistance`) as metadata. The scout pass skips LOD nodes when an eye point is set.

The JT reference says the same ordering is **not** a detail order: the children of a LOD node carry "no implicit semantics associated with the ordering" (section 5.3.8). The first-child policy is therefore a convention, not a statement about detail.

## 2. What the format says (JT 10.6 reference, Rev D, 10 February 2026)

Quoted by section, not reproduced:

- **5.3.9 Range LOD node.** Range Limits and Centre are "soft values": a viewer may discard them and compute its own selection. Best practice: first alternative while distance is at most the first limit (and when there are no limits), the next while it is at most the second, and the last beyond the final limit. Typical Centre is the centre of the highest-detail alternative.
- **4.1.3 (Data Segment), NOTE 1 on the segment type.** Segment types 7 to 16 are Shape LOD segments; the lower the type, the more detailed (type 7 the most detailed); LOD 9 and above share type 16. Type 6 is the generic shape type, used when the shape is not under a LOD node, is shared by several LOD nodes, has built-in LODs, or its level cannot be determined.
- **11.11.** The conventional part structure is Part node, Range LOD node, one group per alternative, shape nodes beneath.

So the format gives a **reliable detail ordering on the shape segments** (the type number), not on the node children.

## 3. Evidence from the local production corpus

Tool: [`tools/jt/inspect_lod_alternatives.py`](../../tools/jt/inspect_lod_alternatives.py), read-only (table of contents and the root scene graph's element envelopes; no shape payload decoded, no external partition followed, no model string kept). Corpus: nine local files by neutral alias (`documentation-sample-map.json`, local only; no redistribution rights). Regenerate one, output under `out/`:

```
python -I tools/jt/inspect_lod_alternatives.py --output out/jt-research/c5/lod-sample-A95.json <path to the file>
```

Observations (shape segments by type; on-disk MiB is the compressed segment size, a proxy for triangle count, not a triangle count):

| Alias | JT | Shape segments (count, on-disk MiB) | Range LOD nodes |
| --- | --- | --- | --- |
| L80 | 8.0 | type 7: 54, 1.6 | 5, no limits |
| L81, R1 | 8.1 | type 7 only (1,084 / 318) | scene graph not read by the tool |
| Q82 | 8.2 | type 7: 6, 1.5 | 6, no limits |
| R2, R3 | 8.1 | type 6 only (114 / 189), about three shapes per Range LOD node | 38 / 59, no limits |
| **A95** | 9.5 | **7: 2,999, 90.9; 8: 2,999, 19.4; 9: 2,999, 10.2** | 2,228, no limits |
| **A103** | 10.3 | **7: 2,468, 51.4; 8: 2,468, 11.3; 9: 2,468, 5.2** | scene graph profile not read by the tool |
| **A109** | 10.9 | **7: 2,865, 7.5; 8: 2,865, 2.4; 9: 2,865, 1.7** | scene graph profile not read by the tool |

What this shows (observation, these exporters and files only):

1. **Three authored detail levels exist in the largest production files** (A95, A103, A109), one shape segment of each type per shape. Levels 8 and 9 are about 21% and 11% (A95), 22% and 10% (A103), 32% and 23% (A109) of level 7 by segment size. The importer reads only level 7 and discards the rest.
2. **No Range LOD node in any scanned file has Range Limits; every Centre is (0, 0, 0).** (A95, L80, Q82, R2, R3: all 2,336 nodes.) By the format's best practice that means "first alternative always"; the authored distance data that `lodEyePoint` selection relies on is absent from these exporters. Distance-driven switching would use viewer-computed thresholds, which the format allows ("soft values").
3. The 8.x files and the small files carry one level only; R2/R3 carry three alternatives of the generic type 6, whose order is therefore unknown.

Not examined: the scene graph of A103/A109 (the tool does not decode their root graph's compression profile), which child of each Range LOD node uses which segment type (not decoded), triangle counts, and any exporter other than the ones that wrote these files.

Hypothesis (unverified): in A95/A103/A109 the first child of a Range LOD node references the type 7 segment, the second type 8, the third type 9. Verify with a native probe before relying on child order; selecting by segment type does not depend on it.

## 4. What the viewer already has

- **A generated interaction tier.** `SceneMesh::optimizeGeometry` builds a coarse index set with `meshopt_simplify` (target 22% of the triangles, error 0.02; meshes of at least `lodMinTriangles`, default 2,000). It **reuses the mesh's own vertex buffer** (an index subset), is staged in `_pendingLod1Indices`, uploaded as the mesh's LOD index buffer, and drawn while the camera moves on a large model (`RenderableMesh::setLodPolicy`, one global switch). The draw batcher reads the same LOD index buffers. Settings carry the fast-path size and the small-part cull threshold.
- The generated 22% target is close to the authored second level of A95 and A103. An authored level is not an index subset of the finest mesh: it has its own vertices, normals and, per material, its own mesh split.
- On-demand assembly loading is a separate planned branch ([design note](../assembly-loading/ON_DEMAND_ASSEMBLY_LOADING.md)); the memory guard and per-unit budget (6c80a2e) already let a 50k-part assembly load.

## 5. Options

| Option | What it is | Memory | Load time | New risk |
| --- | --- | --- | --- | --- |
| **A. Import detail level** | A setting picks which level to load per shape (finest, medium, coarsest) by segment type, with nearest-available fallback; one alternative per shape, as today | Large saving: about 5x to 9x less shape data at the coarsest level on A95/A103 | Faster (less to read and decode) | Geometry in the session is coarse: measurements, section caps, edges and exports use it |
| **B. Authored coarse tier as the interaction tier** | Decode level 7 for display and a coarser level as the interaction tier, append the coarse vertices to the mesh vertex buffer, skip the generated simplification for such meshes | About +20% to +40% vertices per shape over finest-only; no change to the at-rest geometry | Decode of one more segment, minus the `meshopt_simplify` pass | Per-material mesh splits can differ between levels; needs a mapping or a matching split rule |
| **C. Retain all levels, switch at runtime** | Every level becomes a sibling scene node, exactly one visible, chosen by screen size or distance | About +30% shape data resident; GPU as well unless levels are unloaded | Slowest (all levels decoded) unless decode is deferred | Identity, selection, anchors, bounds, batching and exports all see several meshes per shape; duplicate display if the switch fails |
| **D. Lazy refinement** | Load a coarse level; load the finest for a part on demand (selected, zoomed to, measured, exported) by re-reading its late-loaded segment | Low at rest | Fast open; a read per refined part | Needs a retained file handle and a swap of geometry under a stable mesh identity |

## 6. Recommendation

Do A, then B; design D with the on-demand loading branch; treat C as not needed unless distance-driven display of several levels is wanted for its own sake.

1. **C5a: import detail level.** Select by Shape LOD segment type (7 finest, then 8, 9), not by child order. Default stays finest, so nothing changes unless the owner picks another level. Record the chosen level in node metadata and in the import report. The setting lives with the JT importer settings. Measurements and recorded PMI are placed on the loaded triangles (`MeshSurfaceAnchor`: mesh, triangle index, barycentric), so a level change means re-importing and re-measuring. Sessions are expected to be safe because the MVF is a glTF-based package that carries the loaded geometry itself (hypothesis, not checked: confirm that a saved session reloads its own meshes rather than re-reading the JT file).
2. **C5b: authored coarse interaction tier.** Reuse the viewer's existing interaction machinery (global LOD policy, batch LOD reads, cull threshold) rather than a second switching system. Prerequisite: a probe that prints, per Range LOD child, its segment type and triangle count, to learn how often the levels' material splits agree.
3. **D with on-demand loading.** The refinement hook and the on-demand loader both need "re-read a part's geometry from its file under a stable mesh identity"; design them once.
4. **Defer C.** It carries all the identity/selection/anchor risk for the least extra benefit once B exists.

### Constraints that apply to every option

- **Identity and anchors.** Mesh UUIDs, selection, measurement anchors and recorded PMI must refer to one canonical geometry. Anything displayed from a coarser level is display-only unless the whole session is at that level (A).
- **Exactly one alternative displayed;** a failed switch must leave the previous one visible (the existing rollback model).
- **Bounds.** Levels of one shape can differ slightly; fit-to-screen and culling should use the finest level's bounds so a switch does not move the camera.
- **Resource sharing.** Level segments are late-loaded and cached per segment (`geometryCache`); a shared definition's levels must be shared across occurrences.
- **Cancellation and budgets.** Reading an extra level counts against the per-unit allocation budget and the memory guard; each level's decode is a cancellable unit.
- **Range Limits are absent** in every scanned file: do not build a selection that needs authored distances; compute thresholds in the viewer (screen-space size of the part's bounding sphere).

## 7. Gates and tests

1. Owner-run native probe (to be written, behind the owner's build step): for the 2,228 Range LOD nodes of A95, print child index against segment type and triangle count; confirms or refutes the child-order hypothesis and gives real triangle ratios.
2. C5a unit tests with the existing fixture generators: a Range LOD fixture with three alternatives of types 7, 8, 9 selects each by level; missing level falls back to the nearest; type 6 alternatives keep source order; a rejected level rolls back.
3. Owner measurements on A95 and A103 at each level: load time, peak private bytes, triangle count, orbit gpuMs. Recorded as observations, not predicted here (the on-disk ratios in section 3 are a proxy only).
4. Owner acceptance of visual equivalence of the coarse levels against an independent viewer, per the repository rules.

## 8. Decisions for the owner

1. Is import-time detail level (A) the first step, or is the interaction tier (B) wanted first?
2. For A: which levels to offer (finest, medium, coarsest, and "automatic by model size")?
3. Is the expectation that an MVF carries its own geometry (so saved anchors stay valid at the level they were made) correct for JT imports?
4. Is the lazy refinement (D) to be built with the on-demand loading branch, as proposed?

## 9. C5a as implemented (3 November 2026, awaiting owner build and test)

- `Jt::Options::DetailLevel { Finest, Medium, Coarsest }`, default Finest (behaviour unchanged). `Options::fromEnvironment()` reads the setting `jtDetailLevel` (0 to 2) and `MODELVIEWER_JT_DETAIL_LEVEL` (finest, medium, coarsest; the variable wins).
- `JtSceneBuilder`: `shapeLevel()` reads the Shape LOD segment type (7 to 16) of the shapes below each alternative from the late-load properties, so nothing is decoded to choose; `chooseByDetail()` takes the coarsest distinct level (Coarsest) or `ordered[n/2]` of the distinct levels (Medium; the coarser of two). With fewer than two distinct levels (generic type 6, a single level) the established choice stands. The scout pass follows the same choice so the parallel prefetch decodes the level the build will use. A detail choice overrides `lodEyePoint`; a failed alternative still falls back through source order.
- A note is added to the import notes: levels chosen, nodes reduced, nodes without distinct levels.
- Settings, Import Export tab, JT Importer group: Detail Level combo (Finest, Medium, Coarsest). It applies to files opened afterwards. Translations de/es/fr/it done (5 strings).
- `jt_probe --detail-level finest|medium|coarsest`; scene test `selectionTests` gains a detail-level block (order independence, two levels, type 6 and single-level fall-back, levels above 9).
- Not done: showing the level in the UI after import, the interaction-tier work (C5b) and refinement (D).

Owner checks: build and run `jt_scene_tests`; then open A95 and A103 at Finest, Medium and Coarsest and record load time, peak private bytes, triangle count and orbit gpuMs (observations for this document); compare the coarsest view with an independent viewer.

### 9.1 Probe observations (4 November 2026, owner's build of 3 November)

`jt_probe --detail-level <level> <file>` (no GLB output) on A95 and A103, one run per level, nothing else running. Regenerate (output under `out/`; the probe prints every part name, so the output stays local):

```
out/build/ninja_release_vcpkg/tests/jt/jt_probe.exe --detail-level coarsest <path to the file> > out/jt-research/c5/probe-A95-coarsest.txt
```

| File | Level | Meshes | Vertices | Triangles | Read (s) | Decode wait (ms) | Child chosen at every LOD node |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A103 (10.3) | finest | 2,051 | 4,450,421 | 5,844,669 | 13.8 | 11,827 | 0 (2,115 nodes) |
| A103 | medium | 2,051 | 1,913,020 | 2,335,013 (40%) | 2.3 | 1,204 | 1 |
| A103 | coarsest | 2,051 | 1,258,519 | 1,430,137 (24%) | 1.7 | 750 | 2 |
| A95 (9.5) | finest | 2,228 | 6,614,780 | 7,918,872 | 11.2 | 3,268 | 0 (3,185 nodes) |
| A95 | medium | 2,228 | 1,642,872 | 2,281,847 (29%) | 3.6 | 565 | 1 |
| A95 | coarsest | 2,229 | 1,025,064 | 1,351,273 (17%) | 2.9 | 238 | 2 |

- **The child-order hypothesis of section 3 holds in both files:** choosing by segment type picked child 1 for type 8 and child 2 for type 9 at every LOD node, never anything else.
- The finest runs were the first reads of each file, so part of their time is a cold file cache; compare the load times with that in mind. Peak memory, GPU time and the importer notes were not captured by the probe.
- A95 at the coarsest level has one more mesh than the other levels: at least one shape's material split differs between levels, as section 5 (option B) anticipated.

## 10. Owner decisions and the automatic level (7 November 2026)

Answers to section 8:

1. **First step:** C5a, import detail level (already built, section 9).
2. **Is the authored coarse interaction tier (C5b) wanted before lazy refinement (D)? Recommendation: not needed now.** Reasons, from the evidence in this document and not from a measurement of C5b itself: the viewer already builds a generated interaction tier (section 4); C5b would add about 20 to 40 percent more vertices per shape and a rule to match material splits between levels (A95 at Coarsest already shows one mesh more than the other levels, section 9.1); and the large saving for heavy models comes from loading at Medium or Coarsest (17 to 29 percent of the triangles, load 4 to 7 times faster on A95/A103, section 9.1), which the Automatic level now selects by itself. Revisit only if profiling a finest-level model shows an orbit cost that the generated tier does not remove. D (lazy refinement) stays tied to the on-demand loading branch.
3. **Detail-level offer:** Finest, Medium, Coarsest **and Automatic (by model size)**. Built.
4. **MVF and geometry:** unchanged from the earlier assumption (not checked against an MVF written from a JT import).

**Automatic.** `Detail::buildScene` runs one scout pass at Finest before the build and adds up `PolyRangeMax` of every shape it reaches, i.e. the polygon count the shape nodes advertise for the finest alternative (shared shapes once; the pass opens the part files, which the build reuses). It picks Finest up to 8 million polygons, Medium up to 25 million, Coarsest above. The limits are untuned guesses chosen so A95 (7.9 million at Finest) and A103 (5.8 million) stay at Finest and very large vehicle assemblies drop a level or two; they are constants `kAutoFinestMaxPolygons` and `kAutoMediumMaxPolygons` in `JtSceneBuilder.cpp`. A model whose shape nodes advertise no counts (older files) counts as zero polygons and loads at Finest. The chosen level and the total are written to the import notes ("JT detail level automatic: ..."), and from then on the rest of the build sees the resolved level, so the existing detail-level note and behaviour apply. `jt_probe --detail-level automatic` and `MODELVIEWER_JT_DETAIL_LEVEL=automatic` select it. Tests: `selectionTests` in `jt_scene_tests.cpp` (no counts, 1,000, 10 million and 30 million advertised polygons). Not built or run; whether the shape nodes of production files advertise polygon counts at all has not been checked on a real file, so the owner should open A95 and a large assembly at Automatic and read the import note.
