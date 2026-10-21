# JT feature completion across enabled versions

20 October 2026, `feature/JT-importer`. The owner changed the priority after accepting 10.10: complete remaining features across implemented versions first, then extend to 10.11. This supersedes earlier forward-version-first sequencing; historical gate IDs remain valid.

The enabled matrix is 8.0, 8.1, 8.2, 9.0, 9.5, 10.0, 10.3, 10.5, 10.7, 10.8, 10.9 and 10.10. Do not enable 10.11 or missing earlier revisions during this feature pass. Implement by record/codec family, with explicit not-applicable entries when a later family uses a different encoding. Do not create one importer per point release or infer a feature is qualified merely from an accepted version header.

## Execution gates

| Stage | Deliverable | Completion evidence / owner gate |
| --- | --- | --- |
| C1 — Mesh encodings | Complete missing integer-codec, quantized coordinate/normal/colour/UV transports and attribute bindings; preserve corner seams and source normals | Known-value original packets, truncation/bounds/cancellation cases and complete scene fixtures; applicable family matrix and prior-version corpus checks. Split into small owner gates; C1a below is first |
| C2 — Appearance | Additional RGB/HSV and vertex alpha semantics, blending, image/texture resources, UV sets/transforms and supported shader/material mapping | Independent colour/alpha/texture expectations, overrides/inheritance, shared-instance variants and renderer/export preservation; diagnose shader features without a supported mapping |
| C3 — Other display representations | Point/line sets and documented implicit/analytic display representations | Known positions/topology, selection/bounds/visibility, transforms and units; distinguish generated display tessellation from exact source geometry |
| C4 — Metadata and PMI | Broader typed properties, semantic identity/units, annotation/PMI content and display integration | Original records with expected values and relationships, authored/reference display comparisons, clear preservation versus rendering scope |
| C5 — Runtime detail | Retain/reload qualified LOD alternatives and switch them at runtime | Camera/range criteria, resource sharing, stable scene identity, selection, cancellation, bounds and no duplicated LOD display |
| C6 — Exact geometry | Qualify standalone/embedded XT transports, schema/topology reconstruction, B-Rep validation and exact-source retention; subordinate/MultiXT resolution | Separate XT implementation branch when its prerequisites are ready; shared conversion service through the existing JT seam; geometry-agreement/unit/tolerance checks. Supported stored tessellation remains fallback |
| C7 — Consolidation | Final feature-by-family matrix, diagnostics/progress, malformed-input/resource testing, packaging/notices and broad application regressions | Owner review of delivered features and explicit residual limitations; do not label unfinished, unsupported or unvalidated work complete |
| Forward extension | Only after the feature-completion review: qualify 10.11 horizontally | Authentic 10.11 input plus codec/record/feature regressions; never use a reheadered file as authentic evidence |

All remaining areas stay on the plan, including exact geometry. Existing legal/provenance constraints apply: use lawful references, original tests and audited licensed source; unavailable layout evidence or an incompatible representation is recorded as an unresolved dependency, not silently removed. No blanket feasibility or universal file support is promised by this plan. The [JT/Parasolid scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md) supplies the exact-geometry boundary and branch/service design.

The order above is a dependency sequence. Split each stage into independently reviewable subgates and stop for the owner to build, test and confirm. Routine read-only preparation may proceed while a gate waits, but dependent implementation does not advance before acceptance. Commit/push only when requested. Preserve unrelated workspace changes. About remains limited to supported versions and licence details.

## Accepted gate C1a — bounded legacy Huffman

The first missing mesh transport is CDP1 Huffman in the enabled 8.0–8.2 family. Existing null/bitlength/arithmetic paths remain. This is not a new file version or a Huffman packet mode in modern CDP2/CDP3. Modern and transitional paths remain in regression coverage rather than being forced through an inapplicable encoding.

The inherited Huffman decoder lacked owned tree cleanup, singleton handling and escaped-value bounds, and did not honour the declared bit count inside the final word. The bounded replacement preserves the inherited heap's equal-frequency ordering, uses charged vector ownership, nonrecursive traversal, unsigned frequency sums, exact declared-bit bounds, OOB consumption checks and cancellation polls. The code-generation/canonicalization pass was unused by tree traversal and is removed. Existing upstream GPL notices and source hashes remain in the provenance manifest.

This subgate qualifies one probability context. Multiple-context Huffman remains explicitly rejected and tracked under C1; arithmetic multi-context support is unchanged. No authentic downloaded Huffman example has been established, so the new codec is qualified with original hand-calculated packets and full original model fixtures. This distinction must remain visible in the matrix.

Original tests cover known unequal/equal-frequency outputs, three-way ties, cross-word codes, a zero-bit single-symbol alphabet, ordinary/escape-only alphabets, unsigned high frequency, every truncated prefix, short declared bits with readable word padding, missing/extra OOB values, invalid contexts/frequencies, cancellation and allocation limits. Six full original model fixtures use Huffman strip boundaries 0,4,7: all three enabled 8.x revisions and both byte orders. Quantized geometry/UV/normal/constant-colour tests also exercise the new strip codec. Unknown-codec negative fixtures now use invalid codec 4.

Native results and the owner checklist are in [C1a validation](JT_HUFFMAN_VALIDATION.md). Later C1 transports/bindings and C2–C7 are pending. Owner acceptance of C1a will not imply completion of C1 or the whole feature pass.

## Accepted gate C1b — modern quantized colour transport

C1a is owner-accepted on 21 October 2026. C1b adds packed quantized RGB/HSV colour arrays for the enabled 10.x family, following the current modern collection. Known-value LE/BE tests span all seven enabled 10.x revisions and retain explicit older-family rejection. Full local display controls compare with the previously qualified lossless colour path; they do not establish authentic vendor-export conformance for every minor revision.

| Colour transport | Enabled 8.x | Enabled 9.x | Enabled 10.x |
| --- | --- | --- | --- |
| Earlier constant packed RGB(A) | Existing qualified path | Different collection | Different collection |
| Nonconstant legacy packed/component colours | Pending later C1 gate | Different collection | Different collection |
| Lossless component arrays | Existing raw bindings remain limited | C1c: exponent/mantissa transport | Existing IEEE/Lag1 path |
| Modern packed quantized RGB/HSV | Not applicable | C1c: four Lag1 component vectors | C1b owner-accepted |
| Non-opaque vertex alpha | Pending C2 renderer/appearance work | Pending C2 | Values can be decoded; active translucency remains explicitly rejected by scene adapter |

Do not route the 9.x component arrays through the modern packed reader. Zero-bit nonconstant quantizers remain an explicit unsupported case. Colour hashes are consumed but not yet verified; retain that item for consolidation. See [C1b validation](JT_QUANTIZED_COLOUR_VALIDATION.md) for the exact delivered subset, tests and owner checklist. C1b does not complete the broader colour/binding work or C2.

## Accepted gate C1c — transitional colour arrays

The owner accepted C1b on 21 October 2026. C1c adds the distinct 9.x lossless exponent/mantissa and quantized component-array transports for enabled 9.0/9.5. It reuses bounded quantizer and HSV helpers without changing the modern packed transport. Original field vectors cover both byte orders; private full-model controls are generated separately from authentic 9.0 and 9.5 single-part templates, not by reheadering 10.x geometry.

[9.x colour validation](JT_9X_COLOUR_VALIDATION.md) records the reference diagram, the always-present fourth quantized component vector, full-model agreement and remaining qualification limits. The 9.5 reference is not presented as a complete 9.0 specification or an independently generated 9.0 vertex-colour oracle. The owner accepted C1c on 21 October 2026 with "All tests passed". Variable 8.x colours, zero-bit nonconstant semantics, additional bindings, hash validation and non-opaque vertex-alpha rendering remain later gates.
