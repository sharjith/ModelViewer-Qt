# Advanced 9.x texture blocks — Gate C2j

22 October 2026, `feature/JT-importer`. **Owner accepted C2j on 22 October 2026 after reporting that the tests passed.** The agent authored controls but did not build/run C++ tests or the viewer. C3 production checks are deferred until the owner can test tomorrow; C2j is independent of those results.

## Implemented subset

The original texture reader now consumes the complete JT9 local-version-2/3 hierarchy. Version 2 includes an empty version-1 compatibility block; version 3 includes empty version-1 and version-2 blocks. Lower blocks must have type None and no images. The active block supplies the retained descriptor/images. Unknown future local versions remain bounded, unparsed metadata with a warning.

Advanced type 9 uses the existing pre-lit 2D MODULATE profile. Basic version-1 type 2 keeps its accepted mapping; advanced type 2 is post-lit and must not acquire that basic meaning. Other valid unsupported types retain geometry with diagnostics. Version 2 defaults the supported channel-0 profile to UV0. Version 3 reads its explicit coordinate channel **after the complete image list**, unlike the modern prefix field. Existing stored UVs, affine matrices, model-linear generation, image limits, endian handling, resource restrictions and cancellation are reused. Source type/version/channel metadata is retained. Version-3 separator type 26 uses the existing supported texture-reset path; wider accumulated multi-texture reset semantics remain unqualified.

No file revision, dependency, About claim or scene-graph model changes. JT8 authored textures, unsupported texture types, automatic/multiple texture units, combiners, other pixel data types, borders, authored mip selection and view/projective mapping remain open C2 items. C2 is not complete after this gate. Authentic textured JT exports are still unavailable; enabled 9.0 uses the shared documented 9.5 family layout in original controls without a separate historical texture-reference claim.

## Prepared validation

`jt9_texture_tests.cpp` now covers active versions 2/3 for enabled 9.0/9.5, both byte orders and inline/external images. Checks include exact pixels, complete consumption, every truncated prefix, invalid lower-block type/image count, second stub validation, advanced type semantics, stored/reflected/generated UVs, generation into coordinate slot 2, material UV selection, raw metadata, invalid enums/channels, separator behavior, budgets and cancellation. Existing version-1 and modern tests remain regression coverage. Tests are prepared, not executed by the agent.

The existing [native-suite commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls) include these checks. Owner builds the viewer normally. Original controls are regenerated with:

```powershell
python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures out/jt-research/texture9-advanced-ui-fixtures --advanced
```

Authoring completed: 48 JT roots, 12 reference comparisons and an ignored `authoring.json`. Existing basic controls are untouched. The writer serializes each original 9.x container/attribute directly; it does not reheader a file. Geometry companions reuse the previously accepted original/source-derived 8.2 path. These mixed-family external assemblies qualify the texture layout and adapter, not authentic same-version vendor co-export behavior. All assets, copied source licence, generated reports and images stay under ignored `out/`.

## Owner visual gate

Use `out/jt-research/texture9-advanced-ui-fixtures/`, retaining each folder's companions. Match camera/up-axis settings before comparing the glTF and JT. Compare image orientation, pattern and coverage; lighting can differ.

1. Under `9_5/v2/`, open `texture_inline_le.jt` in each of `stored`, `mirror`, `planar`, comparing each with its sibling `reference.gltf`. Expect unchanged orientation, horizontal reflection, and generated planar mapping respectively.
2. Repeat those three comparisons under `9_5/v3/`. The planar case generates coordinate set 1 on geometry without stored UVs; it must show the same pattern without manual UV generation.
3. In `9_5/v3/stored`, also check `texture_external_le.jt` and `texture_inline_be.jt` against the same reference. In `9_0/v3/planar`, check `texture_external_be.jt`.
4. In `9_0/v2/stored`, check `texture_inline_le.jt`. Each control should retain one mesh, four vertices and two triangles.
5. Reopen a basic C2i control, an accepted modern textured control and an authentic 9.x assembly as regressions.

The owner subsequently reported C2j tests passed and authorized commit/push/proceeding after every successful testing report. This accepts this gate without inferring separate optional-suite execution or authentic textured-export conformance, and does not accept or close C3.

## Sources and method

Consulted the retained Siemens **JT File Format Reference Version 9.5 Rev-A**, section 7.2.1.1.2.3, particularly printed pages 74–80/figures 49–50. PDF SHA-256 `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`. Availability/terms and source links remain recorded in [9.5 qualification](JT_95_VALIDATION.md), [licensing research](JT_IMPORT_FEASIBILITY.md) and [notices](../../THIRD_PARTY_NOTICES.md); no speculative publisher URL is added. The PDF skill rendered and visually checked figure 50 to establish trailing coordinate-field placement:

```powershell
pdftoppm -f 78 -l 78 -singlefile -scale-to 1400 -png out/jt-research/jt95-reference.pdf out/jt-research/c2j-texture-v3
```

Reused existing original reader/adapter/fixture code and GTEX01 comparisons from accepted C2i (`0034b48`). Source URL, checkout/hash and CC0-1.0 attribution are in [GTEX01 provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new third-party decoder, vendor software, SDK/schema or disassembly was used. No PDF or vendor diagram is redistributed. One existing ModelViewer-original decoder implementation changes; inherited upstream files/hashes, notices and the 104 upstream/17 original file counts remain unchanged. The manifest/modification note records this continuation. These are method statements, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
