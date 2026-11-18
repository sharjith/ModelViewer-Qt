# JT 10.6 sampled-subset qualification

18 November 2026, `feature/parasolid-importer`. This gate adds explicit JT 10.6 support from one authentic local file, identified here as **T106-A**. The original name, path and geometry remain private and untracked.

## Evidence and scope

T106-A is a little-endian JT 10.6 file, 35,177,669 bytes, SHA-256 `5ad56dedf58e5870817d486943148379d334856095a383de791454225e8ab026`. Read-only structural inspection finds 827 TOC entries: one LSG segment, 649 shape-LOD segments (223 type 7, 219 type 8, 171 type 9 and 36 type 10), 176 metadata segments and one type-31 segment. No XT, JT B-Rep or PMI segment is present.

The file uses the already-qualified modern graph and stored-tessellation family. The native probe decodes 223 shape payloads and builds 175 mesh occurrences without fallback: 3,169,613 vertices, 3,071,346 triangles, 13 colour-bearing meshes and no UV, line or point primitives. It validates 838 nodes, all local mesh bounds, stored-normal lengths and transforms. The resulting bounds are finite. Repeated unsupported or unresolved metadata pairs are reported and remain outside this gate; they do not remove display geometry.

This is representative sampled-subset evidence, not universal JT 10.6 support. It does not qualify every record, codec, metadata atom, external partition, texture layout, PMI form or precise representation permitted by JT 10.6.

## Implementation and validation

The explicit file-version whitelist adds only minor version 6; it is not replaced by a numeric range. Existing original little- and big-endian container, coordinate, UV, quantized-colour, attribute, palette, texture and malformed-record matrices add 10.6 using the already-qualified modern envelopes. Unsupported 10.4 and 10.11 headers remain negative cases.

The AddressSanitizer-enabled native targets built successfully with MSVC 19.52.37029. The registered `jt_codec_tests` CTest passed 1/1 in 338.42 seconds. The unchanged authentic sample passed `jt_probe --validate` in 42.528 seconds with the counts above and 703,704 charged decoder-allocation bytes. The owner viewer comparison is the remaining gate.

```text
python -B tools/jt/inspect_jt_structure.py <local-samples>/sample-T106-A.jt --output out/jt-research/jt106-candidate-structure.json
<jt-test-build>/jt_tests
<jt-test-build>/jt_probe --validate <local-samples>/sample-T106-A.jt
```

For the UI gate, open unchanged T106-A from the installed viewer. Verify that the complete assembly appears, hierarchy and transforms are plausible, fitting and selection work, colours are credible, and no missing-display-content error is shown. Compare against a trusted viewer if available. Exact geometric and appearance parity remains unclaimed until that check is reported.

No proprietary SDK, vendor implementation or third-party decoder was used. The implementation reuses the existing modified GPL reader; the retained public JT 10.6 reference informs terminology and record boundaries. This statement records method and evidence, not legal clearance.
