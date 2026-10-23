# C2x: authored 2D mip chains

23 October 2026, `feature/JT-importer`, following accepted C2w `9d7c467`. **Owner build and raster mip/filter visual checks accepted; native and other backend checks remain unreported. The shared depth regression and subsequent alpha/mip/filter viewer retest are also owner-accepted.**

## Implemented scope

The original descriptor reader previously skipped levels above the base image. It now retains those bytes within the existing total-byte, count and cancellation limits. The scene adapter applies its existing numeric colour conversion and row orientation separately at each level, using the source row alignment and byte order. A qualified border-free 2D chain must halve each dimension, rounded down with a minimum of one, and end at 1-by-1. Incorrect level sizes, incomplete/overlong tails and unqualified border/composition cases retain geometry with a diagnostic rather than silently displaying only the base image.

Normalized display levels travel with the ordinary canonical PNG through the existing scene/material/image caches. Raster upload supplies authored levels instead of calling automatic mip generation for those images. CPU snapshots use those levels instead of building their box-filter chain; the existing GPU uploader already transports snapshot levels. CPU and GPU cache identities now include authored-level content. Ordinary images without authored levels retain generated mips. The raster upload also restores incoming unpack alignment after tightly packed row uploads.

This groups source retention, per-level validation, shared renderer transport and copy/export preparation, advancing C2 packages 2 and 7. Enabled JT revisions and the About version list do not change.

## Internal carrier and interoperability limits

`include/Materials/TextureMipChain.h` defines a versioned **ModelViewer-private** PNG text field, `ModelViewer.MipChain.v1`. Its JSON array contains base64 PNGs for levels 1 through N; the normal PNG pixels remain level zero. Each member is pixel-only, nested carriers are rejected, and the chain is validated before use. At most fifteen higher levels are allowed, with 64 MiB limits for decoded RGBA data including the base and for encoded text. The importer additionally retains its existing input/resource/memory/cancellation limits. Canonical PNG content hashing includes the chain, so the same base pixels with different higher levels remain distinct. Higher levels are not duplicated as separate embedded scene images.

This uses a self-contained internal image representation because the existing Assimp/material paths carry one image per binding and lack a standard authored-mip field. It is not a Siemens record, a glTF mip extension or a claim that another viewer will use these levels. Unaware PNG/glTF readers see the valid base image and may regenerate their own mips. Original higher numeric precision is normalized to the existing RGBA8 display precision; source byte preservation during decoding is distinct from lossless source-file export.

The prepared native suite checks scene copy and PNG carriage through GLB, not portable authored-mip behavior in other renderers. MVF save/reopen and every export format are not newly qualified by this gate. The existing approximate ray footprint and no-footprint fallback from [C2w](JT_TEXTURE_FILTER_PARITY_VALIDATION.md) remain. Inspect level transitions with anisotropy/denoising disabled when making controlled comparisons.

## Explicit boundaries

- Single supported pre-lit 2D Modulate and the existing direct RGB/RGBA Replace path can retain inline authored levels. Existing numeric converters are reused; the new native family/alignment matrix concentrates on unsigned-byte RGB levels, with a separate RGBA/alpha carrier/sampler check.
- Composed texture stacks, Decal/Blend/Add and wider composed Replace require separate authored-level composition qualification. The adapter diagnoses those combinations instead of dropping the chain.
- Sampled border mipmaps and explicit border rings remain unqualified. An ignored border colour on ordinary wrapping does not add sampled border texels. JT external images remain ordinary PNG/JPEG base images; the private mip carrier is not newly accepted as a JT external-image convention.
- Cube/volume images, other texture roles, camera-dependent/projective mapping, JT8 authored-texture layouts and authentic textured-export evidence remain separate gaps.

## Prepared native checks

`jt_texture_mip_tests.cpp` prepares modern enabled minors and 9.0/9.5 local versions 1/2/3 in both byte orders. Original RGB records use row alignment 1/4/8, deliberately green/yellow higher levels beneath a red base, alternate tails including two bindings in one shared-geometry scene, direct RGB Replace, incomplete/overlong/short-row/border fallbacks, scene copy and GLB checks. The fixture's default one-level serialization remains unchanged. `texture_mip_chain_tests.cpp` checks PNG/format-conversion preservation, independent literal level colours/alpha with the production CPU sampler, and malformed carriers. GPU upload receives the same snapshot chain; runtime GPU validation remains an owner check.

Owner commands from the configured developer environment:

```powershell
cmake --build out/jt-selection-build --target jt_tests
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
$env:PATH = "$jtPrefix/bin;$env:PATH"
./out/jt-selection-build/jt_tests.exe *> out/jt-research/c2x-jt-native.log
cmake -S tests/render -B out/jt-filter-build -G Ninja "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/jt-filter-build --target texture_mip_chain_tests rt_texture_filter_tests
./out/jt-filter-build/texture_mip_chain_tests.exe *> out/jt-research/c2x-mip-native.log
./out/jt-filter-build/rt_texture_filter_tests.exe *> out/jt-research/c2x-filter-regression.log
python tests/jt/check_provenance.py
```

Existing standalone JT configuration instructions are in [descriptor validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls). The agent has not executed a build or native suite. Authoring/static inspection checked Python syntax, thirty control hashes, five private carriers and their complete dimensions/resource paths, sixteen unique launch paths, the provenance JSON and changed document links. Regenerating the accepted original C2v/C2w controls produced the same 110/36 hashes after the writer extension. A successful viewer check does not establish native execution or independent CPU/GPU coverage.

## Original controls and one owner session

With the accepted common controls available, regenerate:

```powershell
python -B tools/jt/make_texture_mip_controls.py out/jt-research/texture-replace-ui-fixtures out/jt-research/texture-mip-ui-fixtures
```

Fresh prerequisites are in [C2v regeneration](JT_APPEARANCE_BATCH_VALIDATION.md#original-authoring-and-shared-resources) and [C2w regeneration](JT_TEXTURE_FILTER_PARITY_VALIDATION.md#original-controls-and-one-visual-session). Original cube geometry/buffers and the inner companion use the common content-addressed folder. A white outer appearance needs one distinct companion; it is shared by all new cases. Generated roots, payloads and reports stay under ignored `out/`.

The writer produces thirty positive JT roots for five cases, with originally serialized 10.0 and 9.0/9.5 envelopes in both byte orders. Nine levels range from 256-by-256 through 1-by-1. The base is blue; successive authored levels are green, yellow, cyan, magenta, orange, white, green and yellow. Deliberately different level colours make accidental regeneration obvious. The variant has the same blue base with pink higher levels. The alpha case introduces partial alpha only in higher levels.

Five glTF comparison documents use a separately authored Python/Pillow PNG carrier with the documented private schema. They compare JT decoding/transport against the common image path; they are not standard glTF authored-mip conformance evidence or an independent renderer oracle. Literal native pixels provide a separate expectation. These controls are original diagnostic data, not authentic JT exports or reheadered sample evidence.

`out/jt-research/texture-mip-ui-fixtures/authoring.json` records hashes, references and dimensions. `out/jt-research/c2x-visual-files.json` lists sixteen positive documents for one launch: five modern JT cases, five references, two representative legacy cases and two accepted C2w controls with their references. No failure cases are opened.

| Case suffix after `texture-mip_` | Owner check in normal and Ray Traced rendering |
| --- | --- |
| `authored_nearest_10_0_le.jt` | Zoom close for blue, then zoom out. Distinct authored colours should appear as different levels become active. The matching reference follows the same sequence. |
| `authored_linear_10_0_le.jt` | The same level sequence blends more gradually between colours rather than switching abruptly. Exact raster/ray transition distances need not coincide. |
| `generated_control_10_0_le.jt` | Remains blue when zoomed out, because its only source image is uniformly blue. It must differ from the authored case at smaller screen sizes. |
| `authored_variant_10_0_le.jt` | Same blue close-up, pink higher levels. Opening it with the other cases must not make their mip colours change through cache reuse. |
| `authored_alpha_10_0_le.jt` | Close-up blue is opaque. At coarser levels, partial transparency should reveal the red inner cube. Compare its reference. |

`authored_nearest_9_0_le.jt` and `authored_alpha_9_5_be.jt` match their modern cases. The C2w nearest/linear checker regressions retain their accepted sharp/smooth distinction. Report which ray-tracing backends were checked and whether the native targets ran. Stop for owner confirmation before committing or dependent implementation.

## Sources and provenance

Consulted retained Siemens JT10.0 Rev-B section 6.1.2.3.1/figure 54, JT9.5 Rev-A inline image definitions, and retained public OpenGL2.1 sections 3.8.8/3.8.10 for mip dimensions/completeness. The existing decoded field order is unchanged; this pass changes retained payload handling. Primary JT10 reference: [Siemens PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), retained SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. JT9.5 retained SHA-256 `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`; a newly verified publisher URL is not available. [Official Khronos mirror](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/gl/glspec21.pdf), retained SHA-256 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`.

Existing original descriptor/adapter, material/image/cache, raster/CPU/OptiX mip transport and original fixture code were inspected. Qt's installed header was checked for its strict base64 API. No new download, vendor software, SDK/schema, disassembly, third-party implementation or asset, or licence interpretation was introduced. Two existing ModelViewer-original files in the decoder folder change; upstream-derived source/hashes/notices and decoder file counts are unchanged. The provenance manifest and modification note record this continuation. All new level pixels, carrier serialization and controls are original; retained vendor documents and generated artifacts remain local. This records method and limits, not legal clearance.

## Owner build correction - 23 October 2026

The first owner build reported MSVC C2144/C4430 at the viewport's initial includes. Source-byte inspection found that prepending the new include had displaced the existing UTF-8 marker into the file body. The marker was restored to byte zero; the included header was not the cause. Changed C++ files were inspected for other displaced markers, with none remaining. The owner subsequently rebuilt successfully. No build/native execution by the agent.

## Owner observations - 23 October 2026

The owner confirmed nearest transitions from blue to green and then yellow at small projected sizes; linear passes through light blue before green/yellow. The generated control stays blue. The variant reaches magenta; the alpha case reaches green, yellow and cyan. These are the intended authored-level distinctions. The owner also reported the nearest/linear checker distinction. Close-up blue is expected and does not indicate a missing authored chain. No control geometry or UV scaling was changed in response.

This acceptance records the reported raster observations, not native-suite, CPU/OptiX, round-trip or every enabled revision qualification. A separate PBR-only GLB transparency regression was reported against earlier commit `3523fcf406560def89b6f0446ae8656d7ba7fc03`. See [coverage correction](JT_BLEND_COVERAGE_REGRESSION.md). Hold the pending changes for owner retest before commit/push or dependent implementation.

## Final combined viewer acceptance - 23 October 2026

After rebuilding the narrower depth correction, the owner confirmed the local GLB regression fixed and subsequently reported no regressions in the reopened 21-document JT alpha/mip/filter session. C2x viewer acceptance and the commit hold are closed on that basis. The earlier hold text records the investigation sequence, not the current status. Native execution, CPU/OptiX backend-specific coverage and authentic authored-texture export comparison remain unreported or unqualified as above.
