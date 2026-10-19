# JT 8.x shared layout family: lossless and quantized coverage

19 October 2026, `feature/JT-importer`. This document records the original shared lossless **8.0–8.2 layout family** milestone. **The owner accepted Gate 0.** The subsequent [quantized follow-up](JT_LEGACY_QUANTIZED_VALIDATION.md) now imports sample Q82 and has passed Gate 1 UI acceptance. The initial encoding rejection described below is historical; it is not the current sample Q82 result. Coverage remains representation-specific rather than a claim that every file in the family renders.

## Why the 8.2 version error was misleading

The previous file-version whitelist rejected 8.2 before reading scene/geometry records. It established a qualification boundary but did not identify an actual layout incompatibility. The shared family gate now admits 8.0, 8.1 and 8.2 to the same checked legacy path. Unsupported encodings are diagnosed where encountered, rather than described as requiring a separate importer for each minor revision.

The sample Q82 sample uses quantized vertex data with normal, texture-coordinate and colour bindings. At Gate 0, the qualified 8.x reader handled lossless positions with absent/per-vertex normals, not those quantized/attribute streams. An 8.0 or 8.1 file using the same encoding would have failed too. The family pass identified **JT 8.x quantized vertex records**, before the less informative raw-binding rejection. Phase 1 subsequently implemented the shared geometry/attribute feature without an 8.2-specific scene model.

## Original local inputs

| Input | Header | Bytes | SHA-256 |
| --- | --- | --- | --- |
| `<local-samples>/sample-L80.jt` | `Version 8.0 JT` | 1,645,630 | `a05a2e48daa6b145ace7d39479b5a2757aa81af3936c8bf5b6932415e053e7d7` |
| `<local-samples>/sample-Q82.jt` | `Version 8.2 JT DM 8.3.7.1` | 6,700,459 | `1878337368d309f33d27099bbe7b2bc111bfb9994ba3af6f2f937e056e734a36` |

Both original files remain unchanged outside version control. Exact download URLs/redistribution permissions have not been recorded. Diagnostic header-modified copies under ignored `out/jt-research` locate decoding gaps; those copies are not conversions or delivered workarounds. Validation uses the original files after changing the family gate.

Sample L80 has a 32-bit directory at 1,644,058 with 56 entries: 54 type-7 display segments, one type-4 segment and one scene graph. Native import emits **54 mesh resources and 54 occurrences**, **185,395 vertices** and **118,781 triangles**. Geometry checks cover finite positions, normalized normals, triangle indices, material/mesh references and finite local transforms. Local resource bounds are approximately `(-44.7158,-121.5,-100.3)` to `(69.6,52.4409,260.895)`, not transformed world bounds. These are regression observations, not independent geometric ground truth.

The sample Q82 has a 32-bit directory at 105 with 20 entries: one scene graph, seven type-4 segments, six type-3 segments and six type-7 display segments. Observed display records use local-version-1 layouts and quantization parameters including **23 position bits**, with per-vertex normal/UV/colour bindings. No mesh count or rendering-fidelity claim is made for this unsupported input.

## Regression coverage and remaining implementation

Original generated full-file fixtures exercise the shared 8.0/8.1/8.2 layout with little/big-endian records, raw/zlib lossless vertices, strips, normals, transforms, materials and typed metadata. Null/bitlength integer packets and malformed data/bindings are checked across the family. Deliberately unsupported quantized headers produce the same encoding diagnostic for all three minors. These fixtures establish our supported layout contract, not independent coverage of all historical encodings.

The sample L81 shortened-final-segment exception remains **8.1-only** and is rejected for 8.0/8.2. Do not extend writer-specific tolerance merely because versions share the normal layout. Existing 8.1 and 10.0/10.3 regressions remain required.

The isolated MSVC AddressSanitizer run passes the original sample L80 `--validate` check, the expected original sample Q82 quantized-encoding rejection, the portable suite (including the 10.3 container fixtures), all five 10.0 part regressions and the sample L81 geometry/cancellation regression. Logs remain under ignored `out/jt-research`. This is native validation; it does not establish owner UI acceptance or independent visual equivalence for sample L80.

The initial next work was quantized 8.x data: integer codecs used by real inputs, quantized positions and normals, UV/colour array envelopes and bindings, attribute/vertex indices and strip reconstruction. The [Phase 1 follow-up](JT_LEGACY_QUANTIZED_VALIDATION.md) implements a qualified subset of these features with checked counts, bounds, cancellation and malformed-input tests. Further encodings still need their own evidence and validation; do not merely reactivate unqualified upstream branches. Preserve appearance seams and diagnose unsupported appearance fidelity.

The overall target remains **6.4–10.11**, with the owner's conditional 7.0 lower cap for substantial 6.4-specific viewer model changes. See [compatibility policy](JT_VERSION_COMPATIBILITY.md) and [legacy assessment](JT_LEGACY_COMPATIBILITY_ASSESSMENT.md). The renderer and owned scene boundary are unchanged by this family pass; no new third-party implementation or proprietary SDK was introduced.

## Reproduction and owner checks

```text
jt_probe --validate <local-samples>/sample-L80.jt
jt_probe --validate <local-samples>/sample-Q82.jt
jt_tests out/jt-research/samples <local-samples>/sample-L81.jt
```

The sample Q82 command was expected to fail at Gate 0; it now succeeds in the Phase 1 native reader. Use the current [Gate 1 checklist](JT_LEGACY_QUANTIZED_VALIDATION.md) for sample Q82 and new original fixtures. The owner accepted the Gate 0 baseline. The agent does not build the full application or infer Gate 1 acceptance from that baseline result. No commit until the applicable owner tests pass.
