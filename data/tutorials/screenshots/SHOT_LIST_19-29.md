# Screenshots to capture for lessons 19-29

Save each as a PNG in this folder under the exact name. The size is a guide; the lesson scales the picture to the page width.
Capture in the English UI; the translated lessons share the same pictures.

## Lesson 19 - Simulation Results

- [ ] `tutorial_19_first_result.png` (700x450) - Open sample-models/Simulation/FEM_box_static_stress.vtu, rotate to an isometric view and capture the whole window: the box with its colour legend, and the Simulation Results tab visible in the bottom-left dock.  
  *FEM_box_static_stress.vtu just opened, coloured by von Mises stress*
- [ ] `tutorial_19_results_tab.png` (420x520) - Same file as before: capture only the Simulation Results tab (crop the dock), scrolled to the top so Field, Component, Range, Colormap and Contours are all visible.  
  *The Simulation Results tab with Field, Component, Range, Colormap and Contours*
- [ ] `tutorial_19_field_list.png` (500x380) - Click the Field drop-down in the Simulation Results tab so the list is open (Displacement, von Mises, Tresca, principal stresses ...), and include the Component box below it in the crop.  
  *The Field list of the static box result*
- [ ] `tutorial_19_contours.png` (700x350) - Two captures of the box joined side by side: left = default smooth colormap, right = Contours set to about 8 bands. Same camera for both.  
  *Smooth colours (left) and the same field with contour bands (right)*
- [ ] `tutorial_19_deformed_plate.png` (700x450) - Open plate_with_hole.frd, go to the last load step, tick Show deformed shape (Scale factor Auto) and Mark minimum and maximum; view the plate face-on so the hole and the max label are visible.  
  *plate_with_hole.frd deformed, with the minimum and maximum marked*
- [ ] `tutorial_19_timeline.png` (700x350) - Open FEM_box_load_steps.frd, move the slider to step 3 of 4 and capture the viewport including the timeline bar at the top and the colour legend.  
  *The timeline at the top of the viewport while playing the load steps*
- [ ] `tutorial_19_modes.png` (700x350) - Open FEM_box_modes.frd with Show deformed shape on; capture Mode 1 and Mode 4 (or any two clearly different ones) joined side by side, timeline label visible in each.  
  *Two vibration modes of the box, deformed*

## Lesson 20 - Looking Inside a Result

- [ ] `tutorial_20_coloured_section.png` (700x450) - Open FEM_box_static_stress.vtu, enable one clipping plane (Section View), tick "Colour the Clipping Plane cut with the field", move the plane to about the middle and rotate so the cut face is visible.  
  *A box cut by a clipping plane, the cut face coloured by stress*
- [ ] `tutorial_20_iso_surfaces.png` (700x450) - Open block.cgns, Field = Temperature, tick Show iso-surfaces with about 5 levels, and one clipping plane so the nested shells are visible.  
  *Iso-surfaces of the temperature field inside the block*
- [ ] `tutorial_20_streamlines.png` (700x450) - Open duct.cgns, tick Show streamlines (Velocity, about 60 seeds, Arrowheads on); rotate to see the half-ring shape of the duct.  
  *Streamlines swirling through the duct, with arrowheads*
- [ ] `tutorial_20_volume_rendering.png` (700x450) - Open block.cgns, tick Show as volume (Temperature, Medium or High quality), and include the opacity curve widget in the Simulation Results tab in the shot (crop window + dock together).  
  *The temperature field of the block rendered as a volume*
- [ ] `tutorial_20_stress_ellipsoids.png` (700x450) - Open plate_with_hole.frd, Show stress ellipsoids with count 2000, last load step, view the plate face-on, zoomed so the hole fills about a third of the viewport.  
  *Stress ellipsoids around the hole of the plate*

## Lesson 21 - Charts and Probing

- [ ] `tutorial_21_hover_value.png` (700x420) - Open plate_with_hole.frd, view face-on and hover next to the hole so the value readout is visible beside the cursor (the cursor must be in the picture if your capture tool allows it).  
  *The value under the cursor, read directly from the plate*
- [ ] `tutorial_21_plot_over_time.png` (700x450) - Open FEM_box_thermal_transient.frd, right-click the middle of the far face > Plot Over Time Here, play to about step 10 and capture the chart window next to the viewport.  
  *A Plot Over Time chart with its orange cursor*
- [ ] `tutorial_21_csv_overlay.png` (700x450) - Chart from the previous shot after Add curve from CSV (thermal_probe_test.csv): both curves and the legend visible.  
  *The simulated history and the measured readings on one chart*
- [ ] `tutorial_21_frf_peaks.png` (700x450) - Open FEM_box_modes.frd, Plot Over Time Here, add FEM_box_modes_frf.csv; capture the chart with the peaks and the orange cursor on one of them.  
  *Mode frequencies on the x axis with the response curve and its peaks*
- [ ] `tutorial_21_plot_over_line.png` (700x450) - Open plate_with_hole.frd, Plot Over Line... and click the edge of the hole then the plate edge; capture the chart showing the stress falling off from the hole.  
  *Stress along a line from the hole edge to the plate edge*
- [ ] `tutorial_21_distribution.png` (600x400) - plate_with_hole.frd, Field = von Mises, click Distribution... and capture the histogram window.  
  *A histogram of the von Mises field*

## Lesson 22 - Comparing Results, Cell and Shell Data

- [ ] `tutorial_22_compare_side_by_side.png` (700x420) - Open ibeam_cantilever.frd, add ibeam_torsion.frd, Compare side by side with "Same colour range for both" and "Link the cameras" on, last load step, deformed shape on; capture the viewport.  
  *Bending (left) and torsion (right) of the same I-beam, side by side with one colour range*
- [ ] `tutorial_22_compare_controls.png` (420x360) - While comparing, crop the part of the Simulation Results tab with Compare with, Show in (layout), Same colour range, Link the cameras and Exit Compare.  
  *The compare controls in the Simulation Results tab*
- [ ] `tutorial_22_cell_data_flat_vs_smooth.png` (700x350) - Open cell_data_cube.vtk, Field = Element_Stress; capture the cube with the option off, then on (same camera) and join the two pictures side by side.  
  *Element_Stress drawn flat (left) and averaged to the nodes (right)*
- [ ] `tutorial_22_shell_plot_over_line.png` (700x450) - Open shell_plate_transient.frd, last step, Plot Over Line from the left edge to the right edge; capture viewport and chart together.  
  *Temperature along the shell plate with the chart beside it*
- [ ] `tutorial_22_openfoam_cavity.png` (700x450) - Open sample-models/Simulation/openfoam_cavity/cavity.foam, Field = U, Component = Magnitude, last time step; capture the viewport with the legend showing m/s.  
  *The OpenFOAM cavity sample, velocity magnitude with its unit in the legend*

## Lesson 23 - 3D Plots from CSV Data

- [ ] `tutorial_23_add_plot_dialog.png` (700x520) - Visualization > Plot 3D..., Open CSV... surface_ripple.csv, plot type Surface, X/Y/Z mapped, preview showing; capture the whole dialog.  
  *The Add 3D Plot dialog with surface_ripple.csv loaded and the columns mapped*
- [ ] `tutorial_23_surface_built.png` (700x450) - After Build Plot: the whole main window with the ripple surface, axes box, colour legend and the 3D Plot tab visible.  
  *The ripple surface built in the scene, with its axes box and colour legend*
- [ ] `tutorial_23_plot_types.png` (700x450) - Build scatter_clusters.csv, bar_histogram.csv and quiver_vortex.csv into one document (they share the axes) and capture the scene from an isometric view.  
  *A scatter, a bar chart and a vector field built into one scene*
- [ ] `tutorial_23_plot_tab.png` (420x560) - With the ripple surface selected, crop the 3D Plot tab from Active plot down to the plane options.  
  *The 3D Plot tab with the controls of the active plot*
- [ ] `tutorial_23_contours_and_sections.png` (700x450) - Rosenbrock Function (formula surface preset) with Contour lines = On the base plane (about 10 levels) and Show section curves on hover on; hover near a ring of the ripple so the red/green/blue curves and the data readout show.  
  *Contour lines on the base plane and the section curves through the hovered point*
- [ ] `tutorial_23_voxel_sphere.png` (700x450) - Build voxel_sphere.csv (Voxel / Volumetric, X/Y/Z = i/j/k, Colour value = occupancy) and capture the cloud from an isometric view.  
  *voxel_sphere.csv drawn as a semi-transparent sphere*

## Lesson 24 - 3D Plots from Formulas, Fills, Images and Notes

- [ ] `tutorial_24_formula_surface.png` (700x450) - Add 3D Plot > Formula surface > Preset Mexican Hat; Refresh Preview; capture the dialog and the preview in the viewport.  
  *The Mexican Hat formula surface, with the dialog and the status line*
- [ ] `tutorial_24_parametric_surfaces.png` (700x350) - Build Parametric surface presets Torus and Klein Bottle separately (two captures joined side by side), isometric view, axes box on.  
  *A torus and a Klein bottle built from parametric presets*
- [ ] `tutorial_24_fill_between_curves.png` (700x450) - Build line_band.csv as Line / Curve with the fill between the line and column z2 (Colour value = value); isometric view so both crossings are visible.  
  *A translucent band between two crossing curves, pinching to a line where they cross*
- [ ] `tutorial_24_fill_curtain.png` (700x400) - Build line_helix.csv as Line / Curve with Fill to Base Z = 0.  
  *The helix with a curtain down to the base plane*
- [ ] `tutorial_24_image_plane.png` (700x450) - Build surface_ripple.csv, then Image on a plane with image_gradient.png on the XZ plane (shared axes); capture so the "L" mark is visible.  
  *An image standing on a plane next to a surface plot*
- [ ] `tutorial_24_text_notes.png` (700x450) - Ripple surface with a note "Peak = 1.0" placed on the central peak; capture the note and the 3D Plot tab Text notes group together.  
  *A note on the peak of a surface*
- [ ] `tutorial_24_log_axis_shared.png` (700x450) - scatter_error_bars.csv and line_growth.csv built, Z axis scale = Log 10; capture the scene with the Axes group of the 3D Plot tab visible.  
  *A scatter plot and an exponential line sharing a logarithmic Z axis*

## Lesson 25 - Plots and Results Played Together

- [ ] `tutorial_25_double_gyre_pathlines.png` (700x450) - Build pathlines_double_gyre.csv (CSV time series), top view so the two cells and the folding trails are visible, legend showing the time range.  
  *Pathlines of the double gyre, coloured by time*
- [ ] `tutorial_25_formula_pathlines.png` (700x450) - Add 3D Plot > Formula pathlines (time-dependent) > Pulsating Vortex; Build; top view with the time legend visible.  
  *The Pulsating Vortex preset: rings whose colour bands bunch up where the particle slows*
- [ ] `tutorial_25_animated_pathlines.png` (700x420) - Double gyre pathlines with Animate pathlines ticked, stopped at about t = 10 s; capture the viewport with the timeline bar at the top.  
  *Pathlines part-way through the animation, with their head dots and the timeline*
- [ ] `tutorial_25_cavity_with_pathlines.png` (700x450) - openfoam_cavity result plus pathlines_openfoam_cavity.csv pathlines, All together selected, paused at t = 1; capture the viewport including the timeline and the two stacked legends.  
  *The cavity result and its pathlines playing together, with the playback box showing All together*

## Lesson 26 - Mesh Tools

- [ ] `tutorial_26_repair_mesh.png` (700x450) - Open RepairMeshTest.obj, Tools > Repair Mesh..., Generate; capture the dialog (status text visible) and the viewport with the repaired mesh.  
  *The Repair Mesh dialog with its status label, next to the repaired result*
- [ ] `tutorial_26_fill_holes.png` (700x450) - Open OpenCylinder.obj, Tools > Fill Holes..., click the detected loop in the list so it shows orange; capture dialog and viewport.  
  *A detected hole highlighted in orange before it is patched*
- [ ] `tutorial_26_subdivide.png` (700x350) - TorusTestCoarse.obj in Shaded with Edges mode before and after Subdivide Surface (Loop, 2 iterations); join the two captures side by side.  
  *The coarse torus (left) and its subdivided, smooth result (right)*
- [ ] `tutorial_26_shrink_wrap.png` (700x420) - Select a few Forklift parts, Tools > Shrink Wrap..., Generate; capture the wrapped shell next to the original (move the original aside or show wireframe).  
  *A shrink-wrap shell around several parts, next to the original*
- [ ] `tutorial_26_context_menu.png` (500x420) - Select two or more meshes in the Scene Tree, right-click one of them and capture the open menu with Split by Connectivity, Merge by Adjacency, Merge Selected, Mesh Union, Group and Select Parent visible.  
  *The mesh operations in the right-click menu*

## Lesson 27 - Measure, Annotate and Report

- [ ] `tutorial_27_distance_measurement.png` (700x450) - Open bottle.step, Tools > Measure..., tool Distance, two clicks on the bottle; capture the model with its dimension label and the dialog with the Measurements list.  
  *A distance measurement between two points on a STEP model, with the Measure dialog open*
- [ ] `tutorial_27_edge_radius_and_pitch.png` (700x450) - Open CAD.step (or any STEP part with holes), measure one hole with Edge Radius and a bolt-hole pattern with Pitch Circle (Enter to finish); capture the two labels on the part.  
  *An edge radius and a pitch circle measured on a CAD part*
- [ ] `tutorial_27_annotations.png` (700x450) - Tools > Annotate..., place two notes with meaningful text on a model (for example on the Forklift); capture the model with the two labels and leader lines plus the dialog list.  
  *Two annotations on a model, with the Annotate dialog beside it*
- [ ] `tutorial_27_report_dialog.png` (600x480) - Capture two views in the Cameras panel, then Tools > Export Report...; capture the dialog with the two views listed and "Include measurement/annotation table" ticked.  
  *The Export Report dialog with two captured views and the table option ticked*
- [ ] `tutorial_27_report_pdf.png` (700x500) - Open the exported PDF in a viewer and capture one page showing a view with its labels and the measurement table.  
  *A page of the exported PDF report with a captured view and the table*

## Lesson 28 - Analysis, Selection and Scenes

- [ ] `tutorial_28_wall_thickness.png` (700x450) - Open "MBB Gehause Rohteil.step", Tools > Surface Analysis..., Wall-Thickness with a limit that highlights the thin walls; capture the dialog and the coloured part.  
  *Wall thickness of a housing coloured on the surface, with thin walls highlighted*
- [ ] `tutorial_28_zebra_and_draft.png` (700x350) - One part (for example bottle.step) with Zebra Stripe, then Clear Overlay and Draft Angle with a vertical pull direction; join the two captures side by side.  
  *Zebra stripes (left) and a draft-angle analysis (right) on the same part*
- [ ] `tutorial_28_mass_properties.png` (700x450) - Open the Skate-Board assembly, give two materials a density (Physical Properties), Tools > Mass Properties...; select a row and capture the table with the mesh highlighted.  
  *The Mass Properties table for an assembly, with a row selected and its mesh highlighted*
- [ ] `tutorial_28_filter_by_material.png` (700x450) - Open the Forklift or Skate-Board assembly, Selection > Filter by Material, choose one material; capture the viewport with the matching parts highlighted.  
  *Every mesh using one material selected at once with Filter by Material*
- [ ] `tutorial_28_states_panel.png` (420x360) - After saving two scene states, capture the States panel in the document dock (crop the dock) with both entries listed.  
  *The States panel with two saved scene states*

## Lesson 29 - Ray Tracing in Practice

- [ ] `tutorial_29_pbr_vs_ray_traced.png` (700x350) - Futuristic Transport Shuttle (or the Tea-Pot on a reflective floor) with the same camera in PBR and, after a few seconds of accumulation, in Ray Traced mode; join the two captures side by side.  
  *The same scene in PBR (left) and ray traced (right)*
- [ ] `tutorial_29_dialog_basic.png` (560x520) - Visualization > Ray Tracing..., Basic tab; capture the whole dialog including Render, Stop and Export Image... at the bottom.  
  *The Basic tab of the Ray Tracing dialog*
- [ ] `tutorial_29_dialog_diagnostics.png` (560x480) - Start a render, switch to the Diagnostics tab and capture it with renderer, GPU, samples/sec and MRays/sec filled in.  
  *The Diagnostics tab while a render is running*
- [ ] `tutorial_29_rendered_image.png` (700x450) - Render the shuttle at 3840 x 2160 (Basic tab, denoiser on), Export Image... as PNG, and place that PNG here (a downscaled copy is fine).  
  *A finished high-resolution ray-traced image of the model*
- [ ] `tutorial_29_batch_render.png` (600x480) - Capture three views in the Cameras panel, then Tools > Batch Render Views...; capture the dialog with the three views ticked, the format and resolution set and a folder chosen.  
  *The Batch Render Views dialog with three captured views ticked*
