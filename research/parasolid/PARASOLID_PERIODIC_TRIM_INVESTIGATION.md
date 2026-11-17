# Oversized periodic geometry and face loss

15 November 2026, branch `feature/parasolid-importer`.

The owner reported a large ring and missing geometry in the viewer. The local standalone sample is `XT-097375059234`, SHA-256 `09737505923474173b8e4fe7334c51b870cfd86fd419038b9e031d040a70f7fb`. The existing Python reader reached the terminator at 260,520 records using the local V13 layout. This analysis used the owner's screenshot, the current source, decoded records and a pre-existing local diagnostic log. It used no new vendor software, SDK, schema catalog or third-party decoder. The layout and its provenance are described in the [provenance record](PARASOLID_PROVENANCE.md).

## Source measurements

- The stream contains 9,249 face records and 474 torus surface records. Torus 113482 has major radius 1.22747 m and minor radius 0.010 m under the importer's metres assumption. Its sole referencing face, 116391, has a five-edge loop. The five boundary vertices occupy a box of approximately 10.3 × 35.6 × 32.7 mm and span 1.84° around the torus axis. Eight other faces reference tori with major radius greater than 0.8 m. Their boundary vertex samples span 0.21° to 2.60°. No torus face in this sample had a zero loop reference.
- The source point records occupy a box of approximately 393 × 541 × 433 mm. These are definition-space point bounds, before occurrence transforms. They exclude analytic extrema between vertices. Angular spans above are the minimum arcs containing the boundary vertices, not measurements of the intervening curves or exact patch size.
- An earlier diagnostic run in ignored `out/parasolid-research/scratch/y2.txt` reported 8,445 reconstructed faces, of which 7,932 had triangulation, and 52 of 75 reconstructed bodies passed its B-Rep validity check. Its rejection counts included 179 pcurve mismatches and 135 self-intersecting wires. The log predates this change and does not identify the viewer build in the screenshot. Its SHA-256 is `18143b1b5e63318bcaa21c315f5110fe438afa0b21028da744ed7d11979432c0`.

Regenerate the source measurements locally from the repository root using the two ignored scratch scripts and the same local sample corpus. The first script checks the neutral sample hash before decoding and writes its intermediate inventory under the same ignored folder.

```powershell
python -B out/parasolid-research/scratch/xt-ring.py > out/parasolid-research/xt-ring-records.txt
python -B out/parasolid-research/scratch/xt-ring-trims.py > out/parasolid-research/xt-ring-trims.txt
```

The input layout SHA-256 is `b5aab4bc21fcc70ca02a07878f0a2340ff8d2dafaee68ebfbc50e0b5576ea462`. The script SHA-256 values are `5e1752d8e9081b27dd6c2f4e4a11ca3f1ac0ee0520671133fa021d2a9cd51c10` and `6d26653344f511ec568a6f22226a5cbc3bd3c1e2bc6a377fb2a813b4940fbb91`, respectively.

## Fix and remaining validation

The source supports a large-radius trimmed patch. The large ring is consistent with a wrong periodic trim or winding. The specific viewer face has not yet been matched to a source node. A point-to-support comparison can be small even when the reconstructed face covers the wrong side of a periodic loop.

The viewer now applies its existing triangle/edge bounds guard after both the body pass and the per-face retry. Previously, the retry accepted a face as soon as OCCT attached any triangulation. This closes one route for oversized retry meshes to reach the scene. The bounds guard is a heuristic. It can miss an artifact whose edges already took the long arc, or reject a legitimate curved patch when interior extrema extend beyond its boundary. It does not recover a missing face.

Owner validation is pending. Reload the same X_T sample after building and check whether the ring remains, how many bodies and faces the viewer reports dropping, and whether the visible engine surfaces change. If the ring persists, match its mesh to the original face and compare source curve ranges, output edge ranges and unwrapped loop UVs before changing the trim logic. An independently exported STEP of the same model is needed to qualify geometry recovery, dimensions and placement.

## Owner reload and revolved surface correction

The owner reported that the ring persists after the bounds-guard change. A source-only follow-up computed the directed circular edge sweeps on the nine candidate TORUS faces whose major radius exceeds 0.8 m. The large-radius boundary arcs are short in the source. The screenshot's wide tube is more consistent with two `SPUN_SURF` supports, 236766 and 245680. Both revolve a circular profile of radius 0.33508 m about an axis 0.84015 m from the profile centre. Their untrimmed support would form a ring with outer and inner diameters of approximately 2.350 and 1.010 m. The 13 referencing faces' first-loop vertex samples each span less than 0.97° about that axis. These are candidate surfaces, since the screenshot still has no mesh-to-face trace.

The July 2022 XT reference, §5.2.2.11, assigns the profile to the first parameter and the angle to the second. OCCT orders them the other way around. Swapping the two derivatives reverses the natural normal. The importer now records that orientation reversal for `SPUN_SURF`, so the existing face-normal and trim-winding logic uses the correct sign. The layout did not change. [OCCT Geom_SurfaceOfRevolution](https://occt3d.com/dev/doc/refman/html/class_geom___surface_of_revolution.html)

```powershell
python -B out/parasolid-research/scratch/xt-arc.py > out/parasolid-research/xt-arc-records.txt
python -B out/parasolid-research/scratch/xt-arc-trims.py > out/parasolid-research/xt-arc-trims.txt
python -B out/parasolid-research/scratch/xt-spun-trims.py > out/parasolid-research/xt-spun-trims.txt
```

Scratch script SHA-256 values in command order are `84e76c1300f795b05f4881bca07192eb99f7c12e22ff081a38fae82acecfc8df`, `1fdce37041d365e2a899d336ed5a0575546a13a5a3750fe0841f4290cd2986f1` and `e4d3287575578437b84bf66abb6f39f902199f8837dff0a3801fbba7236cf7a9`. No new build or geometry run was made in this pass. Owner reload is needed to confirm that the orientation correction removes the ring and retains the intended patches.

## Intersection edges ending at singularities

The owner confirmed that the revolved surface is now correctly limited. Missing surfaces remain. The next read-only pass compared source edge vertices, intersection chart ends and `LIMIT` records in `XT-097375059234`. The July 2022 XT reference, §5.2.1.5, distinguishes the singular endpoint of a type-T limit from its nearby branch point, which is present in the chart. No SDK, vendor runtime or third-party decoder was used.

Of 3,900 edges backed by a direct or trimmed intersection curve, the audit found 234 type-T limit uses. Each stored branch point matches its chart end within 0.0001 mm. Eight edges have a vertex matching the stored singular endpoint, with the vertex 0.102–0.239 mm past the chart end. Edge 119769, for example, has a source gap of 0.102226 mm, matching an intersection-edge mismatch in the prior diagnostic log. The current edge builder rejects endpoint distances greater than 0.1 mm at the default scale. The sample audit measures source coordinates and does not predict a face recovery count.

The curve builder now includes the stored singular endpoint when the limit has the expected two points and the branch point matches the chart end. It falls back to interpolation if a dense chart fit moves the restored endpoint outside the chart deduplication tolerance. The existing edge, face and tessellation checks still apply. This covers a specific source error in intersection reconstruction. Blend fitting, other pcurve failures and tessellation losses still need a current baseline after reload.

```powershell
python -B out/parasolid-research/scratch/xt-chart.py > out/parasolid-research/xt-chart-records.txt
python -B out/parasolid-research/scratch/xt-chart-ends.py > out/parasolid-research/xt-chart-ends.txt
```

Scratch script SHA-256 values in command order are `f62c071ec1c2a885af903b086ff42649681d3fdfb92b3e1e1426d7fc3f6eee33` and `c9a514f365ee1a28a2cf3e7b62995295aeabbba12c40b650eb7cfc35972e0230`. Owner validation is pending. Compare the rebuilt shape inspector's edge and face rejection counts and the viewer's dropped-face warnings on the same sample with the same settings.

## Persistent face loss and a stalled larger sample

The owner reported continued face loss and a stall on `XT-5268a1dca7d6`, SHA-256 `5268a1dca7d6a8a84f2812f425f1b81a43952e33c68ca36e2a17c5bb33c0fd58`. The ignored prior shape-inspector log under `out/parasolid-research/scratch/` ends after a successful record decode of 1,431,957 records (log SHA-256 `39105989ae71a0702a9cd005b9114daad2a7e55bf62ac6120f37dc9faddfe8d8`). This is historical evidence and does not establish the active viewer operation.

The read-only Python audit reproduced the record count and found 61 bodies and 37,406 face records. Body 20 has 12,504 distinct reachable faces, including 5,469 blend faces, counted from its region and shell chains. The maximum number of distinct fins reached from a source loop was 340. The audit used the previous local V13 layout and no CAD runtime or third-party decoder.

```powershell
python -B out/parasolid-research/scratch/xt-large-audit.py > out/parasolid-research/xt-large-audit.txt
```

The scratch script SHA-256 is `b592563722d304477f0897a3613e5fe07116edd9a6f71fc34e0f2f4f48f9b3d9`. The importer now checks fin ring closure while visiting distinct fins, polls cancellation during face construction and supplies the cancellation callback through OCCT's triangulation progress range. Cancellation between face operations is cooperative. A single OCCT operation that does not poll cannot be interrupted by this change.

To obtain comparable owner evidence after rebuilding, set `XT_TRACE_IMPORT` and launch the viewer from the same PowerShell session. Open each affected file separately and collect its stderr trace and import warnings. The trace records source node IDs for build stages and face omissions, plus body IDs for meshing. A log from the stalled sample can distinguish pcurve projection, seam repair, face validation, body parameter reconciliation and meshing. Fresh warnings and omitted face IDs from the first sample are needed to choose the next geometry repair.

```powershell
$env:XT_TRACE_IMPORT = '1'
Start-Process -FilePath "<rebuilt-viewer-executable>" -RedirectStandardError out/parasolid-research/xt-viewer.log
Remove-Item Env:XT_TRACE_IMPORT
```

Use a distinct output filename for the second launch. Viewer build, reload and cancellation verification remain with the owner.

### Follow-up at 64%

The owner reported the rebuilt viewer stalled at 64%. The installed executable contains the new XT trace labels. The live process remained responsive to Windows and its CPU time increased, with most CPU time on one thread. No nonempty trace of the active XT operation was available. This is consistent with a CPU-bound operation and does not distinguish slow progress from an algorithm that fails to converge.

In the XT file-read progress interval, 64% is in shape construction. Replaying source face traversal found 37,406 face attempts. The 64% display interval corresponds to roughly 33,300–34,000 attempts, spanning source bodies 565, 577 and 589. Progress is published every 64 attempts and does not advance during body finalization, so the display alone cannot identify the blocked face or distinguish face work from body finalization. The replay used the same source and local V13 layout as the preceding audit and did not run the viewer, an OCCT operation or a test.

```powershell
python -B out/parasolid-research/scratch/xt-progress-audit.py > out/parasolid-research/xt-progress-audit.txt
```

Scratch script SHA-256 `d3509688a0e209dcfefd91a4bc337933c6d59b9232652b1b6502bd95c0369ca4`. The last flushed `XT build` trace line from a reproduced stall is the outstanding evidence.

### Live trace at the stall

The owner's next run reached 64% with tracing enabled. The live log `out/parasolid-research/xt-stall.log` stops at `XT build repair-seam node=146660` after three pcurve projection entries for that face in body 577. It did not reach the next face validation checkpoint. This locates the stall in the full `ShapeFix_Face::Perform()` call. The trace does not show which internal sub-operation is responsible.

The source face uses sphere 146658 and one loop, 146665, with three fins. The importer now calls `FixMissingSeam()` directly for single-wire spherical faces. The dedicated routine handles the case where a belting wire needs a degenerate edge at a pole. It can return a shape containing more than one face. The builder accepts a single repaired face and reports any empty or split result because its face interface cannot represent the split. Other periodic faces keep their current healing path. The new `repair-spherical-seam` trace label distinguishes a stall inside the dedicated routine from the earlier full-pipeline call. [OCCT ShapeFix_Face reference](https://occt3d.com/dev/doc/refman/html/class_shape_fix___face.html) · [OCCT 7.9.0 implementation](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_0/src/ShapeFix/ShapeFix_Face.cxx)

```powershell
python -B out/parasolid-research/scratch/xt-stall-face.py > out/parasolid-research/xt-stall-face.txt
```

Scratch script SHA-256 `93cd2183e57ad112f1dc4af3259b56a4b93697801415d7f2340257b0a1ac099f`. Owner build and reload are pending. Verify that the trace advances past face 146660 and check for spherical seam omissions. This change targets the stall. The remaining face loss still requires separate validation.

### Dedicated seam repair also stalled

On the owner's rebuilt viewer the trace reached `repair-spherical-seam node=146660` and stopped. A follow-up raw-record audit confirmed a 1 mm-radius sphere bounded by three 90° great-circle arcs. Its three endpoint directions are mutually orthogonal to the precision of the input and all have positive projection onto their sum. The negative XT face sense is accounted for when constructing the forward OCCT face. Together these facts identify a local spherical triangle.

The builder now recognizes positively wound single-wire spherical triangles on the reconstructed geometry. It checks the circle centres and radii, short-arc ranges, loop closure and common hemisphere. For this case it uses an equivalent sphere parametrization with the triangle around longitude π. This places the parameter seam and both poles outside the triangle without changing the sphere or the three boundary curves. It then projects the edges and uses the existing face checks. Other spherical trims continue through the dedicated seam repair.

```powershell
python -B out/parasolid-research/scratch/xt-stall-geometry.py > out/parasolid-research/xt-stall-geometry.txt
```

Scratch script SHA-256 `c2163265161892702bcc9dc0f211d7aa0f4330468b3d3897094744544568049c`. Owner validation is pending. Check for `spherical-triangle-chart node=146660`, for progress past that face, and whether the face was retained. This pass used source records and the live log and ran no build or test.

### Owner reload confirmed

15 November 2026, the owner confirmed `XT-5268a1dca7d6` completed loading without the 64% stall. The run's trace records `spherical-triangle-chart node=146660`, three pcurve projections, face validation and continuation past this face, with no omission entry for that node. Body 577 reached both the end of shape construction and the end of its meshing pass. The trace does not report which individual faces survived the later mesh check.

The same run reported 7,844 of 37,406 face attempts omitted during B-Rep reconstruction, with 6,427 attributed to `BLENDED_EDGE`, and separately reported 7,335 faces omitted during meshing. These counts establish the next baseline. Load completion is owner-confirmed, while geometry fidelity remains open. Regenerate these observations with the viewer trace and warning capture described above.

### Rolling-ball blends supported by another blend

On 15 November 2026 a read-only join of the completed viewer trace and source records for `XT-5268a1dca7d6` identified 3,600 distinct omitted rolling-ball faces with one supporting rolling-ball blend whose own supports are non-blend surfaces. For example, 1,825 omissions in the wider source classification were intersection-spine rolling blends supported by a blend and a plane. These are candidate faces, not a predicted recovery count. The previous builder requested both supports through its ordinary surface lookup, which has no `BLENDED_EDGE` case. It returned without attempting those faces.

The builder now evaluates contact for one level of rolling-ball support with equal offset magnitudes. At the foot of the dependent ball centre on the supporting spine, it obtains the two limiting directions from the ordinary supports and reconstructs the circular section. It accepts a radial contact only within that section and within 0.02 mm of the signed source offset vector under the default scale. It also checks the distance to the dependent blend's other support. The dependent grid still goes through the existing spline approximation, pcurve and tessellation checks. The contact threshold is a local acceptance bound, not a measured error bound for the final face. Cliff-edge, deeper and two-sided nested blends remain unsupported by this change.

Method used the local V13 layout, the owner-run trace, the July 2022 XT reference §5.2.2.6 for the rolling-ball section and signed offsets, and the existing text decoder. No CAD or third-party decoder was run for this analysis.

```powershell
python -B out/parasolid-research/scratch/xt-blend-loss.py > out/parasolid-research/xt-blend-loss.txt
```

Scratch script SHA-256 `40d7a58f1f707905499af6c19145fcd6dc81ebbbe609dd5b86c86ad096931350`. The join uses the baseline `xt-stall.log` from the completed 64% stall-fix run. Owner validation is pending. Preserve that baseline log, use a new filename for the validation run, and compare the `BLENDED_EDGE` omissions, meshing omissions and elapsed time. The shape inspector also reports the fitted surface count under `BLENDED_EDGE (blend support)`. A successful fit can still be rejected when constructing or meshing its trims.

The owner confirmed the viewer's application-data log directory is available for direct inspection. The last completed loads in the session started at 22:05 local time preceded the nested-blend build. They define these baselines.

| Sample | Face attempts | Reconstruction omissions | Blend omissions | Meshing omissions |
| --- | ---: | ---: | ---: | ---: |
| `XT-5268a1dca7d6` | 37,406 | 7,844 | 6,427 | 7,335 |
| `XT-097375059234` | 9,249 | 657 | 287 | 483 |

The installed viewer built at 22:16 local time contains the new blend-support marker. No completed XT load from that executable appeared in the logs at the time of inspection. The application log supplies warning totals and elapsed time. The XT stderr trace is useful when correlating those totals with source faces.

### Neighbour-boundary mesh recovery

15 November 2026, the owner's engine sample reload using the blend-support build reported 657 of 9,249 face attempts omitted at reconstruction (including 287 blend faces) and 483 omitted during meshing. These counts match the preceding engine baseline. No completed larger-sample run with that build was available during this pass.

The XT mesh prepass removed failed faces before the shared converter could try its existing boundary-based recovery. XT now tries that recovery before the guarded per-face OCCT retry. The neighbour map contains only faces whose prepass mesh passed the XT checks. The fallback reconstructs triangles from shared-edge samples, or from the 3D boundary curves when no neighbour has a sample. Its output must pass the same XT extent check as the native mesh. The viewer reports the number recovered separately from omissions. XT also requests the existing triangulations be copied when converting a placed occurrence, so that assembly placement does not discard the mesh just accepted by the prepass. [OCCT face triangulation API](https://occt3d.com/dev/doc/refman/html/class_b_rep___builder.html) · [OCCT occurrence transformation API](https://occt3d.com/dev/doc/refman/html/class_b_rep_builder_a_p_i___transform.html)

This change addresses the meshing stage for faces that already have a reconstructed surface and boundary. The result is a display mesh approximation. It does not reconstruct omitted B-Rep faces. No new SDK or third-party implementation was used. On owner reload, compare the two meshing messages, elapsed time and visible periodic patches. The application log can be read directly with `Select-String -LiteralPath "<current-modelviewer-log>" -Pattern 'Parasolid:|loadingFinished handled on viewer thread'`. Owner verification is pending.

### Boundary fallback result and additional torus forms

The next owner reload of `XT-097375059234` reported 476 faces recovered from their boundary curves. Meshing omissions fell from 483 to 7. Reconstruction still omitted 657 faces, including 287 blend faces and 30 torus faces. The viewer reported completion after about 21 seconds. The comparison uses the application warning groups from the 22:18 and 22:22 local sessions. This confirms a reduction in mesh omissions on the engine sample, while visual fidelity and reconstruction still require work.

The source contains 313 ring torus faces, 28 apple torus faces and two lemon torus faces. The old radius check supports only the ring form and rejects all 30 apple and lemon faces at surface construction. The source record audit measured 96 apple boundary vertex occurrences. Their maximum residual against the nonnegative-radius branch of the published torus equation was less than `5e-13` mm under the default scale. The two lemon faces yielded no explicit fin vertices for this comparison. Their geometry still requires owner verification.

Apple and lemon support now revolves the circular meridian with its authored signed offset. The meridian is bounded by the zeros of the radial coordinate, which removes the self-intersecting extension. Rotation is U and the meridian angle is V, as in the XT torus definition. The importer's existing face validation and mesh guards still apply. The 30 faces are reconstruction candidates. The code change has not been built or run in this pass. [OCCT revolution API](https://occt3d.com/dev/doc/refman/html/class_geom___surface_of_revolution.html) · [OCCT trim API](https://occt3d.com/dev/doc/refman/html/class_geom___trimmed_curve.html)

The audit uses the July 2022 XT reference §5.2.2.5, the local V13 layout, and the existing prefix and record analysis. No vendor CAD software, SDK, or third-party decoder was used. Regenerate with:

```powershell
python -B out/parasolid-research/scratch/xt-ring.py > out/parasolid-research/xt-ring-records.txt
python -B out/parasolid-research/scratch/xt-torus-branches.py > out/parasolid-research/xt-torus-branches.txt
```

The second script SHA-256 is `6444a02a38466cbca5f1a4ce5caef318471a3adba5678b15364559b101c27447`. On owner reload compare total reconstruction and mesh omissions as well as the `TORUS` category. A reduction in that category alone can mean the loss moved downstream to trimming or meshing.

### Remaining missing spherical sheet bodies

The owner reported no visible improvement after the torus change. The 22:26 local session logged 601 reconstruction omissions, down from 657 in the preceding run. The torus category fell from 30 to zero, the blend category from 287 to 263, and meshing omissions stayed at seven. The two complete sheet-body omissions also stayed unchanged.

The source audit located their single faces at 211543 and 258323 in `XT-097375059234`. They are spherical caps with respectively 16 and 8 authored boundary edges. The lowest boundary vertex latitudes are about 81.5° and 72.6° in the authored sphere frames. Seventeen tolerant edges between the two faces carry trimmed SP-curves. Their sampled source endpoint records agree with the fin vertices at the precision printed by the audit. This argues against replacing the spherical patches with untrimmed support surfaces. These are source observations. The exact rejection stage of each face in the owner run was not individually traced.

The compact-cap path now checks closed, positively wound single-wire spherical trims. It bounds every edge in a pole-oriented frame and requires the entire bound on the cap side, beyond half the sphere radius. It then reorients the surface chart so the cap avoids the seam and poles. The surface geometry and boundary curves are preserved, and the face still goes through the existing projection, validation and tessellation checks. This extends the chart approach already verified for the spherical triangle without changing the ordinary seam-repair path for other trims. [OCCT coordinate transform](https://occt3d.com/dev/doc/refman/html/classgp___trsf.html)

```powershell
python -B out/parasolid-research/scratch/xt-missing-bodies.py > out/parasolid-research/xt-missing-bodies.txt
python -B out/parasolid-research/scratch/xt-body-detail.py > out/parasolid-research/xt-body-detail.txt
python -B out/parasolid-research/scratch/xt-sheet-pcurves.py > out/parasolid-research/xt-sheet-pcurves.txt
```

Scratch script SHA-256 values in command order are `fa5499c49b7c88b22459cae76a5192a3a4d11fc1a3e6f455b1ed2423c642919f`, `946712ba66ef81fb0789a3475b1e9b888cc7e4232a7417af19845516e2d81371` and `5014c4c0047f9b0346ec213717bbde1d2165abac65d1918833063741226c1c37`. The audit used local source records and the application warnings, with no CAD runtime or third-party decoder. Owner reload is pending. Check that body omissions 68 and 231437 clear and that the patches render at the correct extent. With tracing enabled, the expected chart label is `spherical-cap-chart`.

### Unused knot-array slots rejected the sheet boundaries

The 22:32 local owner reload still reported 601 reconstruction omissions and both missing sheet bodies. Meshing omissions were five, down from seven. A source audit found 18 two-dimensional NURBS curve records with two trailing pairs of unset knots and zero multiplicities. Seventeen are used by the tolerant boundary edges on the two missing sheet faces. Their populated knots are finite and increasing, and their multiplicities sum to the required pole count plus degree plus one. For example, the pcurve of edge 211555 has 87 poles, degree two, and 90 populated multiplicities in total. The two unused slots caused `knotVectors()` to reject it before face reconstruction.

The helper now trims only paired trailing unset/zero slots, then checks ordering, positive integral multiplicities and the existing total. This is an observed storage convention for the sampled producer. The shared helper covers both curve and surface knots, with no change to the populated data. This pass used read-only source records and the existing decoder. It used no CAD runtime or third-party decoder.

```powershell
python -B out/parasolid-research/scratch/xt-body-detail.py > out/parasolid-research/xt-body-detail.txt
python -B out/parasolid-research/scratch/xt-knot-padding.py > out/parasolid-research/xt-knot-padding.txt
```

Scratch script SHA-256 values are `f0992c0d68bc3276e81fd00eef642cbfbf4d9a068fbab37201f1b60fef5150e` and `ef5ceeb903cc43f6c93e63647590d28566c50d432939e78d9717b60bc8872acb`. Owner build and reload remain pending. The audit establishes why decoding those spline boundaries failed. Whether each complete sheet survives the downstream trim and mesh checks remains open.

### Display recovery for rejected B-Rep trims

The next owner run, logged at 22:37 local time, still showed substantial visual loss. Reconstruction omissions fell from 601 to 595. Neither sheet body appeared in the omission warnings. Five meshing omissions and 504 successful boundary-based mesh retries were unchanged. The remaining reconstruction warnings included 138 self-intersecting wires and 23 pcurve mismatches. The rejected faces could not use the mesh fallback because the builder had already discarded them.

Standalone XT now retains candidates from those two rejection paths and the missing-pcurve path when the built surface and complete 3D trim are available. Closed surface charts remain excluded unless the spherical trim has already passed the bounded chart checks. The viewer tries the existing boundary triangulator and rejects a candidate if any sampled boundary point differs from its projected surface point by more than 0.05 mm, if the fallback detects crossing constraints or cannot triangulate the loops, or if the resulting triangles fail the XT bounds check. Recovered triangles enter the viewer as display meshes, outside the solid and XT-in-JT B-Rep pipelines. They use the original occurrence placement. The application now reports recovered and considered candidates beside the B-Rep omission total. [OCCT mesh-only face API](https://occt3d.com/dev/doc/refman/html/class_b_rep___builder.html)

The 161 logged pcurve and wire rejections are an upper bound on this run's candidate group, since the new path requires supported surfaces and chart checks. Blend and edge reconstruction losses remain. This change used the existing local triangulator and owner application log. No new decoder, build or viewer test was run. Compare the candidate recovery count and inspect recovered patches for cracks, orientation and oversized geometry after owner reload.

### Parameter-space curves carried directly by edges

The 22:45 local owner run recovered 71 of 113 display candidates. The application log still reported 595 B-Rep omissions and five other meshing omissions; the owner reported that the visible problem remained.

The earlier `XT-097375059234` trace, joined to its source records, identified 65 face omissions where the first rejected edge directly names an `SP_CURVE` or a `TRIMMED_CURVE` over an `SP_CURVE`. Those references use 38 distinct curve records. Their supporting surfaces were 12 planes, 30 cylinders, 13 B-surfaces and 10 revolved surfaces, counting by affected face. These are historical face references and candidate counts, not the latest runtime rejection breakdown. The existing curve lookup handled parameter-space curves only when read through a tolerant fin, so these edge records never reached the same evaluator.

The standalone reader now uses the same spline-lifting routine for fin-supplied curves and edge-supplied curves. A requested trim is applied to the 2D spline before sampling it on the 3D support. Linear parameters are scaled where needed and the rotations and profile parameters of revolved supports are mapped to OCCT's order. The sampled 3D curve remains an approximation and still has to pass the usual edge, face and meshing validation. No exact geometry claim or owner acceptance follows from the source audit.

```powershell
python -B out/parasolid-research/scratch/xt-contact.py > out/parasolid-research/xt-contact-records.txt
python -B out/parasolid-research/scratch/xt-sp-edges.py > out/parasolid-research/xt-sp-edges.txt
```

Scratch script SHA-256 values are `a4a3c6ea9f786af2202755397d8be5637ae946d4e8c8f5ee85a15c9a3268c795` and `43f560117f587471e8a2e4328c00ed4f0d1071ec6dcbb988d34383ef93581662`. The join used the earlier mixed-session `xt-stall.log` and filtered by source face and edge IDs. The source layouts and evaluation conventions follow the locally retained XT reference. This pass ran no CAD runtime, build or tests. For owner validation compare total reconstruction and mesh losses as well as the `edge-unsupported` count.
