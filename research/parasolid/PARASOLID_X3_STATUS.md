# Parasolid X3-X6 status: OCCT shapes and viewer import

Written 15 Nov 2026. Everything here is **observation on local samples** unless marked otherwise;
sample IDs are neutral (`XT-<12 hex of SHA-256>`, see [PARASOLID_VALIDATION_PLAN.md](PARASOLID_VALIDATION_PLAN.md)).
Nothing is committed. The code is `XtModel`, `XtOcct`, `XtImport`, `XtSceneImporter` under
`include/Import/Parasolid/` and `src/Import/Parasolid/`.

## What exists

| Piece | Role |
| --- | --- |
| `XtOcct` | Builds one OCCT shape per BODY (analytic surfaces, B-spline surfaces and curves, intersection charts, trimmed curves, offset surfaces, tolerant edges rebuilt from SP-curves, assemblies with rigid instance transforms). Lengths are scaled from metres to millimetres. |
| `XtImport` | Decode + build entry points: `importText` (standalone X_T) and `shapeFromJtElement` (JT XT segment, neutral binary, tracked JT table). |
| `XtSceneImporter` | Qt side: finds the layout file, runs the import, converts through `BRepToAssimpConverter`. |
| `AssImpModelLoader` | New `x_t` / `xmt_txt` branch; the JT branch now sets `options.exactDecoder`, so XT B-Rep segments in JT files are decoded exactly (a shape that fails `BRepCheck` falls back to the stored JT tessellation, as before). |
| `tests/parasolid/xt_shapes_cli.cpp` | Standalone inspector: `parasolid_xt_shapes <layouts> <file> [--deviation N] [--faces] [--mesh] [--attribs] [--dump out.brep]`. |

The base-layout file for standalone X_T is **not** in the repository. The viewer looks for
`xt-layouts-v13.txt` in `MODELVIEWER_XT_LAYOUTS`, the QSettings key `xtLayoutsFile`, next to the
executable (and its `data/` folder), and in the per-user data folder
(`%APPDATA%\Sharjith N\ModelViewer`).

## Conventions confirmed against the local samples

Measured by `--deviation`: the largest distance between sampled edge points and the surface of a face that uses the edge.

- Plane, cylinder, sphere: the reference formulas and OCCT's parameterisation agree; deviation <= 1.4e-14 mm in the bolt.
- **Cone: the reference formula has the wrong sign for these files.** Documented `R = P - vA + ...(r + v tan a)` puts the edges 0.707 mm away. Radius growing by `tan(a)` per unit along `+A` from `pvec` gives 4e-15 mm. The code uses the measured behaviour.
- Edge end vertices: forward vertex of a positive fin is the edge end, of a negative fin the edge start (reference 5.3.9.1); dummy fins carry the vertices of sheet boundaries.
- Solid bodies: faces in a solid region's `face` chain (back faces) enter the OCCT shell with forward orientation, `front_face` chain reversed. Resulting solids have positive volume and no free edges in the bolt and in XT-e62b10ff0279.
- XT has no seam edges. A face on a surface closed in u or v is bounded by loops that do not touch, so OCCT needs a seam; `ShapeFix_Face` with only the missing-seam repair enabled adds it, sharing one `ShapeBuild_ReShape` per body so neighbouring faces keep the same edges.

## Results on the local samples (15 Nov 2026, standalone `parasolid_xt_shapes`)

| Sample | Result |
| --- | --- |
| JT-embedded bolt | 21/21 faces, valid solid, no free edges, volume 4315 mm^3 |
| XT-e62b10ff0279 | 43/43 faces, valid, closed, 2252 triangles |
| XT-8ffda52db5e5 | 510/518 faces (blends and tolerant edges included), 16 of 28 bodies valid |
| XT-8ea8be98e927 (87 MB, 1885 bodies) | 17004/22090 faces, build 94 s (was 593 s before per-face and chart fixes) |

Observations behind the speed fix: the cost was spread over many edges (no single edge above 0.2 s), mostly projecting rebuilt tolerant-edge and chart curves onto faces; reducing sample counts and fitting dense charts with a spline brought it down.

Other findings: XT periodic flags are sometimes paired with a clamped knot vector, so periodic form is used only when the end vertices really repeat; the B-spline control net is stored v-index fastest.

## Open points

- B-spline control-net order (u index fastest) is assumed; the measured deviation for `B_SURFACE` faces is the check.
- Rolling-ball blends (`BLENDED_EDGE`), swept and spun surfaces are not built yet; faces on them are reported as missing.
- Instances with reflection or general affine transforms are placed without the transform (warning).
- Part names and colours (attributes) are not read yet; bodies are named `Body <node index>`.

## Review follow-up (Codex static review, 15 Nov 2026) and spike investigation

Codex reviewed the uncommitted code (no builds). Each point was checked against the code; outcomes:

| Point | Outcome |
| --- | --- |
| Cyclic topology could hang the import | Confirmed. Region, shell, face, loop and edge chains now go through `Builder::chain()`, which stops at a repeated node, reports `cyclic-chain` and polls cancellation. |
| JT could replace stored tessellation with an incomplete or approximate shape | Confirmed. `shapeFromJtElement` now returns a shape only when every face was built, nothing was fitted or approximated (blends, intersection charts and rebuilt tolerant edges count), no occurrence was dropped and there are no diagnostics; otherwise the stored JT tessellation is kept. |
| JT exact parts and stored meshes could use different units | Confirmed in principle (the XT unit inside JT is not verified against the JT unit). The exact decoder is now off unless `MODELVIEWER_JT_XT_EXACT=1`. Standalone X_T still assumes metres (the XT default) and says so in the load warnings. |
| Meshing worker could terminate the viewer on an exception | Confirmed. Every step in a worker is caught, thread start-up failures are tolerated, and a failed body is left out. |
| Reader applied the V13 table without checking the base | Confirmed. `requireEmbedded` now requires a `_13006` base (`unsupported-base-schema`); test added. |
| Unsupported assembly transforms silently changed the model | Confirmed. The occurrence is now left out and counted in the load warnings. |

Spikes seen in the viewer were mostly one bug: lengths in XT *parameters* (line parameters in trimmed curves, plane and cylinder uv of SP-curves) were not scaled to millimetres with the geometry, so edges missed their vertices by hundreds of metres and the repair step stretched tolerances across whole bodies. Trimmed edges now take their range from the end vertices, SP-curve rebuilds scale plane/cylinder uv and are only attempted on surfaces whose uv are known, and edges that miss their vertices, run far beyond their chord, have mismatching pcurves or micro-length are left out and counted. Measured (standalone): the 87 MB sample's bounding box went from about +-10 million to 283 x 375 x 407 mm, and no face reaches outside its edges.

## Face-loss work (16 Nov 2026, standalone `parasolid_xt_shapes --mesh`)

Observations, all measured with the standalone tool on local samples (regenerate: `parasolid_xt_shapes <layouts> <file.x_t> --mesh`, with `XT_UNTRI=1` for the untriangulated-face classification; output under `out/`):

1. **Shared-edge repair dropped pcurves.** Applying the per-body `ShapeBuild_ReShape` replaced edges by the copies of repaired neighbours, which carry only that face's pcurve. About 260 faces of one engine sample had edges without a pcurve and could not be meshed. Missing pcurves are now re-added after the reshape step.
2. **Fitted blend faces had the wrong wire sense.** The outer wire came out clockwise in (u, v) for every fitted blend (581 faces in one sample), which the mesher rejects. The outer-wire direction in parameter space now decides whether wires and face are flipped together (non-periodic surfaces only); the correction is counted per surface kind (`orientationFlips`).
3. **Nested blend contacts.** The side a dependent ball sits on was predicted from the XT sense and was wrong in most cases; the contact is now accepted on either side of the section normal.
4. **Vertex gaps.** Intersection charts can stop up to a couple of millimetres short of a vertex. Gaps up to 2.5 mm (and at most a quarter of the edge length) are absorbed as vertex tolerance; larger gaps still drop the edge.
5. **Self-intersecting wires stay filtered.** Keeping them recovers about 400 more faces in the 87 MB sample but raises its meshing time from 25 s to 456 s and leaves two faces with stray triangles, so the filter stays (`XT_KEEP_SI=1` disables it for experiments).

| Sample | Faces triangulated before | after |
| --- | --- | --- |
| XT-8ffda52db5e5-class engine (9249 faces) | about 8165 | 8817 |
| 18385-face engine | about 17000 | 17855 |
| 4157-face robot assembly | 3836 | 4015 |
| 14375-face twin engine | not measured | 14021 |
| 87 MB sample (22090 faces) | 17964 built | 19096 |

### Second round (16 Nov 2026, after owner review of the viewer)

The owner reported many surfaces still missing in the viewer. Observations (standalone, same meshing parameters as the viewer; `parasolid_xt_shapes <layouts> <file> --mesh`):

6. **STEP post-processing as a model.** OCCT's STEP reader runs a repair pass over every face (wire fixing with self-intersection repair, `FixAddPCurve`, `FixSameParameter`; sources read: `XSAlgo_ShapeProcessor`, `StepToTopoDS_*`, OCCT 7.9.1 tree in the vcpkg build trees). A face whose wire self-intersects now goes through `ShapeFix_Face` with self-intersection repair before it is dropped; 61 more faces of one engine are kept this way. Plane faces with self-intersecting wires are kept as they are (all analytic kinds kept: +300 faces but 5x slower meshing of the 87 MB sample).
7. **Micro edges** shorter than the modeller resolution used to drop every face that used them. The two end vertices are now merged (`ShapeBuild_ReShape::Replace`) and the edge is left out of the wires.
8. **Fins without an edge** (isolated-vertex or dummy fins) were treated as unsupported edges; they are now skipped, the loop continues.
9. **Boundary fill (approximation).** A face whose own surface cannot be built (blend types or contacts not supported, unsupported edges bridged by a straight segment between their vertices) or whose surface disagrees with its edges (pcurve-mismatch) is replaced by a surface filled through its outer boundary (`BRepFill_Filling`, C0 constraints), oriented along the XT loop direction. These faces are approximations: boundary exact, interior inferred; holes in such a face are not cut out. They are counted (`facesFilledFromBoundary`) and reported in the load warnings. `XT_NO_FILL=1` disables this for comparison.

| Sample | faces built (of total) | meshed standalone |
| --- | --- | --- |
| simple model (518 faces) | 518 | 518 |
| XS650-class engine (9249) | 9246 | 9214 |
| 14375-face engine | 14353 | 14343 |
| 4157-face assembly | 4149 | 4142 |

Remaining: faces with no closed boundary in the file (`fill-noring`), bridging edges whose vertices coincide (`fill-nobridge`), fill failures after pcurve mismatch, and untriangulated multi-wire planes (the viewer's neighbour-based recovery rebuilds those; the standalone tool does not apply it). Filled faces have not been inspected in the viewer.

Stray-triangle faces: 0 in all but the experiment above. Remaining gaps: blends with chamfer types or blend supports that are not plain surfaces (`blend-why-1`), nested radius mismatches (`blend-why-7`), edges without supported geometry, pcurve mismatches.

### Third round (16 Nov 2026, after owner review of fills and an external review)

Observations (standalone tool; output under `out/`):

- Boundary fills drew twice: a face rejected by the exact builder had already queued a display candidate, and the fill then added a second patch. The candidate queued during a rejected attempt is now dropped when the fill succeeds.
- Boundary fills capped holes. Inner loops are now cut out of the filled surface (`fill-holes-cut`); when the trimmed face is invalid or leaves its boundary the capped face is kept (`fill-holes-invalid`). Hole cutting rarely succeeds (1 of 18 on one engine sample); this remains open.
- Fills could run away: on one engine sample several filled faces meshed up to 6 km from the part (`XT_STRAY=1` shows them). Each fill is now meshed and rejected when its triangles leave the box of its own edges; the second fill setting, then a plane through a nearly planar boundary (`fill-planar`), is tried instead. No stray triangles remain on that sample, at the cost of a few faces that no fill can close.
- The shape bounding box of that sample is still about 2 000 mm instead of 300 mm because a B-spline surface bound is wide even though its triangles are not; viewer fitting uses triangles.
- Shared tolerance reset: not addressed. OCCT's tolerance update calls only increase tolerances, so a blown-up tolerance can propagate to neighbouring faces (external review finding; not verified here).

Not done: native blend contact solving (`blend-why-9`, `blend-why-1`), the largest remaining class; sewing a fill to its neighbours (fills create their own boundary edges); viewer inspection of any filled face.

### Fourth round (16 Nov 2026): hang, fill cost, blend failure breakdown

Observations (standalone tool; output under `out/`):

- Hang: the stray-fill check meshed the filled face, and the mesher did not return on one runaway surface (traced to the check itself). The check now samples a 25 x 25 grid of the surface over its parameter box and applies the same size test; no meshing is involved.
- Fill cost on the large engine sample (1 885 bodies, 22 090 faces; 21 951 built): build 564 s, of which fills about 380 s (first variant 120 s, second 100 s, fills over existing boundaries 50 s, plus validation 78 s). About 2 150 faces are accepted from the first fill variant and 169 from the second, that is roughly 10 % of the built faces are fill approximations, not faces built from their own edges. Most fills take well under a second; a few failed fills on 4-6 edge boundaries took 3-14 s and the second variant repeated the failure. Trial: skipping the second variant after a first variant that failed in over a second cut the large-sample build from 564 s to 519 s, but built faces fell from 21 950 to 21 942 (second-variant acceptances 169 to 161). Eight faces lost for 45 s, so the trial was reverted; no fill-time reduction is in place. The same run reported 16 397 valid and 5 545 invalid faces of 21 942 by `BRepCheck` (invalid faces are still drawn when they mesh; the meshed count was not measured on this sample).
- Blend faces skipped (`BLENDED_EDGE`, large sample): about 1 214. By code: contact projection or nested contact failure (9) 561, spine or support surface not built or nested type not rolling-ball (1) 405, closed spine (7) 128, degenerate section angle (11) 102, no edge points (5) 17, other 1.
- On the smaller engine sample (171 blend faces skipped) 96 are code 9 and nearly all of them are nested, one support being a blend. The nested contact evaluator fails with: foot-of-spine distance not equal to the ball radius (37), inner support surface not built (25), contact residual outside tolerance (19 + 20), contact outside the section angle (21), blend type not rolling-ball (11).

Hypotheses, not tested: the projection of the spine foot onto the inner supports picks the wrong branch when supports are nearly tangent; unsupported inner surfaces (offset, swept) account for the code 1 and `bsc 3` cases; a closed spine could be split into two halves.

### Fifth round (16 Nov 2026): fixes from the external review, and the AGP-M1 object count

Observations (standalone tool; output under `out/`):

- Periodic blend span: the guard that rejected closed blends took the minimum and maximum of wrapped spine parameters, so a short blend crossing the period origin looked nearly a full turn long. The span is now the smallest arc covering all points (period less the largest gap). Large engine sample: code 7 rejections 128 to 19, blend faces skipped 1 214 to 1 105. The 109 recovered faces are built from an approximated B-spline fit and 110 more faces fail `BRepCheck` than before, so they are probably recovered but not valid; not yet analysed.
- Fill acceptance test: compares each axis with the source boundary and counts only samples inside the trim (the earlier test compared the longest dimension and sampled the whole parameter box).
- Holes: the replacement path used after `SameParameter` now cuts the original's inner loops out of the fill, and both paths accept a cut only when every hole survives (`fill-holes-invalid`, `fill-broken-holes-capped` count the failures).
- Trim validation: a face with an inflated edge tolerance is no longer removed unless its own 3D curves and pcurves disagree (`trim-validated-kept` counts faces kept). Small engine sample: 9 232 to 9 245 faces built, 9 205 to 9 217 meshed, runaway fills 12 to 1. The simple sample is unchanged (518/518 built, 503 valid, 518 meshed).
- AGP-M1 object count: the X_T file has 1 ASSEMBLY, 28 BODY and 29 INSTANCE records. One body (5 faces) is placed twice, at two positions; all others once, so the tree has 29 placed occurrences plus the root. The 28 JT part files (tessellated shape-LOD segments only, no B-rep or X_T segment) hold one file per occurrence, seven carrying an `-oa` suffix. Four JT files are of the 5-face washer size against five washer occurrences in X_T. The JT assembly structure was not decoded (compressed scene graph), so it is not established whether JT omits one occurrence or references one file twice.
- Reference value of the JT: it is tessellated and exists only for this sample among the local X_T samples; it cannot supply topology or geometry strategy for the engine models, where the losses are. An internal check is available for every X_T body (closed shell, positive volume, faces built against faces in the file).

Verification of the fifth-round changes (standalone, large engine sample): 22 010 faces built of 22 090 (was 21 950), fills accepted from the first variant 2 150 and from the second 104 (was 169), runaway fills 56 to 18, `trim-validated-kept` 54, build 603 s. `BRepCheck` reports 16 306 valid and 5 704 invalid faces (was 16 397 and 5 545 of 21 942), so the recovered faces are mostly not valid; the number of invalid faces that still mesh was not measured on this sample. Small engine sample: 9 246 built, 8 234 valid, 9 218 meshed. A first run of this build crashed (access violation) inside OCCT's 2D face classifier on one filled face; the sampling now runs under structured exception handling and falls back to the whole parameter box.

### Sixth round (16 Nov 2026): why rescued faces keep inflated tolerances

Observations (standalone tool, small engine sample; `XT_DEBUG_TRIM=1` prints the traces):

- B4T with meshing (before the changes below): 22 006 faces built, 21 803 triangulated, 1 775 809 triangles, build 660 s plus 168 s meshing. Free edges per body cannot be read as an integrity measure: 1 813 of 1 884 bodies report free edges and most of the bodies are sheets; the per-body line now prints the body type so solids can be separated in the next run.
- Tolerance rescue: the 8 faces that the per-face residual check keeps all come back with one edge at 3.4 to 7.9 mm after the tolerances are reset and recomputed (the first fix attempt rejected them again, so none was kept; counted as `trim-kept-tolerance-still-high` after the reorder). Facts established:
  - A fresh edge made from the same 3D curve and the face's own pcurve reaches a tolerance of 1e-7 with `SameParameter`; the vertex gaps are below 1e-4 mm except on one edge (1.6 mm).
  - OCCT's own all-representation deviation on the shared edge was 3 to 7.5 mm while the deviation on this face was microns. The cause was a pcurve left on the edge by a face that was later rejected. 1 347 edges (1 431 pcurves) on this sample carried one. They are now removed before `SameParameter` (`stale-pcurve-edges`, `stale-pcurves-removed`; `XT_KEEP_STALE_PCURVES=1` turns it off). Counts and validity on the small samples did not change.
  - After removal the all-representation deviation equals the on-face deviation, and the tolerance still returns: the edge tolerance equals the tolerance of both its vertices, and no other edge at those vertices is high. Resetting the edge and its vertices to 1e-5 and running `UpdateTolerances` on that single face alone gives the same 3.4 to 7.9 mm again. So the value is a property of that face's own representations, not of neighbours or of the curve-to-pcurve deviation that the residual check measures. Two of the eight faces are also invalid by `BRepCheck`.
- Hypothesis, not tested: the face's pcurves do not connect in parameter space (a jump between periodic branches), which the 3D residual cannot see. The bottom-up graph spike unwrapped pcurves along each loop for this reason; applying the same unwrapping in the direct builder is the next experiment.

Verification at scale (large engine sample, standalone, with meshing, stale pcurves removed): 22 008 faces built of 22 090, 21 819 triangulated (1 785 591 triangles), 16 306 valid and 5 702 invalid by `BRepCheck`; build 592 s, meshing 168 s. 5 392 edges (5 771 pcurves) carried a stale pcurve and were cleaned; the earlier run without the cleanup gave 22 006 built, 21 803 triangulated, 16 297 valid, 660 s. The 118 solid bodies: 61 closed, 57 with free edges (15 746 free edges in total) and 78 faces missing from the solids; the 1 766 sheet bodies are open by nature and are not counted. This is the first integrity baseline for the solids.
### Closed spline rings and planar junctions (16 Nov 2026)

Branch `feature/parasolid-importer`. The pass used the local samples and the OCCT 8.0.1 APIs listed in the [topology comparison](PARASOLID_OCCT_TOPOLOGY_COMPARISON.md). No vendor reader was used.

For `XT-ed43d4c8caf1`, body 42 had single-ring mesh boundaries whose last UV segment cut across the same spline ring near the end of the U chart. Original XT face nodes 59500, 59507, 59514 and 59521 each have two closed single-edge loops. A periodic copy of the closed spline chart kept the circumference continuous while OCCT assembled the seam. The owner-authorized isolated viewer comparison reduced five mesh omissions to two on body 42. The conversion requires the same U span and sampled surface positions within the XT modelling tolerance. The remaining omissions have other boundary structures.

For `XT-097375059234`, body 756 had six planar mesh retry failures. The UV crossings are close to joins between a line and another edge, where a connector runs back across the preceding segment. Running the existing planar self-intersection repair on that body did not reduce the failures. Periodic wrapping cannot change these planar joins. The location of the visible cover defect within the model has yet to be verified.

The six source loops have a repeated junction vertex on a circle and a two-point intersection chart. For example, edge 102026 in face node 101600 misses the source junction by about 0.064 mm. Its chart declares 0.257 mm chordal error. The revised edge builder can replace a two-point chart chord with a line between its source vertices when endpoint and interior deviations are below the chart error, 0.1 mm at the default scale and five percent of edge length. The isolated body run still saw crossings in the alternate boundary mesh, but the viewer's final meshing pass omitted one face.

The latest full viewer run with both changes reported 17 B-Rep omissions and 17 mesh omissions for `XT-ed43d4c8caf1`, versus 17 and 29 before the UV and spline changes. For `XT-097375059234`, the same run reported four B-Rep omissions and eight mesh omissions, matching the earlier baseline. Visual acceptance is pending.

Isolated viewer evidence can be regenerated from the installed release with the following PowerShell command. Run each sample in its own process, set `XT_ONLY_BODY` to 42 or 756, and use a distinct filename per run.

```powershell
$env:XT_TRACE_IMPORT = '1'
$env:XT_ONLY_BODY = '42'
Start-Process -FilePath "<rebuilt-viewer-executable>" -ArgumentList '"<local-samples>/sample.x_t"' -RedirectStandardError out/parasolid-research/xt-ring-body42.stderr.log
Remove-Item Env:XT_ONLY_BODY
Remove-Item Env:XT_TRACE_IMPORT
```

### Cover trim investigation (17 Nov 2026)

Branch `feature/parasolid-importer`. The owner confirmed the visible lettering fix on `XT-097375059234`. The disk's spherical face 256268 has seven trim loops. The compact spherical chart now preserves those loops and places the pole away from the face. The adjacent housing still has distorted patches.

An XS650-only release run logged 32 verified continuous pcurves on the original revolution face 233258. Edge 237628 still failed the trim check, so the face went through the boundary filler. The source record links the edge to the revolution and a rolling-ball blend boundary. Its two-point chart declares 13.068 mm chordal error. A read-only computation using the source circle, revolution axis and the midpoint of the chart chord gives about 1.28 mm radial deviation from the revolution. This is larger than the importer's 0.5 mm trim check. Replacing that chord requires evaluating the blend contact or otherwise recovering and checking the shared curve against both supports. Moving the chart and improving projection account for the other 32 edges.

In the next XS650-only release run, the importer reconstructed edge 237628. It solved for the ball centre on the housing offset and the spine's conical surface, checked the rolling radius against the original housing and cone, and fitted the contact curve through the source vertices. The checks use sampled residuals and a bounded displacement from the original chord. The trace shows all 33 pcurves attached on housing face 233258 and no boundary fill for that face during face construction. The neighbouring blend face 237642 still used a fill. Viewer totals were four B-Rep omissions and eight mesh omissions.

Owner visual confirmation, 17 November 2026. The post-repair screenshot shows the lettering intact and the long triangular spikes gone from the previously marked housing area. This confirms the appearance of that area in the viewer. The import-wide omission counts above still apply.

### Upper housing panel recovery (17 Nov 2026)

Branch `feature/parasolid-importer`. Local viewer traces and neutral sample `XT-ed43d4c8caf1` were used with OCCT 8.0.1, identified in the [topology comparison](PARASOLID_OCCT_TOPOLOGY_COMPARISON.md). The source records were decoded read-only with the local XT layouts. No vendor importer was used.

The two largest failures in the upper housing of body 128 were original faces 617759 and 616097. Face 617759 had an edge carried as a trimmed SP-curve on an offset B-spline surface. Lifting and attaching those authored trims removed the unsupported-edge failure. Both faces still failed where the 3D samples of plane/spline intersection edges disagreed with their supports. Their measured trim gaps included 0.608, 0.927, 1.009 and 1.384 mm.

The importer now refines qualifying plane/spline intersections in the spline's chart, using the rough curve and the source vertices to select the branch. Acceptance checks retain the original endpoints, bound displacement from the source chart and verify sampled error against both supports. In the isolated body 128 viewer run, both faces passed trim validation on their original surfaces and neither appeared in the mesh-failure trace. A smaller face on the same body still failed meshing. See the isolated viewer command above with `XT_ONLY_BODY=128` and the local CBR sample.

### Closed pipe junction blend (17 Nov 2026)

Branch `feature/parasolid-importer`. The owner reported the upper housing was recovered in `XT-ed43d4c8caf1` and supplied a view of a remaining pipe junction gap. The local XT source and trace identify face 1109 on body 987 as a candidate. It is a rolling-ball blend between a plane and a B-spline support, with a closed spine and three trim loops. Previous viewer traces show the native surface was rejected and both fill variants failed.

The importer now builds a U-periodic OCCT fit for a non-nested rolling-ball blend when its sampled trim spans at least 85 percent of a periodic spine. The grid omits a duplicate end row as required by the OCCT 8.0.1 `GeomAPI_PointsToBSplineSurface` API. The ordinary trim and mesh validation still apply. In the isolated body 987 viewer run, face 1109 used the periodic fit, passed trim validation and did not appear in the mesh-retry trace. Reproduce with the isolated viewer command above and `XT_ONLY_BODY=987`. The fit uses the existing sampled rolling-ball evaluator. Visual validation in the full assembly is pending.

### Exhaust wall on body 919 (17 Nov 2026)

Branch `feature/parasolid-importer`. The owner localized the remaining exhaust gap to one part in `XT-ed43d4c8caf1`. A local source-record scan identified swept face 4891 on body 919. Its cubic section declares 125 poles and carries six additional zero-filled slots. Its knot arrays have two unused slots, and the active multiplicities sum to 129. Reading the declared pole count enabled the source surface. On the first run with that change alone, generic pcurve projection still produced a 311.697 mm endpoint residual on ring edge 6415.

The importer now qualifies a periodic copy of the closed spline section by checking its range and sampled points. For the linear extrusion it projects the edge samples onto the section flattened perpendicular to the sweep axis, keeps the section parameter continuous, and checks the resulting pcurve against the original 3D edge. Face 4891 validated and meshed into 502 triangles in the isolated body 919 run. Nearby blend faces 4918 and 4380 still used fills. Reproduce with the isolated viewer command above and `XT_ONLY_BODY=919`. Owner visual validation and a full assembly regression on this build are pending.

In the next isolated body 919 run, spline face 4050 had been reaching the mesher with a boundary crossing its chart cut and yielding no triangles. It has one single-edge ring and a multi-edge companion loop. Allowing that combination through the existing sampled-equivalence check for the periodic chart restored the face with 372 triangles. A blend on the same body still uses a fill and fails meshing. Visual comparison of the remaining gaps is pending.

The next owner image showed an isolated circular cap. Source cap 3904 shares edge 3825 with spline wall 3822. The installed diagnostic run showed that the boundary fill for 3822 stopped at Z=108.060 mm, while the cap lies at Z=165.267 mm. Moving the periodic chart origin to its middle interior knot let the original wall pass trim validation and produce 347 triangles reaching Z=165.267 mm. Faces 3830, 4050 and 4891 still meshed in the same isolated run. Blend 4900 still failed. Reproduce with the isolated viewer command above and `XT_ONLY_BODY=919`, `XT_TRACE_IMPORT=1` and `XT_TRY_SPLINE_RING_ORIGIN=1`. The origin change is experimental and awaits owner visual confirmation.

The owner supplied an isolated-body image on 17 November showing the restored wall meeting its cap. The importer now moves the seam before comparing the candidate periodic surface against the original spline. The current build uses the qualified chart by default. To repeat the isolated import, use `XT_ONLY_BODY=919` and `XT_TRACE_IMPORT=1`.

The full sample import with `XT_ONLY_BODY` unset meshed 138 bodies. Face 3822 passed trim validation on the default path. The importer reported 7 of 18,385 faces without a B-Rep and 13 further meshing omissions, including blend 4900 on body 919. Full-assembly visual confirmation is pending.

The owner supplied a full-assembly image showing a remaining discontinuity at the lower exhaust junction. The local text-record decoder links the known omission, blend 4900, to swept face 4891 and cylinder 4911. The two blend rails are a trimmed intersection and an ellipse. At the 28 stored intersection-chart points, separation from the ellipse at the corresponding angular coordinate ranges from 0.048 to 1.774 mm. This is a chart-point measurement, not a bound on the exact surface. At the other end of face 4891, blend faces 4918 and 4380 both use boundary fills. The full-assembly image alone does not identify which of these surfaces accounts for the visible opening.

The next isolated experiment used the local parasolid-kit runtime described in [provenance](PARASOLID_PROVENANCE.md) on `XT-ed43d4c8caf1`. The default spine solver for face 4900 rejected an intersection point outside its support tolerance. Interpolating its closed spine chart and sweeping the 2 mm section produced a carrier. Sampled residuals were 0.000076 mm on the 28 intersection-chart points and 0.0152 mm on 64 ellipse points. A common transverse section passed within 0.00027 mm of both authored seam endpoints. Both projected rails traversed one full period in the same direction. On 1,024 stations using linear interpolation between projected samples, the signed separation remained positive. These are sampled measurements of a reconstructed carrier, with no claim of continuous rail agreement or adjacency in the imported body.

Fitted pcurves for the two rails and built a single topological seam with two pcurves one period apart. OCCT accepted the standalone B-Rep and meshed it into 4,705 triangles at 0.02 mm linear deflection and 0.15 rad angular deflection. The finished 3D rails differ from 128 samples on the source ellipse by at most 0.01521 mm and from the 28 intersection-chart samples by at most 0.000099 mm. The prototype uses its own rail edges. Before adopting this method, qualify the fit against the existing C++ rail geometry and verify that both neighbouring faces share the recovered edges.

The native experimental path, enabled by `XT_TRY_CLIFF_RING=1`, qualifies the carrier against the importer's existing rail curves and reuses those edges. The shared body repair splits each rail where the neighbouring face crosses its own periodic cut. Independent pcurve projection on those split edges left one-period discontinuities, and the default mesh covered only part of the ring (55 triangles with X minimum −252.608 mm). Restricting the qualified continuous pcurves to the split edges' corresponding ranges restored both joins. The next isolated run produced 314 triangles with X minimum −269.600 mm. The post-repair topology trace confirms two edge segments shared with face 4891, two with face 4911 and one seam with two pcurves.

Reproduce the isolated run with the installed viewer and `XT_ONLY_BODY=919`, `XT_TRY_CLIFF_RING=1`, `XT_TRACE_IMPORT=1` and `XT_TRACE_SHARED_FACE=4900`. Remove `XT_ONLY_BODY` for the full assembly. The native path remains opt-in pending visual confirmation.

The full assembly completed meshing all 138 bodies with the experimental path enabled. Face 4900 was the only face recovered through this path, kept both shared rails, and did not enter mesh recovery. The full importer reported 12 meshing omissions versus 13 previously, and 7 B-Rep omissions in both runs. The full assembly was left open for visual inspection of the lower exhaust junction.

The owner close-ups still show a lower exhaust opening and a large triangle on the intake duct. On the latter, the isolated trace for body 116 identified face 631706's boundary fill with a 1,934.67 mm² triangle, more than five times the next largest triangle on that body. The source face had failed pcurve validation on edge 633049 with a 0.714 mm sampled gap. The local decoder and parasolid-kit OCCT runtime showed that all seven authored intersection-chart points lie on spline support 632919, but a 3D interpolant through them departs from the support by approximately 1.99 mm at a sampled interior point. The new path refits the chart on its identified spline support within a bounded chart budget, preserves the B-Rep endpoints, and samples the resulting curve against the source branch and support. The isolated duct used its original spline and meshed face 631706 into 743 triangles. The lower exhaust opening remains under investigation. Faces 4380 and 4918 are still filled on the opposite rail of swept face 4891.

In the isolated duct run, the centroid of the largest remaining triangle on face 631706 was 0.505 mm from the spline. The face retained its original carrier and went through normal trim validation. This measurement does not establish visual acceptance or full-assembly regression status. Reproduce with `XT_ONLY_BODY=116`, `XT_TRACE_IMPORT=1` and `XT_TRACE_SHARED_FACE=631706` on the installed viewer.

The owner confirmed the large triangle disappeared and marked a narrow remaining patch beside a rib. Face 633979 was the isolated body's only omitted face. Its seven-edge trim crosses the cut of its closed rational spline. Seam repair left a 0.951 mm trim residual on edge 633961. A sampled-equivalent periodic copy and a bounded chart covering the trim allowed the original source face to build and mesh. Face 633979 produced 408 triangles, and the final topology trace confirmed all seven edges are shared with adjoining faces. Face 631706 still produced 743 triangles. The isolated run reported no face omissions. Reproduce with `XT_ONLY_BODY=116`, `XT_TRACE_IMPORT=1` and `XT_TRACE_SHARED_FACE=633979`. Visual confirmation of the patch and full-assembly regression are pending.

The next owner image still showed two interrupted rib segments. An isolated audit compared the installed importer's B-Rep and triangulation with the authored topology mapped by parasolid-kit. All 530 authored edges have two fin uses. The reconstructed body had 84 distinct nondegenerate, nonseam edges incident to only one face, despite having a mesh for every face. An offline OCCT sewing trial at 0.001 mm reported 14 free edges and an invalid resulting shape. Keeping the original imported B-Rep and using 0.03 mm absolute deflection produced 100,984 triangles versus 72,759 at default settings. With coordinates rounded to 0.001 mm, the single-use mesh edge count rose from 425 to 909. These counts diagnose gaps and mismatched edge discretizations, without assigning the screenshot's individual defects to specific faces. The fine tessellation run is a diagnostic only.

For local reproduction, build the installed viewer and launch the sample with `XT_ONLY_BODY=116`, `XT_TRACE_IMPORT=1` and `XT_TRACE_BODY_BREP` set to an absolute output path under `out/parasolid-research`. Repeat with `XT_TRACE_ABSOLUTE_DEFLECTION=0.03` for the comparison. The ignored `xt-duct-dump-mesh.py`, `xt-mesh-incidence.py`, `xt-duct-brep-incidence.py` and `xt-duct-sew-probe.py.mesh.py` retain the measurement methods. The owner-supplied AI transcript is background for the existing clipping/capping requirement. The counts above come from the decoder and OCCT output.

A shared-edge trial rejected all attempted fallback fills. Sampled same-parameter tolerances for several fitted carriers were 0.17 to 2.78 mm, above the 0.02 mm qualification budget. The experiment was removed. The source records for edge 637512, common to blend faces 633369 and 633331, show a constant longitudinal parameter and a full traversal of the circular section on both fins. A sample of spine 633392 is 2.50000012 and 2.50000016 mm from the two endpoints, consistent with the declared 2.5 mm radius. The experimental `XT_TRY_BLEND_SECTION` path uses matching authored fin ranges and a unique qualified spine sample to build a bounded circular arc for this kind of otherwise unsupported edge.

The native experiment recovered edge 637512. Both source blends then reached trim validation, but face 633369 was rejected on edge 633408 (0.548 mm sampled residual) and face 633331 on edge 633308 (0.802 mm). Both still used boundary fills, with 84 free nondegenerate B-Rep edges and 425 unpaired tessellation edges for the body. A trial with the existing plane/spline edge refitter applied to the spines made no improvement and was removed.

For comparison, the isolated parasolid-kit 0.3.17 run used its normal plane/offset-spline intersection refinement and canal construction on body 116. It meshed both blends (1,015 and 171 triangles) on their reconstructed carriers. Its partial B-Rep kept 181 of 182 faces and had 15 free nondegenerate B-Rep edges. The dropped face was 633077, whose fin-to-edge deviation exceeded the conversion limit. This establishes a reference for the two blend surfaces, without showing that they explain both marked ribs or that the resulting duct can be capped correctly. Reproduce with the setup in [provenance](PARASOLID_PROVENANCE.md) and the isolated driver `out/parasolid-research/parasolid-kit-duct.py` using the same three arguments as the exhaust driver.

An experimental native open-canals path (`XT_TRY_OPEN_BLEND_PIPE=1`, with `XT_TRY_BLEND_SECTION=1`) uses the identified offset-plane/offset-spline intersection for the spine, then builds a circular sweep and checks the sampled source rails before handing the face to normal trim validation. The isolated run retained native canal faces 633009 and 633369, producing 373 and 345 triangles respectively. All 182 duct faces meshed. The audit found 66 free nondegenerate B-Rep edges versus 84 in the baseline, and 311 unpaired triangle edges versus 425 at 0.001 mm rounding. These results do not establish that either marked visual defect is resolved. A screenshot-to-mesh alignment was too approximate to identify the exact marked faces.

The next isolated run also restored face 633331. Its offset is over a spline with a C0 knot, so the experiment allows OCCT to intersect the offset's smooth spans locally and still qualifies the resulting spine and pipe against the source rails. Face 633331 produced 108 triangles, and all 182 faces meshed. Free B-Rep edges fell to 60 and the rounded-coordinate mesh incidence check found 311 unpaired triangle edges. The native pipe path is opt-in. The isolated duct remains open for visual validation, and no full-assembly regression was run.

The updated marked image was aligned against the model silhouette at an estimated 0.956 low-resolution intersection-over-union. Sampling the fitted view against the native mesh and the parasolid-kit reference located the visible depth discrepancies at source faces 636768 and 636640. In the earlier native B-Rep, both shared the same geometry as their reference counterparts but restricted different portions of their doubly closed spline charts. One edge on face 636768 lay on the (V=0) copy while its neighbours met it on (V=1); the face area was 142.45 mm² versus 155.47 mm² for the reference. Restricting the sampled-equivalent periodic carrier to a patch that keeps both cuts outside the outer loop brought the largest positive measured depth differences at the two circled profiles from approximately 2.43 and 4.93 mm to 0.14 and 0.11 mm. The broad trial omitted a different source face, 633337, so selecting the doubly closed chart is currently scoped to requested source faces through `XT_TRY_DUAL_CHART_FACES` during validation. These are approximate view-space samples, not continuous geometry bounds.

The final isolated run used `XT_TRY_DUAL_CHART_FACES=636768,636640`, `XT_TRY_OPEN_BLEND_PIPE=1`, and `XT_TRY_BLEND_SECTION=1` with the same body filter and trace commands. Extending the qualified spine intersection to the offset of a swept surface recovered face 633337 as a canal blend with 411 triangles, so all 182 faces meshed. The view-space depth measurements for the two marked regions were unchanged from the dual-chart trial. The body has 50 free B-Rep edges and 265 unpaired triangle edges at 0.001 mm rounding, compared with 84 and 425 in the baseline. The owner visually confirmed that the exterior rib and small hole were fixed. Interior folds and facets remain in the isolated view. Full-assembly regression remains pending. See the [duct handoff](PARASOLID_DUCT_HANDOFF.md) for the evidence and reproduction commands.

```powershell
$outDir = 'out/parasolid-research'
& "$outDir/parasolid-kit-venv/Scripts/python.exe" "$outDir/parasolid-kit-seam-probe.py" $sample $catalogDir (Resolve-Path $outDir).Path *> "$outDir/parasolid-kit-seam-probe.log"
```


## Per-face outcome table and forced SameParameter (2026-11-17)

Observation: `parasolid_xt_shapes --mesh --face-table <out.tsv>` writes one row per XT face with the surface, how the face was built (native, fill, rescued, broken->fill, omitted), the builder counters raised, the BRepCheck result with its status codes, and the triangle count. On XS650 and the CBR assembly, almost every natively built blend (`BLENDED_EDGE`) face and about half of the spline and swept faces failed BRepCheck with edge codes 8 (InvalidCurveOnSurface) and 11 (InvalidSameParameterFlag) plus face code 27 (NotClosed). Plane and cylinder faces were almost always valid.

Hypothesis, supported by the measurement below: the builder flags edges SameParameter itself, and the non-forced `BRepLib::SameParameter` call skips flagged edges, so the real pcurve-to-3D deviation was never measured and the edge tolerances stayed too small. The final call now forces the measurement (`XT_NO_FORCE_SAMEPARAM` restores the old behaviour).

Measurement (standalone tool, same code otherwise; unvalidated visually):

| Sample | Invalid faces before | After | Meshed faces before | After | Build time |
|---|---|---|---|---|---|
| XS650 | 1,009 | 70 | 9,218 | 9,219 | 26 s |
| CBR assembly | 2,726 | 334 | 18,363 | 18,362 | 105 s |

Edges rescued with a high tolerance fell from 87 to 44 (XS650) and 32 to 18 (CBR). About 30 faces went from valid to invalid, mostly `face27,wire18` on cylinders and `edgeOnFace11` on blends. The largest remaining class is face code 27 on spline and sphere faces, concentrated in a few bodies (CBR body 229613 holds 132). `BRepCheck_Analyzer` faults inside OCCT on one CBR body (46646, a B-spline face with edge tolerances near 0.27 mm); the tool now guards its checks against such faults and reports `check-crashed`. The viewer does not run BRepCheck. The face-table `XT_ONLY_BODY` environment variable limits the build to one body: the viewer imports only that body when it is set, which explains a partial CBR load that was seen. The importer now warns when it is set.

Regenerate:

```powershell
$env:PATH = "$PWD\out\build\ninja_release_vcpkg\vcpkg_installed\x64-windows\bin;" + $env:PATH
& out\parasolid-tests\parasolid_xt_shapes.exe out\parasolid-research\layouts-v13.txt $sample --mesh --face-table out\parasolid-research\scratch\ft.tsv
```

## STEP reference comparison on the engine sample (2026-11-17)

Method: `parasolid_step_summary` (new, `tests/parasolid/step_summary_cli.cpp`) reads the same-model SolidWorks STEP export of the engine sample (241 solids, 23,955 faces) and writes one row per face and solid. `tools/parasolid/compare_face_tables.py` and the XT `--face-table` output (now with area, surface type and per-face free-edge columns) are compared on transform-invariant quantities only: total area, face count and per-face area by surface type. STEP splits closed faces, so its face counts are higher than XT's. Observations only: a mismatch says the two differ, not which is right.

Observations:

- 127 of 138 XT bodies have a total area within 1% of some STEP solid, and the large complex bodies match on area and volume to about 0.1%. The bodies that do not match are 46646, 357, 919, 186, 41, 173, 963, 987, 35 and the duct pair 116 and 83.
- Duct pair (bodies 116 and 83): the STEP counterparts are solids 215 and 216 (253 faces, area about 98,400 mm²). XT body 116 has 182 faces and 79,546 mm², about 19,000 mm² less. STEP has two skin faces of 19,518 and 18,595 mm². XT has one skin face of 18,614 mm² (node 631706, one loop, a non-periodic B-spline surface whose trim covers its whole parameter domain) and none of the 19,518 mm² size. The 56 rib-window faces on one shared surface sum to 35,067 mm² in XT. The two end-cap planes are 11% smaller than their STEP counterparts. The body has one shell of 182 faces in the XT file. Whether the second skin is absent from the XT file or is built wrongly is not yet determined.
- XT body 116 alone has 84 face-level free edges (143 per the body check) and nine faces built by fill with `pcurve-mismatch`.

Regenerate (under `out/`): run `parasolid_step_summary <file.step> out/parasolid-research/scratch/step.tsv`, run `parasolid_xt_shapes` with `--mesh --face-table out/parasolid-research/scratch/ft.tsv` (stdout to a log), then `python tools/parasolid/compare_face_tables.py --xt-tsv ... --xt-log ... --step-tsv ... --out ...`. `XT_DUMP_FACE=<face node>` (with optional `XT_DUMP_SURFACE=<surface node>`) prints the shell and shared-surface structure; `XT_DUMP_FACE_TRIM=<face node>` prints the surface periodicity, the wire in parameter space and the neighbouring faces.

## Assembly tree and names (2026-11-17)

Observation: the XT import showed a flat list named "Body N", while the JT import of the same gearbox showed the assembly tree. The XT file carries both: `SDL/TYSA_UNAME` (Unicode, full) and `SDL/TYSA_NAME` (ASCII, can be cut short, for example `PinionSubass`) attributes on BODY and ASSEMBLY nodes, and the INSTANCE nodes of the assembly graph.

Change (not yet checked in the viewer): the builder reads the name attributes (Unicode first), and each placed part records its chain of containing assemblies (`BuiltInstance::path`, keyed by the INSTANCE node so two occurrences of one assembly stay separate). The scene importer regroups the converter's flat root children under identity-transform group nodes named after those assemblies. An unnamed top-level assembly adds no level. Unnamed nodes keep "Body N" or "Assembly N".

Standalone check with `XT_PRINT_TREE=1` on the gearbox sample: GearSubassembly (two SKF_6007 bearing assemblies, Gear, GearShaft), PinionSubassembly (two SKF_6305 bearing assemblies, Pinion) and Housing, matching the JT tree. XS650 shows its real part names. Placement and the viewer tree are not yet confirmed by the owner (gate G6).

## Colours (2026-11-17)

Observation (gearbox sample, standalone `XT_PRINT_TREE=1`): the file stores `SDL/TYSA_COLOUR` (three reals, 0-1) on FACE nodes and `SDL/TYSA_COLOUR_2` on BODY nodes. Housing and Cover are (0.00, 0.63, 0.72), the bearing covers (1.00, 0.94, 0.14), the gear and pinion (1.00, 0.95, 0.91), the shaft white. These match the teal, gold and grey of the JT rendering by eye only; no numeric comparison with the JT was made. Most parts carry one face colour; the two oil seals, the drain plug and the inspection window have two or three face colours and no body colour.

Change (not yet checked in the viewer): the builder reads both attributes (`BuiltBody::bodyColour`, `faceColours`); face colour wins over body colour; a part with no colour keeps the default grey. The scene importer keeps a one-colour body as one shape with that colour. A body with several colours is split into one face compound per colour, so each gets its own material. Known limit: such a part then appears as several leaves with the same name under its assembly instead of one multi-material leaf. `SDL/TYSA_TRANSLUCENCY` and `SDL/TYSA_REFLECTIVITY` are present on faces and are not used yet. Whether the XT colour values are sRGB or linear is not established.

## Attribute survey of four samples (2026-11-17)

Method: `parasolid_xt_shapes <layouts> <file.x_t> --attribs`, which counts attributes by identifier and owner type and prints two examples of each. Counts below are attribute instances. Example values are not full distributions.

| Attribute (owner) | CBR | XS650 | B4T | AGP |
|---|---|---|---|---|
| `SDL/TYSA_COLOUR` (face) | 18,385 | 9,249 | 22,090 | 518 |
| `SDL/TYSA_NAME` (body) | 138 | 77 | 1,775 | 28 |
| `SDL/TYSA_NAME` (assembly) | 15 | 2 | 0 | 0 |
| `SDL/TYSA_DENSITY` (body) | 10 | 1 | 1,885 | 0 |
| `SDL/TYSA_TRANSLUCENCY` (face) | 0 | 0 | 17,823 | 518 |
| `SDL/TYSA_REFLECTIVITY` (face) | 0 | 0 | 0 | 518 |
| `SDL/TYSA_HATCHING` (face) | 0 | 0 | 1,771 | 0 |

- Material: the only material-like data is `SDL/TYSA_DENSITY` on bodies, a real with a unit string (example `7830.64 Kg/Cu M` on B4T, `7850.02` on XS650, and `1000` with no unit on CBR, which looks like a default). There is no material name and no mass attribute in these files. A density gives mass only together with the volume, so a weight roll-up from XT needs a closed, valid solid.
- Translucency: every example value printed was 0. Whether any face is non-zero was not measured.
- Other attributes seen are CAD-system bookkeeping (monikers, object state, edge and face ids, hatching) and are not mapped.
