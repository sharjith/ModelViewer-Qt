# C3 test-sample plan (line/polyline/point display content)

31 October 2026, `feature/JT-importer`. Planning record only: no sample has been collected, no decoder written, nothing built or accepted by this document. It extends the [C3 audit](JT_DISPLAY_PRIMITIVES_AUDIT.md) and the [viewing priority](JT_VIEWING_PRIORITY.md).

## Rule

Every C3 implementation line item gets at least one sample with a written expectation before code for it is started. A sample is **authentic** only if an exporter other than this project wrote it. Files we generate, or authentic files we alter, are **synthetic/derived**: useful for literal bounds, topology, malformed-input and resource tests, never evidence of exporter compatibility. Do not reheader or repackage other geometry as evidence. Samples and everything derived from them stay under ignored `out/` or outside the repository; tracked text uses the neutral IDs below and SHA-256 values.

Each accepted sample is recorded with: neutral ID, SHA-256, JT version, exporter name/version/settings, an independent description of the expected geometry (primitive kinds, counts, coordinates, boundaries, colours, transforms, units) and a screenshot or equivalent. Public reference sources consulted by the pass are recorded in [JT_PROVENANCE.md](JT_PROVENANCE.md).

## Samples needed, by line item

Item numbers follow the implementation list in the [audit](JT_DISPLAY_PRIMITIVES_AUDIT.md#coherent-first-c3-gate-once-the-family-contract-is-established).

| ID | Content and expectation | Line items | Kind needed |
| --- | --- | --- | --- |
| C3-P1 | One open polyline, 5 points, no triangles. Expect 4 segments, exact coordinates, bounds equal to the point box | register, decode, bounds | authentic, JT 9.5 and 10.x |
| C3-P2 | Two disconnected open polylines in one shape (4 and 3 points). Expect 3 + 2 = 5 segments and **no** segment joining the first polyline's end to the second's start | boundaries | authentic, JT 9.5 and 10.x |
| C3-P3 | One closed polyline (first point repeated) and one polyline with a repeated interior point, to check coordinate-repetition lists | decode (repetition) | authentic preferred, else synthetic |
| C3-PT1 | Point set, 6 points, no triangles. Expect 6 one-index primitives at exact coordinates | register, decode, 1-index topology | authentic, JT 9.5 and 10.x |
| C3-M1 | Triangle solid plus a polyline shape and a point shape in one part. Expect all three present; the solid is not dropped | fallback, hierarchy | authentic |
| C3-I1 | One polyline shape instanced twice with different transforms inside an assembly. Expect two placed copies, one shared resource | hierarchy, transforms, shared instances | authentic |
| C3-C1 | Polylines with a shape colour, and if the exporter supports it per-vertex colours | colour | authentic if available |
| C3-U1 | The same wire exported in two unit systems | units | authentic if available |
| C3-V1 | A wire shape hidden by node visibility and a visible one | visibility | authentic if available |
| C3-X1 | Malformed derivatives of C3-P2: truncated payload, oversized declared counts, zero counts, index out of range, cancellation mid-decode | validation, limits, cancellation | derived (labelled), local only |
| C3-S1 | In-memory line/point/triangle meshes for the copy, subset, merge, palette and flat-normal paths | 1-index and 2-index topology | synthetic unit tests (no JT file) |
| C3-R1 | Round trip: import C3-P2, C3-PT1 and C3-M1, export GLB/glTF, re-import. Expect line and point primitive modes and counts preserved, no fabricated triangles | exports | follows from the authentic samples |
| Selection | Click, hover and box selection on C3-P1, C3-PT1 and C3-M1 in the viewer | selection | manual checklist on the same samples |

Separate path, not part of the first gate: precise wire curves (Wireframe Rep, segment 18). One authentic control already exists (C3-SE-C, JT10.10 LE, 8,407 bytes, SHA-256 `d6c76dc6f083be8d277e2c51b5841730d5d09713de9099deaf151e48f8dbc9b3`), recorded in the audit. It contains no polyline or point nodes, so it cannot serve C3-P* or C3-PT1.

## Where authentic samples can come from

Ordered by how likely they are to give the display polylines and points the first gate needs. None has been obtained.

1. **The owner's existing CAD tools.** Solid Edge already produced the precise-wire control. Re-export with every curve/wireframe/sketch option set differently and inspect each result with `tools/jt/inspect_jt_structure.py` (it counts the four polyline/point GUIDs). Also try NX or VisMockup, the workflow that motivated C3. Nothing is assumed about which option yields display polylines.
2. **A third-party JT writer.** The Datakit JT write samples document wire meshes, point meshes and wire bodies and can write JT 8.0 to 10.7 (default 9.5); they need the vendor SDK, whose evaluation terms are not stated on the public page. Siemens' JT Open Toolkit has a 60-day evaluation. Terms and downloads are the owner's to accept; neither has been requested or used.
3. **Public sample collections.** A search of public sources and of the `.jt` files in five public JT-related repositories found no wire or point files. Seven small public files (JT 8.0 to 10.3) were scanned read-only: six contain no polyline or point nodes and the JT10.3 file's LSG compression is outside the inspection tool's scan profile. They do not satisfy any row above.
4. **Synthetic fixtures** for the rows marked synthetic, generated by `tests/jt` tooling.

## Controls collected so far (31 October 2026)

Method: original STEP inputs with known geometry (`tools/jt/make_wire_step.py`) were imported into the owner's installed Solid Edge 2026 (version 226.00.01.04) and saved as JT with Solid Edge's current default JT options, through the owner's local Solid Edge MCP bridge (`tools/jt/solid_edge_export_jt.py`). The JT files are Solid Edge output; the STEP files only fix the expected geometry. The bridge cannot change JT options. Inspection used `tools/jt/inspect_wireframe_elements.py` (read-only). No vendor SDK, schema or terms were consulted or accepted.

| ID | Input (SHA-256) | Output JT10.10 LE (SHA-256) | Observed |
| --- | --- | --- | --- |
| C3-W1 | one open polyline, 5 points, 4 segments (`71e0707d...7cf70`) | 3,494 bytes (`a65259d2...49a50`) | Wireframe Rep (segment 18): declared edges 4, MCS curves 4. One small Tri-Strip Set shape LOD (segment 7) and its node, content not examined. No polyline or point nodes. |
| C3-W2 | two disconnected open polylines, 4 and 3 points, 3 + 2 segments (`a78bfefc...7ed`) | 3,530 bytes (`6fbde6ee...bce1d`) | Wireframe Rep: declared edges 5, MCS curves 5; the same small shape LOD. No polyline or point nodes. |
| C3-N1 | six loose points as a geometric set (`992fe77c...f624`) | 2,728 bytes (`5771a97e...370a1`) | No shape and no wireframe segment: only the scene graph. Either the points were not imported or the JT export ignores them; not determined. |

Observations: the declared edge and curve counts equal the number of input line segments (4 and 5) in both wire controls, which is consistent with the disconnected polylines of C3-W2 staying separate (5 = 3 + 2); this is a count, not a decoded-curve check. Hypothesis, unverified: with default options Solid Edge writes wires only as the precise Wireframe Rep and never as polyline/point shape nodes, so it cannot supply C3-P* or C3-PT1. The earlier owner control (C3-SE-C) is consistent with this.

Existing owner Solid Edge assemblies: two assemblies (22 and 46 part instances) were opened read-only, saved as JT with the same default options and every produced JT (31 files: the assembly files and one file per part) was inspected. None contains a Wireframe Rep, polyline shape or point shape; they hold solid tessellation only, so they serve assembly/instance and appearance regression, not C3 content.

Solid Edge to JT Translation Options (as shown by the owner; no other options exist): Save, JT document structure, part/assembly simplification, include precise geometry, translate visible parts only, translate visible constructions, translate inter-part copies as constructions, Solid Edge document properties, delete unused JT part documents, save PMI data, save coordinate system, save sectioned bodies, JT version (10.10 in use), units (meters in use), sheet metal model. There is no wireframe, polyline or point option.

Same three inputs re-exported with "Include precise geometry" switched off (other options unchanged, JT10.10): the Wireframe Rep segment disappears for C3-W1 and C3-W2 (3,237 and 3,225 bytes), C3-N1 is unchanged in kind (2,748 bytes), and still no polyline or point node appears. Every wire export also carries the same 205-byte decoded Tri-Strip Set shape LOD whatever the wire geometry (4 or 5 segments, one or two polylines), so it is probably a placeholder rather than the wire's display geometry; its content has not been decoded here. Conclusion so far: this Solid Edge exports wires only as the precise Wireframe Rep and does not write display polyline or point shapes under any available option combination tried (precise on and off, JT10.10). JT version and PMI/coordinate-system options were not varied.

Next step with this route: the bridge uses whatever JT options Solid Edge currently has, so changing those options once in the Solid Edge JT export dialog (curve/wireframe/point related choices) and re-running the commands below reproduces C3-W1, C3-W2 and C3-N1 under the new options without further manual exports.

Regenerate (outputs under `out/`, server path is the local build of the owner's bridge):

```text
python -B tools/jt/make_wire_step.py --output-dir out/jt-research/c3-samples
python -B tools/jt/solid_edge_export_jt.py --server <local>/solid-edge-mcp.exe --folder out/jt-research/c3-samples <abs>/out/jt-research/c3-samples/c3p1.step <abs>/out/jt-research/c3-samples/c3p2.step <abs>/out/jt-research/c3-samples/c3pt1.step
python -B tools/jt/inspect_wireframe_elements.py out/jt-research/c3-samples/c3p2.jt --output out/jt-research/c3-samples/c3p2.wire-elements.json
```

## Gate before decoder work

The decoder (items 2 and 3 of the audit) starts only when at least C3-P1, C3-P2 and C3-PT1 exist as authentic samples for at least one enabled JT family, or when the owner explicitly accepts a synthetic-only decoder with conformance unclaimed. Work that needs no new bit decoding (topology generalisation against C3-S1) does not wait for authentic samples.

## What this does not establish

No sample, exporter behaviour or format rule is verified by this note. It records requirements and candidate sources, not legal clearance of any vendor SDK or terms.
