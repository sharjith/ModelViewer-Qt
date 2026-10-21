# JT development phases and owner acceptance gates

Updated 20 October 2026, `feature/JT-importer`. This is the active execution plan, supplementing the broader [JT/Parasolid scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md) and [version policy](JT_VERSION_COMPATIBILITY.md).

The staged visualization target is **JT 6.4–10.11**. Execution priority, updated by the owner after Gate 5a acceptance, is **feature completion across all enabled revisions before 10.11**; phase identifiers below remain stable for existing gate references. If 6.4 specifically requires a substantial replacement or incompatible extension of the viewer mesh/assembly model, use **7.0 as the lower target**, supported by the documented [legacy decision gate](JT_LEGACY_COMPATIBILITY_ASSESSMENT.md). Missing samples or a difficult codec alone do not demonstrate that condition.

## Delivery rules

- Organize implementation around container/record families and geometry/attribute features. Share code between compatible minor versions; do not build a parser per minor release or accept unknown layouts merely because their version falls in a numeric range.
- Preserve the existing scene boundary, hierarchy, local transforms, resource sharing and normal open/import mechanism. Keep exact Parasolid conversion separate; validated stored tessellation remains the fallback.
- Before each owner gate, finish the phase's code, focused automated checks, relevant prior regressions and documentation. Provide the branch, exact sample paths, expected results and known limitations. The owner builds the full application; the agent may build isolated native tests.
- Stop at each owner gate. Address reported failures within that phase, rerun affected checks, and present the same gate again. Resume the next phase after explicit acceptance; do not infer acceptance from silence, a build starting, or an unrelated model opening.
- Commit/push only when requested, normally after automated checks and owner UI acceptance. An explicit owner request may authorize a distribution commit before UI acceptance so another system can build and test it; record the pending gate separately. Preserve unrelated local/submodule changes.
- A gate qualifies the stated layouts/features and samples, not every file with an accepted version header. Diagnose unsupported required encodings instead of returning successful empty or silently incomplete geometry.

## Current gate

**Latest checkpoint, 20 October:** Gate 5a authentic 10.10 displays are accepted and distribution commit `fe443e7` is pushed. The owner now prioritizes [feature completion across enabled versions](JT_FEATURE_COMPLETION_PLAN.md), deferring 10.11 until that pass is completed and reviewed. C1a bounded legacy Huffman and C1b modern packed colours are owner-accepted. Current gate C1c adds the distinct 9.x colour transports, followed by the remaining codec/binding, appearance, representation, PMI, runtime LOD, exact geometry and consolidation gates in that plan. Earlier version-expansion sequencing below is historical. No new file revision is enabled by this change.

**Gates 0 and 1 accepted, 19 October 2026.** The owner reported “Tests passed, commit, push, and proceed” for the shared legacy quantized follow-up. See [implementation and validation](JT_LEGACY_QUANTIZED_VALIDATION.md). Phase 2 is split at two owner gates: [Gate 2a attribute-envelope foundations](JT_MODERN_ATTRIBUTE_VALIDATION.md), then Gate 2b palette/face-group rendering and 10.9 qualification. The owner accepted Gate 2a on 20 October 2026 with “All tests passed.” Gate 2b (10.9 palette/face-group rendering) passed native production-path validation and prior-family regressions; see [owner handoff](JT_109_PALETTE_VALIDATION.md). The owner authorized committing/pushing on 20 October 2026 to enable that production-system build; Gate 2b remains pending. Distribution commit `4cb6ebe` is pushed to `origin/feature/JT-importer`. The owner subsequently authorized independent next-stage work while the production-system test is pending: reference/sample acquisition and 10.10/10.11 layout qualification may proceed; do not treat that authorization as acceptance of Gate 2b. Changes that depend on the correctness of 10.9 rendering remain subject to its reported results. The owner prioritizes current-version coverage: qualify 10.10/10.11 when matching lawful evidence and inputs are available. The later owner direction below selects the available 9.5 input in the meantime.

The owner subsequently selected the available 9.5 input for the next implementation pass, while authentic 10.10/10.11 inputs remain unavailable. Phase 3 **Gate 3a: JT 9.5 was accepted on 20 October 2026** when the owner reported “9.5 works”; keep Gate 2b pending independently. Do not infer acceptance of either gate from the other. See [9.5 implementation and owner checklist](JT_95_VALIDATION.md).

The owner next requested all known 9.x point versions and all 10.x revisions through 10.8, with the 10.9 production test planned for 21 October. Execute the [point-version expansion gates 3b–3f](JT_POINT_VERSION_EXPANSION.md). Gate 3b corrects the older material mask semantics first; Gates 3c–3f qualify additional file revisions from matching layout/input evidence. No newly targeted version is enabled merely by this plan.

## Phase map

| Phase | Deliverable | Native validation before handoff | Owner gate |
| --- | --- | --- | --- |
| 0 — Shared-family baseline | Consolidate the existing lossless 8.0–8.2 family and 10.0/10.3 subsets; correct the distinction between a version rejection and unsupported encoding | Original sample L80 succeeds; sample Q82 fails explicitly at quantized encoding; LE/BE family fixtures and malformed cases; existing 8.1/10.0 regressions; sample A103 native validation already recorded | **Gate 0 accepted:** owner reported all tests passed |
| 1 — Legacy quantized display | Implement the 8.x encodings required by sample Q82: integer codecs, quantized positions, stored normals, vertex/attribute indices and strip reconstruction; correctly consume/validate associated UV/colour records and preserve supported colour bindings | Small original fixtures with independently calculable geometry/normals, both byte orders where applicable, malformed counts/indices/streams, cancellation; original sample Q82 imports; prior lossless/modern regressions stay green | **Gate 1 accepted, 19 October 2026:** sample Q82 renders completely with plausible placement and smooth shading; counts and stated appearance limits supplied with the handoff |
| 2 — Newer modern attributes and palettes | Decode version-2 trailing attribute fields, checked Sabot wrappers, palette accumulation and face-group appearance for the supplied 10.9 assembly | Attribute/palette fixtures including fallback/inhibition/overrides, material splits and shared-resource cases; original 10.9 geometry checks; prior 8.x/10.0/10.3 regressions | **Gate 2a:** attribute-envelope foundations and synthetic fallback display; **Gate 2b:** 10.9 model opens with complete geometry, authored assembly placement, usable hierarchy and supported colours; compare with a reference preview where available |
| 5 — Current forward range (priority after Phase 2) | Qualify 10.10/10.11 and any remaining target revisions using lawful layout evidence and representative inputs | Explicit differences from the qualified modern family, codecs/records and required semantics; same malformed-input, resource and regression standards | **Gate 5:** representative current-version parts/assemblies render; distinguish unqualified revisions or encodings rather than advertising blanket coverage |
| 3 — Transitional 9.x and intervening 10.x | Establish the family map across 9.x (9.5 first) and remaining 10.x through the documented baseline; adapt only actual envelope/codec differences | Matching references and authentic inputs, element-version/codec matrix, original fixtures, part and repeated-instance assembly cases; cross-family regressions | **Gate 3:** a representative case from each newly qualified layout family passes UI checks; supported/unsupported features are recorded |
| 4 — Pre-8 formats and lower-bound decision | Establish 7.0 container/graph/tessellation handling, then compare/adapt 6.4; apply the owner-approved model-change criterion | Lawful historical layout evidence and authentic samples; original container/geometry fixtures; comparison of 6.4 vs 7.0 semantics; bounded reads, transforms, normals, cancellation and prior-family checks | **Gate 4:** review representative legacy rendering and the evidenced 6.4 decision. If the model-change condition is met, document the 7.0 cap and proceed without redesigning the viewer for 6.4 |
| 6 — Release readiness | Consolidate supported-feature matrix, sample/test provenance, actionable diagnostics, progress/count semantics, documentation, About and distribution notices | Qualified corpus run, source/licence provenance checks, packaging checks and targeted missing-reference/cancellation/resource-limit cases; no regression in other import dispatch paths | **Gate 6:** owner acceptance of the published support matrix and representative cross-family open/import workflow before any release request |

These are feature milestones, not promises to complete every historical representation. If a phase produces several independently useful changes, split it into an additional owner gate **before** beginning the next change. A geometry-first Gate 1 may state that textures or some secondary appearance remain unsupported, but must not skip unknown UV/colour bytes or misrepresent a partial mesh as complete. Supply a concrete appearance follow-up if needed.

Acquire old/current references and authentic samples as preparation while another phase is active. The explicit 20 October authorization also permits independent current-version qualification while Gate 2b is pending; other gates still require acceptance before dependent feature work advances. See [forward qualification research](JT_FORWARD_COMPATIBILITY_RESEARCH.md). Lack of a corpus may change implementation order after being documented. Do not weaken the validation criteria to manufacture range coverage.

## Gate 0: accepted baseline (historical checklist)

Implementation is in the current checkout on `feature/JT-importer`; no worktree transfer or commit is needed. This gate accepts the accumulated shared-family changes before the quantized phase starts. Existing 8.1 owner confirmation is retained; do not assume 8.0 or 10.3 UI acceptance from it.

Rebuild the normal application and run:

| Input | Expected result |
| --- | --- |
| `<local-samples>/sample-L80.jt` | JT 8.0 opens. Native result: 54 mesh resources/occurrences, 185,395 vertices and 118,781 triangles. Check completeness, placement, shading, colours and selection |
| `<local-samples>/sample-L81.jt` | Previously working 8.1 remains correct. Native result: 1,084 meshes, 602,484 vertices and 200,828 triangles |
| `out/jt-research/samples/Bolt.jt` | Existing 10.0 path remains correct: one mesh, 913 vertices and 904 triangles |
| `<local-samples>/sample-A103.jt` | 10.3 opens and the assembly looks complete and correctly placed. Native result: 2,468 mesh resources and 2,853 occurrences; 4,450,421 vertices and 5,844,669 triangles counted once per emitted mesh resource |
| `<local-samples>/sample-Q82.jt` | **Expected failure at this gate:** the message identifies unsupported **JT 8.x quantized vertex records**, rather than rejecting version 8.2. This file becomes a rendering acceptance case in Phase 1 |
| About | Historical Gate 0 check distinguished current subsets from the planned range. Current presentation policy: supported versions and licence credits in About; coverage, targets and validation status in the README |

Resource and occurrence counts are different units. Do not compare sample A103's UI occurrence count against its unique render-resource count. Optional redundant-node purging can change tree depth without changing mesh occurrence placement. Native resource sums are not necessarily the same as UI totals summed across repeated instances.

The existing isolated MSVC AddressSanitizer suite passes the shared-family fixtures, original sample L80 geometry validation, expected sample Q82 encoding rejection, sample L81 and all five original 10.0 parts. Original sample A103 validation was completed in its preceding pass. See [8.x validation](JT_8X_FAMILY_VALIDATION.md) and [sample A103 validation](JT_103_ASSEMBLY_VALIDATION.md). The owner subsequently rebuilt and accepted Gate 0 with “All tests passed.” The sample Q82 rejection in this historical checklist was expected at that gate; the Phase 1 implementation now replaces it with successful native decoding.

Gate 0 is accepted. Phase 1 now adds the checked quantized geometry path, UV retention and the constant RGB(A) form required by the sample. Its owner handoff is [Gate 1](JT_LEGACY_QUANTIZED_VALIDATION.md). The sample Q82 native result is 6 meshes, 115,176 vertices and 205,355 triangles. Texture images/shaders and nonconstant quantized colours remain outside this delivered subset. The owner accepted Gate 1 on 19 October 2026 and authorized proceeding to Phase 2.

## Exact geometry boundary

Standalone XT and JT-embedded precise B-Rep conversion remain a separate future implementation/branch decision. No visualization gate depends on buying a Siemens toolkit or on an exact backend succeeding. Optional future exact output must be validated; failure uses qualified stored tessellation, with an explicit failure if none is usable. Full PMI, shader execution, texture fidelity and runtime LOD switching are separate scope unless a phase explicitly brings them into its acceptance criteria.

## Latest owner confirmation — 20 October 2026

The owner confirmed successful UI testing of the supplied Solid Edge 10.9 assembly export. This supersedes earlier statements that all Gate 2b UI qualification is pending: sampled 10.9 display is accepted. The original production-system palette/face-group comparison remains pending additional coverage; the confirmation does not imply that every such variant was exercised. Gate 3b remains pending independently. Authentic 10.10 exports are now available, while 10.11 inputs remain unavailable. See [10.9 acceptance](JT_109_PALETTE_VALIDATION.md) and the [current expansion plan](JT_POINT_VERSION_EXPANSION.md).

## Additional appearance regression — 20 October 2026

A second owner-exported 10.9 assembly differs in colour from its Solid Edge reimport. The [cross-version investigation](JT_CROSS_VERSION_APPEARANCE_INVESTIGATION.md) records repeated fallback material attributes in three parts and the published list-order rule. Source-derived controls subsequently confirmed that Solid Edge selects the earlier material in the observed duplicate-material shape. A narrow writer/author/record compatibility profile passes native checks, and the owner accepted the ModelViewer colour correction on 20 October with "That fixed"; see the investigation for its guards, opt-out and test paths. This acceptance is specific to the colour correction. The owner confirmed the rough 9.5 oil-pan appearance also occurs in Solid Edge and is an export characteristic. Gate 3b remains pending independently.

**Latest 3c1 follow-up:** owner UI acceptance is recorded for SE9_0. The additional E90 empty-sibling-shape correction passed native tests and the owner subsequently confirmed "Tests passed"; see [9.0 validation](JT_90_VALIDATION.md). Other outstanding checks are not inferred accepted.

**Gate 3e1 handoff:** the available 10.5 export qualifies shared modern containers, Sabot-wrapped attributes, palette maps and stored tessellation. Native evidence and owner tests are in [10.5 validation](JT_105_VALIDATION.md). Stop before 10.7/10.8 implementation until this UI gate is accepted.

**Gate 3e1 appearance follow-up:** an additional 10.5 assembly exposed the matching duplicate-material issue. The guarded exporter profile extension passes native/regression checks; owner colour retest remains pending in [10.5 validation](JT_105_VALIDATION.md).

## Gate 3f handoff — 20 October 2026

The owner confirmed "Fixed" for the 10.5 appearance correction. The available authentic 10.7 and 10.8 assemblies now pass the shared modern decoder, geometry/material/transform comparisons and original LE/BE/malformed-input checks. See [10.7/10.8 validation](JT_107_108_VALIDATION.md). Stop for owner application build/UI confirmation before the available 10.10 qualification. Missing 10.4/10.6 and 10.11 inputs remain unqualified; no continuous version range is inferred. No commit or push requested.

**Gate 3f appearance follow-up:** owner reported purple parts in the additional 10.7/10.8 engine exports. Matching duplicate-material records qualify the two exact exporter signatures for the existing guarded compatibility profile. Native before/after geometry and material checks pass; owner colour retest is pending in [10.7/10.8 validation](JT_107_108_VALIDATION.md). Remain at this gate before 10.10.

## Gate 5a: available JT 10.10 — 20 October 2026

The owner accepted the reported 10.7/10.8 colour corrections and supplied an additional 10.10 engine export during qualification. Qualify both the palette assembly and duplicate-material case with the shared reader. Native evidence, limits and owner checklist are in [10.10 validation](JT_1010_VALIDATION.md). Stop at this application gate; 10.11 still lacks an authentic input. No commit or push requested.

## Gate 5a accepted — 20 October 2026

The owner confirmed "Both engine and vice models look good in 10.10". Both authentic display cases and the engine material correction are accepted. The next forward qualification requires an authentic 10.11 input. Release consolidation, missing intermediate revisions and pre-8 work remain distinct; no full-range or release acceptance is implied. No commit or push requested.

**C1a accepted / C1b handoff, 21 October:** the owner reported all C1a tests passed. [Modern packed colours](JT_QUANTIZED_COLOUR_VALIDATION.md) now pass original LE/BE field tests across the enabled 10.x revisions and private full-model/lossless-reference comparisons; owner application checks are pending. The different 8.x/9.x transports and vertex-alpha rendering remain separate gates.

**C1b accepted / C1c handoff, 21 October:** the owner reported all C1b tests passed. [Transitional colours](JT_9X_COLOUR_VALIDATION.md) now use their separate exponent/mantissa and four-vector quantized transports. Original LE/BE vectors and private full-model controls pass; owner application checks are pending. Earlier variable 8.x colours and active vertex-alpha rendering remain separate gates.

**C1d acceptance, 21 October 2026:** [Topological UV arrays](JT_TOPOLOGICAL_UV_VALIDATION.md) pass native field, seam, copy/export and full twelve-version regression checks. Owner application build/UI tests passed. The qualified addition is channel-0 two-component UVs in enabled 9.x/10.x topological meshes; texture resources/shaders and other UV bindings remain later work. No JT file revision is enabled.

**C1e acceptance, 21 October 2026:** [Multiple UV sets](JT_MULTI_UV_VALIDATION.md) extends enabled 9.x/10.x topological meshes to contiguous two-component UV sets 0–3. Native checks cover binding layouts, seams, variant/palette/flat copies, scene copying and GLB. Owner build/UI tests passed. Source texture resources and unsupported UV layouts remain later work; no new JT file revision is enabled.

**C1f acceptance, 21 October 2026:** [Scalar texture coordinates](JT_SCALAR_UV_VALIDATION.md) extends enabled 9.x/10.x topological collections to mixed scalar/two-component sets 0–3. Scalar U is embedded as `(U,0)` for scene rendering/export; the decoder retains source component counts. Owner application checks passed; diagnostic surface-coverage limitations remain documented. No new file version is enabled, and source texture-resource/shader semantics remain later work.

**C1g acceptance, 21 October 2026:** [Sparse UV sets](JT_SPARSE_UV_VALIDATION.md) preserve absent slots within scalar/2D sets 0–3. The decoder retains absence; the scene pads internal gaps with zero pairs for Assimp copy/export while retaining original set numbers. Native validation and owner checklist are in the linked document. The owner accepted the focused sparse-set application checks on 21 October 2026; no additional file version is enabled.
