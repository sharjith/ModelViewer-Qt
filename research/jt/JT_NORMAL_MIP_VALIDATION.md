# Tangent-normal mip/filter extension - C2ag

24 October 2026, `feature/JT-importer`, following accepted C2af `fe6d623` and shared unlit correction `7a18b9d`. **Owner-accepted bounded build/visual/export gate.** No C++ build or native suite was run by the agent for C2ag. Earlier pending statements below are historical.

## Bounded extension

C2af qualified one automatic tangent-normal image on original stored UV0 and a positive-handed nondegenerate chart, with base-level nearest/linear filtering. C2ag removes the extra normal-role rejection of authored mip levels and mip minification filters. The already qualified image decoder validates complete authored chains, converts each RGB/BGR/RGBA/BGRA byte level to canonical vector bytes, and forces every level's unused alpha opaque. No colour-composition or gamma conversion is applied to normal data. All four mip minification filter choices are retained independently of a colour texture's sampler.

The application already uploads authored levels into normalized RGBA8 raster textures and snapshots them through the CPU/OptiX shared texture path. The shaders/normal samplers interpret the normalized components as vector data and normalize after sampling. When no authored chain exists, the application's existing GPU/CPU mip-generation policy is used; this is not a claim that vendor/driver kernels are identical or a new normal-aware variance/roughness filter. No renderer or tangent-basis calculation is changed in this gate.

The original identity mapping, UV0, nondegenerate positive-handed chart and single-normal-role limits remain. Normal borders, transformed/generated/mirrored normal charts, normal-scale/bump semantics, independent normal coordinate selection and authentic source encoding/basis evidence remain open. This advances a bounded part of the normal role; other C2 residual packages and JT versions are unchanged.

Authored levels continue to use the existing **ModelViewer-private PNG mip carrier**. It is not standard glTF authored-mip interoperability. Other applications may ignore the metadata and regenerate levels. The viewer/export gate therefore qualifies the existing ModelViewer application path, not an external glTF mip contract.

## Original controls and owner gate

Regenerate using the accepted shared C2af original assets:

```text
python -B tools/jt/make_texture_normal_mip_controls.py out/jt-research/texture-normal-mip-ui-fixtures
```

The authoring manifest is `out/jt-research/texture-normal-mip-ui-fixtures/authoring.json`. Thirty-six original roots span 10.0, advanced 9.0 and advanced 9.5 in both byte orders. Six pairs, two legacy-family representatives and one accepted post-lit regression pair give **16 documents** in `out/jt-research/c2ag-visual-files.json`. Named references and content-addressed original geometry/images remain under ignored `out/jt-research/texture-common-ui-fixtures/`.

| Case stem | Expected behaviour in matching Y-Up ADS/PBR views |
| --- | --- |
| `nearest_mip_nearest_rgb` | Blocky normal pattern nearby; discrete authored shading changes as zoom selects successive levels |
| `linear_mip_nearest_bgr` | Smooth within-level sampling, discrete level selection; same shading and transitions as the reference |
| `nearest_mip_linear_rgba` | Blocky within-level pattern with blending between the deliberately different normal directions of adjacent levels |
| `trilinear_bgra` | Smooth normal sampling and blended authored shading transitions |
| `generated_trilinear` | No deliberately different authored directions; existing generated mip smoothing matches the reference |
| `trilinear_with_base` | Authored normal transitions agree while the independent colour checker remains correctly placed and translucent |

Each source is `texture-normal-mip_<case>_10_0_le.jt`, with reference `reference_normal_mip_<case>.gltf`. Images are 256x256 with deliberate normal directions at the eight lower levels through 1x1. RGB/BGR(A) normalization uses independently explicit opaque reference vectors. Source RGBA alpha varies, including zero; it must not make a normal-only cube transparent. No authentic vendor model/export was used.

After owner build/install success, verify build/install executable hashes and launch the grouped list once. Compare at the initial scale, then zoom each pair out by similar amounts until the normal-induced light/dark patches change. Compare direction, smooth/discrete transitions and opacity, not exact mouse-wheel thresholds. The accepted post-lit pair remains a regression check. A successful build alone does not accept the visual or export gate.

After visual acceptance, export these JT sources to the existing ignored `texture-export-ui-fixtures` folder:

| Source case | GLB | glTF |
| --- | --- | --- |
| `trilinear_bgra` | `normal_mip_trilinear_bgra.glb` | `normal_mip_trilinear_bgra.gltf` |
| `trilinear_with_base` | `normal_mip_trilinear_with_base.glb` | `normal_mip_trilinear_with_base.gltf` |

Reopen and repeat the ADS/PBR zoom comparisons. Exact paths are in `out/jt-research/c2ag-export-checklist.json`. Read-only inspections after owner export:

```text
python -B tools/jt/inspect_texture_role_exports.py out/jt-research/c2ag-export-checklist.json out/jt-research/c2ag-export-inspection.json
python -B tools/jt/inspect_texture_mapping_exports.py out/jt-research/c2ag-export-checklist.json out/jt-research/c2ag-export-uv-inspection.json
```

The role inspector now compares every authored level's dimensions/pixel hash and terminal metadata as well as base pixels, sampler/material/alpha values. The effective-UV inspector follows the base-colour slot when present, otherwise the normal slot, covering the shared original UV0 of both selected controls. The normal-map visual basis check remains a separate owner observation.

## Prepared checks and status

The original generator completed, checked all root hashes and named visual files, and read back the independent reference carriers/resources with the expected one or nine normal levels. Python syntax and whitespace review remain static analysis, not native execution. Extending the role inspector retained 4/4 passes on the accepted C2af outputs.

`textureRoleTests()` now prepares every mip filter across enabled original 9.x/10.x layouts, literal normal-vector bytes/unused alpha at base and lower levels, retained sampler keys, malformed-tail fallback without silently using the base, scene copy and representative GLB carrier retention. It remains registered in the owner-run JT suite and was not built/run by the agent. Individual CPU/OptiX, native suite and authentic exporter qualification are not inferred from the forthcoming raster gate.

## Owner build and visual acceptance - 24 October 2026

The owner reported successful build/install. Build and installed executable hashes matched before the grouped 16-document launch. After the requested Y-Up ADS/PBR comparisons, including matched zoom and normal-only opacity checks, the owner reported "All work as expected". The grouped visual gate is accepted. Four representative application GLB/glTF export/reopen comparisons remain pending; native-suite or individual CPU/OptiX execution is not inferred.

## Final owner acceptance - 24 October 2026

After the requested two JT source exports and reopen comparisons, the owner reported "Works perfect". All four requested GLB/glTF outputs are present. Read-only inspection with the commands above reports 4/4 image, authored-level, sampler and material comparisons passing, and 4/4 effective-UV comparisons within the original quantization allowance. The grouped visual and representative application export gate is accepted. This does not establish external glTF authored-mip portability, authentic normal encoding/basis, exhaustive variant/backend coverage or native-suite execution. Previously recorded C2 residuals remain open.

## Provenance

Consulted original adapter/image/mip carrier, shader, upload/snapshot, sampler/export and fixture code plus accepted C2af/C2x records. No new external reference, download, third-party implementation/asset, vendor software/SDK, schema, disassembly or licence interpretation. The retained JT/OpenGL references, URLs and hashes are those in [C2af provenance](JT_TEXTURE_ROLE_VALIDATION.md#evidence-and-provenance). Normal encoding/basis remains the explicit bounded application contract already recorded there. New controls and assertions are original. This records method and qualification limits, not legal clearance.
