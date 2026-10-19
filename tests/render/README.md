# Optional native OpenGL checks

Configure this folder as a standalone CMake project against the application's Qt dependency prefix, or enable `MV_BUILD_RENDER_TESTS` in the application build. These tests need a desktop OpenGL 4.5 core context; they do not open a visible window. They are separate from the GUI-free importer/core suite. CTest reports a skip (77) if a suitable context cannot be created; a skip is not rendering evidence.

On Windows, put the dependency `bin` folder on PATH and make Qt's native platform plugins available (the application prefix uses `Qt6/plugins`). Run `material_wireframe_tests`, or add `--artifacts <folder>` to save wireframe and subsequent solid-frame PNGs.

The test uses the production scoped raster-state helper, real shaders and an offscreen framebuffer. It checks triangle interiors/coverage, solid rendering after wireframe, alternating materials, incoming fill/line/point modes, nested scopes, exception cleanup and OpenGL errors. It also compiles the actual Material implementation and checks runtime JSON serialization of the wireframe flag. Full application checks still need to exercise actual imported meshes, viewport modes, overlays and MVF document round trips.
