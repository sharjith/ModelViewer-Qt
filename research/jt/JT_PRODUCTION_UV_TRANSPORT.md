# Production modern binary UV compatibility

25 October 2026, `feature/JT-importer`. Production failure investigation and correction; owner reports the full assembly opens successfully. Native regression-suite acceptance remains pending. C2 expansion remains paused. This is a concrete existing-model compatibility fix, not a new texture feature or C3 wire implementation.

## Supplied evidence

The owner supplied an assembly and companion folder after reporting a failed import with wireframe export disabled. The screenshot's final display rejection is `Nonfinite JT texture coordinate`, wrapped by exhausted display/LOD alternatives. This is distinct from the earlier unsupported-node production report. It disproves a general assumption that disabling wire export is sufficient for this assembly; it does not establish a cause for a different export's wire-related failure.

The read-only audit inspected 371 JT files (one root and 370 companions), all identifying JT10.9 LE with exporter header `DM 10.4.3.0`. Neutral root PA-000 is 261,183 bytes, SHA-256 `ab441a7733cd851660b9f27b04dff23461a6d563b13c18df760faace0682ebe8`. Derived reports, private file mappings and all source bytes stay local. The inventory contains 2,737 shape payloads and 1,022 PMI-type segments (type 3), but no precise wire segments (18) or polyline/point display node GUIDs in the inspected graphs. PMI segment presence is not validation of annotation contents or PMI preservation/display. The root/companion partition-reference audit checks the file's directory and supplied input root; it is not the native resolver or unrestricted external-file traversal.

Two companion controls contain the 13 unquantized UV payloads found in this package:

| Neutral ID | Bytes | SHA-256 | UV payloads |
| --- | --- | --- | --- |
| PA-064 | 185,133 | `e3bb9a31620564eb0348e76c86dc594dc40f9bb50468d83c4fcaed1cc5f721f1` | 12 |
| PA-341 | 26,566 | `fef1bfa0e9e857bb25406525f92bb2ca9b29513001187f62e51ff63612d7135e` | 1 |

All 13 have finite IEEE words directly after integer packet decoding. The lookup2 hash over each whole component array, chaining components in source order, exactly equals each payload's stored hash. Applying the current reader's additional Lag1 predictor instead creates 102 nonfinite words in ten payloads (nine in PA-064, one in PA-341). Three other payloads remain finite under Lag1 but their values are still wrong. Therefore changing only a finite-value guard or dropping the UV channel would not repair this transport error.

The partial Python audit locates attributes by walking triangle/TopoMesh envelopes and topology/coordinate/normal packet boundaries; it does not search arbitrary bytes for plausible UVs. It models the already retained packet readers, including arithmetic, bitlength, move-to-front and chopper records. It does not reconstruct topology, decode positions/normals, run the C++ importer or certify every envelope. No audit error was reported for the 2,737 inspected shape payloads within that deliberately limited scope.

## Prepared correction

`JtDecode_VertexUV.hxx` now initially decodes modern unquantized component words without prediction. If the complete component-array word hash matches the stored hash, it uses that authenticated-by-checksum transport interpretation. Otherwise it retains the pre-existing Lag1 interpretation. A format checksum is not a cryptographic authenticity guarantee. There is no heuristic based on finite values, UV ranges, filename, or a blanket version change. Scalar/UV component identity, negative/tiled coordinates, size/count/bounds/cancellation limits and rejection of nonfinite output remain in force. Quantized and 9.x paths retain their previous prediction rules.

This compatibility selection does not add general hash validation to the old path, which historically ignored the stored hash. A nonmatching raw hash is not accepted as evidence for unpredicted data; it follows the existing decoder and its checks. The retained JT10 text describes Lag1 for binary UVs whereas the inspected exporter payloads establish the unpredicted whole-array-hash profile. Do not rewrite that observed profile as universal JT10 behaviour or silently replace the old transport globally.

Original native regression cases cover scalar and two-component raw/hash and Lag1/hash forms, both byte orders, representative enabled modern versions, literal independently calculated hashes, and a hash-matching raw NaN that must still be rejected. Existing tests cover zero-hash Lag1 controls, quantized and 9.x transport, truncation, resource/cancellation and seam preservation. The native cases have been prepared, not executed by the agent.

## Reproduction and owner gate

Read-only package inspection (reports contain private paths and derived attribute measurements, so keep under ignored out/):

```text
python -B tools/jt/inspect_uv_transport.py <local-assembly-folder> --output out/jt-research/production-uv-audit.json
```

The tool accepts one modern LE JT or a folder containing the root and companions. Neutral IDs follow sorted local paths and should be checked against hashes when a package changes. Unsupported audit profiles are reported as errors, not interpreted as empty geometry. The report is not an application import pass.

Owner steps:

1. Build and install the viewer as usual.
2. In the configured standalone JT test build, rebuild `jt_tests` and run it (CTest name `jt_codec_tests`). For example, with the configured build at `<jt-test-build>`: `cmake --build <jt-test-build> --target jt_tests`, then `ctest --test-dir <jt-test-build> -R jt_codec_tests --output-on-failure`.
3. Reopen the supplied assembly with the companion folder intact. Confirm geometry, assembly hierarchy and the previously rejected parts appear, then smoke-check ADS/PBR and textured controls. A different first failure is new evidence, not permission to suppress required content.

On 25 October 2026 the owner reported: "Success! The assembly opened completely" after the correction was handed off. Record full-assembly opening as owner acceptance of this production blocker. This does not establish PMI rendering, wire support, native-suite success or exhaustive material fidelity. No agent build/native/viewer execution occurred. The earlier wire-export failure remains a separate open investigation.

## Method and provenance

Consulted the owner's screenshot and supplied local exports, existing GPL-noticed packet/attribute readers and original tests, plus retained Siemens JT10.0 Rev-B sections 13.1.5 and 14.6 and Annex C. Retained PDF SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; primary source/permission limits remain those in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). Annex C attributes the word-hash algorithm to Bob Jenkins (1996) and carries his permissive use statement. The new adapters express the existing integer algorithms and lookup2 word hash; no external decoder or vendor software was run, no new package downloaded or terms accepted, and no sample-derived bytes entered a tracked control. This records method and source attribution, not legal clearance or permission to redistribute models.
