# JT version compatibility policy

Research checked **20 October 2026**, on `feature/JT-importer`. This document defines the forward target, not a claim that the entire range is implemented.

## Released versions and public documentation

The latest **10.x file-format release explicitly confirmed in the current release notes located in this research is JT 10.11**. Cortona3D's [RapidAuthor 2606 release notes, 2 September 2026](https://cortona3d.com/en/rapidauthor-2606-release-notes) announce support for it. A translator's release number, the writer build string and the JT file-header version are distinct identifiers.

The latest public Siemens reference located is **JT 10.6 Revision D, 10 February 2026**. The [Siemens JT Open Program](https://www.siemens.com/en-us/products/plm-components/jt/jt-open-program/) links the [reference ZIP](https://static.sw.cdn.siemens.com/media/published/4Zc4NsBnmaFlEd0mtssp7f/siemens-jt-v-10-6-file-format-reference-rev-d-10-feb-2026.zip). Revision D corrects and clarifies the 10.6 reference; it does not change the file version to 10.11. Public-reference availability therefore must not be used as the latest shipping-version ceiling.

There is unresolved contrary evidence: [Altair Inspire 2025's format table](https://2025.help.altair.com/2025/inspire/en_us/topics/shared/unity_basics/file_formats_st_r.htm) lists JT through **11.0**. No Siemens public 11.0 reference or release confirmation was located in this search. Do not silently dismiss that report as a typo, equate it with 10.11, or claim that 10.11 is definitively the latest JT version across every product. Treat 11.0 as a reported version requiring authoritative clarification and sample-header evidence before including it in our implementation target.

## Validation sample identifiers

The owner supplied an additional 9.5 input (A95) on 20 October 2026: header `Version 9.5 JT  DM 9.6.3.1`, 410,366,195 bytes, SHA-256 `5062b6b7d2e089ce0461cb8bfa4fe2cf9243d8fd9da64741fc9ee6dba5987f6a`. The owner selected it for the next qualification pass while forward-version samples are unavailable. See [9.5 validation and Gate 3a](JT_95_VALIDATION.md) for native results, supported layouts and owner UI acceptance on 20 October 2026.

Downloaded community models are referenced only by neutral IDs: L80 (8.0), L81 (8.1), Q82 (8.2 quantized), A103 (10.3 assembly), A109 (10.9 investigation), and R1-R3 (additional regressions). Their original names and paths are kept in ignored local research notes at `out/jt-research/documentation-sample-map.json`. In reproduction commands, `<local-samples>/sample-*.jt` is a placeholder: substitute the corresponding original local file path. These aliases do not rename the source files or bundle their geometry. Hashes and measured results remain available for matching the validation evidence.

## Target range and current qualification

Adopt **JT 6.4 through JT 10.11 as a staged visualization compatibility target**, covering released versions within that envelope through qualified layout families. **If JT 6.4 requires a substantial change to the viewer mesh/assembly data model, cap the lower target at JT 7.0.** This is the owner's decision, recorded 19 October 2026. Version numbers are identifiers, not a continuous decimal interval. The target is conditional on lawful references and representative samples, not universal compatibility or a change to the current version gate.

Share parsers/codecs across compatible record layouts and add adapters for actual format changes. Qualify individual revisions with regression evidence, but do not create a separate importer for every minor version. The current file-version whitelist records enabled coverage while that family map is established.

| Version/family | Current evidence | Planned treatment |
| --- | --- | --- |
| 6.4 | Historical Siemens format evidence; no matching local corpus or qualified parser | Conditional lower target; compare against 7.0 and apply the scene-model decision gate in [legacy assessment](JT_LEGACY_COMPATIBILITY_ASSESSMENT.md) |
| 7.0 | Historical Siemens format evidence; inherited pre-8 conditionals are incomplete evidence | Qualify a pre-8 container/record adapter and tessellation; fallback lower target if the 6.4-specific model-change condition is met |
| 8.0 | Shared 8.x layout family; sample L80 accepted at Gate 0; quantized follow-up passes original fixtures | Retain shared adapters; see [quantized validation](JT_LEGACY_QUANTIZED_VALIDATION.md), Gate 1 UI check accepted |
| 8.1 | Enabled shared legacy family; sample L81 accepted; lossless and qualified quantized fixtures pass | Retain regressions; unsupported Huffman/additional bindings/colour layouts remain explicit |
| 8.2 | sample Q82 now passes native quantized geometry checks: 6 meshes, 115,176 vertices, 205,355 triangles; Gate 1 UI check accepted | Same 8.x decoder; constant RGB(A) and UV retention qualified; texture images and nonconstant quantized colours remain unsupported |
| 9.5 | Enabled sampled Mk.2/topological tessellation subset; original assembly and LE/BE fixtures under Gate 3a | See [9.5 qualification](JT_95_VALIDATION.md); owner accepted Gate 3a on 20 October 2026 |
| Other 9.x | Not enabled or qualified | Reuse the transitional family only after matching evidence and tests; 9.5 qualification does not enable all 9.x |
| 10.0 | Enabled sampled tessellation subset, with owner-tested assembly/appearance follow-ups | Keep as modern regression baseline; improve representation coverage |
| 10.3 | Enabled subset; original sample A103 sample passes native geometry checks and owner accepted Gate 0 | See [sample A103 validation](JT_103_ASSEMBLY_VALIDATION.md); retain the current scene model and qualify further encodings separately |
| 10.1–10.2 | Modern baseline references available; authentic exports requested from the owner | Compare actual records/codecs and qualify through Gate 3d |
| 10.4–10.6 | Public JT 10.5-based Prostep reference and Siemens 10.6 reference available; actual inputs pending | Existing attribute/Sabot/palette adapters are candidates for reuse; qualify actual files through Gate 3e |
| 10.7–10.8 | No qualified local corpus yet | Assess the shared modern family with representative inputs; do not infer acceptance from 10.9 |
| 10.9 | Unchanged original passes native production-path validation: palette attributes, face-group appearance and stored tessellation | [Gate 2b](JT_109_PALETTE_VALIDATION.md) awaits owner production-system testing; 10.10/10.11 follow before older-format work |
| 10.10–10.11 | Shipping-version evidence; no local qualification corpus or matching public reference acquired | Forward target contingent on lawful layout evidence, samples and tests; reassess scope if new representations require substantial redesign |
| 11.0 | Vendor support-table report; authoritative format/release status unresolved | Investigate separately; outside the committed target range |
| Before 6.4, or beyond the target | No qualification commitment | Add only for demonstrated demand after a separate compatibility assessment |

See [native importer status](JT_NATIVE_IMPORTER.md), [8.1 validation](JT_81_VALIDATION.md) and [10.9 sample investigation](JT_109_SAMPLE_INVESTIGATION.md) for actual evidence and limitations. Even an enabled version can contain unsupported geometry codecs, bindings or required scene records.

## Architecture and implementation order

Follow the feature milestones and explicit owner build/acceptance gates in [development phases](JT_DEVELOPMENT_PHASES.md). Gates 0, 1 and 2a are accepted. On 20 October 2026 the owner prioritized **10.9, then 10.10/10.11**, ahead of transitional 9.x/intervening 10.x and pre-8 work. Representative 10.10/10.11 inputs are still needed; the owner plans 10.9 testing on a separate production system using pushed commit `4cb6ebe`. The owner authorized independent forward qualification while that UI result is pending; see [current research and qualification sequence](JT_FORWARD_COMPATIBILITY_RESEARCH.md).

The latest owner direction prioritizes **9.0–9.5 and 10.0–10.8** while 10.9 production testing waits until 21 October. Follow the [point-version gates](JT_POINT_VERSION_EXPANSION.md); newly targeted identifiers still require qualification evidence.

The output contract remains indexed triangles, positions, stored normals, material groups, shared mesh definitions, occurrence nodes with local transforms, and typed metadata at the owned `aiScene` boundary. The modern and qualified 8.1 paths already fit it. Pre-8 compatibility is a hypothesis to assess rather than established support. Keep source-layout differences within the native reader; apply the 6.4/7.0 decision gate if the output contract would need substantial redesign.

1. Preserve the native-tested lossless 8.0–8.2 family and the 10.0/10.3 paths. The owner accepted 8.0 and 10.3 at Gate 0; retain the quantized follow-up accepted at Gate 1. Dispatch container offsets, element versions, compression and integer/geometry codecs explicitly by known layout.
2. Establish the legacy layout-family map: extend quantized geometry/binding coverage across the shared 8.0–8.2 family, then assess 7.0 and 6.4 using matching lawful references and authentic samples. Implement pre-8 adapters if needed; cap at 7.0 only on evidence of the specified 6.4-specific scene-model change. Missing samples leave qualification pending and do not constitute proof that 6.4 needs redesign.
3. Implement the observed newer attributes from the 10.6 reference: trailing version-2 palette fields, checked Sabot unwrapping, accumulated palette state and face-group membership. Map face groups to render material splits while preserving shared source geometry and occurrence overrides. These require richer internal records, not a new viewer scene graph.
4. Qualify the supplied 10.9 file's tessellation, normals, placements and appearance. A header rewrite or successful TOC inspection is not qualification. Legacy reference/sample acquisition need not stop this independent forward work.
5. Fill 9.x and intervening 10.x coverage using matching references, original fixtures and representative real files. Current-version priority places 10.10/10.11 immediately after 10.9 qualification; establish their actual layout/codec differences and representative inputs before enabling them. Transitional and legacy work follows. Do not add a permissive numeric version-range gate.

Qualification for each version/representation includes bounded reads/decompression, malformed-input and cancellation checks, topology/count/bounds checks, stored-normal binding, hierarchy/transforms, material/colour behavior and owner UI acceptance. Independent reference comparison is required where available; record its absence. Retain little/big-endian coverage where specified. Source mesh resources, material splits and occurrences are separate counts.

## Fallback, limits and provenance

Use validated stored tessellation whenever exact B-Rep conversion is absent, unsupported or fails. Try a qualified alternative LOD/representation transactionally; reject a part explicitly if no usable display geometry exists. Do not return successful empty or silently incomplete geometry. Exact Parasolid conversion remains a separate future backend/branch, not a prerequisite for these visualization milestones.

Full PMI rendering, procedural shaders/textures and runtime LOD switching are separate scope. Unknown mandatory encodings, fundamentally different topology or scene semantics, encrypted content, or missing lawful reference material trigger reassessment rather than an unsupported compatibility claim.

Use published reference material and independently written adapters, retaining audited upstream GPL notices and existing Siemens attribution. The public 10.6 copyright terms and limited patent notice are not blanket clearance for every undocumented later feature. Review the provenance and applicable terms for each extension; do not use leaked SDK code or restricted schema catalogs. See [licensing research](JT_IMPORT_FEASIBILITY.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).

About lists only the supported JT versions (**8.0–8.2, 9.5, 10.0, 10.3 and 10.9**) and licence credits. Keep representation limits, validation status, planned compatibility and reference research in the [README](../README.md#jt-import) and detailed documentation. The README describes format capabilities without naming downloaded models; validation evidence uses neutral sample IDs in dedicated documents; original filenames and local paths remain in ignored research notes. Update this matrix, the README and About together when supported versions change. Only explicitly enabled versions pass the production version gate.

## Latest owner confirmation — 20 October 2026

The owner confirmed successful UI testing of the supplied Solid Edge 10.9 assembly export. This supersedes earlier statements that all Gate 2b UI qualification is pending: sampled 10.9 display is accepted. The original production-system palette/face-group comparison remains pending additional coverage; the confirmation does not imply that every such variant was exercised. Gate 3b remains pending independently. Authentic 10.10 exports are now available, while 10.11 inputs remain unavailable. See [10.9 acceptance](JT_109_PALETTE_VALIDATION.md) and the [current expansion plan](JT_POINT_VERSION_EXPANSION.md).
