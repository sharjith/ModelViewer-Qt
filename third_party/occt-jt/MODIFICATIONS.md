# ModelViewer-Qt modifications to JT-Assistant

Recorded 19 October 2026; functional work performed 18 October 2026, notices added 19 October 2026. Upstream: Open-Cascade-SAS/JT-Assistant, commit e859e8c2d809f9d9468e6211eba0052f433978ee.

Original copyright, GPL-2.0-or-later permissions and warranty notices remain in the upstream-derived files. ModelViewer-Qt selects GPLv3 for the combined work. Dated notices identify modified upstream files. PROVENANCE.json maps each included source/header to its upstream path and original SHA-256, distinguishes original additions, and records whether its pre-notice contents differed (including whitespace). The original decoder additions use GPLv3.

Changes include:

- Modern OCCT handles, RTTI, hashing, standard stream types and API compatibility.
- Explicit JT 10.0 acceptance, 64-bit TOC addressing, corrected versioned graph/attribute and shape-envelope layouts, complete XT payload headers and material alignment.
- XZ/LZMA decompression, bounded file/segment/buffer access, allocation/count/recursion limits, cancellation and explicit diagnostics.
- Mark-3 integer bitlength and move-to-front decoding, modern topology packet masks, binary component streams and packed Deering normal handling.
- Corrected ownership, vector cleanup, integer/bit bounds, arithmetic packet validation and truncated-stream behavior.
- Disabled upstream decoder threading and original ModelViewer CMake integration using OCCT, zlib and LibLZMA. No viewer, Eigen copy or commercial XT converter is included.
- Source whitespace normalized during import. The manifest records provenance; the Git history and upstream comparison provide the exact changes, not a promise that every listed subsystem changed in every file.

## Phase 1 legacy quantized tessellation, 19 October 2026

The shared 8.x vertex reader now adapts the audited quantized coordinate/Deering-normal path, checked strip/vertex-index mapping, original per-vertex UV decoding and the observed packed constant RGB(A) colour form. JtElement_ShapeLOD_Vertex.hxx retains one UV channel; the original application scene builder preserves it through copying and flat variants. Texture images/shaders, nonconstant/HSV/component-array quantized colours and additional bindings remain explicitly unsupported.

CDP1 arithmetic/OOB/multiple-context loading is enabled for the qualified family, with recursion, table/reference/count and code-word checks. Arithmetic decoding bounds nonproductive streams, polls cancellation and uses 64-bit range products. Legacy probability reads reject invalid dimensions/frequencies/references. Quantizers validate ranges/widths; coordinate count/code bounds and normal octant/theta bounds are checked. Predictors use defined modulo-32-bit operations with cancellation polling. Existing file-version qualification and the narrow 8.1 EOF exception remain intact.

See docs/JT_LEGACY_QUANTIZED_VALIDATION.md for the original sample Q82 result, independent small fixtures, current appearance limits and Gate 1 UI instructions. The original test serializer and jt_quantized_tests.cpp are outside this vendored subset. No new third-party source or proprietary SDK was bundled; original upstream notices remain retained.

## Shared 8.x family follow-up, 19 October 2026

JtData_Model.cxx recognizes 8.0–8.2 as a shared qualified lossless layout family. The 8.1-only sample L81 EOF envelope exception remains restricted to that minor revision. Shape-LOD and integer-codec diagnostics now refer to the actual 8.x family; the vertex reader identifies unsupported quantized encoding before testing raw attribute bindings. No quantized geometry decoder is enabled by this change. Original native fixture tests cover supported 8.0/8.1/8.2 layouts and encoding rejection across the family. The original 8.0 sample L80 sample passes native geometry checks; the original 8.2 sample Q82 reaches the shared reader and still fails at unsupported quantized geometry. See docs/JT_8X_FAMILY_VALIDATION.md and docs/JT_LEGACY_COMPATIBILITY_ASSESSMENT.md. No new upstream source or proprietary SDK was added.

## JT 10.3 sample A103 follow-up, 19 October 2026

JtData_Model.cxx explicitly accepts the native-tested 10.3 display subset alongside 8.1 and 10.0. It retains local record/codec guards and rejects other unqualified file versions. The original JtData_Limits.hxx addition now permits a caller-specified cumulative allocation budget, defaults to 4 GiB rather than 1 GiB, restores nested-scope budgets and retains the 256 MiB per-allocation guard. The cumulative counter includes released scratch arrays and is not a live-memory measurement. The original ModelViewer importer forwards the option. See docs/JT_103_ASSEMBLY_VALIDATION.md for sample provenance, large-assembly checks, regressions and pending owner UI acceptance. No new upstream source, commercial toolkit or exact converter was added.

The adapted reader supports the tested JT 10.0 tessellation subset. It does not implement standalone Parasolid conversion or provide IP indemnity. See docs/JT_NATIVE_IMPORTER.md and docs/JT_IMPORT_FEASIBILITY.md in the source tree for coverage and permission limitations.

## Assembly/attribute follow-up, 19 October 2026

JtAttribute_Base.hxx now exposes the decoded accumulation flags; JtAttribute_Base.cxx rejects unqualified base-attribute versions. JtAttribute_Material.cxx/.hxx consume/expose reflectivity and bumpiness, initialize their defaults and reject unqualified material versions. JtData_Model.cxx rejects duplicate IDs and invalid unknown-element envelopes. The new source-level assembly/material/LOD handling is original ModelViewer code under src/Import/Jt, outside this vendored subset. See docs/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md for tests and limits.

## Display-selection follow-up, 19 October 2026

JtNode_LOD.cxx and JtNode_RangeLOD.cxx reject unqualified local versions; RangeLOD also validates distance limits and centre values. JtNode_Switch.cxx/.hxx are original GPLv3 additions implementing the published JT 10.0 selected-child layout. The original ModelViewer scene builder implements traversal, optional eye-distance selection and rollback. See docs/JT_DISPLAY_SELECTION_VALIDATION.md for evidence, static-selection limits and pending viewer checks. No additional JT file version or XT conversion is enabled.

## Scalar metadata follow-up, 19 October 2026

JtProperty_Scalar.cxx/.hxx are original GPLv3 integer/F32 atom readers. JtProperty_Base exposes source flags and accepts qualified local versions 1/2; JtProperty_String accepts qualified local versions 1/2. JtNode_Base retains ordered property pairs and atom IDs within a count/allocation budget. JtData_Model binds those pairs, validates table versions and duplicate entries, and polls cancellation. The original scene builder adapts supported values to typed Assimp metadata with explicit diagnostics for unsupported or unrepresentable values. See docs/JT_METADATA_VALIDATION.md for source-layout evidence, the independent scene-copy fix and application/export limitations.

## Draw-style follow-up, 19 October 2026

JtAttribute_DrawStyle.cxx/.hxx are original GPLv3 readers for the published JT 10.0 local-version-1 draw-style attribute. They reject unqualified versions and reserved data bits. The original scene builder resolves field-specific accumulation, maps culling/unlit hints, generates independent flat face normals, and retains canonical geometry separately from render variants. Unsupported renderer combinations are diagnosed explicitly. See docs/JT_DRAW_STYLE_VALIDATION.md for tests, provenance and pending viewer acceptance; no additional JT file version or XT conversion is enabled.

## Vertex-colour follow-up, 19 October 2026

JtDecode_VertexColors.hxx is an original GPLv3 reader for lossless JT 10.0 RGB/RGBA component packets. It consumes the colour hash without checking it, rejects quantized colours explicitly, and bounds counts/components and decoded values. JtElement_ShapeLOD_Vertex.cxx/.hxx now retain colours and assemble coordinate/attribute-record pairs, including colour seams without normals; count/index validation and cancellation polling are added at this boundary. The original scene builder preserves colours through material copies and flat-normal expansion and resolves the material's vertex-colour override flag. See docs/JT_VERTEX_COLOUR_VALIDATION.md for qualified scope, fixture limitations and pending UI checks. No additional file version, palette, texture or XT support is claimed.

JtData_Model.cxx now includes the detected major/minor version in unsupported-version errors, following inspection of the owner's JT 8.1 sample L81 sample. Acceptance remains JT 10.0 only; see docs/JT_81_CONTAINER_INVESTIGATION.md.

## JT 8.1 follow-up, 19 October 2026

Acceptance now includes the tested JT 8.1 lossless subset as well as JT 10.0. JtData_Model corrects the bounded final-graph length discrepancy seen in sample L81; JtData_Inflate adds complete stream/checksum/output validation and an explicit one-zero-padding allowance for legacy raw vertex blocks. Base/string/scalar property and LOD/RangeLOD guards dispatch older unversioned layouts. Material exposes its legacy layout for nonnegative Phong shininess preservation. The raw vertex decoder is rewritten with checked sizes/reads, explicit unsupported bindings, cancellation and winding reset per strip; CDP1 null/bitlength paths are bounded and unqualified legacy codecs are rejected. TriStrip/vertex LOD local versions are checked. Switch/draw-style readers reject unqualified pre-9 layouts. The original scene adapter diagnoses and reconstructs zero-length stored normals. See docs/JT_81_VALIDATION.md for exact coverage, evidence and pending UI checks. Existing upstream notices remain; this is not a blanket JT 8.x/9.x or exact-XT compatibility claim.
