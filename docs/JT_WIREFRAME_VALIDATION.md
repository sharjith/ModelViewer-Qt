# Per-material wireframe raster follow-up

19 October 2026, `feature/JT-importer`. The draw-style milestone was accepted and committed as `8e36e48`; its filled fallback for wireframe hints is replaced by this follow-up. The owner reports successful application tests; the visible-selection clarification is recorded below. The owner authorized committing this follow-up together with shininess preservation.

## Behavior and integration

The JT importer already resolves the outlined-polygons field into `AI_MATKEY_ENABLE_WIREFRAME`, and `MaterialProcessor` already maps it into `Material::wireframe`. The missing step was raster rendering: neither loaded `SceneMesh` draws nor the base `RenderableMesh` path read the material flag.

Both triangle draw paths now use `ScopedMaterialWireframe` to select line rasterization for that draw and restore the incoming mode afterwards. This renders the existing triangle outlines; it does not convert the mesh into lines or generate feature edges. Vertices, triangle indices, stored normals, occurrence transforms and mesh counts are unchanged. Material flags from other import formats use the same renderer path.

The helper reads the actual incoming polygon mode only for enabled wireframe materials. It restores fill, line or point mode as appropriate, supports nesting and restores state during exception cleanup. Ordinary filled materials do not perform this state query. The application requests an OpenGL 4.5 core profile, where polygon mode applies to both faces together. Query storage accommodates drivers returning one shared mode or two query slots, while using the authoritative core-profile mode.

Analysis overlays bypass the material override and retain the viewport's incoming display mode. Explicit point/line primitives are not changed. Dedicated picking and shadow geometry paths retain their existing behavior and do not apply the material wireframe override. GPU picking draws to a hidden framebuffer using the incoming polygon mode; CPU ray picking tests the triangle surfaces. Visible selection/hover highlighting uses the material color draw and therefore remains wireframe. Selecting the bolt is not expected to display a filled surface. Ray-traced material wireframe and a new material-preview UI are outside this change.

The obsolete importer warning that the viewport will display filled triangles has been removed. The independent approximation diagnostic for single-sided lighting without back-face culling remains.

## Material persistence

`Material::fromVariantMap` already read a wireframe field, but `toVariantMap` omitted it. The writer now includes it. The MVF builder stores this map in `MVF_material_runtime`, so the existing reader can restore the authored raster flag. The tests exercise false/true values through the actual Material writer, JSON conversion and reader, plus the default for older maps without the field.

This does not add a glTF wireframe extension or change the native importer's earlier metadata-export limitations. GLB geometry remains triangulated; a downstream viewer need not reproduce this application-specific raster style.

## Automated evidence

The optional `tests/render` target compiles the production helper and `Material.cpp`, and runs real shaders against an offscreen framebuffer. It passed on an NVIDIA GeForce RTX 4060 Ti using a desktop OpenGL 4.5 core context. A fixed triangle produced **288 white pixels in wireframe** and **4,608 in fill mode**, with an open wireframe interior and filled subsequent draw. Repeated alternating draws produced identical results. Incoming fill/line/point state, disabled overrides, nested scopes, exception cleanup, JSON persistence and OpenGL errors are checked.

The test requests no visible window. It returns skip code 77 when a suitable context cannot be created; a skipped test is not rendering evidence. CTest passed on the current machine. Raster PNGs and local build logs remain under ignored `out/jt-research/material-wireframe-*`.

The isolated JT suite, built with MSVC AddressSanitizer, also passes the five original sample regressions and the existing container, assembly, material, metadata, LOD and draw-style cases. New little- and big-endian mixed-style fixtures confirm two occurrences with correct placement, independent wireframe flags, red/green appearance and unchanged geometry counts. The repository contains the fixture serializer and generator; external Bolt geometry remains unbundled.

## Reproduction

Configure `tests/render` independently against the application dependencies, or enable `MV_BUILD_RENDER_TESTS` for the application project. It is off by default and separate from the GUI-free test option. See [render test instructions](../tests/render/README.md).

Generate viewer fixtures with the standalone JT tools:

```text
jt_fixturegen --kind draw-style --samples out/jt-research/samples --output out/jt-research/draw-style-fixtures
```

The existing `draw_wire_hint.jt` filename is retained for regression continuity; it should now display triangle wireframe in the raster viewport. `mixed_wire_solid.jt` and `mixed_wire_solid_big_endian.jt` contain a red wireframe bolt at X=-30 and a green filled bolt at X=30. They each contain two meshes, **1,826 vertices and 1,808 triangles** in total. The single-bolt fixtures retain **913 vertices and 904 triangles**.

## Viewer acceptance

The owner reports that the application tests passed, with one discrepancy: the selected wireframe bolt did not become visibly solid. That appearance is expected; the earlier picking wording did not distinguish hidden hit testing from visible selection highlighting. No filled selection overlay was implemented.

After rebuilding the application:

1. Open `draw_wire_hint.jt` in the ordinary shaded raster viewport. It should show triangle outlines instead of a filled surface.
2. Open either mixed fixture. Confirm red wireframe and green filled bolts simultaneously; switching display modes should not leave the green bolt in wireframe when returning to shaded mode.
3. Save the mixed model as MVF, reopen it and confirm both material styles remain.
4. Confirm an analysis overlay still uses its existing display mode, and ordinary filled imports remain filled.
5. In shaded mode, click inside a visible triangle of the wireframe bolt, away from its edges. It should select the bolt while the visible highlight remains wireframe. This specific interior-click check is not yet separately confirmed.

The offscreen test verifies the production state helper and actual raster output, not every application pass or driver. The owner's application test report supplies viewer acceptance; the additional interior-click check above remains to be confirmed. No JT file version, geometry codec, XT backend or proprietary dependency is added.
