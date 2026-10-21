# Transitional colour arrays — Gate C1c

21 October 2026, `feature/JT-importer`. **Native tests pass; owner application build/UI tests passed on 21 October 2026 with "All tests passed".** The owner accepted C1b with "All tests passed". This gate adds the separate colour transports for enabled 9.0/9.5; no file revision is enabled and 10.11 stays deferred.

## Reference and implementation

The locally retained public Siemens JT 9.5 Rev-A section 8.1.7, Figure 230 on printed page 273, shows paired exponent/mantissa packets per lossless component and four quantized component vectors: Hue/Red, Sat/Green, Value/Blue and Alpha, all quantized vectors explicitly using Lag1. The diagram was rendered and inspected visually to resolve the fourth vector, which is serialized even when only three colour components are exposed. The right branch mistakenly repeats the label `QuantBits = 0`; the quantizer/vector definitions, hash description and uniform quantization section 8.2.1 identify the quantized branch. Section 8.1.11 specifies RGB/HSV quantizers. The reference and rendered inspection stay private under ignored research storage; neither is bundled.

This is not claimed to be a complete 9.0 specification. The 9.0 path shares the already qualified transitional reader; its new colour collection is checked with original vectors and a native 9.0 geometry template. No independently generated vertex-colour export or independent colour-reader oracle has been established for either revision in this pass.

The ModelViewer-original `JtDecode_VertexColors.hxx` now dispatches by format family:

- 9.x lossless: two raw CDP2 packets per exposed component, exponent then mantissa, without a Lag1 predictor. Exponent fields are bounded to nine bits and mantissae to twenty-three before reconstructing the IEEE word. Nonfinite/out-of-domain colour values are rejected.
- 9.x quantized: four CDP2/Lag1 arrays, each matching the declared colour count. The fourth array is read and validated even for RGB-only bindings. Incomplete/omitted alpha-array layouts are not guessed. Per-component width/range checks precede dequantization; RGB or HSV results use the shared bounded helpers.
- Enabled 10.x: existing packed quantized codes and lossless binary/Lag1 arrays retain their distinct transport. Earlier constant 8.x handling is unchanged.

Quantizer validation, inverse uniform reconstruction and HSV conversion are shared with C1b to avoid duplicating colour mathematics. Modern known-value and full-scene controls stay regression evidence. Nonconstant zero-bit quantizers remain explicit unsupported cases. Fractional alpha can be decoded as data, but active vertex-alpha rendering is still rejected by the scene adapter and remains C2 work. Colour hashes are consumed but not verified, as before. No new dependency, SDK or third-party decoder source is introduced.

## Tests and native controls

Original `jt9_colour_tests.cpp` covers 9.0 and 9.5, both byte orders, RGB/RGBA, exponent/mantissa values and four-vector quantized RGB/HSV. The vectors extend past four records to distinguish raw lossless fields from Lag1 quantized residuals. Tests check invalid exponent/mantissa high bits, NaN/negative/out-of-range reconstructed values, every truncated prefix, missing/short fourth arrays, invalid component codes including unused alpha, count mismatches, cancellation and budgets.

The isolated MSVC AddressSanitizer build and full portable suite pass, including C1a/C1b, the original modern-version field tests and all five prior 10.0 part regressions. Native representative regressions pass for all twelve enabled revisions (8.0, 8.1, 8.2, 9.0, 9.5, 10.0, 10.3, 10.5, 10.7, 10.8, 10.9, 10.10), with counts, bounds and attribute-channel summaries matching the C1b baseline. The eleven existing C1b coloured-control GLBs are also byte-identical after the shared-reader refactor. These are regression checks, not twelve independently generated 9.x colour oracles.

A new test-only `jt9_colour_fixturegen` modifies two separately sourced local single-part templates, one genuine 9.0 and one genuine 9.5. It retains each source's geometry, normals, index topology, metadata and units while writing the new colour bindings/arrays and necessary container lengths/offsets. It handles the observed node-local versions 1 and 2 and keeps the 32-bit TOC; it does not reheader 10.x geometry into a 9.x file. Both original source hashes were verified unchanged.

Each version has five private controls:

| Control | Transport | Expected display |
| --- | --- | --- |
| `lossless_rgb.jt` | Paired exponent/mantissa RGB | Red/green/blue patch reference |
| `lossless_rgba_opaque.jt` | Paired exponent/mantissa RGBA, alpha 1 | Same |
| `quantized_rgb.jt` | Four component vectors with RGB exposed | Same |
| `quantized_rgba_opaque.jt` | Four component vectors with RGBA exposed | Same, fully opaque |
| `quantized_hsv.jt` | Four HSV component vectors with RGB exposed | Same |

All ten controls pass native import and GLB export. Each has **one mesh, 218 vertices and 188 triangles**. Within each version, all emitted position, normal, colour and index arrays match its lossless RGB control through GLB. Source geometry/normals/indices are also checked directly against the original unmodified template. These are source-derived diagnostic controls, not authentic coloured vendor exports; placeholder colour hashes are not independent JT conformance evidence. They contain source geometry and must remain private/ignored.

Ignored evidence: `coverage-c1c-build.log`, `coverage-c1c-tests.log`, `coverage-c1c-final-test-build.log`, `coverage-c1c-final-tests.log`, `coverage-c1c-source-inventory.json`, `coverage-c1c-fixtures-9.0.log`, `coverage-c1c-fixtures-9.5.log`, `coverage-c1c-model-comparison.json`, `coverage-c1c-modern-comparison.json`, `coverage-c1c-native-summary.json` and `coverage-c1c-native-*.log`.

## Owner build and acceptance checklist

1. Rebuild the normal application on `feature/JT-importer`.
2. Open the five controls above, one at a time, under `out/jt-research/colour9-ui-fixtures/9.0/` and then `9.5/`. They should show matching red/green/blue patches on the same part within each set, with full opacity, unchanged smooth/flat boundaries and no missing geometry. Expect one mesh, 218 vertices and 188 triangles per control.
3. Check fit-to-screen and selection. Reopen a previously accepted modern packed-colour control to ensure C1b still displays correctly. Reopen an authentic assembly as a quick integration regression.

The private controls can be regenerated with `jt9_colour_fixturegen <local-single-part-9.x.jt> <output-folder>`; keep source geometry outside the repo. The current generator is deliberately limited to observed little-endian single-part templates; both byte orders are covered by original packet tests, not claimed as authentic big-endian coloured-model qualification.

C1c is owner-accepted; the next implementation remains within C1. Variable 8.x colours, other bindings, ambiguous zero-bit/omitted-array layouts, hash verification and vertex-alpha rendering remain later gates. No full viewer build, commit or push was performed by the agent.
