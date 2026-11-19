# Inventor complex part geometry

19 November 2026 · `feature/inventor3d` · nearest committed checkpoint `81786a9`

## Motor validation

G02 IPT SHA-256 `9eb6cb5ae8989a6b4cb7c03d419ef4bf575d8539e3a059e135985b689ff3cb6e`. STEP SHA-256 `790886e9a89bed023e0470d9fd58e96361399337d3a6df45ae4d7702a993e66b`. The saved ASM data contains 10 spline-supported faces and 56 edges with saved intersection curves. The importer reads clamped polynomial and rational B-spline grids and curves, resolves saved subtype references, and uses the native edge direction, trim interval and error bound when rebuilding 3D curves. Closed curves can cross their saved parameter origin. Non-clamped and unsupported surface forms still require evidence and implementation. G02 qualifies the polynomial clamped profile.

The first motor reconstruction passed face construction but failed OCCT solid validation. A whole-shell repair with OCCT's natural-bound option enabled made some small toroidal patches enclose the complementary region. Disabling natural-bound insertion for spline-bearing solids while keeping the bounded trim and orientation repair preserved the intended regions. The motor passed `BRepCheck_Analyzer` as one solid with 372 faces and 901 edges. All 372 faces tessellated with the viewer's default settings, producing 35,937 triangles.

The maximum bounding-box corner difference between native IPT and STEP was below 0.001 mm. Extents were approximately 37.0 × 22.4 × 70.5 mm. Adaptive integration measured 23,913.840989 mm³ for the IPT and 23,913.863907 mm³ for the STEP, a relative difference of about `9.6e-7`. All 372 faces matched one-to-one by surface type, area within 0.05 mm² and centroid distance within 0.05 mm. These measurements qualify this motor. The native appearance probe resolved yellow as the body default and 110 face overrides. Counts for the dark and two gray styles were 27, 18 and 65, matching the independent STEP style assignments. I01–I05, the wheel geometry and appearance, and all nine repository CTests passed again. On 19 November 2026 the owner provided a screenshot of the native G02 in ModelViewer showing the geometry and multiple face colors.

## Pump findings

G07's part records include one face-bearing body with an explicit identity transform and two wire-only bodies. The solid import recognizes that arrangement and the observed toleranced coedge, edge and vertex layouts. A four-use edge has a retraced loop on a torus and two coedges that join a spline and a plane. The importer checks the torus and plane tangency, samples the saved curve against both, and confirms that the remaining faces form one connected shell after dropping the zero-area loop.

The pump also uses V-periodic splines with matching first and last rows, toleranced vertex positions, and saved lines whose direction vectors encode physical displacement per parameter unit. The maximum authored intersection-curve tolerance in the pump was 0.0106975 mm. The importer honors it within a 0.011 mm limit. A toroidal face has two parallel winding boundaries and a hole. The importer aligns the pcurves in one parameter chart, finds opposed boundary vertices on a shared parallel away from the hole, and uses the torus's exact isoparametric curve to join them with a seam. This preserves the intended surface area and the hole. The implementation identifies the arrangement from topology and geometry.

The probe validated a solid with 349 faces and 870 edges. Its extents are 178.30 × 267.00 × 351.00 mm and its measured volume is 1,202,452.34 mm³. All 349 faces tessellated at the viewer's default settings with 25,596 triangles. G07 has no paired STEP in the supplied package. The checked reference is the saved graphics data in the original document, where all 349 persistent face keys of the solid have matching entries. The reconstructed face boxes stay within the corresponding cached boxes to within 0.02 mm, with a measured maximum excursion of about `1e-7` mm. The cached model extents are 178.3146 × 267.00 × 351.00 mm. These measurements establish agreement with the saved envelopes. A paired export would support exact trim and volume comparison. The wheel and motor appearance probes and all nine repository CTests passed. On 19 November 2026 the owner approved the staged pump display and supplied a screenshot. The pump uses the default gray appearance. See the [G07 profile and hash](INVENTOR_ANALYTIC_GEOMETRY.md#observed-sample-profiles).

## Reproduce

### Additional IPT coverage

The owner reported that G08, SHA-256 `b1b89be345a8c083acb9780ced3267913c4d13e418aa65dec1626eab655260e5`, fails on ASM face 83. The saved surface is a `helix_spl_line` record. Its block carries a procedural helical definition and `nullbs` fields. The currently supported saved B-spline payload is absent. G08 is the regression input for the later procedural-geometry pass.

### Motor and pump commands

Set `$python` and `PYTHONPATH` as in the [container probe instructions](INVENTOR_IMPORT_FEASIBILITY.md#native-compound-file-reader-and-owner-verification). Run from the repository root in the x64 Visual Studio developer shell.

```powershell
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$env:LIB = "$(Join-Path $qt 'lib');$env:LIB"
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
$motor = '<local-G02-native>.ipt'
$reference = '<local-G02-reference>.stp'
$pump = '<local-G07-native>.ipt'
$verify = 'out/inventor-research/motor-verify'
& $python -B tools/inventor/inspect_asm_profile.py --zstd-library "$qt/bin/zstd.dll" --sample G02 $motor --sample G07 $pump `
    > out/inventor-research/complex-ipt-profile.json
cmake --build out/inventor-research/compound-build
& $probe --part-solid $motor > out/inventor-research/G02-motor-solid.json
& $probe --part-appearance $motor > out/inventor-research/G02-motor-appearance.json
& $probe --part-solid $pump > out/inventor-research/G07-pump-solid.json
cmake --build "$verify/build"
& "$verify/build/inventor_compound_probe.exe" $motor $reference > "$verify/G02-motor-verification.log"
if ($LASTEXITCODE -ne 0) { throw 'G02 geometry or tessellation check failed' }
cmake --build out/build/ninja_release_vcpkg --target ModelViewer --parallel 2
$stage = Join-Path (Get-Location) 'out/inventor-research/viewer-motor'
cmake --install out/build/ninja_release_vcpkg --prefix $stage
ctest --test-dir out/inventor-research/ctest-build --output-on-failure --output-log out/inventor-research/motor-ctest.log
```

The motor comparison harness and generated reports stay local under ignored `out/`. The separate STEP input is used by the harness for verification.

The local pump checker builds the production importer with diagnostic output enabled, writes keyed face boxes and checks triangulations. The local graphics verifier reads the original saved graphics segment and compares the diagnostic boxes. Build and run both from the same developer shell as above.

```powershell
$pumpCheck = 'out/inventor-research/gearbox-check'
$env:PYTHONPATH += ";$(Resolve-Path tools/inventor)"
cmake --build "$pumpCheck/build" --target inventor_compound_probe
& "$pumpCheck/build/inventor_compound_probe.exe" --part-solid $pump `
    > "$pumpCheck/G07-final-pump.json" 2> "$pumpCheck/G07-final-pump.log"
if ($LASTEXITCODE -ne 0) { throw 'G07 solid or tessellation check failed' }
& $python -B out/inventor-research/verify_pump_graphics.py $pump "$qt/bin/zstd.dll" "$pumpCheck/G07-final-pump.log" `
    > out/inventor-research/G07-pump-bounds.json
if ($LASTEXITCODE -ne 0) { throw 'G07 graphics envelope check failed' }
$stage = Join-Path (Get-Location) 'out/inventor-research/viewer-pump'
cmake --install out/build/ninja_release_vcpkg --prefix $stage
ctest --test-dir out/inventor-research/ctest-build --output-on-failure --output-log out/inventor-research/pump-ctest.log
```

## Provenance

This pass consulted `Step.cs` and `Brep.cs` from CAD Convert at `a3cbe9f5f41acff810b5006d964d116e805e042b`, the public OCCT 8.0.1 APIs, and the supplied G02 and G07 files. The reference source helped identify subtype indexing, knot multiplicities and pole ordering. ModelViewer builds OCCT geometry directly from the saved IPT and uses independent validation probes. No CAD Convert binary, vendor SDK or disassembly was used. [Pinned research reference](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b/tools/invre)
