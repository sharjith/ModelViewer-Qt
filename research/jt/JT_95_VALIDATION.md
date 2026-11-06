# JT 9.5 qualification and Gate 3a

20 October 2026, `feature/JT-importer`. The owner selected the available 9.5 assembly while authentic 10.10/10.11 inputs remain unavailable. The owner accepted Gate 3a on 20 October 2026 with “9.5 works”; the separate [10.9 Gate 2b](JT_109_PALETTE_VALIDATION.md) production-system check remains pending. The owner builds the full application.

**Latest native A95 baseline after the Gate 3b topology correction:** 2,999 resources, 4,475 occurrences, 6,614,780 vertices and 7,918,872 triangles. The earlier Gate 3a table below is historical. The supplied smaller 9.5/10.0 export comparison and pending owner checklist are in [Gate 3b](JT_POINT_VERSION_EXPANSION.md).

## Layout and implementation

Reuse the audited transitional reader rather than treating 9.5 as a renamed 10.x file. The qualified route preserves:

| Area | JT 9.5 route | Difference from the qualified JT 10 route |
| --- | --- | --- |
| File directory | 32-bit TOC and segment offsets | 10.x offsets are 64-bit |
| Graph and attributes | 16-bit record versions, object IDs, versioned property atoms, earlier bounds/partition fields | 10.x record versions are byte-sized and several reserved fields disappear |
| Materials | Base attribute version 1; material versions 1/2, with reflectivity only in version 2 | No 10.x field-final word, palette tail or bumpiness field |
| Topology | Earlier MeshCoder mask streams and a topological LOD without the 10.x nested element header | Keep topology-mask and envelope adapters distinct |
| Integer packets | Mk.2 null, bitlength, arithmetic and chopper paths | Do not substitute Mk.3 bitlength/probability contexts or modern raw-mask forms |
| Coordinates and normals | Existing quantized/exponent-mantissa coordinate and separate-stream Deering-normal adapters | 10.x binary-float and packed-normal forms remain separate |

The original input exposed an unused zero-frequency entry in a Mk.2 probability table. Empty arithmetic intervals can be skipped without changing symbol selection. Permit those entries in Mk.2 while retaining nonzero-total, overflow, context and stream bounds; keep Mk.1 behavior unchanged. This is an observed compatible representation, not a claim that every zero-filled or malformed table is valid.

Before enabling the 9.5 path, tighten the inherited Mk.2 bitlength decoder: validate control widths, field widths, run lengths and fixed ranges, bound adjustment loops, poll cancellation and avoid signed range/addition overflow. Chopper widths and low fields are checked; reconstruction uses defined unsigned shifts/addition. Packet word-count rounding avoids signed overflow.

The original scene adapter now honors whole-attribute Final state flags as well as field-final masks. Whole-attribute Final also protects shininess; Force overrides once without clearing an ancestor's final state. This shared correction is relevant to older attributes that have no field-final word. Palette, transform and draw-style accumulation retain the same scene boundary.

Only 9.5 is enabled by this pass; other 9.x versions remain unqualified. No exact Parasolid backend is added. Display uses validated stored tessellation and tries an alternative supported LOD when necessary.

## Input and native observations

External sample **A95** has header `Version 9.5 JT  DM 9.6.3.1`, size 410,366,195 bytes and SHA-256 `5062b6b7d2e089ce0461cb8bfa4fe2cf9243d8fd9da64741fc9ee6dba5987f6a`. Its original name/path stays in ignored `out/jt-research/documentation-sample-map.json`; the source geometry is not redistributed.

The initial unchanged-input native run completed with the normal 6 GiB cumulative budget:

| Measurement | Native observation |
| --- | --- |
| Mesh resources | 2,999 |
| Mesh occurrences | 4,475 |
| Resource vertex sum | 6,592,239 |
| Resource triangle sum | 7,893,052 |
| Nodes before optional purging | 20,905 |
| Stored-tessellation fallback parts | 3,625 |
| Cumulative charged allocations | 6,069,877,180 bytes |
| Local mesh-resource bounds | (-1222.47, -1416.7, -1800) to (5761.65, 897, 1165) |

Counts and bounds are regression observations, not independent ground truth or transformed world bounds. Shared resources and occurrences are different units; the UI may report occurrences. Cumulative charges include released scratch memory and are not live RAM usage. No budget increase or input-header rewrite is required.

**Correction following the owner-supplied exports:** the initial Gate 3a run rejected one preferred LOD as an apparent zero-length bitlength packet and used another LOD. A later small 9.5 export exposed the same failure without a usable alternative. The reference identifies the affected stream as a plain high-degree-mask vector, which the reader had incorrectly dispatched to CDP2. The [Gate 3b correction](JT_POINT_VERSION_EXPANSION.md) reads that raw vector correctly and passes independent layout/truncation fixtures. The supplied small 9.5 assembly now imports at the same geometry counts and local bounds as its 10.0 export. This supersedes the earlier diagnosis of an unsupported zero-length encoding; invalid compressed packets remain rejected.

The original observations above remain the accepted Gate 3a baseline, not guaranteed current counts after correcting LOD selection. The unchanged A95 follow-up passes with 2,999 resources, 4,475 occurrences, 6,614,780 vertices and 7,918,872 triangles, with the preferred LOD now decoded. Cumulative charges are 6,083,697,416 bytes under the unchanged 6 GiB budget; node count and local bounds remain unchanged. The previous LOD-rejection diagnostic is absent. No independent reference-render comparison for A95 has yet been recorded. Emitted meshes in the initial observation had no vertex-colour or UV channels; material colours are separate.

## Automated checks and reproduction

`tests/jt/jt95_tests.cpp` contains original LE/BE arithmetic goldens with zero-frequency entries at different table positions, all-zero/truncated rejection, fixed/variable bitlength checks, full-width ranges and malformed chopper fields. The independent arithmetic expected sequence is 10,20,10,20. Original 9.5 assembly graphs reference original 8.2 fixture geometry to check mixed-version references, repeated instances, opaque red/green materials, whole-attribute Final and accumulated transforms. They do not by themselves qualify native 9.5 tessellation; A95 supplies that separate check.

The portable suite and five 10.0 part regressions plus the 8.1 geometry/cancellation regression pass under isolated MSVC AddressSanitizer. The 10.9 regression retains 3,254 resources, 9,587 occurrences, 606,493 vertices and 685,079 triangles. The final unchanged A95 run passes with exactly the measurements above after the attribute and bounds corrections. SHA-256 was rechecked unchanged. The large 10.3 regression also retains 2,468 resources, 2,853 occurrences, 4,450,421 vertices and 5,844,669 triangles. The 8.0 and 8.2 geometry regressions pass unchanged. Source-provenance, whitespace and local documentation-link checks pass. Logs remain ignored under `out/jt-research/95-*`.

```text
jt_tests --95-fixtures out/jt-research/95-ui-fixtures
jt_probe --validate <local-samples>/sample-A95.jt
```

## Gate 3a accepted: owner build-and-test checklist

Build the current local `feature/JT-importer` checkout, then:

1. Open the unchanged original A95 input. Check overall completeness, shading, material colours, placement, hierarchy, picking and fit-to-screen. Use an independent viewer/preview where available. Compare counts using the units above.
2. Open `out/jt-research/95-ui-fixtures/assembly_le.jt` and `assembly_be.jt`, keeping `part.jt` alongside them. Both should show two small planar shapes: one red, one green, each a quad plus a separate triangle. Each occurrence has seven vertices and three triangles. Their x translations are 110 and 210, with y translation -3; after fit-to-screen they should be separated horizontally. The assembly contains two material-specific mesh resources.
3. Reopen representative known-working 8.x, 10.0 and 10.3 files for placement, colours and normals. About should list versions and licence credits only, now including 9.5 and the 2010 reference attribution.

The owner confirmed “9.5 works” on 20 October 2026, accepting the supplied-input viewer gate. No separate UI outcome for each auxiliary synthetic fixture was reported. Native fixture results above remain automated evidence. These changes remain uncommitted; no commit/push was requested with this confirmation. Gate 3a does not accept or replace Gate 2b.

The subsequent [Gate 3b material-mask adapter](JT_POINT_VERSION_EXPANSION.md) corrects older field numbering before wider point-version qualification. Gate 3a remains accepted; the new appearance fixtures await their own UI confirmation.

## References and provenance

Research uses the archived public **JT File Format Reference Version 9.5 Rev-A**, copyright 2010 Siemens Product Lifecycle Management Software Inc.: printed page 12 permission terms; section 7 container/graph/material/tessellation layouts; section 8.1.2 Mk.2 packets and contexts; section 8.2 encoding algorithms. The locally archived reference PDF SHA-256 is `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`. Reference availability and terms are discussed in [licensing research](JT_IMPORT_FEASIBILITY.md) and [third-party notices](../../THIRD_PARTY_NOTICES.md). The PDF itself is not bundled.

Existing upstream GPL notices and pinned provenance are retained, with dated modification notices. No additional third-party implementation, restricted schema or SDK is added. The decoder manifest still covers 104 upstream-derived files and fourteen ModelViewer-original additions. The new fixture source is original ModelViewer work and contains no downloaded geometry.
