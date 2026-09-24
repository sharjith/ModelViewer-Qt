Simulation result samples
=========================

Open any of these with File > Open (each opens in its own document), or add one to the current document with
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
plate.vtk                     A vibrating plate: 312 quadrilaterals with several vector fields (vibration modes).
                              Pick a mode in the Field list, then a component or the magnitude.
post.vtk                      Binary file: 8,750 tetrahedra with a Pressure field.
tetraMesh.vtk                 A small tetrahedral mesh (160 cells) with an integer scalar.
uGridEx.vtk                   A tiny unstructured grid mixing every cell type; some (polygon, strip, vertex) are
                              not displayed, which is expected.

Sources and licences
--------------------
hexa.vtk, plate.vtk, post.vtk, tetraMesh.vtk, uGridEx.vtk
    From the VTK data set (VTKData 5.10.1), Kitware / Ken Martin, Will Schroeder, Bill Lorensen. Distributed under
    the BSD-style licence reproduced in VTK-Data-License.txt (the notice must accompany redistributed copies).

FEM_box_static_stress.vtu, FEM_box_frequency_mode1.vtu
    Exported with FreeCAD 1.1 (FEM workbench, VTK post-processing writer) from the box example results shipped in
    FreeCAD's FEM test data (box_static / box_frequency, solved with CalculiX). FreeCAD is licensed under
    LGPL-2.1-or-later. The model is a plain 10 mm cube.
