// The Quick Help tabs for the Simulation, 3D Plot, Analysis and Ray Tracing features (kept apart from QuickHelpDialog.cpp, which is long already).
// Texts are tr() strings like the other tabs; the four translations live in the .ts files.

#include "QuickHelpDialog.h"

#include <QTextBrowser>

void QuickHelpDialog::setupSimulationTab()
{
	QString content;

	content += createSection(tr("Opening a result"),
		tr("<p>Use <b>File &gt; Import</b> to open a result in its own document, or <b>Visualization &gt; Simulation &gt; Add Result to This Document...</b> to "
		   "add one to the current document. Formats: VTK (.vtk, .vtu), CalculiX .frd, Exodus II, CGNS, MED, OpenFOAM (open the .foam file) and VTKHDF. The result "
		   "is drawn as the model's outer surface, coloured by a field; the <b>Simulation</b> tab (bottom-left dock) controls how.</p>"));
	content += createSection(tr("The Simulation tab"),
		tr("<ul><li><b>Field</b> and <b>Component</b>: the field to colour by and one component or the magnitude; fields marked <b>[cells]</b> are element "
		   "data</li><li><b>Range</b>: Automatic (all steps), Automatic (this step) or Custom; <b>Colormap</b> and <b>Contours</b></li><li><b>Show deformed "
		   "shape</b> with a <b>Scale factor</b> (Auto picks one)</li><li><b>Mark minimum and maximum</b> labels the extremes on the visible "
		   "surface</li><li><b>Show vector arrows</b> and <b>Show stress ellipsoids</b> (choose the field, size and count)</li><li><b>Average cell data to "
		   "nodes</b> draws element data smooth instead of flat</li><li><b>Quantity</b> and <b>Values are in</b> set the unit when the file does not state "
		   "one</li></ul>"));
	content += createSection(tr("Time steps"),
		tr("<p>A result with several steps shows a timeline at the top of the viewport: play, step and stop. By default the colour range covers all steps, so the "
		   "frames stay comparable; choose <b>Automatic (this step)</b> or <b>Custom</b> under <b>Range</b> to change that. A chart's time cursor and the timeline "
		   "drive each other.</p>"));
	content += createSection(tr("Looking inside"),
		tr("<ul><li><b>Sections</b>: the Clipping Planes cut the result; tick <b>Colour the Clipping Plane cut with the field</b> to colour the "
		   "cut</li><li><b>Show iso-surfaces</b>: choose the <b>Iso-surface field</b> and the number of levels</li><li><b>Show streamlines</b>: choose the field "
		   "and the number of seeds, optionally <b>Seed on the Clipping Plane</b></li><li><b>Show as volume</b>: direct volume rendering of a field, with a "
		   "quality setting and an opacity curve</li></ul>"));
	content += createSection(tr("Probing and charts"),
		tr("<ul><li>Hover over the result to read the value at the cursor; right-click a point and choose <b>Plot Over Time Here</b> for a quick "
		   "history</li><li><b>Plot Over Line...</b> (pick two points) and <b>Plot Over Time...</b> (pick one point) open a chart; <b>Distribution...</b> shows a "
		   "histogram of the field</li><li>In a chart: the mouse wheel zooms (Shift: x only, Ctrl: y only), the middle button pans, a double-click resets; the "
		   "orange cursor marks the current step, click or drag to move it</li><li>Right-click a chart: <b>Add a point from the model</b>, <b>Add curve from "
		   "CSV...</b> (a header like <i>Name (unit)</i> gives the curve a unit; a different unit gets a second axis), <b>Save image...</b>, <b>Export data "
		   "(CSV)...</b></li></ul>"));
	content += createSection(tr("Comparing and saving"),
		tr("<p><b>Compare</b> (or <b>Visualization &gt; Simulation &gt; Compare Results...</b>) shows two results side by side or stacked; <b>Same colour range "
		   "for both</b> makes equal colours mean equal values and <b>Link the cameras</b> keeps both views in step. A result is stored inside a <b>.mvf</b> "
		   "session together with its view settings, sections, iso-surfaces and streamlines. Results are not drawn by the ray tracer; PBR and ray-tracing modes "
		   "add little for them.</p>"));

	_simulationBrowser->setHtml(createStyledHtml(tr("Simulation Results"), content));
}

void QuickHelpDialog::setupPlot3DTab()
{
	QString content;

	content += createSection(tr("Adding a plot"),
		tr("<p><b>Visualization &gt; Plot 3D...</b> (or <b>Add 3D Plot...</b> in the <b>3D Plot</b> tab) opens the dialog. Pick a <b>Data source</b>: a CSV file "
		   "or pasted data, a formula (surface, parametric surface or curve, vector field, implicit surface, streamlines, pathlines), a CSV time series "
		   "(pathlines) or an image on a plane. Plot types: Surface, Contour, Line / Curve, Scatter, Bar / Histogram, Voxel / Volumetric and vector (Quiver). "
		   "<b>Preview</b> shows the plot before <b>Build</b> adds it.</p>"));
	content += createSection(tr("Columns and options"),
		tr("<ul><li>Map the table's columns to <b>X</b>, <b>Y</b>, <b>Z</b> and an optional colour <b>value</b>; vectors use <b>U, V, W</b>; bars have "
		   "<b>Base</b>, <b>Width</b> and <b>Depth</b></li><li>Scatter: draw <b>stems to Base Z</b>, show <b>error bars</b>, or <b>fill to Base Z</b></li><li>Line "
		   "and scatter: <b>Fill to Base Z</b>, or fill <b>between the plot and a column</b> holding a second curve's Z values; a parametric curve can fill up to "
		   "a second z(t)</li><li>An image on a plane has an <b>Opacity</b> and <b>Readable from behind</b> (for an opaque picture)</li></ul>"));
	content += createSection(tr("The 3D Plot tab"),
		tr("<p>Choose the <b>Active plot</b>, then change its <b>Title</b>, <b>Colour map</b>, <b>Colour bands</b> and colour range (<b>Automatic colour range</b> "
		   "or a fixed one), <b>Line width</b>, <b>Marker size</b>, <b>Arrow size</b>, bar <b>width</b> and <b>depth</b>, <b>Contour levels</b> and <b>Contour "
		   "lines</b> (on the surface or on the base plane). <b>Show axes box</b> and the reference <b>Planes</b> (XY, XZ, YZ) frame the plot. <b>Edit Plot...</b> "
		   "reopens a plot's definition: change the data, columns or formulas and rebuild it in place.</p>"));
	content += createSection(tr("Shared axes"),
		tr("<p>All plots in the document share one set of axes. Under <b>Axes</b> set each axis' label, <b>Axis scale</b> (<b>Linear</b>, <b>Log 10</b> or "
		   "<b>SymLog</b>), range and tick count; every plot is re-laid out together, arrows are re-aimed and voxel grids resampled to follow the scale. A Log 10 "
		   "axis needs the data of every plot above zero; if a plot added later cannot be shown on a Log 10 axis, that axis returns to Linear.</p>"));
	content += createSection(tr("Notes and probing"),
		tr("<ul><li><b>Text notes</b>: click <b>Place note</b> and click a point on the plot (or <b>Add at centre</b>); drag a note to move it, double-click to "
		   "edit, right-click to delete; all of it can be undone</li><li><b>Show section curves on hover</b> (surfaces): the curves where the X, Y and Z planes "
		   "through the cursor cut the surface, with the point's data values</li><li><b>Animate pathlines (timeline)</b> plays pathline plots over time, together "
		   "with a simulation result</li></ul>"));
	content += createSection(tr("Saving and exporting"),
		tr("<p>Plots are stored in the <b>.mvf</b> session with their data, definition, axes and notes. Point and line plots export to glTF, GLB and OBJ with the "
		   "rest of the scene. Plots are not drawn by the ray tracer.</p>"));

	_plot3DBrowser->setHtml(createStyledHtml(tr("3D Data Plots"), content));
}

void QuickHelpDialog::setupAnalysisScenesTab()
{
	QString content;

	content += createSection(tr("Surface Analysis"),
		tr("<p><b>Tools &gt; Surface Analysis...</b> colours the selected meshes: <b>Draft Angle</b> against a <b>Pull direction</b>, <b>Zebra Stripe</b>, <b>Mean "
		   "Curvature</b>, <b>Wall-Thickness</b> (methods: <i>Inscribed sphere</i>, <i>Local thickness (rays)</i>, <i>Normal ray (fast)</i>; highlight walls "
		   "thinner than a limit) and <b>Deviation</b> from a reference mesh. <b>Show Readout on Hover</b> prints the value under the cursor; <b>Clear Overlay</b> "
		   "removes every analysis colour.</p>"));
	content += createSection(tr("Mass Properties"),
		tr("<p><b>Tools &gt; Mass Properties...</b> lists volume, mass and centre of gravity per mesh and per material. The density comes from the material's "
		   "<b>Physical Properties</b> tab. The table can be searched and sorted; the row menu has <b>Show Only</b>, and selecting a row selects the mesh in the "
		   "viewer. Open (shell) surfaces use a shell thickness.</p>"));
	content += createSection(tr("Selection tools"),
		tr("<p>The <b>Selection</b> menu has <b>Lasso</b>, <b>Filter by Material</b>, <b>Filter by Colour</b> and <b>Filter by Bounding Box</b> (a draggable box "
		   "gizmo), plus named selection sets. The <b>Material Eyedropper / Brush</b> picks a material from one mesh and paints it onto others.</p>"));
	content += createSection(tr("Scene States and batch rendering"),
		tr("<p><b>Selection &gt; Save Scene State...</b> stores a named state: camera, which meshes are visible, the selection and the presentation settings; the "
		   "<b>States</b> panel recalls it in one click. <b>Tools &gt; Batch Render Views...</b> renders the checked captured views to a folder (PNG, JPEG, BMP, "
		   "TIFF or OpenEXR, at the resolution you set) with the ray tracer.</p>"));
	content += createSection(tr("Ray Tracing"),
		tr("<ul><li><b>Visualization &gt; Ray Tracing...</b> opens the Ray Tracing dialog (Basic, Advanced and Diagnostics tabs); <b>View &gt; Rendering Mode</b> "
		   "switches the viewport to a live ray-traced view</li><li>Two engines: the <b>CPU</b> engine (Embree) runs everywhere; the <b>GPU</b> engine (NVIDIA "
		   "OptiX) needs an NVIDIA GPU and a build with OptiX - the About dialog says whether it is enabled</li><li>Progressive rendering with denoising (Intel "
		   "OIDN, on the CPU or the GPU); materials, environment lighting and the shadow-catcher floor follow the raster viewer</li><li><b>Export Ray-Traced "
		   "Image</b> saves PNG, JPEG, BMP, TIFF or OpenEXR at any resolution</li><li>Simulation results and 3D plots are not drawn by the ray tracer</li></ul>"));

	_analysisBrowser->setHtml(createStyledHtml(tr("Analysis, Scenes and Rendering"), content));
}
