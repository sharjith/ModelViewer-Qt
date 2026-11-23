# JT embedded Parasolid (XT) segment survey

Survey run on 23 November 2026 over the local JT corpus (95 files; samples stay outside the repository, neutral IDs below).

## Method

`python -I tools/jt/survey_xt_segments.py <jt-folder> --output out/jt-research/xt-survey.tsv`

Read-only: for every JT file it walks the TOC, takes segments of type 17 or 30, decompresses them (zlib or LZMA/xz, whichever the JT version used), finds the neutral-binary flag sequence `PS\0\0` and reads the modeller version and internal schema key from the XT header. No geometry is exported. Sample IDs are the first 8 hex digits of a SHA-256 of the file name.

## Observations

| Modeller version | Internal schema key | Embedded schema | XT segments | JT files |
| --- | --- | --- | --- | --- |
| 22.0 (2200249) | `SCH_2200249_20000_13006` | yes (over 13006) | 161 | 4 |
| 24.0 (2400151) | `SCH_2400151_20000_13006` | yes | 1 | 1 |
| 25.0 (2500176) | `SCH_2500176_25001_13006` | yes | 11 | 2 |
| 26.0 (2600169) | `SCH_2600169_25001_13006` | yes | 144 | 1 |
| 34.0 (3400153) | `SCH_3400153_34001_13006` | yes | 73 | 1 (JT 10.0, LZMA-framed) |

- 9 of 95 files carry XT B-Rep segments; the other 86 carry tessellation only.
- Every XT stream found names a key of the form `SCH_<modeller>_<schema>_13006`, so each carries its own schema as edits over the V13 base. None names an external schema without the base suffix.
- The reader in `src/Import/Parasolid/XtReader.cpp` (`readNeutralBinary`, `requireEmbedded`) accepts exactly this form. The external schema folders added to the text X_T path are not needed for this corpus.

## Qualification limits

- The survey tables above are header-level. Decoding and building are covered separately in the headless run below.
- The corpus has no JT-embedded stream of the newer, non-embedded kind (for example `SCH_3700000_36001`). Whether JT files from recent Solid Edge or NX releases use it is untested here.
- The exact decoder is still opt-in (`MODELVIEWER_JT_XT_EXACT=1`) and the XT length unit versus the JT unit is unverified.

## Headless decode and build run (23 November 2026)

Regenerate: `python -I tools/jt/survey_xt_segments.py <jt-folder> --output out/jt-research/xt-survey.tsv --extract-dir out/jt-research/xt-elems`, then `bash tools/jt/run_xt_elements.sh out/jt-research/xt-elems out/jt-research/xt-exact-run.tsv`. This uses `parasolid_xt_shapes --jt-binary --mesh` (the XT reader, OCCT builder and viewer triangulation check; not the JT importer's completeness gate and not the viewer).

Observations over the 390 XT streams of the 9 JT files with XT data:

- All 390 decode completely (embedded schema over 13006), modeller versions 22.0 to 34.0.
- 384 build every face; 6 streams build fewer faces than the XT holds (for example 10 of 11, 476 of 508, 15 of 18). In the application that part would take the stored-tessellation fallback.
- 7 faces across 3 streams are rejected by the viewer's usable-triangulation check.
- Bodies reported with free edges: 8 to 29 per file in several files; JT parts include sheet bodies, so this count is not a defect count by itself and was not split by body type.
- Total run time for the 390 streams was a few minutes; individual slow streams took up to 24 s.

Qualification limits: header- and builder-level only. No comparison against reference geometry or dimensions, no unit check, and the application's own completeness gate was not exercised.

## Unit and extent comparison (23 November 2026)

Method: overall bounding box of all XT bodies of a JT file (`parasolid_xt_shapes --jt-binary`, which reports millimetres) against the local mesh bounds of the stored JT tessellation (`jt_probe --validate`, built earlier in `out/jt-native-build`, which needs the VS ASAN runtime folder on `PATH`). Raw per-stream boxes: `out/jt-research/xt-bboxes.txt`.

| Sample | JT stored mesh bounds (mm) | XT union bounds (mm) | Observation |
| --- | --- | --- | --- |
| JT-05d5b79b | x -131.98..125, y -65..147, z -22.42..22.5 | x -132..125, y -65..147, z -22.5..22.5 | match within 0.1 mm |
| JT-80e87842 | x 0..195.8, y 0..100, z -12.04..129.96 | x 0..195.8, y 0..100, z -12.04..130 | match within 0.05 mm |
| JT-8ab830dc | x -27.5..27.5, y -60.92..129.4, z 0..197.46 | x -90..143, y -72..132, z -1..219 | same scale, XT larger (18 solids against one JT mesh) |
| JT-013ae192 | x -19.6..81.3, y -33.8..66.2, z -50..50 | x -19.6..82.5, y -65..66.2, z -50..95 | same scale, XT larger on y and z |
| JT-066cfe65 | x 315..435, y -160..80, z -133..262 | x -8250..8988, y -8685..8545, z -8466..8761 | one stream (segment at file offset 1321540) spans 17 m; the other streams are normal |

Observations: where the extents agree they agree to about 0.1 mm, so the XT length unit equals the JT unit for those samples (XT metres, scaled to millimetres by the importer). The 17 m extent comes from the stream that also has 10 of 11 faces built and an invalid body in the run above; a face with a runaway surface is the likely cause (hypothesis, not yet examined). Two samples show XT extents larger than the stored mesh; not yet explained (candidates: hidden or construction bodies, placement transforms not applied in this comparison).

Qualification limits: two samples agree exactly, so the unit is supported for those only; it is not a proof for every JT version or modeller. No independent physical measurement yet.

## Stream with the 17 m extent (JT-066cfe65, segment at file offset 1321540)

Regenerate: `parasolid_xt_shapes --jt-binary out/jt-research/xt-elems/JT-066cfe65_1321540.bin --mesh --faces --face-table out/jt-research/ft_press.tsv`, with `XT_DEBUG_BROKEN=1`, `XT_DEBUG_TOL=1` or `XT_TRACE_IMPORT=1` for the details below.

Observations:

- One body of 11 faces (4 planes, 4 cylinders, 3 B-spline surfaces). 10 faces are built; one cylinder face is omitted (`fill-runaway`). 21 of 64 edges are built; 43 edges are skipped as `wild-edge`, and 3 edges are reported unsupported.
- The 43 skipped edges are tolerant edges whose 3D curve is rebuilt from an SP-curve. Each one is given the whole 63.3 mm SP-curve (parameter range 0 to 62.95) although its two vertices are 1.81 mm apart.
- Four faces are flagged `inflated ... max edge tolerance 8374-8593 mm` and become `broken`. The 17 m bounding box is therefore edge tolerance, not geometry: the triangulated faces have diagonals of 43 to 102 mm, which matches the stored JT mesh bounds (about 120 mm).
- BRepCheck reports the body invalid and 57 free edges.

Hypothesis (not tested): the tolerant-edge rebuild (`tolerantCurve`) does not trim the shared SP-curve to the sub-range between the edge's vertices for this loop-shaped curve, so each edge is wrongly long. This is the same family as the tolerant-edge and blend problems in the Parasolid backlog.

Application effect: the application's completeness gate rejects this part (a face is missing and the shape is invalid) and keeps the stored JT tessellation, so the viewer shows correct geometry for it. No fix attempted.

## The other streams that build fewer faces or are rejected by the mesh check

Regenerate with the same command as above for each stream (`--mesh --faces`).

| Stream | Faces built | Main reasons recorded | Observation |
| --- | --- | --- | --- |
| JT-fbb0276a, offset 1432123 | 15 of 18 | 19 `wild-edge`, 3 `edge-unsupported` | same tolerant-edge pattern as the 17 m stream |
| JT-298492af, offset 1907081 | 476 of 508 | 48 tolerant edges skipped, 16 `TRIMMED_CURVE` edges skipped, 144 B-spline faces filled or refined | 128 free edges, body invalid |
| JT-298492af, offsets 2167118 and 2814528 | 12 of 12 | 3 B-spline faces with stale pcurves, `TRIMMED_CURVE` edges skipped | the two streams give identical results (a repeated part); volume -12.1, so orientation is wrong; the mesh check rejects 2 faces |
| JT-298492af, offset 3096726 | 838 of 838 | 14 `edge-unsupported`, 12 `TRIMMED_CURVE` edges skipped | closed (no free edges) but BRepCheck invalid on 23 faces; the mesh check rejects 3 faces |

Observation: all five come from the same families as the Parasolid backlog (tolerant edges rebuilt from SP-curves, trimmed-curve edges, B-spline faces). In the application the completeness gate keeps the stored JT tessellation for the parts with missing faces. Whether it also rejects the 12/12 and 838/838 streams was not tested, because the gate is not part of the command-line tool.

## Colour attributes (23 November 2026)

The streams carry `SDL/TYSA_COLOUR` (three reals, 0 to 1) on faces and edges and `SDL/TYSA_TRANSLUCENCY` on some faces. The XT builder already turns these into per-face colours (`BuiltBody::faceColours`), as it does for standalone X_T files.

`XT_COLOURS=1 parasolid_xt_shapes --jt-binary <stream>` (added to the tool; tracked change in `tests/parasolid/xt_shapes_cli.cpp`) prints the distinct face colours per body. Over all 390 streams:

- Nearly every face carries a face colour (for example 191 of 191, 1180 of 1180, 2685 of 2685 faces). No body carries a body-level colour.
- Parts are often multi-coloured: 81 of 144 bodies in one file, 35 of 104 in another.
- Compared with the colours the JT importer resolves from the stored JT materials (`jt_probe --materials`), the XT colour set is a subset of the JT set in all 8 files compared: 0 XT colours missing from the JT, and 1 to 2 JT materials (probably defaults) not present in the XT.

Qualification limits: this compares colour values, not which face carries which colour, and only the files in the local corpus. The A95 file where Solid Edge showed colours that our import shows grey is not in the local corpus, so the original observation could not be re-checked here.

Conclusion for this corpus: the XT colours add nothing the stored JT data does not already give. They would matter only for a JT file whose stored materials lack the authored colours, as reported for A95.

## Colour attributes in the A95 file (23 November 2026)

The A95 JT (`F631000929354_REF.jt`, 410 MB) is in the `machines-and-tooling` data folder, not in `jt-samples`. Survey: 2,228 XT streams, all `SCH_3300247_32001_13006` (Parasolid 33.0, embedded schema). `XT_COLOURS=1 parasolid_xt_shapes --jt-binary` on every fourth stream (557 streams, 54,113 faces) and `jt_probe --materials` on the whole file:

- 54,117 face colours for 54,113 faces: effectively every face carries `SDL/TYSA_COLOUR`.
- 33 distinct colours, including yellow (1, 1, 0), magenta (1, 0, 1), white (1, 1, 1), green (0, 1, 0) and red.
- The JT importer resolves 52 materials (41 distinct diffuse colours) from the stored JT data. All 33 XT colours are in that set, so the colours Solid Edge shows are present in the JT file itself.

Observation: the grey seen in our import for the three wheel-arch covers is therefore not caused by colour information missing from the file. Hypothesis, not tested: the JT importer does not map the stored per-face colours (face groups and palette) onto those parts. The XT path would give the colour of every face directly, but only for parts the exact decoder rebuilds completely.

Qualification limits: colour values compared, not face-by-face assignment; only every fourth stream; the three covers were not identified by part name.

## The six wheel-arch covers in the A95 file (23 November 2026)

The owner identified six parts that Solid Edge shows in three colours (yellow, magenta, white) and that our import shows grey: node names containing `ST05_A1_15259`, `A1_14266`, `A2_17610`, `A2_18603`, `A3_20110`, `A3_21103` (A1, A2, A3 pairs). Observations from `jt_probe` and the exported glTF (`out/jt-research/a95*.glb`):

- The covers are long panels (stored mesh extents about 3622, 4037 and 4697 mm by 241 or 891 mm by 324 mm).
- At finest, medium and coarsest detail all six use one material with plain white diffuse (1, 1, 1) and no vertex colours (0 of 2,999 primitives in the file have `COLOR_0`).
- Their node properties hold only names and IDs (`__CAD_STABLE_BODY_UID`, `__CAD_INST_UID`, `SUBNODE`, `JT_PROP_NAME`); no colour is stored there.
- No Parasolid stream has extents matching the covers (sorted extents compared to 2 mm, and by proportions). The comparison would miss a stream whose bounding box is rotated relative to the stored mesh, so this is not proof that the covers have no Parasolid stream. The segment-to-part mapping is not exposed by the JT reader, so it could not be made directly.

Hypotheses, not tested: Solid Edge takes the colours from data our importer does not read (an attribute on a node above the part, or the Parasolid streams if the covers do have one); or from a source other than the JT.

## NX comparison (owner screenshot, 23 November 2026)

The owner opened the same assembly in NX. In NX the six wheel-arch covers are white, as in our viewer (stored material 1, 1, 1 at every detail level). The yellow fixture, the blue plate and the small magenta brackets match between NX and our viewer. The long cross-rails and a plate that our viewer shows pure green (0, 1, 0) appear grey in NX, and our viewer shows a few red parts that NX does not; in the file, 54 meshes at finest detail (12 at coarsest) carry pure green and 185 (9 at coarsest) carry red.

Observation only: there is no cover colour defect in our importer when NX is the reference; the yellow/magenta/white covers seen earlier in Solid Edge are not shown by NX either. The green and red parts are a separate difference between NX and the stored JT materials, not investigated.

## Provenance of the green and red parts in the A95 file (23 November 2026)

Method: `jt_probe --source-attributes` (attributes as stored, no inheritance applied) and `jt_probe --lod-shapes` on `F631000929354_REF.jt`, then a script that groups the shape nodes under each `RangeLOD` node. Regenerate: `jt_probe --source-attributes <jt> > out/jt-research/srcattr_a95.txt` and `jt_probe --lod-shapes <jt> > out/jt-research/lodshapes_a95.txt`.

Observations:

- All 2,228 parts have one `RangeLOD` with three alternatives. The alternatives are Shape LOD segment types 7, 8 and 9; their vertex counts fall (for the first part: 3372, 1428, 1236), so alternative 0 is the finest and alternative 2 the coarsest.
- Every pure-green (0, 1, 0) and pure-red (1, 0, 0) material is stored as a plain material attribute on a triangle-strip shape node (78 green and 203 red shape nodes; not forced, not final, no inhibit flags, no palette index). None is on an instance, part or group node.
- For the parts whose coarsest alternative has green or red shapes, the finest alternative's shapes carry no material of their own in 176 of 197 parts; 21 parts carry the same colour on both.
- The instance node above those parts carries one material, grey (0.6, 0.67, 0.69) or white (1, 1, 1), not forced and not final. For yellow parts the instance material is itself yellow in 969 of about 1,100 cases, so yellow is part-level data present on every level.
- The stored 22 `MetaData` materials are flagged ignored with alpha 0 (defaults), and are not the source of any visible colour.

Reading: the green, red and magenta per-shape colours are authored only on the coarsest alternative. The finest alternative resolves to the part-level (instance) material, grey or white. NX and our Finest view both show that; our viewer's default shows the coarsest-authored colours on every level (the policy recorded in `JT_C5_LOD_MATERIAL_VARIANTS.md` section 10). Which of the two is right is a policy choice; the spec does not tie materials to a level.

Qualification limits: attributes read from the file graph only (external partitions not followed); the NX behaviour is an owner screenshot, not a measurement; shape counts include repeated geometry.

## Next steps (not started)

1. Green and red parts: provenance established above; remaining decision is the owner's default-material policy (finest or part-level colour against coarsest-authored colour).
2. Confirm the millimetre unit with a physical measurement of one part (owner), and explain the two samples whose XT extents exceed the stored mesh.
3. Fix the tolerant-edge sub-range for the 17 m stream and the 1432123 stream (shared with the Parasolid backlog), then re-run these streams.
4. Make the JT exact path opt-out instead of opt-in only after items 1 to 3, and with a viewer check on the nine files.
