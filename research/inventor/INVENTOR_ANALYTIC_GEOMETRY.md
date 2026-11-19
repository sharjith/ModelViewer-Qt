# Native Inventor analytic geometry

19 November 2026 · `feature/inventor3d` · ModelViewer baseline `eeebd7d77f2aa0482b181ceb01dd06a7f2628248`

The owner visually accepted I01 in the earlier planar viewer build. The current C++ changes extend the saved-body reader to construct trimmed lines, circles and ellipses, plus planes, cylinders, circular cones, spheres and ring tori. Face boundaries follow the saved coedge order and orientation. The implementation uses OCCT to add parameter-space curves, orient multiple trim loops and repair missing periodic seams. Every imported body must yield its own closed OCCT solid and pass the validity check. File Open tessellates those solids into separate scene nodes. Unsupported geometry produces an explicit error with the offending face number where available. All geometry is built in memory. [Native geometry reader](../../src/Import/Inventor/PartSolid.cpp) · [First planar build and acceptance](INVENTOR_IMPORT_FEASIBILITY.md#first-native-planar-solid)

## Observed sample profiles

These are counts from the saved records before the ASM history section. Unreferenced records can be present, and record counts and vertex spans do not establish final OCCT topology or geometric agreement. The probe results and owner visual status are recorded below.

| ID | Evidence | Qualification use |
| --- | --- | --- |
| I01 | Six planes and 12 straight edges. All referenced edges occur twice. | Regression for the accepted 20 × 10 × 5 mm part. |
| I03 | Six planes, one cylinder encoded as a cone with zero slope, and two circular arcs. All 15 referenced edges occur twice. | First curved solid. The feature report specifies a 2 mm edge fillet. |
| I04 | Two saved body records, eight planes, and one cone-family face. The paired report identifies two new-body extrusions. | Independent single-shell solids with overlapping volumes and separate viewer nodes. |
| I05 | Seven planes, one cylinder encoded as a cone with zero slope, and two full circular boundaries. Their saved closure vertices lie on opposite meridians. All 14 referenced edges occur twice. | Planar inner loops and missing periodic seam repair. The feature report specifies a 5 mm drilled diameter. |
| I02 | Eight planes, three cylinder faces and two edge records each referenced four times. Those two edges lie where the outer cut cylinder is tangent to the part's exterior planes. | An exterior and one cavity joined by retraced planar boundary segments in the saved topology. |
| G01 | 53 planes, 30 cone-family faces, 15 tori and 110 ellipse-family edge records. | First production part verified against its paired STEP. The owner accepted the displayed geometry and observed a color difference. |
| G02 | 372 saved face records, including 10 spline surfaces and 56 intersection-curve edge records. | Spline surfaces and exact general edge curves are still required. |
| G07 | Three body records. The inspected links show one face-bearing body and two wire-only bodies. The face records include 78 spline surfaces, and 238 edge records refer to intersection curves. | Useful production input for spline geometry and determining which native bodies the viewer should display. The supplied package contains no separate STEP export. |

The new pump package supplies seven IAM and 23 IPT files. Some part filenames contain `STEP` but their final extension is `.ipt`. The largest IAM is G06, SHA-256 `d61e3428ced03d78e58bf31c87bf8e4de0cb92005a06d9c32159e084bf61c04b`. The inspected pump component is G07, SHA-256 `5a4666a152aa698f8930d3c7de543f2d7c968fa6a8e75424f45c84991cfb3a0e`. The compound inventories found 34 streams in G06 and 28 in G07.

The length-prefixed UTF-16 candidates in G06's top-level `UFRxDoc` included 10 uniquely matching local component basenames and seven with no match or detected embedded document. G05's equivalent audit found 13 unique matches and one unmatched candidate at its top level. Both packages are useful for partial resolution. This audit does not read active occurrence bindings, so a missing candidate may be inactive or suppressed. Full assembly validation still needs a complete Inventor Pack and Go with the matching assembly STEP. A small nested assembly with a repeated, rotated part would give us directly checkable occurrence placements.

## Reproduce the read-only observations

Set `$python` and `PYTHONPATH` as in the [compound-file probe instructions](INVENTOR_IMPORT_FEASIBILITY.md#native-compound-file-reader-and-owner-verification). Substitute the local sample paths and run from the repository root.

```powershell
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$auditArgs = @('--zstd-library', (Join-Path $qt 'bin/zstd.dll'))
foreach ($id in 'I01', 'I02', 'I03', 'I04', 'I05', 'G01', 'G02', 'G07') {
    $auditArgs += @('--sample', $id, "<local-samples>/$id.ipt")
}
& $python -B tools/inventor/inspect_asm_profile.py @auditArgs > out/inventor-research/analytic-profile-audit.json
& $python -B tools/inventor/audit_assembly_references.py '<local-G05-folder>' > out/inventor-research/G05-reference-audit.json
& $python -B tools/inventor/audit_assembly_references.py '<local-G06-folder>' > out/inventor-research/G06-reference-audit.json
& $python -B tools/inventor/inspect_compound.py '<local-samples>/G06.iam' > out/inventor-research/G06-streams.json
& $python -B tools/inventor/inspect_compound.py '<local-samples>/G07.ipt' > out/inventor-research/G07-streams.json
```

## Automated verification and owner visual gate

On 19 November 2026 the owner authorized automated runs and confirmed I01 and I03 loaded correctly in the UI. The owner reported failures on I02 and I05. The probe reproduced both. I05's initially generated seam joined full-circle closure points on different meridians, and OCCT reported invalid trim geometry. The fix detects full cylindrical bands with isolated circular closure vertices, checks the circles are coaxial with the source cylinder, and builds the exact band with both closure points on the same meridian. The extra OCCT seam is a topological edge. It changes neither the 5 mm hole diameter nor its cylindrical surface.

After that fix, the probe reconstructed I01 with 6 faces and 12 edges, I03 with 7 faces and 15 edges, and I05 with 8 faces and 15 edges. I02 still reported its four-use edge in that build. On 19 November 2026 the owner confirmed I01, I03 and repaired I05 all loaded correctly in the viewer. I04 was added after that UI run.

The multibody pass reconstructed I04 as two validated solids. In saved-body order, the block measures 20 × 20 × 10 mm with six faces and 4,000 mm³. The cylinder measures Ø10 × 5 mm with three faces and 392.699081699 mm³. Each has one lump and one shell. Both start at Z = 0 mm and the cylinder extends 5 mm into the block. The original placements and overlapping volumes are preserved. The owner confirmed the I04 tree entries and independent visibility on 19 November 2026.

From the same x64 Visual Studio developer shell used for the standalone probe, rebuild it and check the five controls. Expected topology and dimensions come from the accepted I01 and from the controls' saved analytic records and feature reports.

```powershell
cmake --build out/inventor-research/compound-build
if ($LASTEXITCODE -ne 0) { throw 'Inventor probe build failed' }
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
$controls = '<local-control-folder>'
$expected = [ordered]@{
    I01 = @{ dims = @(20, 10, 5); faces = 6 }
    I02 = @{ dims = @(40, 40, 20); faces = 11 }
    I03 = @{ dims = @(20, 20, 10); faces = 7 }
    I04 = @{ dims = @(20, 20, 10); faces = 9 }
    I05 = @{ dims = @(20, 20, 10); faces = 8 }
}
foreach ($id in $expected.Keys) {
    $report = "out/inventor-research/$id-analytic-solid.json"
    & $probe --part-solid (Join-Path $controls "$id.ipt") > $report
    if ($LASTEXITCODE -ne 0) { throw "Native solid reconstruction failed for $id" }
    $shape = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
    if ($shape.faces -ne $expected[$id].faces -or $shape.volume_mm3 -le 0) { throw "Unexpected topology for $id" }
    for ($axis = 0; $axis -lt 3; $axis++) {
        if ([Math]::Abs($shape.dimensions_mm[$axis] - $expected[$id].dims[$axis]) -gt 0.001) {
            throw "Unexpected dimensions for $id"
        }
    }
    if ($id -eq 'I01' -and ($shape.edges -ne 12 -or [Math]::Abs($shape.volume_mm3 - 1000) -gt 1e-8)) {
        throw 'I01 planar baseline changed'
    }
    $tangentVolume = 32000 - [Math]::PI * (100 - 25) * 5
    if ($id -eq 'I02' -and ($shape.edges -ne 21 -or [Math]::Abs($shape.volume_mm3 - $tangentVolume) -gt 1e-6)) {
        throw 'I02 tangent cut geometry changed'
    }
    if ($id -eq 'I05' -and ($shape.edges -ne 15 -or [Math]::Abs($shape.volume_mm3 - 3901.825229575319) -gt 1e-6)) {
        throw 'I05 drilled solid or seam geometry changed'
    }
    if ($id -eq 'I04' -and ($shape.bodies.Count -ne 2 -or $shape.bodies[0].faces -ne 6 -or
        $shape.bodies[1].faces -ne 3 -or [Math]::Abs($shape.bodies[0].volume_mm3 - 4000) -gt 1e-6 -or
        [Math]::Abs($shape.bodies[1].volume_mm3 - 125 * [Math]::PI) -gt 1e-6)) {
        throw 'Unexpected I04 body count, topology, or individual volume'
    }
}
```

After the probe passes, rebuild and stage the viewer from the x64 Visual Studio developer shell. Open I02 through the Inventor Part filter. Use a section plane perpendicular to the Y axis inside the cut, for example at Y = 2.5 mm. The section should show a ring-shaped void tangent to the two Z faces and a solid central core.

```powershell
cmake --build out/build/ninja_release_vcpkg --target ModelViewer --parallel 2
$stage = Join-Path (Get-Location) 'out/inventor-research/viewer-tangent'
cmake --install out/build/ninja_release_vcpkg --prefix $stage
```

The Release build and staging succeeded. All nine repository GUI-free CTests passed in 169.57 seconds on the tangent-cut pass. The standalone probe exercises native Inventor reconstruction. At that stage G01 failed OCCT validation, and G02 reached an unsupported spline surface.

```powershell
$deps = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed).Path
$toolchain = (Resolve-Path -LiteralPath vcpkg/scripts/buildsystems/vcpkg.cmake).Path
$testBuild = 'out/inventor-research/ctest-build'
$targets = @('import_trace_tests', 'result_tests', 'plot3d_tests', 'mvf_tests', 'pdf3d_tests',
    'exploded_view_tests', 'threedxml_tests', 'jt_tests', 'parasolid_xt_tests')
cmake -S . -B $testBuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DMV_BUILD_TESTS=ON -DVCPKG_MANIFEST_INSTALL=OFF `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DVCPKG_INSTALLED_DIR=$deps"
cmake --build $testBuild --target @targets --parallel 4
$env:PATH = "$(Join-Path $deps 'x64-windows/bin');$env:PATH"
ctest --test-dir $testBuild --output-on-failure --output-log out/inventor-research/tangent-ctest.log
```

## I02 tangent cut experiment

The I02 audit shows two edges with four coedges each. For each edge, two uses join the cylindrical cavity faces and the other two form a closed, two-coedge loop on an exterior plane. Each planar loop retraces the same segment in opposite directions and encloses zero area. The analytic source describes a 40 × 40 × 20 mm block and a 5 mm deep annular void with 10 mm outer and 5 mm inner radii. The outer cylinder touches the exterior on two lines.

In the local experiment, the exterior and cavity faces were sewn separately. The exterior's two retraced loops were omitted. After sewing the cavity, OCCT reported bad subshape orientation. `ShapeFix_Shape` repaired the cavity at the existing `1e-5` mm precision and maximum tolerance. The exterior and reversed cavity then formed an OCCT solid that passed `BRepCheck_Analyzer`. The solid measured 40 × 40 × 20 mm with 11 faces, 21 edges and 30,821.902754904 mm³, matching the analytic volume. A separate OCCT primitive control built with a box and an annulus gave the same volume, both with independent shells and with an OCCT Boolean cut. These checks establish feasibility for this geometry. The local source experiment used record IDs to separate the faces. A general importer still needs to recognize those loops and classify the resulting shells from topology and geometry. Volume and bounds checks alone do not verify every individual trim.

The importer now detects the pattern from the saved topology. It checks that each four-use edge has one two-coedge retraced loop on a plane, with two remaining uses on faces of a shared circular cylinder. It checks line tangency and requires the remaining topology to separate into two connected shells. It sews and validates each shell independently, checks containment with an OCCT Boolean, and builds the solid with the cavity shell reversed. Cavity repair disables changes to 3D wire geometry and topology and uses the existing tolerance limit. The tracked probe above exercises the imported solid, and the profile command reproduces the original loop incidence. This pass qualifies one exterior and one tangent cavity. Other non-manifold edge patterns remain outside this profile.

On 19 November 2026 the Release probe passed I01 through I05, and the owner confirmed that I02 loads in the staged viewer. A local OCCT check linked to the importer verified two shells, triangulation on all 11 I02 faces, and the expected area of every face. Point classification confirmed solid material in the center and outside the annulus, a void within the annulus at mid-depth, and boundary points on both tangent lines. The local check uses the already configured scratch build and writes its report under `out/`.

```powershell
$verify = 'out/inventor-research/tangent-verify'
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
cmake --build "$verify/build"
& "$verify/build/inventor_compound_probe.exe" '<local-control-folder>/I02.ipt' > "$verify/I02-validation.log"
if ($LASTEXITCODE -ne 0) { throw 'I02 geometry or tessellation check failed' }
```

The commands below rerun the original local experiments from the x64 Visual Studio developer shell. The experimental sources and their build outputs remain under the ignored `out/` folder.

```powershell
$experiment = 'out/inventor-research/tangent-experiment'
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
cmake --build "$experiment/build"
& "$experiment/build/inventor_compound_probe.exe" --part-solid '<local-control-folder>/I02.ipt' > "$experiment/I02-two-shell.json"
cl.exe /nologo /EHsc /std:c++17 /MD /DNOMINMAX "/I$qt/include/opencascade" "/Fo$experiment/occt-tangent.obj" `
    "/Fe$experiment/occt-tangent.exe" "$experiment/occt-tangent.cpp" /link "/LIBPATH:$qt/lib" `
    TKernel.lib TKMath.lib TKG3d.lib TKGeomBase.lib TKBRep.lib TKTopAlgo.lib TKPrim.lib TKBO.lib
& "$experiment/occt-tangent.exe" > "$experiment/occt-tangent.log"
```

## Production part comparison on 19 November 2026

G01 is the first complex paired part. IPT SHA-256 `29d751c48584f1138092b1d1d5c3622cd5476a0cb008277379daea916ecc011e`. STEP SHA-256 `c2e69a428390f13fed00b477f0cbf69fc0f73cd730baa83a57597b52db09fccb`. The STEP reference is a separate input to OCCT 8.0.1 for comparison. The IPT is read directly through the ModelViewer importer.

The reference contains one valid solid with 98 faces and dimensions of 38.6 × 38.6 × 6.3 mm. The original reader rejected G01 at solid validation. An exploratory whole-body repair produced a valid solid with the same face count and dimensions, but its adaptive volume was 3,002.314688 mm³ against the reference's 2,996.550841 mm³. Its face areas and centroids isolated the mismatch to three toroidal bands with negative saved minor radii. These bands had been trimmed on the complementary meridian arc.

Rerun the comparison using the local scratch sources and x64 Visual Studio developer shell. The two programs read the independent inputs and emit the face measurements to ignored logs.

```powershell
$scratch = 'out/inventor-research/gearbox-check'
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$env:LIB = "$(Join-Path $qt 'lib');$env:LIB"
cmake -S $scratch -B "$scratch/build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
cmake --build "$scratch/build"
& "$scratch/build/reference_probe.exe" '<local-G01-reference>.stp' > "$scratch/G01-step-areas.log"
& "$scratch/build/inventor_compound_probe.exe" --part-solid '<local-G01-native>.ipt' > "$scratch/G01-ipt-metrics.json" 2> "$scratch/G01-ipt-areas.log"
```

### Native wheel validation

The importer now constructs toroidal bands that have two isolated, full circular boundaries. It checks that the circles are coaxial and on the saved torus, then uses the saved coedge directions and face sense to select the correct meridian interval, including intervals that cross a periodic boundary. Adjacent coaxial bands use the same physical meridian even when their axes point in opposite directions. For a solid whose toroidal faces all have explicit trims, the importer can repair remaining OCCT parameter-space curves and orientations with the existing tolerance limit, geometry and topology modification flags disabled for wires, and checks that the shell and face counts are preserved. The final OCCT solid must pass validation.

G01 now imports as one solid with 98 faces and 257 edges. The wheel verification program checks absolute bounding-box corners against the separate STEP reference within `1e-6` mm and matches each of the 98 faces by surface type, area within `1e-5` mm² and centroid within `1e-5` mm. Fixed-order integration agrees within `1e-6` mm³. Adaptive integration at requested relative error `1e-9` differs by about `4.5e-5` mm³, or `1.5e-8` relative. All 98 faces tessellate with the viewer's default mesh settings. The standalone probe passed I01–I05 again. All nine repository CTests passed in 171.96 seconds. On 19 November 2026 the owner confirmed that G01 loads and looks correct compared with the STEP geometry. The native IPT view uses the loader's uniform gray material. The STEP view shows a yellow wheel and black rim. Native appearance mapping is outside the verified geometry scope.

Run from the x64 Visual Studio developer shell. The optional comparison harness is configured locally under ignored `out/`. Import in the viewer uses the original IPT directly.

```powershell
$verify = 'out/inventor-research/wheel-verify'
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$env:LIB = "$(Join-Path $qt 'lib');$env:LIB"
cmake --build out/inventor-research/compound-build
& out/inventor-research/compound-build/inventor_compound_probe.exe --part-solid '<local-G01-native>.ipt' `
    > out/inventor-research/gearbox-check/G01-native-wheel.json
cmake --build "$verify/build"
& "$verify/build/inventor_compound_probe.exe" '<local-G01-native>.ipt' '<local-G01-reference>.stp' `
    > "$verify/G01-wheel-verification.log"
cmake --build out/build/ninja_release_vcpkg --target ModelViewer --parallel 2
$stage = Join-Path (Get-Location) 'out/inventor-research/viewer-wheel'
cmake --install out/build/ninja_release_vcpkg --prefix $stage
ctest --test-dir out/inventor-research/ctest-build --output-on-failure --output-log out/inventor-research/wheel-ctest.log
```

## Provenance

This pass consulted CAD Convert at `a3cbe9f5f41acff810b5006d964d116e805e042b` (`Brep.cs`, `Step.cs`, `Assemblies.cs`, `Nodes.cs` and `Text.cs`), Inventor-generated control reports, the supplied sample bytes and the public OCCT API headers installed with OCCT 8.0.1. The upstream reader informed field-layout questions. ModelViewer constructs its own OCCT curves, surfaces and topology. Neither CAD Convert nor InventorLoader binaries were executed. No vendor SDK, private specification or disassembly was used. The owner retained visual verification. [Pinned research reference](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b/tools/invre)
