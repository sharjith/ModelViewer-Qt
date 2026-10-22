# Affine texture mapping — Gate C2f

Owner acceptance, 22 October 2026: the owner reported "All tests passed" for the requested gate and authorized commit, push and proceeding. The agent did not run a C++ build or viewer test. Separate optional native-suite execution and authentic textured-export qualification are not inferred.

22 October 2026, `feature/JT-importer`, following committed image mapping `a2cb18e`. **Prepared for owner build/test; C++ changes have not been built or run by the agent.** The owner explicitly confirmed the preceding UV correction on 22 October. That confirms the reported visual correction, not execution of every optional portable check or authentic textured-export conformance.

## Delivered subset

The supported modern texture attribute now accepts an affine 2D matrix for stored S/T coordinates: scale, translation, rotation, reflection, shear and their combinations. The matrix is applied to the selected UV channel on an appearance-specific mesh copy. It does not move model vertices, alter normals/indices, change world up-axis or modify image pixels. Other UV channels retain their transported values. Texture fields still replace inherited fields using the existing inhibit/final machinery; this gate does not introduce matrix concatenation between texture attributes.

The serialized row-vector matrix convention matches the established JT geometric-transform path: `S' = S*m[0] + T*m[4] + m[12]`, `T' = S*m[1] + T*m[5] + m[13]`. The R/Q plane must remain the identity plane; perspective division, coordinate generation, 3D texture mapping and automatic texture-coordinate channels are outside this subset. Unsupported matrices warn and keep untextured geometry. Finite source values whose products exceed the renderer's finite float range likewise disable the texture without retaining partially transformed UVs.

The effective UVs are baked into the output appearance mesh. The cached decoded geometry retains its original coordinates, and source node metadata retains the original texture matrix. The output mesh does not separately retain a second copy of its original selected UV array. This explicit tradeoff supports affine shear/reflection through the existing renderer and standard scene/GLB paths without inventing a partial decomposition into scale/rotation parameters or changing shared shader behavior. Identity mappings preserve stored UVs exactly.

Material cache identity includes the effective matrix when an image is mapped. Since mesh variants are keyed by material, two occurrences sharing one decoded geometry and image can have different effective UVs. Pixel resources remain content-deduplicated. Flat-normal expansion and palette subsets receive the effective UVs through the existing copy path. No source decoder, dependency, supported-version list or About text changes.

## Version and qualification boundary

| Family | Status in this pass |
| --- | --- |
| Enabled 8.0–8.2 | Existing geometry/UV behavior retained; authored texture record layout remains unqualified |
| Enabled 9.0/9.5 | Existing geometry/UV behavior retained; authored texture record layout remains unqualified |
| Enabled 10.0, 10.3, 10.5, 10.7, 10.8, 10.9, 10.10 | Original modern local-version-1 record checks prepared, both byte orders, wrapped/base-v2 variants where applicable |
| 10.11 and other revisions | Not enabled by this gate |

The visual controls use the already authorized GTEX01 source-derived quad/image, a synthetic 10.0 texture container and 8.2 geometry companion. They do not establish authentic later-revision texture layouts or independent Siemens exporter agreement. No authentic authored-texture JT file is available yet. Material texture modes beyond the prior channel-0 2D MODULATE subset stay open.

## Prepared verification and authoring evidence

`tests/jt/jt_texture_transform_tests.cpp` prepares known UV expressions for identity, 2×2 tiling, offset, U reflection, quarter-turn and shear. It compares positions, normals and topology with an original plain geometry fixture, checks effective UVs and embedded image association, and exercises scene copy and GLB round trips. A repeated external-geometry pair tests different effective mappings sharing one image without mesh-cache aliasing. Projective/non-2D matrices and finite arithmetic overflow must preserve geometry with warnings. The normal portable suite calls these checks after the previous image tests. These are prepared cases, not executed native results.

The original authoring tool ran successfully to produce 24 JT comparisons, six reference glTF documents and one unsupported-projective control. Each reference uses separately authored image-space UV expressions for the same physical mapping; the reference does not ask the JT reader to compute its expected UVs. Output, source-derived buffers/image, copied source licence and `authoring.json` stay ignored. No C++ parser, renderer or test executable was run. Exact reproduction from the previous ignored fixture folder:

```powershell
python -B tools/jt/make_texture_transform_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures
```

To build/run the isolated portable checks, the owner uses the existing [image-gate commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls); `jt_tests` includes the new transform cases. The application is rebuilt normally by the owner. No new controls need to be generated by a C++ executable for the visual gate below.

## Owner visual gate

Use `out/jt-research/texture-transform-ui-fixtures/`, keeping all companions together. Each ordinary control has one mesh, four vertices and two triangles.

1. Compare `identity_inline_le.jt` with `identity_reference.gltf`. The original corner labels must be upright and match placement.
2. Compare `tile_2x2_inline_le.jt` with `tile_2x2_reference.gltf`: two repeats in each direction.
3. Compare `offset_inline_le.jt` with `offset_reference.gltf`: the same shifted pattern and wrapped edges.
4. Compare `mirror_u_inline_le.jt` with `mirror_u_reference.gltf`: a deliberate horizontal reflection in both.
5. Compare `rotate_90_inline_le.jt` with `rotate_90_reference.gltf`, then `shear_inline_le.jt` with `shear_reference.gltf`: matching rotated and skewed patterns, respectively.
6. Repeat rotation with `rotate_90_external_be.jt`: external-image and opposite-byte-order behavior should match the inline LE result.
7. Open `unsupported_projective.jt`: the quad should load without an image, with an unsupported-mapping warning, rather than fail or distort the model.
8. Reopen one accepted ordinary JT assembly and a textured glTF as regressions.

Compare each pair with the same view/up-axis when convenient. Model up-axis does not alter the attachment of labels to model corners. Brightness differences from material lighting are acceptable; different orientation, tiling, offsets or coverage are failures. CPU/OptiX spot checks are useful if those rendering modes are available, because all modes receive the same baked UVs; no renderer-specific pass is inferred in advance.

Stop for owner results. Do not commit or push this gate before the owner confirms and requests it. C1 residuals, wider C2 modes/legacy layouts, C3–C7 and 10.11 remain on the plan.

## Provenance

Consulted the already retained Siemens JT 10.0 File Format Reference Rev-B, compound matrix definitions and section 6.1.2.3 texture transform description: [publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. The reference describes applying a matrix before sampling; the row-vector placement is consistent with the already established scene geometric-transform path. Authentic exporter confirmation of this texture profile remains required.

Existing ModelViewer adapter, copy/export and fixture-authoring code at `a2cb18e` supplies integration conventions. GTEX01 source repository/version, CC0 attribution and source hashes remain recorded in the [preceding validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01); no new external source asset was introduced. No new vendor software, SDK, schema, disassembly or third-party decoder was consulted. No vendored file or licence term changed. Existing Qt/Pillow/Assimp dependencies are reused; Pillow is only an authoring-tool dependency. This records method and consulted material, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
