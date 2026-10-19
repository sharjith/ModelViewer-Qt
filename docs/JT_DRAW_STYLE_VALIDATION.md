# JT draw-style rendering pass

19 October 2026, `feature/JT-importer`. Follows the accepted metadata milestone `c435335`. The owner completed the viewer checks and confirmed that the wireframe-hint fixture still renders filled, matching the documented limitation. Together with the automated results, this pass is accepted for commit.

## Implemented behavior

The native reader now recognizes the JT 10.0 Draw Style Attribute Element, including its base attribute header, local version 1 and six defined data flags. Other local versions and nonzero reserved data bits fail explicitly. File-version acceptance remains JT 10.0.

Draw-style fields accumulate independently of material fields. Ignore, force, field inhibit and field final flags follow the existing attribute accumulation rules. Field bits 0/1 name two-sided lighting/back-face culling, while data bits 0/1 name back-face culling/two-sided lighting; this swapped assignment is handled explicitly.

| Authored style | Adaptation |
| --- | --- |
| Back-face culling | `AI_MATKEY_TWOSIDED` reflects whether back faces should remain visible |
| Lighting disabled | Explicit unlit material flag and `aiShadingMode_Unlit` |
| Flat shading | Separate triangle corners with geometric face normals; lit materials retain the lit PBR shading model |
| Smooth shading | Original decoded positions, indices and stored normals |
| Outlined polygons | Native wireframe material hint retained; explicit diagnostic that the viewport displays filled triangles |
| Two-sided lighting disabled while both sides remain visible | Explicit diagnostic for the renderer's two-sided-lighting approximation |
| Separate specular | Resolved source flag retained; texture-dependent ordering is not implemented |

The resolved data byte and field-presence mask are retained as material properties `$mat.jt.drawStyleFlags` and `$mat.jt.drawStyleFields`. Untagged files retain their previous default behavior.

Flat-normal generation does not change triangle positions, triangle count, winding or occurrence transforms. Each triangle gets independent corners so adjacent faces cannot average their normals. Zero-area triangles retain their finite source-normal fallback. This implements faceted normals, not a guarantee of uniform lighting intensity across an entire polygon under every renderer/light model.

The application's generic material adapter treats Assimp's Flat shading model as unlit, so the importer expresses faceting through normals and keeps the lit material shading model. The enum is stored using Assimp's enum property representation, permitting both the application's integer read and the GLB exporter's enum read. Export tests verify that unlit styles produce `KHR_materials_unlit`.

## Geometry sharing and fallback

Canonical decoded geometry is now retained separately from material-bound render resources. Smooth and flat occurrences of one definition receive the appropriate independent normal data. Equal resolved appearances/styles still share a render mesh. Both flat-first and smooth-first traversal orders are tested so cache order cannot change shading.

The LOD rollback checkpoint includes canonical geometry resources and their cache entries. The existing rejected-alternative rollback, per-part exact-to-stored-tessellation fallback, cancellation and allocation limits remain in force. New render copies and flat-normal allocations participate in the import allocation budget. No XT converter or additional geometry codec is added.

## Automated validation

The isolated `tests/jt` build uses MSVC AddressSanitizer for the native decoder/importer and tests. Passing cases include:

- All six draw-style fields under final, forced, inhibited and ignored accumulation; resolved culling, unlit, wireframe and shading-model values.
- A noncoplanar two-triangle definition instantiated with smooth and flat appearances in both traversal orders, plus a second flat occurrence sharing the first flat resource. Expected smooth/flat vertex counts are 4/6, with preserved triangle count and correct independent normals.
- Degenerate triangle fallback and explicit approximation/unsupported-hint diagnostics.
- Original little- and big-endian JT containers, version/header alignment, inherited final fields, unknown local versions and reserved-bit rejection.
- Existing assembly, material, metadata/copy ownership, Switch, RangeLOD rollback and malformed-reference tests. The five local model regressions retain their existing vertex/triangle counts, stored normals, colors and fallback results.
- Seven generated display imports and GLB exports: resolved source flags, culling, unlit extension emission, expected flat-normal counts and rejection of the invalid-version fixture.

Logs and generated result JSON are under ignored `out/jt-research/draw-style-*`. The original decoder additions, synthetic tests, fixture serializer and generator are in the repository.

## Reproduction and viewer checks

Build the standalone `tests/jt` targets, then run:

```text
jt_fixturegen --kind draw-style --samples out/jt-research/samples --output out/jt-research/draw-style-fixtures
jt_probe out/jt-research/draw-style-fixtures/draw_unlit.jt --materials
```

The generator needs a locally supplied `Bolt.jt`; no geometry is bundled or downloaded. Keep the generated `parts/` folder beside the fixtures. After rebuilding ModelViewer, use a lit viewport mode and compare:

| File | Expected viewer result |
| --- | --- |
| `draw_smooth.jt` | Smooth Bolt; 913 vertices, 904 triangles |
| `draw_flat.jt` | Faceted Bolt; 2,712 vertices, 904 triangles |
| `draw_flat_big_endian.jt` | Same faceted result |
| `draw_unlit.jt` | Material color without scene-light shading; 913 vertices, 904 triangles |
| `draw_culled.jt` | Back-face culling enabled; 913 vertices, 904 triangles. A closed Bolt can look unchanged from outside |
| `draw_wire_hint.jt` | Filled rendering with a wireframe limitation diagnostic; 913 vertices, 904 triangles |
| `draw_single_lighting.jt` | Two-sided-lighting approximation diagnostic; 913 vertices, 904 triangles |
| `draw_invalid_version.jt` | Unsupported draw-style version error |

Every successful fixture has one placed mesh. Global viewport shading overrides may mask source flat/unlit differences. Culling can also be checked through the material's Two Sided field or probe JSON rather than relying on the exterior view of a closed part.

Full application build and visual acceptance remain with the owner. The fixtures exercise original authored containers and an existing local part; independent comparison with diverse real JT draw-style examples remains outstanding.

## Reference and provenance

Layout and flag semantics follow the published [JT 10.0 Data Format Reference](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), section 6.1.2.4, Tables 34 and 35. The reference PDF remains outside version control.

`JtAttribute_DrawStyle.cxx/.hxx` are original ModelViewer GPLv3 additions. The provenance manifest, modification summary and shipped notice counts reflect the new files; existing upstream permissions are retained. This pass adds no proprietary implementation or SDK. Vertex/face color arrays, palettes, textures, full wireframe rendering, independent two-sided-lighting control, further JT file versions and exact XT conversion remain separate work.
