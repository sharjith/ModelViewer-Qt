# Signed integer inline images - Gate C2o

22 October 2026, `feature/JT-importer`, following accepted/pushed C2n and shared controls `9a420e6`. **Owner accepted C2o on 22 October 2026 after reporting tests passed.** Original control authoring and read-only reference-path checks ran. The owner built the app and the agent launched the 29 positive checklist/reference/regression documents together in one installed app instance at the owner request. No C++ build/native suite run by the agent. Standing authorization permits commit/push/proceeding.

## Scope and conversion policy

The original application adapter now accepts signed pixel data types 1/6/8 (8/16/32-bit) in the existing borderless 2D colour profile. It assembles source-endian words and sign-extends with explicit widened arithmetic, avoiding out-of-range unsigned-to-signed casts. Existing row/image limits, cancellation, component expansion, alpha policy, UV mapping and resource handling remain. No third-party decoder, shader or dependency change.

The selected pixel-transfer policy follows the OpenGL version-2 references identified in the retained JT10.0 bibliography. For signed component `c` of width `b`, normalize with `(2*c+1)/(2^b-1)`, clamp to [0,1], then apply the established nearest-byte display policy. This is an interoperability interpretation of JT's referenced historical graphics profile; authentic textured-export confirmation remains unavailable. Negative source components display as zero. A signed 8-bit numeric zero displays as byte 1, and positive signed-byte levels map to odd display bytes. Wider signed zero rounds to display zero. Full source precision is reduced in the RGBA8/PNG display image; original descriptor type/bytes stay unchanged.

The same code applies behind accepted basic/advanced 9.x and modern 10.x envelopes. Signed alpha is converted before existing blending-disabled/ONE-ZERO opacity handling. All ordinary integer/float component types are now mapped for the supported colour profile; bit-packed type 4 remains unqualified. JT8 texture layouts, non-colour formats, borders, authored mip selection, wider texture types/combiners, automatic/multiple units and view/projective mapping remain open. No file revision/About/full-C2-completion claim is added.

## Prepared verification and original controls

`jt_texture_pixel_tests.cpp` adds all three signed widths across supported colour formats, enabled modern minors, both byte orders, row alignments and alpha policies. Selected formats cover enabled 9.0/9.5 and texture local versions 1/2/3. Independent raw patterns exercise signed minima, negative interiors, -1, zero, positive interiors, midpoint neighbours and maxima with explicit expected pixels. Short/long rows must fall back. Prior unsigned/float and unspecified/bit-packed fallback cases remain. These C++ cases are prepared, not executed by the agent; owner may use the existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls).

Writers accept mutually exclusive `--signed-bits 8`, `16` or `32`. Colour-control encoding uses signed minimum for black, plus positive samples chosen for the expected display value. Independent explicit native patterns separately test numeric zero and negative cases. Signed-byte glTF references account for its quantization. External-image controls use the corresponding shared expected PNG, keeping their reference comparison meaningful. All geometry/buffers/identical references use the [shared store](JT_SHARED_TEXTURE_CONTROLS.md); no per-gate companion copies are created.

Exact regeneration, writing generated files/manifests under ignored `out/`:

```powershell
foreach ($bits in 8,16,32) {
    python -B tools/jt/make_texture_pixel_controls.py out/jt-research/texture-source-ui-fixtures "out/jt-research/texture-s$bits-ui-fixtures" --signed-bits $bits
    python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures "out/jt-research/texture9-s$bits-ui-fixtures" --advanced --signed-bits $bits
}
```

Authoring completed: 60 modern JT roots and 144 advanced 9.x roots, with shared expected references and gate manifests. Original family roots are serialized directly and reuse accepted original/source-derived 8.2 geometry; these mixed-family synthetic assemblies do not establish authentic same-version vendor co-export conformance. Images, source licence, PDF, preview and manifests stay local/ignored.

## Owner visual gate

Rebuild normally. All physical documents are in `out/jt-research/texture-common-ui-fixtures/`; reference filenames are in the gate manifests' `references` mapping. Match camera/up-axis settings and compare pattern, orientation and coverage rather than brightness.

For each width `8`, `16`, `32`:

1. Open `texture-s<width>_bgra_le.jt` and `texture-s<width>_bgra_be.jt`. Compare both with that width's modern manifest reference key `bgra`.
2. Open `texture-s<width>_luminance_alpha_le.jt` and `texture-s<width>_alpha_be.jt`. Compare with modern reference keys `luminance_alpha`/`alpha`. Expect greyscale and uniformly white respectively; visual controls are opaque.
3. Open `texture9-s<width>_9_5_v3_planar_texture_inline_be.jt` and `texture9-s<width>_9_0_v2_mirror_texture_inline_le.jt`. Compare with that width's legacy manifest keys `9_5/v3/planar`/`9_0/v2/mirror`. Expected coverage/orientation must agree without manual UV generation.
4. Each JT retains one mesh, four vertices and two triangles. Reference images can differ slightly by signed width; compare with the matching width's reference.

Reopen `texture-u32_rgb_le.jt` and `texture-half_rgb_le.jt` as regressions. Open the unique checklist/reference/regression set together using one app invocation; negative/native failure cases are excluded. The owner accepted this visual gate; optional-suite execution and authentic textured-export conformance are not inferred. C3 production checks remain independently pending.

## References and provenance

Consulted retained Siemens JT10.0 Rev-B image-type definitions and its bibliography's OpenGL version-2 reference, plus the official Khronos **OpenGL 2.1 specification, 1 December 2006**, table 2.9 and pixel-transfer section 3.6.3. The table was visually checked with the PDF skill (printed page 59, PDF page index 72). Existing original ModelViewer image code supplied integration conventions. Search also exposed later graphics-profile descriptions; the implementation policy above is explicitly based on the historical version-2 reference. No new third-party implementation code was consulted.

- JT10.0 PDF SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; earlier publisher provenance is in [C2n validation](JT_TEXTURE_UINT32_VALIDATION.md#references-and-provenance).
- [Khronos OpenGL 2.1 specification](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf), browsed on 22 October 2026. A direct local download returned HTTP 403; the retained PDF was obtained from the [official Khronos registry repository mirror](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/gl/glspec21.pdf). SHA-256 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`.

Read-only visual inspection is reproducible after retaining the cited PDF as `out/jt-research/opengl21-spec.pdf`:

```powershell
python -B -c "import pymupdf; d=pymupdf.open('out/jt-research/opengl21-spec.pdf'); d[72].get_pixmap(matrix=pymupdf.Matrix(1.5,1.5)).save('out/jt-research/c2o-signed-conversion.png'); d.close()"
```

No document/table or vendor source is redistributed. GTEX01 URL, checkout/hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new asset, vendor software, SDK/schema, disassembly or licence interpretation was introduced. Upstream code/hashes/notices, decoder file counts and dependencies are unchanged. This records method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
