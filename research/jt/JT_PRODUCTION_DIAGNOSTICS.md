# Production 10.9 triage

22 October 2026, `feature/JT-importer`. The owner supplied two screenshots while the independent C2i basic-9.x gate remained in testing. Some production 10.9 parts loaded; one part and an assembly failed. **No failing production file is available locally and no cause-specific decoder fix is claimed.** Private file/assembly names are omitted; use neutral IDs P109-P1 and P109-A1 when collecting evidence.

## What the errors establish

### Owner production isolation — 22 October 2026

The owner supplied three reports from the current feature-branch build (baseline `0034b48`), then described controlled exports from Siemens VisMockup as JT10.9. Both wireframe-object export and PMI export were initially enabled. Disabling those options independently isolated wireframe-object export as the failure trigger in the tested combinations; PMI was not the observed cause. This is owner-reported production evidence, not a locally executed export or universal PMI qualification.

The reports identify three little-endian 10.9 inputs: one 237,402-byte container with segment counts 1:1/4:131/31:1, one 1,448-byte container with 1:1/4:1/31:1, and one 20,253,686-byte container with 45 shape-LOD segments (15 each in 7/8/9) and one XT segment. All three root graphs use compression flag3/algorithm3. The original diagnostic executable did not inventory those LZMA graph payloads, so no unsupported node GUID was recovered. The application already has an LZMA path; the diagnostic limitation is not evidence of an application compression failure. Reports are owner-provided evidence; do not redistribute names/geometry or infer native decoding success from segment counts.

The earlier structure/reference-only or placeholder hypothesis is incomplete in light of the owner's observation. Zero conventional triangle/XT segments does not establish that an input has no wire/reference content. The current scene adapter has no general JT polyline/wireframe display reader and rejects unknown required graph objects; its display path selects triangle LOD segments. This is consistent with the reported wire-export trigger, but the exact production graph/representation type still needs evidence. Triangle-mesh wire rendering in prior gates is a different path from importing actual line/curve entities. No unknown required object is silently skipped as a workaround. Prioritize C3 wire/line representation investigation; retain C2i acceptance independently.

P109-P1 reached the scene builder's final empty-mesh check. That means the traversal produced no supported mesh or validated exact geometry. It does not establish that the input contains no tessellation. Possible explanations include an exact-only part without a usable stored display representation, geometry behind unsupported records, or geometry stored in external resources. The final generic message alone cannot distinguish them.

P109-A1 exhausted usable LOD alternatives, with the final retained failure identifying an unsupported required graph object. The adapter only knows that its object cannot be cast to the supported node base. The existing unknown-object path does not retain the original type GUID, and the displayed message preserves only the latest rejected alternative. Consequently neither the exact unsupported type nor every earlier failure can be inferred from the screenshot. This is a content/representation coverage gap, not evidence that all 10.9 files share an incompatible version header.

No change is made to ignore required graph objects, silently omit assembly parts, claim absent tessellation, or weaken the validated exact/tessellation fallback policy. C2i acceptance is independent of these production reports and remains pending its own owner result.

## Read-only structural report

### Owner-requested Windows executable

The owner confirmed production-only diagnosis and then explicitly requested an executable because Python is difficult to run there. That authorizes packaging and smoke-checking this diagnostic utility only; it does not authorize a ModelViewer/C++ build or change the C2i owner gate.

`out/jt-diagnostics-package/release/JTDiagnostics.exe` is a standalone unsigned Windows x64 build. Double-clicking opens file-selection dialogs for one or more JT inputs, followed by a report-folder dialog. Reports have neutral timestamped names. No Python installation, model export, administrator rights or viewer rebuild is required by this tool. Graph type registrations are bundled so class identification is available away from the source checkout. The shareable `out/jt-diagnostics-package/JTDiagnostics-Windows-x64.zip` includes the executable, source, instructions, build metadata and licence notices; generated binaries stay ignored.

Built using Python 3.14.4 and PyInstaller 6.22.3 in an isolated environment under out/. The packager is a tool-building dependency, not a linked ModelViewer dependency. Python/Tcl/Tk and the PyInstaller bootloader are bundled; notices are included in the ZIP. The installed Tcl runtime is 8.6.15; its separate licence, absent from this Python distribution's Tcl directory, was retained from [the matching public Tcl tag](https://raw.githubusercontent.com/tcltk/tcl/core-8-6-15/license.terms), SHA-256 `c0a69a2bfd757361ec7e6143973b103c90409316b49e9c88db26ad6388e79f16`. This records packaging materials without a new legal opinion or application About change.

Diagnostic source SHA-256: `27f5e848aaea31f2c217bbe7c27868b6f2753e4a3157b24ac086d92717ddaf1f`. Executable SHA-256: `9e03531a4a06e3dca389ed8a76fa46698ac9b823c9d13d893dd0987f8c3a90fe`. The bundled registry has 27 registered classes. The packaged executable was run in command-line mode against the original modern fixture, returned exit code 0, and emitted expected version/segment/type counts with `registry_available=true`. Evidence is `out/jt-diagnostics-package/exe-modern-report.json`. File-selection dialogs have not been manually exercised by the agent, and production input remains unavailable locally. This is a diagnostic-package smoke check, not native importer/production validation.

Exact rebuild (PowerShell, repository root):

```powershell
python -m venv out/jt-diagnostics-package/venv
./out/jt-diagnostics-package/venv/Scripts/python.exe -m pip install pyinstaller==6.22.3
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/tcltk/tcl/core-8-6-15/license.terms' -OutFile out/jt-diagnostics-package/Tcl-license.terms
./out/jt-diagnostics-package/venv/Scripts/python.exe -B tools/jt/package_diagnostics.py
$jtDiagExe = (Resolve-Path out/jt-diagnostics-package/release/JTDiagnostics.exe).Path
Start-Process -FilePath $jtDiagExe -ArgumentList @('out/jt-research/texture-source-ui-fixtures/texture_inline_le.jt','--output','out/jt-diagnostics-package/exe-modern-report.json') -WindowStyle Hidden -Wait
```

`packaging.json` records the actual tool versions/source/exe hashes. `--reuse-build` recreates the distribution ZIP without rebuilding only when the diagnostic source still matches the saved build record. The original Python CLI remains available below.

The original `tools/jt/inspect_jt_structure.py` uses Python's standard library. It reads header/TOC metadata and inventories element-header type GUIDs in the root LSG. It does not decode/export vertices, material values, property strings, model names, object/segment UUIDs, assembly references or paths. It does not follow external partitions. It includes file version/byte order, byte size, optional SHA-256, segment-type counts, graph/property type GUID counts and available registered class names.

The script can run standalone on production without the viewer or Qt/OCCT. When run within the repository it derives registered class names from the existing registration macros. Standalone mode has no registry: `registry_available=false`, so null class names do not by themselves mean unsupported nodes. The GUID counts remain useful for comparison locally. Reports contain metadata; their presence is not a successful geometry import.

The LSG scan is limited to raw/zlib profiles and 128 MiB encoded/decoded storage. Unsupported compression and limits are diagnosed; TOC counts are retained when possible. The diagnostic tool does not implement all native import compatibility quirks. It targets modern production triage and should not be used as a conformance validator for older files. Required bounds/truncation checks apply. Input is opened read-only; output cannot overwrite the input. Repository-local reports must stay under ignored out/.

In a repository checkout:

```powershell
python -B tools/jt/inspect_jt_structure.py "<production-input>/sample-P109-P1.jt" --output out/jt-research/production-P109-P1.json
python -B tools/jt/inspect_jt_structure.py "<production-input>/sample-P109-A1.jt" --output out/jt-research/production-P109-A1.json
```

Alternatively copy only the script to production, run it with the same input/`--output` arguments and an output location outside this repository. Python 3.10+ is suitable. `--no-hash` omits the whole-file hash pass when desired. The report excludes paths/names by design; file-access failures retain only an error code. A report pair plus the production application commit/build identity is the next evidence. Local access to the failing files, if available, would allow stronger diagnosis; their availability has been requested from the owner.

Interpretation must account for external assemblies: zero local shape-LOD segments does not mean an assembly has no display geometry. An XT segment without local display segments may indicate a precise-only leaf, but unsupported graph/reference behavior must still be considered. An unregistered graph GUID may identify a missing type; it does not authorize treating its payload as an ordinary group.

## Local read-only observations

The inventory command ran against the existing original/synthetic modern image control and the directly authored 9.5 image control, with reports saved as `out/jt-research/structure-inventory-original-modern.json` and `structure-inventory-original-95.json`. Both reports identify their expected version, one LSG segment and partition/texture attribute types. No native importer, C++ build or test suite ran. The source geometry remains untouched and unexported. Exact reproduction:

```powershell
python -B tools/jt/inspect_jt_structure.py out/jt-research/texture-source-ui-fixtures/texture_inline_le.jt --output out/jt-research/structure-inventory-original-modern.json
python -B tools/jt/inspect_jt_structure.py out/jt-research/texture9-ui-fixtures/9_5/stored/texture_inline_le.jt --output out/jt-research/structure-inventory-original-95.json
```

These are diagnostic observations on synthetic containers, not production-file validation. The original controls intentionally have no local shape segments because their geometry is external; that demonstrates why TOC counts alone cannot establish absent tessellation.

## Provenance and next decision

Consulted the supplied screenshots, existing `JtSceneBuilder.cpp`, `jt_probe.cpp` and already licensed decoder header/TOC/element-envelope handling in this working branch. The new original Python script follows those existing container conventions without copying a third-party implementation or introducing a dependency. No new vendor reference, software, SDK/schema, disassembly or licence interpretation was introduced. No vendored file is changed for this diagnostic work. Method statements are not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).

Wait for production reports or an accessible failing sample before choosing a record/codec fix. Keep changes related to this triage distinct from the pending [basic 9.x texture gate](JT_9X_TEXTURE_VALIDATION.md).

## 25 October 2026 - accessible assembly UV failure

A newly supplied wire-disabled production package now provides authentic local evidence. Its displayed failure is a nonfinite texture coordinate, not the earlier unresolved-node message. Read-only inspection isolates a hash-verified unpredicted modern UV encoding in two companions; the existing extra Lag1 step creates invalid values. A bounded compatibility correction is prepared with owner build/native/viewer acceptance pending. See [production UV evidence](JT_PRODUCTION_UV_TRANSPORT.md). This supersedes the no-access statement for this new package only; earlier wire-triggered exports and their cause remain unqualified. The package also has PMI-type segments, which is not a PMI rendering pass.

### Owner confirmation - 25 October 2026

After the binary UV correction, the owner reported the supplied assembly opened completely. This accepts the observed opening failure as resolved for that package; native regression-suite execution, PMI rendering and the independent wire-export failure remain unqualified. See [production UV record](JT_PRODUCTION_UV_TRANSPORT.md).
