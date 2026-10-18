# 3DXML importer research index

Work branch: `feature/3dxml-importer`, created from `dev` at the owner's
request. Research scripts/documentation are versioned here. Application
implementation has not started. Existing unrelated `vcpkg` working changes
were excluded from research commits.

2026-10-18: merged `origin/dev` (through `5f5decb`) into this branch without
conflicts. Research now recognizes first mesh blocks in 150/153 members by
separating position and normal counts. See the
[extended findings](3DXML_extended_layout_findings.md) for coverage limits
and geometry/normal anomalies; additional bodies remain unresolved.

## Current specifications

1. [Mesh decoding](3DXML_mesh_decoding_spec.md): coordinates, persistent packed
   bit reservoir, indices, strip/fan expansion, normal bindings, and coverage.
2. [Scene graph and transforms](3DXML_scene_graph_spec.md): native XYZ,
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

All use the owner's supplied archive at
`D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml`.
Its SHA-256 is
`6f95b1a5bd3e5a51db842eef3793ed492c2aa3db5a00c8b37ac33434b79b11b9`.

Generated OBJ, mesh JSON, preview and occurrence fixtures live in ignored
`out/3dxml-research`. They are regeneratable artifacts, not source-controlled
copies of the original geometry. OBJ exports now preserve native XYZ; the
old cyclic display permutation must not be used for placement.

## Historical notes

`../3DXML_decoding_handout.md` and `3DXML_reverse_engineering.md` preserve
the investigation trail, including hypotheses that were later disproven.
The two current specifications above take precedence over those historical
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
- Resolve representation entry points from the compound structure rather than
  hard-coded sample offsets before production support.
- Keep unsupported binary variants explicit. Current 150/153 first-block
  coverage does not mean that the entire sample assembly can yet be imported
  completely; later candidate blocks still need structural decoding.
- Make scene ownership and custom error reporting explicit in the loader;
  Assimp only owns scenes that its importer created. Keep 3DXML routing
  separate from pretending tessellations are XCAF B-Rep documents.

Research and Python analysis can continue on this branch. Application builds,
owner-run checks, C++ integration and release remain subsequent work.
