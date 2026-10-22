# Sampled constant texture borders - Gate C2q

22 October 2026, `feature/JT-importer`, after accepted/pushed C2p `6f3f81c`. **Owner accepted C2q on 22 October 2026 after reporting tests passed.** The agent authored original controls and inspected their resource paths. The owner built the app; the agent launched the 12 positive comparison/reference/regression documents in one installed instance at the owner request. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Scope and representation

The original scene adapter supports a constant-colour border on either or both axes using clamp-to-border, with nearest/linear non-mipmap filters. The other axis may repeat, mirror or clamp to edge. Inline sources must be RGBA/BGRA; external PNG/JPEG decoding must produce an alpha-bearing image. Existing numeric component conversions remain in use. Unsupported profiles retain geometry with a warning.

The adapter pads the display image with one constant texel on each sampled border side and maps interior-relative UVs into that padded image. For width W and one padded axis, S becomes `(W*S+1)/(W+2)`; T follows the same rule. Clamp-to-edge on that axis then reproduces base-level nearest/linear constant-border sampling, including transition footprints. The operation follows the authored affine/model-linear mapping. Unpadded axes keep their wrap mode and UVs. Image dimensions, memory limits, cancellation and actual resulting alpha remain checked. Blending-disabled appearances force alpha opaque as before. Geometry positions, normals and topology are unchanged.

Effective inherited border colour and padded axes participate in image cache identity. Material identity and appearance-specific UV copies preserve distinct mappings even for a shared source image/geometry. Raw descriptors, border colours and source bytes remain unchanged; the existing display image is RGBA8. Finite border colour components are clamped and rounded to display bytes. No shader, dependency, vendored decoder, About text or enabled JT revision changes.

Sampled explicit border rings, legacy clamp behaviour, mipmapped constant-border filtering and authored mip preservation remain open. Generated padded mip chains would alter the source interior's sampling footprint and are deliberately not accepted here. JT8 authored texture layout and authentic textured-export qualification also remain unavailable. This is a bounded extension, not completion of C2 or all border modes.

## Prepared checks and authoring

`jt_texture_constant_border_tests.cpp` joins the existing native suite. Original 2x2 known pixels and hand-calculated mappings check both padded axes separately/together, both byte orders, nearest/linear minification, opaque/blended appearances, exact pixels, unchanged geometry, external RGBA and inherited colours sharing one child image attribute. Family coverage includes enabled modern minors and 9.0/9.5 local texture versions 1/2/3. Negative profiles include mipmap filtering, explicit rings, missing border environment, legacy clamp and RGB storage. See existing [owner-run native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls). Native checks are prepared, not executed by the agent.

Exact visual-control regeneration:

```powershell
python -B tools/jt/make_texture_constant_border_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-constant-border-ui-fixtures
```

Authoring produced 48 positive JT roots and three independently authored glTF references using the [shared store](JT_SHARED_TEXTURE_CONTROLS.md). Existing GTEX01 geometry/image inputs and C2f tiled UVs are reused. Expected glTFs contain independently padded images and explicitly calculated UVs, using ordinary clamp-to-edge/repeat/mirror samplers. Roots are original serialized modern/9.x envelopes referencing accepted 8.2 geometry; mixed-family synthetic controls do not prove authentic exporter compatibility. No reheadered downloaded files or vendor-export claims. Gate `authoring.json` records filenames/hashes and reference mappings under ignored `out/`.

## Owner visual gate

After rebuilding, compare these files in `out/jt-research/texture-common-ui-fixtures/`. Use matching up-axis/camera settings; compare pattern, border coverage and orientation rather than lighting. Physical glTF names are mapped by reference key in `out/jt-research/texture-constant-border-ui-fixtures/authoring.json`.

1. `texture-constant-border_10_0_both_inline_le.jt` and `texture-constant-border_10_0_both_external_be.jt` against reference `both`: orange outside the tiled source image, with the same pattern and transition placement.
2. `texture-constant-border_10_0_s_repeat_inline_be.jt` against `s_repeat`: orange only beyond S; T continues repeating.
3. `texture-constant-border_10_0_t_mirror_external_le.jt` against `t_mirror`: cyan beyond T; S continues mirrored repeat.
4. `texture-constant-border_9_0_v1_both_be.jt`, `texture-constant-border_9_5_v2_s_repeat_le.jt`, and `texture-constant-border_9_5_v3_t_mirror_be.jt` against the corresponding references above.
5. Regression: C2p `texture-border_10_0_repeat_ring_le.jt` and C2f `tile_2x2_inline_le.jt` should retain their previous appearance.

This gives 12 documents including references and regressions, all positive cases. Native alpha checks use tiny independent fixtures; these visual controls deliberately use opaque borders so border placement is easy to assess. Report any image loss, unexpected border, pattern stretch, orientation mismatch or regression before proceeding to dependent work.

## References and provenance

Consulted retained Siemens JT10.0 Rev-B border colour/image/wrap definitions, retained JT9.5 Rev-A texture hierarchy and retained public OpenGL2.1 sections 3.8.1/3.8.7/3.8.8. Primary JT10 URL: [Siemens reference](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. Retained JT9.5 hash: `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`; no newly verified publisher URL. [Khronos GL2.1 reference](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf), retained via [official repository mirror](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/gl/glspec21.pdf), SHA-256 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`.

Existing original ModelViewer adapter/writers and GTEX01 controls were reused. GTEX01 source checkout/CC0 attribution remains in [source provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new asset, third-party implementation, vendor software, SDK/schema, disassembly or licence interpretation was introduced. No vendor document or generated asset is tracked. This records method and qualification limits, not legal clearance.
