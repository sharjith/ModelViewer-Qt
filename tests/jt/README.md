# JT importer tests and fixtures

Configure this folder as a standalone CMake project against ModelViewer's OCCT, Qt, Assimp, zlib and LibLZMA dependency prefix. Set `MV_JT_SANITIZE=ON` for MSVC AddressSanitizer instrumentation. Run `jt_tests` for the portable synthetic suite or `jt_tests <sample-folder>` to include the five local part regressions. Make the dependency DLLs and, for sanitizer builds, the MSVC sanitizer runtime available on `PATH`.

`JtContainerFixture.h` and `jt_scene_tests.cpp` contain original generated containers and synthetic geometry. They are part of the repository and do not require downloaded CAD data for the portable suite.

To reproduce the viewer acceptance fixtures, supply a locally obtained `Bolt.jt`:

```text
python tests/jt/generate_display_fixtures.py --samples out/jt-research/samples --output out/jt-research/selection-fixtures
```

The generator copies that supplied sample into the chosen output folder. It does not download or grant redistribution rights to the sample. Keep the generated `parts/` folder with the fixtures. Their generated README lists expected colors, placement, mesh counts and errors. The optional source samples and generated exports remain outside version control.

`jt_probe input.jt [output.glb]` inspects an import and optionally exports it. Add `--eye x,y,z` to exercise import-time RangeLOD distance selection; this does not enable runtime switching in the viewer.

Add `--properties` to print supported source node properties as JSON. Generate the metadata acceptance files with:

```text
jt_fixturegen --samples out/jt-research/samples --output out/jt-research/metadata-fixtures
```

This generator also references a locally supplied Bolt sample. It shares the original container serializer used by the portable tests. See [metadata validation](../../docs/JT_METADATA_VALIDATION.md) for the representation, qualified local versions, expected values and UI/export limitations.

Coverage, sample provenance and UI acceptance are recorded in [native importer](../../docs/JT_NATIVE_IMPORTER.md), [assembly validation](../../docs/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md) and [display selection validation](../../docs/JT_DISPLAY_SELECTION_VALIDATION.md).

Run `python tests/jt/check_provenance.py` to check decoder manifest coverage and retained source notices. This checks record consistency; it does not establish legal clearance or fulfil binary source-delivery obligations by itself.
