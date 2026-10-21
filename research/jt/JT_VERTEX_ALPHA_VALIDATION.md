# Vertex-colour transparency — Gate C2a

21 October 2026, `feature/JT-importer`. **Owner-authorized local MSVC AddressSanitizer control build, portable suite and generation checks passed. Owner viewer validation passed after the shader/depth corrections described below.** C1i was accepted on this date. This independent appearance gate uses already qualified RGBA transport; it does not close unresolved C1 layouts or enable another file version.

## Scope and method

The scene adapter previously rejected non-opaque vertex alpha when blending was active. It now retains that alpha and supplies an explicit `BLEND` material with neutral opacity 1. The raster shader multiplies interpolated vertex alpha once; its BLEND RGB premultiplication now includes that same vertex coverage, preventing zero-alpha colours from adding light. The explicit material mode selects the transparent pass and carries the same intent through GLB. Blending disabled, or source ONE/destination ZERO, normalizes effective scene alpha to 1. Vertex-colour override removes the RGBA array and uses material appearance as before. Other blend factors retain the existing conventional-alpha approximation and warning; arbitrary OpenGL blend equations are not claimed as faithfully reproduced.

Material cache identity now includes blend flags and active vertex transparency. Consequently the same geometry under active/disabled blending cannot reuse an incompatible appearance variant, while repeated matching instances can still share. The decoder and vendored third-party source are unchanged. The shader correction also affects other formats using the shared alpha path. OPAQUE retains alpha 1; MASK now includes incoming vertex alpha in its cutoff test, while BLEND includes it in final coverage and RGB premultiplication. The direct change is shared by already enabled families with qualified RGBA collections: 8.x currently only has the earlier constant RGB(A) subset; 9.x and 10.x retain their distinct decoded transports. This is not new codec qualification for every family.

The pass consulted the published JT 10.0 material blending/override definitions and existing application code. Exact source URLs/version/hash, methods and exclusions are in [JT provenance](JT_PROVENANCE.md). No vendor software, SDK, proprietary schema package or disassembly was used; no new dependency or licence interpretation is introduced. Method provenance is not legal clearance. Original synthetic geometry supplies the proposed controls; no downloaded CAD sample or vendor file is redistributed.

## Known rendering limits

This gate targets the standard shaded raster viewport and GLB transport. Code inspection finds that current CPU/OptiX opacity evaluation omits vertex alpha, and the dedicated wireframe shader uses fixed alpha. Ray-traced and wireframe views remain outside C2a; imports with active vertex transparency report that limitation. Ray-traced/wireframe parity stays on C2, together with broader blend semantics and source image/shader resources. No ray-traced display result has been measured in this pass.

## Local control validation

`tests/jt/jt_scene_tests.cpp` now checks active/disabled and ONE/ZERO cases, alpha endpoints 0/1, half opacity, flat-normal processing, scene copy, variable 0.25/0.75 corner alpha and GLB material/colour round-trip. It also checks shared-geometry opaque/transparent variants, repeated resource sharing, and material-colour override. The former deliberately unsupported translucent-colour scene case now expects preserved alpha and explicit blending.

`JtAlphaCubeFixture.h` generates original constant-alpha 8.0/8.1/8.2 controls in both byte orders. At the owner's request, the visual geometry is now a blue outer cube enclosing a smaller opaque orange cube, keeping all existing filenames. Each file contains two meshes, 48 vertices and 24 triangles in total. Face-local vertices preserve sharp edges. The original planar scene unit cases remain for corner-alpha transport checks. The fixture mode writes only to the requested ignored output folder; the mixed-alpha GLBs now use these same original cubes, not a vendor JT export.

On 21 October 2026 the owner explicitly authorized building the controls locally. The isolated MSVC AddressSanitizer build, portable suite and fixture checks passed after correcting the new synthetic test to reload its released payload between independent scene builds. The revised nested-cube generator was also built and run locally on 21 October 2026: the portable suite and generation checks passed, producing 24 original JT controls and two original mixed-alpha GLBs. A strip-boundary predictor encoding error in the new fixture writer was corrected before this successful run; no decoder change was needed. The revised cube controls check bounds and strict nesting, distinct colours, outward normals/winding, enclosed volume, effective alpha and blend classification; GLB reload checks the same properties. This does not validate the shared shader in the full viewer; the agent did not build or run the full viewer. Genuine vendor RGBA models and broader visual comparisons remain future qualification evidence.

## Owner rejection and renderer follow-up — 21 October 2026

The owner reported that none of the revised controls showed the blue outer cube as transparent. This rejected the initial viewer gate; the earlier portable importer/GLB checks establish data transport only, not rendered transparency.

Source inspection found that `main_scene.frag` declared a local `v_color` containing the lighting result, shadowing the incoming vertex RGBA varying. The alpha modulation consequently used lighting alpha (normally 1) instead of source vertex alpha. The earlier premultiplication change also read that shadowed value and could not fix the issue. The local lighting result is now named `shadedColor`; incoming vertex alpha participates in the MASK/BLEND opacity calculation and the complete BLEND coverage premultiplies RGB once. OPAQUE remains fully opaque.

A separate render-state check disabled depth writes only when material scalar opacity was below 1. Vertex-alpha BLEND intentionally keeps scalar opacity 1. `RenderableMesh::needsDepthMaskOff()` now also recognizes explicit Alpha/BLEND mode, so these transparent draws do not write opaque depth.

These follow-up changes were inspected and passed `git diff --check`; the full viewer was not built or run by the agent. The agent did not execute GPU rendering. Existing generated files and their hashes are unchanged. The owner was asked to rebuild and reopen the same controls in shaded raster mode, deselected: blend exposes the orange inner cube; zero exposes only orange; opaque and ONE/ZERO hide orange. Both ADS and PBR use the corrected shared main shader. The mixed-alpha GLBs should exercise the same renderer correction. Ray-traced and dedicated wireframe vertex alpha remain outside this gate.

## Owner acceptance — 21 October 2026

After rebuilding, the owner confirmed that blend is partially transparent, zero fully hides the blue cube, and opaque/ONE-ZERO remain opaque, then authorized commit/push and proceeding. C2a shaded raster acceptance is recorded on that basis. This does not establish separate CPU/OptiX or dedicated wireframe acceptance.

## Regeneration commands

Controls are already available locally. The owner builds the viewer normally for the display check. To regenerate controls, use an MSVC developer PowerShell at repository root:

```powershell
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
cmake -S tests/jt -B out/jt-selection-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMV_JT_SANITIZE=ON "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/jt-selection-build --target jt_tests
$jtCompilerBin = Split-Path (Get-Command cl.exe).Source
$env:PATH = "$jtPrefix/bin;$jtCompilerBin;$env:PATH"
./out/jt-selection-build/jt_tests.exe --vertex-alpha-fixtures out/jt-research/vertex-alpha-ui-fixtures
```

The generator command also runs the portable test suite first. If any command fails, stop and report its output; do not use controls left by an older executable as acceptance evidence. Optional evidence is regenerated with the same command and redirected into ignored research storage:

```powershell
./out/jt-selection-build/jt_tests.exe --vertex-alpha-fixtures out/jt-research/vertex-alpha-ui-fixtures *> out/jt-research/c2a-validation.log
```

Local logs are `out/jt-research/c2a-control-build.log` and `out/jt-research/c2a-validation.log`. The latter is regenerated by the fixture command above; a redirected build regenerates the former:

```powershell
cmake --build out/jt-selection-build --target jt_tests *> out/jt-research/c2a-control-build.log
Get-FileHash out/jt-research/vertex-alpha-ui-fixtures/alpha_8_2_le_blend.jt,out/jt-research/vertex-alpha-ui-fixtures/alpha_8_2_le_opaque.jt -Algorithm SHA256
```

Measured original controls: `AL82-B` (`alpha_8_2_le_blend.jt`) SHA-256 `a004e076cc0d1dbeca9fcd6ffde6ca44d638a538dcc9c195d6fd4e028d19f09a`; `AL82-O` (`alpha_8_2_le_opaque.jt`) SHA-256 `6451bbf82cb272b605c0dc0f9c9af53675aca93046f7e221750976ec7bf8dd1f`. These hashes identify the revised nested-cube originals, replacing the earlier planar controls under the same filenames; they are not vendor samples.

No log or generated fixture is tracked. The owner may also run the usual locally configured `jt_tests <local-samples>` regressions; optional sample geometry remains local.

## Owner viewer checklist

Use the standard shaded/raster viewport. Open files separately from `out/jt-research/vertex-alpha-ui-fixtures/`; no texture assignment is needed.

| File | Expected behavior |
| --- | --- |
| `alpha_8_2_le_opaque.jt` | Opaque blue outer cube hides the enclosed orange cube; 2 meshes / 48 vertices / 24 triangles total |
| `alpha_8_2_le_blend.jt` | Same cubes/counts/placement; blue outer cube has alpha 0.5 and the opaque orange cube is visible through it |
| `alpha_8_2_le_one_zero.jt` | Looks like the opaque reference despite stored alpha 0.5, because ONE/ZERO replaces framebuffer colour |
| `alpha_8_2_le_zero.jt` | Only the opaque orange inner cube is visible; both meshes remain in the tree/counts |
| `alpha_8_2_be_blend.jt` | Matches the little-endian blend control |
| `mixed_alpha_opaque.glb` | Same nested cubes, opaque blue outer cube hides the orange inner cube |
| `mixed_alpha_blend.glb` | Same nested cubes; outer cube has vertex alpha 0.25 at positive Z and 0.75 at negative Z, giving different coverage as the view rotates; inner cube remains opaque |

Rotate the model to confirm that the orange cube is enclosed and remains visible through the blended outer cube. Closed transparent geometry may tint the inner object differently as multiple surfaces overlap; exact tint is not the acceptance criterion. The 8.0/8.1 controls have the same expectations. Reopen an ordinary opaque assembly to check that its appearance is unchanged. Selection highlights can obscure transparency; judge with the part deselected. A fully transparent part may remain selectable because its geometry is intentionally retained.

C2a owner build/test confirmation is accepted. C1 residuals, remaining C2 features and C3–C7 remain open.
