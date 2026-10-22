# Model-linear texture coordinates — Gate C2g

Owner acceptance, 22 October 2026: the owner reported "All tests passed" and authorized commit, push and proceeding. The agent did not build or run the C++ changes; separate optional-suite execution and authentic textured-export conformance are not inferred.

22 October 2026, `feature/JT-importer`, following owner-accepted affine mapping `4de7b62`. **Prepared for owner build/test; the agent has not built or run the C++ changes.** Authoring of comparison files ran successfully under ignored `out/`.

## Behavior and limits

For the previously supported modern 2D image profile, S and T may now independently use stored coordinates (mode 0) or a model-coordinate reference plane (mode 1). A generated component is the signed plane expression `A*x+B*y+C*z+D`, evaluated on each mesh-local vertex before node/assembly/world transforms. Active planes must have unit-length normals within squared-length tolerance `1e-5`, matching the reference's plane-distance definition. Non-unit planes diagnose unsupported mapping; they are not silently normalized. An authored affine texture matrix then transforms the resulting S/T pair, as qualified by C2f.

When both S and T are generated, the selected UV array may be absent: the adapter creates it without invoking the application UV generator. When only one component is generated, the other must have a stored coordinate array; otherwise the image is disabled with a warning while geometry loads. Existing selected UVs are replaced only on the appearance copy. Other stored channels remain unchanged. Missing lower UV slots are padded for existing scene-copy/export paths while preserving the selected set number. The underlying cached geometry and source descriptor metadata retain their original data.

Coordinate-generation modes and planes now participate in material/appearance identity and field accumulation. Repeated occurrences may share decoded geometry and image pixels while receiving different generated UVs. Model-plane parameters are inherited/replaced through the existing texture field rules; this gate does not add texture-plane concatenation. Finite arithmetic overflow is diagnosed without partially committing a UV array. Generated UVs follow existing flat-normal, palette subset, scene-copy, rendering and GLB paths.

The profile is limited to S/T model-linear generation, explicit selected sets 0–3, unchanged R/Q, channel-0 pre-lit 2D MODULATE images and supported affine matrices. View-linear, sphere/reflection/normal mapping, projective coordinates, automatic coordinate-channel assignment, additional texture units and broader image-combining modes remain unsupported with diagnostics. Dynamic world-/view-space mapping is not approximated by mesh-local baking. Authentic textured exporter confirmation of the supported local-coordinate interpretation remains outstanding.

No JT revision is enabled: enabled modern 10.0, 10.3, 10.5, 10.7, 10.8, 10.9 and 10.10 use prepared original modern-record checks. Enabled 8.0–8.2 and 9.0/9.5 retain existing geometry/UV behavior, with their authored texture payload layouts still unqualified. Modern controls referencing original 8.x geometry do not establish legacy texture support or authentic later-revision layout agreement.

## Prepared checks and authored controls

`tests/jt/jt_texture_planar_tests.cpp` is called from the existing portable suite. Prepared cases span both byte orders and all seven enabled modern minors, including wrapped/base-v2 variants. They cover no-UV and existing-UV geometry, selected sets 0/2, lower-slot padding, mixed stored/generated components, post-generation scale/offset, unchanged positions/normals/indices, scene copy, GLB UV round trips and shared geometry with different plane offsets. Negative cases preserve geometry and diagnostics for partial generation without stored UVs, view/R-coordinate generation, non-unit normals and finite overflow. These cases have not been executed by the agent; owner checks remain pending.

The original Python authoring tool reuses authorized GTEX01 positions/image, writes a geometry companion deliberately containing no UV binding/array, and writes modern texture attributes with S/T reference planes. Four glTF references use separately authored image-space expressions from positions. The tool ran successfully, creating 16 JT comparisons and four references. It did not execute the importer, a test binary, vendor software or the renderer. Source-derived assets, source licence copy and `authoring.json` remain ignored. Exact regeneration:

```powershell
python -B tools/jt/make_texture_planar_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-planar-ui-fixtures
```

For the isolated portable C++ checks, the owner rebuilds/runs the existing [image-gate commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls). `jt_tests` includes this pass automatically; the normal viewer build is also the owner's responsibility. The visual controls below already exist and do not require a native fixture generator.

## Owner gate

Rebuild the application, then use `out/jt-research/texture-planar-ui-fixtures/`, keeping all companions together. Compare each `*_inline_le.jt` against its matching `*_reference.gltf`:

| Stem | Expected mapping |
| --- | --- |
| `planar_xy` | Image positioned by model X/Y planes |
| `planar_yx` | S/T axes exchanged, matching the reference orientation |
| `planar_offset` | Plane offsets shift the pattern and wrapped edges |
| `planar_tiled` | Generated coordinates scaled to two repeats in each direction |

All should have one mesh, four vertices and two triangles, with the image visible without manually generating or assigning UVs. Compare pattern placement, orientation and coverage; lighting brightness may differ. Also open `planar_xy_external_be.jt` and compare with the XY reference to check external pixels/opposite byte order. Reopen one accepted ordinary JT assembly and one C2f transform control as regressions.

An optional instance-placement check can use the application's node transformations: moving the model should carry its texture with it, because this supported mapping is baked in mesh-local coordinates. View-dependent mapping is outside the gate. No GPU/native pass is inferred from authoring completion. Stop for owner confirmation before commit/push or dependent work.

## Provenance

Consulted the retained public Siemens JT 10.0 File Format Reference Rev-B, section 6.1.2.3, figure 52/table 29 and compound PlaneF32 definitions: [publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. The supported plane interpretation is checked with original expressions/controls; an authentic authored-texture JT oracle is still absent.

Implementation consults existing ModelViewer image/affine mapping, material cache and scene-copy/export code at `4de7b62`. GTEX01 source repository/checkout, source hashes and CC0 attribution remain in [image validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No new source asset, vendor application, SDK/schema, binary analysis or third-party decoder was used. No vendored decoder file, licence term, dependency or About text changed; the existing decoder already retained generation modes and plane fields. This records method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
