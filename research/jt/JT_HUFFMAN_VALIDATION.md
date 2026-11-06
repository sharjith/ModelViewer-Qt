# Bounded legacy Huffman — Gate C1a

21 October 2026, `feature/JT-importer`. **Native validation and owner application tests passed.** This is the first subgate of the [feature completion plan](JT_FEATURE_COMPLETION_PLAN.md). It adds a missing mesh integer-codec path without enabling a new JT revision. JT 10.11 remains deferred.

## Implemented scope

The enabled 8.0–8.2 family can now read single-context CDP1 Huffman packets. The inherited GPL decoder's heap insertion/removal order, including equal-frequency tie behaviour and left=1/right=0 traversal, is retained. Tree nodes and the heap use charged vector ownership instead of leaking raw allocations. Traversal is nonrecursive; the unused recursive code-assignment pass is removed. Single-symbol alphabets require no input bits, unsigned high frequencies remain correctly ordered, declared code-text bits cannot be supplied by unused final-word padding, and escaped values cannot underflow or remain silently unconsumed. Tree/output allocations retain existing limits and cancellation polling.

Probability-context and nested OOB parsing retain the existing bounded CDP1 readers. More than one Huffman context is explicitly rejected in this subgate and remains tracked under C1. Arithmetic multi-context support is unchanged. This does not add a Huffman mode to the different CDP2/CDP3 transports used by the enabled 9.x/10.x geometry. No new library, SDK or proprietary schema is added.

The implementation is derived from the already vendored, pinned Open CASCADE JT-Assistant source. Existing copyright/GPL notices and upstream hashes remain in the manifest; this is not represented as independently originated translator code. The original hand-calculated fixtures are ModelViewer tests. No authentic vendor-produced Huffman model has been established in the current corpus, so codec qualification is based on original packet and full-scene evidence, not a claim of independent real-export Huffman agreement.

## Automated evidence

The isolated MSVC AddressSanitizer build and full portable suite pass, including the five prior 10.0 part regressions. Original `jt_huffman_tests.cpp` checks:

- Independently calculated unequal-frequency codes 11/10/0, two-/three-way ties and codes crossing a 32-bit word boundary, in both byte orders for 8.0/8.1/8.2.
- Single ordinary and escape-only alphabets, escaped values, unsigned high frequency and complete packet consumption.
- Every truncated packet prefix, declared-bit exhaustion despite readable word padding, empty/invalid probability tables, unsupported context counts and missing/extra escaped values.
- Cancellation and charged allocation limits.

Original full-model fixtures use Huffman-coded strip boundaries 0,4,7. Quantized geometry tests also exercise this codec with positions, normals, UVs and constant colours. The previous deliberately malformed codec-2 examples are replaced with invalid codec 4, preserving negative coverage. Both the original packet tests and scene tests are needed; header recognition alone is not decoder evidence.

Native regression validation passed for every enabled revision:

| Revision | Mesh resources | Vertices | Triangles |
| --- | ---: | ---: | ---: |
| 8.0 | 54 | 185,395 | 118,781 |
| 8.1 | 1,084 | 602,484 | 200,828 |
| 8.2 | 6 | 115,176 | 205,355 |
| 9.0 | 23 | 210,221 | 184,618 |
| 9.5 | 23 | 210,256 | 184,618 |
| 10.0 | 23 | 210,256 | 184,618 |
| 10.3 | 2,468 | 4,450,421 | 5,844,669 |
| 10.5 | 23 | 210,257 | 184,618 |
| 10.7 | 23 | 210,257 | 184,618 |
| 10.8 | 23 | 210,257 | 184,618 |
| 10.9 | 23 | 210,257 | 184,618 |
| 10.10 | 46 | 1,054,489 | 1,105,712 |

These are representative pre-existing samples for regression, not twelve native Huffman source files. Each probe validates finite geometry/transforms, indices and stored-normal lengths. The 10.3 B-Rep fallback count describes existing stored-tessellation selection and is not a new geometry failure. Resource sums differ from repeated-instance occurrence totals. Source models remain private and are not renamed, rewritten or bundled.

All six original Huffman scene fixtures additionally pass `jt_probe --validate` and GLB export: one resource/occurrence, seven vertices, three triangles and local bounds (0,0,0) to (4,1,0). Diagnostic GLBs and all logs remain ignored under `out/jt-research`. Evidence: `coverage-c1-build.log`, `coverage-c1-tests.log`, `coverage-c1-fixtures.log`, `coverage-c1-native-summary.json`, `coverage-c1-native-*.log` and `coverage-c1-ui-validation.json`. Provenance and whitespace/document checks pass.

## Owner build and acceptance checklist

Rebuild the normal application on `feature/JT-importer`. Generate the original fixtures if needed with:

```text
jt_tests --huffman-fixtures out/jt-research/huffman-ui-fixtures
```

Open these six files from that directory:

- `huffman_80_le.jt`, `huffman_80_be.jt`
- `huffman_81_le.jt`, `huffman_81_be.jt`
- `huffman_82_le.jt`, `huffman_82_be.jt`

Each should show the same blue planar quad (two triangles) and separate triangle: one mesh, seven vertices, three triangles. No external companion is required. Check fit-to-screen, selection and matching geometry across byte orders. Optional redundant-node purging may shorten hierarchy. Reopen a previously accepted authentic 8.x model and a recent engine/assembly as quick application regressions.

Stop at C1a for owner confirmation. This does not complete C1 or the other feature stages. No full viewer build, commit or push was performed by the agent for this gate.

## Owner acceptance — 21 October 2026

The owner reported "All tests passed" for C1a. Record this gate as accepted; later codec/attribute features remain separate gates. No commit or push was requested.
