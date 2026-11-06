# C2w: shared ray-traced sampler filtering

23 October 2026, `feature/JT-importer`, following owner-accepted C2v `a8e5664` and documentation separation `3d1da33`. **Owner built and reported tests passed on 23 October 2026. Native-suite execution and CPU/GPU backend coverage were not separately identified.**

## Scope and purpose

C2v exposed an existing renderer limitation: imported sampler settings reach raster rendering, but ray-traced snapshots omit minification/magnification settings and both ray tracers always use bilinear/trilinear filtering. This particularly affects C2v's composed images, whose nonlinear colour operations were qualified with nearest sampling. The correction is shared material infrastructure, applicable to JT and other imports. It does not change JT decoding or add an enabled version.

The original implementation carries raw filter enums from all three existing texture-resolution tiers into `RtTextureSample`, snapshot deduplication, GPU upload/cache identity and `RtOptixTexture`. Distinct uses of the same image with different filters no longer share an incorrectly identical sampler snapshot. A small host/device selector is used by CPU and OptiX: base-level nearest/linear, nearest or linear lookup in the selected mip, and nearest or blended mip selection. Unknown filters retain a linear/trilinear fallback. Mip selection saturates before integer conversion. The CPU sampler is factored into a Qt/GL-free header so the native regression exercises the production lookup directly. GPU kernel rebuild dependencies include the selector header.

Existing UV channel selection, single vertical flip, scale/rotation/offset, wraps, alpha-byte interpolation, colour decode and material evaluation remain in their existing order. Existing bilinear sampling code was moved rather than replaced with a new colour pipeline. Ordinary default linear/trilinear materials retain those defaults.

## Explicit limits

- The ray-cone footprint estimate remains approximate and nonnegative. Zero or unavailable footprint uses the magnification filter at level zero. This is not exact raster-derivative/anisotropic parity or full OpenGL magnification-transition emulation. Shadow/opacity/secondary calls without footprints still use base-level samples.
- The existing generated, byte-domain box-filter mip pyramid is unchanged. Authored JT mip payload preservation, authored border mip semantics, arbitrary independent texture units and authentic textured-export qualification remain open. No new importer mip payload support is claimed.
- Composed C2v nearest images remain nearest in ray tracing, but differences in lighting, antialiasing, denoising and transparency integration prevent a claim of pixel-identical complete raster/CPU/GPU frames. Denoising can soften visible boundaries; inspect with it disabled for filter comparison.
- CPU native and OptiX execution have not been performed by the agent. If OptiX is unavailable, report that separately rather than treating CPU success as GPU evidence.

## Prepared native check

`tests/render/rt_texture_filter_tests.cpp` invokes the production CPU sampler with independent literal pixels and expected values. It covers both magnification filters, all six minification filters, mip midpoint/saturation, absent mips, non-mip filters ignoring an available pyramid, invalid footprint fallback, alpha interpolation, channel/transform selection, negative and endpoint wraps, vertical orientation and absent images. The same host/device level-selection code is compiled into CUDA; GPU pixel execution still requires owner testing. Cache/transport changes were inspected, not represented as native runtime coverage.

Owner commands in the normal developer environment (logs/build products stay ignored):

```powershell
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
cmake -S tests/render -B out/jt-filter-build -G Ninja "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/jt-filter-build --target rt_texture_filter_tests
./out/jt-filter-build/rt_texture_filter_tests.exe *> out/jt-research/c2w-native-validation.log
```

This target uses GLM headers and does not require Embree, an OpenGL context or CUDA execution. The standalone render project also discovers Qt for its existing targets. The owner builds the viewer normally; OptiX kernel/device-header layout changes must be rebuilt together.

## Original controls and one visual session

Regenerate after the accepted C2v controls exist:

```powershell
python -B tools/jt/make_texture_filter_controls.py out/jt-research/texture-replace-ui-fixtures out/jt-research/texture-filter-ui-fixtures
```

Fresh common-resource prerequisites are in [C2v regeneration](JT_APPEARANCE_BATCH_VALIDATION.md#original-authoring-and-shared-resources). The new writer reuses C2v cube geometry and existing common glTF buffers, authors one original tiny colour/alpha checker, and creates sampler-only variants. All 36 positive JT roots, six unique glTF references and generated hashes stay under ignored `out/`. Source families are originally serialized 10.0 and 9.0/9.5 envelopes in both byte orders; this is synthetic semantic evidence, not authentic exporter conformance or reheadered evidence.

`out/jt-research/texture-filter-ui-fixtures/authoring.json` records hashes, references and the visual list. `out/jt-research/c2w-visual-files.json` lists eighteen positive paths for a single application launch: six 10.0 JT roots, six corresponding glTF references, two legacy controls and two accepted C2v controls with their references. No failure cases.

1. Compare `texture-filter_nearest_10_0_le.jt` with the `nearest` reference: zoom close to a cube face; checker cells have sharp boundaries. Switch between normal rendering and **Ray Traced**. CPU and, when available, GPU should retain sharp cells rather than smoothing them into gradients. The transparent cells should still reveal the red inner cube.
2. Compare `texture-filter_linear_10_0_le.jt` with `linear`: transitions between checker cells are smooth in normal and ray-traced rendering. This must visibly differ from nearest when magnified.
3. For the four `*_mip_*` cases and references, inspect close up, then zoom out/tilt the surface. Nearest versus linear within-level filtering follows the filename; mip filters reduce distant detail. Do not expect visibly distinct mip rounding on every camera position with a 4-by-4 source; the literal native check verifies level decisions.
4. `texture-filter_nearest_9_0_le.jt` and `texture-filter_linear_9_5_be.jt` match their modern/reference counterparts.
5. C2v `unit_accumulate` and `blend_rgba` retain their accepted pattern/alpha and match their references in ray tracing as well as normal rendering. Compare texture placement and cell boundaries, allowing renderer lighting differences.
6. Check one previously accepted ordinary textured glTF/material with default linear filtering for a regression. Use an existing local asset; no new downloaded/private model identifier belongs in this record.

Agent authoring/static inspection completed: Python source syntax, all 36 authored hashes, six reference resource paths, eighteen unique existing launch paths, unchanged moved wrap/bilinear function bodies and changed Markdown links were checked. No C++ build/native test was executed by the agent. After the owner build, the agent launched all eighteen positive documents once in the installed application.

Owner acceptance, 23 October 2026: following the build and eighteen-document single-instance launch, the owner reported "Tests passed". This records acceptance of the viewer gate. Native-suite execution and individual CPU/GPU backend coverage were not separately reported; neither is inferred. The footprint, authored-mip and authentic-export limits above remain.

## Provenance

This pass inspected original ModelViewer `Material::Texture`, `RtSceneBuilder`, snapshot and upload caches, CPU/OptiX texture lookups and C2v writer/control records. Existing published sampler enum mappings are retained from the original adapter; reference URLs/retained hashes are in [C2q provenance](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). No new external source or licence interpretation, vendor software, SDK/schema, disassembly, third-party implementation or asset was used. The checker, variant graphs and native literal oracle are original; common geometry is reused. No vendored decoder, notice, dependency or About version list changes. This records method and limits, not legal clearance.
