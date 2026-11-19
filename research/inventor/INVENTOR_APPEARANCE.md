# Native Inventor part appearance

19 November 2026 · `feature/inventor3d` · geometry checkpoint `81786a9`

The G01 STEP reference showed a yellow part and a black rim during the owner's geometry check. The native generic compound-file inventory and read-only inspection of the legacy graphics records located RGB `(1, 1, 0)` in the part's draw attributes and RGB approximately `(0.02, 0.02, 0.02)` in one primary face style. All 98 graphics face keys for the solid's shell matched the persistent keys of the saved ASM faces. The single dark graphics face also agrees with the separate face appearance attribute in the ASM. The STEP comparison lists yellow `(1, 1, 0)` and black `(5/255, 5/255, 5/255)`. See the [G01 hashes and geometry validation](INVENTOR_ANALYTIC_GEOMETRY.md#production-part-comparison-on-19-november-2026).

The importer reads diffuse RGB for the qualified opaque, single-body graphics profile. A body attribute takes precedence over the part default, and a face style takes precedence over both. It requires exact agreement between graphics and B-rep face keys. It follows the color assignment through OCCT sewing and trim repair to the final face, then passes the same shape and its colors to the existing XCAF tessellation path. Unsupported or malformed appearance data uses the geometry loader's default color. The initial scope covers G01's legacy graphics layout. Multibody appearance, newer graphics layouts, transparency and textures need independent qualification.

The standalone probe resolved one yellow part color and one dark face override on the outer surface, with a face box of approximately 38.6 × 38.6 × 3.0 mm. A local OCCT check confirmed that the two colors are available through the XCAF labels consumed by the tessellator. All six geometry controls and a separate G01 copy with damaged graphics metadata loaded in the probe. The damaged copy produced no color assignments. This copy exercises the fallback. All nine repository CTests passed in 176.01 seconds. On 19 November 2026 the owner approved the native appearance and supplied a screenshot showing the yellow wheel and black rim. The screenshot records color and geometry.

## Reproduce the qualified appearance

Run from the x64 Visual Studio developer shell and substitute the sample path locally.

```powershell
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
cmake --build out/inventor-research/compound-build
& $probe --part-appearance '<local-G01-native>.ipt' > out/inventor-research/G01-appearance.json
if ($LASTEXITCODE -ne 0) { throw 'Native Inventor appearance probe failed' }
$appearance = Get-Content -Raw out/inventor-research/G01-appearance.json | ConvertFrom-Json
if (($appearance.body_rgb -join ',') -ne '1,1,0' -or $appearance.face_overrides.Count -ne 1 -or
    ($appearance.face_overrides[0].rgb | Where-Object { [Math]::Abs($_ - 0.02) -gt 1e-6 })) {
    throw 'Unexpected wheel appearance'
}
cmake --build out/build/ninja_release_vcpkg --target ModelViewer --parallel 2
$stage = Join-Path (Get-Location) 'out/inventor-research/viewer-appearance'
cmake --install out/build/ninja_release_vcpkg --prefix $stage
ctest --test-dir out/inventor-research/ctest-build --output-on-failure --output-log out/inventor-research/appearance-ctest.log
```

The XCAF check can be rerun with the locally configured scratch source and build. Set `$env:LIB` to include the vcpkg `lib` directory before building.

```powershell
$check = 'out/inventor-research/appearance-xcaf'
$env:LIB = "$(Join-Path $qt 'lib');$env:LIB"
cmake --build "$check/build"
& "$check/build/inventor_compound_probe.exe" '<local-G01-native>.ipt' > "$check/G01-xcaf.log"
if ($LASTEXITCODE -ne 0) { throw 'Native Inventor XCAF color check failed' }
```

The local fallback check uses `olefile` 0.47 and keeps its damaged copy under `out/`. Set `$python` and `PYTHONPATH` as in the [container probe instructions](INVENTOR_IMPORT_FEASIBILITY.md#native-compound-file-reader-and-owner-verification).

```powershell
$env:INV_COLOR_SOURCE = '<local-G01-native>.ipt'
$env:INV_COLOR_OUTPUT = 'out/inventor-research/G01-malformed-appearance.ipt'
@'
import olefile
import shutil
from os import environ

source, output = environ["INV_COLOR_SOURCE"], environ["INV_COLOR_OUTPUT"]
shutil.copyfile(source, output)
with olefile.OleFileIO(output, write_mode=True) as file:
    label = "PmGraphicsSegment".encode("utf-16le")
    matches = [path for path in file.listdir() if len(path) == 2 and path[1].startswith("M")
               and label in file.openstream(path).read()[:128]]
    assert len(matches) == 1
    path = matches[0]
    payload = file.openstream(path).read()
    file.write_stream(path, payload[:140] + bytes(len(payload) - 140))
'@ | & $python -B -
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare damaged graphics metadata' }
& $probe --part-appearance $env:INV_COLOR_OUTPUT > out/inventor-research/G01-malformed-appearance.json
if ($LASTEXITCODE -ne 0) { throw 'Geometry load failed when graphics metadata was unreadable' }
$fallback = Get-Content -Raw out/inventor-research/G01-malformed-appearance.json | ConvertFrom-Json
if ($fallback.body_rgb -or $fallback.face_overrides.Count -ne 0) { throw 'Unexpected fallback colors' }
```

## Provenance

This pass used the local G01 bytes, the paired STEP solely for comparison, and public source of InventorLoader at `e94bdf5e29052a0dc7ce6fdf755e956ae507caec`. The consulted modules were `importerReader.py`, `importerSegment.py`, `importerClasses.py`, `importerGraphics.py`, `importerEeScene.py`, `importer_Style.py` and `Acis.py`. The source suggested where to look for record lengths and type tables, graphics nodes and diffuse color. ModelViewer's C++ reader was written independently for the observed byte layout. No external decoder, CAD application, vendor SDK, private specification or disassembly was used. [Pinned public source](https://github.com/jmplonka/InventorLoader/tree/e94bdf5e29052a0dc7ce6fdf755e956ae507caec) · [Importer licensing notes](INVENTOR_IMPORT_FEASIBILITY.md#provenance-and-limits)
