# Camera-relative rendering: audit of the current matrix and vertex pipeline

Written 2026-11-14. Read-only audit; no code changed. File and line references are to the tree at that date. Companion to
[zoom-and-depth-range.md](zoom-and-depth-range.md) and [zoom-and-depth-range-literature.md](zoom-and-depth-range-literature.md).

## Why this was audited

On a large model (scene radius 511, coordinates around 120) the surface-distance zoom ended on its precision floor at an eye
depth of about 0.0135 (log lines `[Zoom] stalled ... by the PRECISION FLOOR`, 2026-11-14 12:52). There, single-precision spacing
at the coordinates (about 7.6e-6) is close to one pixel (about 1.1e-5). Going closer needs the geometry transformed relative
to the eye, not relative to the world origin. The question was how much of the pipeline that touches.

## Findings

Observations from reading the code; none measured at run time.

1. **Vertices are mostly mesh-local; some formats are absolute.**
   `AssImpModelLoader.cpp` ~1516 says formats with authored node transforms are positioned by model matrices, while
   identity-transform scenes render from vertex data as-is. For the node-transform case every mesh keeps its vertices and gets
   `nodeWorldTransform` as its scene render transform (`AssImpMeshBuilder.cpp` ~226, `AssImpModelLoader.cpp` ~2486). Whether
   the large test model's vertices are local or absolute was not checked. That decides how much stage 3 below matters.
2. **Per-mesh model matrix, composed in float on the CPU.**
   `SceneMesh.cpp` ~1291: `modelMatrix = globalModelMatrix * combinedRenderTransform()`,
   `modelViewMatrix = viewMatrix * modelMatrix`, both `QMatrix4x4` (float). `combinedRenderTransform()`
   (`MeshInstanceState.cpp` 150) is `explodedView * userTransform * sceneRenderTransform` plus an explosion offset, all float.
3. **The vertex shader forms the position from separate float matrices.**
   `shaders/main_scene.vert`: `worldPos = modelMatrix * vertexPosition` (line 115), then
   `gl_Position = projectionMatrix * viewMatrix * worldPos` (162). The large model translation and the large view translation
   are added and subtracted in float on the GPU, which is where the camera-dependent jitter comes from. A `modelViewMatrix`
   uniform already exists and is uploaded (`SceneMesh.cpp` 1323) but is only used for clip distances and the tangent frame.
4. **Lighting, shadows and environment use world-space varyings.** `v_position`, `vs_out_shadow.FragPos`,
   `v_reflectionPosition`, `v_positionLinear`, `cameraPos`, `lightPos` are all world-space floats. Even with exact geometry,
   view and light vectors are differences of large numbers, so shading could shimmer at the extreme zoom.
5. **The camera position is single precision.** `Camera::_position` is a float; I added an exact double copy for moves
   (`Camera::shiftPosition`) so steps no longer round away, but the view matrix is still built in float.
6. **Many CPU-side consumers use the float view and projection matrices**: picking and depth unprojection, the grid shader
   (`grid.frag` unprojects NDC corners with the inverse view-projection), the floor plane, gizmos, measurement and annotation
   overlays, section capping, clipping planes (view-space planes), frustum culling, the ray-tracer camera and scene builder.
   About 48 call sites read the view or projection matrices (`getProjectionMatrix`, `getViewMatrix`).
7. **Other draw paths set their own model matrix**: wire, edge, outline and point programs (`SceneMesh.cpp` 1649, 1716, 4196),
   `FloorPlane.cpp` 24 and `LightFixtureRenderable.cpp` 20. Each would need the same treatment.

## Stage 1 as implemented (2026-11-14, awaiting the owner's build and test)

- `SceneMesh::render()` composes the model-view matrix in double precision with the translation relative to the exact eye
  (`eyeRelativeModelView()` in `SceneMesh.cpp`) and uploads it as `eyeModelView` plus `eyeRelative = true`; after the draw
  `eyeRelative` is set back to false so floor and batch draws sharing the program see the default.
- `main_scene.vert` and `main_scene_flat.vert`: `gl_Position` uses `projectionMatrix * (eyeModelView * skinnedPosition)` when
  `eyeRelative` is set; every world-space varying is unchanged (stage 2 is not done).
- The context is set in `ViewportWidget::setCommonUniforms()` for the primary camera (`RenderableMesh::setEyeRelativeContext`:
  the view matrix plus the exact eye from `Camera::getRenderPositionExact()`). A draw uses it only when its current view matrix is
  exactly that view, so the shadow pass, light views and anything else fall back to the old path.
- `Camera::updateViewMatrix()` in orbit mode now aims along the stored view direction instead of at the orbit target, so the
  view direction is no longer a difference of two large coordinates.
- Zoom precision floor: 2e-5 x the largest coordinate with eye-relative rendering on (was 1e-4).
- The temporary switch `MODELVIEWER_EYE_RELATIVE` was removed after the owner's test (no regressions, 2026-11-14). The fallback to the
  old path for views that do not match the stored one is structural and stays.
- New log line `[Precision]`, once per scene (on the first zoom refresh): largest mesh-local vertex coordinate against largest
  model translation, which is the missing measurement from the findings above.
- Not done: shading stability (stage 2), CPU consumers (stage 3), re-centring absolute-vertex formats (stage 4).

## Options

Each stage can be switched off by an environment variable and tested on its own.

- **Stage 1: eye-relative model-view (geometry only).** Build `modelView` per mesh in double precision on the CPU from the
  mesh's model matrix and an exact (double) eye position, so its translation is `R x (modelTranslation - eye)`, small near
  the eye. Use `projectionMatrix * modelViewMatrix * position` in the main-scene vertex shaders and the wire, edge, outline and
  point programs. Removes the camera-dependent jitter of the geometry. The one-time rounding of each mesh's placement to float
  (a fixed error of about one spacing at the coordinates) stays, but it does not move with the camera. Small to medium
  change: the matrix composition, a handful of vertex shaders, the uniform upload.
- **Stage 2: shading in a stable space.** Pass `cameraPos`, `lightPos` and shadow matrices relative to the eye, or do the
  lighting in view space, so the view and light vectors keep their precision. Medium change; touches the large fragment
  shader and the light and shadow uploads.
- **Stage 3: CPU consumers.** Move picking, the grid and floor, overlays and the ray-tracer camera to the same eye-relative
  double-precision math. Largest and most scattered; do it only if stage 1 exposes problems there.
- **Stage 4: re-centre absolute-coordinate formats at import** (fold the offset into the mesh's model matrix) so those
  models behave like node-transform ones. Only needed if the test models turn out to hold absolute vertices.

Expected effect: the zoom floor could move from `1e-4 x` the largest coordinate to roughly the larger of
`1e-6 x` the largest local vertex coordinate and the static placement error, which for the large model is about 10x to 50x
deeper. Not measured; estimated from float spacing.

## What would settle it before building

- Per test model: the largest mesh-local vertex coordinate against the largest model-matrix translation. A cheap log line at
  load would give both. If the translations dominate, stage 1 pays off most; if vertices are absolute, stage 4 comes first.
- Whether specular and shadow noise is visible at the floor depth (stage 2 matters) or the picture is stable once the geometry
  is (stage 2 can wait).

## Risks

- Every draw path that sets its own model matrix must switch together, or objects drawn by different programs will sit at
  slightly different places at deep zoom.
- Overlays (measurements, annotations, gizmos) use the float matrices; they could drift against the geometry at the extreme
  zoom until stage 3.
- The ray tracer builds its camera from the projection matrix and a float position; it is unaffected by stage 1 but would
  not gain from it.
