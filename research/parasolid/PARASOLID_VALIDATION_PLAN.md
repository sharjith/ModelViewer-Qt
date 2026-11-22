# Parasolid X_T validation plan and acceptance table

15 November 2026, branch `feature/parasolid-importer`. Defines how each stage in [PARASOLID_IMPLEMENTATION_PLAN.md](PARASOLID_IMPLEMENTATION_PLAN.md) is checked. Samples are local only and referenced by neutral ID (first 12 hex digits of SHA-256); see [PARASOLID_XT_FORMAT_COVERAGE.md](PARASOLID_XT_FORMAT_COVERAGE.md). Owner acceptance is **not yet recorded** for anything below.

## 1. Principles

- Separate four claims: transport decoded; source graph consistent; OCCT shape valid; shape *correct*. A file passes a stage only for that stage.
- An independent oracle is needed for correctness. Same-model STEP (or IGES) from the producing CAD system and authored dimensions are independent. A second decoder written from the same reference is not. OCCT checking itself, or STEP round-trip through our own build, is supporting evidence only.
- Never exclude or weaken input to hide a failure. Record unsupported content with counts instead.
- The owner builds and runs tests. This plan names checks; it does not run them.
- No sample, derived export, image or model name enters tracked files. Evidence goes to `out/parasolid-research/`.

## 2. Check families

| ID | Check | Applies from | Independent? |
|---|---|---|---|
| T1 | Transport: header, prefix, edit scripts, every record consumed, terminator at end of stream, zero stray tokens | X1 | Yes (stream length is an exact invariant) |
| T2 | Hostile input: truncation at every record boundary, huge counts, bad edit op, unknown required type, missing terminator, deep/cyclic chains | X1 | Yes |
| G1 | Graph: reference resolution (only documented-ignorable dangling pointers), fin rings closed, each non-wireframe edge has two fins, loop/face/shell/region/body ownership | X2 | Mostly (invariants from the reference) |
| G2 | Counts: faces, edges, vertices, bodies, instances vs. an independent recount (STEP entity counts or a second pass in another language) | X2 | Partly |
| S1 | `BRepCheck_Analyzer` valid, expected closure only for solids | X3 | No (supporting) |
| S2 | Bounding box, surface area, volume vs. authored values and same-model STEP, stated tolerance | X3 | **Yes** |
| S3 | Sampled distance between faces/edges and the STEP reference. For interpolated curves report the source chart error and the measured reconstruction error as two separate numbers | X4–X5 | **Yes** |
| S4 | Tolerance and healing log: maximum tolerance, shapes touched, no unbounded inflation | X3– | Supporting |
| A1 | Instance count, placement matrices, shared-part reuse vs. the file's own graph and STEP assembly | X6 | **Yes** |
| A2 | Colour and name present, resolved by the documented precedence, against a control with known colours/names | X6 | **Yes** |
| U1 | Units: control with known dimensions | X6 | **Yes** |
| V1 | Viewer: loads, orbits, cancels, reloads, memory and time budget | X6 | Owner |

## 3. Acceptance table for the local samples

"Must load" means the stated stage's check passes. Entries are targets for the stage, not current results; the **current** transport column is from the probe run of 15 November 2026 (see the coverage document). `—` means no check is meaningful yet.

| ID | Size | Role | Current T1 | Target stage | Must load / pass | Extra checks |
|---|---:|---|---|---|---|---|
| XT-e62b10ff0279 | 53 KB | Small embedded-schema reference, Onshape kernel 38 | Pass | X3 | One solid body of mostly analytic faces; two intersection curves; S1, S2 vs. owner-authored dimensions and same-model STEP (to be obtained) | Colour/name attribute meaning (A2) |
| XT-8ffda52db5e5 | 0.9 MB | Solid Edge, old kernel 18 | Pass | X3–X4 | 28 solids, assembly of 29 instances; S1; A1 | Mixed header (no format key, local-language date) |
| XT-817da15a5a8f | 7.5 MB | SolidWorks 2020 assembly | Pass | X4–X6 | 26 solids, 140 instances; S1–S3; A1, A2 | Blends and B-splines |
| XT-b774f7febff6 | 18.9 MB | Solid Edge, kernel 28 | Pass | X4–X6 | 54 solids, 152 instances; helix forms; S1–S3 | Group nodes present |
| XT-097375059234 | 23 MB | SolidWorks 2018 | Pass | X5–X6 | 75 solids, 2 sheets, 89 instances | Mixed solid/sheet |
| XT-380a3dbf46c9 | 24.9 MB | SolidWorks 2016 assembly | Pass | X5–X6 | 233 solids, 993 instances; heavy sharing | Instance stress; helix forms |
| XT-8ea8be98e927 | 87.8 MB | Unigraphics-header, 1,767 sheet bodies | Pass | X6 | Sheet versus solid handling; A1; V1 memory/time | Instances mostly without transform |
| XT-5268a1dca7d6 | 115.6 MB | Unigraphics-header, very many blends | Pass | X5–X6 | V1 on a heavy model; blend policy (G5) | Largest decoded stream (1.4 M records) |
| XT-ed43d4c8caf1 | 58 MB | SolidWorks 2019 | **Stops: undocumented node type** | X1 closure, then X6 | T1 must pass once the type is known; otherwise a precise refusal | Used to prove refusal wording |
| XT-950660d6bedb | 12.2 MB | SolidWorks 2025, legacy key | Not started | X7 | Refusal with a precise diagnostic until X7; then T1, G1 | Do not mislabel as supported |
| XT-676618a88448 | 23.8 MB | SolidWorks 2025, legacy key | Not started | X7 | As above | |
| XT-79bc2cd92b51 | 123 MB | SolidWorks 2012, legacy key | Not started | X7 | As above; V1 for the largest file | |

Weakest points of this table: none of the 12 files has an independent geometry reference yet, so S2, S3, A1 (STEP comparison), A2 and U1 cannot be run on them. This is the same evidence gap Codex recorded.

## 4. Missing evidence (document, then ask; do not fabricate)

Needed before the stated checks are meaningful. Each item is a **request to the owner**:

1. **Dimensioned controls with same-model STEP**: a box; a box with a through-hole; a cylinder-and-cone part; a torus/sphere part. Ideally exported from the same producer families as the local files (SolidWorks, Solid Edge, Onshape, NX). Record exporter and version, authored dimensions and units. Closes S2 and U1.
2. **Instance-transform control**: an assembly with two identical parts, one translated, one rotated, one **mirrored**, and one scaled if the exporter allows. Closes A1 and the transform gap.
3. **Colour/name control**: an assembly with named parts and known face/part colours, including a body colour overridden on one face. Closes A2.
4. **Wire, general and sheet controls** (small): to confirm body-type handling.
5. **A re-export of a legacy-key model with a current embedded schema** (the same geometry as one of the three legacy-key files if the owner can): decides whether X7 is inference or exchange.
6. **A neutral-binary `.x_b` pair** of one of the controls (for X8).

## 5. Regeneration commands (all write only under `out/`)

Prefix inventory (Codex, tracked):

```powershell
New-Item -ItemType Directory -Force out/parasolid-research | Out-Null
python tools/parasolid/inspect_xt.py "<local-samples>" > out/parasolid-research/inventory.json
```

Transport decode (tracked tool; needs the local untracked layout file described in its README):

```powershell
python -I tools/parasolid/xt_decode.py out/parasolid-research/layouts-v13.json "<local-samples>" > out/parasolid-research/decode.json
```

Once X1 exists, the tracked inventory tool and tests replace the probe. Tests belong in `tests/parasolid/`, fixtures generated there, synthetic only.

## 6. Comparison with the existing paths

- **STEP/IGES**: these go through the existing XCAF processors and produce `aiScene` after tessellation. The XT path reuses the same stage, so *tessellation, normals, colour and instancing* are common and are not re-validated per format beyond the checks above. The XT-specific risks are exactly those that come before tessellation: schema decode, blend and intersection geometry, tolerance. This is why the STEP exports of the same models are the main oracle.
- **JT**: the JT importer reads stored tessellation and proves the display pipeline. Exact B-Rep from XT is a separate achievement. Where a model exists in both JT and XT, the JT result is a **visual** cross-check only.
- **Open Cascade's commercial Parasolid reader**: not available here. Its guide is a design reference (provenance document), not a measurement. Comparing our result against it would need a licensed evaluation, which this plan does not assume.

## 7. Acceptance records

None yet. When the owner confirms a gate, add a dated row here with the build, the checks run, and any exceptions.

| Date | Gate | Build/commit | Result | Exceptions |
|---|---|---|---|---|
| | | | | |
