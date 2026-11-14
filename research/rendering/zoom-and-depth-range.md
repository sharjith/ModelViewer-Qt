# Zoom limits and depth range (orthographic and perspective)

Status: analysis and proposal, 2026-11-14. Part A is implemented (2026-11-14, awaiting the owner's build and
test); B and C are not. Observations come from reading the code and from arithmetic on the formulas below; none
of it has been measured in the running viewer.

## Part A as implemented

- `Camera::setDepthRange(near, far)` (`include/Render/Camera.h`, `src/Render/Camera.cpp`): an override of the
  projection's depth range, in distances from the eye. Orthographic replaces near and far. Perspective only moves
  the near plane outward and never inside the old formula's value; the far plane is unchanged because it barely
  affects precision.
- `ViewportWidget::fitCameraDepthRange()` runs each frame in `render()` right after the frustum planes are
  extracted. It takes the depth interval of every visible mesh whose box touches the four side planes, and adds the
  part of the ground plane inside those planes. Orthographic range = that interval plus two view widths each side;
  perspective near = 80% of the nearest depth. When anything is skinned or morphing, or geometry reaches the eye,
  it falls back to the old range.
- The temporary switch `MODELVIEWER_FIXED_DEPTH_RANGE` was removed after the owner's test (no regressions, 2026-11-14).
- Known limit: the fit uses mesh bounding boxes. A large part that spans the whole view in depth keeps the range
  wide, so the gain is largest when the part in view is small or the detail is spread over several parts. Reading
  back the previous frame's depth range would tighten this further; it is not done.
- Not covered by the fit: items that are neither meshes nor ground (clip-plane gizmos, light fixtures, labels). The
  margin keeps those near the model; ones far from the model may be cut at deep zoom.

## Surface-distance zoom (perspective orbit), implemented 2026-11-14, awaiting the owner's test

Based on the owner's observation of VisMockup (owner's report, not verified against the product): the wheel-to-zoom ratio
shrinks until zoom stops; the range reopens only after a camera move (pan or rotate), not with time, even on the same
surface; repeated nudges keep going deeper.

- `ViewportWidget::zoomTowardSurface()`: each step moves the camera along the ray to the surface point P under the cursor
  (depth read from the depth buffer; over empty space, the orbit target's depth). With `d` the eye depth of P and `s` the
  stand-off: zoom in gives `d' = s + (d - s) / factor`, which stalls at `s`; zoom out gives `d' = d x factor`. The whole camera
  (target and view range) is scaled about P, so P stays under the cursor; only small differences are computed, so large world
  coordinates cost no precision.
- Stand-off: refreshed to 10% of the eye depth at the moment the camera last moved by anything other than these steps (detected
  by comparing the camera pose with the pose left by the previous step). So a pan or rotation reopens the allowance; waiting
  does not.
- Hard floor: `1e-4 x` the largest coordinate in play (single-precision stability, about half a pixel of vertex jitter at a
  1000-pixel-high view; first set to 2e-4, halved after the first test on large models) or
  `1e-6 x sceneRadius`, whichever is larger. This is the part that camera-relative rendering (part C) would lift.
- Used by the wheel, the drag-zoom and the zoom inertia. Orthographic, fly mode, multi-view and compare mode keep the old zoom.
- The temporary switch `MODELVIEWER_LEGACY_ZOOM` was removed after the owner's test. The old view-range zoom remains only where the new
  one does not apply: fly mode, multi-view and compare mode; orthographic uses a numeric precision floor instead of the old mesh-radius one.
- Not changed: keyboard zoom still uses the old limit.

## How zoom works today (before this change)

Orbit camera (`src/Render/Camera.cpp`, `src/Viewport/ViewportWidget.cpp`):

- **Zoom is a change of `viewRange`.** The wheel, drag and inertia handlers multiply or divide it and then
  clamp it (`ViewportWidget.cpp` wheel ~18700, drag ~18480, inertia ~19356).
- **Perspective eye distance** is `getOrbitDistance()`, which works out to 1.25 x `viewRange` at the default 45 degree FOV. The eye sits that
  far behind the orbit target, along the view direction. The camera never moves independently of `viewRange`.
- **Perspective near/far** (`updateProjectionMatrix`): `near = max(0.001 x viewRange, 0.0001)`,
  `far = max(1000 x viewRange, 200 x sceneRadius)`.
- **Orthographic near/far**: `depthCenter -/+ max(200 x viewRange, 40 x sceneRadius)`. A comment calls this
  "completely safe" because orthographic depth is linear.
- **Zoom-in floor**: `minVR = 0.5 x radius of the smallest mesh fully inside the frustum` (perspective) or
  `that radius / 100` (orthographic), with absolute floors of 1e-4 and 1e-5. The zoom-out ceiling is
  `100 x sceneRadius`.
- **Depth buffer**: 24-bit fixed point for the window (`format.setDepthBufferSize(24)`), standard (not reversed) depth.
- **Zoom to cursor**: shifts the orbit target by the cursor's offset, measured in the plane through the target.

## What goes wrong

All figures use a 10 m assembly (`sceneRadius` 5), 24-bit depth. "Step" is the smallest depth difference the buffer can
separate at the orbit target. Surfaces closer together than that z-fight, which shows as tearing or shimmering.

| viewRange | Perspective now: step / viewRange | Orthographic now: step / viewRange |
|---|---|---|
| 10 (whole scene) | 9e-5 | 2e-5 |
| 0.1 | 9e-5 | 2e-4 |
| 0.005 | 5e-6 | 5e-3 |
| 0.0005 | 5e-7 | 5e-2 |

1. **Orthographic loses depth precision as you zoom in.** The far/near span stays at 80 x `sceneRadius` or more, so the
   step stays at about 2e-5 in absolute units while the visible region shrinks. Zoomed on a 5 mm feature the step is
   several percent of the view height. The "no precision problem because depth is linear" note in the code is true
   for the perspective divide but not for a fixed 24-bit step. This is the strongest effect.
2. **Perspective precision is constant but poor.** Because `near` scales with `viewRange`, the relative step at the
   target stays near 1e-4 of the eye distance at every zoom level. Surfaces that are a few thousandths of a percent
   apart, such as thin walls, coplanar faces or shells, z-fight everywhere. The near plane is 800 times closer
   than it needs to be for any surface that is actually in view.
3. **The near plane is not tied to the scene.** Geometry between the eye and the target but closer than `near` is
   clipped. In perspective this is rare (near is 0.08% of the eye distance), but the absolute floor of 1e-4 stops
   tracking the model once the model is small in scene units, or after the floor starts to bind at deep zoom.
4. **Zoom-in limits are heuristic.** The floor depends on which meshes happen to be fully inside the frustum
   (`computeFullyVisibleMinMeshRadius`) and is smoothed on the way up. It can jump between mouse events, and it
   stops the camera at half the radius of the smallest enclosed mesh, not at a real surface.
5. **The target is a plane, not a surface.** The eye moves toward a point in the plane through the target. Zoom to
   cursor never converges on the surface under the cursor, so zooming into a point on a surface can pass through
   it or never reach it, depending on where the target sits relative to the model.
6. **Float precision in the view transform.** Models far from the origin lose precision in the single-precision
   model-view matrix, which shows as vertex jitter before any depth problem appears. That limit is roughly the
   scene offset divided by 1e4 to 1e5, and no near/far choice moves it.

## Proposed methodology

Split the problem into three parts that can ship separately.

### A. Fit near and far to the visible scene every frame (fixes 1, 2, 3)

- Project the bounding boxes of the visible meshes (the visible-id list and bounds already exist for frustum culling)
  onto the view direction. That gives the nearest and farthest depth of anything that can be drawn.
- **Orthographic:** `near`/`far` are that slab plus a margin. The span becomes comparable to the visible depth, and the
  step improves by about 100x at deep zoom.
- **Perspective:** `far` is the slab's far edge plus a margin. `near` is the slab's near edge, but never closer than
  a fixed fraction (for example 5%) of the eye distance to the target, so a stray bounding box behind the eye cannot
  push it to zero. The same calculation puts the step near 3e-7 of the view range. That is about 300 times better
  than now.
- Skinned meshes and animated models use their bind-pose bounds, which are unreliable (the code already
  special-cases them). For those, widen the slab, or use the full scene sphere.
- Everything that draws with the main projection (floor, grid, gizmos, ray-tracer camera) must read the same
  near/far. Several overlays assume the current formulas, so they need auditing.

### B. Zoom toward a real surface, with limits from the geometry (fixes 4, 5)

- On each zoom step, read the depth under the cursor (the code already does this in
  `get3dTranslationVectorFromMousePoints`). Use that point as the zoom anchor: move the target and the eye so the
  point under the cursor stays under the cursor.
- Replace the mesh-radius heuristic with a distance limit: stop when the eye is within a small multiple of the
  near plane of the nearest visible surface at the cursor. When nothing is under the cursor, fall back to the
  scene sphere.
- Keep the zoom-out ceiling at 100 x `sceneRadius`. Smooth the transition at the limit with a soft stop instead of a hard clamp.

### C. Camera-relative rendering (fixes 6, optional and larger)

Subtract the camera target from vertex positions in double precision on the CPU, or keep a per-mesh offset, so the
float model-view matrix is small near the camera. This touches every draw path, so do it only if models far from
the origin turn out to matter.

### What to avoid

- **Reversed-Z with a 32-bit float depth buffer** would give about 100x more precision on its own, but the window's
  default framebuffer is 24-bit fixed. Rendering into a private framebuffer first is a large, risky change.
  Keep it as the fallback if A is not enough.
- A polygon-offset fix for coplanar surfaces is a different problem from the depth-range problem here.

## Decisions for the owner

1. Do A first, as its own change, for orthographic and perspective together?
2. Is B (zoom to the surface under the cursor) wanted, or should the target keep its current meaning?
3. Is C needed? Do real models sit far from the origin?

## Test plan (owner runs it)

Use one model with fine detail inside a large assembly, one far from the origin, and one tiny in scene units.
For each, in both projections, zoom from the full view to the closest the viewer allows and note:

- when the first shimmering appears, and where;
- whether anything between the eye and the target disappears;
- whether the zoom stops cleanly or jumps.

Record results in this folder, with the model identified by neutral IDs only.
