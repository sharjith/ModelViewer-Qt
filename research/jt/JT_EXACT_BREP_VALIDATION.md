# JT exact B-Rep (embedded Parasolid) - validation record

Written 24 November 2026. Covers the work that makes the Parasolid B-Rep inside a JT file usable as the part geometry, after the header-level
survey in [JT_XT_SEGMENT_SURVEY.md](JT_XT_SEGMENT_SURVEY.md). Samples stay outside the repository; they are named here by the neutral IDs of that
survey (`JT-<8 hex digits>`, the start of the SHA-256 of the file name).

## What the feature does

Settings > Import Export > JT Importer > "Use the embedded Parasolid B-Rep when available" (off by default;
`MODELVIEWER_JT_XT_EXACT=1` or `0` overrides it). With it on, each JT part that has an XT B-Rep segment is rebuilt from it instead of drawn from
the stored tessellation:

1. The XT payload is decoded (`Xt::shapeFromJtElement`) with the length unit the file declares (JT_PROP_MEASUREMENT_UNITS; millimetres when none).
2. The part must pass the completeness gate (below), its extents must match the stored shapes' box within 5 % of the box diagonal, and every face must mesh.
3. The shape is meshed like an X_T body (`Xt::meshesFromShape`: configured deflection, the whole shape again at another deflection while faces get no
   triangles, shared B-Rep converter). The meshes have smooth normals and carry the B-Rep edge and face tables, so the wireframe draws the exact edges.
4. The part takes the material of its stored shapes. When those shapes disagree (several colours, or a different material in another LOD alternative) the part
   keeps the stored tessellation, because one material would lose that.
5. A part's meshes become one mesh (`BRepToAssimpConverter::mergeConvertedMeshes`, the edge and face tables merged with it); sheet bodies are
   converted as bodies of their own; faces of a sheet body that repeat a face of a solid of the same part are dropped.
6. A part that fails any step keeps the stored tessellation and says why in the import warnings. The notes end with the number of parts rebuilt, the number
   that kept the stored mesh, and how many meshes carry B-Rep edges.

## Findings

Observations (measured or seen) unless marked otherwise.

- **JT 9.5 XT segments** (two files, `JT-05d5b79b` and `JT-80e87842`, 11 XT segments written by DM 7.3.12.2): the XT element is read correctly by the shared
  reader (the header's object ID is consumed for versions above 8), but it is followed inside the segment by the End-Of-Elements marker element and six bytes (an
  I32 of 1, two zero bytes). The single-element segment reader reported trailing zlib data and rejected every XT B-Rep. `JtData_Model.cxx` now reads and checks the
  marker and trailer for XT segments of JT 9.5 only. 9.0-9.4 XT segments were not observed.
- **Rational flag.** XT streams from the JT files carry NURBS surfaces and curves whose control net holds weights in a fourth coordinate (`vertex_dim` 4,
  x*w, y*w, z*w, w) with the `rational` flag clear. The reader rejected them as inconsistent. A homogeneous net is now rational whatever the flag says
  (`nurbsSurface`, `nurbsCurve`; `XT_NO_DIM4_RATIONAL=1` restores the old rule). This single rule removed the skipped B-curve and trimmed-curve edges, most
  filled (approximated) faces, and the rejection of the nuts of one assembly.
- **V-closed ring chart.** A rational surface of revolution closed in V (a rounded rim between two circles) went through the seam repair, which cut the face into two
  slivers. The ring-chart conversion that existed for U-closed charts now also handles V-closed ones (`XT_NO_V_RING=1` restores the old behaviour).
- **Black patch on a socket head cap screw.** The two defects above kept the rim face of the screw from building. The builder filled a surface through its outer
  boundary, could not cut the hex socket out of it (`fill-holes-invalid`, `fill-capped-incomplete`), and kept the capped face: a patch across the socket, drawn black.
  The gate rejects any part with a capped-hole face; with the two fixes the screw streams build natively and the patch is gone.
- **Completeness gate.** The earlier gate counted the informational counters of `facesSkippedBySurface` and `edgesSkippedByCurve` (chart-refined, tolerant-loop-kept, ...)
  as skipped faces and edges, and refused every fitted blend, so it rejected parts that were fully built. It now tests faces built = faces, edges built = edges, no
  skipped tolerant edge, direction conflict, unsupported transform, missing occurrence or diagnostic, every body placed, and no capped hole.
- **Coincident surfaces.** A part can hold a sheet body next to its solid (a surface model of the same part) that repeats faces of the solid and lies on its bore.
  The lightweight mesh of such a part is one mesh containing both; drawn as separate meshes the coincident surfaces fight for depth. One mesh per part removes it.
- **Import progress.** `BRepToAssimpConverter::convert` reported progress from the start of the reading band for every call, so each rebuilt part set the bar back
  and the JT read pushed it forward again. The JT part conversion now passes `reportProgress = false`.

Hypotheses not verified: the 26 trailer bytes of the 9.5 segments have no documented meaning here; only their structure is checked.

## Results

Gate over the 390 XT streams of the local corpus (`tools/jt/gate_scan.sh`, `tools/jt/gate_tally.py`):

| State | Streams passing |
| --- | --- |
| Original gate (informational counters counted, no fitted blends) | not counted over the corpus; 33 of the 53 streams of `JT-066cfe65` in the viewer |
| Relaxed gate (built-of-total counts, blends accepted) | 362 |
| Plus the capped-hole rule | 331 |
| Plus the rational-flag and V-closed ring-chart fixes | 365 |
| Plus the same rule for NURBS curves | 390 |

A stream that passes has every face and edge built; the cap-screw stream (`JT-066cfe65`) and the nut streams (`JT-013ae192`) are BRepCheck-valid with a closed
shell. The orientation of the built solids was checked on all 53 streams of `JT-066cfe65` (a sample point just outside each face, and each mesh triangle, classified
against its solid): no face points into the material.

X_T regression after the builder changes: 20 local X_T samples under 60 MB, the summary lines compared before and after each change. 16 are identical. The others
differ by one built face or the fifth digit of a volume, which is the builder's run-to-run variation (below), not the changes: the same file gives both values on
identical runs, and with the rules switched off.

Owner acceptance, 24 November 2026, in the viewer with the setting on: two JT 9.5 assemblies (colours, stems of the washers, no z-fighting after the one-mesh
merge), the fastener assembly `JT-066cfe65` (B-Rep edges on every part, no black sockets, the nuts), the assembly with the nuts `JT-013ae192`, the 410 MB file
`JT-f1d65f65`, and the other local JT and X_T samples ("everything looks fine"). The progress bar no longer flickers.

## Qualification limits

- **Run-to-run variation.** One face in a few thousand of the Parasolid builder is built on one run and not on the next (a 58 MB X_T sample alternates between 18380 and
  18381 built faces and a 24 MB one between 5143 and 5144, with the rules on or off). The cause is not traced; a missing face on one load that is present on the next is this.
- Parts whose stored shapes carry different materials keep the stored tessellation (most parts of one 144-stream file).
- Only JT 8.1, 9.5 and 10.0 XT streams were available. The length unit is taken from the declared JT unit (millimetres, centimetres, metres, inches, feet); other units
  and files whose nodes disagree keep the stored tessellation.
- The meshes are finer than the stored ones (about 84 triangles per cylinder face against 56-74), because the importer's relative deflection is used.

## Regenerating the evidence

```
python -I tools/jt/survey_xt_segments.py <jt-folder> --output out/jt-research/xt-survey.tsv --extract-dir out/jt-research/xt-elems
bash tools/jt/gate_scan.sh out/jt-research/xt-elems out/jt-research/gate.txt
python -I tools/jt/gate_tally.py out/jt-research/gate.txt
```

Per stream (the test tool must be built, see `tests/parasolid/CMakeLists.txt`): `XT_ORIENT_CHECK=1 parasolid_xt_shapes --jt-binary <stream.bin> --mesh` for the
orientation check, `XT_FACE_DETAIL=<face id>` for the wires of one face, `XT_FALLBACK_CHECK=<body>` for the importer's recovery of unmeshed faces,
`XT_DEFL_SWEEP=<body>` for meshing at several deflections, and `--face-table <out.tsv>` for the per-face table. The JT 9.5 segment check:
`JT_PROBE_XT=1 jt_probe <file.jt>` (a stub decoder; every XT payload must report its size). Output goes under `out/`.
