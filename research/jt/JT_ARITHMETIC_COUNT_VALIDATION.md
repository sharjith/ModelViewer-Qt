# Legacy arithmetic symbol counts — Gate C1h

21 October 2026, `feature/JT-importer`. Native validation and owner application build/UI checks passed on 21 October 2026. This gate follows accepted C1g and adds no JT version or new codec.

## Corrected behavior

The inherited CDP1 arithmetic driver already followed probability-context transitions, including escapes that emit an out-of-band (OOB) value only in context 0. Its packet reader consumed a separate Symbol Count for multi-context packets but discarded it. Decoding stopped when the requested output-value count was reached and otherwise used a heuristic step cap. That could ignore a declared non-emitting tail, accept inconsistent counts or reject sufficiently long valid context-transition sequences.

The reader now passes the declared CDP1 symbol count to the arithmetic driver. Single-context CDP1 uses the output-value count as its implicit symbol count. The driver processes exactly that many symbols, including context-only escapes after the final output value, rejects extra output before writing, checks the final output count and requires complete OOB consumption. Counts remain bounded to 16 million symbols; cancellation polls continue during non-emitting tails. This bound is an implementation resource limit, not a file-format maximum. Modern CDP2/CDP3 retain their existing driver and are not forced through the CDP1 contract. Existing arithmetic code-text loading, word padding and interval arithmetic are unchanged; this gate does not add hash or exact code-bit verification.

Reference: the retained public Siemens JT 9.5 Rev-A section 8.1.1 (printed page 255) distinguishes Value Element Count and Symbol Count and defines context-0 escape emission; section 8.1.1.1.1 (page 257) and the Appendix C driver description specify decoding by symbol count and the Next Context field. These definitions also match the inherited licensed CDP1 context-switch behavior. This is not presented as a complete historical 8.x specification. No proprietary SDK, schema, new third-party package or reference-document copy is introduced. Upstream notices and source identities remain intact.

## Applicability and native tests

| Family | C1h applicability |
| --- | --- |
| Enabled 8.0–8.2 CDP1 arithmetic | Exact declared symbol/value agreement; original LE/BE fields and full controls |
| Enabled 9.x/10.x topological CDP2/CDP3 | Existing transport unchanged; regression coverage |
| CDP1 Huffman/null/bitlength | Unchanged; multi-context Huffman is still unsupported |

Original known-value packets cover context-0 OOB emission, non-emitting context-1 escapes, tails ending on an emitting or non-emitting symbol, ordinary symbols with inherited associated values, negative/short/excessive symbol counts, extra/missing output, OOB underflow/leftovers, invalid next contexts, every truncated prefix, allocation limits and cancellation during a long non-emitting tail. A 5,000-symbol valid tail demonstrates that the former output-based heuristic is not used for counted packets. Existing multi-context malformed-loop tests now expect the precise output-count mismatch instead of a heuristic step-limit error.

Original complete models span 8.0/8.1/8.2 and both byte orders. A pair of singleton escape contexts alternates, emitting strip boundaries 0,4,7 from three OOB values and then consuming a final non-emitting escape. Counts are checked against an otherwise identical null-coded reference: one mesh, seven vertices and three triangles, with every position, normal and triangle index equal. Short/long count controls reject. A bad alternative followed by a usable alternative must retain LOD fallback.

The isolated MSVC AddressSanitizer build and full test suite pass, including the five prior 10.0 part regressions. Representative authentic-file regressions across all twelve enabled versions pass with previous geometry counts, bounds, occurrences and attribute-channel summaries unchanged. All six endian/version model controls, their null-coded references and the malformed/fallback fixture generation checks pass.

These packets and geometry are original tests. No authentic vendor-export multi-context arithmetic conformance oracle has been added by this gate; authentic files remain regression evidence. Native evidence stays ignored under research storage: `coverage-c1h-build.log`, `coverage-c1h-tests-build.log`, `coverage-c1h-tests.log`, `coverage-c1h-fixtures.log` and native regression logs/summary. The agent did not build the full viewer. The owner subsequently confirmed all application checks passed. The accepted gate was committed as `066a468` before C1i work.

## Owner check — no texture setup needed

Rebuild the viewer and use `out/jt-research/arithmetic-count-ui-fixtures/`. These are original geometry, not community models.

1. Open `arithmetic_8_2_le.jt`. It should show two blue pieces: a square made of two triangles and a separate triangle. Mesh Info should report **one mesh, seven vertices and three triangles**.
2. Open `arithmetic_8_2_le_reference.jt`. It should have the same geometry, colour, placement and counts. This reference uses uncompressed integer values for the same strip boundaries.
3. Open `arithmetic_8_2_be.jt`; it should match the little-endian control. The corresponding 8.0/8.1 pairs provide the same cross-version check.
4. Open `invalid_short_count.jt` and `invalid_long_count.jt`: each must report a load error. Open `count_lod_fallback.jt`: it must load the usable one-mesh/seven-vertex/three-triangle geometry despite the bad first alternative.
5. Reopen one ordinary accepted legacy and one modern assembly as application regressions.

Pass means matching positive/reference geometry and counts, explicit failure for both invalid files and successful fallback. The owner confirmed all C1h application checks passed on 21 October 2026. Exact pixels can differ with camera/lighting settings; no texture assignment or UV selector change is involved.

## Remaining mesh work

C1 is still open: multiple-context Huffman, variable/component/HSV legacy colours, broader legacy bindings, zero-bit nonconstant semantics, higher/projective UV layouts and additional mesh representations remain unqualified. Historical layout evidence and independent inputs are still needed for some paths; renderer data-model limits apply to others. This gate does not remove those dependencies or mark C2–C7 complete.
