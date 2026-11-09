# Float32 inline images - Gate C2l


Current control storage, 22 October 2026: [shared texture resources](JT_SHARED_TEXTURE_CONTROLS.md) replaces per-gate companion copies. Regeneration commands are unchanged; file lists below use logical case names. Current JT filenames use the documented gate prefix in the common directory; glTF locations come from each gate manifest. Earlier acceptance records do not imply viewer requalification of every relocated file.

22 October 2026, `feature/JT-importer`, following owner-accepted/pushed C2k `e4c5840`. **Owner accepted C2l on 22 October 2026 after reporting the tests passed.** Controls were authored; no C++ build/test or viewer run by the agent. The owner standing authorization permits commit/push/proceeding after that report; optional-suite execution and authentic textured-export conformance are not inferred.

## Scope

The original application adapter now accepts pixel data type 2 (IEEE single-precision float) in the existing borderless 2D colour image profile. The descriptor already retained the format, raw bytes and owning file byte order. Explicit byte assembly and `memcpy` recover float values without aligned-pointer casts or host-endian assumptions. Existing image/padding limits, cancellation, UV mapping and resource policy remain. No third-party decoder or shader changes are needed.

Finite components are clamped to [0,1] and rounded to the nearest byte in the established canonical RGBA8/PNG image path. This is a display conversion: negative/high values do not preserve HDR range and float precision is reduced in the embedded display image. Original descriptor type/payload remain unchanged. NaN and infinity in any component reject the image mapping with a warning and preserve geometry, including invalid alpha when blending is disabled. Existing luminance/individual-channel/alpha-only rules and blend-state policy are reused.

The same conversion runs behind accepted 9.x basic/advanced and modern 10.x image envelopes. Unsupported numeric formats, JT8 texture layouts, additional texture types/combiners, borders, authored mip selection, automatic/multiple units and view/projective mapping remain open. No version, dependency, About or full-C2-completion claim is added. Authentic textured vendor-export qualification remains unavailable.

## Prepared checks and authored controls

`jt_texture_pixel_tests.cpp` adds float32 to existing known-pixel tests for all enabled modern minors, supported colour formats, both byte orders, alpha policies and row alignments. RGB/luminance-alpha/BGRA checks cover enabled 9.0/9.5 and local texture versions 1/2/3. Independent IEEE bit patterns cover finite clamp limits, signed zero, subnormal, exact fractions and adjacent values around 0.5. NaN (quiet/signalling) and positive/negative infinity in colour or alpha must retain geometry with diagnostics. Short/long rows still fall back. Earlier unsigned 8/16-bit checks remain; remaining numeric types must stay unsupported. These native tests are prepared, not executed by the agent. Owner can use the existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

The existing original authoring tools accept mutually exclusive `--float32`/`--unsigned16` switches; default behavior is unchanged. Float controls encode normalized source components, with independent 8-bit expected images. Exact regeneration commands:

```powershell
python -B tools/jt/make_texture_pixel_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-float-ui-fixtures --float32
python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures out/jt-research/texture9-float-ui-fixtures --advanced --float32
```

Authoring completed: 20 modern roots/ten references and 48 advanced 9.x roots/twelve references, with ignored companions and `authoring.json` files. Odd-width modern rows use alignment 8. External images in the 9.x control folders are unchanged PNG regressions; only inline payloads use float components. Roots are directly serialized original family records, not reheadered files. Accepted original/source-derived 8.2 geometry companions make these mixed-family synthetic assemblies, not evidence of authentic same-version vendor co-exports. All generated assets and source licence copies remain under ignored `out/`.

## Owner visual gate

Rebuild normally and keep companions beside each root. Match camera/up-axis settings when comparing JT and glTF; compare pattern, orientation and coverage rather than lighting brightness.

1. In `out/jt-research/texture-float-ui-fixtures/`, compare `rgb_le.jt`, `rgb_be.jt`, `bgra_le.jt`, `bgra_be.jt` with the corresponding `rgb_reference.gltf`/`bgra_reference.gltf`. Both byte orders should look identical.
2. Compare `luminance_alpha_le.jt`, `red_be.jt`, `alpha_le.jt` with their matching references. Expect greyscale, red-only, uniformly white respectively. Visual controls are opaque; this is not a new transparency gate.
3. Under `out/jt-research/texture9-float-ui-fixtures/9_5/v3/`, compare `stored/texture_inline_be.jt` and `planar/texture_inline_le.jt` with each folder's `reference.gltf`.
4. Under that root's `9_0/v2/mirror/`, compare `texture_inline_be.jt` with `reference.gltf`.
5. Each control retains one mesh, four vertices and two triangles. Reopen one accepted C2k unsigned-16 control and one 8-bit modern image control as regressions.

The owner accepted this gate and authorized commit/push/proceeding. C3 production validation remains independently pending.

## References and provenance

Consulted the already retained public Siemens JT10.0 Rev-B image-type/byte-order descriptions and the Khronos OpenGL2.1 image reference, alongside existing original ModelViewer numeric texture code. Source URLs/hashes and the 9.x image-layout provenance are retained in [C2k validation](JT_TEXTURE_UINT16_VALIDATION.md#references-and-provenance): JT10.0 PDF SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`, JT9.5 PDF `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`, [Khronos glTexImage2D](https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/glTexImage2D.xml) retained XML `b862c336c5d3e5d15caed4029fb70520634e3f400a35a7691c675b09af4f58d8`. Reference availability was not newly verified; no speculative URL is added. Nearest-byte conversion and nonfinite rejection are ModelViewer adaptation policies, not a claim of mandated JT output precision.

GTEX01 URL, checkout, hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new asset, third-party decoder, vendor software, SDK/schema, disassembly or licence interpretation was used. Inherited upstream sources/hashes/notices and decoder file counts remain unchanged; no vendored file changes in this gate. No vendor document is redistributed. This records method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
