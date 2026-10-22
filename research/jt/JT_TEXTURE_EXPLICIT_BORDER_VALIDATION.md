# Sampled explicit texture borders - Gate C2r

22 October 2026, `feature/JT-importer`, after accepted/pushed C2q `227605e`. **Owner accepted C2r on 22 October 2026 with "Looks good to me" and supplied screenshots.** Original authoring and read-only resource inspection ran under ignored `out/`. The owner built the app; the agent launched the 12 positive comparison/reference/regression documents together in one installed instance. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Supported boundary

This gate extends the accepted non-mipmapped clamp-to-border path to authored one-texel inline rings in RGBA/BGRA images. Either or both S/T axes may sample the ring. The other axis may repeat, mirror or clamp to edge. The original adapter retains the actual border texels on sampled axes and crops the ring on unsampled axes. Different edge and corner colours remain distinct; no constant-colour substitution is made.

For an interior width W, retaining the two border columns changes display S to `(W*S+1)/(W+2)`; an unretained axis keeps its original normalized coordinates. The same applies to T. The existing clamp-to-edge display sampler then reaches the nearest authored border texel outside the interior, including base-level linear transitions. Authored affine/model-linear transforms are applied before this mapping. Source bytes, descriptors and metadata remain unchanged; display conversion remains RGBA8. Positions, normals and topology are unchanged.

Image caching includes the effective sampled axes, so a shared image attribute inheriting different wraps produces the correct cropped image per appearance. Alpha is inspected after any crop: sampled transparent texels participate in the existing blend policy; discarded texels do not. Input decoding still validates the complete source, including nonfinite ring components. Bounds, extra crop allocations and cancellation remain in force. Unsupported/inconsistent profiles retain geometry with a warning.

Mipmap filtering/preservation, external explicit rings, sampled non-RGBA formats and authored-border legacy clamp remain unqualified. JT8 authored texture layout and authentic textured-export agreement are also pending. No new JT version, vendored decoder change, shader/data-model extension, dependency or About change. C2 remains open.

## Prepared validation

`explicitBorderTests()` in `jt_texture_border_tests.cpp` adds distinct known ring texels around a known 2x2 interior. Prepared checks inspect exact canonical pixels, separately sampled axes, UV mapping, alpha policy, unchanged geometry, unsigned-byte/unsigned-16/float32 components, both byte orders, enabled modern minors and 9.0/9.5 local texture versions 1/2/3. A shared child image inherits different wrap axes to detect crop-cache aliasing. Nonfinite sampled-ring input must retain geometry without a texture. Existing C2p negative coverage now uses a mipmapped sampled ring, because a non-mipmapped ring is accepted here. C2p unsampled-ring and C2q constant-border regressions remain active. These checks were not run by the agent; use the existing [owner-run native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

Exact visual-control regeneration:

```powershell
python -B tools/jt/make_texture_constant_border_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-explicit-border-ui-fixtures --explicit-ring
```

Authoring produced 42 positive original roots and three shared glTF references. The existing GTEX01 image/geometry and C2f tiled UVs are reused. Original opaque ring pixels use orange/purple side columns, magenta/cyan top/bottom rows and white corners. Expected glTFs carry explicitly cropped full images and independently calculated UV buffers with ordinary clamp-to-edge/repeat/mirror samplers. Existing [shared resources](JT_SHARED_TEXTURE_CONTROLS.md) prevent companion/reference duplication. `authoring.json` in the ignored gate folder records filenames/hashes and reference mappings.

These are original modern/9.x descriptor envelopes referencing accepted 8.2 geometry controls. Synthetic mixed-family files are not authentic same-version exports or conformance evidence. No negative case is included in the visual set.

## Owner visual gate

Rebuild normally, then compare the following in `out/jt-research/texture-common-ui-fixtures/`, with camera/up-axis settings matched. Reference keys resolve through `out/jt-research/texture-explicit-border-ui-fixtures/authoring.json`. Pattern placement, border edges/corners and orientation should match; lighting need not.

1. `texture-explicit-border_10_0_both_inline_le.jt` and `texture-explicit-border_10_0_both_inline_be.jt` against reference `both`: border colours vary by edge, with white corners where both coordinates lie beyond the image.
2. `texture-explicit-border_10_0_s_repeat_inline_be.jt` against `s_repeat`: sampled side borders remain visible, while T repeats the interior.
3. `texture-explicit-border_10_0_t_mirror_inline_le.jt` against `t_mirror`: sampled top/bottom borders remain visible, while S mirrors the interior.
4. `texture-explicit-border_9_0_v1_both_be.jt`, `texture-explicit-border_9_5_v2_s_repeat_le.jt` and `texture-explicit-border_9_5_v3_t_mirror_be.jt` against the corresponding references.
5. Regressions: `texture-constant-border_10_0_both_inline_le.jt` and `texture-border_10_0_repeat_ring_le.jt` should retain their accepted appearance.

The set contains 12 positive documents including references and regressions. Report missing/wrong borders, orientation differences, pattern stretching, unintended transparency or regressions before dependent implementation proceeds.

## Sources and provenance

Consulted retained Siemens JT10.0 Rev-B border/environment/wrap definitions and retained public Khronos OpenGL2.1 sections 3.8.7/3.8.8 on interior-relative coordinates, explicit border texels and base-level filtering. The existing 9.x field layout follows the retained JT9.5 Rev-A hierarchy. Source URLs and exact retained SHA-256 hashes are listed in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). No new download, third-party implementation, vendor software, SDK/schema, disassembly or licence interpretation was used. GTEX01 CC0/source attribution remains in [source provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No generated asset or vendor document is tracked. This records implementation method and qualification limits, not legal clearance.
