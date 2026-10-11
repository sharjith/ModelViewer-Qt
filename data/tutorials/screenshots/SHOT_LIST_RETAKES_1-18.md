# Retake list - lessons 1-18

These screenshots show the interface as it was before 2026.11 and are now misleading. Keep the **file name** (the lessons already point to it) and roughly the
old size (given in brackets; the pictures are scaled to the lesson width anyway). Use the default dark theme and the same sample models as before.

What changed in the interface, so the new pictures show it:

* The **Model Tree** is no longer a tab of the right-hand dock. It is a panel that floats over the **left edge of the viewport** (Search box, pin button and the
  tree; hover the left edge to show it, pin it to keep it visible). The right-hand dock now has the tabs **Document**, **Properties** and **Environment**.
* The **Document** tab holds Variants, Animations, Cameras (top group) and Selections, States, Simulation Results, 3D Plot (bottom group).
* The menu bar is **File, Edit, View, Selection, Tools, Visualization, Window, Help**.
* The bottom toolbar has **Standard / Tools** tabs and a pin button.
* The mesh right-click menu now reads (top to bottom): Select Parent, Copy, Cut, Center Screen, Transformations, Edit Material, Hide, Show, Show Only,
  Duplicate, Split by Connectivity, Merge by Adjacency, Merge Selected, Mesh Union, Group, Delete, Mesh Info.
* Sessions (documents) are **tabs** across the top of the viewport area.

Models: `Skate-Board-Assy.stp`, `Forklift.STEP`, `Teapot.obj`, `Fox.glb`, `ANC101.step`, `ElectricHandDrill-2.obj` (all in `sample-models/`).

## Lesson 1 - Welcome

- [ ] `tutorial_01_main_window.png` (800x430) - Open Skate-Board-Assy.stp, isometric view, pin the Model Tree so it is visible at the left, Properties/Document dock open at the right; capture the whole main window.
- [ ] `tutorial_01_interface_labeled.png` (800x450) - The same window with callouts: **Menu Bar**, **Viewport**, **Model Tree** (left), **Dock Panels** (Document / Properties / Environment, right), **Toolbar** (bottom, pinned so it is visible), **View Cube**. The lesson caption says "Viewport, Model Tree, Dock Panels, Toolbar, Menu Bar".

## Lesson 2 - Opening Models

- [ ] `tutorial_02_file_menu.png` (347x261) - Crop the File menu open under the new menu bar (File Edit View Selection Tools Visualization Window Help visible), cursor on Open.
- [ ] `tutorial_02_multiple_models.png` (800x430) - Import two or three models (e.g. LightsPunctualLamp.glb and DiffuseTransmissionPlant.glb); capture the window with the Model Tree pinned at the left listing all items.

## Lesson 3 - Basic Navigation

- [ ] `tutorial_03_toolbar_revealed.png` (1256x127) - Crop only the revealed toolbar with its **Standard** tab selected and the pin button visible (wide strip, as before).

## Lesson 4 - Selecting Objects

All on Skate-Board-Assy.stp with the Model Tree pinned at the left.

- [ ] `tutorial_04_single_selection.png` (800x430) - Click one part (the wheel axle); capture the window with the glow in the viewport and the row highlighted in the tree, status bar "Selected 1 object".
- [ ] `tutorial_04_rubber_band.png` (800x430) - Rubber-band rectangle (blue) over several parts, captured while it is drawn, tree rows highlighted.
- [ ] `tutorial_04_list_selection.png` (300x414) - Crop the Model Tree with a sub-assembly selected (highlighted rows).
- [ ] `tutorial_04_search_filter.png` (641x814) - Crop the Model Tree with "wheel" typed in the Search box (red circle around the box), matching rows highlighted.
- [ ] `tutorial_04_context_menu.png` (300x375) - Right-click a selected mesh in the viewport; crop the open menu (new items, see above).

## Lesson 8 - Manipulating Objects

- [ ] `tutorial_08_duplicate.png` (1280x720 collage) - Teapot.obj: right-click the selected mesh > **Duplicate** (menu open, Duplicate highlighted), then the result with two teapots and both rows in the tree; join the two captures as before.

## Lesson 9 - Materials and Textures

- [ ] `tutorial_09_uv_generation.png` (800x570 collage) - Forklift driver mesh: **Tools > Generate UVs...** dialog open (before), then the same mesh with a fabric texture (Properties > Materials, Denim) after; join the captures. Replaces the old right-click "Generate UVs".

## Lesson 11 - Working with Visibility

- [ ] `tutorial_11_checkboxes.png` (800x782) - Model Tree crop with "wheel" in the Search box, the wheel rows unchecked and **Hide/Show All** circled.
- [ ] `tutorial_11_show_only.png` (800x451) - Forklift: right-click menu with **Show Only** highlighted (left), result with only the driver visible (right).
- [ ] `tutorial_11_swap_visible.png` (800x512) - Skate-Board-Assy with the wheel assemblies hidden, tree on the left with the unchecked rows.

## Lesson 12 - Advanced Features

- [ ] `tutorial_12_mesh_info.png` (800x483) - Forklift: right-click > **Mesh Info** (menu open) next to the Mesh Info dialog.
- [ ] `tutorial_12_multiple_sessions.png` (800x430) - Three documents open (Skate-Board-Assy.stp, Forklift.STEP, ANC101.step) shown as **tabs** along the top; capture the window with the tab strip visible.

## Lesson 16 - Morph Targets

One picture now covers the whole lesson (the two others were removed): Fox.glb with Document > Animations open.

- [ ] `tutorial_16_animations_panel.png` (800x513) - Dock open on **Document > Animations**, the **Run** clip selected, Loop/Speed/Play visible (red circle on the clip).

---

Lower priority (not misleading, redo only if convenient): the other full-window pictures that still show the old Navigation tab or the old toolbar -
`tutorial_03_fit_all_before/after`, `tutorial_05_*` with the toolbar, `tutorial_08_transform_panel/rotate/scale/translate`, `tutorial_15_panel_overview`,
`tutorial_15_manual_placement`, `tutorial_18_mesh_edges_mode`, `tutorial_18_shaded_with_edges_mode`, the gifs `tutorial_02_drag_drop` and
`tutorial_03_rotate/pan/zoom_gesture`, plus the Environment, Material, Clipping and Settings panels (`tutorial_10_environment_panel`, `tutorial_10_floor`,
`tutorial_09_material_panel`, `tutorial_12_clipping_planes`, `tutorial_13_settings`) which gained a few controls.
