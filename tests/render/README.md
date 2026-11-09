# Optional native OpenGL checks

Configure this folder as a standalone CMake project against the application's Qt dependency prefix, or enable `MV_BUILD_RENDER_TESTS` in the application build. These tests need a desktop OpenGL 4.5 core context; they do not open a visible window. They are separate from the GUI-free importer/core suite. CTest reports a skip (77) if a suitable context cannot be created; a skip is not rendering evidence.

On Windows, put the dependency `bin` folder on PATH and make Qt's native platform plugins available (the application prefix uses `Qt6/plugins`). Run `material_wireframe_tests`, or add `--artifacts <folder>` to save wireframe and subsequent solid-frame PNGs.

The test uses the production scoped raster-state helper, real shaders and an offscreen framebuffer. It checks triangle interiors/coverage, solid rendering after wireframe, alternating materials, incoming fill/line/point modes, nested scopes, exception cleanup and OpenGL errors. It also compiles the actual Material implementation and checks runtime JSON serialization of the wireframe flag. Full application checks still need to exercise actual imported meshes, viewport modes, overlays and MVF document round trips.

The opt-in `MV_RT_VERTEX_ALPHA_TESTS=ON` target `rt_vertex_alpha_tests` links the actual CPU path tracer and Embree intersector. Prepared original overlapping-triangle tests check alpha interpolation, OPAQUE/MASK/BLEND behavior, endpoints, scalar multiplication and statistical foreground coverage through renderer guide albedos. It needs Embree and Qt Widgets but does not create an OpenGL context. Owner commands, scope and pending CPU/OptiX viewer checks are in [C2c validation](../../research/jt/JT_RAYTRACED_ALPHA_VALIDATION.md). The agent has not built or run this new target.

## Pure ray-traced sampler check

`rt_texture_filter_tests` requires no GL context, Embree or CUDA runtime. It invokes the same extracted CPU sampling header used by `CpuPathTracer` and verifies authored nearest/linear/mip filtering with literal pixel/alpha values, UV transforms and wrap boundaries. The shared selection header is also used in OptiX. [C2w validation](../../research/jt/JT_TEXTURE_FILTER_PARITY_VALIDATION.md) gives owner commands, paired viewer controls and explicit footprint/GPU evidence limits. Prepared only; no agent build/test execution.

## Authored mip carrier and sampler check

`texture_mip_chain_tests` verifies bounded PNG carrier/format-conversion preservation, literal higher-level colours and alpha through the production CPU sampler, and malformed/incomplete chain rejection. It needs Qt Gui and GLM but no GL context, Embree or CUDA execution. [C2x validation](../../research/jt/JT_AUTHORED_MIP_VALIDATION.md) separates this prepared check from raster/GPU viewer evidence and portable export limitations. No agent build/test execution.

## BLEND depth compatibility

`blend_depth_policy_tests` compiles the production main-scene shaders and uses the same alpha classification/depth decision helper as RenderableMesh. It checks texture-only surface depth, vertex/scalar transparency and adverse triangle order in a textured shell. It needs an offscreen OpenGL 4.5 core context (skip 77 is not a pass). [Coverage regression](../../research/jt/JT_BLEND_COVERAGE_REGRESSION.md) gives owner build/run commands, the failed earlier attempt and the owner-accepted application gate. Native target prepared only; no agent build/native execution.

C2y extends `texture_mip_chain_tests` with the explicit private 3x3 terminal profile, malformed-profile rejection, PNG/format preservation and literal terminal interior/border sampling. [C2y validation](../../research/jt/JT_MIP_COMPOSITION_BORDER_VALIDATION.md) gives owner commands and the pending visual gate. Existing depth-policy and filter checks remain regressions; no agent native execution.


## Resident image export encoder

`embedded_texture_image_tests` invokes the PNG encoder used by application texture packaging. It checks asymmetric image orientation, alpha, distinct authored mip tails, border terminal metadata and malformed/missing payload rejection, without an OpenGL context. [C2aa application export](../../research/jt/JT_RESIDENT_TEXTURE_EXPORT_VALIDATION.md) records the separate pending viewer build and GUI GLB/glTF round-trip gate. Prepared only; no agent native execution.

## Unlit material export

`unlit_material_export_tests` uses the shared material helper called by both application export builders, then serializes/reimports original lit and unlit triangles through GLB and glTF. It checks the unlit extension/usage declaration and reopened flag, colour factors and alpha mode. It requires Qt Gui and Assimp but no GL context. Build and run this target using the owner-managed isolated render configuration; no agent native execution is claimed. [Shared unlit validation](../../research/jt/JT_UNLIT_MATERIAL_VALIDATION.md) records the separate owner-accepted application rebuild and repeated post-lit export/reopen gate.
