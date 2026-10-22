# Unsigned 16-bit inline images — Gate C2k

22 October 2026, `feature/JT-importer`, following owner-accepted C2j `9c8d37a`. **Owner accepted C2k on 22 October 2026 after reporting the tests passed.** Control authoring ran; the agent did not build/run C++ tests or the viewer. The owner authorized commit/push/proceeding after each reported successful test gate; this pass records that acceptance without inferring optional-suite execution or authentic textured-export conformance.

## Scope and limits

The existing supported borderless 2D colour-image path now accepts pixel data type 5 (unsigned 16-bit components), alongside type 3 (unsigned 8-bit). Source texels remain raw in the descriptor; the original reader additionally retains the owning file's byte order. The adapter assembles words explicitly from bytes, avoiding host-order and unaligned-pointer dependencies. Row alignment applies to complete byte widths, including two bytes per component. Existing exact base-image size, raw/decoded image limits and cancellation checks remain.

Components use normalized unsigned colour semantics and round to the nearest byte for the existing canonical RGBA8/PNG material path: `round(value * 255 / 65535)`, implemented exactly for integer inputs as `(value + 128) / 257`. This is an intentional display conversion; full 16-bit texture precision is not preserved in the renderer or derived embedded PNG. Original descriptor bytes/type remain unchanged. Supported colour/luminance/alpha component expansion, source row orientation, image alpha policy, UV mapping and resource deduplication are reused. No shader or global rendering change.

The conversion applies behind accepted basic/advanced 9.x and modern 10.x texture envelopes. Original controls qualify those paths; no authentic textured vendor export is available. JT8 texture layouts, signed/32-bit/float/half/bit pixel types, depth/stencil formats, borders, authored mip selection, automatic/multiple texture units, additional combiners and view/projective mapping remain open. Unsupported image mappings retain geometry and diagnostics. No dependency, file-version or About claim changes; C2 remains incomplete.

## Prepared checks

`jt_texture_pixel_tests.cpp` extends existing image checks across all seven enabled modern minors, both byte orders, all ten supported colour formats, blending policies and row alignment. Values with unequal high/low bytes distinguish byte order. Hand-calculated endpoint and rounding-boundary samples exercise values outside simple byte replication. Additional RGB/luminance-alpha/BGRA cases cover enabled 9.0/9.5, all three local texture versions and both byte orders. Short/long row data must fall back; other numeric types must remain diagnosed. Existing 8-bit checks remain active. These C++ checks are prepared and unexecuted by the agent; owner may use the existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

Original authoring reuses GTEX01 and generates unequal-byte unsigned words that round back to independently authored 8-bit reference images. Modern controls use odd-width rows and alignment 8, exercising padding. The old default authoring commands retain their 8-bit behavior. Exact new regeneration commands:

```powershell
python -B tools/jt/make_texture_pixel_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-u16-ui-fixtures --unsigned16
python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures out/jt-research/texture9-u16-ui-fixtures --advanced --unsigned16
```

Authoring completed: 20 modern roots/ten comparisons and 48 advanced 9.x roots/twelve comparisons, with companion geometry, images and ignored `authoring.json` files. Modern roots are original 10.0 records; 9.x roots are directly serialized original family records. Both reuse accepted original/source-derived 8.2 geometry companions. These synthetic mixed-family assemblies establish no authentic same-version co-export conformance. External-image controls in the 9.x folders remain 8-bit external PNG regressions; the new numeric decoding applies to inline payloads only. Generated files and copied source licence stay ignored under `out/`.

## Owner visual gate

Rebuild normally. Keep each folder's companions together and use matching camera/up-axis settings. Compare orientation, pattern and coverage; lighting brightness can differ.

1. In `out/jt-research/texture-u16-ui-fixtures/`, compare `rgb_le.jt`, `rgb_be.jt`, `bgra_le.jt` and `bgra_be.jt` with their corresponding `rgb_reference.gltf`/`bgra_reference.gltf`. Endian variants must look identical.
2. Compare `luminance_alpha_le.jt`, `red_be.jt` and `alpha_le.jt` with their matching references. Expect greyscale, red-only, and uniformly white respectively. The visual controls are opaque and do not establish a new transparency result.
3. Under `out/jt-research/texture9-u16-ui-fixtures/9_5/v3/`, check `stored/texture_inline_be.jt` and `planar/texture_inline_le.jt` against each folder's `reference.gltf`.
4. Under that same root's `9_0/v2/mirror/`, check `texture_inline_be.jt` against `reference.gltf`.
5. Every control retains one mesh, four vertices and two triangles. Reopen an accepted 8-bit modern image control and a C2j 9.x image control as regressions.

The owner accepted this gate and authorized commit/push/proceeding. C3 production checks remain independently pending.

## References and provenance

Read-only research consulted the retained Siemens **JT10.0 Rev-B** file byte-order definition and texture image format/type/row-alignment descriptions, plus the already retained **JT9.5 Rev-A** image descriptions and the public Khronos OpenGL2.1 image reference. No new implementation source was consulted:

- [Siemens JT10.0 Rev-B publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; retained text was used, publisher availability not newly verified.
- JT9.5 Rev-A PDF SHA-256 `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`; availability/terms remain in [9.5 qualification](JT_95_VALIDATION.md).
- [Khronos glTexImage2D reference](https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/glTexImage2D.xml), retained XML SHA-256 `b862c336c5d3e5d15caed4029fb70520634e3f400a35a7691c675b09af4f58d8`. The nearest-byte output policy is ModelViewer's bounded RGBA8 adaptation, not a claim of a mandated JT output precision.

GTEX01 repository URL, checkout, source hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). Existing original ModelViewer reader/adapter/tests/tooling supply integration conventions. No new asset, vendor software, SDK/schema, disassembly or third-party decoder was used. Two existing ModelViewer-original decoder files change only to retain byte order; inherited upstream sources/hashes/notices and 104 upstream/17 original file counts remain unchanged. The manifest and modification note record the continuation. No vendor document is redistributed. These are method statements, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
