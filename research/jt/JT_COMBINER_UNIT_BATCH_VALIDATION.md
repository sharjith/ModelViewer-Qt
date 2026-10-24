# Combiner/unit batch - C2ac

24 October 2026, `feature/JT-importer`, following accepted C2ab and the cross-format PBR smoke check. **Owner accepted the grouped viewer and six representative export comparisons on 24 October 2026.** No C++ build, native test, importer execution or viewer inspection was performed by the agent for this gate.

## Implemented portion of Batch 1

The existing application binds one base-colour image. This gate extends the exact, bounded adapter representation without introducing a renderer-only texture language that would disappear on copy/export or differ across backends.

| Area | Prepared behavior | Limit |
| --- | --- | --- |
| Constant vertex RGB | Compose identical per-vertex RGB as the incoming colour for Decal/Blend/Add and qualified stacks; neutralize RGB on the appearance copy only after image and coordinate mapping succeed | RGB must be identical. Incoming alpha remains on vertices, may vary, and is removed only when a qualified Replace discards it. Varying-RGB affine sums still need a richer representation |
| Modulate/Replace vertex stacks | Compose from neutral incoming factors; preserve incoming RGB/alpha unless a source-format Replace discards that component. This factorization also applies to varying vertex colours | Source sampling must satisfy the existing co-sampled composition restrictions. Visual/native controls here use constant legacy vertex input, not a full varying-colour qualification |
| Wider vertex Replace | Luminance keeps incoming alpha; luminance-alpha replaces both; alpha-only keeps incoming RGB; decoded external RGB/RGBA follows its actual components | Qualified colour layouts only. Non-colour internal formats remain diagnosed |
| Different image resolutions | Use the least common multiple of widths/heights to retain every source texel boundary. Replicate nearest texels on that grid and compose in ascending unit order | Common UV mapping, wraps and border footprint; nearest base-level min/magnification; no authored/generated mip filtering on differing sizes. Output is capped at 64 MiB RGBA8 before allocation |
| Preservation | Keep original material factors in existing source properties, and source colours/geometry untouched in caches; change only the successful appearance copy | Existing source-metadata GLB limitations remain. Display pixels/factors use the ordinary material/export path |

Composition retains the existing linear RGB/sRGB-image and linear-alpha contract, with RGBA8 display quantization. Constant incoming vertex RGB participates in composition-cache identity. Vertex alpha is kept as a separate factor rather than quantized into PNG, preserving its existing transparent-depth classification unless Replace explicitly discards it. For Modulate/Replace stacks, neutral input and retained vertex components avoid applying vertex alpha twice. Blending-disabled inputs remain opaque, while RGBA Decal still uses texture alpha to mix RGB. Existing filter restrictions remain: constant-input single-stage Blend and RGB Decal are affine and may retain linear filtering; RGBA Decal, saturated Add and products of independently filtered stages cannot generally be flattened with linear/trilinear filtering.

The common nearest grid is a texel partition, not an approximate UV bake. For two stages of width 2 and 3, the six output cells preserve both source boundaries. Non-dividing two-dimensional sizes are handled independently along each axis. GCD-first checked arithmetic bounds the resulting dimensions/area, allocations are charged, and row/inner-loop cancellation polling remains active. Mismatched filtered/mipmapped grids are rejected rather than silently resampled. Existing same-size authored mip composition retains its per-level grid sizes.

Material/mesh cache identity also retains the ordered source program (operation, decoded image/internal format and blend colour). Before examining source vertex colours, mesh caches consult a material-factor composition. Different programs can coincide for that material input but differ for vertex input: white Modulate and saturated white Add are one example. Derived pixels alone cannot identify that program. A prepared shared-geometry regression distinguishes those two appearances while requiring a repeated identical program to retain sharing. No new source decoder or global material representation is introduced.

## Explicitly unfinished Batch 1 scope

This is not the completion of arbitrary multi-texturing. Different UV sets/transforms/wraps per stage, arbitrary independent min/mag filters, nonlinear filtered composition and varying-input Decal/Blend/Add still need a richer material/evaluation/export representation. This gate does not approximate them. Other roles, image/border profiles, generated/projective mapping, JT8 evidence and authentic exporter qualification remain separate.

The retained JT10.0 reference identifies Combine through the OpenGL environment mode but the parsed descriptor supplies no qualified RGB/alpha function, sources, operands or scales. The retained OpenGL reference makes those independent state variables. No authentic/reference Combine example resolving that contract is available in the recorded evidence. Combine therefore remains a diagnosed evidence/representation gap; assigning guessed operands or treating it silently as Modulate would not close the requirement.

## Prepared native coverage

The existing `jt_texture_replace_tests.cpp` now includes literal constant-RGB Decal/Blend/Add colours with separately retained vertex alpha, blending-disabled cases, original material-factor preservation, wider/external Replace component selection, rejected filtered RGBA Decal fallback, an ordered constant-vertex stack, alpha-only Replace inside a stack and coincident-output program-cache sharing. Existing single-stage Replace/Modulate sharing and missing-UV checks remain. Qualified family loops cover both byte orders and enabled 9.x/10.x envelopes/local versions; the geometry companion uses the accepted original 8.x constant-colour layout. Varying legacy colour transport is not invented.

`jt_texture_batch_tests.cpp` replaces the obsolete blanket different-resolution rejection with a literal six-cell 2-by-3 texel-partition check. It retains independent mapping/filter/Combine rejection, adds a coprime-grid expansion limit check and prepares deep-copy/GLB pixel preservation for the new grid. Existing inheritance/reset/inhibit/final/forced, unit-order and automatic-allocation checks remain. These are prepared assertions, not execution results.

Use the existing [owner native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls). All native output stays under ignored `out/`. Viewer acceptance does not imply that the optional native suite or each CPU/OptiX backend ran.

## Original controls and regeneration

From the repository root, after the accepted C2ab controls exist:

```powershell
python -B tools/jt/make_texture_combiner_batch_controls.py out/jt-research/texture-vertex-replace-ui-fixtures out/jt-research/texture-combiner-batch-ui-fixtures
```

The input controls can be regenerated using the preceding commands in [C2ab validation](JT_VERTEX_REPLACE_VALIDATION.md). All assets use the [common store](JT_SHARED_TEXTURE_CONTROLS.md); shared geometry, image and buffer payloads are content-addressed rather than copied into each case folder.

The generator authors 78 positive roots, 13 independent comparison pairs, a 31-document grouped launch list and three representative export pairs. Roots use original 10.0, 9.0 version-1 and 9.5 version-3 texture envelopes in both byte orders with original 8.2 geometry. This is mixed-family synthetic evidence, not authentic same-version exporter agreement. No failure cases enter the viewer list.

- Gate manifest and source hashes: `out/jt-research/texture-combiner-batch-ui-fixtures/authoring.json`.
- Launch list: `out/jt-research/c2ac-visual-files.json`.
- Source roots/reference aliases: `out/jt-research/texture-common-ui-fixtures/`.
- Owner export destinations: `out/jt-research/texture-combiner-batch-ui-fixtures/exports/`.

Each source below is `texture-combiner-batch_<case>_10_0_le.jt`; its independent reference is `reference_combiner_batch_<case>.gltf` in the same common folder.

| Case | What to inspect |
| --- | --- |
| `vertex_decal_rgb_linear` | Texture RGB replaces the incoming tint; translucent coverage agrees |
| `vertex_decal_rgba_nearest` | Texture alpha mixes the source RGB, while incoming alpha remains the coverage |
| `vertex_blend_rgba_linear` | Red/blue pattern blends toward the authored environment colour; edges soften with zoom |
| `vertex_add_rgba_nearest` | Added colour saturates correctly; nearest cells stay sharp |
| `vertex_replace_luminance_linear` | Grayscale replaces RGB and keeps incoming transparency |
| `vertex_replace_luminance_alpha_linear` | Grayscale and image alpha both replace vertex input |
| `vertex_replace_alpha_linear` | Vertex tint remains; image alpha controls transparency |
| `vertex_replace_external_rgba` | Decoded external image replaces both components, without the old vertex tint |
| `vertex_stack_modulate_add` | Ordered multiplication/addition agrees with the reference, without doubled alpha |
| `vertex_stack_modulate_replace_alpha` | Modulated vertex RGB remains; the upper alpha-only Replace controls coverage |
| `vertex_multires_modulate` | Different source partitions produce the same six-by-six pattern as the reference |
| `material_multires_decal` | Mixed-resolution Decal agrees without vertex colours |
| `vertex_inherited_multires` | A child overrides the lower unit without dropping or reordering the inherited upper unit |

Two representative big-endian 9.x documents and accepted RGBA Replace/Blend linear regressions complete the 31-document list. All comparisons require matching Y-Up and standard isometric views. Inspect ADS and PBR; compare pattern placement, tint, visibility of the red inner cube and edge filtering. No new renderer or light-state code is part of this gate.

After building, the agent can launch all listed documents in one installed viewer invocation. The owner performs visual acceptance. Do not open one process per document or launch negative/fail cases.

For representative application round trips, export the **JT source** for `vertex_replace_external_rgba`, `vertex_stack_modulate_replace_alpha` and `vertex_inherited_multires` as both GLB and glTF. The six full destination filenames are in the manifest's `export_pairs`. Reopen them and compare against the corresponding independent reference in the same up-axis/view. This is six exports, not a request to export the reference or overwrite original controls. The prepared native round trip separately targets mixed-grid image preservation.

## Evidence and provenance

The authoring command completed. Read-only inventory verified all 78 root hashes, the 13 independent references' image/buffer paths and the three export-pair records. Python syntax was inspected and diff whitespace checks passed. These establish authoring/resource consistency only; the importer and renderer have not been executed for this gate.

Consulted existing original adapter/composer and fixture code, the retained JT10.0 Rev-B texture environment definition and retained OpenGL 2.1 texture-function/Combine state definitions. Primary URLs and retained hashes are recorded in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). Rechecked PDF SHA-256: JT10 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; OpenGL 2.1 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`. No new download, third-party implementation, asset, vendor software, SDK/schema, disassembly or licence interpretation was used. Source documents and generated evidence remain local. This records method and qualification limits, not legal clearance.


## Owner viewer result - 24 October 2026

The owner reported the build succeeded. The agent launched all 31 positive checklist documents in a single installed-viewer invocation. The owner then reported "Looks good - tested in ADS and PBR, consistent across". This accepts the grouped source/reference viewer comparisons. The three representative application export pairs (six outputs) remain pending; no export result or prepared native-suite execution is inferred, and C2ac is not yet recorded as fully accepted.


## Owner export acceptance - 24 October 2026

After the ADS/PBR comparison pass, the agent launched the three JT export sources and their independent references together. The owner exported each requested source as GLB and glTF, reopened the six outputs and reported "All passed". This accepts the bounded C2ac viewer/application export gate and releases its commit hold. Earlier prepared/pending statements above are historical. The prepared native suite and individual CPU/OptiX execution remain unreported; no such result is inferred from the viewer/export acceptance. Combine evidence, arbitrary independent sampling, varying-RGB affine sums and the other C2 residual areas remain explicitly open.
