# User-added lights: design note

Status: decisions made 2026-11-13; slice 1 written, awaiting the owner's build and test. Branch: `feature/user-lights` (from `dev` 34796ec).

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
