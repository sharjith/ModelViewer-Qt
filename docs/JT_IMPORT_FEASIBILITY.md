# JT import feasibility and licensing research

Research date: 18 October 2026. Initial JT 10.0 tessellation implementation and validation are recorded in [native importer status](JT_NATIVE_IMPORTER.md). Exact Parasolid conversion is not implemented.

Follow-up research and concrete milestones: [JT/Parasolid implementation scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md), [standalone Parasolid assessment](PARASOLID_IMPORT_RESEARCH.md), and [bounded local sample inventory](JT_PARASOLID_SAMPLE_INVENTORY.json).

## Current provenance and distribution status (19 October 2026)

The implemented subset comes from the official Open CASCADE repository at [verified commit e859e8c2d809f9d9468e6211eba0052f433978ee](https://github.com/Open-Cascade-SAS/JT-Assistant/commit/e859e8c2d809f9d9468e6211eba0052f433978ee), not from the earlier mirror candidate. It contains 104 upstream-derived files retaining GPLv2-or-later notices and eleven original ModelViewer decoder source/header files. See [modification notices](../third_party/occt-jt/MODIFICATIONS.md), the [per-file manifest](../third_party/occt-jt/PROVENANCE.json) and [shipped notices](../THIRD_PARTY_NOTICES.md). GPLv3 is selected for the combined application. Binary releases must provide corresponding source in a manner allowed by GPLv3 section 6; an undated README or credits entry alone is insufficient.

About displays the importer credits and embeds the notices. CMake installs the notices and applicable licence/provenance files; the Windows installer includes the installed data tree. These measures address notice delivery, not patent freedom-to-operate. No requirement to repeat the full notice in every installer UI screen is inferred from the inspected reference terms.

The candidate evaluation and proposed milestones below are historical research, not a statement that implementation is absent. See [native importer status](JT_NATIVE_IMPORTER.md) for current capabilities. No complete IP clearance is claimed; the format-reference permission, source-code licences, sample rights and patent rights are separate.

## Recommendation

A native JT visualization reader is a credible route without purchasing a Siemens CAD seat or JT Open Toolkit license. Siemens explicitly permits independently written JT-reading/display applications and publishes a limited royalty-free patent grant. Those permissions have conditions: this is not a claim that JT has no licenses or that every possible implementation is cleared of all third-party rights.

Start with stored tessellation, normals, assembly hierarchy and transforms. Decide supported file versions from a representative sample corpus. Do not promise universal JT compatibility or exact CAD reconstruction on the basis of this research.

## Primary specification and permissions

Siemens' [JT Open Program page](https://www.siemens.com/en-us/products/plm-components/jt/jt-open-program/) publicly links the format reference. The actual publisher download inspected was [JT 10.6, Revision D, 10 February 2026](https://static.sw.cdn.siemens.com/media/published/4Zc4NsBnmaFlEd0mtssp7f/siemens-jt-v-10-6-file-format-reference-rev-d-10-feb-2026.zip), containing a 493-page PDF and a PMI property spreadsheet. File format version 10.6 and reference revision D are distinct identifiers.

The PDF's printed page ii contains the Siemens JT Data Format Reference Intellectual Property License Terms. In substance:

- Anyone may write and distribute applications accepting JT input and displaying, printing or otherwise interpreting it, subject to the terms.
- The necessary copyrighted list of structures/operators may be used for those purposes with an appropriate copyright notice.
- Permission does not extend to copying the whole reference, Siemens product code or other Siemens copyrighted material. Implement original code from documented algorithms; separately audit any source reused from elsewhere.
- The grant expressly excludes files mixing JT output with data in another format, and incorporating/integrating/combining the JT format, structure or schema into another format, structure or schema. Preserve these boundaries. Reading a conformant JT file into our ordinary rendering scene is the proposed use; the terms should receive specific legal review before extending the feature to hybrid containers or a separately distributed conversion product. Do not infer a blanket prohibition on every geometry export from that clause alone.
- No trademark permission is granted. Patent permission is provided separately.

The [Siemens patent clarification notice](https://www.plm.automation.siemens.com/media/global/en/Siemens-PLM-JT-Data-Format-Reference-Patent-Clarification-Notice-mi-X1_tcm27-58013.pdf) grants non-exclusive, royalty-free rights for the term of the listed patents, for developing, distributing and using software producing, consuming and interpreting specification-compliant JT files. Its discussion covers specification version 9.5 and later. The grant is limited to necessary rights in the named patents and their corresponding foreign patents; it does not cover all Siemens patents, unrelated implementation techniques or third-party patents. These limits matter when choosing algorithms and dependencies.

This is a technical feasibility assessment of the published permissions, not an IP indemnity or jurisdiction-specific legal opinion. There is a documented basis for an independent reader without a separately purchased Siemens software license.

## SDK and source-code alternatives

| Route | Finding | Implication |
| --- | --- | --- |
| Independently implement the published reference | Explicit reader/display permission plus limited patent grant | Recommended baseline; retain notices and respect scope |
| Siemens JT Open Toolkit | Official program distinguishes free Advocate membership from fee-based membership including toolkit access | Do not treat the toolkit as free/open-source, or bundle viewer/SDK binaries without their own permission |
| Older Open Cascade reader, mirrored as cbsghost/oce-jt | Inspected native reader and decoder files have GPLv2-or-later headers | Potential reuse candidate for this GPLv3 project, subject to a complete per-file/dependency audit and version tests |
| Liamsi/jt-java | Inspected Java files have LGPLv3-or-later headers | Useful secondary candidate; needs its own audit and is not a direct native C++ integration |

The official program describes toolkit support for product structure, attributes, LODs, PMI, precise XT B-Rep and supported production JT versions. That commercial coverage is not evidence that an independent first implementation will cover the same cases. JT2Go is a no-charge viewer suitable for visual comparisons under its own terms, not an automatically redistributable decoding library.

The repository root LICENSE is GPLv3. For the native source candidate, README wording alone is insufficient: the inspected files explicitly allow GPL version 2 or later, which permits choosing GPLv3. A GPLv2-only dependency would require a different compatibility analysis; see the [FSF compatibility explanation](https://www.gnu.org/licenses/gpl-faq.en.html#v2v3Compatibility).

Native candidate examined: [cbsghost/oce-jt](https://github.com/cbsghost/oce-jt), tree commit `a7ea581f0dba325c649bab7fe1bc68ca5e85b946`. License-header samples:

- `TKJT/src/JtData/JtData_FileReader.cxx` and `.hxx`.
- `TKJT/src/JtDecode/JtDecode_BitReader.cxx`.
- `TKJT/src/JtDecode/JtDecode_Int32CDP.cxx`.

This was a limited inspection, not a whole-project clearance. Its age, older OCE/Qt dependencies and modern file-version coverage need evaluation before adoption. No third-party implementation code was added during that initial candidate-research phase; the later implementation vendors the official subset identified above.

Follow-up: a complete header scan of the pinned archive's 106 TKJT C/C++ source/header files found GPLv2-or-later notices in all of them. This is stronger evidence than the initial samples, but not a full dependency/provenance audit. Code inspection found old 32-bit TOC-offset handling, whereas the current 10.6 reference specifies 64-bit offsets, and an explicit unimplemented TopoMesh decoding path. Reuse must be selective and version-qualified; it is not a ready-made current JT reader.

## Technical coverage and limits

The current reference documents:

- File headers, byte order, table of contents, segment addressing and compression, including LZMA.
- Logical scene graph nodes, instances, parts, partitions, transforms, material attributes and LOD selection.
- Tessellation representations, quantized vertex data, integer coding, polygon topology and Deering normal decoding. Annex B includes decoding examples; Annex D covers polygon mesh topology.
- XT B-Rep in Annex F and deprecated JT B-Rep in Annex K. Exact representations are therefore not simply absent from the published reference. However, implementing a robust exact-geometry evaluator/tessellator is a separate, much larger undertaking.

For files containing supported stored tessellation, mesh visualization does not inherently require a Parasolid runtime. Files containing only exact B-Rep need an explicit unsupported-representation diagnostic in the initial reader, unless a separate B-Rep solution is implemented. Not every possible payload is specified: the reference explicitly says segment type 33, STEP B-rep, is not defined in this document.

Also account for external partition files, multiple LODs, distinct topology encodings, version-dependent layouts and inherited attributes. A single successfully rendered model will not establish general compatibility.

## Proposed integration

Reuse the existing native 3DXML-to-`aiScene` integration pattern and the existing scene builder. Suggested module responsibilities, subject to a prototype:

1. `Jt::Importer`: entry point, options, cancellation, diagnostics and owned `aiScene` result.
2. `Jt::FileReader`: bounded binary reads, endian handling, header/TOC validation and segment lookup.
3. `Jt::SegmentReader`: bounded decompression and version-aware element dispatch.
4. `Jt::SceneReader`: logical scene graph, occurrence names, local transforms, external partitions, LOD and attribute resolution.
5. `Jt::MeshDecoder`: supported topology/integer codecs, vertices, indices and stored normals.
6. `Jt::SceneBuilder`: mapping shared geometry, occurrences and materials into `aiScene` nodes and meshes for the current rendering pipeline.

Keep the existing XCAF reader ordering. The present dispatch in `src/Import/AssImpModelLoader.cpp` is XCAF, then 3DXML, then Assimp. A dedicated JT branch can follow 3DXML before the Assimp fallback. Extend file dialogs/filters, progress reporting, cancellation and actionable errors with that integration. No intermediate OBJ/GLB conversion is necessary.

## Next research/prototype gate

1. Obtain representative, legitimately usable JT samples; inventory file versions, tessellation codecs and external-file relationships. No `.jt` samples were found directly in the existing ModelViewer-Github data directory during this pass.
2. Audit the native Open Cascade candidate's complete source/dependency licenses and assess supported versions before choosing reuse versus an original reader.
3. Prototype header/TOC/scene inspection and one tessellation path. Validate positions, indices and stored normals before expanding codecs.
4. Compare assembly placement and visible geometry against JT2Go, with quantitative bounds, triangle/occurrence counts and normal checks. Test inherited colors after geometry and hierarchy.
5. Add unsupported-version diagnostics and malformed-input coverage; publish an explicit compatibility matrix. Only then integrate and test the normal open/import workflow.

The initial research-only phase performed no runtime tests. The later implementation and tests are recorded in JT_NATIVE_IMPORTER.md.

## Evidence provenance

Downloads and extracted text used for research are local under ignored `out/jt-research/`; the Siemens reference is not redistributed in tracked documentation.

- Download archive SHA-256: `71b2a73e90094586b099250665a30149c0f52a6c792ed282d4fe95dbea4d93d3`.
- `Siemens JT V10.6 Format Description Rev D 10 Feb 2026.pdf` SHA-256: `30e421356e67cfdee1860916f401b633639a3e8651817dd8ae6a360a5f43381a`.
- Copyright terms inspected on printed page ii; patent notice separately retrieved and read in full.
- Additional candidate: [Liamsi/jt-java](https://github.com/Liamsi/jt-java), inspected `DebugJTReader.java` and `BitReader.java` under `JTViewerGC/src/main/java/uk/ac/liv/jt/`.
