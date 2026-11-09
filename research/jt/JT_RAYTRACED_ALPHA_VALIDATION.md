# Ray-traced vertex alpha — Gate C2c

21 October 2026, `feature/JT-importer`. **Owner reported the requested tests passed and authorized commit/push on 21 October 2026. The agent did not build/run the viewer or the optional regression.** C2b wire/feature-edge acceptance was committed and pushed as `a343467`. No JT revision or decoder transport is added.

## Implementation and scope

The existing scene snapshot already carries linear vertex RGBA, defaulting to `(1,1,1,1)` without colours, and Embree interpolates all four channels. CPU alpha tests previously ignored that interpolated alpha. Primary/bounce rays, shadow rays and the through-transmission denoiser-guide search now multiply existing material/texture/radial coverage by clamped vertex alpha once. OPAQUE bypasses alpha as before. MASK uses the existing cutoff; BLEND retains stochastic surface acceptance, allowing undeviated pass-through without multiplying throughput again.

OptiX previously uploaded `float3` RGB despite snapshots retaining RGBA. Its host upload/refit buffers and shared hit-group pointer now use `float4`. The static, CPU-refit, GPU-skin and GPU-morph upload paths retain the same stride. Closest-hit RGB shading explicitly uses XYZ; any-hit interpolates W and applies it to existing opacity before MASK/stochastic BLEND acceptance, including shadow rays. The shader source and shared header are already dependencies of the normal PTX compile/embed step. Rebuild/restart the viewer so host upload layout and embedded GPU code agree. This does not modify skin/morph position kernels, source geometry, blending equations or source texture-resource import.

The scene adapter's obsolete ray-alpha unsupported warning is removed. These are shared renderer fixes, so non-JT imports with MASK/BLEND vertex RGBA also benefit. Nonstandard JT blend equations remain approximated as documented in C2a. Existing CPU/GPU texture-opacity fallback differences, light transport differences, ray budgets, alpha sampling noise and denoising behavior are not newly qualified or redesigned here. Exact image equality with raster is not an acceptance criterion.

## Provenance and evidence limits

The pass inspected only existing ModelViewer source at `a343467`: the snapshot builder, Embree interpolation, CPU alpha tests, OptiX uploads/refits, any-hit/closest-hit functions and build dependencies. No external reference, vendor software, SDK, schema, downloaded model or third-party decoder was newly consulted. Existing application CUDA/OptiX build dependencies are reused; no new package or vendored source is introduced. This is a method record, not legal clearance. See [provenance](JT_PROVENANCE.md) and the [research handout](../../RESEARCH_FOLDER_HANDOUT.md).

Agent checks are read-only path/layout inspection and `git diff --check`. No compilation, numerical pass, CPU image result or GPU run is claimed. The original nested cube controls remain ignored under `out/jt-research/vertex-alpha-ui-fixtures/`; no regeneration is required. The [C2a record](JT_VERTEX_ALPHA_VALIDATION.md) retains their generation commands and hashes. No sample or machine output is committed.

## Prepared CPU regression

`tests/render/rt_vertex_alpha_tests.cpp` uses actual Embree hits and the production CPU renderer on original overlapping triangles, with guide albedos identifying foreground versus background independent of lighting. Prepared cases cover OPAQUE ignoring zero alpha, BLEND endpoints and half coverage, interpolated corner alpha, scalar-times-vertex coverage and MASK values below/at cutoff. Fractional cases use a fixed seed and a probability tolerance over 4096 camera rays. This is a primary-ray regression; shadow/guide-search and OptiX still require viewer checks. These numerical test triangles are separate from the owner-facing cube controls.

Optional owner commands from an MSVC developer shell at repository root:

```powershell
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
cmake -S tests/render -B out/rt-alpha-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMV_RT_VERTEX_ALPHA_TESTS=ON "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/rt-alpha-build --target rt_vertex_alpha_tests
$env:PATH = "$jtPrefix/bin;$env:PATH"
./out/rt-alpha-build/rt_vertex_alpha_tests.exe *> out/jt-research/c2c-cpu-alpha.log
```

If a command fails, do not infer a pass from older files. The target is opt-in so existing OpenGL-only tests do not acquire an unconditional Embree dependency. The owner can also run the existing portable JT suite for scene/GLB regressions. Builds, optional logs and all output stay under ignored `out/`.

## Owner viewer gate

Rebuild and restart the application, including the normal OptiX PTX compile/embed step when the GPU backend is enabled. Use the same files separately, deselected, first with **CPU ray tracing**, then **OptiX** if available. Let accumulation settle; temporarily disable denoising if it obscures the comparison.

| File | Expected ray-traced result |
| --- | --- |
| `alpha_8_2_le_opaque.jt` | Blue outer cube hides orange |
| `alpha_8_2_le_blend.jt` | Orange is visible through partially covering blue; initial sampling noise may settle with accumulation |
| `alpha_8_2_le_zero.jt` | Only orange is visible; blue does not block primary or shadow rays |
| `alpha_8_2_le_one_zero.jt` | Matches opaque coverage |
| `alpha_8_2_be_blend.jt` | Matches little-endian blend behavior |
| `mixed_alpha_blend.glb` | Same alpha semantics via GLB; coverage varies with stored 0.25/0.75 corner alpha |

For a shadow check, enable a lit floor and shadows, hide the orange inner cube through the tree, and compare zero against opaque: zero must add no blue-cube shadow. Restore inner visibility afterward. Reopen an ordinary opaque assembly and switch back to shaded raster/wire views to check prior appearance. If an animated vertex-coloured model is locally available, compare its paused/posed GPU view after an update to exercise refit colour uploads; no such animated sample has been qualified in this pass.

Record CPU and OptiX results separately. If OptiX is unavailable or not exercised, mark its validation pending. The owner reported "Tests passed, commit, push, and proceed" on 21 October 2026. This records acceptance of the requested viewer gate; no separate machine log or optional standalone-regression execution was supplied. C1 residuals, remaining C2 texture/blend semantics and C3–C7 remain open.
