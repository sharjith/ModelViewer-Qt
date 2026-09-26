# Simulation Results - Large-Result Review

Status: **review + first fixes** (2026-09-26). Branch: `feature/simulation-results`. Companion to `simulation_results_design.md`.

Question: what happens when a result has millions of cells and many time steps - in memory and in time - and what should change?

Method: a read of the load and display paths, plus an opt-in benchmark (`result_tests --bench`, `--time`, section 6) so the numbers can be
**measured** instead of guessed. Everything below marked *estimate* is reasoning from the code, not a measurement.

## 1. Memory model

What a loaded result holds (`ResultDataset`), all resident for the life of the result:

| Data | Cost |
|---|---|
| node position | 12 B per node (+ 8 B if the file gave ids) |
| hexahedron | 32 B connectivity + 4 B offset + 1 B type = 37 B per cell (+ 8 B ids) |
| polyhedron faces | 4 B per face node + offsets (see `ResultDataset::faceNodes`) |
| one field at one step | 4 B x tuples x components |

The last row is the one that grows: **every step of every field is read at open** (`ResultField::stepData`).

| Mesh (hexahedra ~ nodes) | Geometry | One step of {node scalar, node vector, cell scalar} | 50 steps |
|---|---|---|---|
| 1 M cells | ~ 50 MB | ~ 20 MB | ~ 1.0 GB |
| 5 M cells | ~ 250 MB | ~ 100 MB | ~ 5.1 GB |
| 20 M cells | ~ 1.0 GB | ~ 410 MB | ~ 20 GB |

So the eager-step model, not the geometry, is what caps result size: a 5 M-cell transient result already needs several GB before anything is drawn.
Compare mode holds two results at once.

Transient peaks on top of that:

- **Readers.** Exodus reads a field straight into its final array (fixed earlier). CGNS accumulated every component of every step and then
  assembled the shown fields from them, so the peak was ~2x the data; it now releases each component as soon as its field is built.
  MED reads one value block at a time as doubles (a transient 8 B/value block). VTKHDF/OpenFOAM/VTK read into the final arrays.
- **Boundary extraction.** Face records are 24 B each, held ~2 M at a time (~48 MB) and sorted per partition; *estimate*: the surface itself is
  small (a cube of n^3 cells has ~6 n^2 faces).
- **Showing a step.** `buildDisplayScalar` allocates the whole field's scalar (4 B x N) per frame; the arrow layer does the same for its own field.
- **Snapshot encode.** Byte-shuffled copies of the surface data (small: boundary vertices only, at most 100 steps).

## 2. Time model

*Estimates* (the benchmark replaces them):

- **Open.** Reading is I/O bound (text formats - `.frd`, ASCII `.vtk` - are much slower than HDF5/NetCDF ones); `validate()` is one pass over the cells.
- **Boundary extraction.** The face-hash space is split in `ceil(faces / 2M)` partitions (at most 64) and **every partition scans every cell**:
  5 M hexahedra = 30 M faces = 16 partitions = ~480 M face keys built. Single-threaded that is on the order of 10 s.
- **Showing one step** costs O(N) in the number of nodes (or cells), not in the surface: build the scalar (allocate, copy or magnitude, unit
  conversion, min/max), then map only the surface's share of it. For a volume mesh the surface is ~1 % of the nodes, so ~99 % of that work is
  not needed for what is drawn. At 5 M nodes I expect tens of milliseconds per frame - it caps playback rate.
- **Range over all steps** (the default colour range) is a scan of every step of the field, once per field/component/unit change, **on the UI thread**.

## 3. Done in this pass

1. `buildDisplayScalar` no longer allocates the output twice and converts units and finds min/max in **one** pass (was: allocate, fill, convert,
   min/max = 4 passes). Same results.
2. `computeAllStepsRange` scans each step **in place** (`computeStepRange`), without building a per-step copy of the whole field: one pass and no N-sized
   allocation per step (was: a full `buildDisplayScalar` per step). A test checks it equals what `buildDisplayScalar` reports.
3. Boundary extraction runs its partitions **on up to 4 threads** (each partition is independent; results are merged and sorted, so the surface is
   identical). Peak transient memory rises by ~50 MB per extra worker.
4. CGNS releases the accumulated components as fields are built (see section 1).
5. The benchmark harness (section 6).

## 4. Findings still open, ranked

**P1 - Load steps on demand.** The only change that removes the memory ceiling. `ResultField::stepData[s]` empty already means "not loaded" and
`validate()` already accepts it; what is missing is a way to load it later and a budget for what stays resident:

- a `IResultStepSource` (shared pointer in the dataset) with `loadStep(field, step)`; formats that can seek support it (VTKHDF, Exodus, CGNS, MED,
  OpenFOAM time directories; `.frd` needs an index of file offsets; single-file VTK is one step anyway);
- an LRU of resident steps under a memory budget (a setting), the current step and its playback neighbours pinned;
- the per-field ranges over all steps are needed **before** any step is shown (legend, fixed colour scale): gather them while opening in one streaming
  pass (min/max per field/component/step, the shape `ResultField::storedRange` already has for snapshots) instead of from resident data;
- consumers must ask for a step (`ensureStep`) before reading it: the display refresh, arrows, probe, extrema markers, snapshot save, derived stress.
  Playback prefetches the next steps on a worker thread.

Cost: opening does one full pass over the file for the ranges; a step change may wait for I/O. Risk: touches every reader and every consumer of `stepData`.

**P2 - Build the shown scalar for the surface only.** Fill only the values the surface uses and take the range from the cache (all-steps) or from the
per-step table of P1; then a frame costs O(surface). The hover probe and markers already sample only surface vertices. The arrow layer needs the same:
its magnitudes are wanted only at the sampled sites.

**P3 - Cheaper boundary extraction.** Threading (done) cuts the wall time; the O(faces x partitions) work is still there. Bucketing the records by hash
in one pass (or keying each face once into a 64-bit hash + owner, 12 B) would make it O(faces).

**P4 - Keep long scans off the UI thread.** The all-steps range, `restoreSimulationSessions` decode and a deformed-mesh rebuild block the UI. Run them
in a worker with a progress hint; needed once steps stream (P1).

**P5 - Smaller items.** MED's per-block double buffer; `SimulationSession` keeping both the `DisplayScalar` and the surface values (probe reads the
former); glyph arrows recomputing magnitudes for the whole field per frame.

## 5. Recommendation

Measure first (section 6), then P1 - it is the only fix for RAM - designed together with P2, because a step that is not resident is exactly a step
whose range must already be known. P3/P4/P5 are independent and can follow.

## 6. Measuring

Opt-in, nothing runs in the normal test pass. Times each stage of opening a result and of showing steps, and reports the dataset size and the
process's peak working set (Windows):

```
result_tests.exe --bench 100 20        (a synthetic 100 x 100 x 100 hexahedron block, 20 steps; 1 M cells)
result_tests.exe --bench 170 10        (~ 5 M cells)
result_tests.exe --time path\to\file.vtkhdf   (any supported result file, real data)
```

It prints: read/build, validate, boundary surface (time, triangles, peak MB), *show a step* per node and cell field (the per-frame cost),
range over all steps, snapshot encode, process peak. Please run it on the sizes you care about; those numbers replace the estimates above.

## 7. Measured (2026-09-26, Windows, release build, after the fixes of section 3)

| Stage | 1 M cells, 20 steps | 4.9 M cells, 10 steps |
|---|---|---|
| dataset in memory | 438 MB (~20 MB per step) | 1181 MB (~118 MB per step) |
| boundary surface | 0.49 s | 4.6 s |
| show one step (node or cell field) | 2.6 ms | 12 ms |
| range over all steps of a field | 26 ms | 63 ms |
| snapshot encode | 18 ms | 50 ms |
| process peak | 649 MB | 1503 MB |

Conclusions: the estimates for the per-frame cost (P2) and the range scan (P4) were pessimistic - at 5 M nodes a step takes 12 ms, so neither is
worth changing now. The boundary extraction (4.6 s at 5 M cells, one time per open) is the slowest stage (P3, not urgent). **Memory is the limit**:
the dataset grows linearly with the steps (118 MB per step here), so P1 only matters once steps x size approaches the machine's RAM.
Decision (2026-09-26): P1 is deferred; the section / iso-surface / streamline work goes first.
