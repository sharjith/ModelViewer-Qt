# Hierarchical GLB inspection checkpoint

## Loading progress denominator mismatch

Owner observed progress exceeding its displayed mesh total (roughly
`535/385`). Source inspection identifies a ModelViewer accounting mismatch:
`collectSceneMeshInfo()` deduplicates mesh indices through `processedMeshes`
before incrementing `info.meshCount`, whereas `processNode()` increments
`_processedMeshCount` for every node-to-mesh reference. Its progress signal
uses `_sceneStats.meshCount` as the denominator. The UI displays these values
directly. Shared mesh instances can therefore take the numerator beyond the
unique-resource denominator; this does not indicate extra exported geometry.

Direct GLB JSON counts for both `_hierarchy_v2.glb` and
`_stored_normals_candidate.glb` are 385 distinct material primitives and 538
node/primitive occurrences. Thus loading all instances can show `538/385`.
The first, superseded GLB contains 388 distinct primitives and 553 occurrences;
grouping by material within its representation nodes gives 370 resources and
535 occurrences, consistent with the earlier merge finding. Actual Assimp
postprocessing can affect counts in that superseded layout.

A loader correction should count node-to-mesh references for the progress
denominator (538 here), keeping unique-resource statistics separate. Do not
disable mesh sharing or change geometry to make the progress numbers agree.
This checkpoint documents the diagnosis; application code is unchanged.

## Corrected owner-inspection export (v2)

Use `out/3dxml-research/export/ASM-GETRAG_V160_hierarchy_v2.glb`
(297,474,648 bytes). The original export below is superseded.

Owner screenshots exposed three export/import compatibility issues:

* The original GLB attached 15 effectively hidden helper mesh occurrences.
  Although ModelViewer parses `KHR_node_visibility`, its static-file load path
  skips runtime visibility setup when `GltfAnimationData::isEmpty()` returns
  true (no clips). Thus these helpers appeared. V2 retains all hidden XML
  nodes without mesh attachments and does not depend on visibility support.
* ModelViewer enables Assimp `OptimizeMeshes` by default. In the original
  export, grouping multiple bodies under one representation node allowed
  18 same-material body splits to merge. The count is exactly explained by
  538 visible OBJ body/color splits + 15 hidden helpers - 18 merges = 535.
  V2 uses one mesh per body, with identity child nodes for multibody parts.
  All 252 original XML nodes remain, plus 86 body nodes: 338 total nodes,
  185 shared body mesh resources and 538 visible body/material primitives.
* The original export imposed sRGB-to-linear conversion and roughness 0.6.
  The verified OBJ supplies direct RGB/255 diffuse values and shininess 24.
  ModelViewer reads those diffuse values directly and derives roughness as
  `sqrt(2/(24+2))`. V2 supplies the same numeric base colors and roughness.
  Source color-space semantics and full material properties remain unproven;
  matching this inspection baseline does not establish them.

V2 retains 5,608,912 visible triangles. Its 4,126,750 resource triangles omit
26,632 triangles belonging only to hidden helper definitions. Independent
validation checks all original hierarchy links and matrices, every normal,
the absence of hidden mesh attachments, shared body resources, every
occurrence/body/color triangle count, numeric palette/roughness equality,
538 body/color splits, and bounds matching the verified OBJ. All pass.
The official Khronos validator reports zero errors and zero warnings, with
217 informational messages (identity matrices and unsupported visibility
metadata). ModelViewer visual confirmation remains pending.

Use the reproduction commands below with `_hierarchy_v2.glb` as output and
verification input. The scripts now generate the corrected policy by default.

## Original export record (superseded)

Generated 2026-10-18 from the owner-supplied sample using our existing decoder.
No application code changed. Source-derived artifacts remain ignored locally.

Artifact: `out/3dxml-research/export/ASM-GETRAG_V160_hierarchy.glb`
(299,295,240 bytes; SHA256
`6d1e58ef219b378af461300082f801aaf447de28d239c347732bdfc78a0a24d3`).

The 252 nodes retain the original XML root and all 251 occurrences, names,
parent links and local transforms. Repeated occurrences share 150 nonempty
representation meshes. The two empty representations retain their XML nodes.
Internal bodies become primitives of their representation mesh, with body
paths and source offsets recorded in primitive extras. This preserves the
product assembly tree without introducing artificial body nodes.

All finest triangles are retained: 4,152,382 across unique resources and
5,608,912 in the default-visible assembly. Positions are local source XYZ.
Flat triangle normals are derived from the stored winding; the unresolved
smooth-normal quantizer is not involved. Unindexed triangle vertices preserve
flat edges; this inspection file prioritizes fidelity over size optimization.

The root converts inferred source millimetres to glTF metres and applies
native XYZ -> Y-up `(x,z,-y)`. Child matrices retain their original millimetre
values. ModelViewer's existing +90-degree X glTF import rotation restores the
native displayed orientation. Measurement values may consequently differ
from the earlier millimetre-numbered OBJ by a factor of 1000.

The 45 decoded BGR face colors become RGB materials. CAD bytes are interpreted
as sRGB and converted to linear glTF base-color factors. This color-space
interpretation is an export policy, not an additional proven source field.
Metalness zero, roughness 0.6 and opacity one are inspection defaults; full
source material properties are not claimed.

Seventeen effectively hidden XML occurrences use `KHR_node_visibility`.
ModelViewer contains support for this extension. Other readers that ignore
the optional extension may display helper geometry. Specification:
[Khronos node visibility](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_node_visibility/README.md).

## Validation

`verify_exported_glb.py` independently checked GLB structure, all XML parent
links/names/local/world matrices, reachability, shared representation bindings,
effective visibility, accessor bounds and every triangle's normal/winding.
Visible triangle count and world bounds agree with the owner-verified colored
OBJ (converted to metres). All checks passed.

Official Khronos npm `gltf-validator` version `2.0.0-dev.3.10` reported zero
errors, zero warnings, 131 informational messages, without truncation.
Information consists of 130 explicit identity matrices and one unsupported
visibility-extension notice. This validator therefore does not validate that
extension; the independent checker verifies its effective hidden states.
Reports and manifest are adjacent to the GLB as `.verification.json`,
`.khronos-validation.json` and `.manifest.json`.

## Reproduce

```powershell
python -I tools/3dxml/export_decoded_glb.py 'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml' --occurrences out/3dxml-research/scene_graph.json --output out/3dxml-research/export/ASM-GETRAG_V160_hierarchy.glb
python -I tools/3dxml/verify_exported_glb.py out/3dxml-research/export/ASM-GETRAG_V160_hierarchy.glb --occurrences out/3dxml-research/scene_graph.json --obj-manifest out/3dxml-research/export/ASM-GETRAG_V160_face_colors.manifest.json
```

Owner inspection of the hierarchy, visibility, placements and face colors in
ModelViewer remains the next checkpoint before native importer implementation.
