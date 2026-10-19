# Phase 1: shared legacy quantized tessellation

19 October 2026, `feature/JT-importer`. The owner accepted **Gate 0** with “All tests passed.” This follow-up implements the quantized 8.x subset needed by sample Q82 and passed **Gate 1** on 19 October 2026: the owner reported “Tests passed, commit, push, and proceed”. The owner built the full application; the agent ran isolated native checks.

## Implemented feature boundary

The 8.0/8.1/8.2 layout family now shares both the existing lossless path and a checked quantized path:

- CDP1 null, bitlength and arithmetic integer packets, including recursive out-of-band values and multiple probability contexts. Huffman remains explicitly unsupported in the qualified 8.x path.
- Quantized XYZ positions, stored Deering normals and the unique vertex/attribute index list. Indices are checked against the decoded records; triangle-strip winding restarts at each strip.
- Per-vertex quantized UV arrays, retained as Assimp texture-coordinate channel 0. UVs survive render copies, appearance variants, flat-normal corner expansion and application scene copying.
- The **packed constant RGB(A) quantized-colour form** observed in the corpus, preserved without multiplying by an unrelated diffuse tint. Nonconstant packed colours, separate component-array colours and HSV quantizers are explicitly unsupported. This pass does not claim general quantized colour coverage.

Texture images, shader interpretation, per-facet/per-primitive bindings and active vertex translucency remain outside this subset. Retained UVs do not imply imported textures. The importer emits an explicit warning when UV-bearing source meshes are retained without texture-image/shader interpretation. The sample Q82 can therefore render with its stored constant vertex colours while lacking texture detail.

No new viewer scene model, separate 8.2 importer, additional accepted file version or exact-geometry backend was needed. Validated stored tessellation remains the display path.

## Decoder checks

The new path validates quantizer ranges/widths, finite values, packet/array counts, normal codes, vertex indices and attribute bindings before building render arrays. Uniform quantizer arithmetic avoids signed shift overflow. Predictors use explicit modulo-32-bit arithmetic. UV and colour records are decoded and checked, not skipped heuristically.

CDP1 probability tables and next-context references are bounded. Out-of-band counts must match their nested packet. Code-word counts must match declared bit lengths. Recursion, arithmetic steps and cancellation checks bound malformed or nonproductive streams. Arithmetic range products use 64-bit intermediates, including the full 65,536-frequency case. Existing per-allocation, cumulative import and segment limits remain in force.

The adapted quantized position/normal and arithmetic components come from the already audited GPLv2-or-later Open CASCADE subset, with notices retained. UV/constant-colour adaptation, validation and fixtures are original ModelViewer code. Local sample stream boundaries and independently calculable fixtures establish this qualified subset; archival of a matching historical reference remains separate qualification work. The publicly viewed Go package API/type documentation was an orientation reference; no Go implementation was copied or added as a dependency. No proprietary SDK implementation or restricted schema catalogue was introduced. Existing [reference/source permissions and limitations](JT_IMPORT_FEASIBILITY.md) still apply.

## Original sample Q82 result

Input: `<local-samples>/sample-Q82.jt`.

- Header: `Version 8.2 JT DM 8.3.7.1`.
- Size: 6,700,459 bytes.
- SHA-256: `1878337368d309f33d27099bbe7b2bc111bfb9994ba3af6f2f937e056e734a36`.
- The original file is unchanged and remains outside version control; download/redistribution rights have not been established for repository inclusion.

Native import emits **6 meshes/occurrences**, **115,176 vertices** and **205,355 triangles**, with **32 nodes** before optional redundant-node purging. All six meshes retain vertex colours; five retain UV channel 0. The observed colours are constant RGB values of approximately 0.8 or 0.9, with opaque alpha. All emitted indices, normalized normals, positions, local transforms, material/mesh references and retained UV/colour values pass validation.

Local mesh-resource bounds are approximately `(-6.38321,-1.27098,-3.793)` to `(4.8162,16.2665,2.90927)`. These are not world bounds or independently established geometric ground truth. Stored source units/placement are retained; no unit inference is claimed.

Other local native results from this pass:

| Input | Mesh resources | Vertices | Triangles |
| --- | --- | --- | --- |
| `sample-L80.jt` | 54 | 185,395 | 118,781 |
| `sample-R1.jt` | 318 | 246,555 | 82,185 |
| `sample-R2.jt` | 38 | 31,344 | 28,356 |
| `sample-R3.jt` | 63 | 52,261 | 49,745 |

These are local rendering/geometry regressions, not broad authoring-system conformance or independent visual comparison. Their source CAD files remain outside the repository.

## Automated checks and reproduction

`tests/jt/jt_quantized_tests.cpp` and the extended original `JtLegacyFixture.h` cover little/big-endian 8.0/8.1/8.2 containers with known integer coordinates, analytically known +Z normals, exact UV values, constant RGB colours and a deliberately permuted vertex-index list. The small fixture has seven vertices and three triangles across two strips, so index mapping and strip winding can be checked independently of a large CAD sample.

Arithmetic fixtures cover known interval-halving outputs, out-of-band and multiple-context transitions, full-range frequency arithmetic, truncated streams, invalid context/count references, recursive-depth rejection, nonproductive context cycles and cancellation. Geometry fixtures reject mismatched attribute counts, invalid indices/ranges/normal codes and unsupported colour layouts. Appearance/flat-normal copies and application copying preserve UVs and colours. GLB roundtrips compare geometric corner tuples because exporters may validly reorder vertex records.

Run with the standalone MSVC AddressSanitizer build and dependency/runtime DLLs on `PATH`:

```text
jt_tests out/jt-research/samples <local-samples>/sample-L81.jt
jt_tests --quantized-fixtures out/jt-research/quantized-ui-fixtures
jt_probe --validate <local-samples>/sample-Q82.jt
jt_probe --validate <local-samples>/sample-A103.jt
```

Logs and generated native acceptance files stay under ignored `out/jt-research`. The source serializer/tests are in the repository. Existing lossless 8.x, sample L81, modern part, assembly/style/metadata/colour and 10.3 regressions remain required.

Before this handoff, the isolated MSVC AddressSanitizer portable suite, all five original 10.0 part regressions and sample L81 checks pass. The original sample A103 regression retains 2,468 resources, 2,853 occurrences, 4,450,421 vertices and 5,844,669 triangles. Final sample Q82 and generated LE/BE acceptance-file validation passes. Source-provenance, whitespace and document-link checks pass. The owner accepted Gate 1 on 19 October 2026 with “Tests passed”. Independent authoring-system comparison remains unrecorded.

## Gate 1: owner build and UI check

Rebuild the current checkout on `feature/JT-importer`, then check:

1. Open the original sample Q82. It should now render with **6 meshes, 115,176 vertices and 205,355 triangles**. Check complete geometry, plausible placement, smooth shading, selection and fit-to-screen. Texture detail is not expected in this pass; the stored constant vertex colours are retained.
2. Open `out/jt-research/quantized-ui-fixtures/legacy_quantized_le.jt` and `legacy_quantized_be.jt`. Both should look identical: a blue-purple unit square and a separate triangle, **one mesh, 7 vertices, 3 triangles**, with consistent +Z shading. Their common local translation is deliberate and should not affect fitting the model.
3. Reopen a known-working legacy model (sample L81 or sample L80) and `out/jt-research/samples/Bolt.jt`. Their prior appearance and counts should remain unchanged.

The owner accepted this checklist and authorized committing, pushing and starting the modern 10.9 palette phase on 19 October 2026. Secondary appearance work can be split into a subsequent concrete gate if additional samples require nonconstant quantized colours or textures.
