# C2z: filtered affine combiners and colour-layout border mips

23 October 2026, `feature/JT-importer`, after accepted C2y `6183863` and documentation encoding correction `aa22f6a`. Owner build succeeded; delegated raster visual gate accepted on 24 October 2026. No agent C++ build or native-suite execution.

## Implemented scope

This batch advances the combiner, image/filter and end-to-end packages. A single Blend stage with constant incoming material now accepts the existing nearest/linear and six minification choices, with generated or authored levels. A single inline RGB/BGR Decal also accepts these filters. Blend is affine in texture colour, `C*(1-T)+B*T`, and its alpha is a constant times texture alpha. RGB Decal takes texture RGB and retains material alpha. These operations commute with normalized spatial/mip interpolation in the existing linear-colour pipeline, subject to the existing RGBA8 conversion/rounding. Source descriptors and material factors remain retained separately.

RGBA Decal contains a product of sampled colour and sampled alpha; Add includes saturation; multi-unit products are generally nonlinear. They keep the nearest-only restriction. No approximate bilinear bake is introduced for these cases. Independent coordinate mappings/filters, arbitrary Combine, vertex-colour interactions, other image roles, generated mapping and authentic exporter comparisons remain separate.

Sampled authored explicit rings now accept all ten already converted colour layouts: RGB/RGBA, luminance/luminance-alpha, red/green/blue-only, alpha-only, BGR/BGRA. The same per-level component conversion and validated square, both-axis, power-of-two interior profile from [C2y](JT_MIP_COMPOSITION_BORDER_VALIDATION.md) applies. Alpha-only retains neutral white RGB under Modulate. This extends the application's existing channel conversion contract; it does not claim a new legacy OpenGL internal format for channel-only storage. Constant borders for those channel-only layouts remain unqualified. No new pixel encoding or JT version is enabled.

There are no renderer/shader, depth-policy, dependency, About or vendored decoder changes. The source adapter's two gating restrictions change; existing conversion, resource limits, cancellation, PNG carrier, sampler and source metadata paths are reused.

## Prepared native checks

The existing `textureMipBatchTests()` adds literal known-colour checks for filtered Blend/RGB Decal over all six minification choices and asserts the selected material sampler. All ten ring formats have terminal/interior component checks. The existing enabled modern/9.x family, local-version and endian loops apply. Filtered RGBA Decal and Add remain negative geometry-with-warning cases, alongside the earlier independent-filter stack rejection. Tests are prepared, not executed by the agent.

Owner commands (output stays under ignored out/):

```powershell
cmake --build out/jt-selection-build --target jt_tests
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
$env:PATH = "$jtPrefix/bin;$env:PATH"
./out/jt-selection-build/jt_tests.exe *> out/jt-research/c2z-jt-native.log
```

## Original controls and owner viewer gate

Regenerate after the shared C2x/C2y controls exist:

```powershell
python -B tools/jt/make_texture_affine_filter_controls.py out/jt-research/texture-mip-ui-fixtures out/jt-research/texture-affine-filter-ui-fixtures
```

The writer creates 78 positive roots and 13 reference mappings (11 unique documents) in the common content-addressed store, with no per-folder geometry/image copies. It uses original nested cubes, coloured checker patterns, deliberately different lower mip colours/alphas and asymmetric explicit border colours. Three affine cases cover base-only linear Blend, authored trilinear Blend and authored trilinear RGB Decal. Ten ring cases cover each accepted colour layout. Basic 9.0, advanced 9.5 and modern 10.0 envelopes are independently written in both byte orders. Geometry companions use the original 8.2 serializer; these mixed-family controls are not authentic exporter conformance evidence.

After the owner builds normally, launch the 29 positive documents listed in `out/jt-research/c2z-visual-files.json` together in one installed viewer invocation. No fail cases are on that list. Gate `authoring.json` maps neutral case names to hashed references and root hashes. Resources remain under `out/jt-research/texture-common-ui-fixtures/`.

| Group | Visual check |
|---|---|
| Three affine JT/reference pairs | Checker edges soften at close range. Blend combines material and texture colour; RGB Decal retains material transparency. The red inner cube should be visible where the outer cube is translucent. Compare with matched camera/up axes. |
| Two authored affine pairs | Zoom from fit to roughly 100 pixels wide and smaller: colours change through the authored levels with gradual transitions, rather than remaining at the base image. |
| Ten ring JT/reference pairs | Borders and corners remain present and keep their texture-relative positions while zooming. Luminance is grayscale; red/green/blue-only retain only the named channel; alpha-only preserves white RGB with varying transparency. RGB/BGR and RGBA/BGRA pairs should agree. |
| Two 9.x representatives | Match the corresponding modern Blend and luminance-alpha ring cases. |
| Three accepted regressions | The C2y linear ring retains its mapping; nearest versus linear filter controls remain sharp versus smooth. |

Also check ADS appearance if available. Record CPU/OptiX separately; raster acceptance is not backend certification. Stop for owner confirmation before committing or dependent implementation.

References use independently computed expected pixels and padding but the same ModelViewer renderer/private mip carrier. They are not an independent rendering oracle. Static authoring inspection checks file hashes, URI containment, shared resources and expected mip dimensions; it does not run the importer.

## References and provenance

Consulted the retained JT10.0 Rev-B texture Blend Type table and compatible JT9.5 texture hierarchy, plus retained OpenGL2.1 tables 3.21/3.22 and the filtering definitions for affine-versus-product ordering. Exact retained URLs/hashes are in [C2y references](JT_MIP_COMPOSITION_BORDER_VALIDATION.md#references-and-provenance). Existing original adapter, fixture serializers and independent Python component/combiner writers were inspected and reused. No new external source/download, vendor software, SDK/schema, disassembly, third-party implementation, sample asset or licence interpretation was introduced. This records method and qualification limits, not legal clearance. All generated evidence stays local under ignored out/.


## Static review and owner handoff - 23 October 2026

Python syntax, all 78 recorded root hashes, the 13 case/reference mappings (11 unique reference documents), expected base/terminal dimensions, shared-resource URI containment, 29 unique positive launch paths and changed relative document links were inspected. The checklist initially repeated two content-identical RGB/BGR and RGBA/BGRA references; it now opens each shared reference once. `git diff --check` passed. The existing production diagnostics JSON records under research/jt/diagnostics are unrelated owner-supplied evidence and were preserved; this gate adds no generated research data.

Source review confirms the new Assimp filter macro has its explicit GltfMaterial header, nonlinear negative cases remain prepared, and the existing geometry fallback and resource checks remain active. No C++ build, native suite, importer execution or C2z viewer verification was performed. The owner should build normally; after build completion the agent can launch the 29 positive documents together for the visual checks above. C2z remains uncommitted and unaccepted.


## Owner build and positive launch - 23 October 2026

The owner reported a successful build. The agent verified matching installed/build executable SHA-256 hashes and launched all 29 unique positive checklist paths as arguments to one installed viewer process. The launch record remains local at out/jt-research/c2z-single-instance-launch.json. No fail cases were requested. Build/launch status does not establish visual acceptance or native-suite execution; those results remain pending.


## Delegated visual acceptance - 24 October 2026

The owner reported build success, inspected the controls and delegated the detailed visual decision and continuation to the agent. The agent visited all 29 original checklist documents individually in ADS, checking colour components, border regions, translucency, the two 9.x representatives and accepted nearest/linear/ring regressions. Representative authored mip cases were zoomed from fit to small footprints and changed colours as intended. A PBR smoke check covered the affine Blend control and the earlier linear-filter regression; this is not exhaustive PBR or CPU/OptiX qualification. No load error or missing positive geometry was observed.

The initial glTF references were single-sided, whereas the original JT geometry uses the viewer's default two-sided material. This produced different overlapping patterns on transparent closed cubes. Source review found the fragment shader discards reference backfaces, and GUI exports confirmed matching positions, normals, triangle indices and UVs. The writer now explicitly matches two-sidedness and the exponent-16 roughness approximation for both reference materials. References were regenerated, the 29-document session reopened, and the affine pair compared with matching Z-up cameras: the previously discrepant overlap pattern agrees. This correction changes original controls only, not the importer or depth policy. Transparent self-overlap still uses the existing renderer policy; no order-independent-transparency claim is made.

The installed executable SHA-256 was 649bf7d08d1df60653511aff66dc966490dabd7fc9e9a746c20e8743b66b8d50. The corrected positive launch inventory is out/jt-research/c2z-agent-review-launch.json; regenerate controls with the command above and pass the listed paths to one installed viewer invocation. GUI-only evidence exports remain under ignored out/jt-research/. C2z is accepted for the bounded raster scope under the owner's delegation. Native-suite execution and CPU/OptiX checks remain unreported.

A separate end-to-end defect was reproduced during inspection: exporting an embedded JT image through the application's File > Export path writes an unresolved embedded:// URI into GLB, whereas the external-image reference exports a bufferView image. This is carried into the next package-7 checkpoint; C2z acceptance does not claim application GLB round-trip completion.
