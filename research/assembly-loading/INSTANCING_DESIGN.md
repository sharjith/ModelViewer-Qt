# Shared-definition instancing: design proposal

Status: **proposal only, no code written. Stages B and C are conditional.** Owner decision 2026-10-28: do the cheaper options first on `feature/JT-importer` (per-definition preparation, removing duplicate CPU vertex copies, culling if draw calls dominate). Stages B and C start only if production-model measurements justify them, on a separate branch.

**Owner observation 2026-10-28: the production 34 000-part model has very few repeating parts.** Stages B and C would therefore gain little on the target workload and are shelved; revisit only for assemblies that repeat parts heavily. Stage A is implemented (`7083b79`) and measured at about 17% of the optimise stage on a sample with 1537 occurrences of 913 meshes.

Go/no-go for Stages B and C (thresholds to be set by the owner before the measurements are read): memory above what the production machines can hold after the cheaper options, or orbit frame time / hover latency still unacceptable, and a high share of parts that repeat a definition.
Date: 2026-10-28. Branch context: `feature/JT-importer` (applies to every importer that produces shared definitions: JT, 3DXML, STEP/IGES/BREP via XCAF).

## 1. Problem

Large assemblies repeat the same part many times. In the JT sample used for the measurements below (neutral ID `JT-S1`, 371 files) there are 913 unique meshes referenced by 1537 occurrences, 8.49 M vertices after expansion. The importer pays for every occurrence in full:

- `AssImpModelLoader::processMesh` bakes the occurrence's node transform into the vertices (`normalMatrix`, `hasNegativeScale`, around lines 1749-1990 and 2179), so two occurrences of one definition have different vertex data and must be optimised, uploaded and stored separately.
- Memory per vertex is about 256 B: `SceneMesh::_vertices` (128 B `Vertex`), `RenderableMesh` CPU copies (about 64 B), GPU buffers (about 64 B), plus the world-space caches in `MeshInstanceState`.
- Preparation work (meshoptimizer, LOD1, feature-edge extraction) is repeated per occurrence.

Observation (measured): after `cae128c` the end-of-load private bytes on `JT-S1` were 8.28 GB. Hypothesis (not measured): with geometry shared per definition, the vertex-proportional part would drop roughly in proportion to the occurrence/definition ratio (1537/913 here; much higher on assemblies with fasteners, which is where a 34 000-part model is expected to gain most). This must be measured on the production model before it is relied on.

## 2. Existing building blocks

- `MeshInstanceState` already separates local geometry from transform layers (user TRS, exploded-view TRS, scene render TRS, explosion offset) and composes a combined render transform. Local-space AABB is already computed once.
- Its transformed-vertex caches (world-space points/normals/tangents/bitangents) are what picking and bounds use. These are the O(N) per-occurrence copies.
- Importers already know definition identity: JT part files, 3DXML representation ids, XCAF part definitions (`releaseConvertedParts`, the per-definition conversion cache).

## 3. Staged proposal

The stages are independently shippable; each ends at an owner test gate.

### Stage A - per-definition preparation (no change to scene content)

Optimise once per definition in local space (meshopt, LOD1, feature edges), then for each occurrence apply the transform to the already-optimised result.

- Saves preparation CPU time; does **not** save memory.
- Output per occurrence is unchanged in structure, so picking, bounds, capping and measurement are untouched.
- Risk: low. Equivalence can be proven with the existing `worker.scene-hash` checkpoint plus a vertex/index hash after preparation.
- Caveat: meshopt reordering is deterministic for identical input, but transforming before versus after optimisation can change vertex welding on non-uniform or mirrored transforms; mirrored occurrences (negative determinant) need winding flipped after the fact, as `hasNegativeScale` does today.

### Stage B - shared CPU geometry, per-occurrence transform

`SceneMesh` stops owning baked world vertices. Occurrences of one definition hold a `shared_ptr` to a local-space geometry block and carry only their matrix.

Required changes:
- Loader: emit local-space meshes once per definition plus node transforms, instead of baking in `processMesh`. Assimp already models this (one `aiMesh` referenced from several `aiNode`s); the baking is our choice, not a format limit.
- Normals: the normal matrix moves to the render transform; mirrored occurrences need a per-instance front-face flip.
- `MeshInstanceState`: world-space caches must be computed lazily and only for what needs them (see section 4), not eagerly per occurrence.
- Per-instance data that differs even when geometry is shared: material override/variants, colour, visibility, selection, exploded-view offsets. These stay on the occurrence; only vertex/index data is shared.

### Stage C - GPU sharing and instanced draws

One VAO/buffer set per definition; draw occurrences with a per-instance model matrix (instanced array or uniform per draw). Start with simple repeated `glDrawElements` sharing the buffers (removes GPU memory, keeps the draw count), then `glDrawElementsInstanced` for runs with identical material and state (reduces draw calls, which is the likely cost for mouse response on 34 k parts, to be profiled on the production system).

## 4. Consumers that need a decision

Each of these currently assumes world-space vertices exist per occurrence.

| Consumer | Today | With sharing |
|---|---|---|
| Picking / hover | Tests world-space cached triangles | Transform ray into local space and test the shared mesh (cheaper than caching); consider a per-definition BVH |
| Bounds / fit | World cache AABB | Transform the 8 local AABB corners; exact enough for fit, but tight bounds for section/measure need care |
| Section capping | Operates on world geometry | Needs the cut plane in local space per occurrence, or a world-space fallback for sectioned views |
| Measurement, geodesic, wall-thickness, surface analysis | Assume a mesh with world positions | Materialise a world-space copy on demand for the selected occurrence |
| Mesh editing (split, merge, union, shrink wrap, repair) | Mutate the occurrence's vertices | Copy-on-write: un-share the occurrence before editing |
| Export (glTF, MVF, SceneGraphExporter) | Per-occurrence meshes | glTF and MVF3 can express shared meshes natively; first version may expand on export to stay safe |
| Ray tracing (`RtEmbreeScene`) | World-space triangles | Embree supports instancing of one geometry; first version may expand |
| Exploded view / animation | Per-occurrence TRS | Unchanged (already matrix-based) |

Copy-on-write is the central rule: anything that mutates or needs world data first materialises it for that occurrence only.

## 5. Risks and open questions

- Mirrored occurrences and non-uniform scale: front-face winding and normal matrix must be handled per instance.
- Texture-mapped or vertex-coloured parts that differ per occurrence while sharing geometry: colours are in the vertex stream today; occurrence-specific colours would force un-sharing or a per-instance colour override.
- Memory arithmetic only pays off when definitions repeat. Assemblies of mostly unique parts gain nothing from Stages B/C (they still gain from the earlier slimming).
- Whether the 34 k model's cost is dominated by vertex memory, preparation time or draw calls is **unmeasured**. Profiling on the production system should choose the starting stage.
- Merge options (`MODELVIEWER_JT_MERGE_PART_MESHES`) combine meshes of a part; interaction with sharing needs defining (merge per definition, then share).

## 6. Test and equivalence plan (owner runs builds and tests)

1. Stage A: compare `worker.scene-hash` and a post-preparation vertex/index hash with the feature on and off (add an off switch such as `MODELVIEWER_NO_DEFINITION_PREP`, following the existing env-switch convention).
2. Stages B/C: visual comparison on the owner's set (textured, skinned, mirrored, section-capped, exploded), selection and hover, bounds/fit, measurement on a selected occurrence, save/load round trip, glTF export.
3. Metrics to record per run via `MODELVIEWER_IMPORT_DIAGNOSTICS=1`: private bytes at end of load, GL upload time, draw call count, frame time during orbit, hover latency.

Regeneration of evidence: run the viewer with `MODELVIEWER_IMPORT_DIAGNOSTICS=1` and read the newest `import-trace-*.log` in the application log directory; copy any derived measurements under `out/`, not into tracked files.

## 7. Recommendation

1. Profile the production model first (orbit frame time, hover latency, memory split) to learn whether memory, preparation or draw calls dominate.
2. Build Stage A (low risk, proven by hashes) regardless.
3. Do Stage B+C only if profiling shows memory or draw calls dominate, as one dedicated branch, with copy-on-write and an expand-on-export first version.

This document records a proposal; nothing here has been owner-accepted.
