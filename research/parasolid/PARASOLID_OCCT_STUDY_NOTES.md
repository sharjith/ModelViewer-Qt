# OCCT 8.0.1 study notes for the Parasolid X_T importer

17 November 2026, branch `feature/parasolid-importer`.

## Provenance and scope

Read the OCCT 8.0.1 source tree extracted by vcpkg from the [OCCT V8_0_1 release](https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1) (tarball SHA-256 `0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a`, as recorded in [the topology comparison](PARASOLID_OCCT_TOPOLOGY_COMPARISON.md)), its user guides under `dox/user_guides` (Shape Healing, Modeling Data, STEP, XDE) and the STEP shape-processing resource file `XSTEPResource/STEP` from the owner's local OCCT 8.0.1 binary package (headers and resources only; it holds no `.cxx` files). Nothing from OCCT was copied into the importer; these notes record behaviour that was read. Local paths are relative to the source root.

Studied so far: shape healing (`ShapeFix`, `ShapeAnalysis`, `ShapeConstruct`, `ShapeUpgrade`), the STEP and IGES read pipelines and their tolerance rules, `BRepCheck`, `BRepFill_Filling`, `GeomFill` and `GeomPlate` (guide level), the XDE document model and how the viewer's XCAF processor consumes it, and the DE wrapper (guide level). Read at overview level only: `ShapeFix_Wire::FixSelfIntersection` and `FixShifted` bodies, the `BRepLib::SameParameter` algorithm body, the `STEPCAFControl` colour transfer, `BRepBuilderAPI_Sewing`, and the blend builders (`ChFi3d`, `BRepBlend`).

## BRepCheck status codes

`src/ModelingAlgorithms/TKTopAlgo/BRepCheck/BRepCheck_Status.hxx` numbers the codes from `NoError = 0` in this order: 1 InvalidPointOnCurve, 2 InvalidPointOnCurveOnSurface, 3 InvalidPointOnSurface, 4 No3DCurve, 5 Multiple3DCurve, 6 Invalid3DCurve, 7 NoCurveOnSurface, 8 InvalidCurveOnSurface, 9 InvalidCurveOnClosedSurface, 10 InvalidSameRangeFlag, 11 InvalidSameParameterFlag, 12 InvalidDegeneratedFlag, 13 FreeEdge, 14 InvalidMultiConnexity, 15 InvalidRange, 16 EmptyWire, 17 RedundantEdge, 18 SelfIntersectingWire, 19 NoSurface, 20 InvalidWire, 21 RedundantWire, 22 IntersectingWires, 23 InvalidImbricationOfWires, 24 EmptyShell, 25 RedundantFace, 26 InvalidImbricationOfShells, **27 UnorientableShape**, 28 NotClosed, 29 NotConnected, 30 SubshapeNotInShape, 31 BadOrientation, 32 BadOrientationOfSubshape, **33 InvalidPolygonOnTriangulation**, 34 InvalidToleranceValue, 35 EnclosedRegion, 36 CheckFail.

Earlier notes in this folder read 27 as NotClosed and 33 as a geometry code; both were wrong.

- `BRepCheck_Analyzer.cxx` sets 27 on a face as a derived flag. If any edge-on-face status is NoCurveOnSurface, InvalidCurveOnSurface, InvalidRange or InvalidCurveOnClosedSurface, wire orientation is not checked and the face is marked unorientable. It is marked the same way when any wire has a status other than NoError. So 27 alone names no cause; the cause is the edge or wire status behind it.
- 33 comes from `BRepCheck_Edge` and compares an edge's polygon on triangulation with its curve. It appears only when the shape has been meshed first, so a face table taken after `--mesh` reports it even if the B-Rep itself is fine.
- Wire statuses that depend on the face (closure and self-intersection in parameter space) are stored on the face context, not in the wire's own status list. The face table now reads both (`wireOnFace...` entries in the status column).

After the forced SameParameter change, the remaining 89 invalid faces on XS650 split into about 29 InvalidPolygonOnTriangulation (a meshing artifact of the check order), about 23 wires not closed in parameter space (28), about 24 self-intersecting wires (18), and about 12 with an edge deviation (8 or 11). These counts are rounded groupings of the status strings in one run.

## How STEP import builds and repairs faces

Source: `dox/user_guides/step/step.md` (sections on tolerance and transfer), `XSTEPResource/STEP`, `ShapeFix.cxx`, `ShapeFix_Edge.cxx`.

- Edges are built from the 3D curve alone with tolerance `Precision::Confusion()` (1e-7). Their tolerance is then set to the measured deviation between the 3D curve and the pcurves.
- Per edge loop, `ShapeFix_Edge::FixSameParameter` is called. If the resulting tolerance is greater than max(1.0, 2 x basis precision), the pcurve is recomputed by projection and the better of the original and the recomputed pcurve is kept.
- `ShapeFix_Edge::FixSameParameter` on an edge already flagged SameParameter does not reparametrize it. It measures the deviation on the existing pcurves per face (`ShapeAnalysis_Edge::CheckSameParameter`) and raises the edge and vertex tolerance to the measured value. For an edge not flagged, it builds a copy, runs `BRepLib::SameParameter` on the copy, and keeps whichever of the two has the smaller tolerance.
- `ShapeFix::SameParameter` (whole shape) runs that per edge and per adjacent face, and for planes adds a 23-point check of 3D curve against pcurve.
- Tolerance limit: `read.maxprecision.val` defaults to 1 mm in "Preferred" mode (`read.maxprecision.mode = 0`): it is a limit that may be exceeded when needed to keep the shape valid.
- `read.surfacecurve.mode = 3` ("3D curves preferred") rebuilds pcurves from 3D curves, which is what the importer does for spline faces.
- After translation, `FromSTEP.exec.op : FixShape` runs `ShapeFix_Shape` with all fix modes on automatic (-1), `ModifyTopologyMode 0`, `ModifyGeometryMode 1`, `ClosedWireMode 1`, `PreferencePCurveMode 1`, `FixEdgeSameParameterMode 0`, `FixVertexPositionMode 0`, `AutoCorrectPrecisionMode 1`, and a minimum tolerance of 1e-7.

Importer comparison: the importer's rejection threshold is `kMaxVertexGap` 2.5 mm plus 0.5 mm, so it is looser than STEP's 1 mm for edge deviation. Its remaining weakness is elsewhere: it does not run the wire and face repairs that STEP gets from `ShapeFix_Shape` (reorder, connected, self-intersection, lacking, shifted pcurves on closed surfaces, missing seam). No measurement of what `ShapeFix_Shape` would repair on the XT faces has been made yet.

## How OCCT projects a 3D curve onto a surface

Source: `ShapeConstruct_ProjectCurveOnSurface.cxx`.

- Order of attempts: analytic projection (`ProjLib_ProjectedCurve`) when curve and surface allow it; for a B-spline curve with highly uneven parametrization, `ProjLib` as well; otherwise point-wise projection followed by `approxPCurve`; interpolation as a last resort.
- Point-wise projection tracks continuity: the first point uses a global search (`ShapeAnalysis_Surface::ValueOfUV`), later points start from the previous solution (`NextValueOfUV`). End points are cached by 3D position and reused by the neighbouring edge, so two edges that meet at a vertex get the same UV there.
- It detects curves lying on an iso-parametric line (boundary curves), straight pcurves, jumps across the period of closed surfaces (`insertAdditionalPointOrAdjust`), and singular points (`projectDegeneratedPoints`, `correctExtremity`).
- Not yet compared with how the importer projects pcurves for spline faces.

## Fill faces

`BRepFill_Filling` (`BRepFill/BRepFill_Filling.hxx`) takes boundary edges with an order per edge: C0, or G1 and G2 against a supplied support face (`Add(edge, supportFace, order, isBound)`). It also takes an initial surface to deform (`LoadInitSurface`), point constraints, constraint tolerances (`SetConstrParam`), resolution (`SetResolParam`) and approximation limits (`SetApproxParam`), and reports `G0Error`, `G1Error`, `G2Error`.

Every fill in the importer (`XtOcct.cpp`, both `BRepFill_Filling` constructions) adds its edges with `GeomAbs_C0`, no support face and no initial surface. A blend face is tangent to its two support faces along its contact edges, so G1 constraints against the neighbouring native faces, and possibly an initial surface taken from the blend itself, are the first untried options for the crumpled and fan-shaded fill patches.

## XDE and the viewer's XCAF path

Source: `dox/user_guides/xde/xde.md`, viewer `src/Import/XCAFDocProcessor.cxx`.

- XDE stores a part definition once as a shape without a location, and each use as a component (instance) with a location. Assemblies are labels with components. Names are `TDataStd_Name` on labels, colours are set per shape or sub-shape with `XCAFDoc_ColorTool` (generic, surface, curve), and material references use `XCAFDoc_MaterialTool` (the viewer reads the material name only, not density).
- This matches the XT structure: BODY parts, ASSEMBLY nodes, INSTANCE nodes with transforms.
- The viewer's XCAF processor converts each definition once, caches it, clones it for further instances, names nodes from labels and applies colours from the colour tool, including per face. The XT importer currently bakes every placed instance into world-space geometry, so a repeated part is copied and meshed per instance.
- A possible architecture is to build an XCAF document from the XT model (definitions, components, names, colours), mesh the definitions in the importer, and hand the document to the same processor. That would reuse the STEP behaviour for hierarchy, naming and colour and avoid one geometry copy per instance. Not evaluated; the existing mesh recovery and display-fallback logic would have to run per definition first.

## Face and wire repair order (second pass)

Source: `ShapeFix_Face.cxx` `Perform`, `ShapeFix_Wire.cxx` `Perform` and `FixLacking`.

`ShapeFix_Face::Perform` runs in this order:

1. Wire pass one, with lacking-edge and self-intersection repair switched off: `FixReorder`, `FixSmall`, `FixConnected`, `FixEdgeCurves` (add or remove pcurve or 3D curve, reversed pcurves, seam pcurves, shifted pcurves, SameParameter), `FixDegenerated`, `FixNotchedEdges` or `FixTails`, closed-wire fix. Before this pass `AutoCorrectPrecision` lowers the working precision to half the shortest edge if that is smaller.
2. `FixPeriodicDegenerated` (cones) and `FixMissingSeam`.
3. For each face that results, wire pass two with the other fixes switched off: `FixSelfIntersection`, then `FixLacking`, notches, tails. Then loop wires, wire splitting, wires with two coincident edges, intersecting wires, orientation, natural bounds, face splitting, and small-area wires.

`ShapeFix_Wire::Perform` itself orders its fixes: reorder, small edges, connected, edge curves, degenerated, notches or tails, self-intersection, lacking, then vertex tolerance.

`FixLacking` closes a gap between the pcurves of two adjacent edges in parameter space. In order of preference it bends both pcurves to meet at the middle point (`TryBendingPCurve`, accepted when the resulting 3D deviation stays inside the edge tolerances), increases the vertex tolerance when the 3D gap is below the working precision or below the maximum tolerance, adds a long 3D edge, or adds a degenerated or closed edge. The importer has no pcurve bending and runs no wire pass of this kind on faces that are not already rejected.

Where the importer already uses these tools (`XtOcct.cpp`): `ShapeFix_Face::FixMissingSeam` for periodic faces and spherical belts, a `ShapeFix_Face` self-intersection repair at precision 5e-3 on faces that BRepCheck reports as self-intersecting (disabled by `XT_NO_SI_REPAIR`), `ShapeFix_Edge::FixAddPCurve` to add missing pcurves, `ShapeAnalysis_Surface::ValueOfUV` and `NextValueOfUV` for projection, and `ShapeFix_Face::Perform` inside the fill code. No general `ShapeFix_Shape`-style pass is run.

## Surface closure and projection (second pass)

Source: `ShapeAnalysis_Surface.hxx` and `.cxx`.

- A B-spline surface is treated as closed in a direction when its edge curves coincide to within the given precision, even when it is not flagged periodic (polynomial: distance between boundary poles; rational or with non-clamped ends: distances at knots and mid-knots; extrusion: distance between the ends of the basis curve). Such faces need a seam, and a wire that goes round the closed direction is "not closed" in parameter space until the seam is inserted. This is the likely cause of the wires-not-closed group; it has not been checked per face.
- `ValueOfUV` for a B-spline surface runs `Extrema_ExtPS` over the surface range widened by one parametric tolerance on closed surfaces, takes the nearest of all minima, refines by Newton iteration, and checks the iso-lines near the boundary (`UVFromIso`) to avoid wrong boundary solutions. `NextValueOfUV` runs Newton from the previous point and falls back to `ValueOfUV` near C0 knots or when the result looks like a strange attractor.

## SameParameter variants (second pass)

Source: `BRepLib.hxx`, `ShapeFix.cxx`, `ShapeFix_Edge.cxx`, shape healing guide (operator `SameParameter`).

- `BRepLib::SameParameter(shape, tol, forced = true)` marks every edge as not SameParameter and recomputes its pcurves to the 3D parametrization. It does not compare the result with the original.
- `ShapeFix::SameParameter(shape, enforce)` (the operator STEP uses, with `Force` for `enforce`): for edges flagged SameParameter it only measures and raises tolerance, with no geometry change. For edges not flagged, or all edges when forced, it runs `BRepLib::SameParameter` on a copy and keeps whichever of the original and the recomputed edge has the smaller tolerance. It also raises vertex tolerances to the edge tolerance.
- The forced `BRepLib` call currently used by the importer therefore recomputes pcurves for every edge, including edges whose authored or projected pcurve was already within tolerance. `ShapeFix::SameParameter(shape, true)` would choose per edge. Not yet measured.

## Surface construction tools (second pass)

Source: `modeling_algos.md`, `GeomFill`, `GeomPlate`.

- `GeomFill_ConstrainedFilling` fills a region bounded by three or four curves with optional tangency constraints from adjoining surfaces (`GeomFill_BoundWithSurf`), in Stretch, Coons or Curved style. It suits a corner patch between blends.
- `GeomPlate_BuildPlateSurface` with `BRepFill_CurveConstraint` (order 0 to 2 per curve) builds a plate surface, converted to B-spline with `GeomPlate_MakeApprox`. `BRepFill_Filling` wraps the same machinery.
- `GeomFill_Pipe` sweeps a section along a path (constant circular section, constant section, or evolving section).
- `BOPTools_AlgoTools::BuildPCurveForEdgeOnFace`, `AttachExistingPCurve` and `CorrectTolerances`, and `BRepLib::FindValidRange`, are further helpers for pcurves and tolerances.

## Upgrade tools (second pass)

Source: shape healing guide, upgrade and shape processing sections.

- `ShapeUpgrade_ShapeDivideClosed` (operator `SplitClosedFaces`) splits closed faces, including closed or periodic B-spline surfaces, into pieces so no seam remains; `NbSplitPoints` sets the number of pieces. Splitting changes face counts and edge sharing and needs `ShapeFix::SameParameter` afterwards.
- `ShapeUpgrade_ShapeDivideContinuity` splits geometry to a target continuity; `ShapeUpgrade_ShapeDivideArea` splits large faces.

## Data Exchange wrapper and IGES (second pass)

- `DE_Wrapper` (`dox/user_guides/de_wrapper`) puts each format behind a provider and a configuration node, reading into a shape or an XDE document, with settings from code or a resource string. A Parasolid reader could follow the same shape (read into a shape or a document, settings in one configuration object).
- IGES reading builds each loop from 3D and 2D curves and passes the wire through `ShapeFix_Wire` at construction time, with working precision from the file and `MaxTol` of max(1 mm, precision), before a final `ShapeFix_Shape`. Loops from XT have the same inputs (3D curve and pcurve per fin), so the same build-time wire repair applies.

## Experiments proposed from this study

Each is small and can be measured with the standalone tool before any viewer build. Report counts of invalid faces, dropped faces, area change against the STEP reference, and build time.

1. Run `ShapeFix_Face::Perform` with the STEP parameter set on the faces still invalid after the build, and count how many become valid, change area, or are dropped.
2. Replace the forced `BRepLib::SameParameter` with `ShapeFix::SameParameter(shape, true)` and compare tolerances, validity and time.
3. Compare the importer's pcurve projection on spline faces with `ShapeConstruct_ProjectCurveOnSurface` on the same edges: deviation and UV continuity at vertices.
4. Rebuild the fill faces with G1 constraints against the neighbouring native faces, with and without an initial surface, and compare shape and `G1Error` with the current C0 fills.
5. For wires not closed in parameter space on closed B-spline faces, test `ShapeUpgrade_ShapeDivideClosed` and `ShapeFix_Wire::FixLacking` with bending.
6. Evaluate the XCAF-document architecture on a small assembly, measuring load time and memory against the current baked path.

### Outcome of the experiments run so far

Experiments 1 (shape-fix pass) and 2 (SameParameter variant) were run, and two further experiments suggested by the specification study (exact intersection refinement and contact curves from the exact spine). Results and regeneration switches are in the [specification study notes](PARASOLID_SPEC_STUDY_NOTES.md#experiments-after-the-study-17-18-november-2026). Not run: 3 (pcurve projection comparison), 4 (G1 fills), 5 (`FixLacking` bending and closed-face splitting), 6 (XCAF document architecture). Experiment 1 showed that `ShapeFix_Face::Perform` repairs roughly half of the faces that remain invalid (51 of 90 on the XS650-class engine) without dropping any; it has not been integrated.
