Simulation result samples
=========================

Open any of these (.vtu, .vtk, .frd) with File > Open (each opens in its own document), or add one to the current document with
Simulation > Add Result to This Document. They are shown as the model's outer surface, coloured by a result field;
use the Simulation tab (bottom-left dock) to change the field, range, colormap and contours.

No units are stored in these files; values are shown without a unit.

File                          What it is
----------------------------  ---------------------------------------------------------------------------------
FEM_box_static_stress.vtu     Static structural analysis of a 10 mm box (CalculiX): 280 nodes, 129 quadratic
                              tetrahedra. Node fields: Displacement (vector), Displacement Magnitude, von Mises,
                              Tresca and the three principal stresses. Opens coloured by von Mises stress.
FEM_box_frequency_mode1.vtu   The same box, first vibration mode of a frequency analysis (CalculiX).
hexa.vtk                      A cube of 10,648 hexahedra with a scalar field (a distance-like function): a smooth
                              test of the colormap.
cell_data_cube.vtk            An 8 x 8 x 8 block of hexahedra with CELL data only (element-wise results): a smooth
                              scalar 'Element_Stress' and an integer 'Element_Group' (four regions). Each cell is
                              drawn in one flat colour - fields of this kind are marked [cells] in the Field list.
openfoam_cavity/cavity.foam  An OpenFOAM case (open the empty cavity.foam file): a 20 x 20 x 1 hexahedral mesh of a
                              lid-driven-cavity-like flow, written in OpenFOAM's ASCII layout (constant/polyMesh and
                              five time directories 0 .. 2 with U, p, T and a symmetric tensor sigma). The FIELD
                              VALUES ARE SYNTHETIC (a spin-up vortex and a warming gradient made with a script; no
                              OpenFOAM was run), meant to exercise the reader, the cell-data display, the timeline and
                              the units taken from the files' dimensions (T in K, U in m/s, sigma in Pa; the kinematic
                              pressure p has no unit). All fields are cell data, shown flat; only the boundary is drawn.
plate.vtk                     A vibrating plate: 312 quadrilaterals with several vector fields (vibration modes).
                              Pick a mode in the Field list, then a component or the magnitude.
post.vtk                      Binary file: 8,750 tetrahedra with a Pressure field.
tetraMesh.vtk                 A small tetrahedral mesh (160 cells) with an integer scalar.
uGridEx.vtk                   A tiny unstructured grid mixing every cell type; some (polygon, strip, vertex) are
                              not displayed, which is expected.
FEM_box_static.frd            The same static box analysis as a CalculiX result file (.frd), exactly as the solver
                              wrote it (values in the solver's units: here mm and MPa, whereas the .vtu export above
                              shows stress in Pa). Node fields DISP, STRESS, TOSTRAIN; ModelViewer derives von Mises,
                              principal stresses and max shear while loading. FreeCAD's own test suite expects these
                              ranges for this file: von Mises 385.38 .. 2203.51, max principal -924.05 .. 1169.55.
FEM_box_frequency.frd         First vibration mode of the box (CalculiX). Its "time" is the frequency (0.0194 Hz).
FEM_box_modes.frd            The box's first six vibration modes (CalculiX, generated for these samples). Six steps
                              labelled Mode 1..6 with their frequencies in Hz: use the timeline (top centre of the
                              viewport) to step through or play the modes.
FEM_box_load_steps.frd        The box under a load ramped over four increments (25%, 50%, 87.5%, 100%; geometrically
                              nonlinear step). Play the timeline to watch the stress grow on a fixed colour scale.
FEM_box_thermal_transient.frd A transient heat-transfer analysis of the box (CalculiX): one face held at 100, the rest
                              starting at 20, 20 time steps over 10 s. Field NDTEMP (temperature). The file states no
                              temperature unit, so none is assumed: choose it in the Simulation tab (Quantity /
                              Values are in) to see it labelled. Play the timeline to watch the heat flow in.
beampl.frd                    CalculiX's "beampl" example: a cantilever beam under tension with deformation
                              plasticity, 32 twenty-node hexahedra, results in the solver's units.

Sources and licences
--------------------
hexa.vtk, plate.vtk, post.vtk, tetraMesh.vtk, uGridEx.vtk
    From the VTK data set (VTKData 5.10.1), Kitware / Ken Martin, Will Schroeder, Bill Lorensen. Distributed under
    the BSD-style licence reproduced in VTK-Data-License.txt (the notice must accompany redistributed copies).

FEM_box_static_stress.vtu, FEM_box_frequency_mode1.vtu
    Exported with FreeCAD 1.1 (FEM workbench, VTK post-processing writer) from the box example results shipped in
    FreeCAD's FEM test data (box_static / box_frequency, solved with CalculiX). FreeCAD is licensed under
    LGPL-2.1-or-later. The model is a plain 10 mm cube.

FEM_box_static.frd, FEM_box_frequency.frd
    The CalculiX result files of the same FreeCAD FEM test data (Mod/Fem/femtest/data/calculix), unmodified.

beampl.frd
    Result file of the CalculiX example "beampl" (test objective: deformation plasticity), computed by CalculiX,
    which is licensed under GPL-2.0-or-later; the example model ships with CalculiX.

FEM_box_modes.frd, FEM_box_load_steps.frd, FEM_box_thermal_transient.frd
    Computed with CalculiX for this project on the same 10 mm box mesh (modal and ramped-load analyses); CalculiX is
    licensed under GPL-2.0-or-later.

cell_data_cube.vtk, openfoam_cavity
    Generated with a script for this project (no external data or licence).

Exodus II (.e / .exo / .ex2 / .g)
    Needs a build with NetCDF (vcpkg feature netcdf-c[netcdf-4]); other builds do not list these extensions.
    block.exo ships here; the test program can rewrite it:
        result_tests.exe --write-exodus-sample block.exo
    It is an 8 x 8 x 8 block of HEX8 elements with five time steps of a bending-and-warming cube: disp_x/y/z
    (gathered into one vector field "disp"), temperature, a symmetric stress tensor (stress_xx ... stress_zx, gathered
    into "stress" with von Mises and principal stresses derived) and the element variable element_quality (a cell field).
    The values are synthetic; real solver output (MOOSE, Cubit, Sierra ...) is the true check.

CGNS (.cgns)
    Needs a build with the CGNS library (vcpkg port cgns); other builds do not list the extension. Two files ship here, both
    written with the test program (which can rewrite them):

    block.cgns    (result_tests.exe --write-cgns-sample block.cgns)
        An 8 x 8 x 8 block of HEXA_8 cells in one UNSTRUCTURED zone, five steps (BaseIterativeData/TimeValues 0 .. 1):
        vertex solutions Temperature, Pressure and VelocityX/Y/Z (gathered into one vector field "Velocity") and a per-cell
        field Quality (CellCenter).

    duct.cgns     (result_tests.exe --write-cgns-structured-sample duct.cgns)
        A half-ring duct made of two curved STRUCTURED blocks (13 x 7 x 5 points each), four steps of a swirling flow with
        the same kind of fields. The blocks keep their own points, so their shared interface shows as a pair of coincident
        faces inside the duct.

    Values are synthetic. Unstructured and structured zones are read; polyhedra (NGON/NFACE) and boundary-condition
    sections are skipped.
