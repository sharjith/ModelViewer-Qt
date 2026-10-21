# Scalar texture-coordinate collections — Gate C1f

21 October 2026, `feature/JT-importer`. **Native controls and owner application build/UI tests accepted on 21 October 2026.** C1e is owner-accepted. No additional JT file version is enabled, and 10.11 stays deferred.

Subsequent [C1g sparse-set work](JT_SPARSE_UV_VALIDATION.md) supersedes the sparse-layout rejection described for this historical gate; the owner accepted its focused application check on 21 October 2026.

## Scope and representation

Enabled 9.x/10.x topological meshes now accept one-component texture-coordinate collections alongside the previously accepted two-component collections, including mixed dimensions across contiguous sets 0–3. The existing 8.x transport is unchanged. This follows the component counts already represented by the qualified U64 bindings and the repeated-component collections described in the retained Siemens 9.5/10.0 UV references used for C1d. It is not a new texture-resource or shader implementation.

`JtDecode_VertexUV` reads exactly the declared number of quantizers and component vectors. A scalar quantized collection has one quantizer and one vector; it must not consume a nonexistent V quantizer/vector or shift the next collection/hash. The separate 9.x exponent/mantissa and modern IEEE lossless transports retain their existing prediction rules. Finite negative/tiled U values are retained. The decoder and corner-expanded arrays retain the source component count.

The scene adapter embeds scalar U as an explicit two-component `(U,0)` pair. Direct one-component Assimp output failed the GLB transport checks, so two-component scene/export arrays are intentional. This retains numeric U values and gives the existing viewer and GLB path the same representation. The GLB representation does not encode the original scalar dimension separately. Source texture-image interpretation, 1D sampling resources and shader mapping remain C2; this normalization must not be described as complete source texture fidelity.

Three-/four-component coordinate semantics, sparse UV sets and sets above 3 remain explicit unsupported cases. No proprietary SDK/schema or additional dependency/source package is added. Existing upstream identities, notices and manifest hashes remain intact.

## Native tests

- Original scalar lossless/quantized vectors across all enabled 9.x/10.x revisions in both byte orders verify U values, negative/tiled ranges, complete consumption and every truncated prefix. Mixed scalar/2D bindings preserve channel component identities. Existing count/quantizer/finite/code/cancellation/budget tests remain passing.
- Combined scene tests use scalar sets 0/2 and 2D sets 1/3. Stored/generated/flat normals, appearance variants, scene copies and GLB preserve every U value and scalar V=0 embedding.
- Isolated MSVC AddressSanitizer full suite and the five prior 10.0 part regressions pass.
- Private source-derived fixture runs use authentic 9.0, 9.5 and 10.0 single-part geometry templates, with patterns `1`, `1212` and `2222`. Lossless/quantized streams agree; original positions/normals/indices, copy/export rendered corners, flat mappings, malformed-width rejection and unsupported-layout fallback pass. Scalar outputs have V=0 at every vertex.
- Final representative regressions for all twelve enabled revisions pass with prior geometry counts, bounds, occurrences and attribute-channel summaries unchanged. These are regression checks rather than authentic scalar vendor-export qualification.
- All nine prior four-set 2D GLBs are byte-identical after this extension. The private controls are diagnostic modifications with placeholder hashes, not authentic scalar vendor exports or independent conformance oracles. They remain ignored and are not redistributed by inference.

The standalone generator accepts `jt_uv_fixturegen <local-source> <private-folder> 4 1212`: the last pattern declares the component count for each set. Omitting the pattern retains the earlier all-2D behavior. Source templates remain limited to observed little-endian 9.0/9.5/10.0 single parts; endian coverage comes from original field/binding fixtures. Ignored evidence is under research storage: `coverage-c1f-build.log`, `coverage-c1f-control-build.log`, `coverage-c1f-tests.log`, `coverage-c1f-fixtures-*.log`, `coverage-c1f-two-component-comparison.json` and the native regression summaries/logs.

## Visual coverage limitation of the diagnostic UVs

The private fixture modifier assigns binary U/V values by attribute-record order; it does not unwrap the CAD part. These transport patterns have only four distinct 2D UV pairs (or two scalar U values). Many geometrically valid triangles consequently have zero UV area and sample only a line or point in the image. This produces broad constant-colour regions, thin stripes and stretched patterns even when every UV value is transported correctly. A texture can be applied to the whole mesh while large parts of the image are never sampled on a particular face.

The exported 9.5 control audit found 115/188 zero-area UV triangles in set 0, 158/188 in set 1, 164/188 in set 2 and 175/188 in set 3 for the original four-set 2D controls. Mixed scalar controls intentionally have zero 2D UV area in every triangle of scalar sets 0/2 because V=0; their 2D sets have the same coverage limitation as the earlier controls. The private numerical audit is `out/jt-research/uv-coverage-audit.json`.

These controls qualify preservation, indexing, prediction, channel selection and agreement between lossless/quantized/JT/GLB paths. They do not qualify aesthetically useful unwrapping or full-image surface coverage. No UV regeneration or importer correction is justified by this fixture appearance alone. Authored, properly unwrapped source UVs and an independent viewer comparison remain needed for that visual-fidelity check. Owner tests should compare corresponding patterns rather than expect the complete image to map neatly over the part.

## Owner application checklist

Rebuild, then use the three folders under `out/jt-research/scalar-uv-ui-fixtures/`: `9.0`, `9.5`, `10.0`. The controls use scalar sets 0/2 and 2D sets 1/3.

| Control | Expected behavior |
| --- | --- |
| `lossless_uv.jt`, `quantized_uv.jt` | Same geometry and UV sampling; one mesh, 218 vertices / 188 triangles in 9.x, or 913 vertices / 904 triangles in 10.0 |
| `colour_and_uv.jt` | Same counts with magenta vertex colours and mixed scalar/2D sets |
| `uv_flat.jt` | Same triangles, flat shading; 564 vertices in 9.x or 2,712 in 10.0; UV values remain associated with the same corners |
| `invalid_uv.jt`, `unsupported_uv_sets.jt` | Explicit quantization-width / unsupported-layout errors respectively |
| `uv_lod_fallback.jt`, `uv_layout_fallback.jt` | One usable part with the ordinary lossless counts and all four supported sets |

Assign the original procedural `out/jt-research/scalar-uv-ui-fixtures/uv-diagnostic.png` as a base-colour texture to lossless and quantized controls. With identical material parameters, select Texture Coordinate 0–3. Sets 0/2 vary U with V fixed at zero; sets 1/3 retain both coordinates. Compare matching sets across lossless, quantized and companion `.jt.glb` controls; do not regenerate UVs. The diagnostic uses red/green coordinate gradients plus a blue pattern; material lighting/filtering affects exact displayed colours. Repeat on the flat control to check corner association without expecting smooth shading.

Reopen an accepted C1e four-set 2D control and an authentic assembly as regressions. The owner accepted C1f transport and requested proceeding on 21 October 2026; the diagnostic UV coverage caveat above remains. The agent did not build the full viewer or commit/push C1f.
