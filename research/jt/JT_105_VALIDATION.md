# JT 10.5 qualification — Gate 3e1

20 October 2026, `feature/JT-importer`. **Native validation passes; owner application build/UI acceptance is pending.** Gate 3c1 (9.0, including the additional empty-shape assembly) was accepted by the owner with "Tests passed". Missing 9.1–9.4, 10.1–10.2, 10.4 and 10.6 inputs do not block this independently available 10.5 case. This gate enables 10.5 only, not a continuous 10.x version range.

## Evidence and implementation

The authentic owner export SE10_5 contains one assembly and seventeen referenced parts, all header-verified as `Version 10.5 JT ISO14306-4 DM 10.8.0.1`. Original paths, names and SHA-256 values stay outside the repo in ignored `out/jt-research/gate3e1-source-inventory.json` and `solid-edge-root-map.json`. The source corpus is not bundled or rewritten. Its exporter settings exclude precise geometry and use metres; this is stored-tessellation/assembly qualification, not exact B-Rep conversion.

The public [Prostep ivip PSI 14 Part 1 Version 3 / JT Industrial Application Package Edition 3](https://www.ps-ent-2023.de/fileadmin/prod-download/PSI14_1_V3_JTIAP_Format_Description_and_annexs.pdf), 10 June 2021, explicitly states it is based on JT 10.5. Its file/LSG, attribute, Sabot V104 (section 6.2.9) and per-face-group descriptions are consistent with the shared modern implementation exercised here. The reference remains unmodified in ignored local research storage with its publisher copyright; it is not bundled with ModelViewer. This does not make it a reference for every later or intervening revision.

Inspection and native reading of the eighteen authentic files confirm:

- Modern 64-bit TOC offsets, checked segment envelopes and supported compression.
- Existing modern partition, part, instance, LOD, triangle-strip and transform records, with external part resolution and local transforms preserved.
- All 23 material attributes use base version 2 and material local version 1. Twelve indexed material attributes are Sabot-wrapped; six palette maps select per-face-group appearance. The scene contains seventeen source shapes and emits twenty-three material-specific render resources.
- Shared modern topology, coordinate, stored-normal and face-group decoding. No new codec or scene model is required by this corpus.

`JtData_Model.cxx` therefore adds 10.5 to the explicit qualified-version gate. The existing field/codec/envelope guards remain active. There is no separate 10.5 parser, new SDK, dependency or translator source. The initial qualification retained the previously tested 10.9-only Solid Edge duplicate-material profile. The additional 10.5 appearance case below requires its own observed writer signature. No new exact-geometry backend is introduced.

## Native results and comparison

| Measurement | SE10_5 | Accepted SE10_9 regression |
| --- | --- | --- |
| Render resources | 23 | 23 |
| Mesh occurrences | 30 | 30 |
| Expanded vertices per resource | 210,257 total | 210,257 total |
| Triangles per resource | 184,618 total | 184,618 total |
| Nodes before redundant-node purging | 182 | 182 |
| Cumulative charged bytes | 123,359,007 | 123,359,004 |
| Local resource bounds | (-0.075, -0.054, -0.03) to (0.11, 0.054, 0.0428582) | Same |

The authentic 10.5 assembly passes finite coordinate/transform, index-range, triangle and normalized-normal checks within the unchanged default allocation limits. No geometry rejection, palette-map fallback or duplicate-material compatibility diagnostic is present. Existing optional/unresolved metadata-pair diagnostics remain; importing every metadata or PMI type is not claimed. Resource totals and UI occurrence totals are different units.

A comparison of private GLB diagnostics matches all thirty occurrence paths, all triangle-index arrays, all stored-normal arrays, resolved render materials and transform chains between SE10_5 and SE10_9. Position arrays differ in only three scalar components across two rendered occurrences, at most `7.194245220746838e-16` source metres. These tiny differences are observed between independently exported inputs; the importer does not rewrite coordinates to force equality. This is a measured comparison for this corpus, not a general precision guarantee. All 54 original 10.0/10.5/10.9 export hashes were rechecked unchanged.

Native evidence stays ignored: `gate3e1-initial-native.log`, `gate3e1-109-regression.log`, `gate3e1-comparison.json`, `gate3e1-source-inventory.json` and `gate3e1-source-attributes.json`. A private preview is `out/jt-research/gate3e1-105.glb`.

## Original checks and limits

Original LE/BE tests now include 10.5 container/instance graphs; wrapped and unwrapped version-2 material/transform/draw attributes and palette indices; base-v1/v2 fallback rendering and GLB preservation; payload truncation, bad base versions, invalid palette indices, duplicate IDs and Sabot bounds/type/version failures; invalid local material/draw-style versions; palette maps with valid/invalid tails; and same-node material ordering/final protection. Tests reuse the existing decoders. Synthetic graph headers alone are not native geometry qualification; the authentic corpus above supplies that evidence.

The isolated MSVC AddressSanitizer build and portable suite pass. The five prior 10.0 part regressions and authentic 8.1 geometry, stored-normal, index and cancellation checks also pass; see ignored `gate3e1-final-regressions.log`. Fixtures containing original geometry are generated by:

```text
jt_tests --105-fixtures out/jt-research/105-ui-fixtures
```

These LE/BE 10.5 graph fixtures reference the existing original 8.2 geometry file; they do not claim an authentic big-endian 10.5 tessellation corpus. The supplied real exports are little-endian. Textures, full PMI, procedural shaders, exact B-Rep and runtime LOD switching remain outside this gate. Other 10.5 encodings can still require qualification. About lists enabled versions/licence credits only; detailed scope stays here and in the README.

## Owner build and acceptance checklist

Rebuild the normal app on `feature/JT-importer` and:

1. Open the original assembly root under `<owner-export-folder>/JT-10.5/`, keeping the referenced part directory intact. The exact path is in ignored `solid-edge-root-map.json` under `10.5`. Compare with the accepted 10.9 export: complete geometry, material/face colours, placement, hierarchy, shading, selection and fit-to-screen. Expect thirty mesh occurrences from twenty-three render resources. Tree-node purging can shorten hierarchy depth.
2. Open `out/jt-research/105-ui-fixtures/attribute_105_le.jt` and `attribute_105_be.jt`, keeping `geometry.jt` beside them. Each should show one green quad-plus-triangle mesh: seven vertices and three triangles, world x=20 and y=-3.
3. Reopen the accepted 9.0 additional assembly and corrected 10.9 colour case as quick regressions. Retain any still-outstanding Gate 3b material-mask fixture checks separately; this 10.5 handoff does not silently accept them.

Stop at Gate 3e1 for owner confirmation. Available 10.7/10.8 inputs follow after acceptance; missing intermediate revisions remain unqualified. No commit/push or full viewer build was performed by the agent.

## Additional 10.5 appearance case — 20 October 2026

The owner subsequently opened an additional 10.5 assembly (private alias E105) and reported the same purple-cover discrepancy seen in E109. Inspection of all 38 original files finds seven source shape lists with two materials; their ordered material records match corresponding E109 records. The selected display geometry triggers the profile in three places. The affected cover has dark diffuse RGB (0.2, 0.2, 0.2) first and purple (0.62, 0.35, 0.75) second, with base version 2, palette index -1, flags 0x3980 and no inhibit/final/Force/Ignore state. The exact observed header is `Version 10.5 JT ISO14306-4 DM 10.8.0.1`.

The compatibility profile now also accepts this exact header, retaining the Solid Edge author metadata, shape-only, exactly-two ordinary fallback material, no-palette and branch-isolation guards. Published list order remains active outside these conditions; the programmatic opt-out remains available. This extends the measured 10.9 convention to matching 10.5 records; no independent Solid Edge 10.5 control test has yet been reported. No decoder or global accumulation semantics changed.

The MSVC AddressSanitizer build and full portable suite, including all five prior 10.0 part regressions, pass. All fifteen profile/negative cases and sibling-isolation cases run for each of the two exact writer headers. Native E105 passes with 46 resources/occurrences, 1,054,985 vertices, 1,105,712 triangles, 290 nodes and 673,309,310 charged bytes. Before/after GLB comparisons confirm byte-identical positions, stored normals, every other emitted vertex attribute, triangle indices and identical scene nodes/transforms across all 46 meshes. Ignoring generated material names, exactly three resolved materials change: purple to grey, dark and grey; other purple assignments remain. All 38 source hashes remain unchanged. The original SE10_5 palette assembly remains a regression case and does not trigger the profile.

Ignored evidence: `engine105-source-inventory.json`, `engine105-duplicate-audit.json`, `engine105-before.log`, `engine105-after.log`, `engine105-comparison.json`, `engine105-fix-build.log`, `engine105-fix-tests.log` and `engine105-vice-regression.log`. Diagnostic GLBs stay private under `out/jt-research`.

**Owner retest pending:** rebuild the app and reopen the original additional 10.5 assembly. Compare the affected covers/grey parts against the accepted 10.9 appearance and, if possible, a Solid Edge reimport of this 10.5 export. Genuine purple details should retain their colours. Gate 3e1 remains pending this retest; do not advance to 10.7/10.8 or infer blanket 10.5 appearance acceptance.

## Owner colour acceptance — 20 October 2026

The owner confirmed "Fixed" after rebuilding and checking the 10.5 appearance correction. Record the reported purple-material issue as UI-accepted. This supersedes the pending colour-retest status above; it does not assert independent Solid Edge 10.5 control results or acceptance of unreported optional fixtures. Proceed to available 10.7/10.8 qualification.
