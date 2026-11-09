# Constant luminance and alpha borders - Gate C2t

22 October 2026, `feature/JT-importer`, after accepted/pushed C2s `53db11e`. **Owner accepted C2t on 22 October 2026 after reporting tests passed.** Original control authoring and read-only reference inspection ran under ignored `out/`. The owner built the app; the agent launched the 20 positive comparison/reference/regression documents together in one installed instance. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Bounded interpretation

Constant-border mapping now also accepts luminance, luminance-alpha and alpha-only inline images, using the image's internal components. Luminance takes Border R and replicates it to display RGB. Plain luminance omits alpha, so Border A is unused and the display border stays opaque. Luminance-alpha also retains Border A. Alpha-only uses Border A with neutral white display RGB, preserving incoming material colour under MODULATE. RGB/RGBA/BGR/BGRA and explicit-ring behaviour from accepted gates remain unchanged.

Finite colour values retain the accepted clamp/nearest-byte conversion into RGBA8. The blend policy is applied after border preparation: disabled blending forces alpha opaque; enabled blending selects BLEND only when the resulting image contains non-opaque alpha. Source descriptors, Border Colour and image bytes remain unchanged. No new allocation path, geometry change, renderer/data-model extension, enabled revision, dependency, vendored decoder or About change.

Sampling remains base-level nearest/linear clamp-to-border, with the other axis optionally repeat/mirror/clamp-to-edge. Authored mips and mipmapped border filtering, authored-border legacy clamp and external explicit rings remain pending. Constant-border interpretation for red/green/blue-only storage remains unqualified; those transport labels alone do not establish a matching legacy internal format. JT8 authored texture layout and authentic textured-export qualification are also unavailable. C2 is not complete.

## Prepared checks

`jt_texture_border_format_tests.cpp` now includes known constant-border/interior components for luminance, luminance-alpha and alpha-only, with enabled and disabled blending, both byte orders, enabled modern minors and 9.0/9.5 texture local versions 1/2/3. Assertions inspect exact display pixels and effective alpha mode. Border R/G/B differ deliberately, so accidental use of G/B or coloured alpha-only borders is observable. Existing RGB and explicit-ring cases remain active. C2q's unsupported constant-format check now uses red-only storage. Native checks are prepared, not run by the agent; use the existing [owner-run native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

## Original cube controls

Exact regeneration:

```powershell
python -B tools/jt/make_texture_border_component_controls.py out/jt-research/texture-border-components-ui-fixtures
```

This writer authors original blue outer and red inner cubes, rather than reusing a downloaded model. Both geometry companions, the glTF geometry buffer and expected display images use the [common asset store](JT_SHARED_TEXTURE_CONTROLS.md); no per-case geometry/reference copies. Each cube has 24 face vertices and 12 triangles. The inner cube has 55 percent of the outer cube's dimensions and is centred inside it. Each outer face has UVs covering both image interior and border regions. Expected glTFs use independently calculated padded UVs and literal RGBA border/interior pixels; the red inner cube has no texture and remains opaque.

Five cases distinguish plain luminance opacity, quarter-alpha luminance-alpha, quarter-alpha alpha-only, zero-alpha border and fully opaque alpha-only. The source material permits blending in all cases. The opaque image interior remains a solid blue patch; transparency applies to border regions, not the entire cube. The clear-border case is therefore expected to retain some opaque patches. The plain-luminance case deliberately stores zero Border A, which must have no effect.

Authoring produced 40 positive original roots (modern 10.0 and representative accepted basic/advanced 9.x envelopes in both byte orders) and five unique glTF references. Original geometry uses the existing bounded 8.2 serializer; these mixed-family synthetic records do not establish authentic exporter conformance. The source serializers gained optional blend/diffuse and untextured sibling controls; defaults preserve earlier outputs. No reheadered downloaded file is used. Gate `authoring.json` records file hashes/reference mappings under ignored `out/`.

## Owner visual gate

After rebuilding, open the positive set from `out/jt-research/texture-common-ui-fixtures/`. Physical glTF names resolve through `out/jt-research/texture-border-components-ui-fixtures/authoring.json`. Match camera/up-axis settings and rotate if needed to inspect interior visibility. Judge colour/visibility rather than exact lighting.

1. For each case below, compare `texture-border-components_<case>_10_0_le.jt` and `_be.jt` with its reference key of the same case name:
   - `luminance_opaque`: blue outer cube stays opaque; red inner cube is hidden, despite zero Border A.
   - `luminance_alpha_partial`: dark blue border regions are partially transparent and reveal the red inner cube; solid interior patches remain.
   - `alpha_partial`: blue border regions are partially transparent and reveal the red inner cube without changing the material's blue colour.
   - `alpha_zero`: border regions are fully clear; the red inner cube is visible through them, alongside opaque blue interior patches.
   - `alpha_opaque`: outer cube is opaque blue and hides the red inner cube.
2. Check representative 9.x roots against the corresponding references: `texture-border-components_luminance_alpha_partial_9_0_v1_be.jt`, `texture-border-components_alpha_zero_9_5_v2_le.jt`, and `texture-border-components_alpha_partial_9_5_v3_be.jt`.
3. Regressions: `texture-border-formats_bgr_constant_inline_be.jt` and `texture-explicit-border_10_0_both_inline_le.jt` retain their accepted appearance.

The set contains 20 positive documents including five references and two regressions. Stop for owner results before dependent implementation or commit.

## References and provenance

Consulted retained JT10.0 Rev-B image/border definitions, the accepted JT9.5 Rev-A hierarchy and retained public OpenGL2.1 section 3.8.8/table 3.15 on constant-border internal components, together with existing MODULATE conversion. Reference URLs and exact retained SHA-256 hashes remain in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). These published component definitions support luminance/luminance-alpha/alpha handling; the adapter's white RGB for alpha-only remains the neutral MODULATE representation.

Existing original adapter/writers were reused and cube coordinates/payloads/references were authored originally for this gate. No third-party asset, new download, third-party implementation, vendor software, SDK/schema, disassembly or licence interpretation was used. No generated asset or vendor document is tracked. This records method and qualification limits, not legal clearance.
