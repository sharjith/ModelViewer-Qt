# Rounded topological coordinates — Gate C1i

21 October 2026, `feature/JT-importer`. Native evidence is recorded below; owner application build/UI confirmation is pending. Accepted C1h was committed before this work. No new JT file version is enabled.

## Correction and qualification boundary

The inherited quantized coordinate reader used a midpoint reconstruction with a denominator of `2^bits` and a half-code offset. That remains the separately qualified legacy 8.x path. The enabled 9.x/10.x topological coordinate collection now uses the inverse of the rounded uniform encoder: `min + code * (max - min) / (2^bits - 1)`, with the 32-bit maximum handled without a shift overflow. Thus code zero maps to the minimum, the largest legal code maps to the maximum, and interior codes are evenly spaced between them. It reconstructs quantized positions; it does not recover precision discarded by the exporter or add triangles.

Sources retained privately are Siemens JT 9.5 Rev-A sections 8.1.4, 8.1.9, 8.1.12 and 8.2.1 (printed pages 267–268, 275, 278 and 287), and Siemens JT 10.0 sections 13.1.3, 13.1.9, 13.1.12 and 13.2.1 (printed pages 246–247, 256, 258 and 266). Both uniform-encoder descriptions use codes from zero to `2^bits - 1` with rounding. The JT 10.0 coordinate-array description explicitly requires three coordinate components and equal quantization widths in all axes. The shared qualified path requires that layout, checks coordinate counts before allocation, and rejects inconsistent widths explicitly. Constant axes use a constant range with the same positive width as the other axes. All-zero widths retain the existing lossless transport; this gate does not assign new meaning to nonconstant zero-bit quantization or mixed per-axis lossless/quantized layouts.

Original field tests span every enabled 9.x/10.x revision and both byte orders. Full private controls use genuine 9.0, 9.5 and 10.0 source templates separately, without reheadering. Applying the shared record-family rule to 9.0 is not a claim that the 9.5 reference is a complete 9.0 specification, or that the modified controls provide an independent vendor quantization oracle for every revision. Independent authored quantized exports remain desirable for wider conformance qualification.

The new reconstruction is selected only for topological compressed coordinate arrays. Lossless exponent/mantissa and IEEE fields, the legacy midpoint inverse, stored normals, colour/UV transport, scene graph structure and version admission remain unchanged. No third-party source package or dependency is added. Upstream notices and hashes remain intact. Coordinate hash fields are still consumed without verification; test controls use placeholders.

## Native validation

- The final isolated MSVC AddressSanitizer build and full test suite pass, including the five prior 10.0 part regressions. Private controls pass separately for all three source-template versions.
- Known-value coordinates test endpoints and interior values at widths 1, 3, 8, 24, 31 and 32, including sign-bit values and predictor wrapping, across every enabled 9.x/10.x revision in both byte orders. Constant axes, exact collection consumption, every truncated prefix, invalid counts/components/widths/codes, inconsistent axis widths, cancellation and budgets are checked.
- The existing legacy quantized fixture suite retains its earlier integer-position expectations, verifying that the 8.x midpoint path is unchanged. Existing lossless, normals, colours, UV, scene and export suites remain regression coverage.
- The original private fixture modifier creates 8-bit and 16-bit coordinate collections over source-derived geometry for 9.0/9.5/10.0. It scans every stored display LOD to establish common ranges, modifies each coordinate collection and retains all source topology/normals. Every rendered vertex is compared with an independently calculated rounded expectation; error must stay within half a code step plus float rounding. Scene copies, GLB corners, malformed width/code rejection and LOD fallback are checked.
- These modified models are diagnostic controls, not authentic vendor quantized exports. No source geometry is bundled or licensed for redistribution by inference.

Evidence stays ignored under research storage: `coverage-c1i-build.log`, `coverage-c1i-control-build.log`, `coverage-c1i-tests.log`, `coverage-c1i-fixtures-*.log` and native regression logs/summary. The agent does not build the full viewer or commit the new gate before owner confirmation.

## Owner check — no texture setup needed

Rebuild and start with `out/jt-research/coordinate-ui-fixtures/9.5/`:

1. Open **`lossless_reference.jt`**, then **`quantized_16.jt`**. Both should look like the same part with the same orientation, size and placement. Small positional rounding is allowed; there should be no gross shrinkage, displacement or broken triangles. Both have **one mesh, 218 vertices and 188 triangles**.
2. Open **`quantized_16.jt.glb`**. It should match the visible geometry and triangle count of `quantized_16.jt`; lighting/material presentation may differ through the GLB path. GLB may omit an unreferenced vertex, so its total vertex count need not exactly match the native JT count.
3. Open **`quantized_8.jt`**. It has the same triangle/vertex counts but deliberately coarser position precision. Small edges/details can move at close zoom. This is not a lower-density tessellation and does not need to look visibly different at normal scale.
4. **`invalid_width.jt`** and **`invalid_code.jt`** must fail explicitly. **`coordinate_lod_fallback.jt`** must load one usable part after rejecting the first alternative.
5. Repeat the positive comparison in the `9.0` and `10.0` folders. The 9.0 counts are also 218 vertices / 188 triangles; the native 10.0 JT controls have **913 vertices / 904 triangles**. Reopen an ordinary accepted legacy and modern assembly as application regressions.

Pass means matching high-precision/reference shape and counts, matching native/GLB quantized geometry, explicit negative-file errors and successful fallback. The numerical rounding checks run in native tests, so the viewer test is a visible regression check. Stop at C1i for owner confirmation. This does not complete C1 or the wider C2–C7 feature pass.
