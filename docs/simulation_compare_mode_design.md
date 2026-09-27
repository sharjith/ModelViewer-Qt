# Simulation Results - Compare Mode (Design Note)

Status: **DRAFT for review** (2026-09-24). Branch: `feature/simulation-results`. No code exists for this yet.
Companion to `simulation_results_design.md`.

## 1. Problem

Opening a second simulation result today adds a second scene node at its own file coordinates. Two results of
the same model land exactly on top of each other (z-fighting), and the only ways to look at one are to select its
mesh (which swaps the Simulation panel and the legend) or to hide the other by hand.

The main reason to have two results open is **comparison**: two designs, a static run against a frequency mode,
before/after a change, or two time steps. That needs the results visible **side by side, from the same viewpoint,
with legends whose colours mean the same thing**.

Goals:

- Results keep their true file coordinates (the dataset is the source of truth and attach-to-CAD will need them).
- One shared camera, so orbit/pan/zoom stay in sync across the results being compared.
- A legend per result, with an option for one shared colour range.
- No regression in single-view rendering, picking or any existing tool.

Non-goals: more than four results at once, comparing across documents, difference fields (A - B), persisting the
compare layout in MVF (first version).

## 2. What the existing split viewport is (checked in the code)

`ViewportWidget::renderMultiView()` (`src/ViewportWidget.cpp:6651`) is a **fixed 2x2 layout**: Top, Front and Left
orthographic panes plus the isometric view. For each pane it calls `glViewport(...)`, configures a camera and calls
`render(camera)`, then draws a label; `splitScreen()` draws the dividing lines.

What that means for reuse:

- **Every pane draws the same scene.** `render(Camera*)` (line 10719) draws every displayed mesh; there is no
  per-pane mesh selection. It refreshes the visibility/culling cache each call through
  `_sceneRuntime.refreshRuntimeVisibilityCacheForCurrentView(root, revision, isMeshAnimationVisible)` (line 10751) -
  a natural place to hook a per-pane filter, but the cache is keyed by a revision, so a per-pane filter must force
  it to be recomputed per pane.
- **The pane geometry is hard-coded in several places.** `PickingHelper::viewportRectForPoint()` and
  `clientRectForPoint()` (`src/PickingHelper.cpp:23`, `:39`) are 2x2 quadrant maths; `getCameraForPoint()`
  (`ViewportWidget.cpp:15659`) repeats the quadrant logic; `viewportRectForPoint`/`clientRectForPoint` are called
  about 15 times in `ViewportWidget.cpp` (mouse, wheel, window-zoom, picking, gizmos) and `SelectionManager.cpp`
  has 7 more `multiViewActive` checks.
- **Many features are simply switched off in multi-view** (`!multiViewActive()` gates: plane gizmos, hover
  highlighting, the global axis triads, and others), and the measurement, annotation, seam-marking and
  fill-holes controllers each have their own multi-view drawing branch (`ViewportWidget.cpp:10958-10964`).
- `drawOpaqueMeshes()` reads `_primaryCamera->getRenderPosition()` for its clipping uniforms - a single-camera
  assumption that already works for the four panes only because they share a scene centre.

So the split viewport supplies the *rendering pattern* (`glViewport` per pane, dividing lines, per-pane camera
state) but **not** a reusable layout, per-pane scene content, or per-pane picking.

## 3. Options

| | A. Compare panes inside one viewport | B. Linked document windows | C. Offset one result |
|---|---|---|---|
| Idea | New layout: N panes (A \| B), one shared camera, each pane draws only its own result | Each result in its own document (MDI), tiled side by side, cameras linked | Translate one result's node beside the other |
| True coordinates kept | Yes | Yes | No (moved; undoable) |
| Camera sync | Free (one camera) | Needs a "link cameras" feature | Free |
| Legends | One per pane | One per document (already works) | One shared legend problem |
| Shared colour range | Easy (same session state) | Awkward across documents | Awkward |
| Render-core changes | Yes: layout, per-pane filter, picking | None | None |
| Existing tools per pane | Need per-pane handling (see 2) | Work unchanged | Work unchanged |
| Works with a shared CAD reference | Yes | No (separate scenes) | Yes |
| Effort / risk | Medium-high; risk in render + picking | Low-medium | Low |

## 4. Recommendation

Build **A**, in phases, and keep **B** in mind as a cheap fallback.

Option A is the design that fits the product: comparison inside one scene with one camera and true coordinates,
and it generalises later to time steps and to "CAD reference next to result". Its risk sits in the two places I
cannot run here (the render loop and picking), so it is phased so that each phase is independently testable and
the first ones change nothing visible.

Option C is dropped: it is a hack that moves geometry, and compare replaces its purpose.
Option B is worth revisiting only if A proves too invasive; it needs only a camera-link feature and a
"tile windows" command, but it cannot show a result next to a CAD reference in one scene.

## 5. Design for option A

### 5.1 Layout model (GUI-free, unit-testable)

Replace the scattered quadrant maths with one small value type:

```
struct ViewPane { QRect glRect;      // rect in GL coordinates (origin bottom-left), for glViewport
                  QRect clientRect;  // the same rect in widget coordinates (origin top-left), for mouse events
                  int index; };

class ViewLayout {                   // pure geometry, no GL, no Qt widgets
    static ViewLayout single(w, h);            // 1 pane
    static ViewLayout quad(w, h);              // today's 2x2 (order and rects exactly as now)
    static ViewLayout sideBySide(w, h, n);     // n panes in a row (n = 2..4)
    const ViewPane* paneAt(QPoint clientPixel) const;
};
```

`PickingHelper::viewportRectForPoint` / `clientRectForPoint` become thin wrappers over the layout, so the ~22
existing call sites keep working while the maths lives in one tested place. `quad()` must reproduce today's rects
exactly; that is the phase-1 acceptance test (a table-driven test over many pixels and window sizes, old formula
versus new).

### 5.2 Pane content

Each pane gets a *content descriptor*:

```
struct PaneContent { QSet<QUuid> meshFilter;   // empty = draw everything (today's behaviour)
                     Camera* camera;           // compare: all panes share _primaryCamera
                     QString label; };
```

`render(camera)` gains an optional pane filter consulted by the visibility cache refresh (and forced to refresh per
pane). With an empty filter it behaves exactly as today, which keeps single view and the quad view untouched.

### 5.3 Rendering

A new `renderComparePanes()` next to `renderMultiView()`: for each pane, `glViewport(pane.glRect)`, set the
pane's filter, `render(_primaryCamera)`, draw the pane label, then `splitScreen()`-style dividers (generalised to
the layout). Shadow, SSS and transmission passes are done once for the whole scene as `renderMultiView()` does today
(they are view-independent), and path tracing stays off in compare mode, as it is in multi-view.

Because all panes share `_primaryCamera`, every existing camera interaction (orbit, pan, zoom, fit, view cube)
works unchanged and stays synchronised; no camera-link code is needed.

### 5.4 Interaction

Mouse events resolve their pane through `ViewLayout::paneAt()`. Because compare panes share one camera and full
scene, picking can stay scene-wide with the pane rect used for the ray, exactly like the isometric pane does
today; the filter only affects *drawing*, and picking must also respect it (a mesh not drawn in a pane must not be
pickable there) - this is the one place the filter must be honoured outside `render()`.

In compare mode these stay disabled, like in multi-view: plane gizmos and hover highlighting (initially).
Measurement/annotation/seam/fill controllers get **no** compare branch in the first version: the tools are
unavailable while compare is on (the toolbar entries are disabled), rather than half-working.

### 5.5 Legends and colour range

- One `SimulationLegendWidget` per pane, positioned in the pane's top-right corner (the legend already repositions
  from the viewport size; it takes a pane rect instead).
- `SimulationViewState` gains `sharedRangeGroup` semantics at the compare level: a checkbox "Use one colour range
  for all compared results" computes the union of the data ranges (or applies the active result's custom range) to
  every compared session, so equal colours mean equal values. Off by default, each result keeps its own range.

### 5.6 UI

In the Simulation panel, a "Compare" group:

- a list (or combo pair) to choose which open results take part (2-4), and a **Compare** toggle;
- "Same colour range" checkbox;
- turning Compare on switches the viewport to `sideBySide(n)` and assigns each result its pane; turning it off
  returns to the previous layout.

Panes are labelled with the result's file name.

### 5.7 Persistence

Not persisted in the first version: compare mode is a transient viewing state. (The two results themselves are
persisted like any results, once the MVF snapshot exists.)

## 6. Phases

| Phase | Change | Visible effect | Test |
|---|---|---|---|
| 1 | `ViewLayout` (single/quad), route `PickingHelper` and `getCameraForPoint` through it | none | table-driven test: new rects == old formula for many sizes/pixels |
| 2 | Per-pane mesh filter in `render()` and in picking; empty filter = today | none | render/pick with empty filter unchanged; unit test on the filter logic |
| 3 | `sideBySide(n)` layout + `renderComparePanes()` behind a hidden toggle | compare renders | manual: two results, orbit/zoom sync, resize |
| 4 | Per-pane legends + shared colour range | legends per pane | manual |
| 5 | Simulation panel "Compare" group; disable unsupported tools while on | user-facing feature | manual matrix of tools |

Phases 1-2 are pure refactors and can be reviewed and tested on their own before anything changes on screen.

## 7. Risks

- **Render core and picking cannot be exercised here.** Mitigated by the phase split (refactors first, table
  tests for the geometry) and by keeping every new behaviour behind an empty-filter/off-by-default path.
- **Visibility-cache invalidation** with a per-pane filter: a stale cache would draw the wrong meshes in a pane.
  Needs care and a manual test (toggle visibility, undo, add a mesh while comparing).
- **Passes that assume one scene** (shadows, SSS, transmission, capping, clipping planes) are treated as global;
  results with section-cap or clip features need a check.
- **Feature gating** (`!multiViewActive()` checks) must not be copied blindly: compare mode has its own flag so
  the quad view's behaviour is unchanged.
- **Performance:** each pane redraws its meshes, so compare of N large results costs roughly the sum of the
  results; acceptable, but worth measuring on a multi-million-triangle boundary.

## 8. Open questions

1. Maximum number of compared results: 2 only, or up to 4?
2. Pane arrangement: side by side only, or also stacked (top/bottom) for wide models?
3. Which tools must work in compare mode from day one (measurement? probe?) versus being disabled?
4. Should compare also be offered for a result against a CAD reference mesh (a plain scene mesh in one pane)?
5. Is Option B (linked windows) wanted as a stopgap while A is built?

## 9. Implemented approach (2026-09-25) - supersedes sections 4-6 where they differ

Decisions taken with the user: compare shows **two different results** (never the same result twice: it would need a
hidden mesh copy and a lot of disambiguation); the existing 2x2 multi-view code is **not** reused or refactored; the
first version is rendering + navigation + legends, with picking-based interactions off.

- **Pane geometry** is a small standalone model, `ComparePaneLayout.h/.cpp` (pure QtCore, unit-tested): 1-4 panes,
  side by side, stacked or 2x2. A pane is *not* rendered with a narrower camera. It is the ordinary full-window view
  (same aspect ratio and projection, so every camera interaction and future picking maths stay unchanged) drawn
  into a full-size `glViewport` shifted so the model is centred in the pane, and clipped to the pane with `glScissor`.
  `ComparePane::toWindow` maps a point of the pane to the equivalent point of the full-window view.
- **Rendering**: `ViewportWidget::renderComparePanes()` (next to `renderMultiView()`, chosen in `paintGL()` only while
  compare is active). Shadow/SSS/transmission passes run once for the whole scene; each pane then draws with
  `_paneMeshFilter` set to its own mesh, consulted in `isMeshVisible()`. With no filter (every other case) nothing
  changes.
- **Interactions**: orbit, pan, zoom and view changes work (one shared camera). Selection, hover highlight, the hover
  probe and plane gizmos are disabled while compare is on. Their next step is the pane mapping above plus the mesh filter
  in the pick pass.
- **Legends**: one `SimulationLegendWidget` per pane (top-right of the pane, result name as a heading);
  **same colour range** unions the two results' own ranges (only while both show the same unit).
- **UI**: Simulation tab, "Compare with" drop-down + Compare / Exit Compare, "Stacked" and "Same colour range" checks.
  Compare ends by itself when either result is hidden, closed or undone. Not persisted.
- **Known limits (v1)**: fit-to-screen still fits the whole window, so a wide model can be wider than its pane (zoom
  out); the timeline drives the active result only; measurement/annotation are not pane-aware; no Simulation-menu entry
  yet.
