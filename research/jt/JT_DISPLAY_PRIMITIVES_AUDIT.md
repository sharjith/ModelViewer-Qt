# C3 line/wire and point coverage audit

24 October 2026, `feature/JT-importer`, following the owner-directed priority change `aa772b6`. **Read-only audit; no C3 implementation, build gate or acceptance yet.** C2 remains paused at C2ag. No C++ build, native test, vendor exporter or application run was performed in this pass.

## Confirmed application gap

The generic application path already maps Assimp line/point primitives to GL_LINES/GL_POINTS, draws indexed or unindexed primitives, and exports primitive modes through SceneGraphExporter. This does not by itself establish complete JT viewing or picking support. The shared ray-tracing snapshot explicitly filters out non-triangle meshes; line/point rendering in CPU/OptiX is a separate limitation, not evidence of JT import success.

The JT importer has four concrete missing registrations: polyline and point shape nodes, and their corresponding LOD payloads. The existing Vertex Shape LOD reader leaves TopoMesh compressed representation data as an unimplemented branch. Scene validation, geometry copies and palette subsets all assume three indices per face. Merely registering the missing node names, or relaxing one triangle check, cannot supply correct line/point geometry.

Reproduce the read-only source inventory:

```text
python -B tools/jt/audit_display_coverage.py --output out/jt-research/c3a-display-coverage.json
```

The report records zero of the four required display types registered, source hashes and exact lines for nine triangle/generic-path probes. This is a source inventory, not native/importer execution. No private model data or paths appear in the report.

## Record-family findings

The retained JT10.0 Rev-B diagrams were visually reviewed for shape nodes, Vertex Shape LOD, TopoMesh LOD and compressed representation data (printed pages 49, 95, 97 and 103). The retained JT9.5 Rev-A compressed representation diagram on printed page 119 was also reviewed. Both describe separate polyline/point primitives rather than triangle-edge visualization.

Important differences must be resolved by family, rather than borrowing the triangle parser:

- JT9.5 conditions the face-group count/list on a polyline shape and carries an explicit unique-coordinate count. JT10's diagram includes the face-group collection in the common structure and omits that explicit unique count.
- The JT10 diagram labels Lag1 prediction for the boundary/index arrays. The reference-only Java 9.x reader uses no predictor there. Do not transplant that choice across families.
- Vertex records may repeat each unique coordinate according to a length list. Normals/colours/UV records and primitive indices cannot simply be zipped with the unique coordinate array.
- Primitive boundaries must retain disconnected polylines; connecting the last vertex of one strip to the first of the next would invent geometry. Declared-count versus sentinel-count conventions, nested envelopes, local versions and any shared-record references need literal expectations.
- Optional vertex flags, auxiliary data, stored attributes and later record versions require explicit handling or a bounded diagnostic; they cannot be silently skipped as if they were triangle data.

The retained public definition and the inspected reference implementation do not, in this audit, establish authentic 10.x polyline/point payload envelopes and boundary/count/predictor expectations. The Java implementation is 9.x-oriented and even names a two-byte region as unknown; it is a secondary implementation cross-check, not permission to copy an unexplained skip into the new reader. Existing production wire-export correlation does not identify every missing payload or prove these four records caused that production failure.

## Evidence requested before the first decoder gate

Request one small authentic JT containing actual wire/polyline objects (preferably a known open polyline plus a second disconnected polyline), and a point-set export if available. A supported 9.5 or 10.x exporter is useful; record its version/settings and the expected shape/counts, with a screenshot or simple independent geometry description. A small mixed triangle-plus-wire model helps verify that adding lines does not drop the solid representation. Triangle wireframe display alone is not such a control.

Keep inputs and derived payloads under ignored out/ or outside the repository; use neutral IDs/hashes in tracked records. A reduced non-sensitive control is sufficient; a full production assembly is not required. If only production diagnostic reports are available, retain the uncertainty: those inventories do not expose the needed primitive payload. Original fixtures can provide literal bounds/topology regressions, but must not be presented as authentic exporter evidence or made by reheadering other geometry.

## Coherent first C3 gate once the family contract is established

### Authentic Solid Edge curve evidence, 24 October 2026

The owner supplied a Solid Edge 2026 part export after adding an extruded surface and derived edge curves. The first sketch-only export (neutral control C3-SE-A, SHA-256 `2aa9613f793e557cbc6a761460d8cdb287d637b78923d4db0ed639612fd0907c`) contained no shape, XT or wireframe segments. The surface/derived-curve export without precise geometry (C3-SE-B, SHA-256 `4da21791fbff94c12d3e5c561dc496e250df69c83528791c2f91a748bea069a9`) added one triangle-strip shape LOD, but no polyline/point shape or wireframe segment.

Enabling precise geometry produced C3-SE-C (JT10.10 LE, 8,407 bytes, SHA-256 `d6c76dc6f083be8d277e2c51b5841730d5d09713de9099deaf151e48f8dbc9b3`). Container inspection found one triangle shape segment (type 7), one XT segment (17), and one precise wireframe segment (18). Bounded standard-library LZMA decompression and element-header inspection identified Wireframe Rep `873a70d0-2ac8-11d1-9b6b-0080c7bb5997`, local version 1, with declared edge count 4 and MCS curve count 4. No polyline/point display nodes were present in the inspected LSG. The triangle LOD's type is `10dd10ab-2ac8-11d1-9b6b-0080c7bb5997`.

This establishes authentic precise-wire input, not a decoded-curve or viewer pass. Curve payloads, declared counts versus actual geometric entities, late-loaded association and remaining segment bytes are not fully validated. The retained JT10 reference describes Wireframe Rep as precise MCS curves; this is a separate C3 route from TopoMesh display polylines. The production wire failure remains unqualified, and point/polyline evidence is still missing. Do not discard this sample merely because it has no polyline LOD, or equate the existing triangle display with curve support.

Reproduce the container inventory, keeping the supplied sample local:

```text
python -B tools/jt/inspect_jt_structure.py <local-samples>/sample-C3-SE-C.jt --output out/jt-research/c3a-owner-precise-inventory.json
```

That tool currently reports algorithm-3 LSG compression as outside its scan profile. The supplemental local inspection and its exact executable script are retained at `out/jt-research/inspect_owner_precise.py` and `out/jt-research/c3a-owner-precise-elements.json`; rerun with `python -B out/jt-research/inspect_owner_precise.py` while the local input is retained. No geometry was exported, vendor software invoked, third-party decoder executed, native test run or C++ code changed in this inspection. The owner performed the Solid Edge exports; this records observations and method, not conformance or legal clearance.

1. Register the applicable shape/LOD types and decode bounded compressed coordinate/attribute/index records, preserving local versions, primitive boundaries, coordinate repetition and resource/cancellation checks.
2. Generalize scene validation/copy/subset paths for one-index points and two-index line segments while preserving triangle behaviour, hierarchy, transforms/units, shared instances, colour and palette identity. Surface-only normal/flat-shading/texture operations must not reinterpret line/point topology.
3. Prepare one grouped original/reference session for disconnected wires, points, mixed primitives, transformed/shared instances, vertex colours and malformed/fallback cases. Validate fit/bounds, visibility and selection as well as appearance. Confirm all selection paths rather than assuming that a generic draw path establishes picking.
4. Prepare literal native decoder/topology/resource/cancellation/copy tests and representative GLB/glTF round trips; the owner builds/tests. Exported primitive modes/counts/positions must remain actual points/lines rather than fabricated triangles.

This is a capability gate, not one build per byte order or coordinate encoding. Additional representations and authentic-family coverage remain explicit follow-ups. No new JT version is enabled, unknown required records are not suppressed, and the existing exact/tessellation fallback contract remains intact.

## Reference provenance and reproduction

Siemens JT10.0 Rev-B PDF SHA-256: `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. Siemens JT9.5 Rev-A PDF SHA-256: `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`. Retained reference availability/permission limits and primary links are recorded in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance), [9.5 qualification](JT_95_VALIDATION.md) and [feasibility/provenance](JT_IMPORT_FEASIBILITY.md). No new Siemens package, SDK or terms were accepted.

Also read five source files in [Liamsi/jcadlib](https://github.com/Liamsi/jcadlib/tree/27a12d1970e7684ef369d0a815aee0b7a194b02b/src/de/raida/jcadlib/cadimport/jt/model) at commit `27a12d1970e7684ef369d0a815aee0b7a194b02b`, as a reference-only comparison. Their headers identify Johannes Raida, copyright 2014, and the MIT permission notice. The notice permits reuse subject to retaining its copyright/permission notice and includes a warranty disclaimer; this reading does not clear model redistribution, JT format rights or any future copied implementation. The files remain local with their notices; no Java source was incorporated, compiled or executed.

| Reference file | SHA-256 |
| --- | --- |
| `TopoMeshCompressedRepDataV1.java` | `d5cb4e40a6b3a0df837790b2bcea5b667196dfbe8186b94ed3e1351bb18dfd3c` |
| `TopoMeshCompressedLODData.java` | `225c0ceb49e7d6e2ba65c917ac5e017615263ed771f12eb35696d6855fcc3a68` |
| `VertexShapeLODData.java` | `3ac64fbf23d287a4fab9efc8e6b993569f3fafe37ce4410080522ff803634ff5` |
| `PolylineSetShapeLODElement.java` | `9e9fce419dd4c24d99b191bb1c9165c46de2f94c2d4b5245e1dccd23cd66831a` |
| `PointSetShapeLODElement.java` | `d63a039495b18da57e06f192bd48d16f9b68b9db4bb2f08f80a9506da1cb0225` |

Recollect the pinned public reference files, retain their notices, render the already retained PDFs and regenerate the inventory (PyMuPDF required for rendering):

```text
python -B tools/jt/audit_display_coverage.py --fetch-reference-source --render-references --output out/jt-research/c3a-display-coverage.json
```

The five-file URL/hash manifest is `out/jt-research/c3a-reference-review/jcadlib/provenance.json`; diagrams are under `out/jt-research/c3a-reference-review/`. These are local research inputs/evidence, not redistributed assets. No third-party decoder or vendor software was run. This records method and limits, not legal clearance or C3 conformance.
