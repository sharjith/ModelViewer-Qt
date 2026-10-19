# JT development phases and owner acceptance gates

19 October 2026, `feature/JT-importer`. This is the active execution plan, supplementing the broader [JT/Parasolid scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md) and [version policy](JT_VERSION_COMPATIBILITY.md).

The staged visualization target is **JT 6.4–10.11**. If 6.4 specifically requires a substantial replacement or incompatible extension of the viewer mesh/assembly model, use **7.0 as the lower target**, supported by the documented [legacy decision gate](JT_LEGACY_COMPATIBILITY_ASSESSMENT.md). Missing samples or a difficult codec alone do not demonstrate that condition.

## Delivery rules

- Organize implementation around container/record families and geometry/attribute features. Share code between compatible minor versions; do not build a parser per minor release or accept unknown layouts merely because their version falls in a numeric range.
- Preserve the existing scene boundary, hierarchy, local transforms, resource sharing and normal open/import mechanism. Keep exact Parasolid conversion separate; validated stored tessellation remains the fallback.
- Before each owner gate, finish the phase's code, focused automated checks, relevant prior regressions and documentation. Provide the branch, exact sample paths, expected results and known limitations. The owner builds the full application; the agent may build isolated native tests.
- Stop at each owner gate. Address reported failures within that phase, rerun affected checks, and present the same gate again. Resume the next phase after explicit acceptance; do not infer acceptance from silence, a build starting, or an unrelated model opening.
- Do not commit before automated checks and owner UI acceptance. Commit/push only when requested. Preserve unrelated local/submodule changes.
- A gate qualifies the stated layouts/features and samples, not every file with an accepted version header. Diagnose unsupported required encodings instead of returning successful empty or silently incomplete geometry.

## Current gate

**Gates 0 and 1 accepted, 19 October 2026.** The owner reported “Tests passed, commit, push, and proceed” for the shared legacy quantized follow-up. See [implementation and validation](JT_LEGACY_QUANTIZED_VALIDATION.md). Phase 2 is next: newer modern attributes, Sabot wrappers and palette appearance for the 10.9 assembly. Stop at Gate 2 for the next owner build/check.

## Phase map

| Phase | Deliverable | Native validation before handoff | Owner gate |
| --- | --- | --- | --- |
| 0 — Shared-family baseline | Consolidate the existing lossless 8.0–8.2 family and 10.0/10.3 subsets; correct the distinction between a version rejection and unsupported encoding | Original sample L80 succeeds; sample Q82 fails explicitly at quantized encoding; LE/BE family fixtures and malformed cases; existing 8.1/10.0 regressions; sample A103 native validation already recorded | **Gate 0 accepted:** owner reported all tests passed |
| 1 — Legacy quantized display | Implement the 8.x encodings required by sample Q82: integer codecs, quantized positions, stored normals, vertex/attribute indices and strip reconstruction; correctly consume/validate associated UV/colour records and preserve supported colour bindings | Small original fixtures with independently calculable geometry/normals, both byte orders where applicable, malformed counts/indices/streams, cancellation; original sample Q82 imports; prior lossless/modern regressions stay green | **Gate 1 accepted, 19 October 2026:** sample Q82 renders completely with plausible placement and smooth shading; counts and stated appearance limits supplied with the handoff |
| 2 — Newer modern attributes and palettes | Decode version-2 trailing attribute fields, checked Sabot wrappers, palette accumulation and face-group appearance for the supplied 10.9 assembly | Attribute/palette fixtures including fallback/inhibition/overrides, material splits and shared-resource cases; original 10.9 geometry checks; prior 8.x/10.0/10.3 regressions | **Gate 2:** 10.9 model opens with complete geometry, authored assembly placement, usable hierarchy and supported colours; compare with a reference preview where available |
| 3 — Transitional 9.x and intervening 10.x | Establish the family map across 9.x (9.5 first) and remaining 10.x through the documented baseline; adapt only actual envelope/codec differences | Matching references and authentic inputs, element-version/codec matrix, original fixtures, part and repeated-instance assembly cases; cross-family regressions | **Gate 3:** a representative case from each newly qualified layout family passes UI checks; supported/unsupported features are recorded |
| 4 — Pre-8 formats and lower-bound decision | Establish 7.0 container/graph/tessellation handling, then compare/adapt 6.4; apply the owner-approved model-change criterion | Lawful historical layout evidence and authentic samples; original container/geometry fixtures; comparison of 6.4 vs 7.0 semantics; bounded reads, transforms, normals, cancellation and prior-family checks | **Gate 4:** review representative legacy rendering and the evidenced 6.4 decision. If the model-change condition is met, document the 7.0 cap and proceed without redesigning the viewer for 6.4 |
| 5 — Remaining forward range | Qualify 10.10/10.11 and any remaining target revisions using lawful layout evidence and representative inputs | Explicit differences from the qualified modern family, codecs/records and required semantics; same malformed-input, resource and regression standards | **Gate 5:** representative current-version parts/assemblies render; distinguish unqualified revisions or encodings rather than advertising blanket coverage |
| 6 — Release readiness | Consolidate supported-feature matrix, sample/test provenance, actionable diagnostics, progress/count semantics, documentation, About and distribution notices | Qualified corpus run, source/licence provenance checks, packaging checks and targeted missing-reference/cancellation/resource-limit cases; no regression in other import dispatch paths | **Gate 6:** owner acceptance of the published support matrix and representative cross-family open/import workflow before any release request |

These are feature milestones, not promises to complete every historical representation. If a phase produces several independently useful changes, split it into an additional owner gate **before** beginning the next change. A geometry-first Gate 1 may state that textures or some secondary appearance remain unsupported, but must not skip unknown UV/colour bytes or misrepresent a partial mesh as complete. Supply a concrete appearance follow-up if needed.

Acquire old/current references and authentic samples as preparation while another phase is active; that does not authorize advancing unaccepted feature changes past a gate. Lack of a corpus may change implementation order after being documented. Do not weaken the validation criteria to manufacture range coverage.

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
