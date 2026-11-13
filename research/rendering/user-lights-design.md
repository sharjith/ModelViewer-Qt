# User-added lights: design note

Status: decisions made 2026-11-13; slice 1 built and owner-tested (ac6a11b); slice 2 owner-tested (4e5b77c); slice 3 built, not yet tested. Branch: `feature/user-lights` (from `dev` 34796ec).

## Goal

The user can add Point, Spot and Sun lights to the scene, select them (in the viewport and in the lights tree), move and aim them, edit colour, intensity and range, delete them, undo and redo, and have them saved in MVF and exported to glTF. Imported lights (KHR_lights_punctual) and user lights behave the same once they exist. Area lights are out of scope until glTF specifies them.

## Observations (verified in the code on 2026-11-13)

- **Data model.** Lights are stored per source file: `SceneGraph::_lightDataByFile` (`QHash<QString, GltfLightData>`), each `GltfLightEntry` holding a name, a world-space `GPULight` and an `enabled` flag (`include/Scene/GltfLightData.h`, `include/Scene/SceneGraph.h`).
- **A synthetic key already exists.** Legacy MVF files load their flat light list under the key `__mvf__` (`ModelViewer.cpp`, MVF load around line 8564). New MVF files save and load `punctualLightsByFile` (`sourceFile`, per-light `name`, `enabled`, `gpuLight`).
- **Pipeline.** `AnimationRuntimeController` keeps the parsed (original) lights, a per-light origin map (`_lightFileIndexMap`: file and index) and the repositioned lights; `buildGizmoLights()` filters by the `enabled` flag; `SceneGraph::buildEnabledLightList()` feeds the raster UBO and the ray tracers.
- **Deletion cleanup.** `ModelViewer::validateLightData()` clears the light data of every file that has no live mesh. This would also clear a synthetic key such as `__mvf__` or a user bucket, so it needs an exemption.
- **Model transform.** Imported lights follow the file's user model transform (`ViewportWidget::userModelTransformForFile`) and an appearance-preserving intensity/range scale in `updatePunctualLights()`. A user light has no file, so that mapping must be the identity for it.
- **UI.** Imported lights appear in the Environment panel's Punctual Lights tree (`treePunctualLights`), one parent item per file, one leaf per light. The model navigation tree (`treeWidgetModel`) holds geometry and is not involved.
- **Gizmos.** Fixtures are drawn by `ViewportWidget::drawLights()` from `buildGizmoLights()`; they are not pickable. The main (default) light is a separate pendant lamp driven by the position sliders and stays out of this feature.

## Proposed design

1. **Storage.** A reserved bucket `__user__` in `_lightDataByFile`. User lights are then covered by everything that already handles per-file lights: the tree, the enabled flags, the GPU lists, MVF save and load, the ray-tracer feed. Needed changes: exempt the bucket in `validateLightData()`; treat its model transform as the identity; give it its own handling in glTF export (see slice 4).
2. **UI.** The Punctual Lights tree gets a top-level "Scene Lights" group for the bucket, with Add Point, Add Spot and Add Sun buttons above the tree, and Delete. Selecting a row selects the fixture in the viewport and the other way round. A small properties area for the selected light edits name, colour, intensity, range and cone angles.
3. **Viewport.** Fixtures become pickable: a ray test against a bounding sphere per fixture, sized from the constant-screen-size scale, run before mesh picking when lights are shown. The selected fixture is drawn highlighted. For a selected spot light the beam cone is shown; the cone can then default to off for unselected spots (owner to decide).
4. **Move and aim.** The existing transform gizmo is attached to the selected light: translate for all types, rotate for spot and sun (their direction). Scale is not offered.
5. **Undo and redo.** One command type for add, delete and edit (snapshot of the `GltfLightEntry` before and after), following the pattern of `DeleteMeshCommand` and `TransformCommand`.
6. **Placement of a new light.** In front of the camera on the focus point (view centre, at about the distance of the scene centre); sun lights aim at the scene centre. Names: "Point Light 1", "Spot Light 1" and so on.

## Slices

1. Data model and tree: the `__user__` bucket, Add and Delete from the panel, save and load in MVF, undo and redo for add and delete. Lights appear as fixtures; no viewport interaction yet.
2. Selection: picking fixtures, tree and viewport selection in sync, highlight, properties editing with undo and redo.
3. Move and aim with the transform gizmo, with undo and redo.
4. glTF export of user lights (exporter work, see questions) and the ray-tracer check.

## Decisions (owner, 2026-11-13)

- The beam cone of a spot light is shown only while that light is selected. This applies to imported spots too; it lands with selection (slice 2), so slice 1 leaves the cones as they are.
- User lights stay where they are when models are moved or scaled. Only the default light follows the model.
- glTF export writes user lights as root-level nodes of the exported scene, whatever the number of imported files. Lights belong to the scene.

## Still open

- Whether user lights affect the CPU/GPU path tracers: by the existing pipeline they would (they go through `buildEnabledLightList()`); verified in slice 4.
- The cap on the number of lights in the raster shader (UBO size): checked in slice 1; the UI refuses to add beyond it.

## How to verify (owner)

Per slice, on `feature/user-lights`: build, add each light type, save and reload an MVF, undo and redo each operation, delete a model with user lights present (the lights must remain), and check the lit result in the raster view.

## Slice 1 notes (implemented, untested)

- `SceneGraph`: `userLightsKey()` (`__user__`), `totalLightCount()`, `insertUserLight()`, `removeUserLight()`.
- `ModelViewer::validateLightData()` skips the user bucket.
- `UserLightCommand` (add/remove) in `include/Commands` and `src/Commands`; undo restores a deleted light by value at its old index.
- `ViewportWidget::makeUserLight()` places a light above and to the right of the scene centre, nearer the eye, aimed at the centre; intensity = 2 x distance squared (the default light's calibration), spot cone 20 to 35 degrees. `userLightsChanged()` restores the default-light fallback when no light is left and tells the ray tracers.
- Panel: Add Point, Add Spot, Add Sun and Delete buttons above the tree; the "Scene Lights" group is listed first; only its leaves are selectable; the group box is always visible; adding a light turns Show Lights on; the buttons are disabled at 16 lights (the shader limit).
- Existing behaviour that applies to user lights too: while any punctual light exists, the default-light fallback light is not created (the default light itself still lights raster output through `lightSource`).
- The tree also has a permanent, italic "Default Light" row at the top, modelled on the System Camera row of the cameras panel (which likewise lists a synthetic "Captured Views" group first). It cannot be selected or deleted; its check box mirrors the Default Lights check box, which stays the source of truth (`syncDefaultLightRow()`).

## Slice 2 notes (written, not yet built)

Decisions (owner, 2026-11-13): imported lights are selectable but read-only; the aim is edited as a direction vector plus an "Aim at Scene Centre" button; intensity is shown as the raw glTF value (candela or lux).

- Selection: `ViewportWidget::selectLight(file, index)`; the lights tree selects any light; the selection is cleared when its light disappears. The selected fixture gets an accent ring (a circle facing the eye) and a tinted housing, and a spot shows its beam cone only while selected.
- `AnimationRuntimeController::buildGizmoLights()` can now return each light's origin (file and index) so the viewport knows which fixture is selected.
- `SceneLightPropertiesPanel` (under the tree): name, type, position, direction and Aim at Scene Centre, colour, intensity, range (0 = unlimited), inner and outer cone angles. Rows that do not apply to the type are hidden; imported lights are shown disabled.
- `EditUserLightCommand` applies each edit through the undo stack and merges consecutive edits of the same field of the same light; `SceneGraph::replaceUserLight()`.
- Not in this slice: picking fixtures in the viewport, the transform gizmo (slice 3).

## Slice 3 notes (built, not yet tested)

- The panel's spin boxes got an explicit minimum width (the panel was wider than the dock); an italic hint under the lights tree is shown only in ADS mode, which ignores punctual lights (the ADS path shades with the default light alone).
- Clicking a light fixture in the viewport (within 26 pixels of its centre) selects it; the selection replaces any mesh selection, and selecting a mesh ends the light selection.
- A selected scene light shows the transform gizmo: translate arrows (not for a sun, which has no position) and rotate arcs that turn the aim (not for a point light); the scale handles are hidden (`TransformGizmo::set*HandlesVisible`). The gizmo is shown while Show Lights is on, without needing the mesh gizmo to be requested.
- Drags are shown live and end as one undo step (`EditUserLightCommand`, Position or Direction). Imported lights stay read-only.
- Follow-up (owner request): the light gizmo's handles highlight on hover, Escape cancels a drag (the light returns and the rest of the mouse gesture is ignored), and the gizmo follows the light while it is dragged (it stays at the start point only while aiming, because a sun's head moves as its direction turns).

## Deselecting a light (owner request)

A light is deselected by clicking its selected row again (not its check box), clicking empty space in the lights tree, clicking empty space in the viewport (a click that hits no mesh and no light), or pressing Escape. Selecting a light, from the tree or by picking, now also clears any mesh selection.

## Reset to defaults (owner request)

Colour, intensity, range and the cone angles each have a small reset button beside them. Defaults: colour white; intensity 2 at the scene centre, so 2 x distance squared for a point or spot (the value the light got when added, but recomputed for its current position) and 2 for a sun; range 0 (unlimited); cone 20 and 35 degrees (the two angle boxes share one reset). Each reset is an undoable edit. Position and direction have no reset: Aim at Scene Centre covers the aim.
