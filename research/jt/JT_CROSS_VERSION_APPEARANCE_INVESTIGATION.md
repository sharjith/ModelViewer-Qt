# JT cross-version appearance investigation

20 October 2026, `feature/JT-importer`. Additional owner exports are identified here as E95 and E109; original model/part names, paths, hashes and geometry remain in ignored local research storage. The scoped material compatibility correction passes native checks and was accepted by the owner in ModelViewer on 20 October 2026 ("That fixed"). It enables no additional JT version.

## Owner observations

The owner compared the native CAD assembly, the 10.9 JT reimported into Solid Edge, and ModelViewer imports of 9.5 and 10.9. Some covers/other parts appear purple only in ModelViewer's 10.9 view. The owner explicitly confirmed the Solid Edge reference screenshot was the **10.9** reimport.

The owner then confirmed that the rough oil-pan underside in the 9.5 import also appears rough when **that 9.5 export is reimported into Solid Edge**, supplying a matching screenshot. Classify this as an export characteristic, rather than a demonstrated ModelViewer mesh or normal defect. Do not replace source geometry/normals to conceal it.

## Unchanged-input native evidence

| Assembly | Render resources / occurrences | Vertices | Triangles |
| --- | --- | --- | --- |
| E95 | 46 / 46 | 1,007,351 | 1,109,118 |
| E109 | 46 / 46 | 1,054,753 | 1,105,712 |

Both pass bounded native geometry, index, normalized-normal and transform validation. Counts are observations of different exported representations; equality is not an acceptance requirement when the stored source representations differ.

For the oil pan, both versions contain **4,432 distinct, bit-identical position triples and 8,860 triangles**. Of their unordered coordinate-triangle sets, 8,858 triangles match; two differ in triangulation. The 9.5 source declares 6,404 lossless normal records; the 10.9 source declares 9,262. Both use zero-bit normal quantization (lossless floating-point transports), with the older exponent/mantissa versus modern binary representation. Expanded/render vertex counts can differ from the raw dictionary count. Combined with the owner's reimport check, these measurements do not justify changing LOD selection or regenerating normals.

### Repeated fallback materials

The unaccumulated graph audit covers all 37 referenced parts per version. No E95 part has repeated fallback material attributes on one node. Three E109 parts do. In each affected shape's attribute list:

1. An ordinary fallback material supplies the expected dark/grey diffuse RGB: either `(0.2, 0.2, 0.2)` or approximately `(0.752941, 0.752941, 0.752941)`.
2. A later ordinary fallback material supplies purple RGB approximately `(0.62, 0.35, 0.75)`.

Both are material attributes with base version 2, palette index -1, inhibit/final flags zero, Force/Ignore unset, opaque diffuse alpha and data flags `0x3980`. The inspected standalone cover graph has no ancestor material override. This is not purple invented by the viewer's default material or a palette-index decoding failure in that inspected part. It does not explain every possible appearance record in the assembly.

The published JT 10.6 reference, section 11.10, specifies same-type attributes on a node accumulating in attribute-list order, front to back. Section 5.4 describes material replacement and final/inhibit/force rules. Under those rules the later purple material replaces the earlier colour. See the [public Siemens reference download](https://static.sw.cdn.siemens.com/media/published/4Zc4NsBnmaFlEd0mtssp7f/siemens-jt-v-10-6-file-format-reference-rev-d-10-feb-2026.zip). Our output is consistent with that rule for the inspected list, but **does not match the owner's Solid Edge reference**. Do not dismiss that interoperability discrepancy as resolved or assume either exporter or reader is faulty solely from this evidence.

## Diagnostics and regression checks

`jt_probe --source-attributes <part.jt>` prints the unaccumulated node attribute lists as JSON. It deliberately does not follow external partition files or decode geometry; inspect the referenced part directly. It retains normal importer allocation limits. `jt_probe --materials <input.jt>` now also prints resolved diffuse RGBA and opacity.

Original binary regression graphs exercise repeated fallback material order and field-final protection in little/big endian for enabled 10.0, 10.3 and 10.9 headers. The isolated MSVC AddressSanitizer build and full portable suite pass. These graphs qualify attribute semantics, not native tessellation for a different revision.

Generate independent-reader fixtures with:

```text
jt_tests --material-order-fixtures out/jt-research/material-order-ui-fixtures
```

Keep `geometry.jt` beside the six assembly files. `order_10_*_red_green.jt` should resolve to green; `order_10_*_green_red.jt` should resolve to red under published list-order semantics. The geometry is an original quad plus triangle, seven vertices and three triangles, referenced through the existing 8.2 test representation. These are mixed-version synthetic diagnostic assemblies, not copies or conversions of owner geometry. A reader rejecting the fixture is not evidence about material-order semantics.

Native logs, source-attribute JSON, GLB diagnostics and SHA-256 audit records stay under ignored `out/jt-research/engine*`. Temporary decoder trace instrumentation was removed. The subsequent compatibility correction is described below. No full application build, commit or push was performed by the agent.

## Remaining decision

The owner opened the affected standalone 10.9 cover directly in Solid Edge and confirmed it appears **dark**. Thus the discrepancy also occurs without assembly inheritance; assembly-level overrides alone do not explain it. This is consistent with Solid Edge preferring the earlier material in this case, but does not yet establish a general first-material rule. Compare the original forward/reverse material-order fixtures in Solid Edge and, if available, another independent JT reader. Establish whether additional documented semantics are missing, a writer-specific convention requires an explicit compatibility option, or the reference reader differs from published list-order rules. Do not silently adopt first-material-wins for every file or version.

The previously accepted 10.9 exported assembly remains an accepted sampled case. The new E109 colour correction has **owner UI acceptance**, recorded below. Gate 3b remains independent and pending; this report does not accept it. Continue version qualification with per-part geometry, stored normals, placement and resolved appearance comparisons against each export's own reference, rather than forcing byte/count/colour equality between source files that already differ.

## Solid Edge fixture rejection and replacement controls

The owner corrected an initial green/red report: it was observed in ModelViewer, not Solid Edge. When tested in Solid Edge, **both synthetic material-order assemblies were rejected with “File cannot be opened”**. This supplies no evidence for first- or last-material precedence in Solid Edge.

Inspection found that our synthetic container writers omitted the required five ASCII/binary translation-detection bytes at header positions 75–79: space, LF, CR, LF, space (public reference section 4.1.1). Modern, legacy, 9.5 and unsupported-version fixture writers now emit this marker while retaining the same 80-byte header size and offsets. The isolated AddressSanitizer build and full portable suite pass after regeneration. This fixes an identified fixture defect; whether it was the sole reason for Solid Edge's rejection remains unverified. The regenerated synthetic files are not the primary next interoperability test.

Four private controls were therefore created from the affected standalone E109 cover that the owner already opens successfully. They remain in ignored `out/jt-research/cover-material-controls/`, with source/output SHA-256 records. The source file was not changed. The LSG segment is recompressed and TOC offsets/lengths adjusted. Every non-LSG segment, including the entire tessellation segment and metadata segments, is byte-identical to the source. Each passes the native reader with one mesh, 82,768 expanded vertices and 78,404 triangles, unchanged bounds and validated normals/indices.

| Control | Change to source material fields | Native before correction | Solid Edge result |
| --- | --- | --- | --- |
| `control_repacked.jt` | None; repack only | Purple, same as source import | Owner confirms opens and remains dark |
| `both_yellow.jt` | Both diffuse RGBA values set to yellow/opaque | Yellow | Owner confirms yellow |
| `first_red_second_green.jt` | Earlier diffuse red, later diffuse green | Green before correction | Owner confirms red |
| `first_green_second_red.jt` | Earlier diffuse green, later diffuse red | Red before correction | Owner confirms green |

Close each imported document before opening the next control, to reduce cache ambiguity. If the repacked control fails, do not infer material semantics from the other controls. If all changed variants stay dark, investigate another appearance source or cached source data rather than claiming a first-material rule. These are source-derived local diagnostic copies, not redistributable synthetic fixtures; they and their original metadata must not be committed or bundled. The owner results and subsequent scoped precedence correction are recorded below; no commit has been made.

## Confirmed source convention and scoped correction

The owner tested the source-derived controls in Solid Edge: repack-only stays dark, both-yellow turns yellow, red-then-green stays red, and green-then-red stays green. These results establish preference for the **first diffuse material in this source-derived shape**, independently of assembly inheritance. They do not establish that Solid Edge always uses first-material ordering or that published JT ordering should be changed globally. The earlier synthetic rejection and withdrawn ModelViewer-only result supply no contrary precedence evidence.

The scene adapter now has a narrow automatic compatibility profile. It applies only when **all** of these conditions hold:

- The containing file has the observed header signature `Version 10.9 JT DM 10.8.0.1`.
- Explicit `CAD_SOURCE::` string metadata identifies `Solid Edge` on the shape's ancestor branch or the shape itself. Names, geometry, file paths and colour values are not used as identifiers.
- A shape has exactly two material attributes, both base-version 2 ordinary fallback attributes: palette index -1, no Ignore/Force, no field inhibits/finals or whole-attribute Final, and observed data flags `0x3980`.
- No palette map or inherited palette entries are active, and the shape does not introduce a palette-map attribute.

For this pattern, select the first material record and emit a compatibility diagnostic. Other attributes continue to accumulate normally. Published order remains in effect for other writers, metadata, revisions, non-shape nodes, palette entries and flagged/masked records. Source context is copied per branch and reset for external files; source identity does not leak from an assembly into unrelated part files.

`Options::solidEdgeDuplicateMaterialCompatibility` defaults to true. Setting it false restores published order; `jt_probe --published-material-order` exposes that diagnostic choice. This is an importer option, not a new Settings Dialog control. Extend the measured profile only with additional independent evidence. The control experiments establish diffuse-colour precedence; complete equivalence of specular, shininess or every appearance feature remains subject to reference/viewer comparison. The profile selects a material record rather than recolouring the geometry or deleting all purple materials.

### Validation and owner gate

The isolated MSVC AddressSanitizer build and full portable suite pass. Original guards cover opt-out, other writer/revision/author, absent author, field masks/finals, whole-attribute Final, Force, palette indices/maps/inherited entries, three-material lists, non-shape nodes and sibling isolation. Existing source-order LE/BE binary checks for 10.0/10.3/10.9 and the five 10.0 part regressions remain green.

All four source-derived controls pass natively with the compatibility profile: dark, yellow, red and green, matching the owner's Solid Edge results. The opt-out restores green for the red-then-green control. The original E109 assembly passes with unchanged 46 resources/occurrences, 1,054,753 vertices, 1,105,712 triangles, 290 nodes and local bounds. Before/after GLB checks verify position, normal and index arrays and all occurrence transforms are byte-identical across all 46 occurrences. Exactly three diffuse assignments change: two purple-to-grey and one purple-to-dark. Other authored purple parts remain purple. The original source files are not rewritten.

Logs: ignored `engine-compat-*`, `engine109-after.log`, `engine-compat-comparison.json` and control-specific `.compat.log` files. A corrected private preview is `out/jt-research/engine109-after.glb`; it is not bundled or committed. No third-party decoder source changed for this material correction.

Owner handoff on `feature/JT-importer`: rebuild the normal app, reopen the original E109 assembly and standalone cover, and compare them with the Solid Edge reference. Expected: the affected top cover is dark and the affected grey parts lose the unintended purple; genuine purple fasteners/other parts retain their colours. Verify the four controls now match Solid Edge. Reopen E95 and the previously accepted exported 10.9 assembly for regressions. The E95 rough underside should remain as in its Solid Edge reimport. The owner subsequently confirmed "That fixed", accepting this colour correction. This confirmation does not separately accept the outstanding Gate 3b material-mask fixtures or every regression case in this handoff. No commit/push was requested.

## Owner acceptance — 20 October 2026

The owner confirmed the ModelViewer colour correction with "That fixed". Record the E109 appearance fix as UI-accepted, alongside its native validation. Do not infer a new file-version qualification, acceptance of the original A109 production-system comparison, or acceptance of the separate Gate 3b mask fixtures. Changes remain uncommitted pending a commit request.

## Matching 10.5 writer follow-up — 20 October 2026

The owner reported the purple-cover issue in additional assembly E105. Matching ordered material records and the exact `Version 10.5 JT ISO14306-4 DM 10.8.0.1` header justify extending the guarded profile to that observed signature. All other guards and the opt-out are retained. Native validation changes exactly three resolved materials while preserving every emitted geometry attribute/index and all node transforms; the owner 10.5 retest is pending. See the [10.5 follow-up](JT_105_VALIDATION.md) for counts, evidence and acceptance scope. The earlier E109 owner acceptance remains specific to 10.9.

The owner subsequently confirmed "Fixed" for the 10.5 appearance follow-up. The reported colour correction is UI-accepted; independent 10.5 control-reader results are not inferred.

## Matching 10.7/10.8 writer follow-up — 20 October 2026

Additional E107/E108 exports reproduce the same purple-part issue. All seven duplicate shape lists in each corpus match corresponding E109 records. The profile now accepts their exact 10.7/10.8 DM 10.8.0.1 headers while retaining all author, shape, flag and palette guards. Native comparisons correct exactly three materials per assembly without changing geometry or transforms, and both initial palette assemblies remain byte-identical. All four writer signatures run the original fifteen guard cases plus sibling isolation; MSVC AddressSanitizer/native regressions pass. Owner colour retest remains pending; see [10.7/10.8 validation](JT_107_108_VALIDATION.md). This extension is based on matching observed records and the existing E109 reference controls, not a general first-material rule or independently reported E107/E108 control-reader tests.

## Owner 10.7/10.8 colour acceptance — 20 October 2026

The owner confirmed "Fixed" after the two additional engine appearance corrections. Record those reported colour issues as UI-accepted. This supersedes the pending colour-retest status above; it does not assert independent control-reader tests or completion of unreported optional fixtures. Proceed to the available 10.10 qualification.

## Matching 10.10 writer qualification — 20 October 2026

The owner supplied E1010 during forward-version qualification. Its seven duplicate source shape lists match confirmed E109 records. The guarded profile accepts its exact 10.10 DM 10.8.0.1 header; native comparison changes exactly three semantic materials while retaining all geometry attributes, indices and scene nodes/transforms. All five exact writer signatures run the original fifteen guard cases and sibling isolation. No global accumulation change is made. [Gate 5a](JT_1010_VALIDATION.md) owner checks cover both this appearance case and the supplied palette assembly; independent 10.10 control-reader results are not inferred.

The owner subsequently confirmed both authentic 10.10 assemblies look good, accepting the E1010 appearance correction along with palette-assembly display at Gate 5a. No independent control-reader results are inferred.
