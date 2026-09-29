# General-Purpose 3D Data Plotting - Implementation Blueprint

Status: **IN PROGRESS** (2026-09-27). Phase 1's source decision and Phase 2's Core data/CSV layer are complete on
`feature/3d-data-plotting` (off `dev`, a
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
  parser/evaluator class anywhere). CSV-file and pasted tabular input ship first, but formula plotting is an important
  planned follow-up: select and licence-review a suitable evaluator rather than build one by hand, then pair it with a
  curated preset library for known analytical and statistical surfaces (for example plane, saddle, Gaussian,
  Gaussian-mixture, sinc/ripple, Mexican hat, and bivariate-normal density). Presets must expose named parameters,
  show the formula before building, and generate ordinary Plot3D surface data so they share all normal plot controls.

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
| **Voxel / volumetric (occupancy)** | a 3-D boolean/occupancy grid, optionally with per-voxel RGB | **`SimulationVolumeController`'s ray-march** (`docs/simulation_volume_rendering_blueprint.md`), occupancy as the field, with zero transparent and nonzero values fading in by occupancy |
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
"Add 3D Plot..." (a new action in the Visualization menu, `on_action...` alongside `on_actionNew_triggered()`/
`openSimulationResult()`-style entry points - see section 4) builds one or more scene nodes from the plot data and
adds them to the active document. This is a real
simplification versus treating a plot as its own document type - reuses the entire existing save/undo/scene-tree/
material infrastructure for free, and matches the precedent both CAD import and simulation results already set.

```
Data source (CSV file / pasted tabular points; function-on-a-grid is deferred - section 1)
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
- An "Add 3D Plot..." action in the existing **Visualization** menu (`ui/App/MainWindow.ui`'s `menuVisualization`),
  immediately after Ray Tracing and before the existing debug-only separator/Texture Debugger entry. This menu was
  chosen on 2026-09-27 over a new top-level menu, since Simulation's own menu was reserved for that one distinct
  capability. Add `ModelViewer` methods mirroring `openSimulationResult()`'s shape, but build scene nodes directly
  rather than a `SimulationSession`.

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
the axis box + tick/label rendering (`Plot3DAxisController`) and the panel. A function evaluator is deferred beyond v1.

## 6. Suggested implementation order (one phase per commit, build+test between each - the same mechanics that worked
for both the folder restructuring and the simulation charts/volume-rendering work)

1. **Complete:** defer function entry and ship CSV/pasted-tabular import for v1.
2. **Complete:** `Plot3D/Core/Plot3DData.h/.cpp` (data structs + CSV import) + tests. No GL yet.
3. **Complete (2026-09-27):** `Plot3DAxisController`'s layout math (tick generation, log/symlog transforms, the axis
   box + reference-plane layout - tested in `testAxes()`), PLUS its actual GL rendering: `ViewportWidget::
   drawPlot3DAxisOverlay()` (a `SceneRenderController`-owned line-overlay VAO/VBO, same pattern as
   `drawBoundingBoxOverlay()`, drawing `axisLines`/`tickLines`/`referencePlanes` and projected screen-space tick/
   axis labels via the existing `_axisTextRenderer`), driven by `setPlot3DAxisLayout()`/`clearPlot3DAxisLayout()`.
   A temporary "Preview Axis Box" button originally pushed a hardcoded range for early rendering verification. It
   was removed once real plot builders supplied data-derived bounds; closing the import dialog now leaves the built
   plot's axes intact for the persistent 3D Plot controls to own. Reference planes are drawn as an outline for now
   (no fill/blend yet - a filled, translucent quad can follow when a primitive needs the visual weight).
4. **Complete, awaiting the user's build+visual check (2026-09-27):** Surface + Wireframe. `Plot3D/Core/
   Plot3DMeshBuilder.h/.cpp` builds a triangulated mesh from Surface data - v1 requires a COMPLETE regular X/Y grid
   (nx*ny samples forming a full rectangle, any row order); scattered/unstructured Surface data needing a Delaunay
   triangulation is reported as an error rather than guessed at, and can be added later without changing this
   builder's contract. `Plot3DPanel` gained a primitive combo + X/Y/Z/colour-value column-mapping combos and a
   "Build Plot" button: it parses the CSV, builds the mesh, adds it as an ordinary `SceneMesh`/`SceneNode` to the
   active document (the same direct-insertion pattern `ModelViewer::presentSimulationResult()` uses, no undo command -
   matches the precedent section 3 cites), colours it by value via `setAnalysisOverlayColors()` (same mechanism a
   simulation result's field colouring uses), and points the axis-box overlay at the plot's own data bounds instead
   of the fixed preview range. Wireframe needed NO new code at all - it is the existing per-mesh wireframe display
   mode applied to this same mesh, exactly as this table row always said it would be.
5. Contour (reusing `ResultSlice`) once Surface exists to contour.
6. **Complete, awaiting the user's build+visual check (2026-09-28):** Scatter/Stem and Line/Curve. A first version
   built these as SOLID tube/octahedron geometry (reusing Surface's `SceneMesh`/`setAnalysisOverlayColors()` path
   directly) - the user tried it and found the size both too large AND, more fundamentally, wrong in kind: real 3D
   geometry inevitably looks bigger on screen as the camera zooms in, whereas matplotlib's own scatter/line markers
   are flat, constant-pixel-size regardless of 3D zoom. Rebuilt on the user's own suggested reuse: "Use Line and
   Point primitives instead, like they are read from glTF currently" - `Plot3DMeshBuilder`'s Line/Scatter builders
   are now just flat, unindexed vertex lists, and `Plot3DPanel::buildPlot()` constructs the `SceneMesh` with
   `GL_LINE_STRIP`/`GL_POINTS` as its primitive mode - the exact same native-primitive path `SceneMesh::draw()`
   already uses for glTF point-cloud/line-set import, which draws at a fixed PIXEL size via `glPointSize()`/
   `glLineWidth()` rather than real 3D geometry, so it is inherently zoom-invariant with no new rendering code at
   all. Stem was subsequently added as a Scatter option with a configurable base Z. `Plot3DPanel::buildPlot()`
   dispatches on the chosen primitive to the right builder.
7. **Complete, awaiting the user's build+visual check (2026-09-28):** Quiver, exactly as predicted - reusing
   `SimulationGlyphController`/`GlyphSet` directly needed no new rendering code. `Plot3DMeshBuilder` gained
   `buildPlot3DQuiverSiteMesh()` (Core, GUI-free - just the arrow base positions as a flat point list, same shape as
   Scatter). `Plot3DPanel::buildQuiverPlot()` (UI layer, where the Simulation-module dependency belongs - Core stays
   simulation-free) builds a small `GL_POINTS` anchor `SceneMesh` from that, then a `GlyphSet` whose `anchors` index
   into it (one arrow per site, all 3 anchor slots the same vertex - `GlyphSet`'s own "a node arrow repeats one
   vertex" convention), coloured by magnitude with the same `AnalysisColorRamp` ramp the site markers use.
   `Plot3DPanel` gained U/V/W column-mapping combos (always visible, like "Colour value" is, rather than only shown
   for Quiver). First version used the CSV's own U/V/W values as the arrow length directly (matplotlib's default
   quiver behaviour) - the user found this made the cone heads (`SimulationGlyphController` sizes them as a
   fraction of each arrow's own shaft length) dominate the plot, since a CSV's raw vector units have no reason to
   already be a sensible arrow length for that data's own grid spacing. Fixed to size arrows the same way
   `buildGlyphSet()` already sizes real simulation vector-field arrows: the largest magnitude becomes a fixed
   fraction (6%) of the data's own bounding-box diagonal, every other arrow scaled down from that by its magnitude
   ratio - so arrow (and head) size is always proportionate to the plot, not to the CSV's arbitrary vector units.
8. **Complete, awaiting the user's build+visual check (2026-09-28):** Bar/histogram. `Plot3DMeshBuilder` now
   creates one closed, flat-shaded cuboid per input row, with per-face vertices for hard edges and the row's scalar
   value repeated across the whole bar for uniform colour mapping. Both positive and negative heights extend from
   the selected base. The panel provides optional Base/Width/Depth column mappings with 0/0.8/0.8 defaults, and Y
   may be left unset for a one-dimensional histogram (all bars then use Y=0). The builder rejects non-finite or
   non-positive dimensions and is covered by GUI-free tests for positive/negative geometry, normals and invalid
   widths.
9. **Implemented, awaiting the user's build+visual check (2026-09-29):** Voxel / volumetric occupancy. Sparse
   CSV `i,j,k,occupancy` cells are expanded into a bounded dense grid (missing cells are transparent), then drawn
   by the existing `SimulationVolumeController` through a volume-only SceneMesh proxy. The proxy preserves ordinary
   scene-tree visibility, transforms, deletion and undo while its point geometry is suppressed by the renderer in
   favour of the ray-marched volume. Zero occupancy is transparent and nonzero cells fade in with their supplied value; colour-map
   selection remains available from the persistent 3D Plot tab, while mesh-only colour range/banding controls are
   disabled. Each dimension is capped at 256 cells to prevent accidental sparse-grid allocations.
10. **Complete (2026-09-28):** the persistent **3D Plot** document tab sits beside Simulation and is rebound to
    the active ModelViewer like SimulationPanel. It provides per-plot selection, axis-box visibility, colormap,
    colour range/bands, and labels/scales/ranges/ticks for all axes. The early fixed-range Preview Axis Box button
    and dialog-close axis teardown were removed. Multi-plot axes use the bounds of every currently visible plot,
    while the active plot owns the axis presentation settings.
11. **Complete (2026-09-29):** Contour / iso-lines for Surface. Surface-following contours are live-adjustable
    from the 3D Plot tab. Projected contours on the XY reference plane remain an optional future display mode.
12. **Complete (2026-09-29):** Stem plots. Scatter's import form has a
    **Draw stems to Base Z** option; it creates independent, fixed-pixel-width GL line segments from every sample to
    the selected base plus matching constant-pixel endpoint markers. The normal scene node, colour map and
    combined-axis handling are retained, and the base is included in the Z-axis extent.
13. **Implemented, awaiting the user's build+visual check (2026-09-29):** scattered/unstructured Surface input.
    Complete grids retain their deterministic cell connectivity. Every other non-collinear X/Y set is triangulated
    by CGAL Delaunay; duplicate X/Y positions and collinear sets remain explicit errors. `surface_scattered.csv`
    exercises this path.
14. **Implemented, awaiting the user's build+visual check (2026-09-29):** Formula surfaces and parametric surfaces.
    The self-contained expression evaluator supports arithmetic, powers, parentheses, `x`/`y` and `u`/`v`, named
    parameters, `pi`/`e`, and common trigonometric, hyperbolic and scalar functions. Formula presets cover plane,
    saddle, paraboloid, cone, Gaussian, sinc ripple, standing wave, Mexican hat, bivariate normal, logistic and
    Rosenbrock fields. Parametric presets cover torus,
    ellipsoid, Möbius strip, Klein bottle, superellipsoid, helicoid, catenoid, Enneper surface and a tunable
    spherical harmonic. Generated data bypasses the CSV mapping UI and is added as an ordinary persistent Plot3D
    surface with its own axis box, colour controls and scene-tree node.
15. **Implemented, awaiting the user's build+visual check (2026-09-29):** main-viewer Plot3D preview. The Add 3D
    Plot dialog can render a transient plot directly in the viewport before Build Plot commits it. The preview uses
    a short-lived render-only scene node, with no session, undo entry, save data, document-modified state or
    navigation-tree row. It replaces the preceding preview, supplies its own temporary axes box, and is cleared on
    dialog close or before the committed plot is created. Surface, contour, line, scatter/stem, bar, formula and
    parametric surface use the common mesh preview path. CSV Quiver uses the same glyph renderer and normalized
    arrow sizing as its committed plot, while CSV Voxel uses the same volume renderer, transfer function and bounds
    proxy as its committed plot.
16. **Partly implemented (2026-09-29):** presentation and editing. The active visible scalar plot now gets an
    outlined in-viewport colour legend driven by the same range, map and banding as the plot. It uses a separate
    overlay from Simulation and stacks below the Simulation legend when both are visible. The persistent panel also
    exposes zoom-stable line width, scatter marker size and Quiver arrow-size controls. Quiver colour range, map,
    bands and legend are based on the vector magnitude actually drawn, independent of the optional CSV value
    column. Remaining work is bar-specific styling, filled translucent reference planes, and re-edit/rebuild from
    retained source data and column mapping.
17. **Implemented, awaiting the user's build+visual check (2026-09-29):** ordered parametric curves. The formula
    source selector now includes a one-parameter curve mode that builds an ordinary `GL_LINE_STRIP`, with Helix,
    Lissajous, Trefoil Knot, Viviani Curve and Damped Spiral presets. Curves use the same main-view preview and
    persistent axes/colour controls as CSV lines, while preserving fixed-pixel line width under zoom.
18. **Implemented, awaiting the user's build+visual check (2026-09-29):** formula-driven planar vector fields.
    The formula selector supplies Vortex, Radial, Saddle and Helical presets, evaluates u/v/w over a configurable
    X/Y grid, hands the resulting arrows to the existing Quiver renderer, and uses that same glyph path for the
    temporary main-view preview.
19. **Implemented, awaiting the user's build+visual check (2026-09-29):** implicit surfaces. A scalar expression
    `f(x,y,z)` is sampled over a bounded 3-D grid and its zero crossing is tessellated into a normal scene mesh;
    the initial preset collection includes Sphere, Torus, Gyroid and Wave Interference. Preview and Build use the
    ordinary Surface pipeline, while the grid is capped at 64 samples per axis to prevent runaway geometry.
20. **Complete (2026-09-29):** formula streamlines. Vector expressions are integrated from a configurable seed set
    and rendered as coloured line segments through the common preview and persistent Plot3D paths.
21. **Complete (2026-09-29):** scatter error bars. A mapped error column produces fixed-pixel-width capped Z error
    bars, with its full range included in the combined axes box.
22. **Implemented, awaiting the user's build+visual check (2026-09-29):** filled scatter-to-plane. Scatter samples
    can be drawn as translucent, colour-mapped ribbons down to the selected Base Z. Preview, Build, and the
    persistent colour controls update baked per-vertex colormap RGB while a neutral, unlit alpha-blended material
    retains transparency without scene lighting darkening the data colours. Stem, error-bar and filled modes are
    mutually exclusive.
23. **Later plot family:** time-dependent pathlines. This needs a time-varying vector-field data model rather than
    being forced through the static CSV/formula streamline importer.
24. **Last:** MVF persistence for Plot3DSession metadata. Do this after the primitive and controls model stabilises so
    the saved schema is written once; the generated mesh itself already follows ordinary scene persistence.
