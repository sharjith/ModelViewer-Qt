# Inertia coasting: jerky frames and abrupt stop on some models

Status: analysis from code reading only. No profile, no build, nothing run. Observations are from the code; the model-size
dependence is a hypothesis until a profile confirms it.

Code: `ViewportWidget::onInertiaTimer()` (16 ms `QTimer`), the release path in `mouseReleaseEvent`, `wheelEvent`,
`get3dTranslationVectorFromMousePoints()`, `zoomTowardSurface()`, `surfaceEyeDepthAt()`.

## Verdict on the recent commits

The depth/zoom commits (`9755542`, `df11d43`, `9136435`) are **not the root cause**. The inertia timer body only changed in the
zoom block (surface-distance zoom instead of the view-range scale). The pan and rotation coasting, the damping constants and the
stop thresholds are untouched, and the jerk/abrupt-stop mechanisms below are older. The commits add some per-frame cost that
makes the underlying problem show on heavy models (see "Contributing, new").

## Root causes (existing design)

1. **Motion is per timer tick, not per elapsed time.** Each tick moves the camera by the stored velocity and multiplies the
   velocity by `_inertiaDamping` (0.8; pan and rotation) or `0.8 * 0.1` (zoom). `update()` only schedules a paint. When a
   paint takes longer than 16 ms (cost grows with mesh count/triangles), several ticks can run per displayed frame: the
   velocity decays and the camera advances several steps between two images. The result is large jumps, then a coast that is
   over in a few displayed frames. Wall-clock duration of the coast is set by the frame rate rather than by the damping.
2. **Pan coasting is quantised to whole pixels.** `QPoint newPanPoint = lastPanPoint + panDelta.toPoint()` truncates the
   velocity to an integer each tick. With velocity capped at 20 px and a 0.8 decay, the sequence is 20, 16, 12, 10, 8, 6, 5, 4,
   3, 2, 1, 0 ... The tail moves in 1 px, then 2 px jumps and the movement ends at |v| < 1 px while the stop threshold is
   `lengthSquared > 0.01` (|v| = 0.1). From |v| < 1 the camera does not move at all but the timer keeps running with `update()`:
   an abrupt stop followed by a dead period. This affects every model, but is more visible when frames are slow.
3. **Zoom coasting is almost non-existent.** Wheel velocity is `±0.05`, decay per tick `0.08`, stop at `0.001`: the coast is at
   most two ticks, a fixed `1.005` factor each. Visible as a stop rather than a decel, and independent of the wheel step.
4. **Synchronous GPU depth read per pan tick.** `get3dTranslationVectorFromMousePoints()` perspective branch calls
   `makeCurrent()` + `glReadPixels(GL_DEPTH_COMPONENT)` on every inertia tick (and a 9x9 read when the pixel is empty). A
   read-back of the default framebuffer waits for the frame in flight, so on heavy models each pan tick blocks for about one frame
   time. This makes cause 1 worse: the timer stalls inside the handler, and events then bunch up.

## Contributing, new (from the three commits)

- `fitCameraDepthRange()` now runs every frame from `renderSingleView()` / `render()`: a pass over all visible meshes' boxes
  (plus a polygon clip for the ground), followed by a matrix/frustum rebuild when the range changes. It is O(visible meshes) on
  the UI thread and it re-runs the frustum extraction, so it grows with mesh count. Probably small (sub-millisecond for thousands
  of boxes) but it is unmeasured.
- Zoom inertia and wheel now go through `zoomTowardSurface()`. The surface depth is cached while the camera is only moved by
  this feature, so the read-back normally happens once per gesture. It re-reads when the cursor moves more than 6 px or any
  other camera move occurred, and when the framebuffer is multisampled, `glReadPixels` is refused and it falls back to
  `processSelection()` (a pick depth pass over candidate meshes). That is a full extra render pass on a heavy model on a cache
  miss.
- Zoom now ends in a stand-off stall (`ratio == 1`) while the timer reports `active`: another "stops dead" case near surfaces.

## Proposed fix (owner to decide; not implemented because it changes the feel of all navigation)

1. Drive inertia by elapsed time. Keep a `QElapsedTimer`; per tick compute `dt` (clamped to, say, 50 ms), scale the step by
   `dt/16.67 ms`, and decay with `pow(damping, dt/16.67 ms)`. Frame-rate independent coast length; slow frames give larger
   steps but fewer of them, without the speed collapse.
2. Make pan sub-pixel: keep the velocity as a float and accumulate the fractional remainder, or pass a float pixel delta to
   the unit-per-pixel computation (the perspective branch already works from `dxPixels`/`dyPixels` floats). Stop on a
   speed threshold equal to one device pixel per second rather than the current mixed thresholds.
3. Reuse a depth sample for the whole pan coast (as `zoomTowardSurface()` already caches): read once at release, scale per tick,
   no `glReadPixels` in the timer.
4. Zoom: use a velocity-proportional factor and a real decay (e.g. `0.9` per tick) instead of `0.08`, and end the coast
   when the stand-off is reached (`ratio == 1`) instead of idling.
5. Optional: only schedule a new tick after the previous paint finished (`frameSwapped`) so ticks never outrun frames.

## Implemented (2026-11-14, untested, not committed)

`ViewportWidget::onInertiaTimer()` now steps by elapsed time (proposals 1 and 2): the tick length is measured with a
`QElapsedTimer` (first tick or a gap over 100 ms counts as one nominal 16.67 ms tick, at most three for a slow frame), pan,
rotation and zoom steps scale with it, damping is `pow(damping, tickScale)`, and the pan keeps a sub-pixel remainder
(`_inertiaPanRemainder`) with rounding instead of truncation. Proposals 3 (cached depth for the pan coast), 4 (zoom constants)
and 5 (`frameSwapped` pacing) are not done: they change the feel or need a measurement first.

Second step (2026-11-14, after the owner reported it still stuttered): the 16 ms timer beats against the display refresh
(16.67 ms), so even with elapsed-time steps some displayed frames get no step (a repeated image) and some two (a jump). The
coast is now stepped from `frameSwapped` (`stepInertia()`, at most one step per 8 ms), so each frame is preceded by exactly one step,
and the timer only steps when no frame has driven the coast for 50 ms. As a side effect the per-step depth read-back of the
perspective pan now runs right after a swap, when the GPU is idle, instead of in the middle of a frame. Untested.

## Owner's screen recording (2026-11-14, 30 fps capture, rotation only, heavy truck-cab assembly)

Method: frames scaled to 480x258 grey, mean absolute difference between consecutive frames (script kept outside the repo).
Observations:

- Each release starts a coast of about 12-14 captured frames (0.4-0.5 s) with a large first difference that decays, then stops
  abruptly (difference falls to ~0.01 within one frame). Consistent with the code: the rotation velocity at release is
  `-delta/dt` of the last mouse event in pixels per second, then capped at 10 (`maxInertiaVelocity`), so it is saturated almost
  every time: the coast always starts at the cap (5 degrees per tick) along the direction of the last mouse event, whatever the
  drag speed, and decays by 0.8 per tick to the 0.1 threshold (about 21 ticks).
- In several coasts there are captured frames with no change at all in the middle of the coast (difference 0.00-0.04 between
  two large ones, and in one coast every second frame). A 30 fps capture of a 60 Hz window only repeats an image when the
  application did not present a new one for more than ~33 ms, so these are real frame hitches of the application, about 4 of 14
  coasts, one at an app rate of roughly 15 fps.
- The tail of the coasts is not monotone (e.g. 3.2, 4.0, 2.3, 2.2, 3.0, 1.7, 2.6): irregular step spacing, or capture beating
  against the 60 Hz frames; a 30 fps capture cannot tell which.
- It is not known which build the recording was made with (before or after the elapsed-time and `frameSwapped` changes).

A 30 fps capture cannot resolve 60 Hz judder, so the next step is data from the application:
`MODELVIEWER_INERTIA_LOG=1` (added 2026-11-14) logs, while a coast runs, `[InertiaLog] frame interval` for every displayed frame
and `[InertiaLog] step cost / tickScale / frameDriven / rotVel` for every step, in the usual log directory.

## Owner's coast log (2026-11-14, `MODELVIEWER_INERTIA_LOG=1`, rotation, ~0.6 M polygon model)

5 coasts, 108 steps. A step costs 0.01-0.05 ms: the stepping code itself is negligible. In 4 coasts every step was
frame-driven and the spacing was 16.7-18 ms with `tickScale` 1.00 +/- 0.01: regular. In 1 coast the spacing was 27 ms and then
66 ms, after which the timer fallback took over for the remaining 14 steps. Cause (found in the log): the frame of the 27 ms step
was swapped only 6 ms after that step, and the frame handler ignored frames within 8 ms of a step without scheduling another
step, so the chain stopped until the 50 ms fallback fired: a hitch created by the new frame-driven stepping itself. Fixed
by deferring the step instead of dropping it (`_inertiaStepPending`). The "frame interval" lines of that log are not meaningful
(the interval was shared between the widgets of all open documents; now per widget).

Second log (after the deferred-step fix, 8 coasts, 175 steps): 7 coasts perfectly regular (steps 16.7-18 ms apart, `tickScale`
1.00, step cost < 0.1 ms); the first coast had two 30 ms gaps. So the step timing is no longer the problem, but the owner still
sees the rotation stutter: the cause is in what the coast does, not when. Found in the release code (rotate branch of
`mouseMoveEvent`):

- the coast velocity was `-delta/dt` of the last mouse event only (pixels per second), capped at 10 "units"; a unit is
  a degree per tick x 2, so any drag faster than ~600 px/s... in fact almost any drag (the value is in px/s, not px/tick) was
  saturated at the cap: the coast started at the maximum speed whatever the drag speed (a speed kick at release), along a
  direction taken from a one-pixel delta (snapping to an axis: a sideways veer at release);
- it ignored `_mouseSensitivity` and the inverted-Y setting, which the drag itself applies (with inverted Y the coast
  moved the other way vertically).

Changed: the velocity is the mouse displacement over the last ~80 ms converted to the coast's per-tick units, with the same
sensitivity and Y direction as the drag (`_rotateSamples`), so the coast continues the drag speed and direction. Untested.

Third log (23 coasts, 562 steps, owner reports the stutter is worse zoomed out and almost gone zoomed in): all ~19 rotation
coasts have regular steps (16-18 ms, two with a 21-24 ms step). The long (110-180 ms) gaps in the other "coasts" are separate wheel-zoom
gestures merged by the 200 ms grouping, not hitches. Render cost is trivial for this model (21 meshes, 0.5 M triangles,
`frameCpuMs` 0.4-4 ms, `gpuMs` 0.6-5 ms, `fastPathViews=0`, so the oversize-model interaction fast path, small-part culling and
the LOD tier are all off). Conclusion: step timing and frame cost do not explain it; what is left is how the frames look (temporal
aliasing of thin details, which grows when zoomed out) or the start speed (the coast still starts at the cap, rotVel 8, 4
degrees per frame). Not resolved.

## Owner's second recording (2026-11-14, 30 fps, zoomed out) and the "large models" remark

The owner describes the artifact as the whole model jumping around in small vibrations, worse on large models and when zoomed
out. Measured on the recording (silhouette of the model against the background gradient, per frame): during the coasts, with the
mouse cursor motionless, the silhouette keeps a constant height (282 px) but its vertical position jumps by 5-25 px from
frame to frame (e.g. top edge 433, 444, 451, 456, 433 ...; centroid y 578.7, 572.3, 570.0, 583.4, 585.1, 589.9, 566.8) and
x by about 8 px, not decaying with the coast. At rest the image is perfectly still. So the whole picture is translating by a
few pixels between frames: not judder in time, not shimmer. The jump is far larger than the coast's own motion, which means the
camera or view matrix placement is not stable from step to step.

Candidate cause (not proven): the orbit view is built as eye = pivot - viewDir * orbitDistance in float32
(`Camera::getRenderPosition`, `Camera::updateViewMatrix`), and a comment there already records that rounding of large numbers made
the view direction jittery on large models. Zoomed out, the orbit distance grows; on a large model the coordinates are large, and the
view translation is a difference of large numbers. The eye-relative double-precision composition (`df11d43`) covers the model
matrix; whether the view matrix and the per-step `rotateX`/`rotateY` re-orthogonalisation are covered is to be checked with
numbers: `MODELVIEWER_INERTIA_LOG=1` now also logs, for every displayed frame of a coast, the camera pivot and view direction, the
view matrix translation, `proj(0,0)`, `proj(2,2)`, `proj(2,3)`, the view range and the orbit distance (9 significant digits). If these
change smoothly while the picture jumps, the error is in the rendering path; if the pivot or `viewT` jump, it is in the camera.

## Regression, confirmed by the owner (2026-11-14)

The owner tested the released 2026.11.0 build: the vibration does not exist there, and it is strong on large models, worst zoomed out. So
it comes from the changes since the release (the camera and render commits `9755542`, `df11d43`, `9136435`). Three differences
touch the view of a rotating orbit camera. Switches to bisect them (set one environment variable per run, compare the same
rotate-and-release on the same large model zoomed out; combine them if one alone does not remove it):

| Variable | Restores the 2026.11.0 behaviour of |
|---|---|
| `MODELVIEWER_NO_DEPTH_FIT=1` | the default depth range instead of the per-frame fit to the visible boxes |
| `MODELVIEWER_NO_EYE_RELATIVE=1` | the ordinary model-view instead of the eye-relative double-precision one |
| `MODELVIEWER_OLD_ORBIT_AIM=1` | aiming the orbit camera at its target instead of along the stored view direction |

## Cause found and fixed (2026-11-14, untested)

Bisect result: `MODELVIEWER_OLD_ORBIT_AIM=1` removed the vibration. The owner left `MODELVIEWER_NO_DEPTH_FIT` and `MODELVIEWER_NO_EYE_RELATIVE`
set at 1 during that run (they had been tried first and had not helped), so the result is for the three together; the aim is the
only one that was expected to matter and the analysis below explains it.

`Camera::updateViewMatrix` (orbit) aimed with `lookAt(eye, eye + viewDir, up)`. `eye + viewDir` adds a unit vector to coordinates
that are large on a large model (and larger still zoomed out, where the orbit distance is large): the float sum is rounded to the
spacing of floats at that magnitude (0.06 at 1e6, 0.0078 at 1e5), so the aim direction `(eye + viewDir) - eye` was quantised, in
steps of whole degrees at the worst, and changed from frame to frame as the eye moved: the whole picture vibrated while rotating
and was still at rest. The old code aimed at the target, a difference whose size is the orbit distance, so the same rounding was
negligible. Fixed by building the rotation at the origin with the stored direction and applying the eye as a translation:
`lookAt(0, dir, up); translate(-eye)`, which is exactly what `lookAt` does internally, without ever forming `eye + dir`.
`MODELVIEWER_OLD_ORBIT_AIM=1` still selects the old aim for comparison. The other two switches can be removed once confirmed.
The non-orbit modes (fly, first person) still use `lookAt(eye, eye + dir)` as in the release: same rounding in principle, not touched.

## To confirm with the owner's test (not run here)

- Whether the jerk is worse with MSAA on (pick-pass fallback) and with a perspective vs. orthographic view (the ortho branch
  has no depth read).
- A per-tick log of `dt` between `onInertiaTimer()` calls and the paint duration on a heavy and a light model. If ticks arrive
  late (`dt` well above 16 ms) cause 1/4 is confirmed; if `dt` is steady and the pan stops at the 1 px tail, cause 2 is.

## Outcome (2026-11-14)

Owner confirmed with the fix: the vibration is gone. The three bisecting switches (`MODELVIEWER_NO_DEPTH_FIT`,
`MODELVIEWER_NO_EYE_RELATIVE`, `MODELVIEWER_OLD_ORBIT_AIM`) and the `MODELVIEWER_INERTIA_LOG` diagnostics were removed again. What
stays: the orbit view built with `lookAt(0, dir, up)` + `translate(-eye)` (the cause), and the coast changes made on the way
(elapsed-time steps, steps driven by `frameSwapped` with the timer as fallback, sub-pixel pan, release velocity measured over ~80 ms
with the drag's sensitivity and Y direction). The earlier "inertia" findings above about step timing remain true but were not the
cause of the reported vibration.
