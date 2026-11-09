# JT 10.3 tessellation: sample A103

19 October 2026, `feature/JT-importer`. Native validation is described below. The owner accepted the rebuilt-application baseline at Gate 0; an independent visual comparison remains unrecorded. No full application build was performed by the agent.

## Input and diagnosis

Local sample: `<local-samples>/sample-A103.jt`, **228,631,617 bytes**, SHA-256 `f9db16346afa615b792ba10279cc9a7e16418c8e17e79805b9d4f7e9fd4e52d6`.

Header: `Version 10.3 JT  DM 9.1.0.0`. File version 10.3 and writer build 9.1.0.0 are distinct identifiers. The original sample is unchanged and remains outside the repository; no redistribution permission or exact download URL is recorded.

The little-endian file uses a 64-bit directory offset of 109 and **11,550 entries**, all with ranges within the file. Segment types/counts: `1:1, 3:23, 4:2072, 7:2468, 8:2468, 9:2468, 17:2049, 31:1`. These are segment counts, not source-part or mesh-occurrence counts. The type-1 scene graph at offset 369,713 has length 846,561 and LZMA/XZ-decompresses to **7,093,529 bytes**.

Its observed material attributes use the existing base/material local-version-1 layouts. Unlike the later 10.9 sample, this input did not require version-2 palette/Sabot implementation. Known group, instance, transform, shape and RangeLOD records use the current scene path. Unsupported metadata remains diagnosed separately from display geometry.

A temporary diagnostic copy under ignored `out/jt-research` changed only the file-version text to 10.0 to expose subsequent decoding failures. That copy was an investigation tool, not a converted model or delivered workaround. It reached geometry decoding but exhausted the old cumulative allocation budget. Production support now explicitly accepts 10.3 and is checked against the original file without header changes; the decoder does not accept an arbitrary numeric 10.x range.

## Resource budget

The decoder's allocation counter is cumulative across charged allocations, including scratch buffers subsequently freed. Its previous **1 GiB** import budget was therefore too small for this assembly even though it was not holding all those allocations simultaneously.

The default cumulative budget is now **4 GiB**, with the existing **256 MiB per charged allocation** limit retained. Segment-size/decompression limits and cancellation remain active. `Jt::Options::decodedAllocationBudget` allows callers to impose a smaller cumulative budget. This counter is neither peak resident memory nor accounting for every allocation made by the application or compression libraries.

Tests exercise exact-budget boundaries, rejection without charging the counter, oversized requests, nested-scope restoration, cancellation, and propagation of a tiny caller budget through the native importer. No multi-gigabyte allocation is needed for the accounting tests.

## Native result and limits

The qualified display path produces:

| Measure | Result |
| --- | --- |
| Render mesh resources, including appearance variants | 2,468 |
| Mesh occurrences in the scene tree | 2,853 |
| Nodes before the application's optional redundant-node purge | 10,145 |
| Vertices summed once per emitted mesh resource | 4,450,421 |
| Triangles summed once per emitted mesh resource | 5,844,669 |
| Parts using stored tessellation instead of unavailable exact geometry | 2,408 |

The probe checks all emitted triangle indices, finite positions, normalized normals, material and node mesh references, and finite local transforms. Local mesh-resource bounds are approximately `(-971.889,-1269.86,-516.928)` to `(2507.73,2579.97,920.209)`. These are not transformed world/occurrence bounds. Counts and bounds are native regression observations, not independently established ground truth.

The isolated MSVC AddressSanitizer build passes the original, unmodified sample A103 import with `--validate`, the portable suite including LE/BE 10.3 containers and allocation budgets, the five 10.0 part regressions, and the sample L81 8.1 geometry/cancellation regression. The original and diagnostic-copy results agree. Provenance and whitespace checks pass. Logs and the diagnostic copy remain under ignored `out/jt-research`; the owner subsequently accepted Gate 0.

Diagnostics identify stored-tessellation fallback and unsupported/unresolved metadata pairs on 2,408 part traversals. Exact B-Rep conversion remains unavailable; the result uses stored display meshes and one static selected LOD per representation. This does not qualify every 10.3 encoding, full metadata preservation or visual equivalence to the authoring system.

Original synthetic little/big-endian 10.3 graph fixtures exercise names, instances, transforms, materials and nested external references, including mixed 10.3 containers with qualified 10.0 part data. They reuse known supported local layouts; they are not an independent format oracle. Unqualified 10.1, 10.2, 10.4, 10.6 and 10.9 headers remain rejected. Existing 8.1 fixtures/sample L81 and the five 10.0 part regressions remain required.

## Reproduction and viewer check

With the standalone `tests/jt` build and dependency DLLs on `PATH`:

```text
jt_probe --validate <local-samples>/sample-A103.jt
jt_tests out/jt-research/samples <local-samples>/sample-L81.jt
```

After rebuilding ModelViewer, open the original sample A103 file. Check assembly completeness, relative part placement, shading, colours, fit-to-screen behavior and hierarchy/selection. The scene contains **2,853 mesh occurrences backed by 2,468 mesh resources**; counters must be compared using the same unit. Compare against a source preview or independent viewer where available. Also reopen sample L81 and a 10.0 part. Commit only after the owner's UI tests pass.

No proprietary SDK code or restricted schema material was introduced. This change reuses the audited reader's existing supported layouts, retains upstream notices, and adds original validation/resource-budget handling. See the [compatibility policy](JT_VERSION_COMPATIBILITY.md) and [permissions research](JT_IMPORT_FEASIBILITY.md).
