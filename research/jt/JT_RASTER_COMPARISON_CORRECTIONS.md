# Shared raster comparison corrections

24 October 2026, `feature/JT-importer`. Owner accepted the remaining comparison correction after rebuilding. See [C2ab validation](JT_VERTEX_REPLACE_VALIDATION.md) for the detailed investigation, original control generation and opt-in capture commands.

Three shared rendering seams were exposed by equivalent original JT and glTF controls:

- Mesh blend-state changes selected straight-alpha blending although the surface shader already premultiplies coverage. Both mesh paths now retain the viewport's premultiplied blending contract.
- Refreshing asymmetric light-slider ranges implicitly replaced the light offset with range midpoints. Range synchronization now preserves the exact existing runtime offset and blocks slider signals.
- JT-qualified scenes received a synthetic fallback point light in addition to raster's default light, while glTF scenes were excluded from that fallback. The synthetic-fallback branch now exposes zero raster punctual lights. Authored punctual lighting retains its existing path; other consumers of the synthetic buffer remain unchanged.

The owner confirmed the RGB comparison, later the corrected shadow extent, and finally the remaining comparison after the duplicate-light correction. Capture from matching Y-up views found identical camera positions and directions, rejecting a camera mismatch as the cause. No orthographic shader correction was made. ADS had also been reported discrepant during the investigation; the final owner report was that the correction fixed the comparison, without an individually enumerated mode/backend matrix.

Opt-in diagnostics use `MODELVIEWER_APPEARANCE_DIAGNOSTICS` with an absolute local file under ignored `out/`. They record bounded scene state and deduplicated primary draw state, with a 1 MiB file cap. Final light ambient/diffuse/specular values are GPU uniform readbacks; other draw fields are CPU submission values. No model names, source paths, texture payloads or vertex arrays are recorded. Ordinary launches do not capture evidence.

Validation limits: the agent ran static source/diff review and read-only local evidence analysis, not a C++ build or native test. Owner viewer acceptance does not establish backend-specific coverage, general transparent ordering or ray-tracing light assembly parity. Blend linear and opaque-interior smoke results were not separately reported in this investigation. Preserve these controls for the next owner gate.

Provenance: existing project source, original project controls, owner screenshots and local diagnostics only; no new external source or third-party implementation was consulted. This records method and scope, not legal clearance or authentic exporter conformance.
