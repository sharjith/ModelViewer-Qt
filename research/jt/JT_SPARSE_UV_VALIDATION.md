# Sparse texture-coordinate sets — Gate C1g

21 October 2026, `feature/JT-importer`. Native validation and owner application build/UI checks passed on 21 October 2026. C1f is accepted for UV transport, with its diagnostic coverage limitation retained. No additional JT version is enabled.

## Scope and representation

Enabled 9.x/10.x topological records can contain absent sets among scalar/two-component texture-coordinate sets 0–3. Each binding independently declares its component count; collections are consumed only for present bindings, in set order. This follows the independent per-set bindings and conditional collections in the retained Siemens 9.5/10.0 references used for C1d. It adds no codec and does not qualify three-/four-component coordinates, sets 4–7, source images or shaders. The legacy 8.x single-set transport is unchanged.

The JT decoder retains absent sets as absent, preserves original set numbers and uses the first present UV collection to establish attribute count when normals/colours are absent. UV-only seams in set 3 remain distinct even if sets 0–2 are absent. All present collections must match the common attribute count.

Assimp's UV-channel counting assumes contiguous arrays. The scene adapter therefore supplies explicit `(0,0)` arrays for missing sets below the highest present set. It never compacts or renumbers real data. These placeholders survive renderer copies, flat expansion and GLB; they are not claimed as authored source coordinates. No arrays are invented above the highest present set or on untextured models. GLB does not separately retain the source absence mask. This normalization also prevents the generic model loader from generating replacement UV0 coordinates simply because a higher source set exists.

## Native validation

- Isolated MSVC AddressSanitizer build, full test suite and five prior 10.0 part regressions pass. All twelve private source-template runs pass; all nine prior mixed scalar/2D GLBs remain byte-identical.
- Representative authentic-file regressions for all twelve enabled revisions pass with prior geometry counts, bounds, occurrence counts and attribute-channel summaries unchanged. These remain regression evidence, not authentic vendor sparse-UV qualification.
- Original binding checks cover all 16 presence masks across enabled 9.x/10.x revisions and both byte orders. Existing illegal multi-bit bindings, unsupported dimensions and higher-set rejections remain.
- Original corner tests cover UV-only set-3 seams, with/without normals, absent decoder slots, mismatched counts and out-of-range corner indices.
- Scene tests cover gaps at sets 0/2, present sets 1/3, source/generated/flat normals, zero padding, scene copy and GLB. Existing variants and palette tests remain regression coverage.
- Private source-derived controls are generated separately from authentic 9.0/9.5/10.0 templates with patterns `0002`, `0201`, `202` and prior contiguous `1212`. Zero means no source collection. Positions, normals, indices, scalar embedding, every padded value, rendered GLB corners, colour coexistence, flat expansion and invalid-layout LOD fallback are checked.
- These controls are diagnostic modifications with placeholder hashes, not vendor-export conformance evidence. They retain the earlier binary record-order UV patterns and do not provide useful surface unwrapping or full-image coverage.

Evidence stays ignored under research storage: `coverage-c1g-build.log`, `coverage-c1g-tests.log`, `coverage-c1g-fixtures-*.log` and native regression logs/summary. The agent does not build the full viewer or commit/push this gate.

## Owner application checklist

Rebuild, then use the folders under `out/jt-research/sparse-uv-ui-fixtures/9.5/` (also available under `9.0` and `10.0`). Apply the previous scalar diagnostic image with identical material settings; do not regenerate coordinates.

| Folder pattern | Expected source sets / scene behavior |
| --- | --- |
| `0002` | Set 3 is 2D; sets 0–2 sample a constant image point through zero placeholders |
| `0201` | Set 1 is 2D, set 3 is scalar U; sets 0/2 are constant zero placeholders |
| `202` | Sets 0/2 are 2D; set 1 is constant zero; no source set 3 |
| `1212` | Prior contiguous scalar/2D control remains unchanged |

In each folder, compare `lossless_uv.jt`, `quantized_uv.jt` and corresponding `.jt.glb` with Texture Coordinate 0–3. Matching real sets must have matching patterns and retain their original selector numbers. `colour_and_uv.jt` retains magenta colours; `uv_flat.jt` retains corresponding corner patterns. Constant placeholders are expected, not missing material application. Missing sets above the highest set are not part of the preservation check.

Expected ordinary counts: one mesh, 218 vertices / 188 triangles for 9.x; 913 vertices / 904 triangles for 10.0. Flat controls have 564 / 2,712 vertices respectively. `invalid_uv.jt` and `unsupported_uv_sets.jt` must fail explicitly; both fallback controls must load one usable mesh. Reopen an ordinary accepted assembly as regression. The owner confirmed the focused 9.5 sparse-set check with "Works" on 21 October 2026: constant sampling in slots 0–2, the stored pattern in slot 3, and matching lossless/quantized/GLB behavior. This accepts C1g; broader source-texture fidelity remains outside this transport gate.
