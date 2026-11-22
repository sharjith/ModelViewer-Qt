# Parasolid X_T inventory and implementation gates

15 November 2026, branch `feature/parasolid-importer`, based on `dev` at `694f8cd`.

## Provenance

The prefix inspector was written from the Siemens *Parasolid XT Format Reference*, April 2008, printed pp. 5–8 and 13–17, and checked on local files. The locally retained PDF has SHA-256 `6a1c3b48aac8577dac96fda4a24b2aca460ff8897fa04915487a93af9ed7792a`. The publisher's current product page describes XT data exchange. The October assessment records the unresolved standalone distribution question. [Siemens product page](https://www.siemens.com/en-gb/products/plm-components/parasolid/data-access-translation/) · [Siemens reference, mirrored PDF](https://ww3.cad.de/foren/ubb/uploads/schulze/XT_Format_April_2008_tcm73-62642.pdf) · [October assessment](PARASOLID_IMPORT_RESEARCH.md)

For candidate selection, the locally saved parasolid-kit profile notes from snapshot `2bc959687c8ca87145c4e0594828c3fa11aab912` describe an Onshape V30 analytic subset under internal key `SCH_3000000_30000`. The notes report comparison with paired producer exports. Those results belong to the upstream project. No third-party code or schema tables were copied or executed. No CAD application, private SDK, restricted schema catalog or disassembly was used in this pass. This records research method only. [Pinned profile notes](https://github.com/monozukuri-ai/parasolid-kit/blob/2bc959687c8ca87145c4e0594828c3fa11aab912/docs/builtin-profiles.md)

## Measured corpus

The standalone inspector reads at most 64 KiB of each file for prefix analysis, streams the file for SHA-256 and emits a path-free inventory. It strips physical text wrapping and reads the internal strings by their transmitted lengths. Its grammar covers the observed ASCII prefix forms. Successful prefix inspection leaves schema edits, record boundaries and geometry unqualified.

```powershell
New-Item -ItemType Directory -Force out/parasolid-research | Out-Null
python tools/parasolid/inspect_xt.py "<local-samples>" > out/parasolid-research/inventory.json
```

All 12 files in this inventory yielded a text prefix. Nine internal keys end in `_13006`, which the 2008 reference describes as a base-schema suffix. Their first integer after the internal key ranges from 186 to 249 and is reported as the declared node-type limit. Three files use unsuffixed internal keys with a zero user-field size. Two of those advertise the same newer modeller version while carrying an older internal schema key. The complete internal key is the candidate dispatch key. The common header and modeller version remain useful diagnostics.

| Sample | Bytes | Common schema | Internal schema |
| --- | ---: | --- | --- |
| `XT-e62b10ff0279` | 53,282 | `SCH_3801231_38101` | `SCH_3801231_38101_13006` |
| `XT-8ffda52db5e5` | 884,175 | `SCH_1801333_18106` | `SCH_1801333_18106_13006` |
| `XT-950660d6bedb` | 12,207,763 | `SCH_3601212_36001` | `SCH_800000_8008` |
| `XT-676618a88448` | 23,832,787 | `SCH_3601212_36001` | `SCH_800000_8008` |
| `XT-79bc2cd92b51` | 122,955,132 | `SCH_2400216_20000` | `SCH_900000_9008` |

Each ID contains the first 12 hex digits of the file SHA-256. The inventory records the complete hashes and all 12 rows. The smallest sample has SHA-256 `e62b10ff0279e4efed512badc08f1efaa9db31cb86079ac5f37e09bb96714a2c`. It is a compact embedded-schema investigation candidate. Its shape, body types, units and dimensions have no independent reference in this pass.

## First geometry target and acceptance

Start with one fixed text schema and bounded analytic solids. `SCH_3000000_30000` is the proposed first target because upstream has documented the exact key and a focused analytic subset. The local corpus contains no match. The 53 KB sample exercises the eventual embedded-schema path and needs a base layout plus qualified edits before full decoding.

| Stage | Implementation and owner check |
| --- | --- |
| Prefix and corpus inventory | The standalone inspector has run on the local corpus. No application integration is present. |
| Fixed-schema records | Add a native C++ reader keyed by the exact internal schema. Check complete consumption, reference resolution, counts and source IDs against genuine box and through-hole controls. Focused owner checks exercise truncation, impossible counts, unsupported node types and invalid required references. Stop for acceptance before OCCT conversion. |
| OCCT B-Rep | Map points, lines, circles, planes and cylinders, with ordered oriented fins, shared edges and face-local parameter curves. Confirm expected closure, `BRepCheck_Analyzer`, dimensions, bounds, area and volume against authored measurements and same-model STEP. Record tolerances and any healing. Stop for owner acceptance before enabling the viewer path. |
| Viewer import | Add the qualified backend through `XCAFDocProcessorFactory`, use the existing XCAF scene conversion and check placement, normals, cancellation and reload. Qualify repeated assembly instances and appearance against dedicated controls before advertising them. |
| Compatibility expansion | Inventory more genuine schema keys, then qualify base-schema resolution and edits separately. Add each layout family with its own references and regression evidence. Keep X_B and embedded JT transport outside the initial text milestone. |

The staged approach follows the [JT development phases](../jt/JT_DEVELOPMENT_PHASES.md) and the separation of decoding from scene integration in the [3DXML implementation plan](../3dxml/3DXML_IMPLEMENTATION_PLAN.md). Standalone and JT paths can share a normalized record and B-Rep layer after each transport is qualified. Retaining exact shapes for downstream tools would require an owned result beyond the current XCAF-to-`aiScene` conversion.

## Evidence gap

The next input is a genuine X_T export of a dimensioned box and a box with a through-hole, together with same-model STEP files, authored dimensions and units, and the exporter/version settings. Prefer Onshape V30 for the proposed key. A different available exporter can establish the first target after prefix inspection. Keep the files local and record only neutral IDs and hashes. No owner acceptance or viewer build/test result has been recorded.
