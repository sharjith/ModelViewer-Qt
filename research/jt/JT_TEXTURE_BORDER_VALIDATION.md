# Unsampled texture borders - Gate C2p

22 October 2026, `feature/JT-importer`, following accepted/pushed C2o `c616126`. **Owner accepted C2p on 22 October 2026 after reporting tests passed.** Original controls were authored and reference paths inspected. The owner built the app; the agent launched the 13 positive checklist/reference/regression documents together in one installed instance at the owner request. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Supported boundary

The original application adapter now handles authored borders when both S and T use repeat, mirrored repeat or clamp-to-edge, whose sampling stays within the interior. A constant-border environment with a borderless image is accepted for those wrap modes because the constant colour is unused. An explicit one-texel inline border ring is decoded/validated, then removed before creating the existing canonical image. Normalized UVs continue to address the interior; they are not rescaled or offset. All supported numeric/colour conversion, row orientation, image limits and cancellation are retained. Transparent ring pixels do not make an otherwise opaque interior transparent.

Border mode and inline descriptor must agree. Explicit rings need an inline descriptor and positive interior dimensions. Legacy clamp or clamp-to-border combined with an authored border remain unsupported: this gate does not approximate sampled border colours/texels. External explicit rings are also unqualified; external images with an unused constant border can reuse the existing path. Nonfinite float values in the ring still reject image mapping, even though that ring would not be sampled. The complete raw image remains subject to bounds before cropping; extra interior allocation is charged. Unsupported/inconsistent cases retain geometry with a warning.

No texture/shader/material data-model extension, dependency, enabled revision or About change. Accepted image source bytes/dimensions/border metadata remain unchanged; the derived display image contains only interior pixels. Image/material cache identity retains its existing source/environment distinctions. Authored mip preservation, sampled border fidelity and other C2 work remain open. JT8 texture layout and authentic textured-export qualification remain unavailable; this is a bounded modern/9.x appearance extension, not full border completion.

## Prepared validation

The new `jt_texture_border_tests.cpp` joins `jt_tests`. Original RGBA fixtures put a bright transparent ring around four independently known opaque texels. Cases cover no/constant/explicit border environments; repeat/mirror/edge wraps; unsigned-byte, unsigned-16 and float32 inputs; both byte orders; enabled modern minors and enabled 9.0/9.5 local texture versions 1/2/3. Checks inspect exact interior pixels, unchanged positions/UVs/counts and absence of transparency from the discarded ring. Negative cases cover sampled wrap modes, missing/contradictory descriptors, no interior, external explicit rings and nonfinite ring values. These C++ checks are prepared, not run by the agent. Owner can use the existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

Original authoring reuses accepted GTEX01 geometry/image and the independently authored tiled UV reference from C2f. The JT matrix tiles 2x2; three glTF samplers independently specify repeat, mirrored repeat or clamp-to-edge. RGBA interiors are opaque, with an added transparent magenta one-texel ring for explicit cases. Shared resources/references are reused; no gate-specific copies are made. Exact regeneration:

```powershell
python -B tools/jt/make_texture_border_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-border-ui-fixtures
```

Authoring completed: 84 positive roots (12 modern 10.0 and 72 basic/advanced 9.x), three shared glTF references and the ignored gate manifest. Modern later revisions have prepared native-record checks rather than new authentic textured files. Roots are directly serialized original family records using accepted original/source-derived 8.2 geometry companions. Mixed-family synthetic controls do not establish authentic same-version exporter agreement. No negative case is included in the visual output set.

## Owner visual gate

Rebuild normally. All physical files are in `out/jt-research/texture-common-ui-fixtures/`; reference mappings are in `out/jt-research/texture-border-ui-fixtures/authoring.json`. Match camera/up-axis settings; compare pattern/coverage/orientation rather than brightness.

1. Compare `texture-border_10_0_repeat_ring_le.jt` and `texture-border_10_0_repeat_ring_be.jt` with reference key `repeat`. Both should show the same 2x2 repeated interior without a magenta fringe or transparency.
2. Compare `texture-border_10_0_mirror_ring_le.jt` with reference `mirror`, and `texture-border_10_0_edge_ring_be.jt` with reference `edge`. Mirror tiles alternate orientation; edge sampling extends the edge texels. Neither should show the authored ring.
3. Compare `texture-border_10_0_repeat_constant_le.jt` with `repeat`. Its unused constant-border setting should not change the image.
4. Compare `texture-border_9_0_v1_repeat_ring_be.jt`, `texture-border_9_5_v2_mirror_ring_le.jt`, `texture-border_9_5_v3_edge_ring_be.jt` with their matching references. Basic and advanced layouts should agree with the corresponding modern case.
5. Each control retains one mesh, four vertices and two triangles. Reopen `texture-s8_bgra_le.jt` and `texture-u32_rgb_le.jt` as regressions.

This is 13 unique positive documents including three shared references and two regressions; open together in one app invocation after the owner build. The owner accepted this visual gate; optional-suite execution and authentic textured-export conformance are not inferred. C3 production checks remain independently pending.

## References and provenance

Read-only research consulted retained Siemens JT10.0 Rev-B texture border-mode, image-border and wrap descriptions (section6.1.2.3), plus the retained public Khronos OpenGL2.1 texture image/interior and wrap/filtering rules in sections3.8.1,3.8.7 and3.8.8. These establish interior-relative sampling and the selected unsampled-border subset. No new document download or licence interpretation was needed:

- [Recorded JT10.0 publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), retained SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; publisher availability not newly verified.
- [Khronos OpenGL2.1 specification](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf), retained SHA-256 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`; retrieval/mirror provenance is in [C2o validation](JT_TEXTURE_SIGNED_VALIDATION.md#references-and-provenance). Retained text was inspected; no diagram/table is redistributed.

Existing original ModelViewer adapter, fixture writers, Qt image API and resource policy provide implementation conventions. GTEX01 URL, checkout/hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new asset, third-party implementation, vendor software, SDK/schema or disassembly was used. Vendored sources/hashes/notices, decoder file counts and dependencies are unchanged. All generated controls, images and manifests remain ignored under `out/`; tracked research stays Markdown. This records method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
