# Autodesk Inventor import feasibility

19 November 2026 · `feature/parasolid-importer` · ModelViewer baseline `52566dd8e2ba56c0a290ecac07f594c966a93708`

19 November 2026 · `feature/inventor3d` started from `eeebd7d77f2aa0482b181ceb01dd06a7f2628248` with the preliminary Inventor work.

## What FreeCAD does

FreeCAD offers native Autodesk Inventor part import through the separate Python workbench **InventorLoader**, installed through Addon Manager. The add-on registers `.ipt` parts and leaves registration for `.iam` assemblies, `.ipn` presentations and `.idw` drawings commented out. FreeCAD documentation also lists `.iv`, the Open Inventor scene format with a similar name. [FreeCAD add-on catalog](https://www.freecad.org/addons.php?lang=en) · [InventorLoader registration](https://github.com/jmplonka/InventorLoader/blob/master/Init.py) · [FreeCAD format table](https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/Import_Export.md)

The `.ipt` reader opens an OLE Compound File with `olefile` and reads Inventor-specific streams and compressed model segments. It offers three strategies. Feature mode attempts to reconstruct editable FreeCAD features. SAT mode uses the embedded ACIS/Autodesk Shape Manager boundary representation to build geometry. STEP mode translates that representation to STEP and imports it into FreeCAD. The converter also calls FreeCAD modules, so extracting it as a standalone tool would require further work. The README describes Inventor 2010 onward and records support for Inventor 2021 in its release history. Those statements provide hypotheses for a version matrix. Compatibility with the user's current Inventor files needs sample evidence. [IPT reader](https://github.com/jmplonka/InventorLoader/blob/master/Import_IPT.py) · [Segment reader](https://github.com/jmplonka/InventorLoader/blob/master/importerReader.py) · [STEP converter](https://github.com/jmplonka/InventorLoader/blob/master/Acis2Step.py) · [Upstream README](https://github.com/jmplonka/InventorLoader/blob/master/README.md)

## GPLv3 and the upstream license

FreeCAD publishes its application under LGPL 2.1 or later. The public IPT import path uses `olefile` to open the compound file and InventorLoader's own Python modules to decode Inventor and ACIS data. The source and dependency declarations reviewed show no Autodesk SDK or runtime in that path and disclose no Autodesk format license. FreeCAD's LGPL and the add-on's license concern their software. They do not grant rights to Autodesk code, confidential specifications or third-party models. [FreeCAD license notice](https://github.com/FreeCAD/FreeCAD/blob/main/src/Doc/LICENSE.html) · [IPT reader](https://github.com/jmplonka/InventorLoader/blob/master/Import_IPT.py) · [Geometry reader](https://github.com/jmplonka/InventorLoader/blob/master/importerSAT.py) · [Dependency declarations](https://github.com/jmplonka/InventorLoader/blob/master/package.xml)

InventorLoader's package metadata identifies version `1.5.1` and declares `LGPL-3.0-or-later`. The repository's `LICENSE` contains GPLv2 and GitHub labels the repository `GPL-2.0`. This discrepancy needs clarification from the rights holders for the exact files and revision proposed for reuse, including dependencies. GPLv2-only code is incompatible with a combined GPLv3 distribution. If the LGPL-3.0-or-later grant is confirmed for the relevant files, that removes the version conflict subject to the applicable license obligations. A C++ translation of the upstream parser would still have to comply with the original code's license. An independently installed general-purpose converter exchanging ordinary STEP files is a practical integration boundary. The FSF evaluates whether two programs form a combined work based on how they interact. A separate process alone does not settle that question. [Package metadata](https://github.com/jmplonka/InventorLoader/blob/master/package.xml) · [Repository license](https://github.com/jmplonka/InventorLoader/blob/master/LICENSE) · [FSF license compatibility](https://www.gnu.org/licenses/gpl-faq.en.html#v2v3Compatibility) · [FSF guidance on aggregation](https://www.gnu.org/licenses/gpl-faq.en.html#MereAggregation)

## Implementation options

**Owner requirement, 19 November 2026.** ModelViewer's Inventor import must work without a FreeCAD installation. InventorLoader remains a reference for understanding the format. The self-contained reader is the recommended implementation path. Apprentice Server is a separate Windows option if an Autodesk runtime is acceptable.

**Owner direction, 19 November 2026.** Write ModelViewer's own Inventor reader. Use public reference implementations to identify questions about the format, then establish the stream layouts, units and geometry against authorized samples. The current work uses no InventorLoader code. The production reader is intended to run without FreeCAD or an Autodesk runtime.

**Importer pipeline.** Read IPT directly into ModelViewer. Convert decoded boundary-representation records into OCCT geometry and topology in memory, then use ModelViewer's existing CAD tessellation and scene pipeline. If a usable stored mesh is qualified, its triangles can go straight to the scene pipeline. Use STEP exports only as external validation references. Neither FreeCAD nor a STEP conversion stage is part of the importer.

| Goal | Path | Scope |
| --- | --- | --- |
| View Inventor geometry with the current viewer | Export STEP from Inventor | Uses the existing ModelViewer STEP pipeline. For IAM, export with all referenced components available. Autodesk's Inventor 2025 documentation lists AP242 for parts and says it is unavailable for assemblies. AP214 is the assembly baseline for that release. Verify the choices in the release used for the export. |
| Open IPT and IAM directly on Windows with an installed Autodesk runtime | Evaluate a standalone Apprentice Server converter that reads assembly occurrences and saved display facets and writes GLB | Autodesk documents access to references, occurrence structure and geometry. Check availability and fidelity of stored facets and appearances on target versions. Convert API positions and occurrence translations from centimeters to glTF meters. Qualify nested transforms, repeated instances and missing-reference handling. Confirm Autodesk terms before adopting or distributing the integration. This route supplies mesh geometry. |
| Ship a self-contained open-source reader | Build an independent, scoped geometry reader after qualifying a sample corpus, starting with one tested IPT version | Investigate OLE streams and whether stored display tessellation is usable. For exact geometry, decode the embedded ASM/ACIS representation and construct OCCT shapes directly. Add IAM after qualifying external reference resolution and occurrence transforms. The existing Parasolid X_T reader handles a different kernel format. |

[Autodesk Inventor export documentation](https://help.autodesk.com/cloudhelp/2025/ENU/Inventor-Help/files/GUID-68FCAD0E-93BD-4D90-B9FE-9CF82AA4E64D.htm) · [Autodesk STEP color export](https://www.autodesk.com/support/technical/article/caas/sfdcarticles/sfdcarticles/What-are-the-Inventor-Save-As-STEP-Export-Options-stp-ste-step.html) · [Apprentice Server 2026](https://help.autodesk.com/cloudhelp/2026/ENU/Inventor-API/files/Apprentice_Overview.htm) · [Saved display facets](https://help.autodesk.com/cloudhelp/2026/ENU/Inventor-API/files/SurfaceBody_GetExistingFacets.htm) · [Autodesk API units](https://help.autodesk.com/cloudhelp/2025/ENU/Inventor-API/files/UnitsTypeEnum.htm)

Start with a read-only IPT inventory. Inspect authorized samples for stored display geometry and embedded ASM/ACIS data. Establish whether stored geometry covers every expected body and includes enough information to recover units, normals, and appearances. If it does, map the decoded meshes into the existing scene pipeline and qualify a narrowly scoped IPT importer. If the file only yields a usable B-rep, the next feasibility question is whether the supported ASM/ACIS entities can be translated to OCCT shapes with reliable trims and topology. A STEP export from Inventor serves as the geometric reference for either path. [Format registration](../../src/App/ModelViewerApplication.cpp#L97) · [Loader dispatch](../../src/Import/AssImpModelLoader.cpp#L1358) · [STEP reader](../../src/Import/XCAFSTEPProcessor.cxx#L402)

## Evidence required before implementation

The local corpus below includes two candidate native/reference pairs. When the importer produces geometry, open both files in ModelViewer and compare units, physical dimensions, orientation, body count, colors and curved silhouette. A matched current-version export with several bodies or per-face colors would extend coverage. For IAM evaluation, add a nested assembly with repeated occurrences and a copy with one deliberately missing reference. Compare occurrence counts, names, transforms, visibility and diagnostics with an Inventor reference export or the source application.

### GrabCAD native and STEP pairs

19 November 2026. The owner supplied two IPT files with same-stem STEP exports from a [GrabCAD library entry](https://grabcad.com/library/dc-motor-with-gearbox-1). Both STEP headers declare `AUTOMOTIVE_DESIGN` (AP214) and identify Autodesk Inventor 2015. That identifies the STEP exporter. The native saved version and whether the two representations agree geometrically remain unverified. Each STEP contains one `MANIFOLD_SOLID_BREP` entity and zero `NEXT_ASSEMBLY_USAGE_OCCURRENCE` entities.

| ID | IPT bytes | IPT SHA-256 | STEP SHA-256 | STEP `ADVANCED_FACE` entities |
| --- | ---: | --- | --- | ---: |
| G01 | 378,368 | `29d751c48584f1138092b1d1d5c3622cd5476a0cb008277379daea916ecc011e` | `c2e69a428390f13fed00b477f0cbf69fc0f73cd730baa83a57597b52db09fccb` | 98 |
| G02 | 970,752 | `9eb6cb5ae8989a6b4cb7c03d419ef4bf575d8539e3a059e135985b689ff3cb6e` | `790886e9a89bed023e0470d9fd58e96361399337d3a6df45ae4d7702a993e66b` | 372 |

The strict compound-file inventory succeeded on both parts. Each has 30 streams, 23 under `RSeStorage`. The four CAD Convert controls also opened successfully and each has 21 streams under `RSeStorage`. This is directory evidence. It does not establish which streams carry usable geometry or which layouts the files share.

To regenerate the observations, set the two local sample directory placeholders and the Python executable. Use `olefile` 0.47 in the research environment (`& $python -m pip install --no-compile --target out/inventor-research/python olefile==0.47` from the repository root). The source files remain in the external sample folders.

```powershell
$python = '<research-python>/python.exe'
$controls = '<local-samples>/inventor-samples'
$pairFolder = '<local-pair-folder>'
$env:PYTHONPATH = (Resolve-Path -LiteralPath out/inventor-research/python).Path
$parts = @(Get-ChildItem -LiteralPath $pairFolder -Filter '*.ipt' -File | Sort-Object Length)
$samples = [ordered]@{ G01 = $parts[0].FullName; G02 = $parts[1].FullName }
foreach ($id in 'I01', 'I02', 'I03', 'I05') { $samples[$id] = Join-Path $controls "$id.ipt" }
foreach ($id in $samples.Keys) {
    $inventory = & $python -B tools/inventor/inspect_compound.py $samples[$id]
    if ($LASTEXITCODE -ne 0) { throw "Compound inventory failed for $id" }
    $inventory | Set-Content -LiteralPath "out/inventor-research/$id-streams.json" -Encoding utf8
}
$summary = foreach ($id in 'G01', 'G02') {
    $step = [IO.Path]::ChangeExtension($samples[$id], '.stp')
    $body = Get-Content -Raw -LiteralPath $step
    $counts = [ordered]@{}
    foreach ($entity in 'ADVANCED_FACE', 'MANIFOLD_SOLID_BREP', 'NEXT_ASSEMBLY_USAGE_OCCURRENCE') {
        $counts[$entity] = [regex]::Matches($body, "(?m)^\s*#\d+\s*=\s*$entity\s*\(").Count
    }
    [pscustomobject]@{
        id = $id
        ipt_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $samples[$id]).Hash
        step_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $step).Hash
        schema = [regex]::Match($body, 'FILE_SCHEMA\s*\(\(\s*''([^'']+)''').Groups[1].Value
        inventor_header = [regex]::Match($body, 'Autodesk Inventor[^''\r\n]*').Value
        entities = $counts
    }
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath out/inventor-research/grabcad-step-inventory.json -Encoding utf8
```

### Assembly candidate G03

19 November 2026. The owner supplied an IAM with same-stem STEP and Parasolid X_T exports. The package contains one IAM and no IPT files or additional IAM files. A raw UTF-16 string scan of the IAM surfaced several `.ipt` names and a different `.iam` name. The scan does not establish the active reference graph, but the package cannot qualify complete component resolution. The compound inventory found 34 streams, including `CacheGraphics/OGSCache/OGS/DefaultScene/streams` at 448,979 bytes. This is a candidate for saved assembly display geometry. Its payload has not been decoded. The AP214 STEP header names Inventor 2021 as its exporter. The STEP text contains 13 `MANIFOLD_SOLID_BREP`, 25 `NEXT_ASSEMBLY_USAGE_OCCURRENCE` and 630 `ADVANCED_FACE` entities. The native saved version and agreement between the exports remain unverified.

| G03 input | SHA-256 |
| --- | --- |
| IAM | `af0e9d448de739596c9b5d0f0a66bef99694b9fe0dd49e0873ad0aada58795d1` |
| STEP | `82a49fa44c11727303864a7357fc84d78583b8268a8fe56ffcce956b49f3ccf1` |
| X_T | `15360b075d2e0f9e5a732e6e0e11a49e290d918fe42cdfc0baaad25e9dfe7721` |

To reproduce, use the research Python and `PYTHONPATH` setup above. Fill in the external assembly folder placeholder.

```powershell
$folder = '<local-assembly-folder>'
$iam = (Get-ChildItem -LiteralPath $folder -Filter '*.iam' -File).FullName
$inventory = & $python -B tools/inventor/inspect_compound.py $iam
if ($LASTEXITCODE -ne 0) { throw 'Compound inventory failed for G03' }
$inventory | Set-Content -LiteralPath out/inventor-research/G03-streams.json -Encoding utf8
$bytes = [IO.File]::ReadAllBytes($iam)
$names = foreach ($offset in 0, 1) {
    $decoded = [Text.Encoding]::Unicode.GetString($bytes, $offset, $bytes.Length - $offset)
    [regex]::Matches($decoded, '[\p{L}\p{N} _().-]{1,90}\.(?:ipt|iam)\b', 'IgnoreCase') |
        ForEach-Object Value
}
$names | Sort-Object -Unique | Set-Content -LiteralPath out/inventor-research/G03-reference-name-candidates.txt -Encoding utf8
$step = (Get-ChildItem -LiteralPath $folder -Filter '*.stp' -File).FullName
$body = Get-Content -Raw -LiteralPath $step
foreach ($entity in 'ADVANCED_FACE', 'MANIFOLD_SOLID_BREP', 'NEXT_ASSEMBLY_USAGE_OCCURRENCE') {
    "$entity = " + [regex]::Matches($body, "(?m)^\s*#\d+\s*=\s*$entity\s*\(").Count
}
Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.Extension -in '.iam', '.stp', '.x_t' } |
    Get-FileHash -Algorithm SHA256
```

### Large part and nested assembly candidates G04 and G05

19 November 2026. The owner supplied two additional packages. Both selected native files opened with the strict compound inventory tool. G04 is a large single-file IPT suited to later scale testing. Its two STEP candidates differ in schema and solid entity count, so keep their identities separate. G05 is the first assembly package in this research that includes a hierarchy of native companion files. It contains seven IAM files (including two in `OldVersions`) and sixteen IPT files. The 2,170,166-byte largest stream under `CacheGraphics` in the top-level IAM is a saved-display candidate. Full native reference resolution, saved versions and geometric agreement are still unverified.

| Input | Bytes | SHA-256 | Observation |
| --- | ---: | --- | --- |
| G04 IPT | 311,348,224 | `eb339fe38c607f8403bd3c6094a0227f713c71a7f62421d20f3f612c00b290ad` | 30 streams, no `CacheGraphics` directory |
| G04 STEP A | 250,128,755 | `3b9776e93ff5d1d1d418ec80cfaaaa1fecff67626580bc34ec46e56782d06169fc` | Same-stem AP214, Inventor 2026 exporter, 58 solid B-rep entities |
| G04 STEP B | 378,852,644 | `a020740a646df65d8a13949ff8394b4be1abf135fdff7adb4ac3437931c35a6c` | AP203, 52 solid B-rep entities |
| G05 IAM | 2,364,928 | `2bf45f6f8facc12cda4dc6592e0712f2f0d12f0196c9a42eda898920e39b8c8c` | 35 streams, four under `CacheGraphics` |
| G05 STEP | 8,721,604 | `e6f79f7a2c7e4e03c06eddd7b5aff3be301f35d156b2029f8077dfc92d0883a3` | Same-stem AP214, Inventor 2025 exporter, 25 solid B-rep entities and 39 occurrence entities |

Run from the repository root with the research Python and `PYTHONPATH` setup above. The placeholders are the two external package directories.

```powershell
$folders = [ordered]@{ G04 = '<local-large-part-folder>'; G05 = '<local-nested-assembly-folder>' }
$extensions = @{ G04 = '*.ipt'; G05 = '*.iam' }
foreach ($id in $folders.Keys) {
    $native = Get-ChildItem -LiteralPath $folders[$id] -Recurse -File -Filter $extensions[$id] |
        Sort-Object Length -Descending | Select-Object -First 1
    $inventory = & $python -B tools/inventor/inspect_compound.py $native.FullName
    if ($LASTEXITCODE -ne 0) { throw "Compound inventory failed for $id" }
    $inventory | Set-Content -LiteralPath "out/inventor-research/$id-streams.json" -Encoding utf8
    Get-ChildItem -LiteralPath $folders[$id] -Recurse -File -Filter '*.stp' | ForEach-Object {
        Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName
        Get-Content -LiteralPath $_.FullName -TotalCount 35 | Select-String 'FILE_SCHEMA|Autodesk Inventor'
        rg --count-matches '^\s*#\d+\s*=\s*MANIFOLD_SOLID_BREP\s*\(' -- $_.FullName
        rg --count-matches '^\s*#\d+\s*=\s*NEXT_ASSEMBLY_USAGE_OCCURRENCE\s*\(' -- $_.FullName
    }
}
```

### Initial Inventor 2026 corpus

Four native files and their paired JSON reports were downloaded from the public CAD Convert corpus on 19 November 2026. Upstream identifies the files as generated with Inventor 2026 and provides the JSON reports from Inventor. The reports contain parameters, sketches and feature properties. They provide expected construction and dimensions, with STEP geometry comparison still pending. The native files matched the upstream byte lengths and had the OLE Compound File signature. Local file hashes were verified against the acquisition manifest. No geometry was decoded. [CAD Convert, pinned revision](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b)

| ID | Geometry described by the report | IPT SHA-256 |
| --- | --- | --- |
| I01 | 20 × 10 mm rectangle extruded 5 mm | `83ace091378834d2abe28124b2cb96ca18fe354965cffcb43be1e8301d35cf57` |
| I02 | Extrusion with full revolution cut | `f0d95737cb0c132057fc9f2a4b3afada9b8126131603720ef5ae65687ba1488a` |
| I03 | Extrusion with 2 mm edge fillet | `c34a648312ff221e7cecb1d3454ce664b918dae3268f01caabc0771ab2b7b824` |
| I04 | Two separate new-body extrusions | `d28be3c50bdfde6ea4450957d581f6fb0c2001f251c2e12db298d8b1b9662cbc` |
| I05 | Extrusion with 5 mm drilled hole and angled tip | `ec36a2fc5bd6926376462adb64cfe88705ec323a89f6f3c0ca41914e47191389` |

I04 was reserved for a multibody control and was skipped in the initial selection. On 19 November 2026, a fifth control was downloaded from the same pinned CAD Convert revision. Its paired report records two new-body extrusion operations. The raw saved profile confirms two body records. [Multibody profile and reproduction](INVENTOR_ANALYTIC_GEOMETRY.md#observed-sample-profiles)

The files and `source-manifest.json` are stored in the owner's external CAD data folder. The manifest records immutable download URLs, lengths and SHA-256 values for both the native files and the reports. To reproduce the hash checks after fetching the manifest's URLs, run the following from the repository root with the external sample directory filled in.

```powershell
$sampleRoot = '<local-samples>/inventor-samples'
$manifest = Get-Content -Raw -LiteralPath (Join-Path $sampleRoot 'source-manifest.json') | ConvertFrom-Json
foreach ($sample in $manifest.samples) {
    foreach ($file in $sample.files) {
        $path = Join-Path $sampleRoot $file.file
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash -ne $file.sha256) {
            throw "Sample hash mismatch for $($file.file)"
        }
    }
}
```

Run the following PowerShell commands from the repository root to identify the original file and reference export. Replace the placeholder with the local path of the authorized source. Share neutral IDs and hashes in tracked research.

```powershell
New-Item -ItemType Directory -Force out/inventor-research | Out-Null
Get-FileHash -Algorithm SHA256 -LiteralPath '<local-samples>/I01.ipt', 'out/inventor-research/I01-inventor.step' |
    ForEach-Object { $_.Hash } |
    Set-Content -LiteralPath out/inventor-research/I01-sha256.txt
```

The [compound inventory tool](../../tools/inventor/inspect_compound.py) records the file hash and stream directory with declared sizes, without opening the stream payloads. A stream name can suggest what to examine next. Establishing geometry coverage requires examining the payload and comparing it to the reference export. Run with Python 3 and `olefile==0.47` available in the research environment. The output may contain sample-specific stream names and stays local.

```powershell
$python = '<research-python>/python.exe'
New-Item -ItemType Directory -Force out/inventor-research | Out-Null
& $python tools/inventor/inspect_compound.py '<local-samples>/I01.ipt' > out/inventor-research/I01-streams.json
```

## Provenance and limits

**CAD Convert as a research reference, 19 November 2026.** The upstream README describes `tools/invre` as a C# reader that decodes `.ipt` and `.iam` directly, emits a neutral model, and converts the stored B-rep to STEP before FreeCAD reconstructs editable features. This makes the reader and corpus relevant to ModelViewer's investigation of storage records, saved geometry, topology and assembly placement. Upstream reports testing Inventor 2023 and 2026. A STEP produced by `invre` can serve as a second implementation's comparison output. An Inventor-exported STEP still provides the independent CAD reference. This pass reviewed the upstream description. Detailed reader source inspection and converter execution remain pending. [CAD Convert pinned source and README](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b)

This pass used the linked public InventorLoader `master` source and documentation (package metadata version `1.5.1`, dated 12 December 2024), FreeCAD public documentation, Autodesk public Inventor 2025 and 2026 documentation, FSF guidance, and the ModelViewer checkout identified above. The upstream commit hash was unavailable from the web endpoints checked. An exact revision and dependency manifest should be recorded before implementation. This was source and documentation research. FreeCAD, Inventor, Apprentice Server and third-party decoders were not executed, and no vendor SDK, private specification, disassembly, sample or generated export was used. There are no measured compatibility results or owner acceptance for Inventor import. This record documents the method and the licensing question.

The independent inventory tool uses the public API documented for `olefile` 0.47. Upstream publishes BSD-style terms and the inherited PIL notice. The package is a separate research dependency and is absent from the viewer build and distribution. The tool reads generic compound storage metadata and includes no Inventor-specific decoding. The CAD Convert acquisition used its sample data and reports. Its parser was neither used nor incorporated. The GrabCAD pass used only directory metadata and STEP text inspection. [olefile 0.47 API](https://olefile.readthedocs.io/en/latest/Howto.html) · [olefile license and notices](https://olefile.readthedocs.io/en/latest/License.html)

## Native compound-file reader and owner verification

The first native C++ layer reads the generic container header, FAT and DIFAT, directory tree, mini FAT and stream payloads. It handles the CFB v3 and v4 sector layouts and reads requested streams from disk with a caller-supplied byte limit. The implementation was written from the Microsoft MS-CFB public specification (revision 13.0, 26 October 2026). The Python tool is a separate cross-check for stream paths, lengths and hashes. This container implementation pass used no CAD Convert or InventorLoader decoder source or binaries and preceded viewer registration. [MS-CFB specification](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/53989ce4-7b05-4f8d-829b-d08d6148375b) · [Native reader](../../src/Import/Inventor/CompoundFile.cpp)

On 19 November 2026 the owner authorized the standalone test runs. The probe built in Release with MSVC 19.52 and the existing Qt installation. Stream paths, sizes and payload hashes matched `olefile` for I01, I02, I03, I05, G01, G02 and G04 (30 streams each), G03 (34) and G05 (35). G04 exercises 37 DIFAT sectors and contains 308,885,828 bytes of stream payloads. CFB v4 remains unqualified.

The following commands reproduce the agreed three-file gate from an x64 Visual Studio developer shell with Ninja and the existing Qt installation. Add the other local native controls to `$samples` to reproduce the extended run. Fill in the sample paths and the research Python path with `olefile` available.

```powershell
$qt = (Resolve-Path -LiteralPath out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows).Path
cmake -S tools/inventor -B out/inventor-research/compound-build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
if ($LASTEXITCODE -ne 0) { throw 'Probe configuration failed' }
cmake --build out/inventor-research/compound-build
if ($LASTEXITCODE -ne 0) { throw 'Probe build failed' }
$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
$env:PYTHONPATH = (Resolve-Path -LiteralPath out/inventor-research/python).Path
$python = '<research-python>/python.exe'
$samples = [ordered]@{
    I01 = '<local-samples>/I01.ipt'
    G03 = '<local-assembly-file>.iam'
    G04 = '<local-large-part-file>.ipt'
}
foreach ($id in $samples.Keys) {
    $nativeFile = "out/inventor-research/$id-native-probe.json"
    $pythonFile = "out/inventor-research/$id-olefile-probe.json"
    & out/inventor-research/compound-build/inventor_compound_probe.exe --hash-streams $samples[$id] > $nativeFile
    if ($LASTEXITCODE -ne 0) { throw "Native probe failed for $id" }
    & $python -B tools/inventor/inspect_compound.py --hash-streams $samples[$id] > $pythonFile
    if ($LASTEXITCODE -ne 0) { throw "Python inventory failed for $id" }
    $native = (Get-Content -Raw -LiteralPath $nativeFile | ConvertFrom-Json).streams
    $reference = (Get-Content -Raw -LiteralPath $pythonFile | ConvertFrom-Json).streams
    $nativeRows = @($native | Select-Object path, size_bytes, sha256 | ForEach-Object { $_ | ConvertTo-Json -Compress })
    $referenceRows = @($reference | Select-Object path, size_bytes, sha256 | ForEach-Object { $_ | ConvertTo-Json -Compress })
    if (Compare-Object $referenceRows $nativeRows -CaseSensitive) { throw "Stream inventory differs for $id" }
    "$id matched $($native.Count) streams"
}
```

## Saved part B-rep extraction

19 November 2026 · `feature/inventor3d`. The [native segment reader](../../src/Import/Inventor/PartSegment.cpp) locates the length-prefixed `PmBRepSegment` metadata label, follows the paired data stream, and decompresses its payload with zlib or Zstandard under a byte limit. CAD Convert's `Segments.cs`, `Codec.cs` and `Brep.cs` at the [pinned revision](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b/tools/invre) were consulted to identify the envelope and marker. The label, compression selectors and marker were checked against the six local parts. ModelViewer's code was written independently. CAD Convert and InventorLoader binaries were not run. No STEP conversion or geometry reconstruction was performed.

Native extraction matched the separate Python compound reader for decoded length, SHA-256 and `ASM BinaryFile4` marker offset on all six parts. Python and C++ used the same installed Zstandard runtime for Zstandard payloads, so this compares the extraction paths and their results. G03 was rejected by the part-only segment probe. This pass verified the saved B-rep payload.

| ID | Compression | Decoded bytes | ASM header offset |
| --- | --- | ---: | ---: |
| I01 | Zstandard | 12,065 | 151 |
| I02 | Zstandard | 20,563 | 182 |
| I03 | Zstandard | 21,906 | 182 |
| I05 | Zstandard | 15,028 | 367 |
| G01 | zlib | 295,644 | 1,044 |
| G02 | zlib | 1,289,846 | 3,619 |

Run after the probe build above with the same Python and Qt environment. Substitute the actual paths of the native input files for the placeholders.

```powershell
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
$checkArgs = @('--probe', $probe, '--zstd-library', (Join-Path $qt 'bin/zstd.dll'))
foreach ($id in 'I01', 'I02', 'I03', 'I05', 'G01', 'G02') {
    $checkArgs += @('--sample', $id, "<local-samples>/$id.ipt")
}
& $python -B tools/inventor/verify_part_segment.py @checkArgs > out/inventor-research/part-brep-verification.json
if ($LASTEXITCODE -ne 0) { throw 'Saved B-rep extraction comparison failed' }
& $probe --part-brep '<local-assembly-file>.iam'
if ($LASTEXITCODE -eq 0) { throw 'Expected the part-only reader to reject G03' }
```

## First native planar solid

19 November 2026 · `feature/inventor3d`. The [native geometry layer](../../src/Import/Inventor/PartSolid.cpp) traverses the saved body's shell, faces, loops, coedges and edge endpoints. It checks that the points agree with their saved planes and straight edge curves, applies the ASM header's millimeter scale, and sews the faces into a closed, oriented OCCT solid. `BRepCheck_Analyzer` must accept the result before it reaches the viewer. This pass compared I01's saved record layout with the observations in `Brep.cs` and `Step.cs` at the previously pinned CAD Convert revision. The ModelViewer reader and OCCT builder were written independently and no reference converter was executed.

I01 reconstructed with extents of 20 × 10 × 5 mm, six faces, 12 edges, and a volume of 1,000 mm³. The construction dimensions agree with the Inventor-generated report. In the first planar build, the revolved cut I02 and edge fillet I03 were rejected by the planar and straight-edge profile check, and the drilled I05 was rejected by the multiple-loop check. The [analytic geometry pass](INVENTOR_ANALYTIC_GEOMETRY.md) expands the profile after this baseline.

Rebuild the standalone probe as described above. Use the same Qt runtime and a local copy of the four inputs to reproduce the geometry checks from the repository root.

```powershell
$controls = '<local-samples>/inventor-samples'
$probe = 'out/inventor-research/compound-build/inventor_compound_probe.exe'
$report = 'out/inventor-research/I01-solid.json'
& $probe --part-solid (Join-Path $controls 'I01.ipt') > $report
if ($LASTEXITCODE -ne 0) { throw 'I01 geometry probe failed' }
$shape = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
if (($shape.dimensions_mm -join ',') -ne '20,10,5' -or $shape.faces -ne 6 -or
    $shape.edges -ne 12 -or [Math]::Abs($shape.volume_mm3 - 1000) -gt 1e-8) {
    throw 'Unexpected I01 geometry'
}
```

The Release viewer was built and staged with its runtime assets. Run these commands from the same Visual Studio developer shell to reproduce the staged build under `out/`.

```powershell
cmake --build out/build/ninja_release_vcpkg --target ModelViewer --parallel 5
cmake --install out/build/ninja_release_vcpkg --prefix out/inventor-research/viewer
```

The staged `bin/ModelViewer.exe` opened I01 through File Open and displayed the rectangular solid. The local trace recorded one converted scene mesh, GPU upload, and a successful viewer return. To reproduce the trace, run the staged executable with `MODELVIEWER_IMPORT_DIAGNOSTICS=1`, open the local I01 file through File Open, and copy that process's `import-trace-*.log` from ModelViewer's logs folder to `out/inventor-research/I01-viewer-import.log`. The standalone probe supplies the dimensions above. The owner visually accepted I01 in the viewer on 19 November 2026.
