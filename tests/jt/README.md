# JT importer tests and fixtures

Configure this folder as a standalone CMake project against ModelViewer's OCCT, Qt, Assimp, zlib and LibLZMA dependency prefix. Set `MV_JT_SANITIZE=ON` for MSVC AddressSanitizer instrumentation. Run `jt_tests` for the portable synthetic suite or `jt_tests <sample-folder>` to include the five local part regressions. Make the dependency DLLs and, for sanitizer builds, the MSVC sanitizer runtime available on `PATH`.

`JtContainerFixture.h` and `jt_scene_tests.cpp` contain original generated containers and synthetic geometry. They are part of the repository and do not require downloaded CAD data for the portable suite.

`JtLegacyFixture.h` also exercises the shared 8.0–8.2 lossless family with LE/BE, raw/zlib vertices, codec/binding rejection and the version-specific sample L81 EOF exception. See [8.x validation](../../docs/JT_8X_FAMILY_VALIDATION.md) for the authentic 8.0 success and expected 8.2 quantized sample Q82 rejection. Recognizing a family does not claim all its geometry encodings are implemented.

The later [quantized follow-up](../../docs/JT_LEGACY_QUANTIZED_VALIDATION.md) supersedes that original sample Q82 rejection. `jt_quantized_tests.cpp` adds independently calculable positions/normals/UVs/constant colours, arithmetic/OOB/multiple-context tests, malformed-input/resource checks and application/GLB attribute preservation. Use `jt_tests --quantized-fixtures out/jt-research/quantized-ui-fixtures` to generate the original `legacy_quantized_le.jt` and `legacy_quantized_be.jt` owner acceptance files. Each contains one mesh, seven vertices and three triangles. No downloaded geometry is included in these generated fixtures.

To reproduce the viewer acceptance fixtures, supply a locally obtained `Bolt.jt`:

```text
python tests/jt/generate_display_fixtures.py --samples out/jt-research/samples --output out/jt-research/selection-fixtures
```

The generator copies that supplied sample into the chosen output folder. It does not download or grant redistribution rights to the sample. Keep the generated `parts/` folder with the fixtures. Their generated README lists expected colors, placement, mesh counts and errors. The optional source samples and generated exports remain outside version control.

`jt_probe input.jt [output.glb]` inspects an import and optionally exports it. Add `--eye x,y,z` to exercise import-time RangeLOD distance selection; this does not enable runtime switching in the viewer.

Add `--validate` to check finite positions/local transforms, normalized normals, triangle indices and scene mesh/material references, and print local mesh-resource bounds and occurrence counts. The [JT 10.3 sample A103 validation](../../docs/JT_103_ASSEMBLY_VALIDATION.md) records the large local assembly check. Portable tests also cover original LE/BE 10.3 graph layouts and configurable cumulative allocation-budget boundaries; downloaded geometry stays outside the repository.

Add `--properties` to print supported source node properties as JSON. Generate the metadata acceptance files with:

```text
jt_fixturegen --samples out/jt-research/samples --output out/jt-research/metadata-fixtures
```

This generator also references a locally supplied Bolt sample. It shares the original container serializer used by the portable tests. See [metadata validation](../../docs/JT_METADATA_VALIDATION.md) for the representation, qualified local versions, expected values and UI/export limitations.

Use `jt_fixturegen --kind draw-style --samples <sample-folder> --output <output-folder>` for the draw-style fixture set. `jt_probe input.jt --materials` prints resolved style/culling/unlit fields. See [draw-style validation](../../docs/JT_DRAW_STYLE_VALIDATION.md) for expected counts and renderer limitations.

The [wireframe follow-up](../../docs/JT_WIREFRAME_VALIDATION.md) adds mixed red-wire/green-solid fixtures and optional native OpenGL checks under `tests/render`. It also verifies material runtime JSON persistence. Refer to that follow-up for the current `draw_wire_hint.jt` expectation.

The [shininess follow-up](../../docs/JT_SHININESS_VALIDATION.md) tests inherited Phong exponents, their approximate PBR roughness, material-cache separation, scene copying and GLB roughness persistence. Use `jt_probe <file.jt> --materials` to inspect both fields.

The [vertex-colour follow-up](../../docs/JT_VERTEX_COLOUR_VALIDATION.md) covers lossless RGB/opaque-RGBA arrays, colour seams and material overrides. `jt_color_fixturegen <local Bolt.jt> <output-folder>` produces native viewer fixtures from locally supplied geometry and checks their import. The test-only modifier is deliberately restricted to that little-endian JT 10.0 sample layout; it is not a general JT writer or independent conformance oracle.

The [JT 8.1 follow-up](../../docs/JT_81_VALIDATION.md) adds original full-file legacy fixtures (`JtLegacyFixture.h`) and `jt_legacy_tests.cpp`. Run `jt_tests <JT-10-sample-folder> <local sample-L81.jt>` for both corpora, or `jt_tests --legacy-fixtures <output-folder>` to generate two small blue legacy models for the UI. Downloaded sample L81 geometry remains external to the repository.

Coverage, sample provenance and UI acceptance are recorded in [native importer](../../docs/JT_NATIVE_IMPORTER.md), [assembly validation](../../docs/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md) and [display selection validation](../../docs/JT_DISPLAY_SELECTION_VALIDATION.md).

Run `python tests/jt/check_provenance.py` to check decoder manifest coverage and retained source notices. This checks record consistency; it does not establish legal clearance or fulfil binary source-delivery obligations by itself.

The [modern attribute foundations](../../docs/JT_MODERN_ATTRIBUTE_VALIDATION.md) add original version-2/Sabot envelope and identity tests in `jt_modern_attribute_tests.cpp`. Generate geometry-independent Gate 2a viewer files with `jt_tests --modern-attribute-fixtures out/jt-research/modern-attribute-ui-fixtures`. Palette maps and nonfallback palette-scoped rendering remain explicit unsupported boundaries at this gate; no additional JT version is accepted.

The [10.9 palette follow-up](../../docs/JT_109_PALETTE_VALIDATION.md) adds `jt_palette_tests.cpp`: independent state accumulation, fallback/inhibition, material splits, transforms, shared resources, raw/nested arithmetic escapes and malformed packet/tail checks. `jt_tests --palette-fixtures out/jt-research/palette-ui-fixtures` exports original three-colour triangle geometry as GLB for scene/material checks. `jt_probe --allocation-budget-mib <MiB>` overrides the cumulative charged-allocation budget for native diagnostics; the current default is 6 GiB and the per-allocation guard remains 256 MiB. Gate 2a's earlier unsupported-palette fixture description is historical and superseded by this follow-up.
