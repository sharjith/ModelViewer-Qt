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

Column mapping notes (Plot3DColumnMapping): every file's columns are laid out in a sensible left-to-right order for
its primitive, but the mapping is index-based and configurable in the import panel. These files' exact column order
is therefore a convenience, not a hard requirement. If Bar's optional Base, Width and Depth mappings are "(none)",
the importer uses 0, 0.8 and 0.8 respectively. If Colour value is "(none)", height supplies the colour value. Bar's
Y mapping may also be "(none)" for a one-dimensional histogram; the importer then places every bar at Y=0.

scatter_error_bars.csv: Select Scatter, enable Show error bars (±Z), and choose the error column.
