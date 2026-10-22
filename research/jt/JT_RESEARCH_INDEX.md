# JT research index

22 October 2026, `feature/JT-importer`.

Suggested reading order:

1. [Feasibility and legal scope](JT_IMPORT_FEASIBILITY.md), [JT/Parasolid implementation scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md), [pass-specific provenance](JT_PROVENANCE.md) and [third-party notices](../../THIRD_PARTY_NOTICES.md).
2. [Native importer](JT_NATIVE_IMPORTER.md), [version compatibility](JT_VERSION_COMPATIBILITY.md) and [development history](JT_DEVELOPMENT_PHASES.md).
3. [Feature-completion gates](JT_FEATURE_COMPLETION_PLAN.md), including unresolved mesh dependencies and the independent appearance track.
4. Latest accepted mesh gate: [C1i coordinate quantization](JT_COORDINATE_QUANTIZATION_VALIDATION.md).
5. Accepted shaded raster appearance gate: [C2a vertex alpha](JT_VERTEX_ALPHA_VALIDATION.md).
6. Accepted wire appearance gate: [C2b wire vertex alpha](JT_WIREFRAME_ALPHA_VALIDATION.md).
7. Accepted ray-traced appearance gate: [C2c ray-traced vertex alpha](JT_RAYTRACED_ALPHA_VALIDATION.md).
8. Committed image mapping and owner-confirmed UV correction: [C2d/C2e textures](JT_TEXTURE_DESCRIPTOR_VALIDATION.md).
9. Accepted and pushed affine mapping: [C2f texture matrices](JT_TEXTURE_TRANSFORM_VALIDATION.md).
10. Current owner build/test handoff: [C2g model-linear texture coordinates](JT_TEXTURE_PLANAR_VALIDATION.md).

Sibling validation documents retain the source, measurement and owner-acceptance records for earlier gates. Research documents belong here; generated evidence and fixtures remain under ignored `out/jt-research/`. The [repository handout](../../RESEARCH_FOLDER_HANDOUT.md) governs new work. See the [repository README](../../README.md) for user-facing import scope.
