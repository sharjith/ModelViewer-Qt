# Simulation Results Visualisation - Design

Status: **DRAFT for review** (2026-09-24). Branch: `feature/simulation-results`. No code exists yet.

ModelViewer is a viewer, not a creator. This feature lets users load the *results* of FEA/CFD/thermal
runs made elsewhere and inspect them with ModelViewer's presentation strengths (PBR/path-traced
rendering, section, measurement, annotation, report export). It is **read-only**: no solving, no
general filter pipeline, no expression language. That boundary is what keeps it a viewer and not a
post-processor.

## 1. Decisions already taken

| Topic | Decision |
|---|---|
| Domain priority | Structural, then thermal, then CFD |
| Scale | Production use; assume millions of cells |
| Scope of software | Open-source solvers only (CalculiX, Code_Aster, Elmer, FreeCAD FEM, OpenFOAM, MOOSE); no vendor-SDK formats |
| First delivery | Result arrives as its **own mesh**; attaching to an existing CAD mesh is a later phase |
| Interior visualisation | Deferred, but the data model must not preclude it (see 4.4) |
| Persistence | Two layers: portable glTF-form snapshot in MVF, plus optional link to the raw dataset (see 8) |
| Units | Mandatory (see 7) |
| UI | "Simulation" tab in the **bottom group** of the Document side dock; timeline overlay in the viewport; no toolbar tab (see 9) |
| Rendering | Scalar as a vertex attribute + shader colormap lookup, **not** per-vertex RGB (see 6) |

## 2. Non-goals

Solving or meshing; editing results; user-defined expressions; volume rendering; streamlines and
iso-surfaces in the first phases; proprietary formats (.odb, .rst, OP2); attaching results to CAD
geometry in Phase 0-2.

## 3. Architecture overview

```
 file (.vtu/.vtk/.frd/...)          off-thread reader (ResultReader)
            |                                   |
            v                                   v
   ResultDataset  <----------------------  fields, cells, steps, units
   (topology + fields + steps)
            |
            |  boundary extraction (face hash)          ResultView (field, component, range,
            v                                            colormap, bands, deform scale, step)
   boundary SceneMesh  <---------------------------------------- drives shader uniforms
   (own scene-tree node, unlit)
```

New, deliberately separate pieces:

- **`ResultDataset`** - the source of truth. Independent of `SceneMesh`. A future Simulation
  workbench is built on this, not on scene meshes.
- **`ResultReader`** interface with one implementation per format; runs off-thread with progress and
  cancel (same pattern as the MVF preparation worker and `AnalysisComputeSession`).
- **`ResultView`** - the small piece of view state that decides how the dataset is currently shown.
- The displayed mesh is an ordinary `SceneMesh` (boundary surface) so selection, transforms,
  measurement, section, annotation and rendering keep working.

## 4. ResultDataset model

### 4.1 Contents

- **Nodes**: positions (float, in file length unit) + original node ids.
- **Cells**: type, connectivity, original cell ids. Supported types in order: tri3, quad4, tet4, hex8,
  wedge6, pyramid5, then quadratic tri6, quad8, tet10, hex20, wedge15; polyhedra in the CFD phase.
- **Fields**: name, association (node / cell), component count (1 scalar, 3 vector, 6/9 tensor),
  quantity kind, file unit, and per-step data.
- **Steps**: ordered list of (index, time value, label). Topology is shared across steps in the first
  phases; per-step meshes (moving/adaptive) are out of scope.
- **Metadata**: source file path + size + modification time, solver name if known, length unit.

### 4.2 Memory policy

Topology stays resident. Field data is loaded **per step on demand** with a small cache (3-5 steps).
The format never imposes a step cap; memory does. Arrays are plain contiguous floats (struct of
arrays), not per-element objects.

### 4.3 Derived fields (Phase 1)

Computed at load or first use from tensor/vector components: vector magnitude and components, von
Mises, Tresca, maximum/minimum principal. They inherit the source field's unit. Nothing beyond this
list without a new decision.

### 4.4 Foundations kept for the deferred interior work

Retain from day one:

- the full cell topology with original node and cell ids;
- for each boundary triangle, its source cell and local face;
- for each boundary vertex, its node id.

These also give a probe that reports the real node id, and are what attach-to-CAD will need later.

## 5. Boundary extraction and display mesh

Face-hash extraction: hash every cell face by its sorted node ids; faces occurring exactly once are
boundary faces. A 5M-tet mesh yields a boundary of a few hundred thousand triangles - that, not the
volume, is what is rendered and orbited.

- **Quadratic elements** are tessellated into linear sub-faces so curved edges display correctly.
- **Node data**: shared vertices, smooth interpolation.
- **Cell data**: unshared vertices, one flat value per face (same shader path).
- Shell/surface cells (tri/quad files, e.g. thin-walled structural models) are displayed directly.
- Extraction runs off-thread; the result is uploaded like any prepared MVF mesh.

## 6. Rendering

- The raw scalar is a dedicated **vertex attribute**; range, colormap and band count are shader
  uniforms. Changing any of them is a uniform update, not a recolour.
- The GPU interpolates the **scalar**, then maps it through a 1D colormap lookup, so colours are
  correct on coarse meshes and contour bands fall exactly at iso-values, inside triangles.
- Result meshes render **unlit** (`KHR_materials_unlit` in glTF terms) so the legend stays accurate.
- Range modes: automatic across all steps (default - keeps animation frames comparable), automatic
  per step, user-set. Optional log scale. Out-of-range values clamp to the end colours with an
  optional marker.
- Colormaps: one perceptually uniform default, classic rainbow, blue-white-red. The existing Surface
  Analysis colormaps are reused, not duplicated.
- Deformation: node displacement field x user scale factor, applied in the vertex shader (original
  positions stay untouched).
- **Relation to Surface Analysis:** that overlay is CPU per-vertex RGBA
  (`RenderableMesh::setAnalysisOverlayColors/FlatColors/SubTriangleColors`). The new path is added
  alongside; whether Surface Analysis later migrates to it is a separate decision.
- One colour-mapped overlay per mesh: enabling a result colouring clears any Surface Analysis overlay
  and vice versa (same rule as the Zebra Stripe fix).

## 7. Units

Correct units are a hard requirement; a wrong stress unit is exactly the error a production tool must
not make.

- Each field stores a **quantity kind** (stress, displacement, temperature, velocity, pressure, ...),
  a **file unit** and a **display unit**, separately.
- Conversions are more than scale factors: temperature (K / degC / degF) is affine.
- Sources: read a unit hint where the format carries one (VTK field data can); otherwise ask on load
  with a guess. Geometry length follows the existing "unconfirmed default unit" policy
  (`LengthUnits.h`).
- **Never guess silently** for mixed-unit files: require an explicit choice per field.
- Derived fields inherit the unit. Legend, probe and reports use the display unit.

## 8. Persistence (MVF / glTF)

Two layers, in line with MVF being glTF-spec JSON + a binary chunk:

1. **Portable snapshot inside MVF** - the boundary surface plus fields, viewable elsewhere:
   - scalar as a normalised `TEXCOORD_n` plus a 1D colormap texture (so other glTF viewers still show
     it; in-app the raw scalar and live range are kept);
   - time steps as **morph targets** carrying scalar and displacement deltas, driven by a weights
     animation (reuses the existing MVF morph-target serialisation);
   - metadata (fields, kinds, units, ranges, step times, solver, source reference) in an `extras`
     block with a vendor-prefixed name such as `MV_simulation_result`.
2. **Optional link to the raw dataset** (path, size, mtime), kept outside MVF. Needed for the future
   interior view and attach-to-CAD. If the file is missing on reload, prompt to relocate; the snapshot
   still opens.

Constraints and caveats:

- Morph targets load eagerly, so the snapshot **caps steps (default 100)** and subsamples evenly on
  save, telling the user. Warn if the snapshot would exceed a size budget (~500 MB).
- To verify against the glTF 2.0 spec during Phase 1: that morph targets may carry `TEXCOORD_n` /
  `COLOR_n` deltas as assumed above. If not, fall back to per-step scalar accessors in `extras`.
- Raw volume results are **not** embedded in MVF.
- Document the new keys in `docs/mvf_format_spec.md` when implemented.

## 9. UI

- **Simulation tab in the Document side dock**, bottom group (with Selections and States). Holds
  everything: open/close, dataset info, fields and units, range mode and values, colormap, legend,
  contours, deformation scale, derived fields, min/max markers. Empty state: a hint plus an
  "Open Result..." button. The tab is always present; controls are inactive until a result is
  selected. The dock switches to it when a result loads.
- **Simulation menu**: Open Result..., Close Result, Export snapshot. Open also works through
  File > Open with format detection.
- **Probe**: always-on hover readout while a result is selected (value, unit, node id), reusing the
  Surface Analysis hover readout.
- **Timeline overlay** at the bottom of the viewport, shown only for multi-step results: play/pause,
  previous/next, slider, step and time, loop, speed. It has its own visibility because the viewport
  toolbar auto-hides and is too narrow for a slider.
- **No toolbar tab** (few true commands). `TabbedViewportToolbar` can gain one later.
- Time steps are not merged into the Animations tab (they are not user-authored animations).
- Icons for light and dark themes and translations (de/es/fr/it) are part of the work, not an
  afterthought.

## 10. Threading

Read, decompress, tessellate and extract off the UI thread with a progress bar and cancel; upload
on the main thread. Same shape as MVF loading. Nothing may block the UI at multi-million-cell scale.

## 11. Formats

| Order | Format | Notes |
|---|---|---|
| 1 | VTK XML and legacy (.vtu, .vtk) | De facto interchange; also supports polyhedra. Own reader, no VTK dependency. XML: inline, appended raw and zlib-compressed data; legacy: ASCII and binary. |
| 2 | CalculiX .frd | ASCII, documented, produced by FreeCAD FEM. Binary .frd is out of scope at first. |
| 3 | Exodus II, CGNS, OpenFOAM | Needed for thermal/CFD breadth; Exodus and CGNS need HDF5/NetCDF - evaluate the dependency then. |

Write own readers rather than adding VTK as a dependency. The project is GPLv3; third-party GPL
converters (for example ccx2paraview) are useful references for FRD quirks but code should not be
copied without deliberate review.

## 12. Testing

- Readers and the dataset/extraction code are **GUI-free** and unit-testable.
- Hand-built tiny VTU/VTK fixtures with known values: one tet, one hex, a tet10 (quadratic), a
  cell-data field, a vector field, a two-step file, and a mixed-unit case.
- Real data: generate locally with FreeCAD FEM (the installed FreeCAD 1.1 ships `ccx.exe` and
  `gmsh.exe`), plus the downloadable sets listed in the accompanying test-data note.
- Round-trip tests: dataset -> MVF snapshot -> reload; range and unit preserved.

## 13. Phases

| Phase | Scope | Exit criterion |
|---|---|---|
| 0 | `ResultDataset`, VTU reader, boundary extraction, node scalar via GPU colormap, Simulation dock tab, MVF snapshot | A tet mesh with one node scalar loads, shows the boundary with a legend, a probe reporting correct value/unit, and round-trips through MVF |
| 1 | Legend/range/contours/probe/min-max, time steps + timeline, displacement deformation, .frd reader, derived structural fields, unit dialog | A CalculiX static and a thermal transient result behave correctly, incl. von Mises and time scrubbing |
| 2 | Cell data, quadratic elements, vector glyphs, data-aware section through the volume | Interior of a solid shown with correct field values on the cut |
| 3 | Exodus/CGNS/OpenFOAM (polyhedra), thermal (isotherms) and CFD (streamlines, iso-surfaces) extras | An OpenFOAM tutorial case loads and displays |
| 4 | Simulation workbench, attach results to CAD meshes | - |

## 14. Risks and open items

- **Scale:** the boundary of very large models may still be heavy; a decimation/LOD option may be
  needed for interaction.
- **Data-aware section** is the hardest single piece: existing capping is closed-mesh only and not
  field-aware. The dataset retains the volume so it stays possible.
- **Morph-target size** limits the MVF snapshot (see 8).
- **Format quirks** (FRD element numbering and quadratic node order, VTK cell-type ids) need real-data
  testing, not just spec reading.
- **Unconfirmed numbers:** the 100-step snapshot cap, 3-5 step cache and 500 MB budget are proposals.
- **Whether Surface Analysis migrates** to the scalar+lookup path is undecided.
