# Multiple texture-coordinate sets — Gate C1e

21 October 2026, `feature/JT-importer`. **Native checks and owner application build/UI tests passed.** C1d, directory alignment and deprecated-include cleanup are owner-accepted. This gate adds no file revision and leaves 10.11 deferred.

Subsequent [C1g sparse-set work](JT_SPARSE_UV_VALIDATION.md) supersedes the sparse-layout rejection described for this historical gate; the owner accepted its focused application check on 21 October 2026.

## Delivered subset

Enabled 9.0/9.5 and 10.0/10.3/10.5/10.7/10.8/10.9/10.10 topological meshes now retain up to four contiguous, two-component UV sets, numbered 0–3. These match the existing application vertex layout and Texture Parameters coordinate selector. The different legacy 8.x layout retains its existing single-set path; modern bindings are not forced onto it.

The qualified UV field transports from C1d are unchanged. The public Siemens 9.5 topological-record diagram (section 7.2.2.1.2.6 / Figure 90, printed page 118, inspected during C1d) describes repeated UV collections after colours; the U64 bindings identify each collection. No additional decoder source, SDK or dependency is introduced. The 9.5 reference is not presented as a complete 9.0 specification or an independent vendor multi-set oracle.

`JtElement_ShapeLOD_Vertex` retains four arrays and expands each using the same coordinate/attribute identity as normals and colours. A seam that exists only in set 3 remains split even when sets 0–2, positions and normals coincide. Binding validation rejects holes in the set sequence, sets 4–7, other dimensions and invalid multi-bit component flags explicitly. These are current qualification/runtime boundaries, not claims that those layouts are invalid JT.

Original scene-adapter paths now retain every supported set while converting to Assimp, copying appearance variants, extracting palette face groups and expanding flat normals. Scene copying and GLB export keep all four sets. Resource budgets account for allocated arrays. The diagnostic probe checks finite coordinates in every present Assimp UV channel.

No texture-image/shader/UV-transform records are imported by this gate. Manually attaching a texture is an application transport/render check; it does not demonstrate JT texture-resource support. Higher/sparse/different-dimensional sets remain C1 work, while source texture resources and mapping semantics remain C2.

## Native evidence

- Isolated MSVC AddressSanitizer build and full suite, including all five existing 10.0 part regressions: pass.
- Original binding tests across every enabled 9.x/10.x revision and both byte orders: zero through four sets, sparse layouts, an unsupported fifth set, wrong dimensions and invalid component flags. Existing C1d field truncation/count/finite/range/predictor/budget/cancellation tests remain passing.
- Original seam test: only the fourth set differs at a shared position, with and without normals/colours. Attribute-count/index mismatches and sparse arrays reject.
- Original combined-colour scene tests: four distinct UV sets through stored/generated/flat normals, appearance variants, scene copying and GLB. Palette fixtures verify coordinate association for all sets after material splitting and GLB.
- Private source-derived controls independently use authentic 9.0, 9.5 and 10.0 single-part geometry templates. One-, two- and four-set generator runs pass lossless/quantized agreement, original positions/normals/indices, scene copies, GLB rendered-corner comparison, flat-normal remapping, invalid-width rejection and unsupported-five-set rejection/fallback. GLB may omit unreferenced vertices; rendered corners are the comparison unit.
- The nine prior single-set positive GLBs are byte-identical to the C1d controls after this change.
- Representative authentic regressions across all twelve enabled revisions retain prior mesh/vertex/triangle counts, scene occurrences, local bounds and attribute-channel summaries. These assemblies provide regression coverage, not authentic multi-UV vendor-export conformance.
- Provenance check: 119 files, 104 upstream-derived and 15 ModelViewer-original; upstream identities/notices retained. No public source geometry or PDF is added.

Ignored logs are `coverage-c1e-build.log`, `coverage-c1e-tests.log`, `coverage-c1e-fixtures-<version>-<sets>.log`, `coverage-c1e-native-summary.json`, `coverage-c1e-native-*.log` and `coverage-c1e-single-set-comparison.json` under research storage. The fixture modifier takes an optional count: `jt_uv_fixturegen <local-source> <private-folder> 4`. Omitting the count keeps one-set generation. It remains limited to observed little-endian single-part 9.0/9.5/10.0 templates; big-endian binding/field coverage comes from original vectors. Controls use placeholder hashes and are not independent conformance evidence. Source-derived files stay private/ignored.

## Visual coverage limitation of the diagnostic UVs

The private fixture modifier assigns binary U/V values by attribute-record order; it does not unwrap the CAD part. These transport patterns have only four distinct 2D UV pairs (or two scalar U values). Many geometrically valid triangles consequently have zero UV area and sample only a line or point in the image. This produces broad constant-colour regions, thin stripes and stretched patterns even when every UV value is transported correctly. A texture can be applied to the whole mesh while large parts of the image are never sampled on a particular face.

The exported 9.5 control audit found 115/188 zero-area UV triangles in set 0, 158/188 in set 1, 164/188 in set 2 and 175/188 in set 3 for the original four-set 2D controls. Mixed scalar controls intentionally have zero 2D UV area in every triangle of scalar sets 0/2 because V=0; their 2D sets have the same coverage limitation as the earlier controls. The private numerical audit is `out/jt-research/uv-coverage-audit.json`.

These controls qualify preservation, indexing, prediction, channel selection and agreement between lossless/quantized/JT/GLB paths. They do not qualify aesthetically useful unwrapping or full-image surface coverage. No UV regeneration or importer correction is justified by this fixture appearance alone. Authored, properly unwrapped source UVs and an independent viewer comparison remain needed for that visual-fidelity check. Owner tests should compare corresponding patterns rather than expect the complete image to map neatly over the part.

## Owner build and UI checklist

Rebuild the normal viewer. In each folder under `out/jt-research/multi-uv-ui-fixtures/` (`9.0`, `9.5`, `10.0`), open controls separately:

| Control | Expected result |
| --- | --- |
| `lossless_uv.jt`, `quantized_uv.jt` | Same part and geometry; four UV sets. One mesh, 218 vertices / 188 triangles in 9.x, or 913 vertices / 904 triangles in 10.0 |
| `colour_and_uv.jt` | Same geometry/counts, magenta vertex colours and four retained UV sets |
| `uv_flat.jt` | Same triangles with flat normals; 564 vertices in 9.x, 2,712 in 10.0; four retained sets |
| `invalid_uv.jt` | Invalid texture-coordinate quantization-width error |
| `unsupported_uv_sets.jt` | Explicit unsupported texture-coordinate layout error for five sets |
| `uv_lod_fallback.jt`, `uv_layout_fallback.jt` | One usable part with the ordinary lossless counts and all four sets |

For a visible test, assign `out/jt-research/multi-uv-ui-fixtures/uv-checker.png` as a base-colour texture to the lossless and quantized controls. In Texture Parameters, change **Texture Coordinate** through 0, 1, 2 and 3. Patterns should change between sets and agree between lossless and quantized controls at the same selected set. Do not regenerate UVs. Check the companion `.jt.glb` at matching material/coordinate settings; selection, fitting and geometry should also remain correct. The flat control should preserve the same pattern at each corresponding set while changing the shading. The magenta control separately checks combined colour/UV handling.

Reopen an accepted C1d single-set control and an authentic assembly as integration regressions. The owner accepted C1e on 21 October 2026 with "done, passed". The agent did not build the full viewer.

## Subsequent scalar-binding gate

[C1f](JT_SCALAR_UV_VALIDATION.md) extends the accepted repeated-collection path to scalar U and mixed scalar/UV sets. Scalar values are embedded as `(U,0)` pairs in the viewer/GLB representation. The two-component-only boundary above records C1e's original scope.
