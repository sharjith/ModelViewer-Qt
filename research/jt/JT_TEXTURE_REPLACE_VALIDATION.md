# Inline RGB/RGBA texture Replace - Gate C2u

22 October 2026, `feature/JT-importer`, after accepted/pushed C2t `bf63388`. **Owner accepted C2u on 22 October 2026 after reporting tests passed.** Original control authoring and read-only reference inspection ran under ignored `out/`. The owner built the app; the agent launched the 17 positive comparison/reference/regression documents together in one installed instance. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Bounded combiner mapping

The original adapter accepts texture environment Replace for channel-0 pre-lit inline RGB/RGBA/BGR/BGRA images on geometry without active vertex colours. Existing qualified UV/generation/transform/filter/border rules remain in force. The source image is not baked with material tint. Instead, the derived display material uses white RGB factors so the existing modulating renderer/export path displays the texture's RGB directly.

RGB/BGR Replace retains the incoming material alpha, because those internal formats lack texture alpha. RGBA/BGRA Replace also replaces incoming alpha: the derived material opacity is neutral, and the texture supplies its own alpha. Existing source blend-enable/factor policy still determines whether transparency is active. Disabled blending forces opaque display as before. Source material/image metadata and source pixels remain unchanged. Other material properties and the existing viewer lighting approximation remain in use; this is not a fixed-function lighting implementation.

Replace on external images, luminance/single-channel layouts, or active vertex colours is not qualified here. Such cases retain geometry/material colours with a diagnostic. Vertex-colour arrays are preserved. Material override rules that already remove vertex colours continue to apply. Unsupported combiners remain diagnosed; no silent conversion to Modulate. Multiple texture units, other combiners, view/projective coordinates, higher mip preservation, other image/border gaps and authentic textured-export qualification remain open. No enabled revision, renderer/data-model extension, dependency, vendored decoder or About change. C2 is not complete.

## Prepared native checks

`jt_texture_replace_tests.cpp` joins the existing suite. Original RGB/RGBA/BGR/BGRA component bytes and blue material with half alpha distinguish incoming tint from texture colour and incoming alpha from texture alpha. Prepared cases cover both byte orders, enabled modern minors, accepted 9.0/9.5 local texture versions 1/2/3, enabled/disabled blending, exact image pixels, derived material RGB/opacity/alpha mode, preserved vertex colours on fallback, and unsupported combiners. A shared-geometry pair checks distinct Replace/Modulate material variants with one canonical image. The legacy fixture now optionally authors material alpha, preserving its default value. Native checks are prepared, not run by the agent; use the existing [owner-run native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

Renderer/export results use ordinary image/material/UV fields rather than a new JT-specific shader operation. Final cross-renderer/export qualification remains a later consolidation item; no new native execution or export result is claimed here.

## Original controls

Exact regeneration, using the original nested-cube inputs from C2t:

```powershell
python -B tools/jt/make_texture_border_component_controls.py out/jt-research/texture-border-components-ui-fixtures
python -B tools/jt/make_texture_replace_controls.py out/jt-research/texture-border-components-ui-fixtures out/jt-research/texture-replace-ui-fixtures
```

Authoring produced 32 positive original JT roots and four shared glTF references. The outer cube has a green material tint and half material alpha. Its tiny RGB/RGBA image contains red, green, blue and white texels; the RGBA image has one quarter-alpha texel. An opaque red inner cube makes transparency observable. References independently encode the expected material factors: Modulate retains tint/half alpha; Replace uses white RGB and retains half alpha only for RGB storage. Expected images keep original components. Lighting differences are not the oracle.

Original C2t geometry buffers and the inner geometry companion are reused in the [common store](JT_SHARED_TEXTURE_CONTROLS.md). Only the changed outer appearance/UV geometry and small UV/image assets require different content. No gate-specific resource copies or downloaded models are introduced. Roots are original modern/9.x envelopes with original 8.2 geometry companions, not authentic same-version textured exports. `authoring.json` records filenames/hashes/reference mappings under ignored `out/`. Serializer combiner parameters default to earlier Modulate behaviour.

## Owner visual gate

After rebuilding, compare these files in `out/jt-research/texture-common-ui-fixtures/`, with matching camera/up-axis settings. Reference keys resolve through `out/jt-research/texture-replace-ui-fixtures/authoring.json`.

1. For each case `rgb_modulate`, `rgb_replace`, `rgba_modulate`, and `rgba_replace`, compare `texture-replace_<case>_10_0_le.jt` and `_be.jt` with the corresponding reference key. Modulate should visibly tint/darken the pattern; Replace should preserve its full source colours.
2. RGB Replace keeps half material alpha, so the red inner cube remains visible through the textured shell. RGBA Replace removes that incoming half-alpha factor: opaque texture regions hide the inner cube, while the partial-alpha region reveals it. RGBA Modulate retains the additional half-alpha factor. Rotate as needed to inspect the coloured regions.
3. Compare `texture-replace_rgb_replace_9_0_v1_be.jt`, `texture-replace_rgba_replace_9_5_v2_le.jt`, and `texture-replace_rgba_modulate_9_5_v3_be.jt` against their references.
4. Regressions: `texture-border-components_alpha_partial_10_0_le.jt` and `texture-border-formats_rgb_constant_external_le.jt` retain their accepted appearance.

The set contains 17 positive documents including four references and two regressions. Stop for owner results before dependent implementation or commit.

## References and provenance

Consulted retained JT10.0 Rev-B texture Blend Type definitions, accepted JT9.5 Rev-A hierarchy and retained public OpenGL2.1 table 3.21 on Replace/Modulate internal RGB/A semantics. Reference URLs and exact retained SHA-256 hashes remain in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). No specification table is reproduced here.

Existing original adapter/writers and C2t original cube inputs were reused. Expected material factors and tiny image components were authored independently of importer execution. No third-party asset, new download, third-party implementation, vendor software, SDK/schema, disassembly or licence interpretation was used. No generated asset or vendor document is tracked. This records method and qualification limits, not legal clearance.
