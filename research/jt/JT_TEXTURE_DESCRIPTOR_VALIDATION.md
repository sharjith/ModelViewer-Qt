# Texture descriptor foundation — Gate C2d

## Current handoff: combined C2d/C2e visible image mapping

Update, 22 October 2026: committed/pushed as `a2cb18e`; the owner explicitly reported that the UV correction was tested and correct. This confirms the visual fix, not optional portable-suite execution or authentic textured-export conformance. The next owner handoff is [C2f affine texture matrices](JT_TEXTURE_TRANSFORM_VALIDATION.md). The text below records the earlier implementation and testing sequence.

Owner disposition, 21 October 2026: the owner explicitly requested commit and push after the UV correction. Visible image rendering was previously confirmed; a separate corrected-orientation result and portable-suite execution were not explicitly reported. Commit authorization is recorded without inferring those test results. Authentic textured-export qualification remains open.

21 October 2026, `feature/JT-importer`. This section supersedes the descriptor-only instructions below. After the owner observed untextured controls, they requested actual image mapping before visual acceptance. Implementation is prepared for owner build/test; the agent has not built or executed the C++ changes. No test pass or authentic-export agreement is claimed.

The decoder now retains bounded base-mip bytes and the owning JT filename. The scene adapter accumulates the supported channel-0 texture fields, resolves relative PNG/JPEG resources within the assembly directory, or decodes borderless unsigned-byte RGB/RGBA/BGR/BGRA inline base images. Supported mapping is pre-lit 2D MODULATE with stored UV0–3, identity texture transform and no coordinate generation. Wrap/filter settings are transported; the renderer regenerates mipmaps rather than preserving authored higher mip levels. Unsupported mapping modes or missing resources warn and retain geometry. Legacy texture record layouts, multiple texture units, arbitrary transforms/generation, cube maps and additional pixel formats remain open.

Decoded images are normalized to bounded embedded PNG resources in the Assimp scene, with content deduplication and a 4096-resource cap. Input and decoded image sizes are capped at 64 MiB; import memory accounting and cancellation remain active. Resource caches may retain bounded unused images from rejected alternatives. The generic material adapter now consumes embedded scene images, keeps decoded pixels available to deferred GPU upload and ray tracing, and carries UV/sampler settings. JT and Assimp-imported glTF use the same renderer V conversion without a JT-specific reflection or modification of stored scene UVs. Scene deep-copy now copies compressed texture byte counts correctly. These generic adapter/copy changes close existing gaps exposed by JT image import.

Rebuild the viewer, then use the existing files under ignored `out/jt-research/texture-source-ui-fixtures/`; no regeneration is required:

1. Open `reference.gltf` as the visual reference.
2. Open `texture_external_le.jt`, `texture_external_be.jt`, `texture_inline_le.jt` and `texture_inline_be.jt`, one at a time. Keep all companions beside them.
3. Each should show the image over the quad, with the same orientation and coverage as the reference, one mesh, four vertices and two triangles. Lighting brightness can differ; mirrored, upside-down, absent or differently tiled patterns are failures.
4. Temporarily move only `source_image.png` aside and reopen an inline control: its texture should remain visible. The external control should still load geometry with a missing-image warning. Restore the PNG afterward.
5. Reopen one previously accepted ordinary JT assembly and a textured glTF as regression checks.

The existing portable-suite commands below now also cover original inline/external pixel equivalence and row orientation, all enabled modern minors in both byte orders, UV/material association, unchanged stored UVs, missing-image fallback and compressed-image scene copying. These are prepared tests, not reported execution results. The old descriptor cube controls still exercise missing-UV fallback and are not the visible texture acceptance controls. Stop for owner results before committing. No version-support or About claim is expanded.

Provenance for this continuation: existing application material/shader/image upload and scene-copy code, the retained public JT reference already cited below, and the owner-authorized GTEX01 controls. No new vendor SDK, schema, proprietary implementation or dependency was introduced; existing Qt image support is reused. Authentic textured JT export comparison remains outstanding. This describes engineering method, not legal clearance.

## Historical descriptor-only preparation

### Owner image report and V correction — 21 October 2026

The owner rebuilt and confirmed images are visible, but reported a vertical reflection relative to the glTF reference. The corner labels were upside down; changing world up-axis cannot account for that surface mapping difference. Inspection found the JT-specific material compensation cancelled the shader's necessary V conversion. The fixture's source-to-JT UV conversion was already consistent with Assimp's glTF importer. Removed the JT-only hint and runtime scale/offset override, retaining the existing raw-row conversion, canonical image rows and stored UVs. No fixture regeneration or geometry/up-axis change is needed. Rebuild and reopen the same four controls, checking corner labels against the reference; this correction awaits owner confirmation.

For this diagnosis, inspected the existing installed dependency's Assimp v6.0.4 `code/AssetLib/glTF2/glTF2Importer.cpp` UV extraction (SHA-256 `6d64e77c31c22f8a992946e22401a18c36f10d7a12952caca5c25d5402055b74`; [upstream version](https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/glTF2/glTF2Importer.cpp)). It converts source V before scene output. No dependency code was copied or modified, and existing Assimp notices/licence terms remain unchanged. Prepared original in-memory glTF UV-convention and identity JT material checks accompany the earlier pixel checks; the agent has not built/run them. This records the consulted source and diagnosis, not independent vendor-export conformance or legal clearance.

21 October 2026, `feature/JT-importer`. **Prepared for owner build/portable checks; not built or run by the agent.** C2c ray-traced alpha was owner-accepted and pushed as `8f416ca`. This is a descriptor/diagnostic foundation, not texture-rendering support or a new enabled JT revision.

## Why this gate precedes image rendering

Earlier UV controls proved coordinate transport using manually assigned images. They did not import images, texture environment, generation modes or resource references from JT. Unknown unwrapped texture attributes were skipped; unknown attributes in modern Sabot wrappers could reject the graph. The new original attribute class registers the texture-image GUID, enabling the existing bounded wrapper path to recognize it.

For modern local-version-1 records, the reader retains texture/coordinate channels, environment enums, colours, the row-major transform, coordinate-generation modes/planes, external names, inline image format/dimensions/alignment/shared flag and mip byte counts. Raw texels are skipped in bounded chunks with cancellation checks; they are **not decoded, retained in the output scene or rendered**. External image references are preserved as text only; no file is opened, resolved or fetched. Node metadata under `JTTextures` retains descriptors in source attribute order, including palette/inhibit/final flags. It describes source records, not accumulated effective texture state. Image mapping, channel-by-channel inheritance, separator/reset semantics and material/mesh cache identity belong to the next rendering gate.

Active image descriptors produce an explicit unsupported-rendering warning while stored geometry retains its existing appearance. Ignored attributes are recorded without an active-rendering warning. None/reset descriptors do not imply missing image rendering. Legacy 8.x/9.x texture payload layouts remain unqualified: after the common base header the envelope skips their payload, and scene output identifies the descriptor as unparsed with a warning. Modern unknown local versions, malformed fields and inconsistent/truncated lengths reject explicitly. This does not yet provide recovery from every unsupported resource version.

Bounds include image count 64, mip count 16, positive inline dimensions, supported enum ranges, finite float parameters, nonnegative lengths, exact aggregate mip bytes and a 64 MiB declared inline byte cap per attribute. Existing element envelopes, recursion/resource limits and cancellation still apply. Metadata allocation is charged conservatively to the cumulative import budget. External names exceeding Assimp's fixed metadata string capacity are explicitly marked omitted and warned about rather than silently truncated. Empty external mip collections and empty image lists use non-null empty metadata objects because `aiMetadata::Alloc(0)` returns null.

## Reference, provenance and interpretation limits

Consulted the locally retained Siemens **JT 10.0 File Format Reference, Revision B**, section 6.1.2.3 and figures 49–54, plus compound matrix definitions: [publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. The PDF skill was used for read-only diagram inspection. Text extraction interleaves diagram labels: visual inspection places environment and coordinate-generation collections before texture/coordinate channels, empty field and image storage; inline image format precedes total bytes and mip blocks. Figure 50 contains an overlapping version label near the coordinate-channel field. The implementation follows the I32 coordinate-channel description/flow position; authentic texture-export confirmation remains required. No diagram or substantial specification text is redistributed.

Existing ModelViewer readers at `8f416ca` supply bounded element envelopes, base-version/Sabot handling and metadata conventions. The new `.hxx/.cxx` files are original GPLv3-only additions inside the established decoder directories. Their provenance entries and modification summary are added; upstream-derived sources and their hashes/notices are unchanged. The manifest now has 104 upstream-derived and 17 original decoder files. Existing dependency/licence terms are unchanged. No vendor application, new SDK/schema/package, binary disassembly or third-party implementation was consulted. This records method, not legal clearance. See [provenance](JT_PROVENANCE.md), [notices](../../THIRD_PARTY_NOTICES.md) and the [handout](../../RESEARCH_FOLDER_HANDOUT.md).

No authentic textured JT export is currently qualified. The owner has none available and explicitly suggested authoring a synthetic JT from the local glTF sample collection. That fallback is now prepared; lack of a vendor export remains a qualification limit, not a blocker to initial synthetic image mapping. Original serialized fixtures are not independent vendor-conformance evidence. Modern original record coverage is prepared for all seven enabled 10.x revisions (10.0, 10.3, 10.5, 10.7, 10.8, 10.9, 10.10); headers do not establish that every later revision's texture layouts match the 10.0 diagram. Enabled legacy families retain explicit unqualified status. No support-version list or About text changes.

## Prepared checks and controls

`jt_texture_tests.cpp` prepares direct descriptor checks, base versions 1/2, both byte orders, wrapped/unwrapped containers, node metadata and unchanged geometry. It checks every truncated prefix, unknown local version, invalid storage/dimensions, negative/inconsistent mip bytes, cumulative budget and cancellation. Existing portable tests remain in the same executable. No parser/native pass is claimed: the agent ran read-only reference/code inspection, JSON parsing and `git diff --check`. At the owner's subsequent request, the original Python control authoring tool was executed on a local glTF asset; it did not build or execute the new C++ reader.

The owner builds the viewer as usual. To run the isolated suite and generate the original controls from an MSVC developer shell:

```powershell
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
cmake -S tests/jt -B out/jt-selection-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMV_JT_SANITIZE=ON "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/jt-selection-build --target jt_tests
$jtCompilerBin = Split-Path (Get-Command cl.exe).Source
$env:PATH = "$jtPrefix/bin;$jtCompilerBin;$env:PATH"
New-Item -ItemType Directory -Force out/jt-research | Out-Null
./out/jt-selection-build/jt_tests.exe --texture-fixtures out/jt-research/texture-descriptor-ui-fixtures *> out/jt-research/c2d-texture-validation.log
python tests/jt/check_provenance.py
```

Generation is only attempted after the portable suite succeeds. If a command fails, stop and report the output; no generated controls from this pass are claimed as already available. The option emits 28 descriptor assemblies and one original shared `geometry.jt`. Keep that geometry file beside the assemblies. External reference name `original_texture.png` deliberately need not exist because this gate must not open it. Original inline texels are a 2×2 RGB pattern; no source geometry or third-party image is used. Generated JT files, screenshots, logs and build output remain ignored under `out/`; research contains documents only.

## Owner acceptance gate

After the isolated checks succeed, open these generated files:

| File | Expected result |
| --- | --- |
| `texture_10_0_le_inline.jt` | Two opaque nested cubes, 48 vertices and 24 triangles total; texture-rendering warning, no image painted on cubes |
| `texture_10_0_le_external.jt` | Same geometry/appearance and explicit warning despite missing external image; no resource-opening error |
| `texture_10_10_le_inline.jt` | Same result through the modern wrapped/base-v2 container |
| `texture_10_10_be_external.jt` | Same result in the opposite byte order |

The portable checks inspect descriptor metadata; unchanged cube appearance is intentional. Reopen an ordinary accepted legacy and modern assembly as a basic regression. Node metadata/diagnostics do not imply effective texture inheritance or image rendering. Stop for owner results before committing C2d. The next image-rendering gate can use the owner-authorized glTF-derived controls below for bounded pixel/resource decoding and an explicit supported mapping into materials/UVs; agreement with an authentic textured JT export remains an open qualification item. Remaining mesh transports, other C2 features and C3–C7 stay open.

## Owner-authorized glTF-derived control — GTEX01

The owner supplied a local glTF sample collection and requested synthetic texture authoring after confirming no textured JT export is available. Selected source `GTEX01` is a small opaque textured quad from the [Khronos glTF sample-assets repository](https://github.com/KhronosGroup/glTF-Sample-Assets), local checkout `92160912f77d4204e5f4712d877d8594b70f93b8`. Its local asset licence/metadata identifies CC0-1.0, creator Marco Hutter, year 2017. This records source attribution, not a new legal opinion; source paths and asset identity remain in the ignored authoring manifest, with a copied source licence. No downloaded model name or local source path is published in tracked research.

Source glTF SHA-256 is `f7798eaeb74f48870776edfbf0284fc8e24865826a822c7e9e0b9743464e9efd`; original image SHA-256 is `f5ba45490d9a62c2d9112143ba49bff49e25567a51a3523d1c8f7b0ad63cf6f4`. Source-derived output stays in ignored `out/jt-research/texture-source-ui-fixtures/`. It contains `reference.gltf` and its buffer/image companions, one shared `geometry.jt`, and four `texture_<inline|external>_<le|be>.jt` roots. Each control has one mesh with four source vertices and two triangles, a 256×256 opaque image, authored wrap/filter values and UV set 0. This is a mixed 10.0 texture container / 8.2 geometry control; it is not an authentic export in either family.

The original fixture writer `tools/jt/make_texture_control.py` is intentionally limited to one identity-placed triangle primitive, white base factor, one opaque image and UV0. It reads accessor offsets/strides, reconstructs source positions/UVs with legacy quantization, converts V to the declared JT/OpenGL convention and writes bottom-up inline RGB rows; the glTF reference retains original UVs. Source normals are not translated by this small writer; JT normals are generated from its triangles. This is not a general converter and does not claim complete glTF material translation. It requires Pillow, already available locally, without adding an application dependency. Unsupported structures reject rather than silently flattening unrelated content.

Owner-authorized authoring ran successfully and produced the four JT roots plus companions. Writer-side numerical checks cover reconstructed coordinates and UV error bounds; no JT reader, C++ build or rendering execution occurred. Texture orientation, raw-row and external-image agreement must be established against the copied glTF reference in the subsequent image-mapping gate.

Exact regeneration command (use the local source matching GTEX01's hash):

```powershell
python tools/jt/make_texture_control.py "<local-gltf>/sample-GTEX01.gltf" out/jt-research/texture-source-ui-fixtures
Get-FileHash out/jt-research/texture-source-ui-fixtures/texture_external_le.jt,out/jt-research/texture-source-ui-fixtures/texture_inline_le.jt -Algorithm SHA256
```

Authoring evidence is `out/jt-research/texture-source-ui-fixtures/authoring.json`, regenerated by that command. Measured root hashes: external LE `9e1a57fd1f00b2ed86312c1e1cf775a75d58907340f102213a3b9c323ba49cf0`; inline LE `1117d9d9525f60769a32a001c539bcb8084e579d3a7058e0c2b7671a02056f65`.

These four files are available immediately for the C2d owner build check. Keep every companion beside them. For now they should load the same **untextured** quad and warn about unsupported image rendering. `reference.gltf` shows the intended texture. The next gate will make the JT image visible and compare orientation/tiling against that reference. Do not call these synthetic controls independent vendor-export validation.

## Owner build report and correction — 21 October 2026

The first owner build rejected the new test helper name `Model`, which is hidden inside the reader subclass by inherited `JtData_Reader::Model()`, and the adapter's use of undeclared `MAXLEN`. The helper is renamed `TextureTestModel`, and the metadata-name bound now uses the installed Assimp's `AI_MAXLEN`, matching the existing property path. The remaining reported test parser errors followed the invalid initializer. The source fixes pass `git diff --check`; the agent did not rebuild. C2d remains pending owner rebuild/test confirmation and uncommitted.
