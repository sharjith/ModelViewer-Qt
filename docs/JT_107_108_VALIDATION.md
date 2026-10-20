# JT 10.7 and 10.8 qualification — Gate 3f

20 October 2026, `feature/JT-importer`. **Native validation passes; owner application build/UI acceptance is pending.** The owner confirmed the preceding 10.5 appearance correction with "Fixed". This gate covers the independently supplied 10.7 and 10.8 exports; missing intermediate revisions are not enabled by inference.

## Implementation and evidence

The owner corpus supplies eighteen original files per version: one assembly and seventeen referenced parts. Exact headers are `Version 10.7 JT DM 10.8.0.1` and `Version 10.8 JT DM 10.8.0.1`. File version and exporter build are distinct. Neutral IDs SE10_7 and SE10_8 are used here; source names, paths, headers and SHA-256 values remain in ignored `out/jt-research/gate3f-source-inventory.json` and `solid-edge-root-map.json`. Original geometry is not bundled or rewritten. Export settings exclude precise geometry and use metres.

Both corpora use the already implemented modern container, scene graph, attribute, palette and tessellation family. Raw local graph inspection of each corpus finds seventeen source triangle-strip shapes, twenty-four transform attributes, twenty-three material attributes (twelve indexed), and six palette maps. Referenced parts and repeated instances are resolved by the existing scene adapter. No new parser, codec, scene model, dependency, SDK or third-party source package is introduced. The explicit version gate adds 10.7 and 10.8, while existing local record, envelope, codec and allocation guards remain active.

The public Siemens 10.6 and Prostep JT 10.5-based references retained in local research establish the existing modern baseline; neither is described as a complete 10.7/10.8 specification. These versions are qualified with their authentic inputs and shared-reader regression evidence. This gate does not promise all encodings permitted by either version.

The initial vice-assembly qualification did not exercise the duplicate-material convention. Additional engine exports subsequently exposed matching duplicate lists in 10.7/10.8. The [appearance investigation](JT_CROSS_VERSION_APPEARANCE_INVESTIGATION.md) and follow-up below document the extended exact-header profile; no global first-material rule is introduced.

## Native results

| Measurement | SE10_7 | SE10_8 |
| --- | --- | --- |
| Render resources | 23 | 23 |
| Mesh occurrences | 30 | 30 |
| Vertices, summed once per resource | 210,257 | 210,257 |
| Triangles, summed once per resource | 184,618 | 184,618 |
| Nodes before optional purging | 182 | 182 |
| Cumulative charged bytes | 123,359,012 | 123,359,030 |
| Local resource bounds, metres | (-0.075, -0.054, -0.03) to (0.11, 0.054, 0.0428582) | Same |

Both authentic assemblies pass finite-coordinate/transform, normalized stored-normal and index/triangle validation within the unchanged default allocation limits. No geometry rejection, palette fallback or material compatibility diagnostic occurs; existing optional/unresolved metadata-pair diagnostics remain. Cumulative charges are not peak resident RAM. UI counts may sum occurrences rather than unique resources.

Private GLB comparisons against the accepted SE10_9 export confirm all emitted triangle indices, stored normals, resolved materials, named node hierarchy and local transforms are identical for both versions. Position arrays differ in ten scalar components for 10.7 (maximum `2.0844275216185535e-18` metres) and forty for 10.8 (maximum `7.194245220746838e-16` metres), counted once per emitted resource. Coordinates are not altered to force equality. Node metadata extras are excluded from this visual-scene comparison: independently exported atom IDs differ and arbitrary metadata equality is not claimed. All 54 original 10.7/10.8/10.9 file hashes were rechecked unchanged.

Ignored evidence includes `gate3f-source-inventory.json`, `gate3f-source-attributes.json`, `gate3f-10.7.log`, `gate3f-10.8.log`, `gate3f-comparison.json`, `gate3f-build.log`, `gate3f-tests.log` and `gate3f-fixtures.log`. Diagnostic GLBs remain private.

## Original regression tests

The isolated MSVC AddressSanitizer build and full portable suite pass, including all five prior 10.0 geometry/normal/B-Rep-failure fallback cases. Original LE/BE container/instance tests, wrapped/unwrapped modern attributes, palette indices, base-v1/v2 fallback scene and GLB output, malformed/truncated records, invalid base/material/draw-style versions, duplicate identities, Sabot bounds/type checks, palette packets/tails, and published material-order/final semantics now include 10.7/10.8.

Generate the owner controls with:

```text
jt_tests --107108-fixtures out/jt-research/107108-ui-fixtures
```

This writes `attribute_107_le.jt`, `attribute_107_be.jt`, `attribute_108_le.jt`, `attribute_108_be.jt` and shared `geometry.jt`. These are original 10.7/10.8 attribute graphs referencing original 8.2 geometry, not authentic big-endian 10.7/10.8 tessellation corpora. The authentic supplied exports are little-endian. Keep all five files together.

## Owner build and acceptance checklist

1. Rebuild the normal app on `feature/JT-importer`.
2. Open the authentic assembly roots under the existing owner export folder's `JT-10.7` and `JT-10.8` directories, retaining their referenced part folders. Exact paths are in ignored `solid-edge-root-map.json`. Compare each with the accepted 10.9 export: complete geometry, face/part colours, shading, placement, hierarchy, selection and fit-to-screen. Expect thirty mesh occurrences from twenty-three render resources. Optional purging can shorten tree depth.
3. Open the four original controls above. Each should show one green quad-plus-triangle mesh, seven vertices and three triangles, at world x=20 and y=-3.
4. Reopen the accepted 10.5 colour case as a quick regression. The affected cover should remain dark; genuine purple details should retain their colours.

Stop for owner confirmation at Gate 3f. Available 10.10 qualification follows after acceptance. Missing 10.4/10.6 and 10.11 inputs remain unqualified. Exact B-Rep/XT, full PMI, textures/shaders and runtime LOD switching remain separately scoped. No full viewer build, commit or push was performed by the agent.

## Additional engine appearance follow-up — 20 October 2026

The owner reported unintended purple parts in additional 10.7 and 10.8 exports (private aliases E107 and E108). Both corpora are now available locally, with 38 files each. Every file has its corresponding exact `Version 10.7 JT DM 10.8.0.1` or `Version 10.8 JT DM 10.8.0.1` header. Seven raw shape lists per corpus contain duplicate materials; their ordered records match the corresponding confirmed E109 records. Three selected display shapes per assembly exercise the compatibility case.

The guarded profile now accepts these two exact headers in addition to the observed 10.5 ISO14306-4 and 10.9 signatures. The required Solid Edge author metadata, shape-only scope, exactly two ordinary base-v2 fallback materials, flags 0x3980, palette index -1, zero inhibit/final state, absent Force/Ignore, no active/inherited palettes and branch isolation remain unchanged. Published ordering remains the default outside this profile, and the programmatic opt-out remains available. This uses the same convention confirmed with independent Solid Edge controls for E109; new independent E107/E108 control-reader results have not been reported. No third-party decoder code changed in this appearance follow-up.

| Native measurement | E107 | E108 |
| --- | --- | --- |
| Resources / occurrences | 46 / 46 | 46 / 46 |
| Vertices | 1,054,489 | 1,054,809 |
| Triangles | 1,105,712 | 1,105,712 |
| Nodes | 290 | 290 |
| Cumulative charged bytes | 673,254,742 | 673,291,517 |

Both imports pass native geometry/normal/transform checks within unchanged limits. Before/after comparisons confirm byte-identical emitted position, normal and other vertex attributes, triangle indices, and identical scene nodes/transforms for all 46 meshes of each assembly. Ignoring generated material names, exactly three material assignments change in each: purple to grey, dark and grey. Other purple details retain their assignments. All 76 original source hashes remain unchanged. The initial vice exports SE10_7/SE10_8 do not trigger the rule; their diagnostic GLBs remain byte-identical to the preceding gate outputs.

The isolated MSVC AddressSanitizer build and full portable suite, including the five original 10.0 part regressions, pass. All fifteen profile/negative cases plus sibling isolation now run for each of four exact writer signatures. Existing author, writer, opt-out, palette, flags and non-shape safeguards remain tested.

Ignored evidence: `engine107108-source-inventory.json`, `engine107108-duplicate-audit.json`, `engine107-before.log`, `engine107-after.log`, `engine108-before.log`, `engine108-after.log`, `engine107-comparison.json`, `engine108-comparison.json`, `engine107108-build.log`, `engine107108-tests.log` and `engine107108-vice-*`. Diagnostic CAD files remain private under `out/jt-research`.

**Owner colour retest pending:** rebuild the app and reopen both original additional engine exports. The affected top cover should be dark and the other two affected parts grey, matching the accepted 10.5/10.9 appearance. Genuine purple details should remain purple. Gate 3f stays open until the reported appearance issue is confirmed fixed; no 10.10 work, full viewer build, commit or push is included in this follow-up.

## Owner 10.7/10.8 colour acceptance — 20 October 2026

The owner confirmed "Fixed" after the two additional engine appearance corrections. Record those reported colour issues as UI-accepted. This supersedes the pending colour-retest status above; it does not assert independent control-reader tests or completion of unreported optional fixtures. Proceed to the available 10.10 qualification.
