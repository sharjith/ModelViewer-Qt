# Inline texture colour formats — Gate C2h

Owner acceptance, 22 October 2026: the owner reported "All looks good" and authorized commit, push and proceeding. This records the requested visual checks, not unreported optional-suite execution or authentic textured-export conformance. The agent did not build/run C++ or the viewer.

22 October 2026, `feature/JT-importer`, following owner-accepted model-linear mapping `38f6c9a`. **Prepared for owner build/test; the agent has not built or run the C++ changes.** Original authoring produced comparison controls under ignored `out/`.

## Supported subset

The original image adapter now expands unsigned-byte inline colour images to canonical RGBA for the already supported borderless 2D MODULATE texture profile. RGB/RGBA and BGR/BGRA behavior is retained. Added formats are luminance, luminance-alpha, individual red/green/blue components and alpha-only. Luminance replicates into RGB; single colour components leave the other colour channels zero and supply opaque alpha. Alpha-only supplies white RGB as the neutral MODULATE factor, preserving incoming part colour while applying the texture alpha. This last rule preserves alpha-texture environment semantics rather than blindly retaining temporary zero RGB from pixel-transfer assembly.

Row strides respect authored alignments 1, 2, 4 or 8, with exact base-image byte counts. Padding is ignored and bottom-up source rows retain the already accepted image orientation. Existing bounds, image deduplication, cancellation and resource policy remain. Existing blend-disabled/ONE-ZERO behavior forces alpha opaque; supported active blending retains image alpha and selects a transparent material. No global shader/blend policy changes.

Unspecified, depth and stencil formats remain unsupported image mappings with a warning and geometry fallback. Signed, bit-packed, 16/32-bit integer, float/half-float samples, image borders, 1D/3D textures and authored higher-mipmap retention remain open. Unsupported pixel type/format does not silently become RGB. No new decoder payload layout, dependency, enabled JT revision or About claim is introduced. Legacy authored texture records remain unqualified; all seven enabled modern minors have prepared original-record checks, not authentic-export pixel-format certification.

## Prepared verification

`jt_texture_pixel_tests.cpp` joins the existing portable suite. Original 3×2 texels have distinct colour values and alpha values spanning zero, partial and opaque. Expected canonical pixels are authored independently of the adapter. Cases cover all ten supported colour formats, both byte orders, enabled modern minors, wrapped/base-v2 records, active/disabled blending and explicit alpha-pass association. All four alignments run against the baseline minor; later modern minors retain alignment-8 coverage. Missing/excess base-row bytes and non-colour formats must keep geometry with diagnostics and avoid pixel overreads.

These tests have not been executed by the agent. The owner rebuilds/runs the existing isolated [image-gate commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls); the new cases are automatically included in `jt_tests`. Native/renderer execution is not inferred from fixture authoring or prior gates.

## Authoring and visual gate

The original authoring tool reuses authorized GTEX01 geometry and derives packed pixels plus canonical reference images. An odd-width crop (255×256 for the current source) and alignment 8 deliberately add padding. Opaque alpha is used in all visual controls, including alpha-only, so this gate's picture comparison isolates component conversion. Alpha-only therefore appears uniformly white with the current white material; this is expected. These quad controls do not claim a new transparency/occlusion acceptance result; the prior cube transparency gates remain separate, and the prepared native cases check non-opaque image bytes and material selection.

The tool ran successfully and authored 20 JT controls (ten formats, both byte orders), ten glTF references, companion images/buffers and ignored `authoring.json`. It did not execute a JT reader, C++ test or renderer. Exact regeneration:

```powershell
python -B tools/jt/make_texture_pixel_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-pixel-ui-fixtures
```

Rebuild the viewer. Use `out/jt-research/texture-pixel-ui-fixtures/`, keeping all companions together. Compare each `<stem>_le.jt` with `<stem>_reference.gltf`:

| Stems | Expected result |
| --- | --- |
| `rgb`, `rgba`, `bgr`, `bgra` | Matching full-colour pattern, with no component swap or row bands |
| `luminance`, `luminance_alpha` | Matching grayscale pattern derived from the same supplied luminance samples |
| `red`, `green`, `blue` | Matching isolated-colour patterns |
| `alpha` | Matching uniformly white opaque surface |

Every control has one mesh, four vertices and two triangles. Check orientation, pattern, channels and coverage; shading brightness may differ. Also open `bgr_be.jt` and `luminance_alpha_be.jt` to compare with their matching references. Reopen an accepted ordinary JT assembly and a C2g planar control as regressions. No manual texture assignment is needed.

Stop for owner results before committing this gate. Broader C1/C2 features, C3–C7 and forward 10.11 qualification remain on the plan.

## Provenance and semantics

Consulted retained Siemens JT 10.0 File Format Reference Rev-B section 6.1.2.3, image format/data type tables 30/31 and row alignment description: [publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`.

Also read the Khronos OpenGL 2.1 [glTexImage2D reference](https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/glTexImage2D.xml), retained local SHA-256 `b862c336c5d3e5d15caed4029fb70520634e3f400a35a7691c675b09af4f58d8`, and [glTexEnv reference](https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/glTexEnv.xml), SHA-256 `59a5807f07d94b4e69ac848abe98dac5d3e270e2f176ccf6081bc7657b4c13f1`. They distinguish colour-component expansion from alpha-only base-format MODULATE behavior. Pages remain local under ignored research storage; no specification text or source implementation is copied into the repo. The browser could not parse the XML content type, so the public XML pages were downloaded directly for read-only inspection.

Implementation uses original ModelViewer image/fixture code at `38f6c9a`. Existing GTEX01 source repository, checkout, hashes and CC0 attribution are recorded in [image validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new source asset, vendor software, SDK/schema, disassembly or third-party decoder implementation was used. No vendored source or notice/licence term changed; existing Qt/Pillow dependencies are reused. This describes consulted sources and method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
