# Optional native OpenGL checks

Configure this folder as a standalone CMake project against the application's Qt dependency prefix, or enable `MV_BUILD_RENDER_TESTS` in the application build. These tests need a desktop OpenGL 4.5 core context; they do not open a visible window. They are separate from the GUI-free importer/core suite. CTest reports a skip (77) if a suitable context cannot be created; a skip is not rendering evidence.

On Windows, put the dependency `bin` folder on PATH and make Qt's native platform plugins available (the application prefix uses `Qt6/plugins`). Run `material_wireframe_tests`, or add `--artifacts <folder>` to save wireframe and subsequent solid-frame PNGs.

The test uses the production scoped raster-state helper, real shaders and an offscreen framebuffer. It checks triangle interiors/coverage, solid rendering after wireframe, alternating materials, incoming fill/line/point modes, nested scopes, exception cleanup and OpenGL errors. It also compiles the actual Material implementation and checks runtime JSON serialization of the wireframe flag. Full application checks still need to exercise actual imported meshes, viewport modes, overlays and MVF document round trips.

The opt-in `MV_RT_VERTEX_ALPHA_TESTS=ON` target `rt_vertex_alpha_tests` links the actual CPU path tracer and Embree intersector. Prepared original overlapping-triangle tests check alpha interpolation, OPAQUE/MASK/BLEND behavior, endpoints, scalar multiplication and statistical foreground coverage through renderer guide albedos. It needs Embree and Qt Widgets but does not create an OpenGL context. Owner commands, scope and pending CPU/OptiX viewer checks are in [C2c validation](../../research/jt/JT_RAYTRACED_ALPHA_VALIDATION.md). The agent has not built or run this new target.

## Pure ray-traced sampler check

`rt_texture_filter_tests` requires no GL context, Embree or CUDA runtime. It invokes the same extracted CPU sampling header used by `CpuPathTracer` and verifies authored nearest/linear/mip filtering with literal pixel/alpha values, UV transforms and wrap boundaries. The shared selection header is also used in OptiX. [C2w validation](../../research/jt/JT_TEXTURE_FILTER_PARITY_VALIDATION.md) gives owner commands, paired viewer controls and explicit footprint/GPU evidence limits. Prepared only; no agent build/test execution.
