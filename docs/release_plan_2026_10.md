# Release plan: ModelViewer 2026.10.0

Status: **plan, for discussion** (2026-10-10). Nothing in the product changes by writing this plan.

The last release is `Release-2026.7.0` (published 2026-07-05, Windows installer + Linux AppImage). Since then `dev` has gained about **519 commits**, most of them one large new area (Simulation results and 3D data plotting) plus mesh-editing, measurement and viewport work. This plan covers everything a proper release needs besides the code: release notes, help, tutorials, the website, the splash image, packaging, quality checks and publishing.

## 1. Release identity

| Item | Proposal |
|---|---|
| Version | **2026.10.0** (the project's year.month.patch scheme: 2026.7.0 was July) |
| Where the number lives | `CMakeLists.txt` lines 367-369 (`APP_VERSION_MAJOR/MINOR/PATCH`; feeds `config.h`, the About dialog and CPack). `vcpkg.json` still says `1.0.0` (a manifest label only; can follow). |
| Tag / release | tag `Release-2026.10.0`; `master` brought up to date from `dev` first (master is still at the 2026.7.0 site overhaul) |
| Artifacts | Windows installer (.exe) and a Linux **.deb** built in WSL (replacing the AppImage, as planned in the release pipeline notes), both uploaded with `gh release create` from local builds (CI is not part of the release path) |

## 2. What is new since 2026.7.0 (the feature inventory)

This is the list every document below is written from. It is drawn from the branches merged to `dev`; the first step of the work is to reconcile it with `git log 2026.7.0..dev` so nothing is missed.

**A. Simulation results (new module).**
- Readers for VTK (.vtk/.vtu), CalculiX .frd, Exodus II, CGNS, MED, OpenFOAM, VTKHDF; units (file and display); node fields and cell (element) data, vectors and tensors; derived stress fields (von Mises, principals, max shear); multi-step results with a timeline.
- Display: colour map, range (step / all steps / custom), contours, deformed shape, min/max markers, vector arrows, stress ellipsoids (field, size, count), cell data as stored or averaged onto the nodes.
- Cutting and probing: sections, iso-surfaces, streamlines, direct volume rendering, probe; XY charts (plot over line / over time) on volume, shell/surface and cell results, with cursor/seek, extra curves from CSV, zoom/pan, PNG/CSV export, second axis.
- Compare mode, synchronised playback with 3D plots, saving a result inside an `.mvf` session, a notice when switching to PBR / path tracing.
- Sample results and README recipes in `sample-models/Simulation`.

**B. 3D data plotting (new module).**
- Surface, contour, line, scatter, bar/histogram, voxel, quiver, pathlines, formula-generated sources (surface, parametric surface/curve, vector field, implicit surface, streamlines, pathlines), image planes (opacity, readable back).
- Fills (to a base plane or between two curves/point sets), stems, error bars, text notes (click to place, drag, edit, undo/redo), reference planes, **shared** linear / log / symlog axes for every plot, section probe with data readout.
- Export of points/lines to glTF/GLB/OBJ, save/reopen in `.mvf`, translated formula messages.
- Sample data and README recipes in `sample-models/Plot3D`.

**C. Mesh and CAD tools.** Mesh Union, Shrink Wrap (CGAL alpha wrap), Subdivide Surface, Reconstruct Surface from points, Repair Mesh, Split / Merge by adjacency / Merge selected / Group, cross-document copy/cut/paste, UV smart project, selection and material tools (lasso, filter by material/colour, eyedropper/brush, named selection sets), STEP import watertight fixes.

**D. Measurement and annotation.** Measurement tools (19, including geodesic distance, cylindrical diameter, edge radius/concentricity), annotations, reports and export.

**E. Rendering and path tracing.** CPU and GPU (OptiX) path tracer improvements, OIDN denoising, environment importance sampling, interactive accumulation, shadow-catcher ground, camera basis fix for results in ray-traced mode.

**F. Application and interface.** MDI unified panels, tabbed Standard/Tools toolbar, dialogs moved to `.ui` files with remembered geometry, theme fixes, German / Spanish / French / Italian translations kept at 0 unfinished, status balloon.

**Known limitations to state plainly in the notes:** path tracing does not draw Simulation results or 3D plots (by design); streamlines on a cell *vector* field are not supported; MED lazy loading and reload-from-source are not implemented; a tensor field is offered as ellipsoids only when it is named as a stress.

## 3. Workstreams

Each is its own branch, written by the assistant and checked by the user, like the rest of the work. "Build lock" applies: the assistant never builds or runs; the user builds, runs and captures screenshots.

### W1. Release notes and CHANGELOG
- A `## [2026.10.0]` entry in `CHANGELOG.md` in the same style as 2026.7.0 (New Features / Improvements & Fixes / Known limitations), grouped by the themes above; internal refactors summarised.
- The GitHub release body: a shorter "highlights" version with screenshots and the download list.
- Source: the inventory in section 2, reconciled with the git history.

### W2. README and website
- `README.md`: Features Overview gets Simulation and 3D plotting sections; *Supported File Formats* gets the result and CSV/plot-data formats; Acknowledgments and Architecture updated for the new modules and libraries (netCDF, HDF5, CGNS, MED as applicable).
- `docs/index.html` (GitHub Pages): feature sections and screenshots for the two new areas; the release number and download links; localized screenshots (the previous overhaul already localized them).
- Third-party licence notices checked for every dependency added since 2026.7.0.

### W3. In-app help
- **Quick Help** (`QuickHelpDialog`, ~1,450 lines of topic pages): new pages for Simulation (panel, timeline, probe, charts), 3D Plot (add/edit, controls, notes, axes), the new mesh tools and measurement additions, plus the keyboard and mouse tables for what was added (chart wheel/pan, note placement). Each page is plain `tr()` text, so the four translations are updated with it.
- **Tooltips and status messages** reviewed for the new panels (several were written in this work with multi-line tooltips already).
- **About dialog**: version line, the library list (adds the result-reader libraries), credits.
- **Optional: a "What's new" dialog** shown once after an update, linking to the new tutorial lessons. (A decision, section 5.)

### W4. Tutorials (`data/tutorials`, a multi-page HTML course, lessons 1-18 today)
New lessons, continuing the numbering and the existing look (`common-styles.css`), each tied to a sample file that already ships:

| Lesson | Topic | Sample |
|---|---|---|
| 19 | Simulation results: opening, fields, ranges, deformed shape, the timeline | `FEM_box_*.frd`, `stress_states.vtu` |
| 20 | Looking inside a result: sections, iso-surfaces, streamlines, volume rendering, arrows and stress ellipsoids | `FEM_box_static.frd`, `openfoam_cavity` |
| 21 | Charts and probing: plot over time/line, adding curves, zoom/export | `thermal_probe_test.csv`, `shell_plate_transient.frd` |
| 22 | Comparing results, cell data and shell results | `cell_data_cube.vtk`, `plate.vtk` |
| 23 | 3D plots from CSV: surface, scatter, line, bar, voxel, quiver | `sample-models/Plot3D/*.csv` |
| 24 | 3D plots from formulas, fills, image planes, notes, shared axes | presets, `line_band.csv`, `image_gradient.png` |
| 25 | Playing plots and results together | `pathlines_openfoam_cavity.csv` + cavity |
| 26 | Mesh tools: union, shrink wrap, subdivide, reconstruct, repair, split/merge | (shipped meshes to be chosen) |
| 27 | Measure and annotate (if not already covered by lesson 12; audit first) | |
| 28 | Path tracing in practice (distilled from `ModelViewer_RayTracing_HowTo.pdf`) | |

- The tutorial index and the `TutorialDialog` page list are updated; the lessons are English HTML (the dialog's own labels are translated). Whether the lessons themselves get translated is a decision (section 5).
- Screenshots: the assistant writes each lesson with a numbered **shot list** (what to open, what to click, what the picture must show); the user captures them into `data/tutorials/screenshots` (the same way the existing lessons do).
- The package's `data/tutorials/README.md` (still says lessons 1-14) is corrected.

### W5. Splash screen
The current artwork (`res/Splashscreen.png`, also in `screenshots/`) is a dark showroom render with the car, a chrome sphere, a wireframe car and the library logo strip. It already says "Advanced visualization for engineering and beyond" with **Science** in the tagline, but its three feature lines are *STEP · IGES · glTF · FBX · OBJ*, *PBR & GPU path tracing* and *Measure · Section · Annotate*: nothing in it tells a new user that the application now views **simulation results** and draws **3D data plots**, the largest part of this release. The logo strip lists Qt, OpenGL, glTF, OpenCASCADE, embree, OptiX, CGAL and Assimp; it does not show the result-reader libraries or OIDN (which is optional).

Proposal: keep the artwork and add a fourth line in the same style as the other three (icon + text), for example **Simulation results · 3D plots**, or fold it into the existing lines; no version number on the image (the splash already reports startup progress). Options for producing it: (a) the assistant composes a draft by overlaying the new line on the current image with the same font and an icon drawn to match, for the user's approval; (b) the user edits the original layered artwork. The splash code (`StartupSplash::prepareArtwork`) takes any PNG of the same size, so no code change is needed. The About dialog and the website banner that reuse the image would follow.

### W6. Translations
- Final `lupdate` (run directly, not through the build target), fill de/es/fr/it for every new string, check placeholders; `unfinished` = 0 for the four languages before the release is cut. Largely done feature by feature, so this is a verification pass plus the new Quick Help / About / tutorial-dialog strings.

### W7. Packaging and licences
- Bump `APP_VERSION_*`; CPack settings and the Windows installer text; Linux `.deb` built in WSL with `cmake --build out/build/linux_system_release --parallel` (not `--target ModelViewer`), then `cpack`; no AppImage.
- Check that the data directory in each package contains the new samples (`sample-models/Simulation`, `sample-models/Plot3D`) and the extended tutorials, and that the optional dependencies (netCDF, HDF5, CGNS) are bundled by the installer so Exodus / CGNS / VTKHDF open on a clean machine.
- Licence file / third-party notices for each new dependency; GPL compatibility of anything added (the project is GPL-3.0).

### W8. Quality gate (before tagging)
1. Unit tests: `result_tests`, `plot3d_tests`, `mvf_tests` all green on the release build.
2. A manual checklist, one line per feature, taken from the README recipes in the two sample folders and from the lessons above (the lessons double as the test script: if a lesson step does not work, the release is not ready).
3. Upgrade check: open `.mvf` files saved by 2026.7.0 (and by recent `dev`) and check they load and re-save; open a session with a result and plots, save, reopen.
4. Clean-machine check: install the Windows build on a machine without development tools, start, open the tutorial, open one file of each family; the same for the `.deb`.
5. Language check: switch to each of the four languages, open the Simulation and 3D Plot panels and one error message.
6. High-DPI and second-monitor spot checks (the interactive path tracing and overlay widgets had issues there before).
7. The UI-test idea stays parked; this checklist is its manual stand-in.

### W9. Publishing
Order: finish W1-W8 on branches, merge to `dev` → merge `dev` to `master` → tag `Release-2026.10.0` → local builds of both artifacts → `gh release create` with the notes and files → refresh the GitHub Pages site → announce. Merged branches are kept (as agreed).

## 4. Order and effort

| Step | Content | Notes |
|---|---|---|
| 1 | Inventory reconciliation, decisions in section 5 | short; everything else depends on it |
| 2 | W5 splash draft, W1 CHANGELOG draft | independent, quick to review |
| 3 | W3 Quick Help + About + What's-new decision | code (and translations) |
| 4 | W4 tutorials, in the order 19-25 first, then 26-28 | largest; screenshots by the user, lesson by lesson |
| 5 | W2 README / website | after the screenshots exist |
| 6 | W6 translation pass, W7 packaging, W8 quality gate | last, on the release candidate |
| 7 | W9 publish | |

## 5. Decisions needed

1. **Version number and name:** 2026.10.0, or another? Release date target?
2. **"What's new" dialog** after the update: yes / no.
3. **Tutorial lessons:** English only (as now), or translated with the UI? (Translating ten HTML lessons four times is substantial.)
4. **Splash:** add a fourth feature line (draft by the assistant for approval), or have the artwork redone by hand? Anything else on it to change?
5. **Scope of the tutorial set:** all of lessons 19-28, or Simulation + 3D plotting first (19-25) and the rest later?
6. **Website:** full refresh with new screenshots, or only the version, downloads and a short "what's new" section?
7. **Linux:** .deb only, as planned, or also keep an AppImage?
8. **A long-form guide** (a PDF like the ray-tracing How-To) for Simulation / 3D plotting, or are the lessons enough?
