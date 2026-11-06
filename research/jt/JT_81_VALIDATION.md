# JT 8.1 lossless tessellation and sample L81

19 October 2026, `feature/JT-importer`. The owner confirms that JT 8.1 works in the application after rebuilding. The implementation remains uncommitted. The owner explicitly requested backward compatibility for older community exports.

## Implemented subset

The native reader now accepts JT 8.1 alongside JT 10.0. JT 8.1 uses its 32-bit file/TOC offsets, older element identifiers/layouts and unversioned graph/property/attribute records. Base/string/integer/float properties and LOD/RangeLOD dispatch no longer apply JT 10-only local-version assumptions to older records. Scene traversal, transforms, occurrence hierarchy, names and material handling reuse the tested scene builder.

The legacy mesh path supports lossless XYZ arrays with absent or per-vertex normals, CDP1 null and bitlength strip-boundary packets, and raw or zlib-compressed vertex data. It checks local versions, strip ranges, counts, byte lengths, every vertex read, zlib completion/checksum and input limits. Triangle winding restarts for each strip; the old code incorrectly carried its alternating parity into the next strip. Cancellation is polled during decoding.

Zero-length stored normals are reconstructed from adjacent triangles; valid stored normals remain. Nonfinite normals are rejected. An isolated/degenerate corner with no usable adjacent normal receives the existing deterministic +Z fallback. Reconstruction counts are diagnosed. Legacy materials retain finite nonnegative shininess, including sample L81's values up to 131072, and use the same documented Phong-to-PBR approximation. The JT 10.0 [1,128] validation remains in place.

JT 8.1 quantized vertex records, per-facet/per-primitive normals, UV/colour bindings, Huffman/arithmetic integer packets, Switch nodes and draw-style attributes are explicitly unsupported in this first path. This prevents the mere presence of an old decoder branch from being treated as qualified support. Another usable display alternative may still be selected. JT 9.5 and additional 10.x versions remain subsequent targets. Exact XT decoding has not been added.

## Sample-specific envelope handling

The original [sample L81 inventory](JT_81_CONTAINER_INVESTIGATION.md) identified a final zlib-compressed scene graph whose declared segment length is one byte shorter than the complete stream. The reader permits exactly one extra byte only for a JT 8.1 type-1/zlib segment ending at physical EOF whose compressed-length field demands that byte. Other offsets and bounds remain checked. Complete zlib checksum/output validation is required; appending a byte after this shortened segment causes rejection in the original regression fixture.

The sample's raw vertex zlib blocks include a single zero padding byte after the zlib stream. That precise padding is allowed only in the legacy vertex-array path. Other trailing bytes, extra decoded output and checksum errors are rejected. The normal segment completion check does not enable arbitrary padding.

## Validation evidence

The isolated MSVC AddressSanitizer tests pass for original little- and big-endian JT 8.1 fixtures with compressed/uncompressed vertices, conventional and shortened-final-graph envelopes, null/bitlength CDP1 packets, typed string/integer/float properties, an F32 `(10,-3,0)` translation, blue appearance and high legacy shininess. The two-strip fixture checks the exact triangles `(0,1,2)`, `(1,3,2)`, `(4,5,6)`, including a zero stored normal that is reconstructed. Tests reject bad strip ranges, wrong raw byte sizes, bad vertex/graph checksums, nonzero padding, unsupported bindings/codecs, truncation and misuse of the EOF exception.

Sample L81 decodes to **1,084 meshes**, **602,484 vertices**, **200,828 triangles**, with no exact-backend dependency. All emitted triangle indices and normalized normals are checked. The local-coordinate bounds observed across mesh resources are approximately `(-584.731,-244.814,-163.409)` to `(774.547,2138.41,169.739)`; these are not transformed occurrence/world bounds. Cancellation during graph import is exercised. These counts are regression observations, not independently established ground truth.

The five original JT 10.0 part regressions and existing assembly, display selection, metadata, draw-style and vertex-colour tests pass. The prior vertex-colour viewer fixtures remain separate from the newly supported legacy path.

This work adapts the audited GPLv2-or-later upstream legacy reader and adds original bounds/validation/tests. It does not copy a proprietary SDK. Original notices and the provenance manifest are retained. Matching historical specification archival and an independent geometry/viewer comparison remain qualification work; the tests are not a claim of universal JT 8.1 conformance or patent clearance. See the existing [permissions research](JT_IMPORT_FEASIBILITY.md) for the scope of the published notices.

## Reproduction and owner checks

Using the standalone `tests/jt` build:

```text
jt_tests out/jt-research/samples <local-samples>/sample-L81.jt
jt_tests --legacy-fixtures out/jt-research/legacy8-fixtures
```

After rebuilding ModelViewer, open the original `sample-L81.jt`. Check that the building/equipment geometry is present, placements and colours are plausible, the hierarchy is usable, and the displayed mesh count is 1,084. Compare visually against an independent viewer or the source preview if available. A successful load alone does not prove fidelity.

The two generated `legacy8_two_strips_le.jt` and `legacy8_two_strips_be.jt` files are original synthetic geometry: each should show a blue 1-by-1 square (two triangles) and a separate blue triangle, with **7 vertices and 3 triangles**. Both should look identical. Check the original JT 10.0 Bolt still opens normally. The owner reports that JT 8.1 works; individual fixture results and an independent geometry comparison were not itemized. The full application has not been built by the agent.
