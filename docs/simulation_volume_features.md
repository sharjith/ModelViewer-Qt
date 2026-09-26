# Simulation Results - Sections, Iso-surfaces and Streamlines

Status: **implemented** (2026-09-26), branch `feature/simulation-results`. Companion to `simulation_results_design.md` (phase 2/3 items) and
`simulation_mvf_persistence_design.md` (section 6 addendum: what a saved `.mvf` keeps of these).

All three features work on the **volume cells** of a result (tetrahedra, hexahedra, wedges, pyramids, their quadratic forms through the corner nodes,
and polyhedra through their explicit faces). A shell or surface result has nothing to cut or trace; the panel says so. They are overlays, not scene meshes:
no selection, scene tree, export or path tracing, and they are drawn on the undeformed mesh. They lie *inside* the model, so they are seen through a cut:
switch on a Clipping Plane (the section cap is left open while iso-surfaces or streamlines are shown, because an opaque cap would hide them).

## 1. The volume cutter (`ResultSlice`)

One engine serves plane sections and iso-surfaces: it cuts every volume cell where a per-node signed function changes sign - the distance to the plane, or
the field minus the level. Each face is walked, the runs of negative nodes are isolated by segments between the edges they leave through, the segments of a cell
chain into closed loops, and each loop is fan-triangulated. Vertices are welded per mesh edge (lower node first), so neighbouring cells produce the same vertex and
the cut is crack-free; the edge and its fraction are stored so the cut can be recoloured for another field or step without cutting again. A node exactly on the cut
counts as positive; the rule for an ambiguous face is the same for both cells that share it.

- **Coloured cut** (Simulation tab, "Colour the Clipping Plane cut with the field"): the cut of each enabled axis-aligned Clipping Plane, coloured by the shown
  field with the legend's colours, following the plane as it moves. A cell field gives flat colour per cell. With several planes the app removes only what *all* planes
  remove, so a plane's cut is kept only where every other plane has removed the material.
- **Iso-surfaces**: "Show iso-surfaces" with a node field (a scalar, or a vector's magnitude) and 1-20 levels, evenly spaced strictly inside the field's range at the
  shown step, one colour per level, headlight shaded. They are visible wherever some plane still keeps the material. A constant field has no surface; the info line says so.
- The opaque coloured cut hides iso-surfaces behind it: switch it off to see them.

## 2. Streamlines (`ResultStreamlines`)

- **Locating a point**: a uniform grid of cell bounding boxes narrows a point to a few cells; inside one, the point is located in a tetrahedral decomposition of the
  cell (cell centre, the centre of every face of more than three nodes, and the face's edges; a triangular face is used as it is). Both cells that share a face split it
  the same way, so the interpolated field is continuous. The field is the barycentric interpolation of the node values, exact for a linear field.
- **Tracing**: fourth-order Runge-Kutta in arc length (unit speed along the field), a step of 35 % of the current cell, in both directions from a seed, until the line
  leaves the mesh, the speed falls below a millionth of the largest, or a limit is reached (2000 steps or 4 model diagonals per direction). Only node vector
  fields are traced.
- **Seeds**: random points of the volume (the same ones every time, so animation frames stay comparable), or points on the cut faces of the enabled Clipping Planes
  ("Seed on the Clipping Plane"), area-weighted. 1-500 seeds; a seed outside the mesh or where the field is zero gives no line.
- **Drawing**: lines coloured by the field's magnitude, with the legend's range when the surface shows the same field. Lines are trimmed to what the planes leave
  visible. The traced lines are cached per (field, step, seeds, seeding), so moving a plane only re-trims them; stepping the timeline retraces.

## 3. Limits

- Cuts and lines use the undeformed mesh.
- Only axis-aligned Clipping Planes (not the box mode, not custom planes).
- The locator keeps one bounding box per volume cell (24 bytes); tracing is synchronous, so a very large result retraces visibly on each step.
- The overlays are recomputed on every refresh that changes them; nothing is stored between sessions except through the `.mvf` snapshot (below).

## 4. What a saved `.mvf` keeps

- **Always**: the view settings (which features are on, field, levels, seeds ...).
- **Default (surface only)**: the cut faces, iso-surfaces and streamlines *as displayed* (frozen: they do not follow a moved plane or another step), with the Clipping
  Planes they were trimmed to; on reopening those planes are switched on again through the Clipping Planes editor. The fields the arrows, streamlines and iso-surfaces use
  are stored too (a restored result needs them to show the features again).
- **Opt-in "Also store the volume"**: nodes, cells, polyhedron faces, the chosen fields at every node and cell, and how the surface maps onto them (snapshot format
  version 3). A restored result is then the full result and everything stays live. Costs file size roughly in proportion to the volume; the save dialog shows the estimate.
  A damaged volume block falls back to the surface result with a warning.

## 5. Tests

`result_tests` covers the cutter (`testSlice`), the locator and tracer (`testStreamlines`: straight flow, rotation on a circle, exact interpolation in tetrahedra,
wedges and pyramids, polyhedra, no-line cases, seeds, field choice) and the snapshot (`testSnapshotVolumeAndOverlays`: overlays and volume round trip, fallback on a
damaged volume, extra fields).
