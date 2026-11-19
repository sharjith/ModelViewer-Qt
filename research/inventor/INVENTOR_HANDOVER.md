# Native Inventor importer handover

19 November 2026 · `feature/inventor3d` · working tree checkpoint based on `81786a9`

## Resume here

Work in the existing ModelViewer-Qt checkout on `feature/inventor3d`. Run `git status --short --branch` and read `AGENTS.md` and [RESEARCH_FOLDER_HANDOUT.md](../../RESEARCH_FOLDER_HANDOUT.md) first. Keep the current working-tree changes and unrelated owner files. The first analytic IPT implementation is committed at `81786a9`. The subsequent wheel, motor, pump, appearance and foundation work is in the working tree. The current stage has reached headless validation. The next checkpoint is an owner-approved Release build followed by the owner's visual checks on G09, G10 and G15. Track the results separately from the earlier visual approvals.

## Strategy and scope

ModelViewer is GPL-3. Implement an independent native reader for the saved geometry and appearances in IPT and the occurrence hierarchy in IAM. The delivered importer reads the OLE compound document, decodes the relevant Inventor streams and saved Autodesk Shape Manager (ASM) records, and constructs OCCT solids for the existing ModelViewer scene pipeline. Runtime dependencies remain the existing ModelViewer libraries. FreeCAD, Inventor, Apprentice, CAD Convert binaries and intermediate STEP conversion are outside the runtime design. Paired STEP is an independent validation reference. CAD Convert and InventorLoader are research references for hypotheses about the format. Keep implementations independent and track source URLs, versions, hashes and licensing provenance when those references inform the work. [Feasibility and provenance](INVENTOR_IMPORT_FEASIBILITY.md)

IPT has no published open end-to-end specification. Build the working specification from actual bytes, controlled changes in authored files, native topology and graphics metadata, and comparisons with independently generated references. For each observed layout, record the version, field type, units, direction or orientation, bounds and qualification limits. Treat kernel validity as one check. Check face or body correspondence, trim bounds, tessellation and, where reference data allows, exact geometry and appearance. The product scope is saved geometry and assembly viewing. [Working specification and first-failure corpus](INVENTOR_FOUNDATION.md)

Production entry points are `src/Import/AssImpModelLoader.cpp`, `src/Import/Inventor/CompoundFile.cpp`, `PartSegment.cpp` and `PartSolid.cpp`. The native reader exposes `Inventor::readPartSolids` and optionally returns saved appearance. The headless tools under `tools/inventor/` include `compound_probe.cpp`, `inspect_asm_profile.py` and `audit_ipt_corpus.py`. The first two support inspecting individual files and the last one runs repeatable package audits grouped by the first diagnostic.

## Completion status

| Milestone | Evidence and acceptance |
| --- | --- |
| Basic and multibody IPT | I01–I05 passed owner UI checks. I02 exercises opposing edge/seam use. I04 contains two independent bodies the owner toggled separately. [Analytic geometry](INVENTOR_ANALYTIC_GEOMETRY.md) |
| Wheel geometry and saved appearance | G01 has 98 faces and one dark face override over a yellow body. The owner compared the native geometry with STEP and subsequently approved the colors. [Wheel geometry](INVENTOR_ANALYTIC_GEOMETRY.md) · [Appearance](INVENTOR_APPEARANCE.md) |
| Complex geometry and appearance | G02 motor has 372 faces and 110 face overrides. The paired STEP comparison matched all faces by surface type, area and centroid within the stated tolerances. G07 pump has 349 faces with saved graphics envelope matches and tessellation for every face. The owner supplied the native motor screenshot and approved the pump. [Complex geometry](INVENTOR_COMPLEX_GEOMETRY.md) |
| Foundation trim and edge tolerances | The current changes extend the existing bounded OCCT repair to closed plane and cone-family shells and pass accepted endpoint residuals into OCCT vertex tolerances. G09, G10 and G15 open in the rebuilt headless probe. The 39-file corpus improved from 9 to 24 valid solids, with unchanged measured geometry for the original nine. The owner UI check for this stage is pending. [Foundation results](INVENTOR_FOUNDATION.md#trim-and-endpoint-checkpoint) |
| IAM | The ignored experimental A01 probe matches all four flat occurrence transforms against an Inventor-generated report. Production IAM integration is paused while the IPT foundation is qualified. [A01 and assembly plan](INVENTOR_IMPLEMENTATION_PLAN.md) |

The rebuilt standalone probe is `out/inventor-research/compound-build/inventor_compound_probe.exe`. The 39-file baseline and new corpus JSON reports are under `out/inventor-research/foundation/`, alongside the three control logs. All nine CTests passed in the existing `out/inventor-research/ctest-build` in 170.77 seconds. Those tests exercised the preconfigured repository suite. The rebuilt standalone probe exercised the current Inventor code. The CTest record is under `Testing/Temporary/LastTest.log` in that test build directory.

## Next owner UI checkpoint

Build from the current branch in the existing x64 Visual Studio developer shell after obtaining explicit authorization for a full viewer build, or have the owner build. The earlier authorization to run the native probe and CTest covers those headless checks. The proposed new stage is `out/inventor-research/viewer-foundation`, with the executable under `bin/ModelViewer.exe`. Older staged binaries predate these fixes.

```powershell
$build = 'out/build/ninja_release_vcpkg'
$stage = Join-Path (Get-Location) 'out/inventor-research/viewer-foundation'
cmake --build $build --target ModelViewer --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Viewer build failed' }
cmake --install $build --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'Viewer staging failed' }
Get-Item "$stage/bin/ModelViewer.exe", "$stage/Qt6/plugins/platforms/qwindows.dll", "$stage/share/ModelViewer/shaders/axis.vert"
```

Open the local file checklist at `out/inventor-research/FOUNDATION_UI_TEST_FILES.md` for direct links to the three UI inputs, earlier regression controls and upcoming research inputs. This checklist is private to the workspace. Share the three IPT links and the new executable link with the owner in the private chat. Ask the owner to load the three parts through the normal UI and rotate them to inspect holes, cut edges and shaded faces. All three now validate headlessly. A screenshot or explicit confirmation closes the visual gate. Await the results before starting work that depends on acceptance.

## Remaining work in order

1. Close the trim checkpoint. Carry forward the existing nine successful corpus controls plus I01–I05, G01 and G02. The newly reconstructed broader corpus entries currently have headless solid validation. Qualify geometry with separate evidence before making broader compatibility claims.
2. Establish procedural helix semantics on G08 and G11 and the related `helix_int_cur` boundary form. Eight remaining AX files fail first on a helical record. Decode the radial frame, pitch, handedness and parameter mapping from saved records and compare reconstructed boundary curves and pcurves against an authored reference. Start with the smaller G08 case. A missing saved B-spline can indicate a procedural definition.
3. Qualify G14's rational `exact_spl_sur` header and G13's spindle torus separately. G14's definition is nested inside a pcurve and includes the observed optional rationality token `both`. G13 needs the retained branch and trim chart of a surface whose minor radius exceeds its major radius. Compare the retained geometry to independent data.
4. Classify G12 and the remaining body and multi-shell cases. G12 has no body or face records in the saved current ASM segment. Determine whether an external reference, graphics representation or empty file accounts for it. Keep absent geometry, open sheets, wire bodies and unresolved references distinguishable.
5. Resume IAM work from the isolated A01 prototype under ignored `out/inventor-research/iam-wip/`. Its source, executable and the `iam-controls/` files are local. Port the bounded streaming Zstandard support if required, since observed IAM frames can omit the advertised decompressed size. Implement reference resolution, nesting, transform composition, repeated definitions, suppression and visibility. The assembly UI gate needs a complete nested Pack and Go with repeated asymmetric components, scoped transforms and an independent placement or geometry reference.
6. Finish release qualification across observed versions, cancellation and resource bounds, unsupported or malformed files, and packaging. Keep the remaining appearance scope explicit. Current qualified styles cover diffuse body and face colors for observed profiles.

## Working agreement

The owner performs UI verification and controls the desktop. Run only the authorized headless checks. Pause at owner build and UI gates for dependent work. Keep generated controls and outputs under ignored `out/` or outside the repo. Put research Markdown under `research/inventor/`, reusable tools under `tools/inventor/`, and regression tests under `tests/inventor/` when needed. Keep local sample names, third-party source snapshots and sample bytes out of tracked research. Preserve notices and local acquisition manifests. Record observations, hypotheses, verification results and explicit owner acceptance separately. Preserve unrelated changes and await owner direction before committing or pushing.
