# Changelog

## [2026.11.0] - 2026-11-XX (release day to be set)

The release that turns ModelViewer from a model viewer into a full engineering
visualisation tool: a ray tracer, a simulation-results viewer, general 3D data
plotting, measurement and annotation, CGAL-based mesh editing and surface analysis.
518 commits since `2026.7.0`. Internal refactors are summarised, not listed.

### Highlights

- **Ray Tracing** (the former "Path Tracing", renamed throughout the app): a CPU
  engine (Embree 4) and a GPU engine (NVIDIA OptiX), a live viewport hybrid, offline
  renders of any size, denoising (Intel OIDN, CPU or CUDA), EXR and other formats.
- **Simulation results**: open FEM / CFD results (VTK, CalculiX `.frd`, Exodus II,
  CGNS, MED, OpenFOAM, VTKHDF), colour them by any field, play the time steps,
  probe, cut, chart and compare them, and keep them inside an `.mvf` session.
- **3D data plotting**: surfaces, contours, lines, scatter, bars, voxels, vector
  fields and pathlines from CSV or from formulas, with shared linear / log / symlog axes.
- **Measure and annotate** in the viewport (many measurement tools, annotations, PDF
  report export).
- **Mesh tools**: Mesh Union, Shrink Wrap, Subdivide Surface, Reconstruct Surface,
  Repair Mesh, Fill Holes, split / merge / group, UV generation (smart project, LSCM, ARAP).
- **Surface Analysis and Mass Properties**: draft angle, zebra stripes, curvature,
  wall thickness, deviation, volume / mass / centre of gravity per mesh and material.
- A **What's new** dialog and **ten new tutorial lessons**, all translated.

### New Features

#### Ray Tracing
- CPU path tracer with a live PBR / ray-traced hybrid viewport and a settings dialog
  (Basic / Advanced / Diagnostics) with progress, export and persistence.
- GPU backend on NVIDIA OptiX 9.1: direct and indirect lighting, multi-bounce,
  textures and vertex colour, shadows, ambient occlusion, alpha-masked cutouts,
  environment lighting, progressive accumulation and interactive rendering during all
  camera motion, morph targets and skinned poses.
- Material model brought in line with the raster viewer: `KHR_materials_*` unlit,
  specular / IOR, clearcoat, sheen, anisotropy, iridescence, diffuse transmission,
  dispersion, volume and volume scatter, spec-gloss.
- Environment importance sampling with multiple importance sampling, EXR skyboxes,
  a shadow-catcher floor mode, ray-cone texture filtering, stochastic transmission.
- Denoising with OIDN (CPU or CUDA, with albedo / normal guides); multi-format and
  arbitrary-resolution offline export.

#### Simulation results
- Readers: VTK legacy and XML (`.vtk`, `.vtu`), CalculiX `.frd`, Exodus II, CGNS
  (unstructured and structured zones, polyhedra), MED (Salome / Code_Aster),
  OpenFOAM cases, VTKHDF; polyhedral and line cells; large results load step by step.
- Fields: node and cell (element) data, vectors, symmetric tensors, units with a
  file unit and a display unit, derived stress fields (von Mises, principals, max
  shear), an optional average of cell data onto the nodes.
- Display: colour map, range over one step / all steps / custom, contours, deformed
  shape, min and max markers, vector arrows, stress ellipsoids, a timeline with
  playback.
- Cutting and probing: plane sections, iso-surfaces, streamlines, direct volume
  rendering, hover probe, Plot Over Time and Plot Over Line charts (volume, shell
  and cell results) with a time cursor, extra curves from CSV, zoom, pan, PNG / CSV
  export and a second axis.
- Compare two results side by side or stacked; play results and 3D plots together
  on one clock; results are stored in `.mvf` sessions.

#### 3D data plotting
- Plot types: surface, contour, line, scatter (stems, error bars, filled), bar /
  histogram, voxel, quiver, pathlines and an image on a plane.
- Sources: CSV and pasted data, and formulas (surface, parametric surface and curve,
  vector field, implicit surface, streamlines, pathlines) with presets and parameters.
- Fills between curves and point sets, text notes placed by clicking in the view
  (with undo), reference planes, a section probe that shows data values, projected
  contours, animated pathlines.
- One set of **shared axes** (linear, Log 10, SymLog) for every plot in the box;
  quiver arrows and voxel grids follow the scale.
- Edit Plot changes a plot's definition in place; plots are saved in `.mvf`;
  points and lines export to glTF / GLB / OBJ.

#### Measurement and annotation
- Point, distance, minimum distance, 3-point angle, edge length, chain length, edge
  radius, diameters (cylindrical, conical), pitch circle diameter, face area,
  face-to-face and point-to-face, geodesic distance and more: CAD-style dimension
  rendering, hover preview, undo, saved in `.mvf`.
- Text annotations with draggable frames; PDF report export.

#### Mesh editing and repair
- Mesh Union (boolean), Shrink Wrap, Subdivide Surface, Reconstruct Surface from a
  point cloud, Repair Mesh, Fill Holes, Split by Connectivity, Merge by Adjacency,
  Merge Selected, Group, Purge Redundant Nodes; cross-document copy / cut / paste.
- UV generation: smart project, LSCM, ARAP, a cylindrical axis override and manual seams.

#### Surface Analysis and Mass Properties
- Draft angle, zebra stripe, mean curvature, wall thickness (including shell
  thickness for open surfaces), unsigned deviation against a reference mesh.
- Mass Properties dialog: volume, mass and centre of gravity per mesh and material,
  with material density and a Physical Properties tab.

#### Selection, materials and scenes
- Lasso selection, Filter by Material, Filter by Colour, Filter by Bounding Box
  (draggable box gizmo), material eyedropper and brush, named selection sets.
- Named Scene States and Batch Render Views.

#### Viewport and interface
- Unified MDI document layout with a seamless navigation overlay panel; tabbed
  Standard / Tools toolbar; Clipping Planes presets, draggable plane gizmo and a box
  clipping mode; Cavalier and Cabinet oblique projections; compass-corner views.
- Interaction-time level of detail for large models; a rendering-mode notice when
  results or plots are shown in PBR or ray-tracing mode.
- German, Spanish, French and Italian translations kept complete.

#### Import and export
- VRML (`.wrl`) import; Draco mesh compression; Exodus, CGNS and the other result
  formats under File > Import; unindexed point and line meshes round-trip through
  glTF, GLB, OBJ and `.mvf`.

### Improvements and Fixes
- Many glTF / GLB export and texture-handling fixes (KTX2 textures in GLB, image and
  buffer validity on reopen, morph targets kept in step with mesh optimisation).
- STEP / IGES / BREP: pre-tessellation, watertightness fixes for CAD parts, and
  better handling of unmeshable faces.
- Ray-traced mode: stable interactive convergence, camera and skybox coherence,
  correct orthographic views.
- Theme, tooltip, icon and dialog fixes across the application; dialogs remember
  their position and size.

### Platform and dependencies
- Qt 6.11.1, OpenCASCADE 8.0, Embree 4, NVIDIA OptiX SDK 9.1 (optional), Intel OIDN
  (optional), CGAL, and netCDF / HDF5 / CGNS for the result readers.

### Packaging
- Windows installer (Inno Setup) and a Linux `.deb` package, built and published
  locally; the Linux AppImage is no longer produced.

### Known limitations
- Simulation results and 3D plots are not drawn by the ray tracer.
- Streamlines on a cell (element) vector field, MED lazy loading and reloading a
  result from its source file are not available yet.
- Stress ellipsoids are offered only for fields named as a stress tensor.
- Pawn UUID ordering issue in the ABeautifulGame chess scene (carried over).

## [2026.7.0] - 2026-07-04

The largest release in the project's history — 410 commits since `1.2.3`/`Release-1.0`.
Highlights are grouped by theme below; internal refactors are summarized rather than
listed commit-by-commit.

### New Features

- **Exploded views** — automatic (radial, per-axis, or custom-vector) and manual
  gizmo-driven assembly explosion, with capture steps, reorderable sequences,
  presets, and parallel/sequential/separate animation export. Persisted in MVF.
- **Morph target (blend shape) animation** — full glTF import/export round-trip,
  playable through the Animations panel alongside skeletal clips.
- **Punctual lights (glTF lights)** — per-file, multi-model light system with
  point/spot/directional support, viewport indicators, and undo-aware deletion.
- **Interactive transform gizmo** — on-screen translate/rotate/scale handles,
  reused by exploded-view manual placement for non-destructive staging.
- **OCC B-Rep true-edge wireframe rendering** for STEP/IGES/BREP, plus
  feature-edge wireframe modes for OBJ/glTF meshes; new Mesh Edges and Shaded
  with Edges display modes.
- **Interactive view cube** overlay with switchable grid ground mode.
- **glTF camera support**, including animated tracking and Material Variants
  (`KHR_materials_variants`) with full export round-trip.
- **Texture Debugger panel** — live texture/extension overrides, Khronos-aligned
  channel isolation, geometry and texture channel inspection.
- **Unified Material Panel** — single editor merging material/texture editing,
  UUID-based material keys, unsaved-change tracking, user material library.
- **Scene-graph-aware tree widget** replacing the flat object list, with
  assembly-correct multi-select propagation, detachable navigation panel, and
  threaded/progressive model loading with GPU-safe finalization.
- **Dual-camera Z-up/Y-up system** and a full camera/shading math revamp aligned
  to the Khronos glTF reference viewer (PBR, sheen, clearcoat, transmission,
  anisotropy, iridescence, specular/spec-gloss).
- **Settings dialog** reorganized and fully wired end-to-end across every tab
  (General, Camera, Display, Rendering, Materials, Import/Export, Debug), with
  a new "Animate Progressive Fit" option.

### Improvements & Fixes

- Progressive-load fit-to-view no longer stalls mid-load; root-caused to a
  bounds-skipping fast path for scene-render-transformed meshes.
- Orientation-aware fit-to-screen using true view-space AABB projection,
  concurrent rotation + zoom, stable/tight bounding spheres for scene fitting.
- Per-viewport ray picking and multi-view ortho coherence fixes.
- Copy/cut/paste rebuilt as a tree-driven system with deferred-removal cut
  semantics; undo/redo added for mesh rename, deselection, and metadata delete.
- Numerous glTF/GLB export correctness fixes: material index remapping, ORM
  packing detection, roughness inversion, occlusion strength preservation,
  variant/vertex-color/punctual-light export, relative texture paths, embedded
  `data:` URI images, node-transform un-baking.
- STEP/IGES import: crash fixes, transfer guards, multi-body color fallback,
  multiple loading-performance passes.
- Shadow, SSS (subsurface scattering), IBL, and transmission pipeline
  correctness and performance passes; GPU memory leak fix in the animation
  loop (per-frame shadow recomputation).
- Detachable/floating panel UX overhaul (lock/reattach behavior) applied
  across all side panels.
- OpenGL initialization crash fixes on Linux/Wayland.

### Performance

- Large-assembly rendering: eliminated per-mesh shader bind/release in opaque
  and transparent passes, static-frame BVH subtree skipping, removed
  redundant GPU readbacks in view-cube drawing, MDI-safe shader program cache.
- Floor plane, shadow, and PBR shader micro-optimizations.
- AABB-based visibility culling and stencil-based capping-pass culling.

### Architecture (internal, not user-facing)

- Multi-phase mesh/render/runtime refactor: `TriangleMesh` → `RenderableMesh`,
  `AssImpMesh` → `SceneMesh`, introduction of `MeshGeometry`/`DeformableGeometry`,
  `SceneRuntime`, `SceneRenderController`, `ViewportInteractionController`,
  `AnimationRuntimeController`, `ExplodedViewRuntimeController`.
- `GLWidget` renamed to `ViewportWidget`; extensive SOLID-audit passes
  decomposing its responsibilities into dedicated controllers.
- Legacy `QDataStream` binary serialization removed; MVF3 (glTF-spec JSON +
  binary geometry chunk) is now the sole native format baseline.

### Documentation

- Quick Help dialog and all 14 existing tutorial lessons corrected for stale
  shortcuts and menu paths (`Ctrl+I`/`Ctrl+E` not `Ctrl+Shift+I/E`; Settings
  lives under Edit, not File).
- Four new tutorial lessons added: Exploded Views, Morph Target Animation,
  Node Transform Editing, and Edge & Wireframe Rendering.

### Packaging

- Windows installer (Inno Setup, `packaging/windows/mvinstaller.iss`) and a
  Linux AppImage are now built in CI and attached to tagged releases.
- Packaged (Release-configuration) installs ship a curated subset of the
  optional HDRI environment and PBR material preset libraries to keep
  installer/AppImage size reasonable; local Debug installs still get the
  full libraries for development.

### Known Gaps

- Pawn UUID ordering issue in the ABeautifulGame chess scene (deferred,
  severity pending assessment).
