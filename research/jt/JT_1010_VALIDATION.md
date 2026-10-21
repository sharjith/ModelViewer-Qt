# JT 10.10 qualification — Gate 5a

20 October 2026, `feature/JT-importer`. **Owner UI acceptance recorded for both authentic assemblies.** The owner confirmed the preceding 10.7/10.8 engine colour corrections with "Fixed". JT 10.10 is a sampled forward-version qualification through the shared modern reader; 10.11 and missing intermediate revisions remain unqualified.

## Inputs and implementation

The existing owner corpus supplies one palette assembly and seventeen referenced parts, neutral ID SE10_10. During this pass the owner supplied an additional engine assembly and thirty-seven referenced parts, neutral ID E1010, so the known duplicate-material case is checked before handoff. All 56 source files have the authentic header `Version 10.10 JT DM 10.8.0.1`. The two-digit minor identifier is parsed independently of the exporter build. Source names, paths and hashes remain in ignored research storage; no geometry is bundled or rewritten.

`JtData_Model.cxx` adds 10.10 to the explicit qualified-version gate. Existing modern 64-bit TOC, compression, graph/instance/transform, base-v2 attribute, Sabot/palette and TopoMesh position/index/stored-normal code is reused. There is no separate 10.10 parser, new codec, scene model, SDK or translator source. Existing local record/envelope/codec/allocation guards remain active. Public 10.5-based Prostep and Siemens 10.6 references establish the existing modern baseline; neither is presented as a complete 10.10 specification. Authentic exports and original regressions provide the version-specific qualification here.

SE10_10 local graph inspection finds seventeen triangle-strip shapes, twenty-four transform attributes, twenty-three material attributes (twelve indexed) and six palette maps. E1010 contains seven duplicate-material source shape lists, all matching the ordered attributes in corresponding confirmed E109 records. Three selected display shapes exercise the compatibility convention.

The original scene-adapter profile additionally accepts the exact observed `Version 10.10 JT DM 10.8.0.1` header. It still requires Solid Edge author metadata, a shape node, exactly two ordinary base-v2 fallback materials, flags 0x3980, palette index -1, zero inhibit/final state, absent Force/Ignore and no active/inherited palettes. Branch isolation and programmatic opt-out remain. This uses the matching convention confirmed by independent E109 controls and owner 10.5/10.7/10.8/10.9 appearance checks; independent 10.10 control-reader results have not been reported. No global first-material rule or third-party decoder material change is introduced.

## Native measurements

| Measurement | SE10_10 | E1010 |
| --- | --- | --- |
| Render resources | 23 | 46 |
| Mesh occurrences | 30 | 46 |
| Vertices, summed once per resource | 210,257 | 1,054,489 |
| Triangles, summed once per resource | 184,618 | 1,105,712 |
| Nodes before optional purging | 182 | 290 |
| Cumulative charged bytes | 123,359,038 | 673,254,742 |
| Local resource bounds, metres | (-0.075, -0.054, -0.03) to (0.11, 0.054, 0.0428582) | (-0.647319, -0.365258, -0.252571) to (1.90735, 0.392603, 1.02885) |

Validation checks finite coordinates/transforms, index ranges and normalized stored normals within the unchanged default limits. Cumulative charges are not peak resident RAM. UI occurrence totals differ from unique resource sums. Optional unresolved metadata-pair diagnostics remain; full PMI/metadata coverage is not claimed.

SE10_10 compared with accepted SE10_9 has identical triangle indices, stored normals, resolved materials, named hierarchy and local transforms. Position arrays differ in fifty scalar components, maximum `7.194245220746838e-16` metres, counted once per resource. Node metadata extras are excluded because independently exported atom IDs differ. The profile does not trigger in SE10_10; its diagnostic GLB remains byte-identical before and after the appearance extension. No coordinates are adjusted to force cross-export equality.

E1010 before/after comparison confirms byte-identical emitted positions, stored normals, every other vertex attribute, triangle indices, and identical scene nodes/transforms across all 46 meshes. Ignoring generated material names, exactly three semantic materials change from purple to grey, dark and grey. Other purple assignments remain. All 38 E1010 hashes and all 36 SE10_10/SE10_9 comparison-corpus hashes were rechecked unchanged.

The final isolated MSVC AddressSanitizer build and full portable suite pass, including all five original 10.0 geometry/normal/B-Rep-failure fallback regressions and the extended five-signature material-profile guard suite. Original 10.10 endian controls were generated and their 80-byte header trailers verified. Owner UI acceptance is not inferred from native results.

Ignored evidence: `gate4a-source-inventory.json`, `gate4a-source-attributes.json`, `gate4a-comparison.json`, `gate4a-1010.log`, `gate4a-fixtures.log` (initial diagnostic filenames), `gate5a-final-build.log`, `gate5a-final-tests.log`, `gate5a-vice-final.log`, `engine1010-source-inventory.json`, `engine1010-duplicate-audit.json`, `engine1010-before.log`, `engine1010-after.log` and `engine1010-comparison.json`. All diagnostic GLBs remain private under `out/jt-research`.

## Tests and owner controls

Original LE/BE container/instance tests now include minor 10, while unqualified 10.1 and 10.11 headers remain rejected. Modern attribute tests cover wrapped/unwrapped base-v2 material/transform/draw attributes, palette indices, base-v1/v2 fallback scene/GLB, malformed/truncated records, invalid versions/identities/Sabot envelopes, palette packets/tails and published material-order/final semantics for 10.10. All fifteen material-profile guard cases plus sibling isolation run for five exact observed writer signatures. Synthetic headers do not establish native tessellation coverage; the authentic assemblies supply that evidence.

Generate original controls with:

```text
jt_tests --1010-fixtures out/jt-research/1010-ui-fixtures
```

Keep `attribute_1010_le.jt`, `attribute_1010_be.jt` and `geometry.jt` together. Each 10.10 attribute graph references original 8.2 geometry and should display one green quad-plus-triangle mesh: seven vertices, three triangles, world x=20 and y=-3. These are mixed-version original controls, not authentic big-endian 10.10 tessellation. Both supplied corpora are little-endian.

## Owner build and acceptance checklist

1. Rebuild the normal application on `feature/JT-importer`.
2. Open the authentic palette assembly root under the existing owner export folder's `JT-10.10` directory, retaining its part directory. Exact path is in ignored `solid-edge-root-map.json`. Compare colours, shading, placement, hierarchy, selection and fit-to-screen with the accepted 10.9 export. Expect thirty occurrences from twenty-three resources.
3. Open the newly supplied E1010 assembly under the additional export folder's `JT-10.10` directory. Expect forty-six mesh occurrences, complete geometry, a dark affected cover and grey affected parts; genuine purple details should remain purple. Compare with the accepted 10.9 appearance and a Solid Edge reimport of this export if available.
4. Open the two small original endian controls above. Reopen a previously accepted engine version as a quick colour regression.

Gate 5a sampled assembly display is owner-accepted as recorded below. JT 10.11 needs an authentic input before qualification. Missing earlier revisions, pre-8 work, exact B-Rep/XT, full PMI, textures/shaders and runtime LOD switching remain separate scope. No full viewer build, commit or push was performed by the agent.

## Owner acceptance — 20 October 2026

The owner confirmed: "Both engine and vice models look good in 10.10". Record both authentic assembly display cases, including the engine appearance correction, as UI-accepted at Gate 5a. Native validation and automated regression evidence remain as recorded above. This confirmation does not assert independent Solid Edge 10.10 control-reader results, explicit testing of the optional synthetic endian controls, or universal 10.10 encoding support. No commit or push was requested.
