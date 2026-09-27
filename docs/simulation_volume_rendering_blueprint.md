# Simulation Direct Volume Rendering - Implementation Blueprint

Status: **PROPOSAL for implementation** (2026-09-27). Nothing built yet. Branch: `feature/simulation-results` (continue directly on
it - see [[project_simulation_viz_roadmap_next_branch]] and `docs/simulation_volume_features.md`, whose sections/iso-surfaces/
streamlines this is the fourth sibling of). Companion docs: `docs/simulation_results_design.md`, `docs/simulation_volume_features.md`.

## 1. What this is, and isn't

Direct volume rendering: instead of a single iso-surface (one level) or a section plane, show the *whole* field's structure at
once as a translucent cloud - useful for a smoothly varying field (temperature, concentration, density) where a single iso-level
hides the interior structure. This is a genuinely different visualization from what exists, not an extension of iso-surfaces.

**Non-goals for v1** (explicitly deferred, say so if scope-creep starts):
- Cell (element) fields - node fields only, same restriction `sampleFieldOverLine`/`buildTensorGlyphSet` already have.
- Multiple fields blended together, or RGB compositing of more than one variable.
- Adaptive/sparse resolution (octree, bricking) - a single fixed-resolution uniform grid is enough for v1.
- Animating the transfer function, or per-step transfer function presets.
- Shell/surface-only results (nothing to volume-render - same check as streamlines' `volumeCellCount() == 0`).

## 2. Chosen approach: resample to a uniform grid, then GPU ray-march it

Unstructured-mesh volume rendering has three classical approaches (cell projection/splatting, unstructured ray-casting with a
BSP/interval tree, and resample-to-texture). **Resample to a uniform 3D texture, then standard GPU ray-marching** is the right
choice here specifically because:

- It reuses `CellLocator` (already built, tested, and deformed-shape-aware) for the ONLY unstructured-mesh-specific part - point
  location and barycentric interpolation - exactly the way `SimulationCharts.cpp`'s `sampleFieldOverLine`/`sampleFieldOverTime`
  already do (see their `interpolateScalar` helper: a dummy zero vector array plus the real field as CellLocator's optional
  scalar channel).
- Everything downstream of the resample (the actual rendering) is standard, well-understood 3D-texture ray-marching - no new
  unstructured-geometry GPU code, which is the highest-risk part of the other two approaches.
- It cleanly separates into a GUI-free, unit-testable CPU phase (the resample) and a GPU phase (the ray-march), matching this
  codebase's established split (`SimulationCharts.cpp` vs `SimulationChartWidget.cpp`; `SimulationGlyphs.cpp` vs
  `SimulationGlyphController`/`SimulationTensorGlyphController`).

Cost: a uniform grid wastes memory/resolution on empty space around a non-box-shaped model, and a single fixed resolution cannot
resolve a feature much smaller than one voxel. Both are acceptable for v1 (see section 7, resolution budget).

## 3. Data flow

```
ResultDataset + CellLocator (session's own, shape-aware - see below)
        |
        v
buildVolumeGrid()                     <- new, Core, GUI-free (SimulationVolume.h/.cpp)
        |  samples a Nx x Ny x Nz uniform grid over the mesh's bounding box,
        |  one CellLocator::interpolate() per voxel (NaN where the point is outside every cell)
        v
VolumeGrid { values, dims, origin, voxelSize, fieldMin/fieldMax, unit, label }
        |
        v
SimulationVolumeController               <- new, GPU (mirrors SimulationTensorGlyphController's shape)
        |  uploads `values` as a GL_TEXTURE_3D (GL_R32F), caches it keyed the same way the tensor
        |  controller now caches its geometry (field/step/shape key - see section 8), builds/uploads a
        |  1-D transfer-function texture (RGBA) from the panel's colour ramp + opacity curve
        v
Fragment shader ray-march (new: volume_raymarch.frag/.vert, or added to an existing shader program)
        |  for each screen pixel whose view ray intersects the model's world-space bounding box:
        |  step along the ray inside the box, sample the 3-D texture at each step (trilinear),
        |  look up colour+opacity in the transfer function texture, front-to-back alpha-composite
        v
Blended into the framebuffer, drawn where the opaque scene has already been rendered
```

## 4. New files (mirroring the existing pattern exactly)

- `include/Simulation/Core/SimulationVolume.h` + `src/Simulation/Core/SimulationVolume.cpp` - GUI-free, added to
  `tests/CMakeLists.txt` like `SimulationCharts.cpp`/`SimulationGlyphs.cpp` already are.
  - `struct VolumeGrid { std::vector<float> values; int dimX, dimY, dimZ; float origin[3]; float voxelSize[3]; float fieldMin,
    fieldMax; QString label, unit; bool empty(...); }` - `values[k*dimY*dimX + j*dimX + i]` is a NaN where the point was outside
    every cell (the shader must treat NaN as fully transparent, not sample it as 0).
  - `bool buildVolumeGrid(const ResultDataset& dataset, const CellLocator& locator, int fieldIndex, int component, int step,
    int targetResolution, VolumeGrid& out)` - `targetResolution` bounds the LONGEST axis; the grid is `targetResolution` voxels
    along the model's longest bounding-box dimension, proportionally fewer along the others (a thin/flat model does not get a
    cubical, mostly-empty grid). Follows `sampleFieldOverLine`'s exact pattern: `buildDisplayScalar` for the unit-converted,
    labelled per-node values, then `CellLocator::interpolate()` (dummy zero vectors + the real scalar) per voxel, with a `hint`
    reused between ADJACENT voxels along a scan line (voxels are visited in x-fastest order, so the previous voxel's cell is a
    good first guess for the next one, same speedup `sampleFieldOverLine`'s point-to-point `hint` reuse already relies on).
    False for the same reasons `sampleFieldOverLine` is: cell (association) data, no volume cells, no data at the step.
  - Test in `tests/result_tests.cpp` (`testVolumeGrid` or folded into `testCharts`): a linear field over `hexRow`, check a few
    known voxel positions land on the exact linear value (same style as `testCharts`'s exactness checks), check outside-mesh
    voxels are NaN, check the grid's resolution scales correctly with an elongated (non-cubical) bounding box.
- `include/Simulation/UI/SimulationVolumeController.h` + `.cpp` - the GPU piece. Owns a `GL_TEXTURE_3D` per session (`std::map<QUuid,
  ...>` cache, same shape as `SimulationTensorGlyphController::_sets`), a small 1-D transfer-function texture, and the ray-march
  shader program. `setVolume(meshUuid, VolumeGrid)` / `clearVolume(meshUuid)` / `drawOverlay(camera, resolve)` - but see section 5,
  this one cannot just be "another overlay drawn after everything else" the way arrows/ellipsoids/slices are.
- `include/Simulation/UI/SimulationTransferFunctionWidget.h` + `.cpp` - a small hand-drawn (QPainter, house style) opacity-curve
  editor: a handful of draggable control points (value 0..1 on X, opacity 0..1 on Y), linearly interpolated between them. The
  COLOUR half of the transfer function is NOT a new control - reuse the panel's existing colormap choice (`AnalysisColormap`,
  the same sequential/diverging ramps sections/iso-surfaces/glyphs already use), so only opacity needs a new editor. Default: a
  ramp from 0 opacity at the low end to some modest maximum (e.g. 0.15-0.3) at the high end, editable from there.
- Shader: a new GLSL fragment shader (`shaders/volume_raymarch.frag` or wherever the existing shaders live - check
  `SceneRenderController`'s shader loading for the house convention) plus a minimal vertex shader that draws the model's
  world-space bounding box as a cube (front faces only, or a full-screen triangle with per-pixel ray/AABB intersection - the
  cube-mesh approach is simpler to get right first).

## 5. The hard part: compositing against the opaque scene correctly

Every other simulation overlay (arrows, ellipsoids, slices, streamlines) is drawn as a fully opaque or simply-blended pass
**after** the opaque scene, with no need to know what is behind it pixel-by-pixel. Volume rendering is different: a ray marching
through the volume must **stop where an opaque object (or another part of the same model) occludes it**, otherwise the volume
"bleeds through" solid geometry in front of it. That needs the **scene's depth buffer as a readable texture** inside the
ray-march shader, to bound each ray's marching distance.

**This is a genuine open question, not a solved detail - investigate before writing the shader:**
- Does `SceneRenderController`'s render target already have a depth attachment that can be bound as a `sampler2D` in a later
  pass (an FBO with a depth-texture attachment), or does the app render straight to the default framebuffer (whose depth buffer
  is not directly sampleable as a texture without restructuring)? Search for how the existing shadow map (`AdaptiveShadowMapper`,
  referenced in `docs/`) reads depth as a texture - it already solves "render depth, then sample it in a later shader" once in
  this codebase, and the volume pass can very likely reuse the same FBO/depth-texture pattern rather than inventing a new one.
- If there is no readable depth texture available for the main pass today, the minimum viable fallback for v1 is: **draw the
  volume only when nothing else in the scene can occlude it from the current view** (e.g. render it, accept that it currently
  draws in front of everything, and document the limitation) OR restrict v1 to a scene with exactly one visible mesh (the
  result itself) so occlusion by *other* geometry is moot - self-occlusion within the SAME volume is handled by the ray-march
  itself (front-to-back compositing naturally attenuates through the volume), it is only *other, unrelated* opaque geometry in
  front of the volume that needs the scene depth test.

Resolve this with a spike/prototype before committing to the full feature - it changes where in the render pipeline this pass
must sit (`GpuResourcePhase` timing relative to the opaque pass) and possibly requires a render-target change that other
overlays don't need.

## 6. Panel UI

A new toggle in the existing "Charts:"-row's neighbourhood (or its own row) in `SimulationPanel`:
- `QCheckBox` "Show as volume" (mutually exclusive with nothing existing - it can coexist with sections/iso-surfaces/streamlines/
  glyphs/tensor ellipsoids the same way those already coexist with each other, but see the note below about the section cap).
- A field combo (own `SimulationViewState::volumeField`, defaulting like every other overlay's field picker - `chooseDefault...`
  style helper).
- A resolution choice (a `QSpinBox` or a Low/Medium/High combo mapping to `targetResolution` - see section 7's numbers for what
  each tier costs).
- The `SimulationTransferFunctionWidget` (opacity curve) plus reuse of the existing colormap combo for colour.
- Like sections/iso-surfaces (`docs/simulation_volume_features.md`: "the section cap is left open while iso-surfaces or
  streamlines are shown, because an opaque cap would hide them"), an opaque Clipping Plane cap must also stay open while the
  volume is shown, for the same reason.

`SimulationViewState` gains `bool volume`, `int volumeField`, `int volumeResolution`, and the opacity curve's control points
(a small `QVector<QPointF>` or similar) - follow the tensor-glyph precedent for adding new state (section in
`SimulationResultDisplay.h`, wired into `currentState()`/`setSession()` in `SimulationPanel.cpp`, an `updateSimulationVolume()`
in `ModelViewerSimulation.cpp` mirroring `updateSimulationTensorGlyphs()`, and MVF snapshot round-trip in `ResultSnapshot.cpp`
the same shape as the tensor glyph state was added there).

## 7. Resolution / cost budget

A voxel is 4 bytes (one float). A 128^3 grid is 8 MB; 256^3 is 64 MB - both fine as a GPU texture. The EXPENSIVE part is
building it on the CPU: `targetResolution^3` calls to `CellLocator::interpolate()`, each an O(1)-ish point location with the
`hint` fast path (most voxels reuse the previous one's cell) but a full grid-bin search on a miss. Measure with
`result_tests --time` or a small dedicated benchmark (`--bench` already exists for other things) before picking a default;
start the default at a LOW resolution (e.g. 64 on the longest axis - ~260K voxels) given no existing measurement, and let the
panel's resolution choice go higher once real numbers are in hand. Rebuilding the grid only needs to happen when the field,
step, or deformed shape changes - cache it exactly like the tensor glyph controller now caches its geometry (session-keyed,
invalidated by a shape/field/step key, not rebuilt every frame or every orbit).

## 8. Reuse checklist (what NOT to reinvent)

- `CellLocator` - point location and interpolation. Do not write a second one.
- `buildDisplayScalar` - unit conversion, label, range. Same pattern `SimulationCharts.cpp` already follows.
- `AnalysisColorRamp` - colour mapping (sequential/diverging). Only opacity is new.
- `overlayNodePositions` / the session's `locator`/`locatorKey` cache - deformed-shape awareness, exactly as
  `onSimulationChartPointsPicked`/`updateSimulationStreamlines` already share one cached, shape-aware locator per session.
- The `GpuResourceRegistry` / `IGpuContextResource` pattern every other controller (`SimulationGlyphController`,
  `SimulationTensorGlyphController`, `SimulationSliceController`, `SimulationStreamlineController`) already uses for GL
  resource lifetime (`restoreGpuResources()`/`releaseGpuResources()`).
- The MVF snapshot pattern (`ResultSnapshot.cpp`'s glyph/tensor-glyph state read/write) for saving volume-rendering settings.

## 9. Suggested implementation order (one phase at a time, build+test between each - see `docs/source_reorg_plan.md`'s own
"one phase is one commit you build before the next" mechanics, which worked well this session)

1. `SimulationVolume.h/.cpp` (`buildVolumeGrid`) + tests. No GPU code yet - fully verifiable without running the app.
2. Resolve section 5's occlusion question with a throwaway prototype (does a readable depth texture already exist for the main
   pass?). This determines whether phase 3 is straightforward or needs a render-target change first.
3. `SimulationVolumeController` + the ray-march shader, wired to draw a fixed test grid (not yet connected to the panel) -
   verify visually that a known synthetic field (e.g. a linear gradient) looks correct before wiring the real data path.
4. Panel UI (toggle, field combo, resolution choice, transfer-function widget) + `SimulationViewState` fields +
   `updateSimulationVolume()` wiring, following the tensor-glyph precedent file-for-file.
5. MVF snapshot round-trip.
6. Polish: resolution/performance tuning with real measurements, the section-cap interaction, compare-mode support (the tensor
   glyph work's compare-mode picking bug from the second Codex review is a cautionary example - compare panes have their own
   camera/mesh-filter needs that are easy to miss on a first pass).
