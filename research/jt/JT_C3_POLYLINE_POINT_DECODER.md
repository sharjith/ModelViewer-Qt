# C3 polyline and point shape decoder (first implementation)

Status: written and first run 5 November 2026 on `feature/JT-importer`. Compiled; `jt_probe` loads all eleven samples (face counts below). Not yet confirmed visually by the owner; nothing here is a conformance claim.

## Samples

The owner supplied eleven JT10.10 LE files written by NX (`out/jt-research/c3-nx-samples`, with the source `.prt` files). They are the first authentic files with Polyline Set Shape LOD (`10dd10a1-...`) and Point Set Shape LOD (`98134716-0011-...`) segments. Every file except `C3_PT1` and `C3_I1` also carries a precise Wireframe Rep (segment 18); the declared edge counts are 4 (`C3_P1`, `polyline1`), 5 (`C3_P2`, `C3_M1`, `C3_C1`), 7 (`C3_P3`), 3 (`polyline2`), 2 (`C3_U1`, `C3_V1`). Expected geometry per sample is not yet written down; the owner confirms the result visually in the viewer. Inspection commands (read-only, output under `out/`):

```text
python -I -B tools/jt/inspect_wireframe_elements.py out/jt-research/c3-nx-samples/C3_P1.jt --output out/jt-research/c3-nx/C3_P1.wire.json
```

## Layout observed (hand-parsed from `C3_P1`, JT10.10)

- Node elements: `10dd1046-...` (polyline) and `98134716-0010-...` (point) = Vertex Shape Node data, then a version byte and an F32 (observed value 0.01), which this reader calls the area factor. The bounds, area and vertex/primitive count ranges precede it in the usual shape-node block.
- LOD elements: base data, vertex binding, then a logical element of base type 9 whose GUID differs from the tri-strip one: `11c12d32-38f9-45ba-93ba-66f9d538ddfb` (polyline), `...ddfc` (point). Then mesh version, a 4-byte value (not interpreted), version.
- TopoMesh Compressed Rep Data (polylines and points alike): I32 face-group count; I32 primitive count; I32 vertex-list length; CDP2 face-group boundaries (count + 1 values); CDP2 primitive boundaries (count + 1 values); CDP2 vertex list, all three with the Lag1 predictor; I32 hash; 8-byte vertex binding; 4 quantization bytes; I32 vertex record count; CDP2 records-per-coordinate list (Null predictor); I32 (equals the unique coordinate count); compressed coordinates (the existing JT10 coordinate array); optional normals; optional colours.
- The first attempt guessed this layout wrongly in three places, found by running it: points carry the face-group list too; there is no separate unique-count I32 before the records-per-coordinate list; and the three lists need the Lag1 predictor, which (as for every JT predictor) leaves the first four values untouched, so the raw lists looked right for four entries and wrong afterwards.
- Colours: the NX lossless colour arrays of these LODs hold the raw component words (Null predictor); the Lag1 form that the tessellation path assumes for lossless JT10 colours produced nonsense (values above 1e37). `JtDecode_VertexColors::Read` gained a `losslessLag1` argument, default true, which the polyline/point reader sets to false.
- In `C3_P1`'s polyline LOD the counts are 1 face group, 4 primitives, 8 vertex-list entries, 5 vertex records, 5 unique coordinates; the three CDP2 headers carry 2, 5 and 8 values. Reading it as four two-vertex polylines (one per segment) is consistent with the node's vertex count range of 8 and with the Wireframe Rep's 4 edges.

## Implementation

`JtElement_ShapeLOD_Vertex::readTopoMeshCompressedRepData` decodes the above for JT10 and fills `Vertices()` (one per vertex record), `Indices()` (2 per segment, or 1 per point), optional `FaceGroups()` (one per segment, only when there is more than one group) and `Primitive()`. New classes: `JtElement_ShapeLOD_PolylineSet`, `JtElement_ShapeLOD_PointSet`, `JtNode_Shape_PolylineSet`, `JtNode_Shape_PointSet`. `JtSceneBuilder::storedMesh` builds line and point `aiMesh` objects (no normals; palette-group merging and part merging already skip non-triangle meshes, C3 stage B).

Not decoded and rejected with an explicit message: texture, flag and auxiliary vertex attributes; JT 8.x and 9.x polyline/point LODs (9.x still counted as skipped content).

## First run (5 November 2026)

`jt_probe` on each sample, meshes as polylines/points (faces = segments + points): `C3_P1` 4 + 5, `C3_P2` 5 + 7, `C3_P3` 7 + 8, `C3_M1` 5 + 7, `C3_PT1` 6 points, `C3_C1` 5 one-segment meshes, `C3_U1` and `C3_V1` 2 segments, `polyline1` 4 + 5, `polyline2` 3 + 4, `C3_I1` 16 faces in 4 meshes. The segment counts match the Wireframe Rep edge counts listed above. Every sample whose polyline LOD is present also carries a point LOD (apparently the vertices of the polyline); how the viewer should show both is open. Decoded vertex coordinates of `C3_P1` form a plausible planar chain (z = 0).

## Hypotheses to verify

- Boundaries versus counts: the primitive and face-group lists are treated as ascending boundaries starting at 0 (consistent with every decoded sample).
- Records-per-coordinate list: treated as the number of consecutive vertex records sharing each unique coordinate (all ones in the samples).
- Whether NX closed polylines (`C3_P3`) repeat the first point or use a flag.
- Trailing bytes after the element are skipped by the model reader, so a misparse of the tail would not be reported.

## Regression test (5 November 2026)

`tests/jt/jt_c3_sample_tests.cpp` (part of `jt_tests`) imports each local sample from `MV_JT_C3_SAMPLES` (default `out/jt-research/c3-nx-samples`) and checks the segment and point counts above, that every face has the index count of its primitive kind with in-range indices, and that no triangles are invented (`C3_M1` holds only a polyline and a point shape, 5 segments and 7 points, with no triangle solid, as `jt_probe` and the segment inventory show). Files that are not present are skipped. Not run until the owner builds; the counts are the ones the owner's viewer checks matched, not an independent reference.

## Sample checks for instancing, units and visibility (5 November 2026)

Owner observations in the viewer plus read-only `jt_probe --properties` on the local files. The three exports do not exercise what the sample plan asked for, and the exact samples cannot be produced with the available NX export, so these rows stay **inconclusive**; nothing is claimed for them.

- `C3_I1`: an assembly of two different parts (`polyline1` and `polyline2`, each as a `_WF` and a `_POINTS` part under nested wrapper assemblies), not one wire instanced twice. The geometry shown matches the decoder note (4 + 3 segments with their points). Shared-instance handling of line and point shapes is not tested by this file.
- `C3_U1`: `JT_PROP_MEASUREMENT_UNITS` is Millimeters, one wire shape, 4 vertices, 2 segments; structurally the same as `C3_V1`. No other unit system is present, so unit conversion of wires is not tested.
- `C3_V1`: one shape payload, no hidden marker in the node properties, both segments shown. Either NX did not write the hidden wire or the file carries no hidden state; the hidden-node path for wires is not tested.
- Not covered by any sample: selection on line and point meshes (owner manual check), the GLB round trip.

Qualification limit and what covers it instead: from the code, instancing, transforms and visibility are node-level and independent of the primitive kind, the geometry cache and subsets keep the primitive type, part merging skips non-triangle meshes, and the declared length unit is reported on the file node only (geometry is never scaled). These are argued from the code, not shown on authentic wire files. `lineAndPointScenePaths` in `tests/jt/jt_scene_tests.cpp` (synthetic, in-memory node graphs; not evidence of any exporter's output) checks them for line and point shapes: three placements of one shared line shape with two appearances (shared mesh, placement, colour variant, exact coordinates, segment indices, no normals), a hidden line shape and a point shape under a hidden group (kept and marked hidden), and a part with a triangle, a line and a point shape (all three kinds kept). Unit scaling has no code to test (no scaling is applied); a real wire export in a second unit system would still be needed to see the viewer's Import Units behaviour. Not run until the owner builds `jt_tests`.
