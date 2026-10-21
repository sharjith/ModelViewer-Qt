# Topological texture coordinates — Gate C1d

21 October 2026, `feature/JT-importer`. **Native validation and owner application build/UI tests passed.** C1a–C1c are owner-accepted. This gate adds channel-0, two-component UV preservation to enabled 9.x/10.x topological meshes. No new JT file revision is enabled; 10.11 remains deferred.

## Reference and scope

The locally retained public Siemens JT 9.5 Rev-A section 8.1.6, Figure 229 (printed page 271), and JT 10.0 section 13.1.5, Figure 241 (printed page 249), were rendered and inspected visually. The 9.5 lossless transport consists of paired exponent/mantissa vectors; the modern transport carries binary IEEE words with Lag1 prediction. Quantized UVs use one Lag1 vector per component and a uniform quantizer per component. The inverse quantization follows the published round-to-nearest encoder (9.5 section 8.2.1 / 10.0 section 13.2.1). The 9.5 diagram repeats `QuantBits = 0` on its right branch; the definitions identify that branch as the quantized representation. Section 7.2.2.1.2.6 / Figure 90, printed page 118, establishes colours before texture-coordinate arrays in the topological record.

This is not a complete 9.0 reference or an independent vendor UV oracle. Original field vectors cover the shared transitional family; private full-file controls preserve authentic 9.0, 9.5 and 10.0 geometry templates. Later enabled modern minors are exercised by original field tests and authentic assembly regressions, not claimed as independently exported textured files.

The original `JtDecode_VertexUV.hxx` validates header/component/packet counts, quantizer widths up to 24 bits, finite ordered ranges, exponent/mantissa fields and component codes. It supports constant zero-bit component quantizers; nonconstant zero-bit semantics remain unqualified. Finite UVs outside [0,1], including negative coordinates, are preserved for tiling. Hash words are consumed but not verified; verification remains C7 work.

`JtElement_ShapeLOD_Vertex` reads UVs after colours and retains them by the same coordinate/attribute pair used for normals/colours. A position shared by different UV records stays split even when normals and colours coincide or are absent. The existing scene adapter retains UVs through appearance variants, flat-normal expansion, scene copying and GLB export. The 8.x UV path is unchanged and remains in regression coverage.

Only channel 0 with two components is qualified here. Other channels/dimensions fail explicitly, allowing an available supported LOD alternative to load. Multiple UV channels and remaining C1 bindings stay on the plan. Source texture images, UV transforms and shader/material interpretation remain C2; a mesh with UVs still reports that texture images/shaders were not imported. No texture image is inferred from the presence of UV coordinates. No dependency or third-party source package is added; upstream notices and provenance are retained.

## Validation

- Isolated MSVC AddressSanitizer build and full portable suite, including all five prior 10.0 part regressions: pass.
- Original UV field vectors: both byte orders for 9.0/9.5 and 10.0/10.3/10.5/10.7/10.8/10.9/10.10; paired raw fields versus IEEE prediction, quantized predictors, negative/tiled values, 24-bit endpoints, constant zero-bit components, every truncated prefix, count/component/code/field/range rejection, cancellation and budgets: pass.
- UV-only seams and combined colour/normal/UV records: correct corner identity; stored/generated/flat normals, scene copies and GLB preservation: pass.
- Representative authentic regressions for all twelve enabled revisions: unchanged mesh/vertex/triangle counts, bounds, scene occurrence counts and attribute-channel summaries compared with C1c. These source assemblies are regression evidence, not authentic UV-feature exports.
- Three private geometry-template sets: lossless/quantized UVs agree; source positions, normals and indices remain unchanged; combined colour/UV records render magenta; invalid UV widths reject and an original wrapper selects the usable LOD alternative. Copies and GLB retain rendered UV corners. Assimp may remove unreferenced vertices on GLB export, so round-trip agreement is checked by rendered corners rather than exported vertex numbering.

`jt_uv_fixturegen <local-single-part-9.x-or-10.0.jt> <private-output-folder>` is a test-only modifier for observed little-endian single-part templates, not a general writer or independent conformance implementation. It does not reheader one version's geometry into another. Source-derived controls, exported GLBs and references remain ignored; placeholder hashes are not conformance evidence. Original byte-order coverage comes from field fixtures.

Ignored evidence: `coverage-c1d-build.log`, `coverage-c1d-test-build.log`, `coverage-c1d-tests.log`, `coverage-c1d-native-summary.json`, `coverage-c1d-native-*.log`, `coverage-c1d-fixtures-*.log` and `coverage-c1d-source-inventory.json` under private research storage.

## Owner build and acceptance checklist

Rebuild the application, then check `out/jt-research/uv-ui-fixtures/9.0/`, `9.5/` and `10.0/`:

| Control | Expected result |
| --- | --- |
| `lossless_uv.jt` and `quantized_uv.jt` | Same part, geometry, normals and appearance. One mesh; 218 vertices / 188 triangles in either 9.x set; 913 vertices / 904 triangles in the 10.0 set. Stored UVs should avoid a missing-UV generation prompt |
| `colour_and_uv.jt` | Same counts and geometry, magenta vertex colours alongside retained UVs |
| `uv_flat.jt` | Same triangles, visibly faceted normals; 564 vertices in 9.x, 2,712 vertices in 10.0 |
| `invalid_uv.jt` | Explicit invalid texture-coordinate quantization-width error |
| `uv_lod_fallback.jt` | One usable part loads with the ordinary lossless counts despite the invalid alternative |

For a visible UV check, assign `out/jt-research/uv-ui-fixtures/uv-checker.png` as a base-colour texture through the material editor to the lossless and quantized controls, using the same material parameters and existing UVs. Their patterns should match within each version. Do not regenerate UVs for this comparison. The same check on each companion `.jt.glb` exercises application import through a second route; GLB may omit unreferenced vertices. The checker is an original procedural diagnostic image; manually assigning it does not demonstrate JT texture-resource import.

Also check fit-to-screen/selection and reopen a previously accepted coloured control and an authentic assembly. The owner accepted C1d on 21 October 2026 with "C1d build complete and tested - passed". The agent did not build the full viewer.

## Subsequent multiple-set gate

[C1e](JT_MULTI_UV_VALIDATION.md) extends the previously accepted single-set transport to contiguous sets 0–3 without changing its field decoding. The single-set limitations above describe C1d's original scope; the later validation report governs the new multi-set handoff.
