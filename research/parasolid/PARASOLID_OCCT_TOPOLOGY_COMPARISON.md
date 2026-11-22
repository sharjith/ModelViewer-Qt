# OCCT STEP and IGES topology construction compared with XT

15 November 2026, branch `feature/parasolid-importer`.

## Provenance and scope

Read the local vcpkg OCCT 8.0.1 source tree extracted from the [OCCT V8_0_1 release](https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1). The upstream tarball SHA-256 is `0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a`. The local paths below are relative to that OCCT source root. The comparison also uses the current ModelViewer XT code, the established local XT reference and the existing engine-sample measurements. No third-party translator code was copied or executed. No CAD runtime, build or test was run. [STEP translator guide](https://occt3d.com/dev/doc/overview/html/occt_user_guides__step.html) · [IGES translator guide](https://occt3d.com/dev/doc/overview/html/occt_user_guides__iges.html)

## Observed translator sequence

| Layer | STEP | IGES B-Rep | Implication for XT |
| --- | --- | --- | --- |
| Edge identity | `StepToTopoDS_TranslateEdge` builds an edge from vertices, the 3D curve and its parameter limits, and binds the result to the STEP edge. Edge-loop uses carry orientation. | `IGESToBRep_BRepEntity::TransferLoop` obtains the shared edge from the edge list, applies use orientation and transfers any 2D representation back to the shared edge. | Preserve one edge definition and explicit fin uses. An edge's pcurve belongs to a particular face use and support chart. |
| Trim data | `StepToTopoDS_TranslateEdgeLoop` reads usable authored pcurves, handles both branches of seam edges, sets their face-specific ranges, and checks them. Projection is available when a representation is unavailable or rejected. | `IGESControl_IGESBoundary` reconciles supplied 2D and 3D representations under translator precision and maximum tolerance. It can retain 2D input and invoke `FixSameParameter` on individual edges. | Read the fin's SP-curve, trim limits, orientation and support identity before choosing a 3D edge representation. Attach a qualified authored pcurve before using projection. |
| Wire assembly | `StepToTopoDS_TranslateEdgeLoop` marks closure from the resulting topology, using `BRep_Tool::IsClosed`. The face translator considers surface sense and face-bound orientation independently. | `IGESToBRep_BRepEntity::TransferLoop` accumulates oriented uses and can create a degenerate edge explicitly from a vertex-list item. | Check the resulting wire as well as source fin traversal. Retain distinct orientation and seam branches. Handle poles explicitly. |
| Shell and solid | The shell/solid paths assemble already-translated faces and apply transfer tolerances. | IGES collects oriented face results in its shell/solid transfer and applies model unit/precision settings. | Assemble only after local edge/face checks. Track tolerances before and after reconciliation and keep visualization recovery separate from qualified B-Rep. |

Source locations:

- `src/DataExchange/TKDESTEP/StepToTopoDS/StepToTopoDS_TranslateEdge.cxx`, especially the 3D edge creation and source binding.
- `src/DataExchange/TKDESTEP/StepToTopoDS/StepToTopoDS_TranslateEdgeLoop.cxx`, especially lines 686–787 for pcurve attachment, 827–875 for closure, range and checks.
- `src/DataExchange/TKDESTEP/StepToTopoDS/StepToTopoDS_TranslateFace.cxx` and `StepToTopoDS_Builder.cxx` for face bounds, orientation and shell/solid assembly.
- `src/DataExchange/TKDEIGES/IGESToBRep/IGESToBRep_BRepEntity.cxx`, particularly `TransferLoop`, `TransferFace` and `TransferShell`.
- `src/DataExchange/TKDEIGES/IGESControl/IGESControl_IGESBoundary.cxx`, particularly the 2D/3D reconciliation in `Transfer`.

## First XT change indicated by the comparison

Replace the lift-and-reproject default for supported authored SP-curves with a per-fin attachment path. Start with plane and cylinder charts, whose length scaling is already understood. Decode and normalize the 2D spline and its range, verify that the fin belongs to the expected face and edge and that the support chart matches, compare endpoints and curve-on-surface residuals to the 3D edge, and attach it to the shared edge with the face-specific range. Preserve both uses of an edge at a seam when two valid branches are supplied. A tolerant edge with only surface-space geometry may still need a fitted 3D curve. Its authored face-local curve can stay in use while the 3D approximation is qualified. Any alternate chart, such as the spherical stall fix, needs an explicit mapping before the original pcurve can be transferred.

Review the XT face and wire build after this change. It presently calls `wire.Closed(true)` after the source ring walk, generates pcurves through `FixAddPCurve`, then reconciles at body level with `SameParameter`. Move the endpoint/range consistency checks to the edge-use stage, record authored-versus-projected counts and measured residuals, and report the exact repair on an edge before its tolerance affects other faces. Keep the existing mismatch, extent and cancellation protections.

The first owner comparison should use the same engine sample and track authored pcurves accepted, pcurve/wire failures, total reconstruction omissions, remaining display recovery, and load time. A same-model STEP export or authored paired control can establish expected geometry and trims. STEP/IGES topology construction provides sequencing and OCCT API examples. It cannot supply a missing XT rolling-ball contact, choose an XT intersection branch, or validate a fabricated approximation. That work remains in the XT geometry evaluator.

## Plane and cylinder implementation

The XT face builder now checks fin-authored SP-curves on matching plane and cylinder support records. It scales the spline poles into the OCCT chart, uses the authored trim range, and checks both endpoints plus interior samples against the bounded 3D edge. The interior checks test residual distance and progression along the edge. The tolerance comes from the edge tolerance clamped between 0.02 and 0.5 mm for the default metre-to-millimetre import scale. The samples are a qualification check, with no continuous error bound.

An accepted pcurve is attached in the direction of the 3D edge and retains its own range until parameter reconciliation. The face gets a copy of the analytic support so its pcurve on the shared edge cannot replace a neighbour's curve through a common OCCT support key. Missing and rejected candidates use the existing projection path. Edges with two uses on a face also use projection in this pass. They need two qualified branches that share a parameter range. Plane and cylinder wire flags now reflect OCCT's closure check, and open results are counted and traced.

The implementation also consulted OCCT 8.0.1's `BRep_Builder`, `BRep_Tool`, `Geom2d_BSplineCurve`, `GeomAPI_ProjectPointOnCurve` and `ShapeFix_Edge` sources and declarations to verify the public API contracts. The source identity and hash are listed above. No upstream implementation was incorporated.

Owner validation is pending. Rebuild and reload `XT-097375059234` using the [viewer trace command](PARASOLID_PERIODIC_TRIM_INVESTIGATION.md#persistent-face-loss-and-a-stalled-larger-sample) and compare the `XT trim construction` log line with the B-Rep omission and mesh-recovery counts from the same run. The new line counts attachment and projection activity before face validation and shell assembly. A rise in attachments alone does not establish that missing faces were recovered. Also reload `XT-5268a1dca7d6` to check for a return of the import stall.
## STEPCAF transfer and an XT edge-repair control

16 November 2026, branch `feature/parasolid-importer`. This follow-up used the same local OCCT 8.0.1 extraction and archive hash recorded in the provenance section. The additional source files were `TKDESTEP/STEPCAFControl/STEPCAFControl_Reader.cxx`, `TKDESTEP/STEPControl/STEPControl_ActorRead.cxx`, `STEPControl_Reader.cxx`, `STEPControl_Controller.cxx` and `TKXSBase/XSAlgo/XSAlgo_ShapeProcessor.cxx` under `src/DataExchange`. No upstream implementation was incorporated.

- `STEPCAFControl_Reader::Transfer` calls `STEPControl_Reader::TransferOneRoot` before adding transferred shapes to XCAF. The STEP transfer actor supplies precision and maximum tolerance to `XSAlgo_ShapeProcessor`. The STEP reader enables `FixShape` as a transfer operation, and the controller defaults disable vertex-position modification.
- `StepToTopoDS_TranslateEdgeLoop::CheckPCurves` drops bad ranges, handles seam uses and discards explicit planar pcurves so OCCT can derive them from the 3D edge. `XSAlgo_ShapeProcessor::CheckPCurve` checks endpoints, then trials `FixSameParameter` on a fresh edge made from the 3D curve. It can try projection on another fresh edge and compare the results before updating the shared edge. STEP's precision choices are specific to its transfer context.
- XT currently qualifies some pcurves directly on the face, then reconciles the assembled body. The late residual check can identify a face with usable geometric representations, but the tolerance setters used there only increase tolerances. Trial reconciliation before the shared edge is updated would avoid contaminating adjacent faces. The plane path is a useful diagnostic control because a projection there has no periodic branch choice.

First isolate the two adjacent faces around one edge implicated by the engine trace. Build a temporary edge with fresh vertices for each candidate, attach only the relevant face's authored trim or projected trim, and keep both branches together for a seam. Compare endpoint residuals, 3D-to-UV agreement and the resulting tolerance against the XT import budget before accepting a candidate. Preserve the original shared edge identity in the shell, and verify loop closure, hole retention and neighbouring-face validity after committing its pcurves. Run the same topology checks on a STEP export of the same part if available. Track native B-Rep faces, free edges on expected-closed shells, triangulation failures and elapsed time separately from boundary fills. Surface-construction failures, including unresolved blend contacts, require geometry work before these checks can succeed. Owner build and validation are pending.

