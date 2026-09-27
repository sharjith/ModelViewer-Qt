# General-Purpose 3D Data Plotting - Implementation Blueprint

Status: **PROPOSAL for implementation** (2026-09-27). Nothing built yet. Branch: `feature/3d-data-plotting` (off `dev`, a
NEW branch - this is deliberately NOT simulation-results work, see [[project_general_3d_data_plotting_idea]]: no
`ResultDataset`, no steps, no probe - it plots arbitrary data, not a solver result). Companion: matplotlib's `mplot3d`
gallery (https://matplotlib.org/stable/gallery/mplot3d/index.html), whose ~47 examples this blueprint deliberately does
NOT reproduce 1:1 - see section 2's deduplication.

## 1. What this is, and isn't

A mode to plot arbitrary 3D data (a function, a CSV, typed-in points) the way matplotlib's `mplot3d` does, but rendered
with this app's own PBR lighting and camera instead of a basic software renderer - the differentiator identified when
this idea was first raised.

**Non-goals for v1** (say so explicitly if scope creep starts):
- No live data sources (a plot is built once from an import/entry, not re-polled or animated from an external feed).
  matplotlib's own "Animate a 3D wireframe plot" example is explicitly out of scope for the same reason.
- No subplot/multi-figure layout (matplotlib's "3D plots as subplots", "2D and 3D Axes in same figure") - one plot's
  content lives in one ModelViewer document, the same way one simulation result or one imported CAD file does. Multiple
  plots side by side, if ever wanted, is a compare-mode-style feature to consider much later, not v1.
- Function entry (`Z = f(X,Y)`) needs an expression evaluator; **none exists in this codebase today** (checked: no
  parser/evaluator class anywhere). Decide at the start of implementation whether to (a) vendor a small header-only
  expression library (e.g. exprtk, muparser - check licence compatibility first) or (b) ship v1 with CSV/typed-point
  import only and defer function entry to a follow-up. Do not build a hand-rolled parser without raising this choice
  first - it is a real scope decision, not a detail.

## 2. The 8 primitives + 3 axis features (deduplicating matplotlib's ~47 examples)

Agreed with the user (2026-09-27): most of the mplot3d gallery is the same handful of primitives shown with different
data, colour, or matplotlib-figure framing (subplots, projection type - both already free in a real 3D app via the
existing orbit camera and orthographic/perspective toggle). Build these, not 47 separate features:

| Primitive | Data shape | Reuses (see section 5) |
|---|---|---|
| **Surface** | a regular X/Y grid of Z, or a triangulated/unstructured point set (Delaunay) | Mesh/material pipeline, `AnalysisColorRamp` |
| **Wireframe** | same as Surface - a RENDER MODE, not separate data | Existing mesh wireframe rendering |
| **Contour / iso-lines** | same as Surface - iso-levels of Z, in-place or flattened onto a reference plane | `ResultSlice`'s cutter (the field is Z instead of a simulation scalar) |
| **Line / curve** | an ordered list of (x,y,z), optionally with a fill-to-plane or per-point error bars | New: a polyline renderer (see the line-cell tube renderer in `ResultBoundary.cpp` for a "thick line" precedent, or a plain `GL_LINE_STRIP` for thin ones) |
| **Scatter / stem** | an unordered list of (x,y,z), each optionally with a line down to a base plane | New: a point-cloud renderer (mirrors `SimulationGlyphs`' site-sampling shape, without the field-driven arrow direction) |
| **Bar / histogram** | a 2-D grid of bar heights (bar chart), or binned counts (3-D histogram of 2-D data) | Trivial: one `Cube`-like box mesh instance per bar, existing primitive geometry |
| **Voxel / volumetric (occupancy)** | a 3-D boolean/occupancy grid, optionally with per-voxel RGB | **`SimulationVolumeController`'s ray-march** (`docs/simulation_volume_rendering_blueprint.md`), occupancy as the field, a step transfer function (opaque above 0.5, transparent below) instead of the simulation's continuous opacity curve |
| **Quiver (vector field)** | points + a 3-vector at each | **`SimulationGlyphs`/`SimulationGlyphController`'s arrow renderer**, directly - the site is the point, the vector is given rather than sampled from a field |

Axis/viewer features (not separate plot types, but real, needed by every primitive above):
- **Log/symlog axis scaling** - a genuine feature (matplotlib's "Scales on 3D"): the data is transformed before being
  turned into geometry, not a rendering trick.
- **Reference planes** (XY/XZ/YZ, at the axis minimums, matplotlib's default look) - reuses the existing floor-plane/
  grid rendering infrastructure.
- **A labelled 3-D axis box with tick marks and numbers** - genuinely new (checked: the existing orientation trihedron
  widget is a small corner gizmo, not a full labelled axis box with ticks) - see section 4.

Explicitly deferred/merged into the above rather than built separately: 3D errorbars (a Line/Scatter option), fill-
between/fill-under (a Line option), text annotations (adapt the existing CAD Annotation system rather than building a
new one), 2D images in 3D (a textured flat polygon - a Polygon-primitive variant, itself lower priority than the 8
above since nothing in the gallery singles it out as commonly needed).

## 3. Architecture

**A plot's content is scene content in an ordinary ModelViewer document - NOT a new document type or a new top-level
concept.** The same way a simulation result or an imported CAD file becomes scene nodes in whatever document is open,
"Add 3D Plot..." (a new menu action, `on_action...` alongside `on_actionNew_triggered()`/`openSimulationResult()`-style
entry points) builds one or more scene nodes from the plot data and adds them to the active document. This is a real
simplification versus treating a plot as its own document type - reuses the entire existing save/undo/scene-tree/
material infrastructure for free, and matches the precedent both CAD import and simulation results already set.

```
Data source (CSV file / typed points / function-on-a-grid, once an evaluator choice is made - section 1)
        |
        v
Plot3DDataset (GUI-free, Core) - one struct per primitive kind (SurfaceData, LineData, ScatterData, BarData, VoxelData,
        |                        QuiverData), plus shared AxisConfig (ranges, log/linear, labels, tick count)
        v
build*Mesh() / build*GlyphSet() / build*VolumeGrid()  - one builder per primitive, each producing the SAME kind of
        |                                                 output an existing renderer already knows how to draw
        v
Existing renderers, reused per table in section 2, PLUS one new Plot3DAxisController for the axis box/ticks/reference
        planes (shared by every primitive)
```

## 4. New files

- `include/Plot3D/Core/Plot3DData.h` + `.cpp` (GUI-free, `tests/CMakeLists.txt`-added like every `SimulationXxx.cpp`
  Core file already is) - the per-primitive data structs, a CSV parser (columns -> the right struct depending on
  chosen primitive), and `AxisConfig` (ranges/scale/labels), following [[project_source_folder_layout_decisions]]'s
  module-folder convention (`Plot3D/`, not folded into `Simulation/` - this is explicitly not simulation work).
- Per-primitive mesh/data builders, GUI-free, tested the way `SimulationCharts.cpp`/`SimulationVolume.cpp` are: turn a
  `Plot3DData` struct into vertices/indices (Surface/Wireframe/Bar/Polygon), a `GlyphSet`-shaped point/vector list
  (Scatter/Stem/Quiver - reusing `SimulationGlyphs.h`'s own `GlyphSet` type directly is worth considering, since a
  quiver arrow IS a vector glyph), a polyline (Line), a contour cut (reusing `ResultSlice.h`'s types), or a
  `VolumeGrid` (reusing `SimulationVolume.h`'s own type directly - Voxel is a `VolumeGrid` with a step transfer
  function, nothing new needed there beyond the occupancy-to-grid builder).
- `include/Plot3D/UI/Plot3DAxisController.h` + `.cpp` - the one genuinely new GL piece: a labelled axis box (three
  edges meeting at the origin corner matplotlib defaults to, tick marks at `AxisConfig`'s computed "nice" intervals -
  see `SimulationChartWidget.cpp`'s `niceStep()` for the exact same problem already solved for 2-D charts, reuse the
  algorithm), numeric labels (reuse whatever text-rendering the existing trihedron/measurement dimension labels use -
  check `TextRenderer`/`AxisTextRenderer` before writing a new one).
- `include/Plot3D/UI/Plot3DPanel.h` + `.cpp` - mirrors `SimulationPanel`'s shape: primitive-type choice, data-source
  choice (import/paste/function), axis options, per-primitive styling (colour ramp, wireframe on/off, etc.).
- A "Add 3D Plot..." menu entry + `ModelViewer` methods, mirroring `openSimulationResult()`'s shape but building scene
  nodes directly rather than a `SimulationSession`.

## 5. Reuse checklist (the main point of this design - most of the hard parts already exist)

- `AnalysisColorRamp` - colour-by-value for Surface/Bar.
- `SimulationGlyphs.h`'s `GlyphSet` + `SimulationGlyphController` - Quiver directly, Scatter/Stem with a trivial
  variant (no direction vector needed for plain scatter, a short vertical vector for a stem).
- `ResultSlice.h`'s cutter - Contour (Z as the field instead of a simulation scalar - the SAME "cut where a per-node
  signed function changes sign" algorithm, so this is a genuine near-zero-cost reuse, not just architecturally
  similar).
- `SimulationVolume.h`'s `VolumeGrid` + `SimulationVolumeController` + `volume_raymarch.frag` - Voxel/volumetric,
  wholesale (an occupancy grid instead of a resampled field, a step-function transfer function instead of a curve).
- `SimulationChartWidget.cpp`'s `niceStep()` - tick-interval computation for the new axis box.
- The existing `Cube`/box primitive geometry (`Geometry/` module) - Bar/histogram.
- The existing mesh/material/render pipeline (`MeshGeometry`/`RenderableMesh`/`SceneMesh`) - Surface/Wireframe/Polygon,
  the same way every other mesh in the app is drawn; no new rendering path needed for these three at all.
- The scene-node/undo/save infrastructure - a plot is a scene node like any other; do not build a parallel document/
  session concept (see section 3).

**What is genuinely new, not a reuse:** the CSV/data-import parsing itself, the per-primitive `Plot3DData` structs,
the axis box + tick/label rendering (`Plot3DAxisController`), the panel, and (pending section 1's decision) a function
evaluator.

## 6. Suggested implementation order (one phase per commit, build+test between each - the same mechanics that worked
for both the folder restructuring and the simulation charts/volume-rendering work)

1. Resolve section 1's expression-evaluator decision (or explicitly defer function entry, ship CSV/typed-point import
   only for v1) before writing any other code - it affects the `Plot3DData` import surface.
2. `Plot3D/Core/Plot3DData.h/.cpp` (data structs + CSV import) + tests. No GL yet.
3. `Plot3DAxisController` (the one new GL piece) - verify visually with a hardcoded test range before wiring real
   data, the same way the volume blueprint suggested for its ray-march shader.
4. Surface + Wireframe (they share a mesh builder, differing only in a render-mode flag) - the highest-value, most
   demanded primitive, and the one that exercises the axis controller end-to-end first.
5. Contour (reusing `ResultSlice`) once Surface exists to contour.
6. Scatter/Stem and Line/Curve (both simple, independent of Surface).
7. Quiver (reusing `SimulationGlyphController` directly - should be the fastest of all, given zero new rendering code).
8. Bar/histogram.
9. Voxel/volumetric (reusing `SimulationVolumeController` directly).
10. `Plot3DPanel` + the "Add 3D Plot..." menu entry, wiring all of the above together; polish (log-scale axes,
    reference planes, styling options) last.
