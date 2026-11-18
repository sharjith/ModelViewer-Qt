# Parasolid XT specification study: geometry accuracy

17 November 2026, branch `feature/parasolid-importer`.

## Provenance and scope

Read the local copies of the Siemens "Parasolid XT Format Reference" in two editions: July 2022 and October 2006 (132 pages each). Neither document is stored in the repository. The notes below paraphrase the structure and rules that matter for building OCCT shapes; no vendor text or tables are reproduced. Page numbers refer to the 2022 edition. Also read, as a comparison aid only (provenance in [the source record](PARASOLID_PROVENANCE.md)): the intersection-refinement functions of the local parasolid-kit snapshot. No code from it was used. The earlier research notes in this folder were read first so that the questions below are new ones.

Sections read for this pass: model structure (pp. 25-32), curves (intersection, trimmed curve, SP-curve; pp. 44-52), surfaces (rolling-ball blend, blend boundary, offset, B-surface, swept, spun; pp. 59-70), topology records (shell, face, loop, fin, vertex, edge; pp. 94-99). Not re-read in this pass: lattices, mesh surfaces, attributes, the physical file layout.

## What the specification says that bears on accuracy

1. **Resolution.** XT works at a linear resolution of 1e-8 units (metres by default) and an angular resolution of 1e-11 rad. Points closer than that are coincident. Data should be at least that accurate (p. 26).
2. **Accurate edges lie on their faces.** An edge with no tolerance has a tolerance of half the linear resolution. Its curve lies in the surface of each face that uses it to within the modeller resolution (p. 30). So a gap of 1e-5 mm or more between an edge curve and its face surface in the built shape comes from how the importer builds the curve or the surface, not from the file.
3. **Tolerant edges carry per-fin curves.** For a tolerant edge the edge curve is null, and every fin carries a trimmed SP-curve on the surface of its own face, whose ends lie within vertex tolerance of the vertices and which does not deviate from the other fins' curves by more than the edge tolerance (pp. 29-30, 96). The SP-curve is a 2D B-curve in the face's parameter space (p. 52). The importer already uses these authored pcurves.
4. **Intersection curves are exact; the chart is an approximation.** An intersection curve is one branch of the intersection of two surfaces, identified by its two limits. The chart is an ordered array of points on the curve that defines the parametrisation. It is a piecewise-linear approximation and carries `chordal_error` and `angular_error` estimates (pp. 44-45). The curve between two chart points is defined as the local intersection of the two surfaces with the plane through the chord point (the interpolated point) orthogonal to the chord (p. 47). The natural tangent is the cross product of the surface normals with the surface senses applied. Terminator limits store the singular point and a branch point. An optional `intersection_data` node can carry the surface parameters of each chart point on one or both surfaces (pp. 47-48).
5. **Rolling-ball blends are exact.** The blend is `R(u, v) = C(u) + r X(u) cos(v a(u)) + r Y(u) sin(v a(u))`: a circular arc of radius r about the spine point, in the plane normal to the spine, subtending the angle a(u) between the two contact points. u comes from the spine, v is 0 on the first support and 1 on the second. The spine is the intersection of the two supports, each offset by its `range` value along its sense-adjusted normal (p. 60-61). Cliff-edge blends use a blend with range [0, 0] as one support.
6. **Blend boundaries are implicit.** A `BLEND_BOUND` surface has no parametrisation. It is defined by a distance-function expression involving both supports and their radii, and it intersects a support along the contact curve where the blend becomes tangent to that support. Blend boundaries appear almost only as one surface of the intersection curve that is the boundary edge of a blend face (p. 61-62).
7. **Offset and B-surface rules.** An offset surface inherits the parametrisation of its basis and has the same sense. A B-surface is NURBS with periodic and closed flags per direction and a surface form tag; attached to topology it must be G1. Closed-but-not-periodic and periodic directions are distinct (pp. 62-67).
8. **Swept and spun surfaces** take their section or profile from an analytic curve or B-curve, never from an intersection, trimmed or SP-curve (pp. 67-70).
9. **Topology rules.** Loops are oriented so the face is on the left looking along the loop with the face normal up. Fins in a ring appear in spatial order around an edge. XT has no seam edges: a face on a surface closed in a direction has loops that do not touch (pp. 28-29, 94-99).

## Measurements that follow from this (standalone tool, 17 November 2026)

Method: `parasolid_xt_shapes --deviation 9`, with a one-line trace per edge (`XT_DEV_TABLE=1`) of the largest distance between nine sample points of the built edge curve and the face surface it belongs to, grouped by the kind of XT curve the edge came from. `XT_CHART_STATS=1` prints the chart error estimates. Samples are local engine models, referred to here by neutral ID: XS650-class engine (9,249 faces) and the 18,385-face engine.

- **Chart error estimates are large.** XS650-class: 4,530 charts, median chordal error 0.245 mm, 90th percentile 1.37 mm, 99th 13.4 mm, maximum 54 mm; 4,207 of them above 0.001 mm. The 18,385-face engine: 6,892 charts, median 0.225 mm, 90th 1.56 mm, 99th 11.9 mm. These are the file's own estimates of the chord error, not of the importer's interpolated curve.
- **Where edges miss their faces (XS650-class, 45,701 edge-uses).**

| XT curve kind | Edge-uses | median (mm) | 90th (mm) | 99th (mm) | max (mm) | share above 0.01 mm |
| --- | --- | --- | --- | --- | --- | --- |
| LINE | 17,548 | 7e-15 | 2e-13 | 3e-11 | 8e-7 | 0.0% |
| CIRCLE | 12,321 | 1e-14 | 2e-12 | 0.0076 | 5.74 | 0.7% |
| ELLIPSE | 1,798 | 3e-14 | 3e-11 | 0.0049 | 0.0099 | 0.0% |
| B_CURVE | 392 | 8e-15 | 3e-12 | 3e-11 | 6e-11 | 0.0% |
| SP_CURVE | 2,939 | 3e-7 | 0.00027 | 0.0023 | 0.071 | 0.2% |
| TRIMMED_CURVE | 4,283 | 9e-14 | 0.0005 | 0.12 | 1.53 | 3.7% |
| INTERSECTION | 6,420 | 0.0011 | 0.029 | 0.39 | 9.25 | 20.6% |

  Line, circle, ellipse, B-curve and SP-curve edges are within modeller resolution or close. Intersection edges are the main source of edge-to-surface error, which is what inflates edge tolerances, causes pcurve mismatches and sends faces to the fill path. Each row merges the edge uses of that curve kind with either sense.
- **Exact refinement of the chart (experiment, `XT_REFINE_CHART=1`).** Points between chart points are found by a Newton iteration on the two surface parameters (point on both surfaces and on the plane through the chord midpoint orthogonal to the chord), inserted until the chart's own chordal error estimate would fall below 0.0005 mm (at most three halving passes), and the curve is then interpolated or fitted as before. Blend and blend-boundary surfaces are not evaluated this way and are left alone.
  - XS650-class, intersection edges: median 0.0011 to 3e-6 mm, 90th percentile 0.0285 to 0.0071 mm, 99th 0.394 to 0.136 mm, share above 0.01 mm 20.6% to 7.5% (swept and spun surfaces enabled).
  - XS650-class, whole model: invalid faces 71 to 55 (52 before swept and spun surfaces were included), faces built 9,246 to 9,248, edges built 23,257 to 23,265. Build time 53 s to 60 s, single-threaded. 2,405 charts were refined (86,085 points inserted), 1,819 were left alone as unsupported (blend or blend boundary involved), 2 failed.
  - What remains above 0.01 mm: 332 of the 1,529 intersection edges on `BLENDED_EDGE` faces, then about 45 on cylinders, 41 on tori, 40 on cones. These are mostly edges whose face is a fitted blend surface or whose second surface is a blend boundary.
  - Not verified visually, and not yet run on the other samples except the 18,385-face engine (result recorded below once available).

## Design notes from the comparison library (read only)

The local parasolid-kit snapshot solves the same problem with the same ingredients: a point refiner that moves a point onto both supports within the plane normal to the tangent (Newton with Levenberg-Marquardt damping, falling back to alternating projections where two supports meet at a shallow angle), a marcher that continues a branch from a start point, and a fit that checks every interpolation span against both surfaces at interior parameters and adds corrected samples only on failing spans, within the body's linear resolution. Two ideas follow for the importer: verify the fitted curve against the surfaces instead of inserting points by a fixed rule, and allow a per-support uncertainty when a support is itself an approximation (a fitted blend).

## Open questions

- A blend face is built from sections sampled at uniform spine parameters and fitted with a tolerance of at least 0.01 mm; its boundary edges come from separate intersection charts. Building the blend so that its contact curves are its own iso-curves (v = 0 and v = 1) would make those edges exact and their pcurves straight lines. Not tried.
- The importer ignores the optional per-point surface parameters of an intersection curve. Whether samples carry them in these files has not been measured.

## Experiments after the study (17-18 November 2026)

All measured with the standalone tool on local samples (XS650-class engine, 9,249 faces; the 18,385-face engine); none checked visually in the viewer. Switches are environment variables while the changes are being evaluated. Regenerate with `parasolid_xt_shapes <layouts> <file.x_t> --mesh --face-table <out.tsv>` and the variables named below; outputs under `out/`.

| Experiment | Switch | XS650-class result | 18,385-face engine result |
| --- | --- | --- | --- |
| Baseline (forced `BRepLib::SameParameter`, as committed) | none | 70 invalid faces, 9,219 meshed, build 25 s | 334 invalid, 18,362 meshed, 105 s, 113 bodies without free edges |
| Exact chart refinement (adaptive, accepts two-point charts) | `XT_REFINE_CHART=1` | intersection edges above 0.01 mm from their face: 20.6% to 7.9% | 3,555 charts refined |
| Contact curves from the exact spine (blend boundary edges) | `XT_BLEND_CONTACT_SPINE=1` (with the above) | above 0.01 mm: 7.9% to 3.1%; blend-face outliers 331 to 146; 1,005 edges rebuilt | 1,741 edges rebuilt |
| STEP-style `ShapeFix::SameParameter` | `XT_SAMEPARAM_MODE=measure` or `enforce` | invalid 63 to 47 (measure) or 48 (enforce), build time unchanged | |
| All three together | all of the above | 70 to 47 invalid; build 25 s to 35 s | 334 to 304 invalid, 113 to 116 bodies without free edges, 18,363 meshed, build 105 s to 130 s, meshing 9 s to 22 s |
| `ShapeFix_Face::Perform` on the faces still invalid (STEP-style settings, precision 0.001 mm, maximum tolerance 1 mm) | `XT_TRY_SHAPEFIX=1` (tool only) | baseline: 51 of 90 repaired, 8 changed area by more than 1%, none dropped; with the curve changes: 40 of 79 repaired | not run |

The 87 MB sample (B4T class, 22,090 faces, 1,885 bodies) with all three: 22,056 faces built (22,008 in the earlier baseline before the SameParameter commit), 21,628 valid and 428 invalid (16,306 valid and 5,704 invalid), 21,906 meshed (21,819), build 326 s (592 s), meshing 10 s (168 s). That baseline predates the committed SameParameter change, so the gain is the sum of both.

All three are now on by default in the working tree, uncommitted, so that the next viewer build exercises them. Opt-outs for comparison: `XT_NO_REFINE_CHART=1`, `XT_NO_BLEND_CONTACT_SPINE=1`, `XT_SAMEPARAM_MODE=legacy` (the earlier forced `BRepLib` call; `enforce` re-derives every edge). The tuning variables are `XT_REFINE_TARGET` (millimetres per scale unit, default 1e-8 of the model scale) and `XT_BLEND_FIT_TOL`. Defaults checked on the XS650-class sample: 47 invalid, same as the explicit switches.

Isolated duct (body 116 of the 18,385-face engine): with the curve changes, free edges 143 to 52, faces built natively 172 to 176 (fills 9 to 5), faces with a free edge 30 to 20, total area 79,547 to 78,614 mm^2. The large missing skin (see the STEP comparison) is unchanged: that skin is a plain non-periodic, non-closed B-spline (u 0.492 to 1.005, 110 x 85 poles) used by one face with one loop, so the importer does not clip it. Why the STEP export has a second skin of about 19,500 mm^2 that this body lacks is still open.

Findings behind the numbers:

- The edges that fail to lie on their faces are almost all intersection curves (see the table above). Charts have large stated chordal errors (median 0.245 mm), and a two-point chart is replaced by a chord, which misses a curved intersection most at its middle; the worst sample of a bad intersection edge sat in the middle in 52% of cases and at an end in 12%.
- The first refinement version skipped charts with fewer than three points and kept more than twice as many inserted points for the same accuracy; it was replaced by the adaptive one.
- Tightening the blend surface fit tolerance (0.002 or 0.0002 mm instead of at least 0.01 mm) changed blend-face outliers only from 331 to 322 or 327, so the fit tolerance is not the cause; the independently built boundary edges were.
- Face validity does not follow edge accuracy one to one: refinement alone left invalid faces near 52 to 73 in different variants, while changing the SameParameter operator moved it from 63 to 47 with identical geometry.

Not run: experiments 3 to 6 of the [study notes](PARASOLID_OCCT_STUDY_NOTES.md#experiments-proposed-from-this-study) (pcurve projection comparison, G1 fills, `FixLacking` and closed-face splitting, XCAF document architecture). The G1 fill needs the neighbouring native faces, which are not built yet when most fills are made (fills are created while faces are built), and a rebuilt fill creates its own edges, so it would be a post-pass that also has to re-share edges.
