# Colour formats for sampled borders - Gate C2s

22 October 2026, `feature/JT-importer`, after accepted/pushed C2r `e3fdd97`. **Owner accepted C2s on 22 October 2026 after reporting tests passed.** Original controls were authored under ignored `out/`. The owner built the app; the agent launched the 15 positive comparison/reference/regression documents together in one installed instance. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Bounded extension

The original adapter now permits sampled explicit rings in every colour layout already supported by the base image converter: RGB, RGBA, BGR, BGRA, luminance, luminance-alpha, red, green, blue and alpha. Existing canonical conversion is applied to the complete image before retaining/cropping the sampled sides. Alpha-only MODULATE continues to use neutral white RGB. Supported numeric conversions, source-endian handling and source metadata are unchanged.

Constant borders additionally accept inline RGB/BGR and opaque external decoded images. RGB has no alpha component: a zero Border A does not make its border transparent. RGBA/BGRA and alpha-bearing external images retain their accepted alpha behaviour. The existing image alpha policy is applied after border preparation. Single-channel/luminance constant-border interpretation remains outside this gate; it is not inferred from the explicit-ring path.

Sampling remains limited to base-level nearest/linear clamp-to-border, optionally combined with repeat/mirror/clamp-to-edge on the other axis. Authored mip preservation/filtering, explicit external rings and authored-border legacy clamp remain pending. JT8 authored texture layout and authentic textured-export qualification remain unavailable. No new JT revision, renderer/data-model extension, dependency, vendored decoder or About change. Geometry and existing padded-image/UV representation are unchanged.

## Prepared native checks

`jt_texture_border_format_tests.cpp` joins the existing suite. Independently listed component bytes and expected colours distinguish channel order, luminance replication, single-channel placement, neutral alpha-only RGB and alpha-bearing formats. Tiny original explicit-ring cases cover all ten layouts, byte/unsigned-16 components, both source byte orders, enabled modern minors and 9.0/9.5 local texture versions 1/2/3. Existing numeric and mixed-axis suites remain active. Separate RGB/BGR constant-border cases set Border A to zero and assert opaque border/interior pixels and absence of BLEND despite an enabled source blend policy. An external RGB PNG checks the same missing-alpha interpretation.

C2q's unsupported constant-format case now uses luminance because RGB is accepted here. Existing missing/inconsistent descriptors, nonfinite source, bounds and unsupported sampling cases retain their diagnostics. Native checks are prepared, not run by the agent; use the existing [owner-run commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

## Original controls

Exact regeneration:

```powershell
python -B tools/jt/make_texture_border_format_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-border-formats-ui-fixtures
```

Authoring produced 88 positive original JT roots and eleven reference keys resolving to seven unique glTF documents for the local input set. Explicit roots span all ten colour layouts, both byte orders, modern 10.0 and representative accepted 9.x local-version envelopes. Eight modern constant RGB/BGR roots cover inline/external images and both byte orders. They reuse GTEX01 geometry/image inputs, C2f tiled UVs, distinct original edge/corner colours and the [shared store](JT_SHARED_TEXTURE_CONTROLS.md). Canonical reference images and padded UV buffers are independently authored; no importer execution produces expected images. Inline payloads necessarily remain inside each transport case. Gate `authoring.json` records hashes, reference mappings and the shared root under ignored `out/`.

These are mixed-family synthetic controls, not authentic same-version textured exports or conformance evidence. No private/downloaded model names or generated assets are tracked.

## Owner visual gate

After rebuilding, compare the following in `out/jt-research/texture-common-ui-fixtures/`, using matching up-axis/camera settings. Reference keys resolve through `out/jt-research/texture-border-formats-ui-fixtures/authoring.json`. Compare colours, border placement and pattern orientation, allowing normal lighting differences.

1. `texture-border-formats_rgb_10_0_le.jt` and `texture-border-formats_bgr_9_5_v3_be.jt` against reference `rgb`: both should retain the coloured pattern and distinct edge/corner colours.
2. `texture-border-formats_luminance_10_0_be.jt` and `texture-border-formats_luminance_alpha_9_5_v2_le.jt` against `luminance`: both should show the same grayscale image/borders.
3. `texture-border-formats_red_9_0_v1_be.jt` against `red`: only the red component remains, with matching border coverage.
4. `texture-border-formats_alpha_9_5_v3_le.jt` against `alpha`: these opaque-alpha visual inputs produce a neutral white surface. This is a mapping/regression check, not a transparency oracle; native cases carry the non-opaque alpha assertions.
5. `texture-border-formats_rgb_constant_external_le.jt` and `texture-border-formats_bgr_constant_inline_be.jt` against `constant_rgb`: both retain the full-colour interior with a blue-grey opaque border. Zero Border A must not introduce transparency for RGB.
6. Regressions: `texture-explicit-border_10_0_both_inline_le.jt` and `texture-constant-border_10_0_both_inline_le.jt` retain their accepted appearance.

This is 15 positive documents including five unique references and two regressions. Report colour swaps, missing textures, unintended transparency or pattern/border differences before dependent implementation proceeds.

## References and provenance

Consulted retained JT10.0 Rev-B image-format and border definitions, the accepted JT9.5 Rev-A hierarchy, and retained public OpenGL2.1 section 3.8.8/table 3.15 on border colour interpretation and internal colour components. Reference URLs and exact retained SHA-256 hashes are in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). The RGB constant-border extension follows the omission of alpha from RGB internal components; it does not generalize uncertain internal formats. Existing original adapter/writers and GTEX01 source inputs were reused; CC0/source attribution remains in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01).

No new download, asset, third-party implementation, vendor software, SDK/schema, disassembly or licence interpretation was used. No vendor document or generated fixture is tracked. This records method and qualification limits, not legal clearance.
