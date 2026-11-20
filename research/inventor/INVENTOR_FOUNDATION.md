# Inventor IPT working specification

19 November 2026 · `feature/inventor3d` · baseline checkpoint `81786a9` plus the verified appearance and complex-geometry changes

This is a versioned working specification for the saved data ModelViewer needs to import. Each supported field or topology rule needs a source, observed files, and a checkable interpretation. Public documentation and reference readers identify candidate meanings. Saved native data and independent exports or Inventor-generated reports qualify the implementation. This document records the observed profiles and identifies evidence gaps.

## The format layers

| Layer | Current contract | Remaining specification work |
| --- | --- | --- |
| Compound document | Find the bounded streams in the OLE Compound File directory. Pair a recognized length-prefixed UTF-16LE segment label in an `M` stream with its `B` stream. The observed data envelope has an 18-byte header and a compression selector at offset 17. | Establish version and segment boundary tables. A01 proves that Zstandard can omit the frame content size, which needs a bounded streaming decoder before IAM integration. |
| Saved ASM model | Locate the `ASM BinaryFile4` header, read the declared millimeter scale, and parse tagged records for the saved current state through its history marker. Maintain record identity and distinguish typed links, integers, real values and subtype markers. | Catalogue the observed token forms, subtype versions, reference numbering and optional fields. Record whether a nested subtype definition is a surface, 3D curve or parameter-space curve. |
| Body and topology | Follow body → lump → shell → face → loop → coedge → edge → vertex links. Validate each link's expected kind and traverse loops with cycle and incidence checks. Toleranced records extend the corresponding base record. Preserve independently saved solids. | Distinguish absent geometry, wire-only bodies, open sheets, multiple shells, and mixed documents using saved structure and independent controls. Document source closure, orientation and sense conventions before kernel conversion. |
| Geometry and parameters | Analytic supports cover the qualified planes, circles, ellipses, cylinders, cones, spheres and ring tori. Saved B-splines use scoped subtype references, explicit poles and knots, with observed clamped endpoint conventions. An edge uses its saved range, direction and, where present, tolerances. | Specify procedural helical surfaces and their edge curves, singular torus branches, remaining spline forms and stored pcurves. Verify parameters against points and trim bounds before invoking OCCT. |
| OCCT reconstruction | Construct surfaces and trims directly. Add explicit seams where required by the mapped topology and bound any repair. A valid solid is required for the qualified solid path. | Establish when source pcurves take precedence, how seams and holes are oriented, and how source tolerances map to OCCT vertices, edges and faces. Verify face coverage and geometry agreement as well as kernel validity. |
| Appearance and scene | The qualified graphics profile supplies diffuse body defaults and face overrides keyed to persistent ASM face IDs. An unreadable appearance profile falls back to the standard material. | Qualify additional versions, multibody and occurrence appearance, and the assembly occurrence model separately. |

For each record field added to the implementation, retain the tagged type, enclosing record or subtype, source units, range or default, orientation semantics, sample IDs, and an observation that would falsify the interpretation. For derived geometry, retain the rule that maps the authored definition to an OCCT support and wire. Compare exact data where available and document the limits of sampled evidence.

## Corpus baseline

The audited inputs are the supplied lathe and pump IPT packages, excluding `OldVersions`. The audit counts a part as reconstructed when the native probe completes with a valid OCCT solid. Successful corpus entries still need geometry evidence for broader compatibility claims. The following are first-failure counts. They do not measure the full number of gaps in each file.

| Package | IPT files | Reconstructed | Rejected | First-failure groups |
| --- | ---: | ---: | ---: | --- |
| AX | 16 | 2 | 14 | 8 spline payload, 5 solid validation, 1 body profile |
| VP | 23 | 7 | 16 | 8 solid validation, 2 body profile, 2 edge construction, 2 torus profile, 1 record layout, 1 other |
| Total | 39 | 9 | 30 | No timeouts during this run |

Earlier owner-approved samples I01–I05 and G01, G02 and G07 remain positive controls. The two production samples with paired STEP references check that a valid OCCT solid also matches the authored geometry. G07 additionally has matched saved graphics envelopes and owner visual acceptance. [Analytic controls](INVENTOR_ANALYTIC_GEOMETRY.md) · [Complex geometry controls](INVENTOR_COMPLEX_GEOMETRY.md)

## Reported failures and qualification targets

| ID | SHA-256 | Observed first failure | Required evidence for support |
| --- | --- | --- | --- |
| G08 | `b1b89be345a8c083acb9780ced3267913c4d13e418aa65dec1626eab655260e5` | Face 83 refers to a `helix_spl_line` with a procedural definition and `nullbs` fields. | Derive the parameter map, radial frame, pitch and handedness. Match saved boundary endpoints and pcurves, then compare thread geometry with an authored reference. |
| G09 | `166a7c518d3eb636be6a08c880d74fb3a35b705dcfe99cad2ae43f950f5fa78b` | Six plane and cone-family faces decode. OCCT rejects pcurves, orientation and self-intersection. | Isolate the invalid wires, recover the intended periodic parameter chart and verify each boundary and trimmed face. |
| G10 | `4028f1b3054f6d78f776376b370499c9dc7a129c65f0b6dbd442c1f917d358ce` | The 26 plane and cone-family faces reach OCCT. The checker reports bad subshape orientation. | Establish face, wire and coedge sense and reproduce the mismatch on the smallest affected boundary. |
| G11 | `40c26fb25501813986c80dcece3b998fc7ef6ded98b3e9e55a56e29f4d246391` | Face 2859 also refers to procedural `helix_spl_line` data. The file includes 248 pcurve records. | Use this larger case to cross-check G08's helical semantics and saved pcurve mapping. |
| G12 | `f91baaeb1d537306366cd63353f4f2722a537c1a98de49e5f03b8784c974f82e` | The saved current ASM segment contains zero body and face records. | Determine whether the authored document is empty, graphics-only or dependent on another reference. Report the established case explicitly. |
| G13 | `e66e8b7b61c5cc7c8d5e70457d5cccbb4bf6bd644d1af7babe2bbad41a01646b` | Face 12 has major radius 47 mm and minor radius 71 mm. The ring-torus path rejects this spindle profile. | Establish the retained sheet of the spindle, the parameter branch and its trimmed boundary. Check shape and orientation against an independent export. |
| G14 | `d4f1d1a387825a83eb55ff0a85a8f74810b2d2e31b0bdeafe7155c8c6ca4ae19` | Face 2358 references an `exact_spl_sur` definition nested inside a pcurve record. The rational header includes the string `both` before the form fields. The current numeric-header reader rejects it. | Qualify the optional rationality field and per-axis pole/weight rules. Compare the 3D surface and the authored pcurve trims. |
| G15 | `3e4245b4d60ade026d6f4efd8b4e877b76b6f27ff7ffd0bbfe273295f429c5fe` | Face 4's circular edge 26 passes the current endpoint precheck. Its start vertex is about `1.176e-7` mm from the saved curve evaluated at the saved parameter. OCCT returns `BRepBuilderAPI_DifferentsPointAndParameter` during edge construction. | Reconcile the saved vertex, curve, analytic precision and spatial-resolution tolerances in one tested rule. |

Seven AX spline-payload failures trace to `helix_spl_line` on the first failing face. The eighth reaches `helix_int_cur` on a cylindrical face's boundary. That makes procedural helices a recurring gap across all eight. G09 and G10 give small trim and orientation controls for the most common overall failure group.

## Trim and endpoint checkpoint

The native reader now tries the existing bounded OCCT repair on a closed shell made entirely of plane and cone-family faces when validation fails. The repair disables natural-bound insertion, face splitting and loop rewrites. It retains the solid, shell and face counts and requires final OCCT validation. This qualifies the observed repair path for the controls below. It leaves the general saved pcurve and winding specification open.

For a saved edge, validate each vertex against the corresponding saved curve parameter before constructing the OCCT edge. If the accepted residual exceeds the current vertex tolerance, carry that residual plus a numerical margin into the vertex tolerance, capped by the accepted edge tolerance. Keep the vertex coordinate, curve and parameter unchanged. G15's circular edge supplies the small-residual case that OCCT previously rejected.

| Control | Validated and tessellated faces | Extents in mm | Probe volume in mm³ |
| --- | ---: | --- | ---: |
| G09 | 6 of 6 | 11.50 × 10.00 × 11.50 | 707.6452 |
| G10 | 26 of 26 | 11.70 × 12.00 × 54.00 | 1,675.5264 |
| G15 | 22 of 22 | 270.01 × 48.00 × 180.00 | 147,065.4703 |

The instrumented probe matched every reconstructed face to a unique persistent key in the document's saved graphics. The largest excursion outside a corresponding saved face envelope was below 0.000001 mm for each control. The checks measure envelopes and tessellation coverage. No paired export was supplied for these three controls, so exact trim and volume comparisons await independent reference data. Owner UI verification is pending.

The post-change corpus has 24 valid solids out of 39, up from 9. AX went from 2 to 7 of 16 and VP went from 7 to 17 of 23. All nine baseline successes retained their measured counts, extents and volumes. I01–I05 and the G01 and G02 geometry and color counts also passed the standalone probe. The other newly reconstructed parts have solid validation only. Remaining first failures are eight spline payloads, three body profiles, two torus profiles, one record layout and one other failure.

All nine tests in the existing repository CTest build passed in 170.77 seconds. The rebuilt standalone probe supplied the Inventor-specific coverage above. Run `ctest --test-dir out/inventor-research/ctest-build --output-on-failure` with the Qt and OCCT runtime directory on `PATH`.

Repeat the [baseline audit command](#reproduce-the-baseline) with the rebuilt probe and direct its output to `out/inventor-research/foundation/trim-corpus-audit.json`. Join the two reports on `sha256` and compare `status` and `geometry`. For an individual part use `& $probe --part-solid $input`. The following reproduces the supplementary check using the local instrumented probe and saved graphics cross-check script already present under ignored `out/`. Set `$id` and `$input` to a control ID and its original IPT. Use the pinned Python dependencies from the local research setup.

```powershell
$local = 'out/inventor-research'
$env:INV_ZSTD = "$qt/bin/zstd.dll"
$env:PYTHONPATH = "$(Resolve-Path "$local/python");$(Resolve-Path $local);$(Resolve-Path tools/inventor)"
$log = "$local/foundation/$id-trim-final.log"
& "$local/gearbox-check/build/inventor_compound_probe.exe" --part-solid $input > "$local/foundation/$id-trim-final.json" 2> $log
Select-String -LiteralPath $log -Pattern '^MESH '
& $python -B "$local/foundation/verify_trim_graphics.py" $input $env:INV_ZSTD $log
```

## Implementation and verification order

1. Expand the record and subtype tables from the failing cases. Add field-aware diagnostics that identify the offending record, field, expected kind and observed kind. Version the evidence alongside the tables.
2. Establish direct geometry and topology invariants. Start with saved pcurves, winding and orientation on G09–G10, and the circular endpoint tolerance on G15. Preserve the previous positive controls and check that all intended faces tessellate.
3. Specify and qualify the geometry families found repeatedly. Use G08 and G11 for procedural helices, G14 for the rational spline layout, and G13 for the spindle torus. Keep separate geometry evidence for each so that a repair is measured against the source instead of masking a missing representation.
4. Classify G12 and the multi-shell cases from source records and companion evidence. Keep empty, unsupported, invalid and unresolved-reference cases distinct in the probe and UI diagnostics.
5. Rerun the same corpus after each phase. Report improvements by failure group and sample hash. A UI checkpoint needs a repaired source file, a clean headless check and a staged viewer. Resume IAM UI integration after the IPT blockers relevant to its selected components are understood.

## Reproduce the baseline

From an x64 Visual Studio developer shell, substitute the two package directories. This writes only under ignored `out/`.

```powershell
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
New-Item -ItemType Directory -Force out/inventor-research/foundation | Out-Null
cmake --build out/inventor-research/compound-build
& $python -B tools/inventor/audit_ipt_corpus.py --probe $probe --folder AX '<local-lathe-package>' `
    --folder VP '<local-pump-package>' > out/inventor-research/foundation/corpus-audit.json
```

Set `$python` as in the [container probe instructions](INVENTOR_IMPORT_FEASIBILITY.md#native-compound-file-reader-and-owner-verification). The read-only [ASM profile command](INVENTOR_ANALYTIC_GEOMETRY.md#reproduce-the-read-only-observations) measures record and incidence counts for individual sample IDs. The local detailed fault evidence is in `out/inventor-research/foundation/reported-cases.json`, with diagnostic probe runs in `G14-field-diagnostic.log` and `G15-edge-diagnostic.log`.

## Provenance

This pass used the owner's local native bytes, the public MS-CFB specification, the installed OCCT 8.0.1 APIs, `Brep.cs`, `Step.cs`, `Nodes.cs` and `Assemblies.cs` from CAD Convert at `a3cbe9f5f41acff810b5006d964d116e805e042b`, and `Acis.py` from InventorLoader at `e94bdf5e29052a0dc7ce6fdf755e956ae507caec`. The helical field meanings need independent qualification against native files and authored comparison data. The production reader continues to build directly from saved IPT. [Pinned CAD Convert source](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b/tools/invre) · [Pinned InventorLoader source](https://github.com/jmplonka/InventorLoader/tree/e94bdf5e29052a0dc7ce6fdf755e956ae507caec)

## Procedural helix observations (G08), 19 November 2026

Method: read-only inspection of the saved ASM records of G08 (SHA-256 above), summarised under `out/inventor-research/foundation/G08-records.json`. These are observations and hypotheses, not accepted results.

Observed `helix_int_cur` layout, after the `22800` token: a parameter interval, then five vectors and a scalar: centre `C`, major `M`, minor `N`, pitch vector `P`, a scalar (0 in every observed record) and the axis `A`, followed by two `null_surface` and two `nullbs` tokens. Observed values are consistent with `X(u) = C + M cos u + N sin u + P u / 2π`: `|M| = |N|` is the helix radius (0.34 to 0.40 here), `P` is parallel to `A` with `|P|` the pitch (0.125 in the saved length unit) and the interval spans 32π, i.e. 16 turns. Records with the axis reversed store `-P`, `-A` and `-M`/`N` sign changes, so handedness must come from the stored vectors, not from the axis alone.

Observed `helix_spl_line` surface layout: the same helix block preceded by two intervals (the first, 0 to 0.109375, matches the saved v range) and followed by a 3-vector "origin" and the surface bounds. Two surfaces were compared against their bounding helical edges and the saved `exp_par_cur` pcurves (u equals the edge parameter, v is constant along each edge). The edges lie at radial offset ±0.866 v and axial offset 0.5 v from the surface's spine helix, i.e. a thread flank whose generator is a unit line in the axis-radial plane at 30° from the radial direction, rotating with u. The sign of the radial component follows the sign of the stored origin x component (−2.165 gives inward, +2.837 gives outward) and the axial component matches the negated origin z (−0.5).

Open hypothesis: the generator direction is not stored as a vector but derived from the origin point. No formula reproducing 0.866/0.5 from the stored origin has been found yet. Until it is qualified, helical surfaces cannot be reconstructed and G08, G11 and the other AX files continue to fail on first helical record. No production code was changed for this and nothing was built.

## Banded revolved faces, spindle torus and wire orientation, 20 November 2026

Method: headless probe on the 84-file local corpus under the owner's sample folder (neutral IDs only here). Output regenerates with the ignored `out/inventor-research/run_corpus.ps1`, which writes `out/inventor-research/foundation/corpus_latest.txt`. Observations first, then the changes they justified.

Observations.
- A torus whose minor radius exceeds its major radius (a spindle torus, minor 6 vs major 3.5 in one cap part, 71–73 vs 47 in the G13 flange) is stored like any other torus. OCCT accepts it. A boundary circle on the far side of the axis has a negative signed distance, so its tube angle must be chosen from the two candidates by the tube equation.
- Cones whose two boundary loops are full circles (screw tips, chamfers, thread flanks) fell into the generic path, where `ShapeFix_Face` produced an unbounded natural bound and lost the pcurves. Two failing fasteners and a valve body shared this.
- A circle can be saved as a ring of two or four arcs rather than one closed edge (valve body, G13 flange). Treating a ring of arcs that closes one circle as a circular band, and letting the sewer cut the band circle at the arc vertices, builds valid faces.
- For non-planar faces whose saved face sense differs from the surface normal, the saved loops run clockwise about the surface normal. Leaving the orientation to `ShapeFix_Face` produced `BadOrientationOfSubshape` for wires spanning most of a periodic parameter. Orienting the wires up front removed that error.
- Valid is not correct. The valve body had validated with a 180 × 227 × 180 mm box and 12.46 L before the orientation change and validates with 132 × 124 × 132 mm and 0.245 L after; the saved torus radii (63 mm) support the latter. A pump-shaft assembly part changed by −0.7 % in volume when either the ring or the orientation change was enabled alone, which points to one previously mis-oriented face. Neither has paired reference data. Treat both as owner-check items, not as qualified.
- Exact spline surfaces nested in a pcurve carry one extra text token (`both`) between the two degrees and the form fields. Skipping a text token there lets the existing grid reader decode them (two fasteners).
- Saved vertex tolerance −1 (file units) marks "none stored"; intersection-curve tolerances up to 0.0136 mm occur in a standard nut.

Results. Valid solids rose from 47 to 53 of 84 (counted by the probe, which includes template and skeleton files with no solid). All eleven earlier controls re-measure identical except a 0.13 mm³ change on the pump (1.2 L) and 0.007 mm³ on the wheel assembly part, both from exact cone bands replacing approximated ones. Paired STEP comparisons were not re-run in this pass.

Still failing and not understood: G13's toroidal fillets bounded by one circle and one arc-plus-intersection-curve loop (a band that winds in U with a notch, which needs a seam), helical surfaces (previous section), tangent-contact non-manifold edges, two parts whose faces do not close into one shell, and sheet geometry.

## Failure classes examined and left open, 20 November 2026

Observations only; no production change came from this pass.
- Two parts fail with three free edges after sewing: one closed spline edge plus two pieces of it cut at a point. Every saved edge is paired (both coedges present), the shared edge joins two cone faces, and each cone face has one closed loop of that edge. The sewer cuts the closed edge in one face and not the other. Retrying without cutting leaves 24 free edges; building one shared edge object per saved edge did not change the result. A cone face bounded by a single closed intersection curve needs a seam and an apex treatment that `ShapeFix_Face` places inconsistently. Not solved.
- One sphere face carries an outer loop made of two short circle arcs and short intersection curves, plus two full circles (bore openings 60° apart in latitude). The loops do not nest as an OCCT face expects (`InvalidImbricationOfWires`). Enabling the natural-bound repair changed nothing. The face region (patch or complement) is not established.
- One older part has no `ASM BinaryFile4` marker in its B-rep stream at all (the probe reports no header offset), so it is a different saved layout or version, not a failure of the reader's trim logic. A paired STEP exists for it.
- Valid solids stay at 53 of 84 on the probe; the committed code is unchanged by these experiments.

## Helical surfaces and winding boundaries, 20 November 2026

Method: headless probe on the local 84-file corpus, plus a read-only record dump of the AX parts. Observations, then the change they justified.

Observed.
- The `helix_spl_line` surface and `helix_int_cur` curve appear either inline or as a block that other records reference by index (`ref N`); the reader must follow the reference before looking for the helix name.
- The surface generator is a unit vector `d(u) = a·M(u) + b·N(u) + c·A`, with `M(u) = M cos u + N sin u`, `N(u) = −M sin u + N cos u`, `(a, b, c)` the stored three-vector after the helix block and `A` the stored axis. The coefficient `a` is the radial component divided by the helix radius (−2.165 × 0.40 = −0.866); `c` is the component along `A` (−0.5). So the flank is a 30° line from the radial direction, turning with the helix. `S(u, v) = X(u) + v·d(u)`, with v in the saved length unit.
- Evidence: the stored straight edge at u = 0 has direction (0, −0.866, 0.5), equal to `d(0)`; the second helix edge of the same face has radius 0.3375 against the spine radius 0.40, i.e. offset 0.0625 = 0.866 × 0.0722 at the face's maximum v, and axial offset 0.0361 = 0.5 × 0.0722; neighbouring edges then lie on the reconstructed surface within OCCT's check tolerance for every thread face that now validates.
- Reconstructed threaded parts come out with round dimensions (a stud of 10.000 × 10.000 × 80.000 mm and a handle of 8.000 × 8.000 × 76.500 mm), which supports the radial scale.
- The reconstructed ruled surface needed the opposite sense flag to the saved one to validate. The reason is not established; it held for every helical face in the corpus.
- A face bounded by two loops that wind once around a periodic surface (a band whose edges include intersection curves), or by one loop around a pole or apex, has no seam in the saved data. `ShapeFix_Face` discarded the pcurves of these faces. Building the seam explicitly from the boundary vertices, as a straight parameter-space segment that crosses no boundary, produced valid faces; the segment may have to end at a vertex in a neighbouring period because the boundary sags past a straight seam.
- Between two parallels of a surface that is not periodic across them, the face can only be the band, whatever sense the loops carry, so the lower loop is oriented to travel in the positive direction.
- Pcurves of neighbouring edges meet only to about 1e-4, so winding and seam-crossing tests need a tolerance near 1e-3.
- Planar faces with several loops also need their wires oriented for the plane; leaving it to `ShapeFix_Face` produced `BadOrientationOfSubshape`.

Result. Helices are built by cubic Hermite interpolation of the analytic point and derivative, 48 samples per turn (a few 1e-6 mm for these radii), as a ruled B-spline surface between the two bounding rails. Valid solids on the probe: 65 of 84 (the other 19 include 10 files with no solid body). Eleven earlier controls re-measure within 1e-6 relative (largest: 0.17 mm³ in 1.2 L, from the seam change). Paired STEP comparisons and CTest were not re-run. The helical geometry has no independent reference beyond the dimension and edge-on-surface evidence above; an owner visual check is required.

Still failing: two non-manifold tangent-contact parts, two with wire geometry, one sheet part, one older-format part, one sphere face with an unclear region, one planar face with an edge off its plane, and one oversized assembly file.

## Zero-width spikes in planar boundaries, 20 November 2026

Method: headless probe on the local corpus plus a read-only record dump of the two failing parts. Observation, then the change it justified.

Observed.
- The two parts rejected as "non-manifold edge at tangent contact" are not cone or tangent-cylinder cases. In each, one straight edge has four coedge uses: the helical thread face, the planar thread run-out face, and a third planar face that uses the edge twice, consecutively and in opposite senses, inside a longer loop (for example ellipse arc, edge, same edge reversed, ellipse arc).
- The doubled uses enclose no area: the loop leaves a vertex along the edge and comes straight back. The two other uses already form a normal manifold pair.
- The earlier tangent-contact logic only recognised a retraced pair that is a whole two-coedge loop, so these were rejected.

Change. A retraced pair inside a longer loop of a planar face is treated as a spike: both coedges are left out of the boundary and the edge keeps its two real uses. Both parts then sew and validate (dimensions 75 × 53 × 165 mm and 370.5 × 30 × 30 mm). Valid solids on the probe: 67 of 84; every other file's volume is unchanged.

Limits. The spike must be on a planar face and the loop must remain connected through the skipped pair. There is no paired STEP for either part, so the owner's visual check is the qualification.

Correction. The note "Failure classes examined" above attributed the three-free-edge sewing failure to a cone face with a single closed loop. That attribution was not confirmed for these two parts and the cone hypothesis remains unverified; no corpus part is currently known to show it.

## Owner-reported visual discrepancies on fillet surfaces, 20 November 2026

Observation by the owner in the viewer (screenshot kept locally, not tracked), on the G13 flange part: along the rim fillet where the grille ring meets the outer wall, several local patches show shading steps or small dents, at four places around the circumference, while the rest of the fillet is smooth. The same kind of discrepancy was reported for fillet surfaces on some other meshes.

Status: observation only. No cause is established. Hypotheses to test, not conclusions: (a) the fillet is a spline surface whose Hermite or sampled approximation is coarser than the analytic one; (b) the tessellation tolerance is too coarse where the fillet is narrow; (c) normals are interpolated across a seam or a face boundary; (d) a face is built from a complement or patch with a slightly different surface. Next step is to identify which saved face types cover those patches and compare the viewer tessellation against the paired STEP, which exists for this part, with output under `out/`.

## Multiple lumps, void shells, tolerant vertices and a full torus, 20 November 2026

Method: headless probe on the local corpus plus read-only record dumps of the failing parts. Observations, then the changes they justified. Valid solids on the probe rose from 67 to 71 of 84; no other file's volume changed.

Observed and changed.
- One part has one body with two lumps, each a closed shell of 56 and 54 faces. Each lump is a separate solid, so the reader now builds one solid per lump (the two come out as a mirrored pair of about 3275 and 3273 mm³).
- One part has one lump with two shells: a 92-face shell and a 5-face shell. The smaller shell is an internal void. A lump with several shells is now built as one solid: the shell with the largest volume is the exterior, every other shell must lie inside it (checked with a boolean cut), and the inner shells are added reversed. The result validates (25 × 24.7 × 70 mm).
- One part's planar face has an edge about 2e-4 mm off its plane. The edge ends at tolerant vertices whose saved tolerance is 2e-4 mm (2e-5 file units). The planar check used a fixed 1e-5 mm; it now allows the edge's own tolerance.
- The same part has a vertex whose tolerance field holds -1 while the next field holds 2.14e-4 file units; the measured gap between that vertex and its curve is 2.1e-3 mm, equal to that value. When the first field is -1 and the next is positive, the next field is now read as the vertex tolerance. This is an inference from one file's data, supported by the matching gap; it is not confirmed by a format document.
- One part is a single face with no loops or edges on a full torus (3 mm major, 1 mm minor radius). A loop-less face on a torus or sphere is now built with the surface's natural bounds, and a single self-closing face sewn alone is wrapped into a shell. The volume is 59.2176 mm³, equal to the analytic 2π²Rr² for these radii.

Limits. The part with a void shell and the part with two lumps have no independent reference; dimensions are round and the solids validate, and the owner's visual check is the qualification. The vertex-tolerance inference rests on one file.

Still failing: one sphere face with an unclear region, one older-format part, and the oversized assembly file (plus the ten files with no solid body).

## Sphere face with a notch and bore holes, 20 November 2026

Method: read-only record dump and a temporary orientation diagnostic in the probe build (removed). Observations, then the change.

Observed.
- The part is a ball (radius 10 mm) with a through-bore of radius 5 mm along one axis and a notch between two parallel planes 4 mm apart, with a cylindrical floor. The sphere face has three loops: the notch boundary (two circle arcs and four intersection-curve edges) and the two bore circles. The face is the whole sphere minus those three regions.
- All three loops are clockwise about the outward normal, so each is a hole. There is no counter-clockwise outer loop; the outer boundary is the whole sphere and is not saved.
- The notch loop straddles the saved seam meridian of the sphere's parameterisation. The earlier report that the region was unclear was this: with no outer loop and a seam crossing, `ShapeFix_Face` neither added the natural bound nor kept the notch loop in one piece.
- My first diagnostic reported one bore circle as counter-clockwise; that was the diagnostic's own reading of a reversed single-edge wire (the wire explorer's orientation), not the data. The saved coedge senses are consistent: the two circles carry opposite senses on the sphere and on the bore wall, as a manifold requires.

Change. For a sphere face with two or more loops, the sphere is turned about its axis so the seam runs through the widest gap between boundary samples. If every loop is then clockwise in parameter space, the whole-sphere natural bound is added as the outer boundary before `ShapeFix_Face`. The face validates and the solid is 20 × 20 × 20 mm, 2651.92 mm³ (5 faces). Valid solids on the probe: 72 of 84. One other part with a sphere face (a valve) re-measures identical to 1e-14 relative.

Limits. The volume has no independent reference; the check is validity plus the round dimensions, so the owner's visual check qualifies it. The loop-direction test assumes the parameter-space sign convention of the sphere used here.

Still failing: one older-format part and the oversized assembly file (plus ten files with no solid body).

Owner observation on the ball part, 20 November 2026: the sphere is correct but its shading is visibly faceted. Recorded with the fillet report above for the refinement phase (tessellation tolerance and normals), not investigated.

## Older saved layout: "ACIS BinaryFile", 20 November 2026

Method: stream listing and decompression of the B-rep segment of the one part the reader rejected with "Expected a single saved ASM model", then a probe run and a comparison with the part's paired STEP file (measured with a throwaway OCCT program under `out/`, `STEPControl_Reader`, volume and a tight bounding box).

Observed.
- The part's B-rep segment is a normal zlib-compressed stream, but its model header is named `ACIS BinaryFile` rather than `ASM BinaryFile4`. The header strings give the program `Inventor` and a kernel version string of the `ASM 218` series, whereas the other samples carry a newer kernel string.
- The 16 header bytes after the name, the three tagged strings, the three scale reals and the record framing (tagged records, `ref` links, `Begin of ASM History Data` terminator) are identical to the layout the reader already handles. Only the name differed.

Change. The reader accepts either name (still requiring exactly one occurrence of the one it finds). No other reader change was needed.

Result. The part imports as one valid solid: 27 × 6 × 24 mm, 19 faces, 1251.553 mm³. The paired STEP measures 27 × 6 × 24 mm (tight bound), 19 faces, one valid solid, 1251.55 mm³ (printed to six figures). Volume agrees to the printed precision, with the same dimensions and face count. This is the first independent reference check for a part from this batch of fixes. Valid solids on the probe: 73 of 84; the other 11 are ten files with no solid body and the oversized snapshot assembly.

Limits. One part, one older kernel string. Other older files may differ in record contents; none is in the local corpus.

## Large part files: size limits and the free-memory guard, 20 November 2026

Method: stream listing of the largest local part (311 MB on disk), a throwaway streaming Zstandard decoder (ctypes, `out/inventor-research/snapshot_measure.py`) to size the three large streams, then the probe with temporary diagnostics (removed) for record and body counts. Comparison point: the JT importer's handling, recorded in `src/ThirdParty/occt-jt/MODIFICATIONS.md` (budget raised on 26 October, budget restarted per unit of work on 2 November, free-memory guard in `src/Import/Jt/JtMemoryGuard.h`).

Observed.
- The file's three large B streams are 213.8, 68.6 and 25.8 MB compressed and 855.5, 216.9 and 110.6 MB decompressed (about 8 s to decompress the largest). The Zstandard frame of at least the largest does not declare its decompressed size, so the previous decoder, which required a declared size, could not read it at all, independent of the byte limits. The earlier compound-file limit error came first only because the 64 MB cap was checked before decoding.
- The saved model has 1,806,040 records (426 k coedges, 314 k attribute records, 212 k edges, 139 k vertices, 105 k loops, 86 k faces) in two bodies. Marker counts over the raw bytes overstate this because attribute payloads contain the same text.
- With the limits lifted the whole model reads in about 10 s with a peak working set of 2.1 GB.

Change (limits). Fixed constants (64 MB segment, 250 000 records, 4 million fields) are replaced by high absolute ceilings that only stop malformed input (4 GiB segment, 100 million records, 2 billion fields) plus a free-memory guard modelled on the JT one (`src/Import/Inventor/MemoryGuard.h`). It stops with a clear message when free physical memory, less the allocation about to be made, is below a 1 GiB floor (the JT default). It is checked while decompressing a segment (every 64 MB), every 65 536 records, and before each solid is built; a platform that cannot report free memory is never stopped. Zstandard frames without a declared size are now decoded in bounded steps under the same byte ceiling.

Result. Valid solids on the probe stay at 73 of 84 and no other file changed. The large part now gets as far as building solids and fails on geometry: body 1 with a tangent-contact configuration the current rules do not cover, body 2 with a saved vertex tolerance of 0.126 mm against the reader's 0.05 mm safety cap. Neither has been examined.

Limits. The guard has not been exercised under low memory (only normal and 8 GB-free runs). The 1 GiB floor is a constant and not yet an import setting; JT exposes its floor as an option and the owner has planned a settings-dialog entry. Whether to raise the 0.05 mm tolerance cap is a separate decision.
