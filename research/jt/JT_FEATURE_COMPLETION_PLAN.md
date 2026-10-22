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

## Accepted gate C1d — topological UV arrays

C1c is owner-accepted. C1d preserves two-component texture coordinates in channel 0 for enabled 9.x/10.x topological mesh records. The 8.x UV path stays in regression coverage. Transitional exponent/mantissa and modern IEEE lossless transports are separate; quantized component vectors use the documented predictor and bounded inverse quantization. UV-only seams, combined colour/UV records and flat-normal expansion retain the correct corner association.

[Topological UV validation](JT_TOPOLOGICAL_UV_VALIDATION.md) records field tests, private controls and the owner checklist. Multiple UV channels, one-/three-/four-component texture coordinates, other remaining C1 encodings/bindings and C2 texture resources/shaders remain pending. This gate does not enable a new file version or complete C1. The owner accepted the application build/UI tests on 21 October 2026.

## Accepted gate C1e — multiple UV sets

C1d and the subsequent directory alignment/warning cleanup are owner-accepted. C1e extends the qualified topological UV path to four contiguous two-component sets (0–3), matching the existing viewer vertex layout. Lossless/quantized transports remain unchanged. All sets follow the same coordinate/attribute record mapping and survive appearance copies, palette splitting, flat-normal expansion, scene copies and GLB. The different 8.x path still exposes its existing single UV set.

[Multi-UV validation](JT_MULTI_UV_VALIDATION.md) records tests and owner texture-coordinate-selection checks. Sparse sets, sets 4–7, other component counts, remaining legacy codecs/colours/bindings and C2 source texture/image/shader semantics remain pending. No new file revision is enabled. The owner accepted C1e on 21 October 2026.

## Accepted gate C1f — scalar texture coordinates

C1e is owner-accepted. C1f adds scalar U collections and mixed one-/two-component sets to the same enabled topological families. The JT decoder retains the source component count; the scene adapter embeds scalar U as `(U,0)` pairs for the existing viewer/export path. [Scalar UV validation](JT_SCALAR_UV_VALIDATION.md) records original field and private model controls and the build/UI checklist. Source texture resources remain C2; sparse/higher sets, three-/four-component semantics and the other C1 items remain pending. The owner accepted UV transport and requested proceeding on 21 October 2026; diagnostic UVs remain unsuitable as an unwrap oracle. No new file revision is enabled.

## Accepted gate C1g — sparse texture-coordinate sets

C1f is owner-accepted for transport. C1g preserves independently absent scalar/2D sets within slots 0–3, consumes only present collections and retains UV-only corner seams without assuming set 0 exists. The scene adapter pads internal gaps with zero pairs for Assimp copy/export compatibility, preserving original real-set numbers. [Sparse UV validation](JT_SPARSE_UV_VALIDATION.md) records representation limits, native checks and owner build/UI instructions. No new file version is enabled; source textures, higher/projective sets and the remaining C1–C7 items stay pending. The owner accepted the focused sparse-set viewer checks on 21 October 2026 with "Works".

## Accepted gate C1h — declared arithmetic symbol counts

C1g is owner-accepted. C1h corrects the existing CDP1 arithmetic driver to consume the declared symbol count, including non-emitting context transitions after the last output, while enforcing output/OOB agreement and cancellation. Modern CDP2/CDP3 and other codecs retain their existing contracts. [Arithmetic count validation](JT_ARITHMETIC_COUNT_VALIDATION.md) records references, original packets/models and a geometry-only owner checklist. Multiple-context Huffman and the other unresolved C1 encodings/bindings remain pending; no new version is enabled. The owner confirmed all C1h application checks passed on 21 October 2026.

## Accepted gate C1i — rounded topological coordinate quantization

C1h is accepted and committed. C1i selects the published rounded uniform inverse for enabled 9.x/10.x topological coordinate collections, retaining the separate legacy midpoint path. It qualifies coordinate endpoints/interiors through 32-bit widths, count/component/axis-width checks, original field vectors and private full-model controls. [Coordinate quantization validation](JT_COORDINATE_QUANTIZATION_VALIDATION.md) records source and oracle limits, native evidence and a geometry-only owner checklist. Remaining legacy encodings/bindings, higher/projective UV layouts, zero-bit semantics and the broader C2–C7 work stay pending. No new file version is enabled. The owner confirmed C1i application checks passed on 21 October 2026.

## Accepted independent gate C2a — vertex alpha

The owner accepted C1i on 21 October 2026. C1 remains open for multiple-context Huffman, broader legacy colours/bindings, zero-bit semantics and higher/projective UV/data-model work; these are not prerequisites for appearance mapping of the already decoded RGBA subset. C2a independently retains active vertex alpha for raster rendering/GLB, selects an explicit transparent material, and separates shared-instance blend states. ONE/ZERO and blending-disabled cases stay opaque. Ray-traced/wireframe vertex-alpha parity and nonstandard blend-equation fidelity remain C2 residuals. [Vertex-alpha validation](JT_VERTEX_ALPHA_VALIDATION.md) contains the method, regression coverage and exact regeneration commands/checklist. The owner authorized local control build/generation on 21 October 2026; MSVC AddressSanitizer portable checks and generation passed. The full viewer was not built by the agent. The owner accepted the corrected shaded raster cube controls on 21 October 2026 and authorized commit/push. No new version is enabled.

## Accepted gate C2b — dedicated wire vertex alpha

C2a shaded raster transparency is owner-accepted and pushed as `3523fcf`. C2b applies already retained BLEND vertex alpha to triangle wires and heuristic feature edges, including shaded edge overlays, with scoped premultiplied blending/depth state. [Wire alpha validation](JT_WIREFRAME_ALPHA_VALIDATION.md) defines the existing-cube checklist and geometry-first limits. Owner accepted the build/viewer gate on 21 October 2026; no new build or GPU run by the agent. Ray-traced alpha and broader C2 features remain separate. The owner authorized commit/push and proceeding. No new JT version is enabled.

## Accepted gate C2c — ray-traced vertex alpha

C2b is accepted and pushed as `a343467`. C2c uses interpolated RGBA in existing CPU camera/shadow/guide alpha tests and carries RGBA through OptiX uploads/refits and any-hit coverage. [Ray-traced alpha validation](JT_RAYTRACED_ALPHA_VALIDATION.md) records prepared production CPU regressions and the separate CPU/OptiX owner gate using the same original cubes. The owner reported the requested tests passed on 21 October 2026 and authorized commit/push and proceeding. No agent build/run or separate optional-regression execution is claimed. No new JT version is enabled.

## Committed gate C2d/C2e — image textures

C2d/C2e was committed and pushed as `a2cb18e` at the owner's request. It adds bounded inline/external images, channel-0 2D mapping, UV/sampler transport and embedded-image material loading. The owner confirmed visible images, then explicitly confirmed the UV correction on 22 October 2026. [Image validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md) retains synthetic-oracle and authentic-export limits. Separate optional portable-suite execution is not inferred. No new supported revision was enabled.

## Accepted gate C2f — affine texture matrices

[Affine texture validation](JT_TEXTURE_TRANSFORM_VALIDATION.md) covers scale, offset, reflection, quarter-turn and shear mapping for the supported modern 2D image subset. Effective UVs are baked into appearance-specific meshes; image pixels remain shared, and matrix-aware material identity prevents shared-geometry cache aliasing. The owner reported all requested tests passed on 22 October 2026 and authorized commit/push/proceeding. The authoring tool ran; the agent did not build/run C++ or the viewer. Projective/generated coordinates, wider texture modes and legacy texture layouts remain open; no enabled version changes.
