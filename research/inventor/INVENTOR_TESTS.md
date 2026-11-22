# Inventor importer regression tests

21 November 2026 · `feature/inventor3d` · test sources in [tests/inventor](../../tests/inventor/) (MOD-7)

## What is tested

`inventor_tests` is one GUI-free executable built from the importer's reader sources (Qt Core, zlib, Zstandard, OpenCASCADE). It is registered in `tests/CMakeLists.txt` and runs with the other tests when `-DMV_BUILD_TESTS=ON`. Built and run headless on 21 November 2026 (standalone configure, see Run): 213 checks, 0 failures on 147 corpus entries (111 parts, 36 assemblies) and the synthetic tests.

**Synthetic tests (no sample needed, always run).**
- Segment decoding: zlib and Zstandard (with and without a recorded content size) round trips; a result over the limit, a truncated frame, a damaged frame, a short or wrong envelope and an unknown packing are refused with `std::runtime_error`.
- Bounds of the graphics cursor (reading or skipping past the end, negative skips).
- Malformed files: empty, text, OLE magic only, zeros, garbage after the magic, a missing file. The compound-file reader, the part reader (strict and lenient) and the assembly reader must throw `std::runtime_error` and never crash; cancellation is reported as an error.
- Component resolution: a reference as saved on another machine is found next to the assembly (as given, relative, last path components); a missing or embedded reference gives a reason and no path.

**Corpus tests (need local samples, skipped otherwise).** For every entry of [corpus_manifest.tsv](../../tests/inventor/corpus_manifest.tsv) whose sample is present locally and whose SHA-256 matches, the readers' result is compared with the expectation recorded in the manifest:
- parts (`IPnnn`, read the way the viewer does, not strict): solids, open surfaces, incomplete bodies and failed bodies, total faces, total volume (relative tolerance 1e-6), the overall extent of all bodies (to 0.1 mm), colour of the part, number of face colour overrides, or the error text when the part is refused;
- assemblies (`IAnnn`, direct occurrences only): occurrence, sub-assembly, suppressed, hidden and unresolved-file counts, the number of occurrences whose component file was chosen among several of the same name (`bindingNotes`) and of those bound by position with nothing to confirm it (`positionalBindings`), and a checksum of the placements, or the error text.

**Thread check** (`inventor_tests --thread-check`, also `ctest -R inventor_thread_check`). Every part is read twice, serially (`MODELVIEWER_INVENTOR_THREADS=1`) and with the threaded face building forced on for every shell of 20 faces or more (4 workers, 10 faces each, through `MODELVIEWER_INVENTOR_PARALLEL_MIN_FACES` and `MODELVIEWER_INVENTOR_PARALLEL_FACES_PER_WORKER`), and the two results must agree. The fixed-order volume moves by 1e-6 to 1e-5 between the two constructions (the chunk-boundary edges are sewn instead of shared), so a part that differs only in volume is compared again with the adaptive integral (1e-7 relative). The corpus alone never reaches the 4 000 faces at which the importer threads by itself.

A run that compares no sample (a wrong `--only`, or every sample missing while the map exists) fails.

The three smallest parts and assemblies are also cut to one half and one tenth of their size; the readers must refuse the truncated copies without crashing.

**Provenance and what is tracked.** The manifest holds neutral IDs (`IP` parts, `IA` assemblies, numbered in the order samples were first seen), SHA-256 hashes, sizes and expected values only. Sample names and paths are never recorded. The map from ID to local path (`out/inventor-tests/corpus_map.tsv`, or the file named by `MV_INVENTOR_CORPUS_MAP`) is local and ignored with `out/`. Samples larger than 30 MB (the oversized multi-body part takes several minutes) are skipped unless `--max-mb` is raised.

## Run

Build and run (the owner builds; output under `out/`):

```powershell
cmake -S tests/inventor -B out/inventor-research/tests-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<vcpkg_installed>/x64-windows
cmake --build out/inventor-research/tests-build
out/inventor-research/tests-build/inventor_tests.exe
```

Or with the whole project: `-DMV_BUILD_TESTS=ON` and `ctest -R inventor_tests`.

Create (or refresh) the manifest and the local map from a folder of samples; existing IDs are kept by hash, new samples get the next free ID, every expectation is recomputed:

```powershell
out/inventor-research/tests-build/inventor_tests.exe --generate <samples-root> tests/inventor/corpus_manifest.tsv out/inventor-tests/corpus_map.tsv
```

Review the diff of `corpus_manifest.tsv` before committing it: a changed expectation is either a regression or an intended improvement, and only the owner decides which. `--only <id>` runs one entry; `--max-mb <n>` changes the size limit.

## Status and limits

- The expectations are what the readers produced on the first `--generate` run, after the changes already accepted by the owner; they are a regression baseline, not a statement that every entry is right (8 parts are templates or layout parts with no solid body, 20 assemblies have unresolved component files, and so on; the numbers are in the manifest).
- First finding of the suite: one 13 MB part was refused with an **empty** error message. A lump whose shells were all open surfaces threw the first surface's (empty) message and lost its geometry. It now keeps the open surface (1 879 faces), and the manifest was regenerated for that entry only (every other entry compared equal before the regeneration).
- The tests cover the readers (parts, assemblies, containers), not the scene builder or the viewer (`AssemblyScene`, tree, colours as rendered), which need the application (Assimp and the converter) and stay in the owner's UI test batches. Known gaps: no sample has an occurrence hidden in the saved design view (`hidden` is 0 in every assembly entry; the `MODELVIEWER_INVENTOR_SIMULATE_HIDDEN` hook exercises the scene only), and the per-occurrence identity and tessellated geometry are not compared.
- On 21 November 2026 the part entries gained the `extent` field and the assembly entries `bindingNotes` and `positionalBindings`, recorded from the current readers (a regression baseline like the other fields, not a reference). The motor's wrong-side face (an extent of 1 461 mm against 503 mm) would have been caught by it; the counts alone did not.
- Expected values are what the current readers produce, not an independent reference; the independent checks (paired STEP exports for placements and colours) are recorded in the other documents of this folder and were done once, outside the suite.
