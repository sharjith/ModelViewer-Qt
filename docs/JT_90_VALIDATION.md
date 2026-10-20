# JT 9.0 qualification — Gate 3c1

20 October 2026, `feature/JT-importer`. **The owner confirmed SE9_0 opens correctly. The additional E90 empty-shape correction passed owner UI retest on 20 October 2026 ("Tests passed").** The owner explicitly requested continuing the remaining gates after accepting the Solid Edge colour correction. Outstanding Gate 3b material-mask fixture checks are carried into this handoff, not recorded as passed. No commit/push is requested.

## Qualified scope and evidence

The authentic owner export set SE9_0 contains one assembly and seventeen referenced parts, all header-verified as `Version 9.0 JT DM 10.8.0.1`. Originals remain unchanged outside the repository. Source names/paths and SHA-256 values are recorded only in ignored `out/jt-research/gate3c1-source-inventory.json` and the existing export inventory. No source geometry is bundled.

Record inspection of all eighteen files and comparison with the corresponding SE9_5 export establish the shared transitional path exercised here:

- 32-bit TOC offsets and the existing checked segment envelopes/decompression.
- Sixteen-bit local record versions, the existing partition/group/part/instance/LOD/shape records, and authored external-part references and transforms.
- Base attribute version 1. All 23 source material records use local **material version 1**, whereas the matching 9.5 exports use material version 2. The 9.0 reader therefore accepts sampled material version 1 and rejects version 2; 9.5 retains its qualified versions 1/2.
- The existing Mk.2/topological tessellation decoder, legacy mask streams including plain high-degree masks, and coordinate/normal paths. No new codec or scene model is needed for this corpus. Qualify alternative encodings independently rather than inferring every 9.0 representation is handled.
- The earlier combined diffuse/alpha material inhibition semantics already normalized by the shared adapter. This gate does not qualify later separate-field additions for 9.0.

This pass uses observations of owner-supplied data, the audited GPL decoder and the archived public 9.5/modern references to compare shared concepts. No complete public 9.0 reference was acquired or claimed. In particular, the 9.5 reference is not relabelled as a 9.0 specification. No restricted SDK, proprietary translator code, schema catalog or new dependency was introduced. Existing licence notices remain; this qualification is not a blanket intellectual-property clearance for unknown records.

## Implementation

`JtData_Model.cxx` enables **9.0 and 9.5** explicitly within the 9.x family. Intermediate 9.1–9.4 remain unqualified and rejected. `JtAttribute_Material.cxx` checks the sampled local material versions above. All mesh/normal decoding, scene construction, external-file resolution and allocation/cancellation limits are shared with the existing transitional implementation.

The observed Solid Edge duplicate-material compatibility profile remains limited to its tested 10.9 writer/metadata signature. It is not broadened to 9.0 by this gate. About lists the enabled versions and licence credits only; detailed coverage and acceptance status remain in documentation.

## Native results

| Measurement | SE9_0 | SE9_5 regression |
| --- | --- | --- |
| Mesh resources | 23 | 23 |
| Mesh occurrences | 30 | 30 |
| Expanded vertices, counted per resource | 210,221 | 210,256 |
| Triangles, counted per resource | 184,618 | 184,618 |
| Nodes before redundant-node purging | 176 | 176 |
| Cumulative charged allocation bytes | 122,363,564 | 122,369,213 |
| Local resource bounds | (-0.075, -0.054, -0.03) to (0.11, 0.054, 0.0428582) | Same |

The unchanged SE9_0 assembly passes finite coordinate/transform, index-range, triangle and normalized-normal checks with the normal 6 GiB cumulative budget. No representation rejection or compatibility-profile diagnostic is present. Source units are metres as confirmed in the export options. Resource sums differ from UI totals across repeated occurrences.

A comparison of generated private GLB diagnostics matches **all 30 occurrences** by named source part and canonical oriented geometry: coordinate point sets, oriented triangle sets, transform chains and diffuse RGBA agree between SE9_0 and SE9_5 after matching geometry identity. Three colour-split parts reverse their anonymous shape order, so comparing generated node labels alone initially mismatches six occurrences; this is not a colour-assignment defect. The 35-vertex resource-sum difference is expanded attribute/vertex representation, not missing geometry. This comparison does not assert bit-identical stored normals or every specular/material field across independent exports.

Hashes of all 36 original SE9_0/SE9_5 files were rechecked unchanged. Logs and comparison outputs remain ignored: `gate3c1-90-final.log`, `gate3c1-95-regression.log`, `gate3c1-geometry-matched-comparison.json` and `gate3c1-source-inventory.json`.

Original LE/BE 9.0/9.5 graph fixtures check red/green repeated occurrences, composed transforms, local base/material versions and truncated input. Material version 2 is rejected for 9.0 and retained for 9.5. The existing original Mk.2 packet, malformed-input, topology-mask and material-mask checks continue to pass; they are shared decoder tests, not an authentic big-endian 9.0 corpus. The authentic supplied exports are little-endian. The full portable AddressSanitizer suite passes, including palette and Solid Edge compatibility-profile regressions. The five 10.0 part regressions and original 8.1 geometry/normal/index/cancellation regression pass in `gate3c1-final-regressions.log`. Source-provenance, whitespace, documentation-link and neutral sample-name checks pass. The agent did not build the full viewer application.

## Owner build and acceptance checklist

Rebuild the normal app on `feature/JT-importer` and open the original assembly root under:

```text
<owner-export-folder>/JT-9.0/<assembly-root>.jt
```

Keep its referenced part directory intact. The exact local path remains in `out/jt-research/solid-edge-root-map.json` under `9.0`.

1. Compare the 9.0 assembly with the corresponding accepted 9.5 export: complete geometry, placement, hierarchy, colours, shading, selection and fit-to-screen. Expect 30 occurrences corresponding to 23 unique render resources; optional redundant-node purging can shorten the tree. Expanded vertex totals need not match.
2. Open both `out/jt-research/90-ui-fixtures/assembly_le.jt` and `assembly_be.jt`, keeping `part.jt` beside them. Each should show a red and a green occurrence of an original quad-plus-triangle fixture: two resources/occurrences, fourteen vertices and six triangles; world x=110/210 and y=-3. These mixed-version synthetic graphs reference original 8.2 geometry; authentic 9.0 tessellation is covered by the owner export above.
3. Complete the carried Gate 3b `material_masks_le.jt` / `material_masks_be.jt` checks if not already tested: world-x order red at opacity 0.25, opaque green, opaque red. Exact paths and expectations remain in the [point-version plan](JT_POINT_VERSION_EXPANSION.md).
4. Reopen the accepted 10.9 export and the accepted [Solid Edge colour-correction case](JT_CROSS_VERSION_APPEARANCE_INVESTIGATION.md) for a quick regression check. The independently confirmed rough 9.5 export surface remains unchanged.

A private 9.0 preview GLB is available at `out/jt-research/gate3c1-90.glb`. It is a convenience, not acceptance of the native JT app path. Stop at this owner gate before implementing another version family. After acceptance, available 10.5/10.7/10.8 exports can proceed without waiting for missing intermediate revisions. Standalone XT/exact B-Rep remains separate.

## Additional E90 regression: explicitly empty sibling shapes

After the Gate 3c1 handoff, the owner confirmed SE9_0 opens correctly but reported another 9.0 assembly (neutral alias E90) failing with `Unsupported/empty JT tessellation layout`. E90 contains 37 referenced parts. A native part-by-part audit found three failures. Each affected part contains both real triangle geometry and an additional triangle-strip shape with zero declared vertex/polygon ranges and a zero-coordinate topological payload. Treating that empty sibling as an unsupported required mesh caused rollback of the enclosing LOD and rejected the otherwise renderable part.

The correction requires two independent pieces of evidence before retaining such a shape without render geometry:

1. A successfully read shape node explicitly declares both vertex-count and polygon-count ranges zero. An uninitialized/synthetic/unknown range does not count as a declaration.
2. The decoded topological payload explicitly declares zero coordinates, and every serialized topology stream has zero output count. The legacy upper-four-bit mask stream is checked only in the family that serializes it. Required header/packet/tail reads still must succeed.

The decoder exposes validated explicit emptiness, and the scene builder emits a diagnostic and keeps the empty node while continuing with its real siblings. No empty `aiMesh` is created and no vertex, normal or index is fabricated. Contradictory declared geometry, nonempty topology with zero coordinates, truncated packets and arbitrary empty decoder output remain errors. An LOD with only empty shapes still fails to supply a renderable alternative, preserving the existing failure contract and fallback selection. The fix does not weaken unsupported required representation checks or enable another file version.

Native E90 result: **46 resources and 46 occurrences, 1,055,014 expanded vertices, 1,105,828 triangles, 287 nodes**, unchanged local bounds (-0.647319, -0.365258, -0.252571) to (1.90735, 0.392603, 1.02885). Exactly three explicitly-empty-shape diagnostics identify the source placeholders. Different engine exports may contain different tessellation/normal records; matching aggregate vertex totals across versions is not required.

Original LE/BE empty-topology tests cover the legacy and modern mask families, nonempty-mask contradictions and every truncated prefix. Scene tests cover real geometry plus a valid empty sibling, absent/nonzero declared ranges, unvalidated empty output and empty-only LOD rejection. The portable AddressSanitizer suite and five 10.0 part regressions pass. Source records and geometry remain external. Updated upstream-derived files retain their notices, dated modifications and provenance records.

Rebuild and retest the original E90 assembly. Expected: complete model, no empty-tessellation error, normal hierarchy/placement/colours, and genuine purple parts retained. Reopen the already accepted SE9_0 assembly for regression. A private preview is `out/jt-research/engine90-empty-fixed.glb`. Logs remain ignored under `engine90-empty-*`; no commit has been made. The owner subsequently reported "Tests passed", accepting the E90 correction and closing Gate 3c1 for the supplied 9.0 cases. Separate unreported fixture checks and other revisions are not inferred accepted.

## Gate 3c1 acceptance — 20 October 2026

The owner confirmed the E90 retest with "Tests passed", following the earlier successful SE9_0 load. Gate 3c1 is accepted for these supplied 9.0 cases. Proceed to available 10.5 qualification; missing intermediate-version inputs need not stall it. No commit/push was requested.
