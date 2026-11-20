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
