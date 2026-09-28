Sample data for the general-purpose 3D plotting feature (docs/plot3d_blueprint.md, branch feature/3d-data-plotting).
All files are plain CSV (comma-delimited, a header row) - exactly the input Plot3D/Core/Plot3DData.h's parsePlot3DCsv()
and buildPlot3DDataset() take. Each is generated (see make_plot3d_samples.py in this folder), not measured data - they
exist to exercise each of the 6 primitives with a shape that is easy to recognise if the rendering is wrong.

surface_ripple.csv       Surface. A 21 x 21 regular grid (441 rows) of x, y, z = sin(r)/r (r = distance from the
                          origin) - a ripple radiating out from the centre, peaking at 1.0 at the origin. Good for
                          checking the colour ramp, the mesh triangulation of a grid, and (once contours exist) iso-
                          lines of a smooth, radially symmetric field.
line_helix.csv            Line / curve. 200 points of a helix (matplotlib's own "Parametric curve" example): x = cos t,
                          y = sin t, z rising linearly with t over 2 full turns. Tests an ORDERED point sequence (unlike
                          the other files, row order matters here) and colouring by z along the curve.
scatter_clusters.csv      Scatter. 180 points in 3 Gaussian-ish clusters (60 each) at different positions, "value"
                          holds the cluster id (0/1/2) for colouring - checks that scatter colouring is per-point, not
                          interpolated the way a Surface's is.
bar_histogram.csv         Bar / histogram. An 8 x 8 grid of bars (64 rows: x, y, height, base, width, depth), heights
                          forming a smooth bump in the middle (base always 0) - checks per-bar box geometry and that a
                          zero/near-zero bar at the corners still renders (a degenerate height should not crash).
voxel_sphere.csv          Voxel / volumetric. A sphere of occupied voxels carved out of a 9 x 9 x 9 grid (i, j, k,
                          occupancy), occupancy fading from 1.0 at the centre to 0.15 near the shell - checks the
                          occupancy-grid path through the volume-rendering reuse (docs/simulation_volume_rendering_
                          blueprint.md) with a shape whose outline (a sphere) is immediately obvious if wrong.
quiver_vortex.csv         Quiver (vector field). A 7 x 7 x 3 grid (147 rows: x, y, z, u, v, w, value) of a rigid-body
                          rotation about the Z axis (u = -y, v = x) plus a small constant upward w - value is the local
                          speed. Arrows should visibly curl around the Z axis and lengthen with distance from it.

Column mapping notes (Plot3DColumnMapping): every file's columns are laid out in a sensible left-to-right order for
its primitive, but the mapping is index-based and configurable - a future import panel is expected to let a user
re-map columns by header name, so these files' exact column order is a convenience, not a hard requirement. Bar's
"height"/"base"/"width"/"depth" and Voxel's "i"/"j"/"k"/"occupancy" headers match the names already used in
tests/plot3d_tests.cpp's own inline examples.
