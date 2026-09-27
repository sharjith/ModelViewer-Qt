# Simulation Results - Test Data Sources

Companion to `simulation_results_design.md` (section 12). Test data is **fetched by the user**, not by
tooling. Keep downloaded data **outside the repo** (or in a git-ignored folder) unless its licence
clearly allows redistribution; only tiny hand-made fixtures go into the repository.

## A. Generate locally (recommended - no download, known ground truth)

FreeCAD 1.1 on this machine ships `ccx.exe` (CalculiX) and `gmsh.exe`. With FreeCAD FEM:

- **Structural static:** a cantilever/bracket with a fixed face and a load. Produces a `.frd`
  (CalculiX) and can export `.vtu`. Gives displacement, stress tensor, von Mises.
- **Thermal:** a steady-state or transient thermal analysis on the same part (temperature field,
  several steps for the transient).
- **Quadratic mesh:** create the FEM mesh with second-order elements (tet10) to exercise curved-edge
  tessellation.
- Because we know the load case, a hand check is possible (for example cantilever tip deflection
  against beam theory) - use that to validate values and units, not just appearance.

## B. Downloadable sets (verified reachable during research, 2026-09-24)

| Source | What it gives | Licence noted | Use for |
|---|---|---|---|
| [CalculiX-Examples](https://github.com/calculix/CalculiX-Examples) | Parametric example cases: Linear, NonLinear, Contact, Dynamics, Thermal, Elements convergence, CAD integration | MIT | Real solver decks; run them with `ccx.exe` to produce `.frd` (result files were not confirmed to be checked in - generate them) |
| [FSU VTK sample files](https://people.sc.fsu.edu/~jburkardt/data/vtk/vtk.html) | Small ASCII legacy `.vtk`: `ugridex.vtk` (3D unstructured grid), `triangle_mesh_linear.vtk`, `mesh_smag_0040.vtk` (channel flow velocity + pressure), `rbc_001.vtk` (one frame of an 80-frame series) | LGPL | Legacy reader, vector fields, multi-file time series |
| [FSU VTU sample files](https://people.math.sc.edu/Burkardt/data/vtu/vtu.html) | Small XML `.vtu` examples | see page | XML reader, appended/compressed data |
| [VTK examples: VTK file formats](https://kitware.github.io/vtk-examples/site/VTKFileFormats/) | Format documentation with small inline examples and a `tetra.vtu` reference | see page | Cell-type ids, exact layouts |

## C. Reference material (documentation, not data)

- [ccx2paraview](https://github.com/calculix/ccx2paraview) - GPLv3 FRD-to-VTK converter; useful to
  cross-check our FRD reader's output against (run it on the same `.frd`, compare fields). Do not
  copy its code.
- [CGNS examples](https://cgns.org/current/examples.html) and Stanford's
  [CGNS files](http://aero-comlab.stanford.edu/vdweide/CGNSFiles/) - for the CFD phase.
- OpenFOAM's bundled tutorials (`simpleFoam/pitzDaily`, `dieselFoam/aachenBomb`) with `foamToVTK` -
  for the CFD phase; requires an OpenFOAM installation.
- Exodus II: the specification is documented (Sandia); no ready-made download was found in research -
  MOOSE/Trilinos SEACAS test files are the likely source when Phase 3 starts.

## D. Not verified

- `vtk-data` (Kitware's GitLab test repository) returned HTTP 403 to automated fetch. It is the
  canonical VTK test-data repository and worth trying manually in a browser.
- Whether CalculiX-Examples includes prebuilt `.frd` results was not confirmed.

## E. Suggested order as phases progress

1. Phase 0: hand-made single-tet `.vtu` fixtures + one FreeCAD-generated `.vtu`.
2. Phase 1: FreeCAD static (`.frd` + `.vtu`) and thermal transient; the FSU `rbc_001` series for
   multi-file steps.
3. Phase 2: a tet10 FreeCAD mesh; cell-data files.
4. Phase 3: OpenFOAM tutorial via `foamToVTK`; CGNS/Exodus samples.
