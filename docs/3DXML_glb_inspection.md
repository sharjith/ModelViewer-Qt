# Hierarchical GLB inspection checkpoint

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
