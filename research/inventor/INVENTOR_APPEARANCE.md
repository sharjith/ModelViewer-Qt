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
if ($fallback.face_overrides.Count -ne 0 -or ($fallback.body_rgb -and -not $fallback.style_rgb)) { throw 'Unexpected fallback colors' }
# Since the colour-style fallback (below) a part whose graphics are unreadable takes its colour style's colour, if it has one.
```

## Parts whose graphics save no colour: the part's colour style (21 November 2026)

**Observation.** In an assembly exported by Inventor to AP214 STEP (paired reference, used only for comparison), every solid carries one colour, 6 distinct values in all. The 9 part files of that assembly that were shipped (P1 to P9 here) save no colour in their graphics segment, so the reader showed them in the default grey, while Inventor shows, and its STEP carries, 0.184 (five of the nine), 0.02 (two), 0.0 (one) and 0.749 (one).

**Where the colour is saved.** Two places together; both are plain data, no decoder is involved:
1. The part's model-information property stream (a top-level stream of the compound file, text stored as UTF-16) holds `<PartActiveColorStyle><Category>..</Category><StyleName>..</StyleName><Protein><MaterialId>ID</MaterialId>..`. `ID` is the id of an appearance asset ("ACADGen-144", "Plastic-002", "MetalPaint-003", "InvGen-071", ...). A part that was never given an appearance names the library default (`InvGen-071`, style name "Default" in the document's language).
2. The application segment (`PmAppSegment`) lists the appearance assets the document uses. A record is the id (32-bit character count, UTF-16), the 36-character GUID of the asset schema (count, UTF-16), 20 bytes of header, and the colour as four 32-bit floats `r, g, b, 1.0` (offset 20 in all 9 parts; the reader scans the next 60 bytes for the first such quadruple). The segment also keeps the other assets of the document (the default `InvGen-071` at 0.75, material assets), so the colour is looked up by the id, never taken as "the" colour of the file. The record's other fields (texture file names, bump maps, the asset schema) are not read.

**Result.** For P1 to P9 the colour found equals the STEP colour of the same part: five parts 0.184 (one paint appearance), two parts 0.02 (a plastic), one part 0.0 (a gloss paint) and one part 0.75 (the library default, the part has no appearance of its own; the STEP has 0.749, the same 8-bit value 191). Two of the nine parts save more than one appearance asset (one a second grey and a red paint besides its black, the other a plastic besides its gloss paint); the asset the part's style names is the one Inventor shows, so the colour is looked up by that id. (In these two parts the asset listed first in the segment is also the right one; that is not relied on, and a document whose style names a later asset is not in the local samples.) Run over all 90 local part files: 76 have a colour style, and for 70 of them the colour equals what the reader gets with the fallback (graphics colour absent or equal); in 6 the graphics body colour differs from the default-appearance style (colour of the graphics body wins, as before and as the owner approved for the wheel); 14 older files have no colour style and no colour. Unknown: appearance overrides on an occurrence or a face in an assembly, textures, transparency.

**Order of use in the reader.** A face colour from the graphics, then the graphics body colour, then the part's colour style. Parts with a colour in their graphics are unchanged by this.

**Regenerate** (output under `out/`; the probe's `--dump-segments <dir>` writes every stream and each segment's unpacked data for string searches; `--part-appearance` now also prints `style_name`, `style_material_id` and `style_rgb`):
```powershell
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
& $probe --part-appearance '<local-part>.ipt' > out/inventor-research/part-appearance.json
& $probe --dump-segments out/inventor-research/segments/<part> '<local-part>.ipt'
```

Not done: the same lookup for the Content-Center parts that are not shipped (their STEP colour 0.745 and an orange 0.78/0.56/0.11 stay unverified).

## Provenance

This pass used the local G01 bytes, the paired STEP solely for comparison, and public source of InventorLoader at `e94bdf5e29052a0dc7ce6fdf755e956ae507caec`. The consulted modules were `importerReader.py`, `importerSegment.py`, `importerClasses.py`, `importerGraphics.py`, `importerEeScene.py`, `importer_Style.py` and `Acis.py`. The source suggested where to look for record lengths and type tables, graphics nodes and diffuse color. ModelViewer's C++ reader was written independently for the observed byte layout. No external decoder, CAD application, vendor SDK, private specification or disassembly was used. [Pinned public source](https://github.com/jmplonka/InventorLoader/tree/e94bdf5e29052a0dc7ce6fdf755e956ae507caec) · [Importer licensing notes](INVENTOR_IMPORT_FEASIBILITY.md#provenance-and-limits)
