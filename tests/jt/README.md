# JT importer tests and fixtures

Configure this folder as a standalone CMake project against ModelViewer's OCCT, Qt, Assimp, zlib and LibLZMA dependency prefix. Set `MV_JT_SANITIZE=ON` for MSVC AddressSanitizer instrumentation. Run `jt_tests` for the portable synthetic suite or `jt_tests <sample-folder>` to include the five local part regressions. Make the dependency DLLs and, for sanitizer builds, the MSVC sanitizer runtime available on `PATH`.

`JtContainerFixture.h` and `jt_scene_tests.cpp` contain original generated containers and synthetic geometry. They are part of the repository and do not require downloaded CAD data for the portable suite.

`JtLegacyFixture.h` also exercises the shared 8.0–8.2 lossless family with LE/BE, raw/zlib vertices, codec/binding rejection and the version-specific sample L81 EOF exception. See [8.x validation](../../research/jt/JT_8X_FAMILY_VALIDATION.md) for the authentic 8.0 success and expected 8.2 quantized sample Q82 rejection. Recognizing a family does not claim all its geometry encodings are implemented.

The later [quantized follow-up](../../research/jt/JT_LEGACY_QUANTIZED_VALIDATION.md) supersedes that original sample Q82 rejection. `jt_quantized_tests.cpp` adds independently calculable positions/normals/UVs/constant colours, arithmetic/OOB/multiple-context tests, malformed-input/resource checks and application/GLB attribute preservation. Use `jt_tests --quantized-fixtures out/jt-research/quantized-ui-fixtures` to generate the original `legacy_quantized_le.jt` and `legacy_quantized_be.jt` owner acceptance files. Each contains one mesh, seven vertices and three triangles. No downloaded geometry is included in these generated fixtures.

To reproduce the viewer acceptance fixtures, supply a locally obtained `Bolt.jt`:

```text
python tests/jt/generate_display_fixtures.py --samples out/jt-research/samples --output out/jt-research/selection-fixtures
```

The generator copies that supplied sample into the chosen output folder. It does not download or grant redistribution rights to the sample. Keep the generated `parts/` folder with the fixtures. Their generated README lists expected colors, placement, mesh counts and errors. The optional source samples and generated exports remain outside version control.

`jt_probe input.jt [output.glb]` inspects an import and optionally exports it. Add `--eye x,y,z` to exercise import-time RangeLOD distance selection; this does not enable runtime switching in the viewer.

Add `--validate` to check finite positions/local transforms, normalized normals, triangle indices and scene mesh/material references, and print local mesh-resource bounds and occurrence counts. The [JT 10.3 sample A103 validation](../../research/jt/JT_103_ASSEMBLY_VALIDATION.md) records the large local assembly check. Portable tests also cover original LE/BE 10.3 graph layouts and configurable cumulative allocation-budget boundaries; downloaded geometry stays outside the repository.

Add `--properties` to print supported source node properties as JSON. Generate the metadata acceptance files with:

```text
jt_fixturegen --samples out/jt-research/samples --output out/jt-research/metadata-fixtures
```

This generator also references a locally supplied Bolt sample. It shares the original container serializer used by the portable tests. See [metadata validation](../../research/jt/JT_METADATA_VALIDATION.md) for the representation, qualified local versions, expected values and UI/export limitations.

Use `jt_fixturegen --kind draw-style --samples <sample-folder> --output <output-folder>` for the draw-style fixture set. `jt_probe input.jt --materials` prints resolved style/culling/unlit fields. See [draw-style validation](../../research/jt/JT_DRAW_STYLE_VALIDATION.md) for expected counts and renderer limitations.

The [wireframe follow-up](../../research/jt/JT_WIREFRAME_VALIDATION.md) adds mixed red-wire/green-solid fixtures and optional native OpenGL checks under `tests/render`. It also verifies material runtime JSON persistence. Refer to that follow-up for the current `draw_wire_hint.jt` expectation.

The [shininess follow-up](../../research/jt/JT_SHININESS_VALIDATION.md) tests inherited Phong exponents, their approximate PBR roughness, material-cache separation, scene copying and GLB roughness persistence. Use `jt_probe <file.jt> --materials` to inspect both fields.

The [vertex-colour follow-up](../../research/jt/JT_VERTEX_COLOUR_VALIDATION.md) covers lossless RGB/opaque-RGBA arrays, colour seams and material overrides. `jt_color_fixturegen <local Bolt.jt> <output-folder>` produces native viewer fixtures from locally supplied geometry and checks their import. The test-only modifier is deliberately restricted to that little-endian JT 10.0 sample layout; it is not a general JT writer or independent conformance oracle.

The [JT 8.1 follow-up](../../research/jt/JT_81_VALIDATION.md) adds original full-file legacy fixtures (`JtLegacyFixture.h`) and `jt_legacy_tests.cpp`. Run `jt_tests <JT-10-sample-folder> <local sample-L81.jt>` for both corpora, or `jt_tests --legacy-fixtures <output-folder>` to generate two small blue legacy models for the UI. Downloaded sample L81 geometry remains external to the repository.

Coverage, sample provenance and UI acceptance are recorded in [native importer](../../research/jt/JT_NATIVE_IMPORTER.md), [assembly validation](../../research/jt/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md) and [display selection validation](../../research/jt/JT_DISPLAY_SELECTION_VALIDATION.md).

Run `python tests/jt/check_provenance.py` to check decoder manifest coverage and retained source notices. This checks record consistency; it does not establish legal clearance or fulfil binary source-delivery obligations by itself.

The [modern attribute foundations](../../research/jt/JT_MODERN_ATTRIBUTE_VALIDATION.md) add original version-2/Sabot envelope and identity tests in `jt_modern_attribute_tests.cpp`. Generate geometry-independent Gate 2a viewer files with `jt_tests --modern-attribute-fixtures out/jt-research/modern-attribute-ui-fixtures`. Palette maps and nonfallback palette-scoped rendering remain explicit unsupported boundaries at this gate; no additional JT version is accepted.

The [10.9 palette follow-up](../../research/jt/JT_109_PALETTE_VALIDATION.md) adds `jt_palette_tests.cpp`: independent state accumulation, fallback/inhibition, material splits, transforms, shared resources, raw/nested arithmetic escapes and malformed packet/tail checks. `jt_tests --palette-fixtures out/jt-research/palette-ui-fixtures` exports original three-colour triangle geometry as GLB for scene/material checks. `jt_probe --allocation-budget-mib <MiB>` overrides the cumulative charged-allocation budget for native diagnostics; the current default is 6 GiB and the per-allocation guard remains 256 MiB. Gate 2a's earlier unsupported-palette fixture description is historical and superseded by this follow-up.

The [JT 9.5 follow-up](../../research/jt/JT_95_VALIDATION.md) adds `jt95_tests.cpp`: original LE/BE Mk.2 arithmetic tables with unused zero-frequency entries, fixed/variable bitlength packets, full-width integer ranges, chopper fields, malformed streams and cross-version repeated-instance assemblies. Whole-attribute Final/Force tests also cover shininess. Generate the original assembly UI fixtures with `jt_tests --95-fixtures out/jt-research/95-ui-fixtures`; keep `part.jt` beside both assembly files. Their 9.5 graphs reference original 8.2 fixture geometry, so they qualify mixed-version references and scene state, not native 9.5 tessellation by themselves. Native 9.5 tessellation is checked separately on the unchanged externally supplied input.

The [point-version expansion](../../research/jt/JT_POINT_VERSION_EXPANSION.md) first normalizes older material inhibit fields into the scene adapter's numbering. Original LE/BE bit goldens and three mixed-version assembly cases cover combined and separate diffuse/alpha inheritance. Run `jt_tests --material-mask-fixtures out/jt-research/material-mask-ui-fixtures` for the owner display checks; keep the generated `part.jt` alongside both assemblies. No additional file-version identifier is qualified by these fixtures.

Use `python tests/jt/inventory_headers.py <export-folder> --output out/jt-research/export-inventory.json --sha256` to inventory supplied exports independently of the importer's version gate. JSON includes original private paths and belongs outside version control. The console prints only version counts. The script reads 80-byte headers by default, optionally hashes full files, preserves two-digit minor identifiers and does not certify format compatibility.

The supplied-export follow-up adds high-degree topology-mask packet goldens to `jt95_tests.cpp`: both 9.5/10 layout families and byte orders, nonempty raw words, legacy upper-four-bit stream, subsequent split-stream/hash alignment and every truncated prefix. The actual 9.5 export exposed the inherited CDP/raw-array dispatch error; tests keep invalid zero-length CDP rejection intact. See the [point-version expansion](../../research/jt/JT_POINT_VERSION_EXPANSION.md).

## Cross-version appearance diagnostics

See [the appearance investigation](../../research/jt/JT_CROSS_VERSION_APPEARANCE_INVESTIGATION.md). `jt_probe --source-attributes <part.jt>` prints ordered unaccumulated attributes for the input graph without following external partitions. `--materials` includes resolved diffuse RGBA/opacity. Generate original ordered-material interoperability cases with `jt_tests --material-order-fixtures <output-folder>` and retain the generated `geometry.jt` beside them. Forward/reverse order should resolve to green/red respectively; the portable suite checks both byte orders, field-final protection and enabled modern revision headers. Source data and outputs containing private paths remain outside version control.

The source-derived control set (kept private outside the repo) confirmed an observed writer-specific duplicate-material convention. The scoped scene-adapter correction has writer/author, shape, count, flag and palette guards plus opt-out/sibling-isolation tests. `jt_probe --published-material-order` disables that profile for comparison; it does not change parser version coverage. Original fixture header writers now include the required ASCII/binary translation-detection marker. The earlier Solid Edge rejection of synthetic files did not establish ordering semantics.

## JT 9.0 qualification

[Gate 3c1](../../research/jt/JT_90_VALIDATION.md) adds the authentic 9.0 export to the shared 9.x path. Original 9.0/9.5 LE/BE graphs check instances, RGB, local transforms, base/material-version rejection and truncated containers. 9.0 material version 2 remains explicitly unqualified; 9.5 versions 1/2 stay covered. `jt_tests --90-fixtures <folder>` emits two root graphs referencing original 8.2 geometry, not native 9.0 tessellation fixtures: keep `part.jt` beside them. Expect two occurrences/resources, 14 vertices and six triangles, one red and one green at world x=110/210 and y=-3. Real exported 9.0 tessellation is qualified separately by the private authentic corpus. Unsupported-version checks retain 9.1–9.4 rejection.

The additional 9.0 empty-shape regression distinguishes zero-range/zero-topology placeholders from unsupported or malformed geometry. Original LE/BE packets test zero streams, nonempty raw masks and truncation; scene tests retain real siblings while rejecting unknown/nonzero ranges, unvalidated empty output and empty-only LODs. See [9.0 follow-up](../../research/jt/JT_90_VALIDATION.md).

## JT 10.5 qualification

[Gate 3e1](../../research/jt/JT_105_VALIDATION.md) qualifies the supplied authentic 10.5 assembly with palette maps and Sabot-wrapped indexed materials through the modern reader. Original LE/BE container, base-v1/v2 fallback scene, wrapped/unwrapped attribute, local-version, truncation, duplicate-ID, palette-tail and material-order cases now include 10.5. `jt_tests --105-fixtures <folder>` emits LE/BE 10.5 attribute graphs referring to original 8.2 geometry; keep `geometry.jt` beside them. Each should display one green quad-plus-triangle mesh, seven vertices/three triangles, with world x=20 and y=-3. These are mixed-version graph tests, not authentic 10.5 geometry qualification. Public support/limitations and the owner UI checklist are in the validation report.

The additional 10.5 appearance regression extends the exact-header writer-profile tests: all fifteen positive/negative guard cases plus sibling isolation now run for both the observed 10.5 ISO14306-4 and 10.9 writer signatures. Source-order semantics remain the default outside the profile; see [10.5 validation](../../research/jt/JT_105_VALIDATION.md).

## JT 10.7 and 10.8 qualification

[Gate 3f](../../research/jt/JT_107_108_VALIDATION.md) extends the original container, wrapped/unwrapped base-v2 attribute, base-v1/v2 fallback scene/GLB, palette packet/tail, malformed-record and published material-order tests to 10.7/10.8. Authentic stored tessellation is qualified separately through the supplied assemblies. `jt_tests --107108-fixtures <folder>` writes four original LE/BE attribute graphs plus their shared original 8.2 `geometry.jt`; each graph should display one green mesh, seven vertices/three triangles at world x=20, y=-3. Keep all files together. Synthetic headers do not establish authentic big-endian tessellation coverage. No broader duplicate-material writer profile is inferred.

The subsequent additional 10.7/10.8 appearance cases extend `duplicateMaterialCompatibilityTests` to all four observed exact writer signatures (10.5 ISO14306-4, 10.7, 10.8, 10.9; DM 10.8.0.1). Each runs all fifteen guard cases plus sibling isolation, retaining opt-out, writer/author, palette, flags and shape-only scope. No downloaded geometry is included in these original tests.

## JT 10.10 qualification

[Gate 5a](../../research/jt/JT_1010_VALIDATION.md) adds minor 10 to original LE/BE container, modern attribute/palette, fallback scene/GLB, malformed-record and published material-order tests. 10.1 and 10.11 remain negative cases. The original material-profile guard suite also runs the newly observed exact 10.10 writer signature, retaining opt-out and writer/author/flags/palette isolation. `jt_tests --1010-fixtures <folder>` generates two original 10.10 LE/BE attribute graphs referencing original 8.2 geometry; each displays a green mesh with seven vertices/three triangles at world x=20, y=-3. Authentic 10.10 tessellation qualification comes from the separate supplied assemblies, not these mixed-version controls.

## Feature gate C1a: legacy Huffman

`jt_huffman_tests.cpp` supplies original known-value CDP1 packets, tie/singleton/cross-word/escape tests, every truncated prefix, declared-bit and OOB bounds, cancellation and budgets. The full original 8.x geometry tests now include Huffman strip boundaries. `jt_tests --huffman-fixtures <folder>` generates six self-contained blue quad-plus-triangle JT files for 8.0/8.1/8.2, LE/BE: one mesh, seven vertices, three triangles each. Multiple-context Huffman remains an explicit unsupported case; modern packet transports are unchanged. See [C1a validation](../../research/jt/JT_HUFFMAN_VALIDATION.md) and the [feature-completion plan](../../research/jt/JT_FEATURE_COMPLETION_PLAN.md).

## Feature gate C1b: modern packed quantized colours

`jt_quantized_colour_tests.cpp` supplies original RGB/HSV, 32-bit RGBA, endpoint/interpolation, all-sector/hue-wrap and alpha vectors, every truncated prefix and malformed/budget/cancellation cases under every enabled modern revision, LE/BE. The existing private `jt_color_fixturegen` emits packed RGB/HSV/opaque-RGBA and synthetic modern-header controls from a locally obtained 10.0 source, checking geometry/normal/index/colour agreement against lossless reference output. These contain source geometry and stay ignored, not bundled. Older 9.x component arrays and legacy variable colours remain separate work. Active vertex-alpha rendering is not introduced. See [C1b validation](../../research/jt/JT_QUANTIZED_COLOUR_VALIDATION.md).

## Feature gate C1c: transitional colour arrays

`jt9_colour_tests.cpp` supplies original LE/BE 9.0/9.5 paired exponent/mantissa and four Lag1 component-vector colours, including malformed/truncated fields, array counts, alpha-vector validation, cancellation and budgets. `jt9_colour_fixturegen <local-single-part-9.x.jt> <output-folder>` generates five source-derived lossless/quantized RGB/RGBA/HSV controls from an observed little-endian 9.0/9.5 part, preserving geometry/normals/indices and source bytes. The outputs contain source geometry and stay ignored. The current two sets each have one mesh, 218 vertices and 188 triangles. See [C1c validation](../../research/jt/JT_9X_COLOUR_VALIDATION.md) for source/reference qualification limits and the owner checklist.

## Feature gate C1d: topological UV arrays

`jt_uv_tests.cpp` supplies original 9.x/10.x LE/BE lossless/quantized UV packets, tiling/negative ranges, quantizer endpoints, truncation/count/field/budget/cancellation rejection and UV-only seam cases. Combined colour/UV scene tests retain seams through stored/generated/flat normals, scene copies and GLB. `jt_uv_fixturegen <local-single-part-9.x-or-10.0.jt> <private-output-folder>` generates source-derived display controls and verifies source positions/normals/indices, UV transport agreement, copies, GLB corner mappings, flat expansion and invalid-alternative fallback. The tool is limited to observed little-endian single-part 9.0/9.5/10.0 templates. Source geometry and outputs remain private/ignored. No texture-image import is implied. See [C1d validation](../../research/jt/JT_TOPOLOGICAL_UV_VALIDATION.md).

The decoder now follows the main repository layout: `src/ThirdParty/occt-jt` owns its CMake target, implementation and provenance metadata; `include/ThirdParty/occt-jt` owns its package headers. Standalone tests use the same `mv_jt_codec` target as the application. The provenance checker verifies both roots against repository-relative manifest entries while retaining original upstream paths and hashes.

## Feature gate C1e: multiple UV sets

The UV field/binding and scene tests now cover four sets: a seam isolated to set 3; invalid sparse/high/dimensional bindings; colour/normal/UV expansion; four-set scene copies/GLB; and palette submesh coordinate remapping. `jt_uv_fixturegen <local-source> <private-output-folder> 4` generates the four-set private controls and checks every set through lossless/quantized import, copies, GLB rendered corners, flat normals and fallback. Omitting the optional count retains the accepted C1d one-set generator behavior. These controls do not demonstrate source texture-resource import or authentic multi-UV vendor exports. See [C1e validation](../../research/jt/JT_MULTI_UV_VALIDATION.md).

## Feature gate C1f: scalar texture coordinates

Original UV packets include scalar lossless/quantized fields and mixed bindings across enabled 9.x/10.x revisions in both byte orders. Scalar streams use only their declared component/quantizer, without consuming a nonexistent V array. Combined scene tests cover scalar V=0 embedding through flat/normal variants and GLB. `jt_uv_fixturegen <source> <private-output> 4 1212` generates mixed scalar/2D sets 0/1/2/3; the optional pattern defaults to all 2D coordinates. Private generator checks verify source geometry, output V=0 for scalar sets, copy/export/flat mappings and fallback. [C1f validation](../../research/jt/JT_SCALAR_UV_VALIDATION.md) explains the scene/export normalization and independent conformance limits.

C1g sparse UV controls extend the private generator component pattern with `0` for absent slots (e.g. `4 0201`). Original decoder/binding tests retain absence and set numbers; source-derived scene controls check zero-padded internal gaps, copy/GLB/flat mappings and LOD fallback. See [sparse UV validation](../../research/jt/JT_SPARSE_UV_VALIDATION.md). No private geometry is bundled.

C1h original field/model tests are in `jt_arithmetic_count_tests.cpp`. Generate the owner geometry-only controls using `jt_tests --arithmetic-count-fixtures <private-folder>`. Six LE/BE version controls and null-coded references must agree; short/long count files must reject and the LOD fallback must recover. See [arithmetic count validation](../../research/jt/JT_ARITHMETIC_COUNT_VALIDATION.md).

C1i field tests in `jt_coordinate_tests.cpp` cover the published rounded coordinate inverse, widths 1–32, constant ranges and malformed/bounded input. `jt_coordinate_fixturegen <local-single-part-9.x-or-10.0.jt> <private-folder>` creates source-derived 8/16-bit controls and checks every rounded position, error bound, normal/topology preservation, scene copy/GLB corners and fallback. No source geometry is bundled. See [coordinate validation](../../research/jt/JT_COORDINATE_QUANTIZATION_VALIDATION.md).

C2a adds original vertex-alpha scene and full-file controls. After building `jt_tests`, the owner runs `jt_tests --vertex-alpha-fixtures out/jt-research/vertex-alpha-ui-fixtures` to run the portable suite and generate the acceptance files. The owner subsequently authorized a local control build/generation, which passed with the portable suite. The same filenames now contain nested blue/orange cubes for an occlusion-based transparency check (two meshes, 48 vertices, 24 triangles); the revised generator and GLB reload checks also passed locally. The owner accepted the corrected shaded raster viewer checks on 21 October 2026; dedicated wireframe and ray-traced alpha remain separate gates. See [vertex-alpha validation](../../research/jt/JT_VERTEX_ALPHA_VALIDATION.md) for setup, exact commands, raster/GLB scope and expected viewer behavior. No downloaded source geometry is needed.

C2d adds original texture descriptor records and the optional generator `jt_tests --texture-fixtures out/jt-research/texture-descriptor-ui-fixtures`. The normal portable suite runs first. Prepared checks cover the modern/endian/base-version/wrapper paths, metadata, byte bounds and cancellation; no local build/run pass is claimed. Controls retain original cube geometry and diagnose unsupported texture rendering; this gate does not apply images. Keep generated `geometry.jt` beside the descriptor assemblies. See [texture descriptor validation](../../research/jt/JT_TEXTURE_DESCRIPTOR_VALIDATION.md) for exact owner commands and reference/conformance limits.
# Current texture image gate (21 October 2026)

C2i acceptance, 22 October: following C2h `d757c2f`, the owner confirmed the requested [basic 9.x texture checks](../../research/jt/JT_9X_TEXTURE_VALIDATION.md) passed and authorized commit/push. `jt9_texture_tests.cpp` is included in `jt_tests`; directly serialized synthetic roots/references remain under ignored `out/jt-research/texture9-ui-fixtures/`. Advanced texture versions remain diagnosed fallbacks; no separate optional-suite execution is inferred.

Current gate C2h, 22 October: C2g was owner-accepted and pushed as `38f6c9a`. [Inline colour pixels](../../research/jt/JT_TEXTURE_PIXEL_VALIDATION.md) adds prepared format/row/alpha checks through `jt_texture_pixel_tests.cpp`; visual controls are authored under ignored `out/jt-research/texture-pixel-ui-fixtures/`. The owner builds/runs native and viewer checks.

Current gate, 22 October: C2f was owner-accepted and pushed as `4de7b62`. [C2g planar texture mapping](../../research/jt/JT_TEXTURE_PLANAR_VALIDATION.md) adds prepared `jt_texture_planar_tests.cpp` checks to `jt_tests`; no-UV visual controls and glTF references are authored under ignored `out/jt-research/texture-planar-ui-fixtures/`. The owner builds/runs the suite and viewer.

22 October follow-up: the previous UV correction was owner-confirmed. The current handoff is [C2f affine texture mapping](../../research/jt/JT_TEXTURE_TRANSFORM_VALIDATION.md), with `jt_texture_transform_tests.cpp` included in `jt_tests`. Visual controls and glTF references are already authored under ignored `out/jt-research/texture-transform-ui-fixtures/`. Native test execution remains the owner's responsibility.

The combined C2d/C2e gate now expects visible textures. See the [current handoff](../../research/jt/JT_TEXTURE_DESCRIPTOR_VALIDATION.md#current-handoff-combined-c2dc2e-visible-image-mapping). The owner rebuilds and runs checks. `jt_texture_tests.cpp` prepares inline/external pixel equality, row orientation, UV association, missing-image fallback and compressed-texture scene-copy checks, alongside descriptor bounds/container tests. Source-derived visual controls remain ignored under `out/jt-research/texture-source-ui-fixtures/`; the older descriptor cubes intentionally exercise absent-UV fallback. No agent build/run or independent vendor-export pass is claimed.
## Feature gate C2j: advanced 9.x texture hierarchy

`jt9_texture_tests.cpp` extends original records to local versions 2/3, compatibility-stub validation, trailing coordinate selection, advanced type semantics, pixels/affine/generated coordinates, source metadata, truncated prefixes and resource/cancellation checks. `make_jt9_texture_controls.py --advanced` serializes separate enabled 9.0/9.5 controls, keeping generated assets ignored. [C2j validation](../../research/jt/JT_9X_ADVANCED_TEXTURE_VALIDATION.md) provides the exact regeneration command and owner build/UI gate. No authentic textured-export or full C2 completion claim is made.

## Feature gate C2k: unsigned 16-bit inline images

`jt_texture_pixel_tests.cpp` adds endian-distinct unsigned words, independently expected rounding boundaries, padding/alpha checks across enabled modern revisions and accepted basic/advanced 9.x texture envelopes. Existing unsupported numeric types must retain geometry with diagnostics. Authoring tools accept `--unsigned16`; generated controls remain ignored. [C2k validation](../../research/jt/JT_TEXTURE_UINT16_VALIDATION.md) supplies exact regeneration commands and the owner build/UI gate. The renderer's existing RGBA8 path intentionally reduces source precision.

## Feature gate C2l: float32 inline images

`jt_texture_pixel_tests.cpp` adds source-endian float components, independent IEEE boundary values, clamping, nearest-byte output, nonfinite colour/alpha fallback and existing numeric regressions. Authoring tools accept `--float32`, mutually exclusive with `--unsigned16`; outputs stay ignored. [C2l validation](../../research/jt/JT_TEXTURE_FLOAT_VALIDATION.md) provides the owner build/UI gate and exact regeneration commands. Float precision/HDR range are reduced in the established RGBA8 display path.

## Feature gate C2m: float16 inline images

`jt_texture_pixel_tests.cpp` adds binary16 components, independent half bit patterns, clamp/rounding/endianness checks, nonfinite colour/alpha fallback and earlier numeric regressions. Authoring tools accept mutually exclusive `--float16`/`--float32`/`--unsigned16` flags; all outputs stay ignored. [C2m validation](../../research/jt/JT_TEXTURE_HALF_VALIDATION.md) records exact regeneration commands and the owner build/UI gate. Source precision/HDR range are reduced in the existing RGBA8 display path.

## Feature gate C2n: unsigned 32-bit inline images

`jt_texture_pixel_tests.cpp` adds unsigned 32-bit word conversion, independently expected endpoints/rounding thresholds, endian/padding/alpha checks and earlier numeric regressions. Writers accept mutually exclusive `--unsigned32` alongside prior numeric flags; all generated outputs stay ignored. [C2n validation](../../research/jt/JT_TEXTURE_UINT32_VALIDATION.md) records exact regeneration commands and the 20-document owner visual gate. Full source precision is reduced in the existing RGBA8 display path.

Numeric/9.x visual controls now share content-addressed geometry, images, buffers and glTF references under ignored out/. Gate manifests map logical case names to shared physical files; see [shared texture layout](../../research/jt/JT_SHARED_TEXTURE_CONTROLS.md). Existing regeneration arguments remain valid.

## Feature gate C2o: signed integer inline images

`jt_texture_pixel_tests.cpp` adds signed 8/16/32-bit components, explicit sign-extension boundary patterns, historical OpenGL2 normalization and earlier numeric regressions across enabled modern and accepted 9.x image envelopes. Writers accept `--signed-bits 8`, `16` or `32`, mutually exclusive with other numeric flags. Shared references account for signed-byte quantization; no per-gate companion copies are created. [C2o validation](../../research/jt/JT_TEXTURE_SIGNED_VALIDATION.md) records primary references, exact regeneration commands and the 29-document owner visual gate. Source precision is reduced in the existing RGBA8 display path.

## Feature gate C2p: unsampled authored borders

`jt_texture_border_tests.cpp` adds known interior pixels inside transparent bright rings, unused constant-border environments, endian/numeric/family cases, alpha and geometry preservation, plus explicit fallback for sampled/inconsistent/empty/nonfinite border cases. `make_texture_border_controls.py` writes only positive UI controls through the shared asset store. [C2p validation](../../research/jt/JT_TEXTURE_BORDER_VALIDATION.md) records exact regeneration and the 13-document owner visual gate. Border sampling and authored mip preservation remain separate work.

## Feature gate C2q: sampled constant-colour borders

`jt_texture_constant_border_tests.cpp` adds independently known padded pixels and UVs across enabled modern/9.x envelopes, source endian, axis selection, alpha policy, external RGBA, unsupported-profile fallback and shared-image inherited border colours. `make_texture_constant_border_controls.py` writes positive JT comparisons and independent padded glTF references into the common store. [C2q validation](../../research/jt/JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md) records regeneration and the 12-document owner visual gate. No mipmapped border equivalence is claimed.

## Feature gate C2r: sampled explicit border texels

`explicitBorderTests()` in `jt_texture_border_tests.cpp` checks distinct authored ring pixels, mixed-axis cropping, UV mapping, alpha, numeric/endian/family cases, nonfinite fallback and inherited wrap-axis cache identity. The C2p sampled-ring negative now requests mipmap filtering. The existing border writer accepts `--explicit-ring` for 42 positive controls and three common glTF references. [C2r validation](../../research/jt/JT_TEXTURE_EXPLICIT_BORDER_VALIDATION.md) records regeneration and the 12-document owner visual gate.

## Feature gate C2s: sampled border colour layouts

`jt_texture_border_format_tests.cpp` checks ten explicit-ring colour layouts with independent literal component/colour pairs, byte/unsigned-16 components, source endian and enabled modern/9.x envelopes. RGB/BGR constant borders and external RGB check that zero Border A cannot introduce transparency without an alpha component. The C2q unsupported constant-format case now uses luminance. `make_texture_border_format_controls.py` authors positive comparisons using the common store. [C2s validation](../../research/jt/JT_TEXTURE_BORDER_FORMAT_VALIDATION.md) records regeneration and the 15-document owner gate.

## Feature gate C2t: constant luminance and alpha borders

`jt_texture_border_format_tests.cpp` adds known constant-border component/alpha assertions for luminance, luminance-alpha and alpha-only with enabled/disabled blending across enabled modern/9.x envelopes. C2q unsupported constant-format coverage now uses red-only storage. `make_texture_border_component_controls.py` authors original nested cubes and independent glTF references so transparency reveals another object. Shared serializers accept optional blend/diffuse and untextured-sibling inputs without changing defaults. [C2t validation](../../research/jt/JT_TEXTURE_BORDER_COMPONENT_VALIDATION.md) records regeneration and the 20-document owner gate.

## Feature gate C2u: inline RGB/RGBA Replace

`jt_texture_replace_tests.cpp` checks Replace RGB/A material factors, image pixels, blend policy, source endian/enabled families, vertex-colour fallback, unsupported combiners and distinct Replace/Modulate variants sharing an image. The legacy fixture optionally authors material alpha; texture fixture/writers optionally author combiner mode, preserving defaults. `make_texture_replace_controls.py` reuses original nested cubes and common resources. [C2u validation](../../research/jt/JT_TEXTURE_REPLACE_VALIDATION.md) records regeneration and the 17-document owner visual gate.

## Combined appearance gate C2v

`jt_texture_batch_tests.cpp` and `JtTextureStackFixture.h` add independent literal combiner/alpha vectors, linear-factor/sRGB preservation, sparse ordered units, per-unit inheritance/inhibit/final/forced/reset (including descendant final-reset fences), automatic non-displacement, role/channel validation, incompatible-stack rejection, unit caps, unchanged geometry, scene-copy and GLB checks. Modern field-final and legacy whole-final remain separate. Earlier C2u rejection cases retain incompatible filtering. The original writer `make_texture_batch_controls.py` reuses the common cubes/buffers and independently resolved expected recipes. [Combined validation](../../research/jt/JT_APPEARANCE_BATCH_VALIDATION.md) gives exact owner commands and a single 34-document positive visual session. No agent-native build/test execution or universal multi-texturing/renderer parity is claimed.

## Combined authored-mip gate C2x

`jt_texture_mip_tests.cpp` prepares enabled modern/9.x family and endian checks, row alignment 1/4/8, deliberate authored level colours, different tails with the same base, incomplete/overlong/short-row/border fallbacks, scene copy and PNG-in-GLB preservation. The original decoder retains bounded higher payloads; default one-level fixture bytes are unchanged. [C2x validation](../../research/jt/JT_AUTHORED_MIP_VALIDATION.md) records the internal carrier, limits, original controls and owner commands. Native execution is pending.

`jt_texture_mip_batch_tests.cpp` prepares C2y matching authored-level composition, both-axis constant/explicit borders, ignored rings, level-invariant UVs, composed rings, malformed/mismatched fallback and scene-copy/GLB checks across enabled modern/9.x families. Owner commands, original control authoring and pending viewer gate: [C2y validation](../../research/jt/JT_MIP_COMPOSITION_BORDER_VALIDATION.md). No agent native execution.


C2z extends `textureMipBatchTests()` with filtered affine single-stage Blend/RGB Decal, sampler preservation, all ten accepted colour layouts in authored rings, and retained nonlinear rejection. See [validation and owner commands](../../research/jt/JT_AFFINE_FILTER_VALIDATION.md). Native execution is pending.


C2aa reuses the shared C2z sources for application File > Export round trips; encoder regression coverage is prepared as `embedded_texture_image_tests` in the isolated render tests. Owner build and GUI integration checks remain pending; see [resident image export validation](../../research/jt/JT_RESIDENT_TEXTURE_EXPORT_VALIDATION.md). No native tests were executed by the agent.


C2ab extends jt_texture_replace_tests.cpp with vertex RGB/alpha replacement, blend-disable, missing-UV and unsupported-combiner fallback, and shared Replace/Modulate geometry checks. Prepared, not run by the agent. Original 48-root/19-document visual controls and exact generation command are in [C2ab validation](../../research/jt/JT_VERTEX_REPLACE_VALIDATION.md).


C2ac extends the existing Replace/batch native assertions for constant-RGB vertex combiners, component-separable Modulate/Replace stacks, wider/external Replace, differing-resolution nearest texel grids, expansion limits and common-grid copy/GLB preservation. Run with the existing owner native suite; no new target is required. Original grouped UI controls are authored with `python -B tools/jt/make_texture_combiner_batch_controls.py out/jt-research/texture-vertex-replace-ui-fixtures out/jt-research/texture-combiner-batch-ui-fixtures`. See [C2ac validation](../../research/jt/JT_COMBINER_UNIT_BATCH_VALIDATION.md) for limits and the grouped visual/export gate.


C2ad adds `textureBorderBatchTests()` to the existing owner-run native suite for mixed-axis authored profiles, generated anti-bleed/odd-edge averages, inline/external equivalence, bounded legacy-clamp area/UV invariants and carrier copy/GLB preservation. Shared render carrier tests add rectangular single-axis validation and max-LOD samples. Original UI authoring: `python -B tools/jt/make_texture_border_batch_controls.py out/jt-research/texture-combiner-batch-ui-fixtures out/jt-research/texture-border-batch-ui-fixtures`. See [Batch 2 validation](../../research/jt/JT_BORDER_BATCH_VALIDATION.md) for scope, commands and owner gate. Native execution remains unreported.


C2ae prepares `jt_texture_homogeneous_tests.cpp`: literal signed-Q/R contributions, exact face-constant proof, tiny-varying-Q rejection, generated STQ without UVs, UV2 padding, shared appearance separation, geometry/UV fallback preservation and copy/GLB round trips across enabled original texture layouts. These assertions are owner-run; authoring/static inventory is not native-suite execution. See [C2ae validation](../../research/jt/JT_HOMOGENEOUS_MAPPING_VALIDATION.md).


C2af registers `textureRoleTests()` for known-value tangent bases, transactional mirrored/degenerate/cancellation rejection, normal role/colour coexistence and fallback, scene copy, post-lit replacement and GLB unlit retention across enabled original texture layouts. Prepared only: owner native-suite execution is required. Original grouped controls: `python -B tools/jt/make_texture_role_controls.py out/jt-research/texture-roles-ui-fixtures`. See [C2af validation](../../research/jt/JT_TEXTURE_ROLE_VALIDATION.md) for the bounded material contracts and owner build/visual/export gate.


C2ag extends textureRoleTests with each normal mip filter, literal base/tail vector bytes and forced unused alpha, invalid-tail fallback, copy and representative GLB mip-carrier retention. Prepared, not executed by the agent. Original control authoring: `python -B tools/jt/make_texture_normal_mip_controls.py out/jt-research/texture-normal-mip-ui-fixtures`. See [C2ag validation](../../research/jt/JT_NORMAL_MIP_VALIDATION.md) for the pending owner gate and remaining basis/portability limits.
