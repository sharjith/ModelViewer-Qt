# Interactive bottleneck audit (large assemblies)

Status: first pass, 2026-10-28. Read-only code review; nothing here is measured unless it says so.
Target: interactive actions on a ~34k-part assembly (tree, selection, visibility, delete, undo).

Labels: **Observed** = seen in an import trace; **Code** = read in the source, cost inferred from the code; **Hypothesis** = needs a trace to confirm.

## Already fixed on `feature/JT-importer`

| Area | Cause | Fix | Evidence |
|---|---|---|---|
| Tree check/uncheck | per-item view updates | batched propagation | Observed (owner: "super responsive") |
| Context menu | `selectedMeshUuids()` recomputed by every `selectionChanged` listener, each walking every selected assembly's subtree | topmost-only walk + cache | Observed (trace: 0.8-3.4 s before the menu, ~6 s after) |
| Expand/Collapse All | per-item expand/collapse with animation and header re-measure | one `expandRecursively`, batch guard, skip leaves | Observed (owner) |
| Selecting a large node | `onItemSelectionChanged` applied a subtree per changed assembly; per-child `isSelected()` sibling search; viewport deselect/select of every mesh | topmost-only, index-based check, diffed visual sync | Code (owner testing) |
| Progressive load | tree rebuild restarted after every batch | rebuild deferred to the final update | Observed (owner) |

## New findings

### F1. Deleting many meshes emits one `structureChanged` per mesh (fixed)
`SceneGraph::removeMeshUuid` and `restoreMeshUuid` notify per mesh. `DeleteMeshCommand::redo/undo` looped over every mesh unbatched, so deleting N meshes restarted the tree rebuild and ran every structure validator N times.
Fix: `SceneGraph::StructureBatch` around both loops (one signal at the end). Code; untested.

### F2. `SceneRuntime` mesh detach/restore is O(store size) per mesh (fixed for Delete/Undo; other callers still use the single-mesh path)
Per deleted mesh: `moveToRecycleBin` scans the store linearly for the uuid; `detachMeshAt` erases from a vector (shifts the tail) and rewrites both id lists (`std::find` + a full shift loop); every index after the erased one is stale in `_uuidToIndex`, so the next `indexOfUuid` rebuilds the whole hash. Restore (`restoreDetachedMesh`) inserts into the middle and re-sorts `_displayedObjectsIds`.
Cost for N deletions from an M-mesh store: roughly N x M, with a full hash rebuild per lookup. Code.
Fix: `SceneRuntime::moveToRecycleBinBatch` / `restoreFromRecycleBinBatch` (one compaction or merge pass, one id remap, one uuid-index invalidation) behind `ViewportWidget::moveToRecycleBinBatch` / `restoreFromRecycleBinBatch`, used by `DeleteMeshCommand` redo/undo. `meshRecycleStateChanged` is emitted once per batch (its only listener ignores the uuid; before, it refreshed the Plot3D and Simulation panels once per mesh). The restore merge reproduces sequential insertion only for ascending original indices and falls back to the single path otherwise. Code; untested.

### F3. `isMeshDisplayed` is a linear `std::find` over the visible ids (reviewed, not changed)
Used per marker/annotation lookup and per cached item (`ViewportWidget.cpp` ~3864-3945, ~10171). Fine for few callers, quadratic if called per mesh. Code. The callers loop over markers or per-result data, not over meshes, so the cost is small. A bitmap cache is unsafe as long as the visible id lists are mutated through the non-const accessors of `SceneRuntime`; revisit only if a trace shows it.

### F4. Every visibility change walks all meshes several times (open, linear, unchanged)
`visibleIndicesFromState` (hash lookup per mesh), `SceneRuntime::setDisplayList` (builds the full id list and a `set_difference`), `recalculateVisibleSceneStats` (two passes), `updatePunctualLights`, `triggerShadowRecomputation` (`loadFloor()`), `updateFloorPlane`, `updateVisibilityUiFromState`. Each is O(N), so one action costs a few tens of ms at 34k; it matters for rapid repeated toggles and animation. Code. Hypothesis: `loadFloor()` is the one with real GL cost.

### F5. Selection command stores and re-applies the full selection (open, linear)
`SelectionCommand` copies the whole selection into a `QSet<int>` on construction and `setSelectionWithoutUndo` rebuilds tree selection and runs the full viewport/panel notification on undo/redo. Linear, but heavy with the `selectionChanged` listeners (selection-sets panel, simulation hook, texture debugger, exploded view). Code. Hypothesis.

### F6. Remaining `selectionChanged` listeners each call `getSelectedUuids()` (partly fixed)
The tree-side cache removes the repeated tree walk, but each caller still builds its own `QSet<QUuid>` from ids (34k hash inserts). `ModelViewer::getSelectedUuids()` now builds its set straight from the cached tree list (one hash lookup per mesh, not an id round trip). Each listener still builds its own set. Code.

### F7. `SceneTreeWidget` helpers that call `indexFromItem`/`isSelected` per item (open)
`indexFromItem` is O(siblings) (`lastIndexOf`). Remaining per-item callers are in `SceneTreeWidget.cpp` (about 22 hits of `indexFromItem`/`indexOf`/`isSelected`/`removeAll`); the ones reached by common actions (`refreshParentSelectionUpward`, `allDirectChildrenSelected`) are fixed. The rest still need a pass for nodes with thousands of direct leaf children. Code.

## Not yet audited
Hover/picking per-frame cost, render-loop culling, undo/redo for transforms, `MaterialPropertiesPanel` (14 selection-lookup calls), Filter-by dialogs, `SceneGraphExporter`.

## To confirm with traces
Run with `MODELVIEWER_IMPORT_DIAGNOSTICS=1`, perform Delete on a large node and Hide/Show of a large node, and look at `tree.slow-event`, `tree.slow-viewport-event` and the `viewer.display-list-*` checkpoints.
