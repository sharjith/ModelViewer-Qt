# Bounded tangent-normal and post-lit roles - C2af

24 October 2026, `feature/JT-importer`, after accepted C2ae `2eae925`. **Owner-accepted bounded build/visual/export gate.** No C++ build or native suite was run by the agent. Earlier pending/failed records below are historical.

## Representation and scope

The retained JT10.0 Rev-B type definitions distinguish explicit 2D post-lit images (type 2), pre-lit images (9) and automatic tangent-space three-component normal images (4). The advanced role numbering must not be confused with the basic 9.x image enum: the existing basic type 2 still normalizes to pre-lit type 9. Automatic normal assignment retains the existing accumulation, explicit-unit displacement, inhibit/final and reset rules.

This pass prepares two bounded adapters:

- One automatic type-4 normal image becomes an independent `aiTextureType_NORMALS` slot. Original stored UV0 and a finite, nondegenerate, positive-handed tangent chart are required. The application interprets unsigned RGB as the usual normalized tangent vector `2*RGB-1`. RGB, BGR, RGBA and BGRA base-level byte images and bounded external PNG/JPEG decoding use the existing image foundation. Image alpha is unused and forced opaque; it never makes the material transparent. Nearest/linear non-mip filtering and repeat/mirror/edge sampling are supported. Image borders, authored mip chains, mip filters, transformed/generated normal UVs, multiple normal roles and mirrored/inverted charts remain diagnosed and unapplied. A colour texture may coexist with independent sampling, provided its mapping does not rewrite normal UV0 or refine topology through legacy clamp.
- One explicit post-lit RGB/RGBA Replace, or RGB-only Decal, completely replaces the lit RGB result and can use the standard unlit material contract. RGB preserves incoming alpha; RGBA Replace replaces it. Existing blend enable/source rules still control whether alpha is active. Vertex RGB is neutralized for this complete replacement; vertex alpha is retained or neutralized by the same format rule. Output emissive is zero because the post-lit replacement supersedes the previous lighting/emission result. Mixed pre/post stacks, post-lit Modulate/Blend/Add and alpha-dependent RGBA Decal are not approximated as unlit.

The type label alone does not fully specify the normal image encoding or authentic exporter tangent convention. The normalized unsigned RGB/original UV0/positive-handed basis is an **explicit bounded application contract**, not proven agreement with a vendor export. No support claim for gloss, scalar bump, world-space normals, environment/cube images, light maps or reset-exception rendering is introduced. These need further evidence and/or a richer material/evaluation path. Broader mapping, Batch 1 comeback and Batch 2 residuals remain open.

## Implementation review

`JtTextureTangents.h` builds orthonormal tangents after final normal generation/flat expansion. It validates triangle indices and handedness, charges allocations and polls cancellation. New arrays are committed only after all checks succeed. An unsupported chart removes only the displayed normal slot, preserves geometry/colour and emits a warning. Original cached geometry and source descriptors remain unchanged. Material identity includes the normal image/sampler and post-lit state. The normal decoder does not bake vertex/material colour into vector data.

The existing ADS shader ignored the material's unlit flag, although PBR honoured it. A shared shader correction makes ADS use the same base-colour/emissive, tone-map and gamma path for unlit materials as PBR. The floor retains its separate path. This is a shared renderer change and should be committed separately from the JT role adapter after owner acceptance. The previous accepted lit mapping pair is included as a regression control.

## Original control gate

Regenerate the original controls without running the importer:

```text
python -B tools/jt/make_texture_role_controls.py out/jt-research/texture-roles-ui-fixtures
```

The authoring manifest is `out/jt-research/texture-roles-ui-fixtures/authoring.json`. Sixty-six original roots cover 10.0, advanced 9.0 and advanced 9.5 in both byte orders. Eleven independent glTF pairs plus an accepted regression pair give 24 documents in `out/jt-research/c2af-visual-files.json`. Shared images, geometry, named references and roots remain under ignored `out/jt-research/texture-common-ui-fixtures/`.

| Case stem | What to compare in matching Y-Up ADS and PBR views |
| --- | --- |
| `normal_neutral` | Flat normal-map control: ordinary cube lighting, no texture colour tint or transparency |
| `normal_rgb`, `normal_bgr` | Same alternating highlight/dark patches in the same locations; no raised/dented reversal |
| `normal_rgba_unused_alpha`, `normal_bgra_unused_alpha` | Same normal pattern despite zero/partial image alpha; cube stays opaque |
| `normal_external` | Same directional pattern as the inline normal cases |
| `normal_with_base` | Colour checker and normal-induced shading agree together |
| `post_rgb_replace`, `post_rgba_replace` | Image colours agree without face-dependent lighting; RGBA transparency agrees |
| `post_rgb_decal_vertex_alpha` | Vertex RGB does not tint the image; retained vertex alpha makes the cube translucent |
| `post_external_replace` | Same unlit colour/alpha as inline post-lit RGBA |

Each source is `texture-roles_<case>_10_0_le.jt`; its reference is `reference_roles_<case>.gltf`. The reference uses original cube normals and literal image pixels, with the standard glTF normal/unlit slots. It does not derive expected pixels or material roles from importer output. Tangents are generated by the application's existing glTF import path from the reference normals/UVs; this limits the oracle to application parity, not an independent vendor tangent implementation.

After the owner reports build/install success, verify installed/build executable hashes before one grouped launch. The owner compares the pairs; no GUI automation is needed. After visual acceptance, export these **JT sources** to the existing ignored export folder and reopen beside the references:

| Source stem | GLB | glTF |
| --- | --- | --- |
| `normal_with_base` | `normal_with_base.glb` | `normal_with_base.gltf` |
| `post_rgba_replace` | `post_rgba_replace.glb` | `post_rgba_replace.gltf` |

The exact paths are in `out/jt-research/c2af-export-checklist.json`. Read-only export inspections:

```text
python -B tools/jt/inspect_texture_role_exports.py out/jt-research/c2af-export-checklist.json out/jt-research/c2af-export-inspection.json
python -B tools/jt/inspect_texture_mapping_exports.py out/jt-research/c2af-export-checklist.json out/jt-research/c2af-export-uv-inspection.json
```

The role inspector checks normal/base image bytes, samplers, factors, sidedness, alpha and unlit metadata. The UV inspector compares corresponding original vertices within their 16-bit quantization tolerance. Neither establishes rendered normal-basis parity; the reopen comparison is required. Missing exports are pending evidence, not a pass.

## Prepared checks and current status

`jt_texture_role_tests.cpp` prepares direct literal tangent-basis checks, transactional rejection of mirrored/degenerate/cancelled builds, importer role slots, missing UV/mapping/format fallback, automatic normal plus explicit colour, scene copy, normal GLB basis retention and post-lit GLB unlit round trips across enabled 10.x minors and advanced 9.0/9.5 records. These native checks are registered but **not run**. Read-only Python authoring and static review are not native or visual validation. Owner build, grouped ADS/PBR comparison and four application export/reopen checks remain required.

Read-only authoring hashes, Python AST syntax, resource existence and reference role inventory were checked for all 66 roots and eleven pairs; `out/jt-research/c2af-static-inventory.json` records the counts. Its inputs and authoring hashes are regenerated by the command above. This inventory is not importer execution.

## Evidence and provenance

Consulted original project material/import/export/shader code, retained texture descriptor reader, JT10.0 Rev-B texture type/channel definitions (printed pages 60-61) and the retained OpenGL 2.1 image/texture-function definitions for complete replacement. Retained PDF SHA-256: JT10 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; GL2.1 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`. Primary URLs and retained-source provenance are in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance).

Also consulted the already locally built Assimp 6.0.4 [glTF2 importer](https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/glTF2/glTF2Importer.cpp) and [exporter](https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/glTF2/glTF2Exporter.cpp) for UV/tangent transport. Local file SHA-256 values are respectively `6d64e77c31c22f8a992946e22401a18c36f10d7a12952caca5c25d5402055b74` and `659a547e9ad965eccdedf2cd9db9d062736346b60478514ba1984545cd53d724`. Its notices remain with the existing dependency; no source was copied. No new download, asset, third-party implementation, vendor SDK/tool, schema, disassembly or licence interpretation. All new controls/helpers are original project work. Authentic normal-role exporter evidence is missing; its encoding/basis qualification remains open. This records method and limits, not legal clearance.

## Owner build and visual acceptance - 24 October 2026

The owner reported build/install success. Build and installed executable SHA-256 values matched (`89a2df5cd9d36b5873ca6e2e15d30ccd10f3b5b9484b4cec49592ce31cb386da`) before the grouped 24-document launch and requested relaunch. Following the requested matching Y-Up ADS/PBR comparisons, the owner reported "Looks good". The grouped visual gate is accepted. The two representative JT sources still require four application GLB/glTF export/reopen comparisons and read-only export inspection. No native-suite or individual backend acceptance is inferred.

## Application export failure and prepared correction - 24 October 2026

The owner exported both requested sources and reported that post-lit Replace GLB and glTF reopen much dimmer than the JT. The application export gate is **not accepted**. Read-only inspection with the commands above reports 2/4 role/material comparisons passing: the normal-plus-base exports match, while both post-lit exports lack `KHR_materials_unlit`. Their base image bytes, alpha, sampler, colour factors and sidedness match the independent reference. All four UV comparisons pass within the original quantization allowance. Roughness differences are float representation only and within inspector tolerance. These observations identify missing unlit transport rather than changed texture colour.

Both application material builders recreated the runtime material without writing its unlit state: `SceneGraphExporter::buildMaterialFromSceneMesh` and `AssImpMeshExporter::createMaterial`. The already local Assimp 6.0.4 glTF2 writer selects the unlit extension from `AI_MATKEY_SHADING_MODEL`; the import-only glTF flag is insufficient on its own. The original shared helper `UnlitMaterialExport.h` now supplies both the unlit shading model and explicit import flag when the runtime material is unlit. It leaves lit export materials unchanged. Both builders use it, covering hierarchy-preserving, direct and replacement/variant material paths. No vendored source was changed.

`unlit_material_export_tests` is prepared in the isolated render checks. It serializes and reimports both lit and unlit original triangles through GLB and glTF, checking extension presence/absence, usage declaration, colour factors, alpha mode and the reimported unlit flag. It exercises the same helper used by both production builders, without a viewer or GL context; it is not full application-export integration coverage. No build or native check was run by the agent. The C2af role inspector now reports a missing unlit flag as an explicit material mismatch rather than an empty selection error.

Owner action: rebuild/install, then overwrite only `post_rgba_replace.glb` and `post_rgba_replace.gltf` by exporting the original post-lit JT again. Reopen in matching Y-Up ADS/PBR views beside the JT/reference and report the result. The previous normal-map exports remain available for inspection. Rerun the two read-only export inspectors after replacement; prior successful source/reference visual comparison does not accept the failed round trip.

## Final owner acceptance - 24 October 2026

The owner rebuilt/installed the shared unlit export correction, exported the post-lit JT again, and confirmed "Done, all well. Proceed" after the requested reopen checks. Read-only inspection reports 4/4 normal/unlit image, sampler and material comparisons passing, and 4/4 effective-UV comparisons within original quantization tolerance. Both post-lit outputs now carry `KHR_materials_unlit`. The bounded C2af source/reference and application export gate is accepted. Native-suite execution, exhaustive variant/backend coverage and authentic normal encoding/basis evidence remain unreported or open. The shared correction is recorded separately in [unlit material validation](JT_UNLIT_MATERIAL_VALIDATION.md).
