# Inventor assembly (IAM) baseline

20 November 2026 · `feature/inventor3d`

Status: observation only. No production IAM code exists yet. The only IAM reader is the ignored experimental A01 prototype under `out/inventor-research/iam-wip/` (source and executable stay local). Production IAM integration was paused while the IPT foundation was qualified; that foundation now reads 73 of 84 corpus parts ([IPT working specification](INVENTOR_FOUNDATION.md)).

## Prototype baseline on the local corpus

Method: the prototype's `--assembly` mode run on all 33 local IAM files; output kept locally under `out/inventor-research/foundation/iam_survey_proto.txt`. Regenerate per file with:

```powershell
& out/inventor-research/iam-wip/inventor_assembly_probe.exe --assembly <file.iam> > out/inventor-research/foundation/iam_<id>.json
```

| Outcome | Files | Reason reported |
| --- | --- | --- |
| Reads | 2 flat assemblies (+ the A01 control, outside the corpus) | none |
| Component file not found on disk | 4 | a referenced part is not shipped with the sample (standard-library or old-version references) |
| Occurrence list incomplete | 9 | template assemblies; the prototype expects every component to carry a placement |
| Occurrence reference inconsistent | 9 | weldment template assemblies |
| Component name or number inconsistent | 8 | pump, valve and motor assemblies, several of them nested |
| Graphics segment layout unsupported | 1 | different graphics node layout |

Observations.
- The prototype demands that every saved component bind to exactly one file and carry a rigid placement. Real assemblies break each of those assumptions (nested sub-assemblies, template containers with no geometry, patterns, library parts that are not shipped).
- The prototype reads decompressed segments into a fixed 8 MB buffer in its Python reference and a 64 MB limit in C++; the plan already notes that some Zstandard frames omit the decompressed size.
- Occurrence placement, names and visibility for flat assemblies were verified against one Inventor-generated report (A01). Nested transforms, suppression, pattern instances and overrides have no reference yet.

## Evidence needed

- One small nested assembly with a report from Inventor (or a paired STEP assembly) giving each level's placement, to qualify composition of transforms.
- A statement from the owner on the policy for unresolved references (skip with a warning, or fail the whole import).

Open questions and next steps are in [the implementation plan](INVENTOR_IMPLEMENTATION_PLAN.md), stage 5.

## Graceful failure: error nodes in the model tree, 20 November 2026

Decision (owner): a part or body that cannot be loaded must not fail the whole import. It is shown in the model tree as an item with an error or warning symbol, and hovering it shows the reason. Skipping is the default; a strict setting restores all-or-nothing, as the 3DXML importer already offers. A file with no buildable body at all still fails with the usual error dialog.

Design, implemented for Inventor parts (bodies) and reusable by the future assembly reader and by other importers.
- Data. `SceneNode` carries an `ImportIssue` (none, warning or error, plus a message). It is saved in the MVF JSON as an optional `importIssue` object (older files lack it) and in the session stream (version 5; version 4 is still read).
- Transport. An importer records the issue as node metadata on the `aiNode` it creates (`ModelViewer.ImportIssue.Severity` = `error` or `warning`, and `ModelViewer.ImportIssue.Message`). `SceneGraph::buildSubtree` turns it into the `SceneNode` field, so no importer needs to know about the scene graph.
- Part reader. `Inventor::readPartBodies(path, cancelled, appearance, strict)` returns one outcome per body (a solid, or the error text). A body with several lumps reports each lump separately. Cancellation and running out of memory are never skipped. `readPartSolids` is unchanged and strict.
- Loader. Each failed body becomes an empty child node of the part's root with the metadata above, and a line in the import warnings list ("Last Import Warnings...").
- Tree. A node with an issue is always shown, even without meshes. A mesh-less issue node becomes a placeholder item: greyed mesh icon with a warning or error mark, the reason as a rich-text tooltip, no check box, selectable but not editable. Parents ignore it when computing check boxes, selection closure and visibility, and it is not offered assembly actions in the context menu. An assembly node that carries an issue gets the mark and tooltip on its normal icon.
- Setting. Settings, Import Export, Inventor Importer, "Strict"; `QSettings` key `inventorStrict`; environment override `MODELVIEWER_INVENTOR_STRICT=1`.
- Test hook. `MODELVIEWER_INVENTOR_SIMULATE_FAILED_BODY=n` makes the n-th body of a part with several bodies count as failed. It exists only so the tree item can be exercised on a corpus part that otherwise loads; it has no effect when unset.

Status. The viewer sources were not built or run by the agent; the headless probe still reports 73 of 84 corpus parts valid in strict mode and no volume changed. Pending owner check: tree item, tooltip (including in the detached overlay tree), check-box behaviour of the parent, save and reopen as MVF, and the settings entry.

### Extension to the JT and 3DXML importers (MOD-16), 20 November 2026

The same mechanism is used by the other two assembly importers, through the same node metadata keys, so the scene graph and the tree needed no further change.
- 3DXML. A representation that cannot be read was already skipped (unless strict) with a warning, leaving its occurrences without geometry. The scene builder now remembers why each skipped representation failed, and the node of every reference that uses it records the reason as an import issue ("The 3D representation could not be read and was skipped: ..."). Strict behaviour and the settings entry are unchanged. Test hook: `MODELVIEWER_3DXML_SIMULATE_FAILED_REPRESENTATION=n` makes the n-th representation count as unreadable; it has no effect when unset.
- JT. Before this change an unreadable, missing or cyclic external part file failed the whole import and the JT importer had no skip option (the skip-or-fail setting for JT was decided in October but not built). A part file that cannot be read is now rolled back with the builder's existing checkpoint, a placeholder node carries the reason, and a warning is added; the rest of the assembly loads. `Options::strict` (default false) restores the old all-or-nothing behaviour; it is set from Settings, Import Export, JT Importer, "Strict" (`jtStrict`) and `MODELVIEWER_JT_STRICT=1`. Cancellation and out-of-memory are never skipped. Failures inside a loaded part (rejected LOD alternatives, rejected exact geometry) keep their existing handling and are not new error nodes.

Behaviour change to note: a JT assembly with an unreadable part file used to fail to open and now opens without that part, with the error item and a warning, unless Strict is on.

Status: unbuilt. Pending owner check on a JT assembly with one part file temporarily renamed, on 3DXML with the test hook, and on files that load normally.

### Incomplete bodies shown as an open surface (MOD-17), 20 November 2026

Decision (owner): when a body cannot become a solid but some of its faces can be built, show those faces, marked incomplete, instead of an error node with nothing to see. Strict still means all-or-nothing.

Design and status.
- Part reader. In the plain single-shell path, when not strict, a face that fails to build no longer aborts the shell: the rest of the faces are still built and the failures are counted. If the shell then cannot be closed into a solid (a failed face, an unpaired edge, an unclosed sew, a failed solid check) the built faces are returned as a compound of faces in `BodyOutcome::incompleteShape`, with a message such as "25 of 1345 faces could not be built (first: ...). The other faces are shown as an open surface; it has no volume." No sewing is needed to show them. Multi-shell lumps (a solid with voids) and the pieces of a pinched shell are not shown partially, because a partial outer shell next to a void would mislead; those still fail as a whole.
- Loader. An incomplete body is converted and displayed like any other; its node gets a warning-severity import issue with the message, so the tree shows the warning mark and tooltip, and the message is added to the import warnings. A body with nothing buildable is still an error node.
- Probe. `inventor_compound_probe --part-bodies` reports every body as solid, incomplete (bounding box, faces, no volume) or failed, without stopping at the first failure.
- Result on the oversized local part (strict mode rejected it whole): 41 outcomes in 20 s at a 2.1 GB peak: 39 valid solids, one incomplete surface with 1 320 of 1 345 faces (25 faces failed, the first on a vertex tolerance of 0.126 mm), and one body that failed (an edge whose endpoints disagree with the saved trim parameters). The face failures counted earlier were therefore concentrated in two bodies. Strict-mode corpus results are unchanged: 73 of 84 valid, no volume changed.
- Existing behaviour on an open surface. Mass properties already report "open surface" instead of a volume, so no wrong number is shown. Stencil section capping works on the triangle mesh and assumes a closed one, so a cap on an incomplete surface may look wrong; that is the same as for any open mesh. Solid-only exports are not specially guarded.

Pending owner check: the oversized part in the viewer (error node, warning-marked surface, solids), mass properties on the surface, strict mode.

### Lumps with many shells and pinched pieces: the oversized part really is a machine, 20 November 2026

Observation (owner screenshots, then a structure count). The first lenient load of the oversized local part showed only a doughnut-shaped body 4.4 m across and a few small parts, nothing like the machine Autodesk's online viewer shows. A structure count (temporary diagnostic, removed) gave: 2 bodies, 41 lumps, 1 067 shells, 86 466 faces; two lumps hold several shells, one of them 1 025, and the largest shell has 76 794 faces.

Causes and changes.
- The multi-shell rule assumed a lump with several shells is one solid with voids (largest shell outside, the rest inside), so the 1 025-shell lump failed as a whole and took the machine with it. Shells are now classified by the signed volume of their faces under the saved face senses (computed without sewing): positive is an independent solid; negative has its normals pointing into a cavity. A negative shell that lies inside a positive solid (bounding box and a point-in-solid test) becomes that solid's void; a negative shell inside nothing is an independent part, already oriented outward. A part with a real void (a 5-face and a 92-face shell) still gives exactly its earlier volume. A shell that fails is reported on its own instead of failing the lump.
- The 76 794-face shell contains pinched circles, so it is built as pieces. Pieces were built with the all-or-nothing path, so the first bad face aborted the whole shell. Each piece now stands on its own: a piece that cannot be built is reported in its place, as an incomplete surface when some of its faces built.
- Result (lenient, headless): 1 080 bodies in 326 s at a 2.6 GB peak: 997 solids (7 841 faces), 13 incomplete surfaces (75 342 faces; the largest has 65 034 faces with 2 103 failing), and 70 failed bodies ("non-manifold edge at tangent contact", not yet examined). That puts 83 183 of the 86 466 faces (96%) on screen. Strict-mode corpus results are unchanged (73 of 84, no volume changed).

Still different from Autodesk's viewer, not addressed here: colours (appearance is only read for single-body parts, so this part is grey); the component structure and names of the machine (the saved bodies are flat); the 4.4 m doughnut-shaped body (9 faces, 46 litres), which may be reference or hidden geometry whose visibility flag is not read; and the 13 incomplete and 70 failed bodies.

### The oversized part is a derived surface: open surface bodies, 20 November 2026

Observation (owner screenshots of Autodesk's online viewer). The viewer's model browser lists only two items for this part: a solid body and a derived surface ("SRF1::Derived" containing "SRF5"). Owner correction (second screenshot pair): the solid body is the vehicle (frame, tracks, bucket, hoses) and the derived surface, SRF5, is the engine only. The earlier reading that the whole machine was the derived surface was wrong; the open sheets (29 bodies) are most likely the engine and the 1 049 solids the vehicle, not yet verified per body. This matches the 2 saved body records. Names, grouping, colours and body visibility are not in the B-rep segment (the names were searched for in UTF-8 and UTF-16 and are absent); they live elsewhere in the file and are not read yet (MOD-21). The saved face flag for double-sided faces is false on all 86 466 faces, so a sheet body is not marked on its faces: it is a shell of single-sided faces that does not close.

Consequences and changes.
- A derived surface is made of open sheets, which can never become solids. The reader was reporting them as problems. In the lenient path a shell whose faces all built but which does not enclose a volume is now an open surface body (`BodyOutcome::isSurface`, no error, no warning marker); one line in the import warnings states how many bodies are open surfaces. A shell where some faces failed to build stays "incomplete" with the warning, and a body with nothing buildable stays an error node. Strict mode is unchanged and still rejects sheets.
- Sheet bodies legitimately break two solid-only rules, so in the lenient path both are relaxed: neighbouring faces may traverse an edge in the same direction (the "inconsistent edge orientation" face failure), and an edge shared by more than two faces is allowed. When the solid topology rules reject a shell before any face is built, its faces are built on their own and shown as a surface.
- Result (lenient, headless): 1 080 bodies in 331 s at a 2.7 GB peak: 1 049 solids (8 721 faces), 29 open surfaces (11 366 faces), 2 incomplete bodies (64 726 faces, with the two known face problems: a saved vertex tolerance of 0.126 mm and an edge whose endpoints disagree with the saved trim), and no failed bodies. Strict-mode corpus: unchanged, 73 of 84.

Still different from Autodesk's viewer: colours (MOD-21), the component structure (two nodes), body visibility (the 4.4 m doughnut-shaped body), and the two incomplete bodies (MOD-5 and MOD-6).
