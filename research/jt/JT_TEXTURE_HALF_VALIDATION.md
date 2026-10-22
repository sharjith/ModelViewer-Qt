# Float16 inline images - Gate C2m

22 October 2026, `feature/JT-importer`, following owner-accepted/pushed C2l `df79883`. **Owner accepted C2m on 22 October 2026 after completing the requested visual checks.** Authoring ran under ignored `out/`. The owner built the viewer; at the owner request, the agent launched the positive checklist/reference/regression documents together in one installed app instance. The agent did not build C++ or run the native suite. Standing authorization permits commit/push/proceeding after this acceptance.

## Scope

The original application adapter now accepts image pixel data type 9, the documented IEEE binary16 representation. It reuses the existing source byte order, exact byte-stride checks, image limits and cancellation. Sign, exponent and fraction are evaluated directly into an exact double value using powers of two, without a new library or platform half-float type. No third-party decoder, shader or dependency changes.

Finite half values follow the accepted float32 display policy: clamp to [0,1], round to the nearest byte and use the established RGBA8/PNG material path. Source half precision/HDR range is reduced in that display image; original descriptor bytes/type remain unchanged. Signed zero/subnormal/normal inputs are handled. NaN/infinity reject image mapping with a diagnostic while preserving geometry, including invalid colour or alpha with blending enabled or disabled. Existing alpha, component expansion, UV mapping, source row orientation and resource policy remain.

The conversion applies behind accepted basic/advanced 9.x and modern 10.x image envelopes. Signed/32-bit integer and bit-packed image types, JT8 texture layouts, borders, authored mip selection, wider texture types/combiners, automatic/multiple texture units and view/projective mapping remain open. No supported JT version or About claim changes. This gate does not complete C2 or establish authentic textured-export conformance.

## Prepared checks and control authoring

The existing `jt_texture_pixel_tests.cpp` now includes half inputs across supported colour formats, enabled modern minors, both byte orders, row alignments and alpha policies. Selected formats also cover enabled 9.0/9.5 and texture local versions 1/2/3. Independent half bit patterns exercise zero/subnormal, finite extrema, clamp limits, exact fractions and adjacent half values around 0.5. Quiet/signalling NaN and positive/negative infinity in colour/alpha must fall back. Bad row sizes and earlier numeric cases remain regression checks. Tests are prepared, not run by the agent; owner can use the existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

The authoring tools use Python's standard binary16 packer independently of the C++ decoder and retain prior default behavior. Numeric flags are mutually exclusive. Exact regeneration:

```powershell
python -B tools/jt/make_texture_pixel_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-half-ui-fixtures --float16
python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures out/jt-research/texture9-half-ui-fixtures --advanced --float16
```

Authoring completed: 20 modern roots/ten glTF comparisons and 48 advanced 9.x roots/twelve comparisons, with ignored companions and `authoring.json`. Odd-width modern rows use alignment 8. External PNG cases are unchanged regressions; numeric half conversion applies to inline images. Original family roots are serialized directly and use accepted original/source-derived 8.2 geometry companions. These mixed-family synthetic assemblies qualify the texture adapter, not authentic same-version exporter agreement. All generated files, copied source licence and images remain ignored.

## Owner visual gate

Rebuild normally and keep all companions beside each JT root. Match camera/up-axis settings for comparison; check pattern, orientation and coverage rather than lighting brightness.

1. In `out/jt-research/texture-half-ui-fixtures/`, compare `rgb_le.jt`, `rgb_be.jt`, `bgra_le.jt`, `bgra_be.jt` with the corresponding `rgb_reference.gltf`/`bgra_reference.gltf`. Both byte orders should look identical.
2. Compare `luminance_alpha_le.jt`, `red_be.jt`, `alpha_le.jt` with their matching references. Expect greyscale, red-only and uniformly white. Visual controls are opaque and do not establish another transparency result.
3. Under `out/jt-research/texture9-half-ui-fixtures/9_5/v3/`, compare `stored/texture_inline_be.jt` and `planar/texture_inline_le.jt` with each folder's `reference.gltf`.
4. Under that root's `9_0/v2/mirror/`, compare `texture_inline_be.jt` with `reference.gltf`.
5. Each control retains one mesh, four vertices and two triangles. Reopen one C2l float32 control and one C2k unsigned-16 control as regressions.

The owner confirmed all requested documents were checked and reported tests passed. This accepts the visual gate without inferring optional-suite execution or authentic textured-export conformance. The multi-file startup adjustment was exercised by that same single-instance launch and is committed separately. C3 production checks remain independently pending.

## References and provenance

Consulted retained public JT10.0 Rev-B image type definitions, which specify the binary16 sign/exponent/fraction widths, and existing original ModelViewer image conversion code. Prior byte-order/image/reference and 9.x layout provenance remains in [C2k validation](JT_TEXTURE_UINT16_VALIDATION.md#references-and-provenance) and [C2l validation](JT_TEXTURE_FLOAT_VALIDATION.md#references-and-provenance). JT10.0 PDF SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; [recorded publisher URL](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), availability not newly verified. No new reference download or licence interpretation was made. Clamping, nearest-byte display and nonfinite rejection are ModelViewer adaptation policies.

GTEX01 URL, checkout/hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new asset, third-party implementation, vendor tool, SDK/schema or disassembly was used. Upstream sources/hashes/notices, decoder file counts and dependencies are unchanged. No vendor document is redistributed. This records method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
