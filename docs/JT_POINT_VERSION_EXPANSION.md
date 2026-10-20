# JT 9.x and 10.0–10.8 expansion

20 October 2026, `feature/JT-importer`. The owner selected **9.0–9.5 and 10.0–10.8** as the immediate target after accepting Gate 3a. The owner confirmed successful UI testing of the supplied 10.9 export on 20 October. The separate original production-sample comparison remains pending. Pre-8 and 10.10/10.11 expansion follow the immediate target when evidence and inputs are available.

This target contains fifteen file-version identifiers. Three already have accepted sampled coverage: 9.5, 10.0 and 10.3. The twelve additions are 9.0–9.4, 10.1–10.2 and 10.4–10.8. Writer build numbers such as the `DM` string do not add file-format versions.

## Gates and evidence

| Gate | Work | Evidence required before enabling additional versions |
| --- | --- | --- |
| 3a, accepted | Existing 9.5 transitional container, graph and tessellation path | Original A95 and independent LE/BE packet/assembly tests; owner reported “9.5 works” |
| 3b, current | Normalize older material field masks and correct the 9.5 raw high-degree topology stream | Original binary layout/mask goldens, rendered-scene inheritance, supplied 9.5/10.0 assembly comparison, prior-family regressions and owner display checks |
| 3c1 | Qualify 9.0 using the independent Solid Edge export | Matching lawful layout evidence and authentic input; combined diffuse/alpha inhibition, material versions, geometry/normal bindings, instances and unsupported mandatory records |
| 3c2 | Qualify 9.1–9.4 as matching inputs become available | Establish actual differences from the transitional family; missing inputs here do not block independent 10.x qualification after Gate 3c1 |
| 3d | Qualify 10.1 and 10.2 against the modern baseline | Actual file revisions, container/attribute/codec comparisons and representative stored tessellation; keep the accepted 10.0/10.3 cases as regressions |
| 3e | Qualify 10.4–10.6, including applicable palettes/Sabot forms | Public layout comparisons and authentic inputs; distinguish optional presentation additions from required geometry/scene changes |
| 3f | Qualify 10.7 and 10.8 | Lawful differences from the documented modern family and matching authentic files; do not assume identity from successful 10.9 decoding |

These are qualification groups, not separate importers per point release or a claim that every member has an identical layout. Reuse the same code when record semantics agree. A later gate can split further if its evidence reveals another representation family. Each implementation gate includes original malformed-input checks, native validation and an owner UI handoff. Keep stored tessellation as the fallback; exact XT conversion is separate.

The initial local corpus contained 8.0, 8.1, 8.2, 9.5, 10.0, 10.3 and 10.9 files. The owner subsequently supplied a same-assembly export set, identified only by neutral version aliases here. Header/hash inventory confirms **18 files each for 9.0, 9.5, 10.0, 10.5, 10.7, 10.8, 10.9 and 10.10**: one assembly plus seventeen referenced parts per revision. Original names and paths remain in ignored `out/jt-research/solid-edge-export-inventory.json` and `solid-edge-root-map.json`; geometry is not bundled.

No 9.1–9.4, 10.1–10.2, 10.4 or 10.6 input has yet been supplied. Solid Edge's available export range must not be taken to mean every intermediate revision was exported. Missing inputs for those revisions do not block independent qualification of available 9.0, 10.5, 10.7 and 10.8 after the current gate. The 10.10 export is retained for the later forward gate. Reheadered copies and generated cross-version graphs remain behavioral tests, not authentic native-tessellation qualification. About continues to list enabled versions only.

The enabled 10.0 export passes native checks with 23 mesh resources, 30 occurrences, 210,256 vertices and 184,618 triangles. After the topology fix below, the supplied 9.5 export matches those counts and local resource bounds. The enabled 10.9 export passes with the same resource/occurrence/triangle counts and 210,257 vertices. The one-vertex difference is an observation, not assumed corruption or proof of identical geometry. The owner subsequently confirmed this 10.9 export works in the viewer. That accepts sampled 10.9 display, while the original production-system palette comparison remains pending.

### Owner-confirmed export settings

The owner supplied Solid Edge export-options screenshots on 20 October. The offered revisions are exactly **9.0, 9.5, 10.0, 10.5, 10.7, 10.8, 10.9 and 10.10**. Thus the missing intermediate revisions were not choices in this installation's dialog; they were not simply omitted from the supplied batch. This does not establish that those file versions never existed or that another exporter cannot produce them.

The exports use **All documents**, **JT assembly and part documents**, **Meters**, **Design** sheet-metal representation, and **Use Current Configuration** for both part and assembly simplification. Precise geometry, visible-parts-only filtering, PMI, saved coordinate systems, sectioned bodies, unsafe-character removal and modified-parts-only updating were disabled. Visible constructions, inter-part copies as constructions, document properties and deletion of unused JT part documents were enabled.

These are stored-tessellation/assembly cases, not exact B-Rep conversion tests. The observed local bounds around 0.1 source units are consistent with metre-scale exports; bounds are reported in source coordinates. Preserve each assembly's referenced part directory. A comparison with Solid Edge should use the same active configuration. Construction entities or nonvisible parts may be present, so not every source record should be assumed to represent a visible solid. PMI and saved-coordinate-system fidelity are not qualified by this corpus. Document-property export being enabled does not imply that every property type is supported by the reader.

Inventory supplied exports independently of decoder support with `python tests/jt/inventory_headers.py <export-folder> --output out/jt-research/export-inventory.json --sha256`. The output includes private paths and stays outside version control; console output shows version counts only. Inventory checks have been exercised on the existing corpus and original short/invalid and two-digit-minor headers. Header recognition is not import qualification.

## Gate 3b: material field normalization

The public JT 9.5 Rev-A material table retains older field numbering. The shared scene accumulator uses JT 10 numbering. Reading the bytes correctly without translating the mask can inhibit the wrong property.

| Source material inhibit bit | Older meaning | Scene accumulator bit(s) |
| --- | --- | --- |
| 0 | Ambient | 0 |
| 1 | Combined diffuse RGB and alpha, legacy form | 6 and 7 |
| 2 | Specular | 1 |
| 3 | Emission | 2 |
| 4 | Blending and factors | 3 |
| 5 | Vertex-colour override | 4 |
| 6 | Reflectivity, where present | 5 |
| 7 | Separate diffuse RGB, where present | 6 |
| 8 | Separate diffuse alpha, where present | 7 |

`JtAttribute_Material::Read` normalizes the material-specific mask after a successful read for pre-10 records. Generic attribute masks and modern material masks retain their existing numbering. The legacy combined bit inhibits both colour and alpha; separate fields can inhibit either independently. This does not add textures, shaders or new geometry codecs.

Original tests exercise each documented mask bit in both byte orders and three complete 9.5 assembly cases. The graphs reference original 8.2 fixture geometry, whose material inhibits its own legacy fields so the parent state remains visible. This checks cross-version accumulation without downloaded geometry. It does not qualify any additional file-version identifier.

The isolated MSVC AddressSanitizer build and full portable suite pass, including these mask fixtures, the five 10.0 part regressions and the 8.1 geometry/cancellation check. The unchanged A95 regression retains 2,999 resources, 4,475 occurrences, 6,592,239 vertices and 7,893,052 triangles with 6,069,877,180 cumulative charged bytes. Original 8.0 and 8.2 probes retain their previous geometry counts. The normal 6 GiB budget is unchanged. Source-provenance, whitespace, documentation-link and public model-name checks pass. Logs stay ignored under `out/jt-research/phase3b-*`. No full application build was performed by the agent.

### High-degree topology masks found in the supplied exports

The supplied 9.5 assembly initially failed with an apparent zero-length compressed packet. The JT 9.5 Rev-A topology section actually specifies high-degree face masks as a plain counted `VecU32`, just as JT 10 does. The inherited reader incorrectly selected CDP2 for that array in 9.5. Empty arrays hid the error; a nonempty array beginning with the value 1 was misread as a bitlength packet.

Read this array as raw words in both families, while retaining the separate legacy upper-four-bit mask stream. Original LE/BE packet fixtures verify nonempty masks, unsigned high bits, the following split streams/hash boundary and every truncated prefix. This fixes the supplied 9.5 assembly without weakening the rejection of genuinely invalid zero-length CDP packets or changing the version gate. It also identifies the cause of the earlier A95 preferred-LOD rejection; that prior description as an unsupported encoding is superseded by this diagnosis.

The portable AddressSanitizer suite and five 10.0 part regressions pass after this correction. The supplied 9.5 assembly now passes natively at the same geometry counts as the 10.0 export. The unchanged large A95 regression also passes. Its preferred LOD now decodes rather than falling back: 2,999 resources and 4,475 occurrences remain, with 6,614,780 vertices and 7,918,872 triangles. Cumulative charges are 6,083,697,416 bytes, below the unchanged 6 GiB budget; node count and local bounds remain unchanged. The higher vertex/triangle sums replace the earlier lower-count fallback observation. No LOD-rejection diagnostic remains in this run. Logs: `point-fixed-A95.log`, `point-fixed-SE9_5.log`, `point-fixed-corpus.log` and `point-test-model-tests.log` in ignored research storage.

Generate the owner fixtures with:

```text
jt_tests --material-mask-fixtures out/jt-research/material-mask-ui-fixtures
```

Keep `part.jt` beside `material_masks_le.jt` and `material_masks_be.jt`. Rebuild the normal application and open both assemblies. Each should show three separated planar occurrences, each a quad plus a triangle, in increasing world-x order (screen order depends on the camera):

1. Red with opacity 0.25: inherits parent RGB, takes child alpha.
2. Opaque green: takes child RGB, inherits parent alpha.
3. Opaque red: inherits both parent RGB and alpha through the legacy combined bit.

Expected totals are three material-specific mesh resources and three occurrences, 21 vertices and nine triangles. Their x translations are 110, 210 and 310, with y translation -3. Also open the supplied 9.5 assembly root under `<owner-exports>/JT-9.5/`, keeping its sibling part directory intact; compare it with the supplied 10.0 assembly. Reopen the accepted original 9.5 input and representative 8.x input for appearance/placement regressions. Optional redundant-node purging may shorten the hierarchy. Confirm Gate 3b before dependent qualification work advances. No commit is requested at this checkpoint.

## Reference research

The archived Siemens JT 9.5 Rev-A material section 7.2.1.1.2.2 establishes the mask mapping above. The existing Siemens 10.0 and 10.6 references establish the modern baseline. Siemens' [JT technology fact sheet](https://www.plm.automation.siemens.com/fr_fr/Images/Siemens-PLM-Components-JT-Open-Program-Technology-fs_tcm68-121669.pdf) identifies reflectivity and user-defined fields in 9.4, and separate diffuse/alpha inhibition in 9.5. Those historical notes identify investigation points, not complete earlier-version layouts.

An additional public reference was acquired: [Prostep ivip PSI 14 Part 1 Version 3, JT Industrial Application Package Edition 3](https://www.ps-ent-2023.de/fileadmin/prod-download/PSI14_1_V3_JTIAP_Format_Description_and_annexs.pdf), dated 10 June 2021 and explicitly based on JT 10.5. Its Annex R contains a change history and earlier-format descriptions, including 9.5. It documents palettes, version-2 base attribute fields and Sabot V104, which can be compared with our existing implementation. It does not establish all intermediate file versions or 10.7/10.8 layouts.

The reference PDF is retained only in ignored local research storage: 23,607,676 bytes, SHA-256 `df412551f2498f5ff7fab2bc7847f5ab10347de0821cbf8bf5204d8dbd92f7f0`. Copyright remains with prostep ivip Association; the reference's own use/distribution terms apply. It is not bundled with ModelViewer, and no new translator source or SDK was acquired.

NIST publishes [CAD test resources for unrestricted use](https://www.nist.gov/ctl/smart-connected-systems-division/smart-connected-manufacturing-systems-group/mbe-pmi-0). Candidate archive retrieval returned HTTP 403 in this pass, so no new JT file or version qualification is inferred from those listings. A controlled set of owner-created exports remains useful: one solid, a repeated-instance assembly with differing transforms/colours, and a multi-material part, each including stored tessellation at the requested file versions.
