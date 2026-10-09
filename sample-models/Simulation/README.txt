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

Charts driven by the playback bar
---------------------------------
A "plot over time" chart (Simulation tab > Plot over time, then click a point on the result) now shows an orange CURSOR at the
result's current step. Clicking or dragging in the chart moves the result to the nearest step, so the chart and the playback bar
drive each other. Right-click the chart > "Add curve from CSV..." draws another curve on the same axes (test data, a response
curve) with a legend; "Remove added curves" clears them. The CSV needs two columns, x then y; a header row names the curve.

Quicker probing: right-click a point on a result and choose "Plot Over Time Here" - the chart opens in one step, with no need to arm the
Plot Over Time button first (not available in Compare mode). To compare several points, right-click the chart > "Add a point from the model",
then click another point on the same result: its history joins the chart as another curve, named by its position, with a legend.

thermal_probe_test.csv        Synthetic "thermocouple" readings (time, temperature_measured) for the middle of the box's far face
                              (the face at x = 10, opposite the face held at 100) in FEM_box_thermal_transient.frd, time 0.5 .. 10 s:
                              the simulated history of that point plus a little noise and a small offset. Try: open the .frd,
                              Plot over time, click near the middle of the far face (rotate to see it), right-click the chart >
                              Add curve from CSV, pick this file, then Play: the cursor travels along both curves; click the chart
                              to jump in time. Your clicked point is not exactly the sample's, so the curves are close, not identical.
FEM_box_modes_frf.csv         A synthetic frequency-response curve (frequency_Hz, amplitude) with peaks at the six mode frequencies of
                              FEM_box_modes.frd (54280, 54317, 73971, 128658, 143336, 143478 Hz; the close pairs merge into one
                              peak). Try: open FEM_box_modes.frd, Plot over time on any point (its x axis is the frequency of
                              each mode), add this curve, and click on a peak: the result jumps to that mode.


Shell (surface-only) results and the charts
-------------------------------------------
A result with only triangles / quads (no volume cells) is charted on the closest point of its surface: Plot Over Time reads the clicked
point of the shell; Plot Over Line projects each sample of the chord onto the surface (a gap in the curve where the chord leaves it - only possible on a curved shell,
since a click always lands on the surface; the unit test covers it).

shell_plate_transient.frd     A 2 x 1 flat plate of 20 x 10 four-node shell quads (231 nodes, no volume cells), 10 time steps over 5 s, field
                              NDTEMP: heat flowing in from the left edge (synthetic, made with a script, not a solver run). Try: open it,
                              Plot Over Time on a point near the middle (the history rises as the heat arrives), Plot Over Line from the
                              left edge to the right edge at step 10, then play the timeline with the chart open (the cursor follows).
plate.vtk                     Also surface-only (312 quads, several vector fields, one step): Plot Over Line works on it.


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

VTKHDF (.vtkhdf)
    Needs a build with the HDF5 library (vcpkg port hdf5, a dependency of the NetCDF and CGNS ports); other builds do not
    list the extension. block.vtkhdf ships here; the test program can rewrite it:
        result_tests.exe --write-vtkhdf-sample block.vtkhdf
    An 8 x 8 x 8 block of hexahedra (UnstructuredGrid, static geometry), five steps written the way ParaView writes a
    transient dataset (every step of an array in one dataset, located by /VTKHDF/Steps/PointDataOffsets): point data
    Temperature and Velocity (3 components), cell data Quality. Values are synthetic. UnstructuredGrid (partitions, time
    steps, moving meshes), PolyData and ImageData are read; composite files (MultiBlockDataSet ...), StructuredGrid,
    RectilinearGrid and HyperTreeGrid are not supported yet. A file written by ParaView is the true check.

MED (.med)
    Needs a build with the HDF5 library; other builds do not list the extension. MED 3 and later (Salome 6+, Code_Aster, Code_Saturne)
    is read directly on HDF5. Both files below come from the SALOME 9.16 sample set (SALOME SAMPLES/MedFiles, LGPL-2.1):

    pointe_4fields.med
        A 19-node mesh of 12 tetrahedra, 2 hexahedra and 2 pyramids ("pointe"), written with MEDCoupling: node fields (a scalar
        over three time steps, an integer), and a scalar and a vector on the cells - a file that mixes cell types and steps.

    fra.med
        A 1020-node mesh with a nodal velocity vector (VITESSE) and a void fraction (TAUX_DE_VIDE), a real solver-style result.

    Code_Aster 2.x-layout files (older than MED 3) are not read; SALOME medimport converts them.

polyhedra.vtu
    Written by hand for this project (no external data or licence): three polyhedral cells (VTK cell type 42) - two unit cubes
    that share a face, and an L-shaped prism whose caps are concave 6-gons. Point data Temperature, cell data Cell. Checks the
    polyhedron boundary: the shared face is not drawn, and the L caps are filled as an L, not as its convex hull.

polyhedra_legacy.vtk
    Written with VTK 9.3's own legacy writer (the vtkmodules bundled with FreeCAD 1.1; no external data or licence): a legacy 5.1 file
    with a polyhedron (cell type 42, whose CELLS entry is the face stream) next to a regular hexahedron, point arrays "temperature" and
    "velocity" (a flow along +x, so streamlines run from the polyhedron into the hexahedron). Checks the legacy polyhedron reading.

polyhedra.exo
    Written by this project's test tool (result_tests --write-exodus-polyhedra-sample; no external data or licence) in the layout of the Exodus II
    specification: an NFACED element block (a cube given by its six faces: ebepecnt / facconn) and a HEX8 block that share a face, two face blocks
    (five NSIDED faces with fbepecnt, one fixed-size QUAD face), node variables temperature and vel_x / vel_y / vel_z. NOT independently verified:
    VTK 9.3's Exodus reader (FreeCAD 1.1) parses the file - both face blocks, the HEX8 block and the node arrays - but rejects the NFACED block's
    element type, so the layout of that block rests on the specification and the variable names found in the exodus library.

real/  (files written by other tools, to test the readers against data we did not make ourselves)
    Downloaded 2026-09-27 from the VTK ExternalData store (www.vtk.org/files/ExternalData/SHA512/<hash>, the hashes taken from Testing/Data/*.sha512
    of github.com/Kitware/VTK) and verified against their SHA-512 hashes. VTK test data, BSD 3-clause licence (see VTK-Data-License.txt for the notice).
      Exodus II  test-nfaced.exo (a real NFACED polyhedron), different_topologies.ex2 (tetrahedra + hexahedra, 2 node fields),
                 block_with_attributes.g (quads), Flow1D.e (a 1-D beam network with 51 steps and cell fields; nothing drawable)
      CGNS       Example_mixed.cgns (hexahedra), Example_nface_n.cgns (the same mesh as NGON_n faces + NFACE_n polyhedra),
                 Example_ngon_pe.cgns (the same mesh: NGON_n faces with ParentElements only), BoxWithFaceData.cgns (FaceCenter data:
                 the CGNS 4.5.1 library cannot open it - "Location not yet supported" - so it is kept as a known limitation)
      VTKHDF     polyhedron.vtkhdf, hexahedron.vtkhdf, can-vtu.vtkhdf (a real transient crash result, 4800 hexahedra, vector fields)
    result_tests reads all of them and checks node / cell counts and boundary triangles.

ibeam_cantilever.frd, ibeam_torsion.frd, plate_with_hole.frd
    Structural analyses solved with CalculiX (ccx 2.x shipped with FreeCAD 1.1) on meshes made with gmsh, by make_structural_samples.py in this folder (no external data or
    licence; mm, N, MPa, steel E = 210000, nu = 0.3, quadratic tetrahedra C3D10). Each has four load steps (25, 50, 75, 100 % of the load), displacements DISP and stresses STRESS
    (the viewer adds von Mises, the principal stresses and the maximum shear): switch on "Show deformed shape" and play the steps.
      ibeam_cantilever   an I-beam (100 x 60 x 1000 mm, flanges 8, web 5) fixed at one end, 4000 N downwards at the free end: bending. The tip deflection at full load is 2.89 mm
                         (beam theory P L^3 / 3 E I: 2.78 mm; the rest is shear and the fixed end).
      ibeam_torsion      the same beam with a torque of 300 000 N mm at the free end (a couple of side forces on the flange tips): twist and warping of the flanges.
      plate_with_hole    a plate (100 x 50 x 5 mm) with a hole of radius 10 mm, pulled with 100 MPa at one end: the stress concentration at the hole.

Regenerating the samples (developers)
    make_structural_samples.py  (ibeam_cantilever / ibeam_torsion / plate_with_hole .frd)
        Prerequisites
          - Python 3.8 or newer (standard library only, no packages to install).
          - gmsh and CalculiX (ccx) command-line executables. FreeCAD 1.1 ships both in its bin folder, so installing FreeCAD is the easiest way.
            Otherwise install gmsh (https://gmsh.info) and CalculiX (http://www.calculix.de) yourself.
          - The script looks for them in BIN near the top of the file, which is set to C:\Program Files\FreeCAD 1.1\bin. Edit that line if FreeCAD is elsewhere
            or you use standalone tools (the folder must contain gmsh(.exe) and ccx(.exe), and on Windows also the DLLs that ccx needs, which FreeCAD's bin folder has).
        Method
          python make_structural_samples.py <output directory> [mesh scale]
          The mesh scale multiplies the element size: 3.5 gives the small files in this folder (about 8 700 nodes, 6 MB), 1.0 a fine mesh (about 90 000 nodes,
          65 MB, useful to try the lazy step loading), smaller values are finer still. A run takes a few seconds at 3.5 and about a minute at 1.0.
        What it does
          1. gmsh meshes each geometry (OpenCASCADE boxes, unioned for the I-beam; second-order tetrahedra) and writes an Abaqus .inp mesh.
          2. The script reads the mesh and writes one CalculiX input deck per case (material, fixed end, four *STEP blocks with 25/50/75/100 % of the load,
             *NODE FILE U and *EL FILE S).
          3. ccx solves each deck; the <name>.frd results are copied to the output directory (the decks and meshes stay in <output directory>/_work).
          The script also prints the beam-theory tip deflection to compare with the result. To change a case (span, section, load, hole size) edit the
          constants in main() and the geometry strings in the script; result_tests checks the shipped files, so keep the case names if you replace them.

    make_med_sample.py  (a small .med file)
        Prerequisites: the Python of SALOME, or a standalone MEDCoupling build in which "from MEDLoader import *" works (the pip package "medcoupling"
        does NOT work, it is built without MED file I/O).
        Method: python make_med_sample.py block.med
        The file is a synthetic fixture (an n x n x n block of HEXA8 cells, five time steps) for the MED reader; a real Code_Aster or SALOME result is the true test.

