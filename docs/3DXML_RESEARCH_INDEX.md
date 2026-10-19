# 3DXML importer research index

> Consolidated provenance, dated 19 October 2026: [3DXML_PROVENANCE.md](3DXML_PROVENANCE.md). Early source restrictions describe their individual research phases, not the complete branch history.

Work branch: `feature/3dxml-importer`, created from `dev` at the owner's
request. Research scripts/documentation are versioned here. Application
implementation is now present; see the [native importer and validation](3DXML_NATIVE_IMPORTER.md).
Existing unrelated `vcpkg` working changes
were excluded from research commits.

2026-10-18: merged `origin/dev` (through `5f5decb`) without conflicts. The
latest pass fully consumes all 152 UVR Skeleton streams: 188 mesh leaves,
4,152,382 finest triangles, two mesh-free definitions, and a separate TECHREP
attachment. See the [complete mesh research and importer contract](3DXML_complete_mesh_research.md).
Full sample geometry is ready for implementation with owner-accepted parsed normals;
exact stored smooth-normal quantization remains explicitly unproven.

The [additional archive layouts](3DXML_additional_layouts.md) cover XML
TESSELLATED, extended binary groups, image-free materials, allocation padding
and power-of-two index widths. All five supplied archives pass native import
and independent triangle-count checks; additional-file GUI acceptance is pending.

The [published-specification research](3DXML_specification_research.md) records
verified document locations, unavailable schema downloads, the remaining UVR
codec gap and a proposed conformance work list.

## Current specifications

The [final stored-normal pass](3DXML_stored_normal_final_pass.md) confirms the
candidate direction mapping but does not resolve the exact quantizer. It also
isolates 72 opposed corners with exact axis tags, proving that those winding
disagreements do not come from coefficient quantization. The owner visually
accepted `_stored_normals_candidate.glb` in ModelViewer: parsed normals with
constant-face overrides are now the preferred rendering policy for the
supported sample layout, with derived flat normals retained as an alternative
and invalid/missing-data fallback. Exact quantization remains unproven.

A [hierarchical GLB inspection checkpoint](3DXML_glb_inspection.md) now preserves
all 252 XML nodes, shared representation meshes, default visibility and face
colors. The owner accepted the corrected hierarchy/appearance in ModelViewer,
followed by the parsed-normal version; these sample inspection checkpoints pass.
The corrected `_hierarchy_v2.glb` omits hidden helper mesh attachments,
matches OBJ numeric diffuse/roughness values and preserves 538 body/color
splits with separate body nodes. The first GLB's static visibility assumption
was disproven by owner inspection; see the checkpoint's correction details.

The owner's subsequent reference screenshots prompted a
[face-color pass](3DXML_face_color_findings.md): 45 stored BGR colors are now
bound to the corresponding finest face packets in a separate colored OBJ.
The first neutral-color OBJ was a geometry inspection artifact, not a source
appearance export. Full material semantics remain outside this result.

1. [Complete mesh research and importer contract](3DXML_complete_mesh_research.md):
   compound streams, every body, graph reuse, tail records, full audit and rendering policy.
2. [Mesh decoding](3DXML_mesh_decoding_spec.md): coordinates, persistent packed
   bit reservoir, indices, strip/fan expansion, normal bindings, and coverage.
3. [Scene graph and transforms](3DXML_scene_graph_spec.md): native XYZ,
   matrix mapping/composition, reference/occurrence graph, visibility,
   sample-backed units, camera and integration conventions.

## Reproducible evidence

| Evidence | File | Reproducer under `tools/3dxml` |
|---|---|---|
| Archive/coordinate layout inventory | `3DXML_sample_analysis.json` | `analyze_3dxml.py` |
| Historical primitive-boundary measurements | `3DXML_topology_analysis.json` | `probe_topology.py` |
| Historical standalone-code rejection experiments | `3DXML_integer_codec_analysis.json` | `probe_integer_codec.py`; false standalone-reservoir assumption later identified |
| Full pin/ring/screw geometry checks | `3DXML_mesh_verification.json` | `decode_sample_meshes.py` |
| 140-member index/count audit | `3DXML_mesh_inventory_audit.json` | `decode_sample_meshes.py --audit-inventory ...` |
| Matrix/basis/unit/visibility evidence | `3DXML_scene_graph_analysis.json` | `analyze_scene_graph.py` |
| Extended full-member marker scan and unequal-count geometry checks | `3DXML_extended_layout_audit.json` | `probe_remaining_layouts.py --check-unequal-geometry` |
| Complete compound/Skeleton/mesh/surface-link and topology audit | `3DXML_complete_mesh_audit.json` | `decode_representation.py --geometry-qa` |
| Independent geometric normal-quantizer comparisons | `3DXML_normal_quantization_analysis.json` | `analyze_normal_quantization.py` |
| Noncommuting transforms and inverse-transpose normals (synthetic) | `3DXML_transform_fixture.json` | `verify_transform_conventions.py` |
| Per-face style binding and BGR/RGB color histograms | `3DXML_face_style_analysis.json` | `analyze_face_styles.py` |

All use the owner's supplied archive at
`D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml`.
Its SHA-256 is
`6f95b1a5bd3e5a51db842eef3793ed492c2aa3db5a00c8b37ac33434b79b11b9`.

Generated OBJ, mesh JSON, preview and occurrence fixtures live in ignored
`out/3dxml-research`. They are regeneratable artifacts, not source-controlled
copies of the original geometry. OBJ exports now preserve native XYZ; the
old cyclic display permutation must not be used for placement. The complete
assembly preview is `out/3dxml-research/complete_assembly.png`; its reproduction
and limitations are documented in the complete mesh contract.

## Historical notes

`../3DXML_decoding_handout.md` and `3DXML_reverse_engineering.md` preserve
the investigation trail, including hypotheses that were later disproven.
The current specifications above take precedence over those historical
interpretations. The old no-build restriction remains; the owner's later
branch/checkpoint request authorizes research-only Git checkpoints.

## Decisions before C++ implementation

- Preserve native coordinates and XML local transforms; share representation
  meshes and create separate occurrence nodes. The viewer already accumulates
  parent*local transforms.
- Use exact stored index connectivity. Derived face normals are ready;
  source smooth-normal quantization remains provisional.
- Preserve occurrence-path visibility and treat TECHREP review data separately.
- Record millimetres as inferred for this sample, not an unconditional
  format guarantee. Keep units, basis and viewer autoscale distinct.
- Use validated compound catalogs/extents and Skeleton traversal to reach every
  mesh leaf; do not port the historical marker scanner as an importer.
- Keep unsupported container/exporter variants explicit. Complete geometry
  coverage is established for this sample, not every 3DXML file. Descriptor
  localization still validates bounded metadata searches and rejects ambiguity.
- Make scene ownership and custom error reporting explicit in the loader;
  Assimp only owns scenes that its importer created. Keep 3DXML routing
  separate from pretending tessellations are XCAF B-Rep documents.

Research and Python analysis can continue on this branch. Application builds,
owner-run checks, C++ integration and release remain subsequent work.
