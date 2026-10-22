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
10. Accepted and pushed model-linear mapping: [C2g texture coordinates](JT_TEXTURE_PLANAR_VALIDATION.md).
11. Accepted image formats: [C2h inline colour pixels](JT_TEXTURE_PIXEL_VALIDATION.md).
12. Accepted basic 9.x image mapping: [C2i textures](JT_9X_TEXTURE_VALIDATION.md).
13. Owner-accepted advanced image layout: [C2j advanced 9.x textures](JT_9X_ADVANCED_TEXTURE_VALIDATION.md).
14. Owner-accepted numeric image path: [C2k unsigned 16-bit image components](JT_TEXTURE_UINT16_VALIDATION.md).
15. Owner-accepted floating image path: [C2l float32 image components](JT_TEXTURE_FLOAT_VALIDATION.md).
16. Owner-accepted half image path: [C2m binary16 image components](JT_TEXTURE_HALF_VALIDATION.md).
17. Owner-accepted unsigned image path: [C2n unsigned 32-bit image components](JT_TEXTURE_UINT32_VALIDATION.md).
18. Current texture-control storage: [shared resources and reference mappings](JT_SHARED_TEXTURE_CONTROLS.md).
19. Owner-accepted signed image path: [C2o signed integer image components](JT_TEXTURE_SIGNED_VALIDATION.md).
20. Owner-accepted unsampled borders: [C2p unsampled image borders](JT_TEXTURE_BORDER_VALIDATION.md).

22. Owner-accepted sampled constant borders: [C2q RGBA constant-colour borders](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md).

23. Owner-accepted sampled explicit borders: [C2r authored border texels](JT_TEXTURE_EXPLICIT_BORDER_VALIDATION.md).

24. Owner-accepted border colour layouts: [C2s explicit colour rings and RGB constant borders](JT_TEXTURE_BORDER_FORMAT_VALIDATION.md).

Sibling validation documents retain the source, measurement and owner-acceptance records for earlier gates. Research documents belong here; generated evidence and fixtures remain under ignored `out/jt-research/`. The [repository handout](../../RESEARCH_FOLDER_HANDOUT.md) governs new work. See the [repository README](../../README.md) for user-facing import scope.
