# C4 metadata, units and PMI coverage audit

31 October 2026, `feature/JT-importer`. **Read-only source audit; no C4 implementation, build gate or acceptance.** It follows [viewing priority](JT_VIEWING_PRIORITY.md) (C4: basic typed properties, identity and units first, wider PMI later, preservation separated from rendering). Nothing was built or run; statements below come from reading the code and the earlier [metadata validation](JT_METADATA_VALIDATION.md).

## What exists

| Area | State found | Source |
| --- | --- | --- |
| String, I32 and F32 node properties | Retained per node as Assimp metadata `JTProperties` (ordered records with key, value, atom IDs, state flags). Oversized, non-finite or unresolved pairs are skipped with a warning. Accepted at `c435335` | `JtSceneBuilder.cpp` `properties()` |
| Display name | `JT_PROP_NAME` binds to the object name | `JtData_Model.cxx` |
| Scene copy | Node metadata survives `SceneUtils::deepCopyScene` | `SceneUtils.cpp` |
| Representation tags | `JTRepresentation`, `JTSelectedChild`, `JTRangeLODDistance`, texture descriptors | `JtSceneBuilder.cpp` |
| PMI Manager element | A vendored reader exists (2D/3D PMI, notes, spot welds, polylines, text). Nothing in `src/Import/Jt` references it, so its output is neither kept nor shown | `JtElement_MetaData_PMIManager.cxx` |
| MetaData node | Read as a plain group; its content is not interpreted | `JtNode_MetaData.cxx` |

## Gaps (hypotheses until a sample confirms them)

1. **No viewer path for properties.** A search outside the importer found no consumer of `JTProperties`. The data reaches the Assimp scene but the application shows none of it. Other importers (3DXML, glTF extras) may share the same gap; the display side should be generic, not JT-specific.
2. **No unit handling.** No JT code reads or converts a unit; a file in millimetres and one in metres would import at the same numeric scale. Where JT records units (a property or an LSG/metadata record) is not established here and must come from a sample plus the published reference.
3. **Unsupported property atom types:** date, object reference, vector, proxy and non-string keys are skipped.
4. **PMI is decoded by the vendored reader but dropped**, and what the reader accepts per local version is unqualified here.
5. **Late-loaded properties** (references to part files) stay a separate path and are not part of the typed-property display.
6. Root-node metadata is lost in direct Assimp GLB export (recorded earlier); this limits any "export keeps properties" claim.

## Proposed split

- **C4a, preservation and identity (no PMI):** show a node's retained properties in a generic read-only table for any importer that supplies Assimp metadata; add the missing atom types that samples demonstrate; no unit conversion yet.
- **C4b, units:** read the unit record, expose it as metadata and as a user-visible note, and only then decide whether to scale. Scaling changes imported geometry size, so it is the owner's call.
- **C4c, PMI preservation:** keep decoded PMI entities (labels, text, polylines) as metadata with provenance, no rendering.
- **C4d, PMI display:** render polylines and text, which depends on the C3 line decoder.

## Samples needed before code

The owner's Solid Edge installation can produce authentic controls. Its JT options include units and "Save PMI data", the two things C4 needs.

| ID | Content | Expected | Source |
| --- | --- | --- | --- |
| C4-U1 | Same part exported in metres and in millimetres | Different unit records, same shape | Solid Edge, vary the units option |
| C4-P1 | Part with custom properties (text, integer, real, date) | Typed values match what Solid Edge shows | Solid Edge |
| C4-M1 | Part with PMI dimensions or notes, "Save PMI data" on and off | PMI entities present or absent | Solid Edge with a PMI-bearing part |
| C4-A1 | Assembly with different properties per instance | Per-node values, no inheritance | Existing owner assemblies |

Controls are Solid Edge output and stay under `out/`; tracked notes use neutral IDs and SHA-256 values. Whether the Solid Edge bridge can set the unit and PMI options is untested. Earlier, it could not change JT options and the owner changed them by hand.

## Control results (1 November 2026)

Three authentic Solid Edge 2026 JT 10.10 exports of one stepped block with a 6 mm through hole, built by the owner with a sketch feature. They stay under ignored `out/jt-research/c4-samples/`. Method: segment table of contents parsed, LZMA segments decompressed, UTF-16 strings listed (`python -B tools/jt/inspect_jt_structure.py <file> --output out/jt-research/c4-samples/<name>.structure.json` for the inventory; the string listing was a throwaway script, not yet in `tools/jt`).

| ID | Export settings | Size | SHA-256 | Segment types |
| --- | --- | --- | --- | --- |
| C4-S0 | Metres, no PMI (baseline) | 4,034 | `e94ee4c01040334a…` | 1, 4, 7, 31 |
| C4-M1 | Metres, "Save PMI data" on | 12,866 | `5a421157b8743bf6…` | 1, 3, 4, 7, 31 |
| C4-U1 | Millimetres, PMI not saved | 4,251 | `a4ddf78704641378…` | 1, 4, 7, 31 |

Full hashes are in the `.structure.json` files. C4-U1 was exported from the PMI part, so its PMI is off by the export setting; it varies units and PMI together.

**Observations**

- Units are an ordinary LSG string property `JT_PROP_MEASUREMENT_UNITS`: value `meters` in C4-S0 and C4-M1, `millimeters` in C4-U1. The importer already keeps it in `JTProperties`; nothing reads it.
- A PMI segment (type 3) appears only in C4-M1. It holds named entities: `Linear_1`…`Linear_8`, `Radial_Diameter_1`, with attributes such as text height, text direction, text rotation point, colour and leader settings. The PMI part also carries the LSG properties `JT_LLPROP_PMI` and `VisiblePMI`, and every file carries `JT_PMI_CONTENT_HINT` and a `PMI_TYPE_TABLE` of PMI kinds in its metadata segment.
- Metadata segment (type 4) holds the Solid Edge document properties (author, application, dates, file name, material fields). Several fields are empty.
- The shape segment differs by 2 bytes between C4-S0 and C4-U1; whether coordinates are scaled by 1000 is not yet checked.

**Limits.** Strings only; no PMI entity is decoded, no coordinate compared, and dimension values were not matched against the part (a `value` attribute reads `0.1`, which is unexplained). The exported file embeds local paths and a user name, so these controls must not be published. One exporter, one version.

## C4a implementation (1 November 2026, awaiting owner build and test)

Generic and read-only; no unit conversion, no PMI.

- `NodeProperties.h/.cpp` flattens a node's Assimp metadata to display rows (key, value, type). JT `JTProperties` records become one row each; other nested blocks show an entry count. Rows are capped per node.
- `AssImpModelLoader::processNode` builds, per node that carries meshes, a chain of its own set and each ancestor's (flattened once per node per load) and stores it on `AssImpMeshData`, then `MeshImportAdaptor`/`SceneMesh`. Meshes of one node share it.
- The Mesh Info context-menu command (tree and viewport) opens `MeshInfoDialog` in place of the former message box: mesh statistics for every format, then source properties where the importer kept any. Mesh selection box, search, Copy rows; each property row names its source node and numbers ancestors ("ancestor N", counted over ancestors that carry properties). Ancestors are listed, never merged into the mesh's own node. A first version added a Tools menu entry; it was folded into Mesh Info because most formats keep no node properties.
- Limits: not stored in MVF, not copied by `clone()`, so absent after reopening an MVF; the Mesh column shows the first mesh of a shared node plus a count.
- Tests: `tests/jt/jt_node_properties_tests.cpp` (literal in-memory metadata, same layout as `JtSceneBuilder::properties()`); not evidence about authentic files. Expected owner check: open the C4 controls and compare the table with the strings listed above (units property reads `meters` / `millimeters`).

**Owner check (1 November 2026, screenshots):** both the C4-M1 and C4-U1 controls open in the dialog. C4-M1 shows `JT_PROP_MEASUREMENT_UNITS` = `meters` and `JT_PMI_CONTENT_HINT` = `VisiblePMI`; C4-U1 shows `millimeters` and `none`. This matches the string listing above. The dialog lists the part node first, then its ancestors that carry properties.

## Not established

Whether millimetre output rescales coordinates, the PMI element layout version, and how the vendored PMI reader handles this segment are not verified. Decoder correctness against real exporters stays unclaimed until C4b/C4c tests run on these controls.
