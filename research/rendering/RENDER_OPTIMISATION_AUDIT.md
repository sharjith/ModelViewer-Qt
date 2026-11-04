# Rendering optimisation audit (4 November 2026)

Scope: the large-model rendering work of 29 October to 4 November 2026, and the code it touches. It was prompted by hover and selection symptoms seen on a very large assembly (about 4,400 visible meshes, so draw batching was active). Method: code reading only. Nothing was built or run, so every finding below is an observation from the source, not a measured result. Design background: [DRAW_BATCHING_DESIGN.md](DRAW_BATCHING_DESIGN.md).

## Commits in scope

| Commit | Change | Touchpoints |
| --- | --- | --- |
| e2b3a12 | Draw batching, raw-GL mesh buffers, cheaper per-mesh uniforms, culling | `DrawBatcher`, `FastGlObjects`, `RenderableMesh`, `SceneMesh`, `ViewportWidget` passes |
| 238355a | Per-mesh batch validation cached by a render-state epoch | `MeshInstanceState` epoch, `DrawBatcher::drawBatches` |
| e8d71e2 | Selected meshes stay in their batch, drawn in a second pass per group | `DrawBatcher`, `SceneMesh::prepareBatchDraw` |
| bf1bd0b | Colour-pick pass culled by bounding sphere | `SelectionManager::processSelection` |
| 3212c21 | Small-part screen-size culling during interaction | `VisibilityComputationHelper`, `ViewportWidget` |
| 3fe53df, 0d500cd, b2f9433 | Lazy GL objects, GPU residency (evict and restore) | `RenderableMesh::ensureResident`, `getVAO` |
| 58d0690 | Single-sample rasterisation while interacting | `ViewportWidget` render set-up |
| 3e179c2, 7c61596 | Floor reflection: skipped while orbiting, batched, culled by mirrored frustum | `ViewportWidget` reflection pass |
| e01c3e6, af09cd6, cba45c1 | Hollow Mesh and Mesh Edges faster orbit, batched wire passes, fast path for every camera motion, batched transparent wire passes | `ViewportWidget` wire passes, `SceneMesh::prepareWireBatchDraw`, `DrawBatcher::drawTransparentSequence` |

## Findings

### F1. `hovered` uniform leaks into batched runs (fixed, untested)

Observation: a batched run is drawn through `prepareBatchDraw` or `prepareWireBatchDraw`, neither of which sets the `hovered` program uniform. The hovered mesh is always kept out of the batches and drawn on its own, which leaves `hovered = true`. Where the pass then draws further batched runs, those inherit it.

- Opaque Shaded and Mesh Edges fill: batches are drawn first with `hovered = false`, so they are safe.
- Transparent Shaded, transparent Mesh Edges fill and transparent Hollow Mesh: `DrawBatcher::drawTransparentSequence` interleaves individual draws with batched runs and never reset the flag. Every run after the hovered mesh, and the whole transparent pass after an opaque pass that ended on the hovered mesh, was drawn in the hover colour.
- Highlight look: in the filled modes hover and selection brighten the mesh's own colour (selection about twice as strongly as hover; see `main_scene.frag`); the solid gold hover and blue selection colours exist only in `wireframe.frag` (Hollow Mesh, Wireframe).
- Observed effect: hovering a red part lit up the semi-transparent yellow fixtures. Turning batching off removed it, because without batching the hovered mesh is rarely the last one drawn.
- Change: `DrawBatcher.cpp`, in the `flush` lambda of `drawTransparentSequence`, both branches set `hovered = false` after the prepare call when `hoveredId >= 0`.

### F2. Element buffer not rebound in the pick and shadow passes (fixed, untested)

Observation: a mesh's VAO keeps whichever element buffer the last draw bound. Normal draws (`RenderableMesh::render`, `SceneMesh::render`, `renderWireframeFast`) rebind the full or the coarse LOD index buffer every time. Two passes did not:

- `SelectionManager::processSelection` drew `getIndices().size()` indices.
- `RenderableMesh::renderShadow` drew `_nVerts` indices.

The interaction fast path draws the coarse LOD buffer, and a mesh that has joined a merged batch is no longer drawn through its own VAO, so its binding can stay on the coarse buffer for good. A full-count draw from the shorter buffer reads past its end. The result is undefined, in practice missing or partial geometry in the pick buffer.

- Hypothesis, not verified: this makes some large (LOD-bearing, 2,000 triangles or more) meshes impossible to hover or click, which fits "some meshes do not highlight and one never selects".
- Change: `RenderableMesh::bindFullIndexBuffer()` added; used in `processSelection` and `renderShadow`.

### F4. Selection colour hidden by coincident meshes (fixed, untested)

Observation: a mesh-info listing of three selected meshes showed three separate Part nodes (distinct body UIDs, identical geometry and mass) under one assembly. When bodies repeat at one position, which copy wins the depth test depends on draw order and rounding. The opaque passes use `GL_LESS`. In batched drawing the selected meshes are drawn in a second pass after the unselected ones, so a selected mesh loses every tie and its selection colour is hidden; hover and the pick pass resolve the same ties differently, which looked like "hover works, click does not". Whether the copies are exactly coincident was reported by the owner, not measured here.

- Change: new `include/Render/ScopedSelectionDepthBias.h`. A selected opaque triangle mesh is drawn with a polygon offset that pulls it toward the camera, relative to the caller's own offset (reduced but kept positive where the caller pushes the fill back for an edge overlay, so edges of the selected mesh stay visible). Applied in `SceneMesh::render` and in the selected pass of `DrawBatcher::drawBatches`.
- Not covered: the hover highlight (hovered meshes are drawn individually after the batches, so coincident twins can still hide it), selected transparent meshes, and the wire passes.
- Risk: a selected mesh may now show through a genuinely adjacent surface at a very small gap.

### F5. Same ambiguity in Wireframe and Hollow Mesh (fixed, untested)

F4 was confirmed by the owner for Shaded. Wireframe (feature-edge GL_LINES) and Hollow Mesh (polygons in line mode) still showed it: the highlighted lines tie with the lines of coincident twins. A polygon offset does not apply to GL_LINES, and Hollow Mesh would need a separate line-offset state, so both use one mechanism in `shaders/wireframe.vert`: when `isWireframePass` is false and the mesh is hovered or selected, clip-space depth is pulled forward by 1e-6 of w (about eight steps of a 24-bit depth buffer). The line-overlay pass of Mesh Edges is unchanged (it does not signal hover or selection). The shader file must be redeployed with the build. Risk: an uncertain size for very small depth ranges or a 16-bit depth buffer, where eight steps are a larger fraction of the scene.

### F6. Hover highlight hidden by coincident meshes in the filled modes (fixed, untested)

F4 biased only selected meshes. The hovered mesh is drawn individually after the batches, so against coincident twins it loses depth ties the same way, and the hover colour was hidden (reported for Shaded with Edges). `ViewportWidget::drawOpaqueMeshes` now wraps the hovered, unselected triangle mesh's `render()` in `ScopedSelectionDepthBias` in the Mesh Edges fill loop, the Shaded with Edges fill loop and the Shaded per-mesh loop. In the edge modes the helper keeps the fill behind the line overlay (clamped positive offset). Not covered: hovered transparent meshes and the Hollow Mesh non-wire-shader fallback.

### F7. Highlight bias swallowed a near-coplanar patch at a distance (fixed, untested)

Observation (owner): with the structure highlighted, a red patch on a grey panel took the panel's grey when zoomed out and kept its colour when zoomed in. Reading: the F4 and F6 polygon offset (slope factor 1.0, units 2.0) and the F5 wire bias (about eight depth steps) are fixed in depth-buffer steps, and one step spans more real distance the farther the surface is, so far away the highlighted panel was pulled in front of a patch that stands only a hair proud of it. The slope-scaled part grows with the surface's angle to the view, so it dominated on the oblique panel.

- Change: `ScopedSelectionDepthBias` now uses slope factor 0 and 1.0 unit; the wire shader bias is 3e-7 NDC (about two to three steps). Exactly coincident copies differ by at most a step or two, so the tie is still broken.
- Limit: a gap smaller than the bias at that distance cannot be resolved; it is a precision limit of the depth buffer, not of the code. Meshes drawn individually against batched twins can differ by more than the bias, so a stacked copy may still lose a tie at long range.
- Retest: repeat the zoomed-out highlight on the same panel and patch; then repeat the stacked-copy checks of F4 to F6.

### F3. Items reviewed with no defect found

- Render-state epoch (`MeshInstanceState`): selection, deselection, material and texture changes, geometry, transform, skin, morph and debug overrides all reach `noteRenderStateChanged` or `markUniformsDirty`. Selection reaches it through `select()` and `deselect()`. A sampled recheck of 256 meshes per frame and a self-disable after three mismatches act as a safety net.
- Batch group key (`SceneMesh::computeUniformStateSignature`): includes albedo and the other colour vectors, texture state, vertex-colour flag and blend state, so two different colours do not share a group on paper.
- Pick-pass bounding-sphere cull: pose-driven, skinned and morphing meshes are always drawn. It depends on `getBoundingSphere()` following the drawn pose; not checked for meshes with a preserved node transform.
- Batched wire path: pass defaults for `selected` and the other flags are restored after `drawBatches`; the per-mesh flag caches (`WireFlagUniform`) start unset, so the first draw uploads.

## Not audited

- Fast-mode hover (CPU ray against bounding sphere and `intersectsWithRay`): its result for meshes with a preserved node transform or for LOD meshes is unchecked.
- `isMeshAnimationVisibleForSelection`, exploded-view draw paths, floor reflection and shadow culling logic, single-sample switching, residency eviction paths.
- The earlier symptom of a model changing colour once after load, with no variant click: not explained by anything above.

## Retest list for the owner

1. Batching on: hover a red part; the yellow fixtures must not highlight (F1). Check Shaded, Mesh Edges and Hollow Mesh.
2. After an orbit or zoom, hover and click the dense truss parts that previously failed (F2). Repeat after the model has been still for several seconds.
3. Shadows on: orbit, stop, and look for missing or broken shadows from large parts (F2).
4. Select one of three stacked copies and confirm the selection colour shows (Shaded, Mesh Edges, Shaded with Edges); also confirm the edges of a selected part are still visible in the edge modes, and that a selected part does not bleed through a thin gap to a neighbour (F4).
5. Report anything still failing with the display mode, so the remaining paths can be traced.
