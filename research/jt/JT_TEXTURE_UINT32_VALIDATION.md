# Unsigned 32-bit inline images - Gate C2n


Current control storage, 22 October 2026: [shared texture resources](JT_SHARED_TEXTURE_CONTROLS.md) replaces per-gate companion copies. Regeneration commands are unchanged; file lists below use logical case names. Current JT filenames use the documented gate prefix in the common directory; glTF locations come from each gate manifest. Earlier acceptance records do not imply viewer requalification of every relocated file.

22 October 2026, `feature/JT-importer`, following accepted C2m `4183ff6` and multi-file startup `90d13b5`. **Owner accepted C2n and the shared-resource visual layout on 22 October 2026 after reporting tests passed.** Authoring ran under ignored `out/`. The owner built the app; the agent launched the positive checklist/reference/regression set in one installed instance at the owner request. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Scope

The original image adapter now accepts pixel data type 7 (unsigned 32-bit components) in the established borderless 2D colour profile. Existing owning-file byte order, explicit word assembly, image/stride limits and cancellation are reused. Component conversion uses widened integer arithmetic for `round(value * 255 / 4294967295)`, including the full unsigned endpoint without overflow. The output remains the existing canonical RGBA8/PNG image path: full source precision is reduced for display; original descriptor bytes/type remain unchanged. No third-party decoder, shader or dependency changes.

Existing luminance/colour/alpha expansion, blend policy, source row orientation, UV transforms/generation and resource deduplication remain. The adapter applies behind accepted basic/advanced 9.x and modern 10.x texture envelopes. Signed integer and bit-packed image types, JT8 texture layouts, borders, authored mip selection, wider texture types/combiners, automatic/multiple units and view/projective mapping remain open. No version/About/full-C2-completion claim is added. Authentic textured-export qualification remains unavailable.

## Prepared checks and original controls

`jt_texture_pixel_tests.cpp` extends known-pixel tests to unsigned 32-bit components for all enabled modern minors, supported colour formats, both byte orders, row alignments and alpha policies. Selected formats also cover enabled 9.0/9.5 and local texture versions 1/2/3. Unequal bytes expose endian mistakes. Hand-calculated endpoint, rounding threshold and interior bit patterns exercise the full unsigned range; short/long image rows must retain geometry with diagnostics. Prior numeric formats and unsupported-type fallback remain regression coverage. Native checks are prepared, not run by the agent; owner can use the existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

Existing original writers accept a mutually exclusive `--unsigned32` flag. They generate unsigned words with unequal bytes that map to independently authored 8-bit expected images. Prior flags/defaults retain their behavior. Exact regeneration:

```powershell
python -B tools/jt/make_texture_pixel_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-u32-ui-fixtures --unsigned32
python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures out/jt-research/texture9-u32-ui-fixtures --advanced --unsigned32
```

Authoring completed: 20 modern roots/ten comparisons and 48 advanced 9.x roots/twelve comparisons, with ignored companions and `authoring.json`. Odd-width modern rows use alignment 8. External PNG controls remain unchanged regressions; unsigned word conversion is inline-only. Original family roots are directly serialized, not reheadered, and reuse accepted original/source-derived 8.2 geometry companions. These mixed-family synthetic assemblies do not establish authentic same-version exporter agreement. All generated files, images and source licence copies remain ignored.

## Owner visual gate

Rebuild normally. Keep companions beside each root and match camera/up-axis settings when comparing JT/glTF. Compare pattern, orientation and coverage; lighting brightness can differ. The application now accepts multiple file arguments in one invocation, opening each in a separate document within one app instance.

1. In `out/jt-research/texture-u32-ui-fixtures/`, compare `rgb_le.jt`, `rgb_be.jt`, `bgra_le.jt`, `bgra_be.jt` with `rgb_reference.gltf`/`bgra_reference.gltf`. Both byte orders should look identical.
2. Compare `luminance_alpha_le.jt`, `red_be.jt`, `alpha_le.jt` with their matching references. Expect greyscale, red-only and uniformly white respectively. These opaque visual controls do not establish another transparency result.
3. Under `out/jt-research/texture9-u32-ui-fixtures/9_5/v3/`, compare `stored/texture_inline_be.jt` and `planar/texture_inline_le.jt` with each folder's `reference.gltf`.
4. Under that root's `9_0/v2/mirror/`, compare `texture_inline_be.jt` with `reference.gltf`.
5. Each control retains one mesh, four vertices and two triangles. Reopen an accepted C2m half control and a C2l float32 control as regressions.

Together with references and two regressions this is a 20-document visual set. Do not open native negative cases for the UI gate. The owner accepted this visual gate; optional-suite execution and authentic textured-export conformance are not inferred. C3 production checks remain independently pending.

## References and provenance

Consulted retained public Siemens JT10.0 Rev-B image-type/byte-order descriptions and existing original ModelViewer numeric image code. Earlier colour-normalization and 9.x layout reference provenance remains in [C2k validation](JT_TEXTURE_UINT16_VALIDATION.md#references-and-provenance). JT10.0 PDF SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; [recorded publisher URL](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), availability not newly verified. No new reference download or licence interpretation was made. Nearest-byte output is ModelViewer's display adaptation rather than a claim of mandated JT precision.

GTEX01 URL, checkout/hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new asset, third-party implementation, vendor software, SDK/schema or disassembly was used. Upstream sources/hashes/notices, decoder file counts and dependencies are unchanged. No vendor document is redistributed. This records method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
