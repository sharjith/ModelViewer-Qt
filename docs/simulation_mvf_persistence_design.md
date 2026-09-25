# Simulation Results in MVF - Persistence Design

Status: **PROPOSAL for review** (2026-09-25). Branch: `feature/simulation-results`. Refines section 8 of
`simulation_results_design.md`, which was written before any code existed.

## 1. Problem

A result document saved to `.mvf` today keeps only the result *mesh*, as an ordinary grey mesh: the fields, time
steps, units, colormap/range, deformation and timeline are all lost, and if "Show deformed shape" was on, the
saved vertices are frozen in the deformed pose. Reopening gives a plain surface with nothing to show.

Goal: save and reopen restores the **full interactive result** (field/component pickers, all steps and the
timeline, units, range and colormap, deformation, probe, markers) **without the original solver file**. It stays
a session file (see the MVF strategy: MVF is not a document format), so a snapshot of what is displayed is
enough - the raw volume data is not embedded.

## 2. Two facts from the code that shape the design

- **Vertex order is preserved.** The result mesh is created with `skipOptimization`, the MVF writer packs the live
  vertex array, and `uploadOneMvfMesh()` rebuilds it with `setMeshData()` (no optimiser). Per-vertex arrays
  therefore stay aligned across save/load with no remapping.
- **Binary data has a precedent.** OCC edge segments are appended to the `GEOM` chunk (`appendBinary` +
  `bufferViews` + `accessors`) and referenced from JSON `extras`; session-level state lives in `mvfSession`. The
  result snapshot follows exactly that shape - no new chunk type.

## 3. What is stored

**The snapshot is the boundary surface, not the volume.** A result is displayed only through its boundary
surface, so the snapshot stores per **surface vertex** (not per solver node):

| Item | Where | Notes |
|---|---|---|
| Shown geometry (positions/normals/triangles) | the normal mesh path | as today; may be the deformed pose |
| Rest positions | accessor (VEC3 float) | 12 B/vertex; lets deformation be turned off after reload |
| Field values per step | accessors (float), one per field x step | scalar/3/6 components as in the dataset |
| Steps | JSON: time, label, time unit | |
| Field metadata | JSON: name, components, component names, quantity kind, file unit, display unit, "unit confirmed", derived-from | |
| Range data | JSON: per field x step min/max over **all solver nodes** | keeps the legend/all-steps range identical to the live result even though interior nodes are not stored |
| View state | JSON: field, component, range mode/values, colormap, bands, step, deform + scale, markers | |
| Source link | JSON: file path, size, mtime | informational now; for a later "reload full data / interior view" |

**Derived fields** (von Mises, principals, max shear) are *not* stored: they are pointwise functions of the
stored stress tensor and are recomputed on load by the existing `ResultDerivedFields` code.

**On load the snapshot becomes an ordinary `ResultDataset`**: nodes = surface vertices, one triangle cell per
surface triangle, fields/steps/units as saved. The boundary surface is then the identity (vertex i = node i).
Everything downstream - panel, legend, timeline, deformation, probe, markers, units - works **unchanged**,
because it only sees a dataset. The panel's mesh line will read "N nodes, M cells" of the *surface*, which is
honest for a snapshot.

## 4. Size control (this is the real constraint)

Values cost `fields x steps x vertices x components x 4 B`. A 1M-vertex result with STRESS (6) + DISP (3) + TOSTRAIN
(6) over 20 steps is about 1.2 GB, so "store everything" cannot be the default. The Save flow therefore offers, for a
document that contains results:

- **Shown field and displacement, all steps** (default; a stress result with 20 steps at 1M vertices is ~0.3 GB -
  still large, so a size estimate is shown before writing)
- **All fields**
- **Geometry only** (today's behaviour, plus the baked colours below)

Caps from the earlier note stay proposals: warn above ~500 MB, optionally cap steps (default 100, even subsample,
reported to the user). Storing values as float32 only (never double).

## 5. Portability: what another glTF viewer sees

The mesh stays a valid glTF-style mesh. The writer additionally bakes the **currently shown colours into
`COLOR_0`** (colormap applied on the fly in the packer; the live mesh is not modified), so other viewers show the
coloured result. Time steps are **not** written as glTF morph targets (the earlier note's idea): they would load
eagerly into the app's morph-target machinery, which the result mesh must not use (its deformation is driven by
`setMeshData`), and the app's own step arrays are smaller and exact. Deviation from `simulation_results_design.md`
section 8, flagged for confirmation below.

## 6. Format and compatibility

`mvfSession.simulationResults`: an array, one entry per result mesh:

```json
{ "meshUuid": "...", "restPositions": <accessor>, "source": {"path": "...", "size": 0, "mtime": 0},
  "steps": [ {"time": 0.5, "label": "", "timeUnit": ""} ],
  "fields": [ {"name": "DISP", "components": 3, "componentNames": ["D1","D2","D3"], "kind": "length",
               "fileUnit": "mm", "displayUnit": "mm", "unitConfirmed": false,
               "stepData": [<accessor|null>, ...], "stepRange": [[lo,hi], ...]} ],
  "view": { "field": 3, "component": -1, "customRange": false, "step": 0, "deform": true, "deformScale": 100, ... } }
```

- Older builds ignore the unknown `mvfSession` key and open the result as a plain mesh (the baked `COLOR_0`
  still shows). No format version bump needed; documented in `mvf_format_spec.md`.
- If the mesh's vertex count no longer matches the snapshot (the user edited the mesh: split, merge, repair...),
  the result session is dropped on load with a message instead of showing wrong data. A pure move/rotate/scale
  of the node is fine (the arrays are per vertex, not positional).

## 7. Load hook

After meshes are uploaded and `mvfSession` is read (where `punctualLightsByFile` is restored), each
`simulationResults` entry whose mesh exists creates a `SimulationSession` (dataset from the snapshot, saved view
state), registers it, and calls `refreshSimulationDisplay()`. `deformApplied` is initialised from the saved view
so the geometry is not re-uploaded. No undo step (load, not edit).

## 8. Implementation phases

| Phase | Scope | Exit |
|---|---|---|
| S1 | GUI-free snapshot codec (`ResultSnapshot`: dataset + view -> JSON + float blobs, and back), field-selection policy, size estimate; unit tests in `result_tests` | round trip equal (values, units, steps, ranges, derived fields recomputed); wrong vertex count rejected |
| S2 | Save: options prompt with size estimate, packer hook (blobs into `GEOM`, `COLOR_0` bake, rest positions) | a saved `.mvf` contains the block; other viewers show colours |
| S3 | Load hook (section 7) | reopen restores fields, steps, timeline, units, deformation, markers |
| S4 | Docs (`mvf_format_spec.md`), edge cases (result + edited mesh, two results in one document) | |

## 9. Decisions needed

1. **Default content**: "shown field + displacement, all steps" with a size estimate and a choice at save time -
   or a single setting instead of a per-save prompt?
2. **`COLOR_0` bake**: yes (portable colours), or leave meshes grey outside ModelViewer?
3. **Rest positions**: store them (12 B/vertex, deformation can be toggled after reload) - or store only the shown
   pose and disable deformation after reload?
4. **Morph targets dropped** in favour of the app's own per-step arrays (section 5): agreed?
5. **Raw-file link**: keep as informational metadata now, and defer any "reload full data" action?
