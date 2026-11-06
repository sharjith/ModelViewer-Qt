# Shared raster BLEND coverage regression

23 October 2026, `feature/JT-importer`. Owner rebuilt and accepted the narrower correction, then reported no regressions in the reopened JT controls. Native results remain unreported.

## Evidence and diagnosis

The owner reports that local sample GA01 gained apparent interior transparency after `3523fcf406560def89b6f0446ae8656d7ba7fc03`, only in PBR. GA01 SHA-256: `407f7698d1162a99be7929a7c472317029563a254bbf8d3556735209a670826d`. Its name, geometry, images and paths remain local.

Read-only source inventory finds 18 materials: 10 OPAQUE and 8 BLEND; no primitive has a COLOR_0 attribute. Several interior BLEND materials have scalar alpha one and images containing fully opaque texels alongside lower-alpha texels. The earlier commit added vertex alpha to coverage and disabled depth writes for all BLEND materials. With no vertex colours in this sample, the depth-write change is the leading explanation: later triangles of a BLEND mesh can overwrite an earlier opaque face, making the solid face look transparent. Source inventory and code inspection establish this mechanism; an owner runtime retest is still needed to confirm it explains the reported image.

Regenerate the inventory (Python with Pillow installed):

```powershell
python -B tools/jt/inspect_glb_alpha.py '<local-samples>/sample-GA01.glb' out/jt-research/glb-alpha-GA01.json
```

The inventory reports image alpha distributions, not actual screen coverage or UV-weighted visible areas. It is not a rendering oracle.

## Current correction: narrow the vertex-alpha depth change

The failed extra opaque-coverage pass has been removed, including its shader uniform, viewport loops and scoped state helper. Rendering again uses a single normal colour pass. The shared main-scene shader is unchanged from the accepted vertex-alpha implementation.

On geometry upload, RenderableMesh caches whether any RGBA vertex alpha is below one. White loader fallback colours and RGB colours with alpha one do not count. `needsDepthMaskOff()` preserves its historical scalar-opacity threshold and additionally disables depth writes only for Alpha materials with actual vertex transparency. OPAQUE/MASK vertex colour does not activate that additional rule. This isolates the earlier JT vertex-alpha requirement instead of changing every BLEND material's depth behaviour. Rebuilding geometry or recreating context-bound buffers recomputes the cached classification.

For GA01, which has no source vertex colours and receives white fallback RGBA, this returns the same depth-write decision as the parent of `3523fcf`. Shader sampling still uses the original texture alpha; no material mode or image pixel is rewritten. Scalar-transparent glass retains its prior depth behaviour. The accepted mip implementation and ray tracing are unaffected.

This deliberately preserves the existing texture-only depth policy rather than claiming general order-independent transparency. Texture-only Alpha surfaces at scalar opacity one can write depth even at partial/zero image coverage. Multiple overlapping transparent layers and same-mesh ordering remain existing renderer limitations. A future general transparency overhaul needs separate scope and qualification; it must not be presented as solved by this regression correction.

## Owner validation gate

Rebuild the viewer using the usual configuration. Then:

1. Open local GA01 in PBR and inspect the interior areas that previously became transparent. Opaque areas should occlude geometry behind them as before the regression.
2. Confirm windows and deliberately transparent areas still show geometry behind them. Rotate the camera to check more than one view.
3. Check smooth/flat shading and shaded-with-edges, then ADS for unintended changes. Confirm hover/selection colours remain correct.
4. Reopen accepted JT alpha controls: partial coverage remains transparent; full coverage hides the inner object. The accepted C2x mip controls should retain their reported colour transitions.

Prepared offscreen checks compile the actual main-scene vertex/fragment shaders and invoke the same vertex-alpha classification/depth decision helper as RenderableMesh. They check texture-only legacy depth at full/partial/zero texture alpha, scalar and vertex transparency, opaque vertex colours, and a later far triangle in the same texture-only shell. PBR unlit colours isolate coverage from lighting. The adverse triangle-order case fails under the blanket BLEND depth-off policy. A missing OpenGL 4.5 context returns skip 77; a skip is not a pass.

```powershell
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
cmake -S tests/render -B out/jt-filter-build -G Ninja "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/jt-filter-build --target blend_depth_policy_tests
$env:PATH = "$jtPrefix/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$jtPrefix/Qt6/plugins"
./out/jt-filter-build/blend_depth_policy_tests.exe *> out/jt-research/blend-depth-policy-native.log
```

The agent has not built or executed this target or the revised viewer. Stop for owner results before committing or advancing dependent work. C2x reported mip observations remain accepted separately.

## Provenance

This pass inspected local GA01 bytes with an original read-only inventory tool, existing application renderer/shader code, and the earlier repository commit. No new external document, vendor software, SDK/schema, disassembly, third-party decoder implementation, download or licence interpretation was used. New regression triangles and checks are original. No third-party notices or upstream-derived sources change for this correction. This describes method and evidence limits, not legal clearance.

## Owner retest - 23 October 2026

The owner rebuilt and reported GA01 interior transparency still present, also observable in ADS. The earlier PBR-only restriction is withdrawn: the owner is unsure whether ADS already behaved this way in the first report. The attempted opaque-coverage split has not resolved the reported regression and is not accepted. The owner will provide screenshots to identify the exact areas/view and distinguish sampled source alpha, occlusion/draw order, and viewing through glass. The first correction is not accepted; screenshots were subsequently reviewed as recorded below. C2x mip/filter observations remain a separate accepted record; the subsequently opened alpha regression controls have no reported result yet.

## Screenshot evidence and second correction - 23 October 2026

The owner supplied four images: regressed ADS (S1), earlier ADS (S2), regressed PBR (S3), earlier PBR (S4). Red upholstery is visible through the cup-holder wall in S1/S3, with speckled trim surfaces; S2/S4 show the earlier occlusion. This confirms the observation is not restricted to looking through a glass window. It does not by itself identify every fragment's sampled alpha or prove the origin of every speckle.

| ID | SHA-256 |
|---|---|
| S1 | `8b2776737c78bc1ca8f65910a8afc0bcf3f756d00d181ca621ba9ab55378c852` |
| S2 | `b6132ff1ef50cf47552be8c233f033c3a871b625055db819efa4980fbce23c63` |
| S3 | `4fccbf693cd24eae301b38b0cdfcab9bd03df3e72d5c1b0eee5f5b18bf64df94` |
| S4 | `e97ffa439e713c01cfbb36917125c9fa46313289d139c1f31f992ce11ed60ef6` |

Images and any source-derived analysis stay local. The current narrower correction removes the failed pass and restores the parent commit's depth decision for source GA01 while retaining real vertex-alpha support. Owner build/viewer/native results for this revision are pending. The earlier owner build and failed retest apply only to the removed attempt.

## Owner GLB acceptance - 23 October 2026

After rebuilding the narrower vertex-alpha depth revision, the owner reported that GA01 transparency is fixed. This accepts the reported GLB regression outcome; it does not establish separate native-suite execution or a pass for the JT controls. The same 21 positive alpha/mip/filter controls and references were subsequently requested for owner retesting, launched together from the matching installed build. The launch manifest is local at `out/jt-research/blend-depth-policy-retest-launch.json`. JT control results remain pending; do not infer them from the GLB acceptance.

## Final viewer acceptance - 23 October 2026

The owner reported no regression in the reopened tests after confirming the GA01 GLB fix. This accepts the reported viewer alpha/mip/filter regression gate and releases the commit hold. It does not establish execution of the prepared native target or separate CPU/OptiX backend qualification. The removed opaque-coverage pass is not part of the accepted implementation; `BlendDepthPolicy.h` and the geometry-upload classification in RenderableMesh are.
