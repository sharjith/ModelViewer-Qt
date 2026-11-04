# C5 LOD materials as material variants: analysis and plan

4 November 2026, `feature/JT-importer`. Research and design record. Nothing here was built or run beyond the read-only tools named below, and nothing is owner-accepted. It follows [C5 runtime LOD design](JT_C5_RUNTIME_LOD_DESIGN.md) section 9 (C5a, import detail level) and answers the open question recorded there: the coarsest level shows colours the finest does not.

## 1. Question and idea

At Coarsest the viewer shows extra colours (pure red, blue, white and others) that Finest does not. The importer shows each alternative with the materials the file stores for it. The owner's idea: the viewer already has a glTF-compliant data model with material variants (`KHR_materials_variants`), so the materials of the other levels can be variants and the finest level's materials can be the default. A level then changes only the geometry unless the user picks a variant, and no authored colour is discarded.

## 2. Evidence

Tool: [`tools/jt/analyse_lod_materials.py`](../../tools/jt/analyse_lod_materials.py), read-only; input is the output of `jt_probe --source-attributes` (the file's own scene graph; nothing decoded; parts counted, never named). Neutral aliases as in [C5 design](JT_C5_RUNTIME_LOD_DESIGN.md) section 3. Regenerate (output under `out/`):

```
out/build/ninja_release_vcpkg/tests/jt/jt_probe.exe --source-attributes <path to the file> > out/jt-research/c5/srcattr-A95.txt
python -I tools/jt/analyse_lod_materials.py out/jt-research/c5/srcattr-A95.txt --output out/jt-research/c5/lod-materials-A95.json
```

Observations (these three files, these exporters only):

| File | Range LOD nodes | Three alternatives, equal shape count | Materials identical across levels | Materials differ | Palette maps on LOD shapes |
| --- | --- | --- | --- | --- | --- |
| A95 (9.5) | 2,228 | 2,228 | 1,820 | **408** | 0 |
| A103 (10.3) | 2,049 | 2,049 | 2,049 | 0 | 0 |
| A109 (10.9) | 2,865 | 2,865 | 2,865 | 0 | 125 |

Detail for the 408 differing nodes of A95:

1. **Structure.** Part, Range LOD, three group alternatives, shapes below. Every alternative of every node holds the same shapes in the same number (2,999 shapes per level). No material sits on a Part, a Range LOD or an alternative group; materials sit on shapes, and on Instance nodes above the Part (2,250 of them).
2. **The 1,820 single-shape nodes** carry one identical material at all three levels.
3. **The 408 differing nodes are all multi-shape parts** (2 to 8 shapes; 168, 170, 30, 31, 6, 2 and 1 nodes by shape count). Alternatives 0 (finest) and 1 (medium) carry **no material on their shapes**: the shapes inherit the style of an Instance above the Part (a grey in 200 cases, white in 200, a few others). Alternative 2 (coarsest) carries **its own material on every shape**, and within a part every shape's colour is different (the distinct-colour counts equal the shape counts). Medium never differs from finest.
4. **Colours.** The 408 parts add 15 colours that no single-shape part uses, among them pure blue on 279 shapes and pure red on 176 parts. Whether these are designed colours (separately coloured bodies recorded only in the coarsest alternative) or exporter markers is **not established**; the file cannot say.
5. **Other files.** A103 and A109 carry the same materials at every level, so for them a level change is already purely geometric. The difference is specific to the exporter that wrote A95.
6. **Today's effect on meshes** (from [C5 design](JT_C5_RUNTIME_LOD_DESIGN.md) 9.1 and the part-merge code): at Finest the shapes of a part share one style and merge into one mesh. At Coarsest the pieces differ only in diffuse colour, so the part merge bakes them into **one mesh with per-vertex colours**; a material swap cannot reach those colours. This is also why A95 has one more mesh at Coarsest.

## 3. What the viewer already provides

Read from `GltfVariantData.h`, `AssImpModelLoader.cpp`, `ViewportWidget.cpp`, `ModelViewer.cpp`, `MaterialVizState.cpp` and `CaptureVariantCommand.cpp`.

- **Data model.** `GltfVariantData` is per source file: `variantNames` plus `meshVariantMappings`, keyed by the `aiScene` mesh index, each entry listing `{materialIndex into aiScene materials, variant indices}`. The mesh's own material is the default.
- **Loader.** The mesh-processing code builds, for every mesh with mappings, a prebuilt `Material` for the default and for each mapped `aiScene` material (`allVariantMaterials`). Only the glTF branch uses glTF-specific processing; other formats go through the same `processAssimpColorAndMaterial` / `processAssimpTextureMaps` that build the default mesh material. For non-glTF files the default key is `mMaterialIndex`. The import hands `getVariantData()` to the scene graph for any format (`ViewportWidget.cpp`); today only the glTF branch fills `_variantData`.
- **Variants on non-glTF files already work** by capture: `CaptureVariantCommand` adds variants to meshes that never had any (it synthesises the Default entry), so the Variants panel, switching, "Set as Default", undo, delete and save are exercised for non-glTF sources.
- **Persistence and export.** MVF stores `variantMappings` in the primitive extras; `AssImpMeshExporter` and `GltfPostProcessor` write `KHR_materials_variants` on glTF/GLB export. So LOD variants would round-trip through the session file and through export for free.
- **Switching** (`ModelViewer::applyVariant`) sets each mesh's material from `materialForVariant`; **a mesh with no mapping for the chosen variant is skipped and keeps whatever it shows**. A mesh that has mappings must therefore map every variant (to its default material where nothing differs), or switching between two variants leaves it stale. glTF's own rule (a primitive not listed for a variant uses its default) is not implemented in `applyVariant`; fixing that is an independent, smaller alternative to the invariant.

## 4. Decision recorded from the owner

Two owner decisions, 4 November 2026:

1. The default material is chosen by a variant, not by the level that happens to be loaded, and the mapping between levels is by position inside one Range LOD node, whose alternatives hold equal shape counts (the earlier worry about matching coarse shapes to fine ones mostly disappears; section 6 lists what must still be checked).
2. **The default is "explicit over inherited"** (after the three-viewer comparison in section 10): for each shape, an explicit material on the shape in any alternative beats a colour that shape would only inherit from an assembly node, because the format's accumulation rule says the more specific attribute wins (JT reference 11.10) and an authored colour is more specific than an inherited one. Where several alternatives carry explicit materials that differ (not seen in the three files), the loaded level's wins. This replaces the first idea of "the finest alternative's materials". The loaded level's own materials, where they differ from the default, are a named variant.

## 5. Design

### 5.1 Behaviour

1. **Default = explicit over inherited** (section 4). For each shape the default material is the explicit material of that shape in the loaded alternative if it has one, otherwise the explicit material of the positionally matching shape in another alternative, otherwise the inherited style as today. A variant is created **only if the loaded level's own materials differ from that default** for at least one shape. Files like A103 and A109 get none and behave exactly as today (no cost, no panel entry).
2. The variant is named for what it is: "As authored in this level" (one variant; the loaded level is the one the file's own accumulation produces, so it is today's rendering). The default is shown on load (`activeVariant = -1`).
3. The mechanism does not depend on the imported level. Importing Finest, A95's parts show the explicit colours (the coarsest alternative's) on the finest geometry and the variant shows the inherited colours the finest alternative authors; importing Coarsest, the default equals the authored materials and no variant is needed for those parts.
4. A setting "Import LOD materials as variants" (Settings, JT Importer; default **on** only if the owner agrees, see section 8) turns the feature off, restoring today's merging and meshes.

### 5.2 Importer (`JtSceneBuilder`)

1. **Style-only pass over the other alternatives.** Add a `styleOnly` mode to `visit()` (same attribute application, so palette/forced/final/ignored semantics are not duplicated) that skips shape decoding, mesh creation, PMI and metadata and appends each shape's resolved `Appearance` to a sink, in visit order. At a LOD node, run it for the finest alternative and for each other alternative that differs in a distinct level. It uses its own node counter so the existing 1,000,000-node and depth limits are not consumed three times, and it **does not follow external partitions** (a node whose alternatives hold unresolved partitions gets no variants and is noted).
2. **Qualification per LOD node.** Variants are planned only when all hold: at least two alternatives, equal shape counts, no palette map in effect on any alternative (`palette.mapping` null and no palette-map attribute on its shapes), no source vertex colours on the shapes (the "colored" meshes), triangle-mesh shapes. Otherwise the node loads as today and the import note counts it.
3. **Plan the loaded alternative.** A plan object (default style per shape, variant styles per shape, a cursor) is attached while the chosen alternative is visited. The shape branch uses the plan's default style (the finest alternative's, positional) for the mesh's material instead of the loaded alternative's own, and records the variant styles. Nested LOD nodes inside the alternative suspend the plan.
4. **Registering.** `material()` already de-duplicates, so each variant style becomes an `aiScene` material index. A parallel vector `meshVariantMaterials[mesh]` (same pattern as `meshStyles`) holds one material index per variant; `Checkpoint`/rollback truncates it with the meshes, and `PartMerger::compact` remaps it. Every planned mesh maps **every** variant, with its default material where nothing differs (section 3 invariant).
5. **Cache keys.** `meshCache`, `groupMeshCache` and `mergedGroupCache` already include the default material; add the variant material ids to the keys so an instance with a different variant never reuses a mesh with the wrong mapping. If a cached mesh is reached with conflicting variant slots, the variants for that mesh are dropped and counted.
6. **Part merge.** Pieces with variant slots are never unified into a per-vertex-colour mesh (it would bake the default colours and ignore the variants). They group by (default material, variant material ids). A95 consequence: the 408 parts stop merging, and the mesh count rises from 2,228 to 2,999 (equal to the shape count; every coarse colour within a part is distinct): +771 meshes (+35%) at Finest. Parts without differing variants merge as before.
7. **Result.** `ImportResult` gains the variant names and the per-mesh mappings (post-compaction mesh indices), plus an import note: levels with variants, shapes affected, LOD nodes skipped and why.

### 5.3 Loader and UI (`AssImpModelLoader`)

1. After the JT read, fill `_variantData` from the result (`sourceFile` = the path the loader uses for the file, `variantNames`, `meshVariantMappings`). The existing prebuilt-material code and the scene-graph registration then apply unchanged.
2. No Variants-panel change is needed; the panel lists the file's variants. The glTF-only `isGltf` branches in the prebuilt-material code stay as they are.
3. Optionally fix `applyVariant` to fall back to the default for an unmapped mesh (glTF semantics); independent of this work.

### 5.4 Not changed

Geometry selection (C5a), measurements and PMI anchors (they refer to the loaded triangles; unaffected), the interaction fast path, export of meshes without variants.

## 6. Risks and open verification

1. **Positional shape match: tested, not contradicted, partly ambiguous (V0, section 7.1).** Equal shape counts hold in all 6,142 LOD nodes of the three files, and for A95's 408 differing nodes the finest shapes carry no materials, so any match gives the same default there. For an exporter whose finest shapes carry different materials, "shape i of one alternative is shape i of the other" is an assumption. V0 compared each shape's untransformed bounding box and stored area between alternatives: no multi-shape occurrence contradicts the position, 73% of A95's and 99% of A103's are unique, and the rest are ambiguous (two shapes indistinguishable by these two numbers). The importer can apply the same test at import time (both values are on the shape node, nothing is decoded) and treat the three outcomes differently; see section 8.
2. **Which colours are right is unknown.** The data cannot say whether the coarsest per-shape colours are real. An independent viewer comparison (owner gate in [C5 design](JT_C5_RUNTIME_LOD_DESIGN.md) section 7) or the owner's knowledge of the source model decides what the default should be for A95. The design makes either choice a one-line default and keeps both reachable.
3. **Mesh count and performance** on A95-like files (+35% meshes at Finest) is a prediction. Measure after the build (draw calls, orbit gpuMs) and keep the setting.
4. **Palette-mapped shapes** (A109: 125 on LOD nodes) are excluded from the first version. They are identical across levels in A109, so nothing is lost there.
5. **Merged/instanced reuse** (section 5.2 item 5) is the most delicate part of the change; it needs a fixture with one part instanced twice under different ancestor materials.
6. **Memory.** One extra `Material` per variant per mesh in `allVariantMaterials` (several hundred bytes each), plus the style-only pass. Both are small next to the geometry; measure with the memory audit on A95.
7. **Export.** A GLB written from such a session carries `KHR_materials_variants`; check it in an independent glTF viewer once (owner gate).

## 7. Staging

| Stage | Content | Gate |
| --- | --- | --- |
| V0 | **Done 4 November 2026.** `jt_probe --lod-shapes` and [`tools/jt/compare_lod_shape_bounds.py`](../../tools/jt/compare_lod_shape_bounds.py): per-shape bounds, area and count ranges for each alternative of every LOD node (A95, A103, A109). Results in section 7.1. | Owner built and confirmed; results recorded |
| V1 | `styleOnly` mode and the plan; variants and mappings in `ImportResult`; part-merge and cache-key changes; import note; setting | Owner builds, runs `jt_scene_tests` |
| V2 | Loader fills `_variantData`; no UI change | Owner opens A95 at each level: Variants panel lists the variant, switching changes only the 408 parts, A103/A109 unchanged |
| V3 | Tests: scene-test fixtures (a Range LOD whose alternatives differ in per-shape material; equal files; unequal shape counts; a palette-mapped shape; one part instanced twice under different ancestor materials) using the existing generators in `tests/jt/` | Owner builds and runs |
| V4 | Optional: `applyVariant` fallback; export check in an independent viewer | Owner |

Each stage is a separate commit after the owner confirms it; nothing is committed or built before then.

### 7.1 V0 commands (owner runs; output under `out/`)

Build only the `jt_probe` target, then, per file (aliases as in the sample map; the output holds boxes and counts only, no names):

```
out/build/ninja_release_vcpkg/tests/jt/jt_probe.exe --lod-shapes <path to the file> > out/jt-research/c5/lodshapes-A95.json
python -I tools/jt/compare_lod_shape_bounds.py out/jt-research/c5/lodshapes-A95.json --output out/jt-research/c5/lodshapes-report-A95.json
```

#### First V0 run (4 November 2026, owner's build with bounds only)

Counts are LOD node occurrences (an instanced part counts once per instance; 3,625 for A95), shape pairs compared against the finest alternative.

| File | Occurrences | Pairs | Agree within 5% of the box diagonal | Agree within 10% | Largest corner difference | Another order matches |
| --- | --- | --- | --- | --- | --- | --- |
| A95 | 3,625 | 8,950 | 8,925 | 8,950 | 8.3% | none |
| A103 | 2,408 | 5,706 | 5,706 | 5,706 | 4.8% | none |
| A109 | 9,054 | 18,108 | 17,611 | 18,108 | 8.7% | none |

Observations: no pair contradicts the positional match, and no node matches better in a different order. The pairs beyond 5% (25 in A95, 497 in A109) are all single-shape nodes, where the position is not in question; a decimated level simply has a slightly smaller box. **The boxes cannot confirm the match where it matters:** in all 233 distinct multi-shape parts of A95 at least two shapes have boxes that agree within 5% (coincident bodies of one part), so a swap of two shapes would not show in the boxes. In A103 this holds for 21 of 268 distinct multi-shape parts; A109 has none. So the positional match is **not contradicted but not yet confirmed** for A95's colour-differing parts. A second discriminator is needed: the shape node's stored surface area and vertex/polygon count ranges. The probe now prints them (`area`, `vertexRange`, `polyRange`; read-only accessors added to the already-modified vendored `JtNode_Shape_Base.hxx`), and the script classes each node confirmed-unique, ambiguous or contradicted. This needed one more `jt_probe` build; the result is below.

#### Second V0 run (4 November 2026, owner's build with area and count ranges)

Same occurrences and pairs as above; a positional pair is *confirmed* when box (5% of the diagonal) and stored area (35% relative) agree, *unique* when no other shape of the finest alternative agrees on both, *contradicted* when the positional pair disagrees on either.

| File | Occurrences | Confirmed, unique | Ambiguous (a swap cannot be excluded) | Contradicted | Multi-shape occurrences: unique / ambiguous / contradicted |
| --- | --- | --- | --- | --- | --- |
| A95 | 3,625 | 3,481 | 119 | 25 | 321 / 119 / 0 |
| A103 | 2,408 | 2,401 | 3 | 4 | 290 / 3 / 0 |
| A109 | 9,054 | 8,552 | 0 | 502 | 0 / 0 / 0 (no multi-shape parts) |

Observations:

1. **No multi-shape occurrence is contradicted** in any file. Every contradiction (25, 4 and 502) is a single-shape node whose decimated level drifted in box (up to 8.7% of the diagonal) or stored area (up to 70% in A109, 34% in A95); with one shape the position is not in question.
2. **The match is confirmed and unique for 73% of A95's multi-shape occurrences (321 of 440) and 99% of A103's (290 of 293).** For the remaining 119 (A95) and 3 (A103) two shapes of the finest alternative are indistinguishable by box and area, so the positional match is consistent with the data but a swap cannot be ruled out.
3. **The area difference between levels is small:** median 2.3%, 90th percentile 7% (A95); tightening the area tolerance to 10% would wrongly reject 86 multi-shape occurrences of A95 and 15 of A103, so 35% is the working value.
4. **Not examined:** whether the 119 ambiguous A95 occurrences include the 408 colour-differing nodes (the probe does not link them to the attribute dump); the vertex and polygon count ranges (printed, not used: they describe the shape across levels, not per alternative).

Regenerate with the commands above; the area options are `--area-tolerance`, `--tolerance`.

Result: see the two runs above. The positional match is not contradicted for any multi-shape part and is unique for most; the plan therefore keeps it, with the policy decision in section 8.

## 8. Decisions for the owner

0. **Decided (owner, 4 November 2026):** the default is explicit over inherited (section 4).
   **Also decided (owner, 4 November 2026): questions 1, 2 and 4 below are answered yes** (setting on by default; the variant is named "As authored in this level"; ambiguous nodes get the positional variant and the count is reported in the import note). Question 3 (the 408 A95 parts stop merging at Finest) is implied by the default rule and accepted with it; measure the mesh-count and orbit cost after V1.
1. Should the setting default to **on** (the default rule and the variant apply automatically where levels differ) or off?
2. Is the variant name "As authored in this level" acceptable?
3. May the 408 A95 parts stop merging into one mesh at Finest (the cost of per-shape variants)? The alternative is to offer the variant only on the coarse geometry, where the pieces are already vertex-coloured, which does not work for a material swap.
4. For the ambiguous nodes (A95: 119 occurrences of 440 multi-shape ones), plan the variant anyway (positional; a swapped colour between two near-identical bodies is possible, the default is unaffected and the variant is opt-in) or skip variants for those nodes (they keep the finest materials, so part of A95's coloured parts lacks the variant)? My recommendation: plan them and report the count in the import note. Contradicted nodes never get a variant.

## 9. Provenance

All figures come from the owner's local production files by neutral alias (no redistribution rights; the alias map and the `srcattr-*.txt` outputs stay under ignored `out/`). Method: `jt_probe --source-attributes` from the owner's build of 4 November 2026, then the script above. Code facts were read from the current `feature/JT-importer` tree.

## 10. Independent viewer observations (4 November 2026)

Owner screenshots of A95 in three viewers (observations from images, not measurements; angles differ; local only, not stored in the repository):

| Viewer | Colours observed |
| --- | --- |
| This viewer, Finest | Panels and floor grey, fixtures yellow, one blue bracket |
| This viewer, Coarsest | Same, plus green cross-members and a green post, small magenta brackets, a red marker on the pillar |
| GrabCAD's web viewer (same file) | Matches this viewer at Coarsest: green cross-members, green post, red marker, blue bracket; wheel-arch covers white/silver |
| Solid Edge 2026 (same JT opened) | Yellow, magenta and white wheel-arch covers, yellow and magenta floor lines; fixtures mostly white/grey; **matches neither** |

Reading (hypotheses, not established):

1. The per-shape colours of the coarsest alternative are not an artefact of this importer: another JT viewer shows the same set, apparently by loading or colouring from the last alternative. The spec leaves the choice to the viewer (section 5.3.8, 11.12; see the specification check in the conversation record: no rule ties materials to a level).
2. This weakens "finest materials as default" for A95: the finest alternative is the one that lacks per-shape colours, and the richer set is the one an independent viewer shows.
3. Solid Edge's colours probably come from the XT B-Rep segments, which carry `SDL/TYSA_COLOUR` attribute definitions in all 60 evenly sampled segments of 2,228 (scratch scan, values not read). That is a separate gap (C6 exact geometry), not addressed by this plan.

Consequence for section 8: the default is an open owner decision with three candidates: finest materials (grey where the finest lacks colour), the coarsest-authored materials on any level (matches the other viewer), or per-level as authored (today).

## 11. V1 as implemented (4 November 2026, built, scene tests pass, real-file checks partly done)

Status: the owner built it and `jt_codec_tests` passed (including `lodMaterialVariantTests`). Real-file checks so far, as reported by the owner:

- The Variants panel lists "As authored in this level" and a GLB export carries the variant (the export log shows 1,290 variant meshes mapped to the as-authored materials).
- Observation, unexplained: right after load the model showed the explicit default colours, then, after some mouse manipulation and with the panel still on Default, it showed the as-authored look. It was not reproducible later. The code paths read (load registration, variants panel, `applyVariant`, export) do not change a mesh material after load.
- Evidence against the default policy: an independent viewer (GrabCAD) shows the loaded level's own materials, i.e. what this document calls "As authored in this level". Whether the default should stay "explicit over inherited" is therefore open again, and the setting can be switched off.
- Unrelated rendering defects found while checking (hover and selection with batched draws and bodies repeated at one position) were fixed separately; see `research/rendering/RENDER_OPTIMISATION_AUDIT.md`.

Checks 2 to 6 below are still to be confirmed on the sample files.

What changed:

- `include/Import/Jt/JtImporter.h`: `Options::lodMaterialVariants` (default on; setting `jtLodMaterialVariants`; `MODELVIEWER_JT_LOD_MATERIAL_VARIANTS=0` turns it off); `ImportResult::variantNames` and `variantMaterialByMesh` (scene mesh index to scene material index of variant 0).
- `src/Import/Jt/JtSceneBuilder.cpp`: a style-only pass (`styleSink` in `visit()`) records each shape's resolved style, whether the shape carries a material of its own, and its stored box and area; `buildPlan()` runs it over every alternative of a LOD node and returns a plan only when the shape counts match, no palette map, nested LOD, Switch or partition is involved, the bodies of a multi-shape node agree (box 5% of the diagonal, area 35%) and at least one shape differs. A planned shape takes the explicit material of another alternative as its mesh style (explicit over inherited); its own style becomes variant 0 (`meshVariantMat`). The mesh cache key includes the variant material; the part merge never merges meshes with different variant materials (they are also excluded from the per-vertex-colour unification) and `compact()` remaps the parallel vector. The import note "JT LOD materials: ..." reports the shapes and nodes affected, the ambiguous nodes and the nodes left alone.
- `src/Import/AssImpModelLoader.cpp`: fills `_variantData` from the JT result (one variant, one mapping per mesh); the existing prebuilt-material code and scene-graph registration do the rest.
- Settings, Debug tab, JT Importer group: checkbox "LOD Material Variants" (de, es, fr, it translated; the five `.ts` files were edited by hand, no lupdate run).
- `tests/jt/jt_scene_tests.cpp`: `lodMaterialVariantTests()` (finest loaded gets the explicit default and an inherited variant; switch off; coarsest loaded gets none; identical materials; different shape counts).
- Probe support from V0 (`jt_probe --lod-shapes`, area/count accessors on the vendored `JtNode_Shape_Base.hxx`) is part of the same uncommitted change set.

Known limits (not defects of the plan, stated so they are not mistaken for results): the counters in the import note also count nodes of an alternative that is later rolled back; a material on an alternative's group node (not on a shape) is not seen as explicit; variants are skipped for textured, colour-overridden or vertex-coloured shapes.

Owner checks after building:

1. `jt_scene_tests` passes, including the new `lodMaterialVariantTests`.
2. Open A95 at Finest: the green, red and blue parts now show by default (as at Coarsest and as in the independent viewer), the Variants panel lists "As authored in this level", and selecting it restores today's grey. Mesh count about 2,999 (was 2,228); compare load time and orbit.
3. Open A95 at Coarsest: no variant for these parts; look unchanged from today.
4. Open A103 and A109: no variant entry, meshes and look unchanged.
5. Save and reopen the session (MVF) and export a GLB: the variant survives; check the GLB once in an independent glTF viewer.
6. The import note appears with the counts (including ambiguous nodes).
