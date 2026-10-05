Sample data for the general-purpose 3D plotting feature (docs/plot3d_blueprint.md, branch feature/3d-data-plotting).
All files are plain CSV (comma-delimited, with a header row) - exactly the input Plot3D/Core/Plot3DData.h's
parsePlot3DCsv() and buildPlot3DDataset() take. They are generated rather than measured data and exercise each of
the 6 primitives with a shape that is easy to recognise if the rendering is wrong.

surface_ripple.csv       Surface. A 21 x 21 regular grid (441 rows) of x, y, z = sin(r)/r (r = distance from the
                          origin) - a ripple radiating out from the centre, peaking at 1.0 at the origin. Good for
                          checking the colour ramp, the mesh triangulation of a grid, and (once contours exist) iso-
                          lines of a smooth, radially symmetric field.
surface_scattered.csv    Surface. Fourteen irregular X/Y samples with a gently sloped height field. It deliberately
                          does not form a complete grid, so it checks the Delaunay triangulation path for
                          unstructured Surface input.
line_helix.csv            Line / curve. 200 points of a helix (matplotlib's own "Parametric curve" example): x = cos t,
                          y = sin t, z rising linearly with t over 2 full turns. Tests an ORDERED point sequence (unlike
                          the other files, row order matters here) and colouring by z along the curve.
scatter_clusters.csv      Scatter. 180 points in 3 Gaussian-ish clusters (60 each) at different positions, "value"
                          holds the cluster id (0/1/2) for colouring - checks that scatter colouring is per-point, not
                          interpolated the way a Surface's is.
bar_histogram.csv         Bar / histogram. An 8 x 8 grid of bars (64 rows: x, y, height, base, width, depth), heights
                          forming a smooth bump in the middle (base always 0) - checks per-bar box geometry, smooth
                          colour variation, and the visual separation created by the 0.8 bar width/depth.
bar_histogram_1d.csv      One-dimensional histogram. Eight bin/count pairs. Set X to "bin", Y to "(none)", and Z to
                          "count"; leave Base, Width, Depth and Colour value as "(none)". The result should be one row
                          of eight equally sized bars centred on Y=0, with height and colour both driven by count.
bar_signed.csv            Signed/nonzero-base bars. Map X, Y and Z to x, y and height; map Base, Width, Depth and
                          Colour value to the matching columns. Positive bars extend upward from base and negative
                          bars extend downward. Width/depth changes should be visible on the rows using 0.6/0.9.
voxel_sphere.csv          Voxel / volumetric. A sphere of occupied voxels carved out of a 9 x 9 x 9 grid (i, j, k,
                          occupancy), occupancy fading from 1.0 at the centre to 0.15 near the shell - checks the
                          occupancy-grid path through the volume-rendering reuse (docs/simulation_volume_rendering_
                          blueprint.md) with a shape whose outline (a sphere) is immediately obvious if wrong.
quiver_vortex.csv         Quiver (vector field). A 7 x 7 x 3 grid (147 rows: x, y, z, u, v, w, value) of a rigid-body
                          rotation about the Z axis (u = -y, v = x) plus a small constant upward w - value is the local
                          speed. Arrows should visibly curl around the Z axis and lengthen with distance from it.

Renderer preview checks
-----------------------
Open quiver_vortex.csv, select Quiver, map X/Y/Z and U/V/W to their matching columns, and click Preview. The
temporary arrows should match the size and colour behaviour of Build Plot. Open voxel_sphere.csv, select Voxel,
map X/Y/Z to i/j/k and Colour value to occupancy, and click Preview. The temporary volume should match Build Plot.
For both files, Clear Preview and closing the dialog must remove the plot and restore the previous axes box.
After building quiver_vortex.csv, change Arrow size, Colour map, Colour bands and the manual colour range in the
3D Plot panel. Arrow length should change without camera-dependent growth, while arrow colours and the
"Vector magnitude" legend remain synchronized. Line plots expose Line width; Scatter and Stem expose Marker size.

Generated examples
------------------
Formula surface and Parametric surface do not require a CSV file. In Add 3D Plot, choose either source type from the
top selector. Formula surface offers Plane, Saddle, Paraboloid, Cone, Gaussian, Sinc Ripple, Wave, Mexican Hat,
Bivariate Normal, Logistic Regression and Rosenbrock presets.
Parametric surface offers Torus, Ellipsoid, Mobius Strip, Klein Bottle, Superellipsoid, Helicoid, Catenoid, Enneper
Surface and Spherical Harmonic. Use Refresh Preview after changing an expression or parameter, then Build Plot. The
generated plot receives the same scene-tree visibility control and persistent 3D Plot tab controls as CSV plots.
Parametric curve offers Helix, Lissajous, Trefoil Knot, Viviani Curve and Damped Spiral. Its single parameter is
named t; use Preview to inspect the generated constant-screen-width line before building it.

Formula pathlines (time-dependent)
----------------------------------
Choose "Formula pathlines (time-dependent)" in Data source. u, v and w are expressions in x, y, z and t, where t is the TIME
(unlike Formula streamlines, whose field is steady, and unlike Parametric curve, where t is the curve parameter). Particles are
released at the start time from "Y range / samples" seeds spread along Y at the middle of the X range (z = 0) and carried through
the changing field with a 4th-order Runge-Kutta integration over the "T range / steps" time interval. A trail ends where its
particle leaves the X / Y range. The trails are native lines, coloured by TIME (the legend runs from the start to the end time),
and take the usual colour map, bands, range and line-width controls, save into .mvf and bake their colours into glTF / OBJ export
like any other line plot. Presets (no file is needed; Preview and Build Plot as for the other formula sources):

  Pulsating Vortex     A rigid rotation whose rate pulses in time. Expect concentric rings that run a little over one turn;
                       the colour (time) bands along each ring bunch up where the particle slows and stretch where it speeds
                       up. The outermost ring touches the X / Y range edge, so it may end early there.
  Double Gyre          Shadden's classic unsteady flow on x 0..2, y 0..1: two counter-rotating cells whose dividing line
                       oscillates. Trails wind around the two cells and are carried back and forth across the middle by the
                       oscillation, so neighbouring trails fold and stretch (the point of pathlines in an unsteady field).
  Travelling Wave      A steady stream (u0) deflected sideways by a wave travelling in x. Trails drift forward and wiggle in Y
                       with a phase that depends on when each particle passed a crest.
  Oscillating Updraft  A vortex with a vertical velocity that oscillates in time. The rings climb and sink, so the trails draw
                       helices in Z (the Z axis range grows to cover them).

Checks: pick a preset and look at the status line under Refresh Preview ("Pathlines from N seeds over t = ... (M segments),
coloured by time"); change a parameter (for example Pulsating Vortex "a" to 0 for a steady rotation, where the colour bands
become evenly spaced) and Refresh Preview again; set the time range so the end is before the start and the Build button must
disable with a message; enter an unknown name in u(x,y,z,t) and the status must name it. After building, the colour legend
should run from the start to the end time, and the 3D Plot tab should offer the line-width and colour controls.

Surface extras
--------------
Contour lines: any Surface plot (CSV surface such as surface_ripple.csv, Formula surface, Parametric surface, Implicit
surface) can carry iso-lines of Z. Choose "Contour lines" in the creation dialog or later in the 3D Plot tab: "On the surface"
draws dark lines on the surface (visible from both faces), "On the base plane" draws colour-mapped lines flattened onto the
floor of the axes box, and the "Contour levels" box sets how many. The separate "Contour" plot type (CSV or Formula surface)
shows only the lines and can also be flattened with "Project contours onto the base plane".
Section curves on hover: for a Surface plot, tick "Show section curves on hover" in the 3D Plot tab. Moving the cursor over the
surface draws, through the hovered point, the curve where each of the X, Y and Z planes cuts the surface (red, green, blue like
the axes; the blue one is the contour line through the point) with the point's coordinates. Only the connected curve through
the point is drawn, so a saddle shows one branch, not both. surface_ripple.csv is a good test (circular Z curve, straight-ish
X / Y sections); it works in the single view only.

CSV time series (pathlines)
---------------------------
pathlines_double_gyre.csv         A time-dependent planar vector field sampled on a complete regular grid (945 rows: t, x, y, u, v,
                                   w) - Shadden's double gyre on x 0..2 (9 samples), y 0..1 (5 samples), t 0..20 s (21 samples,
                                   one per second; the cells' dividing line oscillates with a period of 10 s). Row order does not
                                   matter. No z column: a planar field (w is 0).
pathlines_rising_vortex_3d.csv    A 3-D field (4,212 rows: time, x, y, z, u, v, w) on a 13 x 4 x 9 x 9 grid: a rotation about the
                                   Z axis whose rate pulses in time, with a vertical velocity that oscillates, so the pathlines
                                   climb as they circle. Has a z column; its column names differ from the first file's
                                   (time, x, y, z, u, v, w), which the importer also recognises.

Open Add 3D Plot, choose Data source "CSV time series (pathlines)", then Open CSV... (or Paste). The Time, X, Y, Z (optional),
U, V and W column choices are guessed from the header names (t / time, x / px, u / ux / vx, ...) and can be changed; the status line
then reports either "N rows form a complete grid. Pathlines from S seeds (M segments), coloured by time." or why the table cannot
be used. Seeds are released along Y at the middle of the X range (in the middle z plane for a 3-D table) and traced with RK4 over
"time steps" equal steps from the first to the last time in the table; the field is interpolated linearly in space and time. A
trail ends where its particle leaves the grid. Checks: Preview and Build the double gyre and compare with the Double Gyre formula
preset (the trails should look alike); build pathlines_rising_vortex_3d.csv and look for climbing spirals; delete one line of a
copy of the file and the status must say the table is not a complete regular grid (it names how many rows are needed); change
a number to text and it must name the row; clear the Z column choice for the 3-D file and the grid must be rejected as incomplete
(its z values then repeat nodes). Then Edit Plot: the table, the column choices and the seed / step counts must come back, and
Rebuild Plot must update the existing plot in place.

Editing generated plots
-----------------------
Plots from the formula sources (Formula surface, Parametric surface and curve, Formula vector field, Implicit surface, Formula
streamlines and Formula pathlines) remember the definition they were built from, so Edit Plot works on them like on a CSV plot.
Build any of them (for example the Double Gyre pathlines or the Torus), select it in Active plot and click Edit Plot: the dialog
reopens on the same source with the expressions, ranges, sample counts, time range and parameter values restored. Change one (for
example the Double Gyre's "eps", or the Torus's "r") and click Rebuild Plot. The existing tree entry must update in place - no new
entry - and its title, visibility, axes settings, colour map / range / bands, reference planes, contour lines and line width must
stay as they were (with "Automatic colour range" on, the colour range follows the new data). Save as .mvf and reopen: Edit Plot
must still be available. The source (and, for a Formula surface, the Surface / Contour type) cannot be changed in an edit; build a
new plot for that.

Column mapping notes (Plot3DColumnMapping): every file's columns are laid out in a sensible left-to-right order for
its primitive, but the mapping is index-based and configurable in the import panel. These files' exact column order
is therefore a convenience, not a hard requirement. If Bar's optional Base, Width and Depth mappings are "(none)",
the importer uses 0, 0.8 and 0.8 respectively. If Colour value is "(none)", height supplies the colour value. Bar's
Y mapping may also be "(none)" for a one-dimensional histogram; the importer then places every bar at Y=0.

scatter_error_bars.csv: Select Scatter, enable Show error bars (±Z), and choose the error column.

Filled scatter-to-plane: open scatter_clusters.csv, select Scatter, map Colour value to "value", enable
Fill to Base Z, and choose a base such as -3. Preview and Build should show translucent blue/green/red ribbons;
the axes box must include the selected base.

Edit/rebuild check
------------------
Build any CSV-backed plot, select it in Active plot, and click Edit Plot. The dialog should restore the original
CSV text, delimiter, header option, primitive and every column mapping. Change one or more numeric rows or mappings
and click Rebuild Plot. The existing tree row must update in place without creating another entry. Its title,
visibility, axes settings, colour map/range/bands, reference planes and line/marker/arrow or bar sizing must remain
unchanged. Repeat with quiver_vortex.csv and voxel_sphere.csv to check the reused glyph and volume renderers.
