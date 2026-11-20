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
