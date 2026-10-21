# Modern packed quantized vertex colours — Gate C1b

21 October 2026, `feature/JT-importer`. **Native tests and owner application tests passed.** The owner accepted C1a with "All tests passed". This is the next subgate of [feature completion](JT_FEATURE_COMPLETION_PLAN.md), not a new file-version qualification. JT 10.11 remains deferred.

## Layout evidence and scope

The modern compressed colour collection is documented in the locally retained public Siemens JT 10.0 sections 13.1.6 / 13.1.11 / 13.2.1, Prostep ivip PSI14 Part1 V3 (JT 10.5-based) modern compressed colour collection, and Siemens JT10.6 Revision D section 10.2.6. The references agree on count/component/quantization headers, RGB or HSV quantizers, one packed Colour Codes vector, channel order with alpha in the least-significant field, and a trailing colour hash. Colour Codes are documented without a predictor; lossless binary component arrays separately specify Lag1. Uniform reconstruction here inverts the reference's round-to-nearest encoder with maximum code `(1 << width) - 1`; this does not change coordinate or UV quantizer mathematics.

The ModelViewer-original `JtDecode_VertexColors.hxx` now implements that packed path for the enabled 10.x family. It checks 3/4 components, expected record counts, widths up to eight bits per component, finite ordered ranges in the colour domain, packed high bits and packet/hash truncation. RGB quantizers can retain constant zero-bit components. HSV hue uses the documented [0,6] range; all six sectors and hue=6 wrapping are checked. Nonconstant zero-bit quantizers remain explicitly unsupported rather than assigning an undocumented default. No new dependency or third-party source package is introduced.

The enabled 9.0/9.5 colour collection is different: its quantized data uses component arrays and its lossless data uses exponent/mantissa streams. This gate does not interpret those arrays as modern packed words. Their qualification remains a separate C1 follow-up. Earlier 8.x constant colours are unchanged; variable packed/component/HSV forms remain future C1 work. Existing modern lossless IEEE/Lag1 handling is unchanged.

RGBA alpha values are decoded numerically and tested, including fractional values. Opaque RGBA display works through the existing scene adapter. Active non-opaque vertex alpha is still rejected there and requires the later C2 renderer/appearance gate. This is not advertised as transparency support. Colour hashes are consumed but not verified, as in the existing path; hash validation remains on the consolidation list.

## Original and full-scene tests

`jt_quantized_colour_tests.cpp` is original test data, not a vendor encoder. It checks both byte orders under every enabled modern minor revision: 10.0, 10.3, 10.5, 10.7, 10.8, 10.9 and 10.10. Known RGB/HSV primaries verify field order, full 32-bit opaque RGBA includes the high sign bit, two-bit RGB levels verify endpoints/interpolation, all six hue sectors and hue wrapping have explicit expectations, and alpha levels are checked independently. Every truncated prefix, invalid count/component/width/range/space, NaN range, packed high bits, zero-bit ambiguity, cancellation and allocation budgets are exercised. The older component-array family remains an explicit rejection case.

The isolated MSVC AddressSanitizer build and full portable suite pass, including C1a and all five original 10.0 part regressions. Native regression validation passes for all twelve enabled revisions: 8.0, 8.1, 8.2, 9.0, 9.5, 10.0, 10.3, 10.5, 10.7, 10.8, 10.9 and 10.10. Counts, bounds and attribute-channel summaries agree with the C1a baseline for each representative authentic input; see ignored `coverage-c1b-native-summary.json`. These are regression inputs, not twelve authentic quantized-colour exports.

Private full-model controls are generated with the extended existing `jt_color_fixturegen` using a locally obtained single-part 10.0 source. Geometry is not bundled or copied into tests. The generator preserves the original source geometry/normals/indices and changes only the colour bindings/payloads and required container lengths/offsets. Each positive control has one mesh, 913 vertices and 904 triangles.

Eleven coloured scene controls pass import and GLB export. All emitted position, normal, colour and index arrays match the independently encoded lossless-colour reference exactly, including packed RGB, HSV and opaque RGBA. The seven modern-header controls reuse the same local 10.0 geometry and are explicitly synthetic structural tests, not authentic exports or independent conformance proof for those revisions. Source vertex-colour hashes are placeholders; the GLBs/controls are internal diagnostics.

The existing intentionally unsupported fixture is now deliberately malformed with quantization width 9. Its LOD fallback test still confirms recovery through the valid lossless colour alternative. Material override and flat-colour controls remain checked by the generator. No renderer/shader code changes in C1b.

Ignored evidence: `coverage-c1b-build.log`, `coverage-c1b-tests.log`, `coverage-c1b-final-test-build.log`, `coverage-c1b-final-tests.log`, `coverage-c1b-model-fixtures.log`, `coverage-c1b-model-comparison.json` and `coverage-c1b-native-*.log`. Generated private controls are under `out/jt-research/quantized-colour-ui-fixtures/`. No external CAD or reference documents are committed.

## Owner build and acceptance checklist

Rebuild the normal application on `feature/JT-importer`. Open these local controls from `out/jt-research/quantized-colour-ui-fixtures/`:

1. `vertex_rgb.jt` — existing lossless red/green/blue patch reference.
2. `vertex_quantized_rgb.jt`, `vertex_quantized_hsv.jt`, `vertex_quantized_rgba_opaque.jt` — each should have the same patch colours, geometry and smooth normals as the reference, with full opacity. Expect one mesh, 913 vertices and 904 triangles each.
3. `vertex_quantized_rgb_10_0.jt`, `vertex_quantized_rgb_10_3.jt`, `vertex_quantized_rgb_10_5.jt`, `vertex_quantized_rgb_10_7.jt`, `vertex_quantized_rgb_10_8.jt`, `vertex_quantized_rgb_10_9.jt`, `vertex_quantized_rgb_10_10.jt` — same display across all enabled modern-version controls. These are synthetic headers, not vendor export qualification.
4. `vertex_override_blue.jt` should retain the earlier uniform blue override; `vertex_color_lod_fallback.jt` should still show one valid coloured part after the malformed alternative is rejected.

Check fit-to-screen, selection and colour consistency. Reopen a previously accepted authentic assembly as a quick regression. The generator can be rerun with `jt_color_fixturegen <local-single-part-10.0.jt> <output-folder>`; its source/geometry requirements and private-data restriction remain unchanged.

Stop at C1b for owner confirmation. This does not complete the older-family colour transports, nonconstant zero-bit cases, vertex-alpha rendering or the whole C1/C2 feature scope. No full viewer build, commit or push was performed by the agent.

## Owner acceptance — 21 October 2026

The owner reported "All tests passed" for C1b. Record the delivered modern packed-colour gate as accepted. Different earlier-family colour transports, active vertex-alpha rendering and the other feature stages remain separate. No commit or push was requested.
