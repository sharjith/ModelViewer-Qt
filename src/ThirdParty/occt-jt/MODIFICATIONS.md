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

## JT 9.5 high-degree topology-mask correction, 20 October 2026

The owner-supplied external-reference export exposed a nonempty high-degree face-mask vector. Public JT 9.5 Rev-A section 7.2.2.1 specifies a plain VecU32, not CDP2. LoadInputData now uses the raw-vector loader for this stream in both 9.5 and JT 10, preserving the additional legacy upper-four-bit stream. Original LE/BE tests verify words, high bits, following split streams/hash alignment and truncation. No permissive zero-length compressed-packet workaround or new file version is added. See research/jt/JT_POINT_VERSION_EXPANSION.md for native results and the pending owner gate.

## Older material field-mask adapter, 20 October 2026

Pre-10 material inhibit masks are normalized to the shared scene accumulator's field numbering after a successful read. The public 9.5 Rev-A table retains the legacy combined diffuse/alpha bit and adds independent RGB/alpha bits; JT 10 reorders these fields. Generic attribute masks and modern material masks are unchanged. Original binary goldens cover every documented bit in both byte orders, and generated cross-version assemblies verify rendered RGB/opacity inheritance. This corrects shared appearance semantics without enabling additional file versions. See research/jt/JT_POINT_VERSION_EXPANSION.md for Gate 3b and the remaining qualification plan. No additional translator implementation or SDK is added.

## JT 9.5 sampled qualification, 20 October 2026

The existing transitional adapters are explicitly enabled only for JT 9.5: 32-bit directory offsets, 16-bit record versions, earlier topology masks and Mk.2 geometry packets remain distinct from JT 10. Mk.2 probability tables permit unused zero-frequency entries, retaining nonzero-total and overflow guards; Mk.1 behavior stays unchanged. Bitlength2 rejects invalid control widths, field widths, ranges and run overruns, bounds nonproductive adjustments and uses defined wide/modulo arithmetic. Chopper widths/low fields are checked and reconstruction uses unsigned shifts/addition; packet word-count arithmetic avoids signed overflow. JT 9.x base/material versions are checked explicitly.

Whole-attribute Final state flags contribute all final fields, including transform/draw accumulation; the original scene adapter separately protects shininess while preserving Force overrides. No new upstream source or SDK was acquired. The archived public JT 9.5 Rev-A reference (2010), existing audited source and bounded observations of sample A95 inform this work. Original tests and the acceptance/LOD limitations are recorded in research/jt/JT_95_VALIDATION.md. This is not blanket 9.x or full 9.5 representation support.

## Palette and face-group rendering, 20 October 2026 (in qualification)

Original JtAttribute_PaletteMap files parse bounded map packets, including the published fallback tail and the observed exact-boundary omitted-tail form. The scene adapter independently accumulates indexed material/draw/transform state, broadcasts fallback attributes into existing entries, and applies inherited map/final/inhibit/force semantics. Topological decoding retains one face-group ID per triangle. The adapter groups faces by resolved appearance, transform and visibility, compacts mesh subsets and shares their resources across occurrences. Inhibited groups retain their geometry as hidden viewer nodes, with an explicit diagnostic that viewer visibility also affects picking. This differs from the reference's render-only inhibition and is not claimed as exact picking fidelity.

Modern arithmetic escape data uses the raw out-of-band vector only inside externally compressed segments; ordinary mesh packets retain nested CDP data. Reader context propagates through bounded views. Affine transform checks tolerate at most 64 double-precision epsilons in the homogeneous row, canonicalize that row and still reject actual projective transforms. Original tests cover raw/nested escapes, tail bounds, state inheritance, maps, hidden geometry, shared resources, transforms and GLB. Palette state now uses copy-on-write snapshots; compact subsets use sorted index remapping. Measured cumulative charges of about 4.64 GB for the 10.9 case require a 6 GiB default budget, retaining caller overrides and the existing 256 MiB per-allocation guard.

The implementation uses the archived public JT 10.6 Revision D sections 5.4.8/5.4.9, 10.2.1 and Annex E, plus local bounded observations. No additional translator implementation or proprietary SDK was added. Fourteen decoder files are original ModelViewer additions; the existing 104 upstream-derived files retain their notices. See research/jt/JT_109_PALETTE_VALIDATION.md for current qualification and owner-gate status.

## Modern attribute envelope foundations, 19 October 2026

JtAttribute_Base retains the version-2 palette index after derived material, geometric-transform and draw-style fields. JtData_Model reads all element payloads through bounded views and replaces Sabot wrappers with their embedded attributes, binding the inner object ID and rejecting invalid/unknown nested payloads. Indexed attributes and palette-map elements remain explicit unsupported boundaries until face-group state accumulation is implemented. No additional JT file version is enabled. Original synthetic tests cover byte orders, field alignment, object identity, malformed envelopes and fallback scene display. See research/jt/JT_MODERN_ATTRIBUTE_VALIDATION.md for the Gate 2a scope and owner handoff. No new third-party source was added; retained upstream notices and manifest classification are unchanged.

## Phase 1 legacy quantized tessellation, 19 October 2026

The shared 8.x vertex reader now adapts the audited quantized coordinate/Deering-normal path, checked strip/vertex-index mapping, original per-vertex UV decoding and the observed packed constant RGB(A) colour form. JtElement_ShapeLOD_Vertex.hxx retains one UV channel; the original application scene builder preserves it through copying and flat variants. Texture images/shaders, nonconstant/HSV/component-array quantized colours and additional bindings remain explicitly unsupported.

CDP1 arithmetic/OOB/multiple-context loading is enabled for the qualified family, with recursion, table/reference/count and code-word checks. Arithmetic decoding bounds nonproductive streams, polls cancellation and uses 64-bit range products. Legacy probability reads reject invalid dimensions/frequencies/references. Quantizers validate ranges/widths; coordinate count/code bounds and normal octant/theta bounds are checked. Predictors use defined modulo-32-bit operations with cancellation polling. Existing file-version qualification and the narrow 8.1 EOF exception remain intact.

See research/jt/JT_LEGACY_QUANTIZED_VALIDATION.md for the original sample Q82 result, independent small fixtures, current appearance limits and Gate 1 UI instructions. The original test serializer and jt_quantized_tests.cpp are outside this vendored subset. No new third-party source or proprietary SDK was bundled; original upstream notices remain retained.

## Shared 8.x family follow-up, 19 October 2026

JtData_Model.cxx recognizes 8.0–8.2 as a shared qualified lossless layout family. The 8.1-only sample L81 EOF envelope exception remains restricted to that minor revision. Shape-LOD and integer-codec diagnostics now refer to the actual 8.x family; the vertex reader identifies unsupported quantized encoding before testing raw attribute bindings. No quantized geometry decoder is enabled by this change. Original native fixture tests cover supported 8.0/8.1/8.2 layouts and encoding rejection across the family. The original 8.0 sample L80 sample passes native geometry checks; the original 8.2 sample Q82 reaches the shared reader and still fails at unsupported quantized geometry. See research/jt/JT_8X_FAMILY_VALIDATION.md and research/jt/JT_LEGACY_COMPATIBILITY_ASSESSMENT.md. No new upstream source or proprietary SDK was added.

## JT 10.3 sample A103 follow-up, 19 October 2026

JtData_Model.cxx explicitly accepts the native-tested 10.3 display subset alongside 8.1 and 10.0. It retains local record/codec guards and rejects other unqualified file versions. The original JtData_Limits.hxx addition now permits a caller-specified cumulative allocation budget, defaults to 4 GiB rather than 1 GiB, restores nested-scope budgets and retains the 256 MiB per-allocation guard. The cumulative counter includes released scratch arrays and is not a live-memory measurement. The original ModelViewer importer forwards the option. See research/jt/JT_103_ASSEMBLY_VALIDATION.md for sample provenance, large-assembly checks, regressions and pending owner UI acceptance. No new upstream source, commercial toolkit or exact converter was added.

The adapted reader supports the tested JT 10.0 tessellation subset. It does not implement standalone Parasolid conversion or provide IP indemnity. See research/jt/JT_NATIVE_IMPORTER.md and research/jt/JT_IMPORT_FEASIBILITY.md in the source tree for coverage and permission limitations.

## Assembly/attribute follow-up, 19 October 2026

JtAttribute_Base.hxx now exposes the decoded accumulation flags; JtAttribute_Base.cxx rejects unqualified base-attribute versions. JtAttribute_Material.cxx/.hxx consume/expose reflectivity and bumpiness, initialize their defaults and reject unqualified material versions. JtData_Model.cxx rejects duplicate IDs and invalid unknown-element envelopes. The new source-level assembly/material/LOD handling is original ModelViewer code under src/Import/Jt, outside this vendored subset. See research/jt/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md for tests and limits.

## Display-selection follow-up, 19 October 2026

JtNode_LOD.cxx and JtNode_RangeLOD.cxx reject unqualified local versions; RangeLOD also validates distance limits and centre values. JtNode_Switch.cxx/.hxx are original GPLv3 additions implementing the published JT 10.0 selected-child layout. The original ModelViewer scene builder implements traversal, optional eye-distance selection and rollback. See research/jt/JT_DISPLAY_SELECTION_VALIDATION.md for evidence, static-selection limits and pending viewer checks. No additional JT file version or XT conversion is enabled.

## Scalar metadata follow-up, 19 October 2026

JtProperty_Scalar.cxx/.hxx are original GPLv3 integer/F32 atom readers. JtProperty_Base exposes source flags and accepts qualified local versions 1/2; JtProperty_String accepts qualified local versions 1/2. JtNode_Base retains ordered property pairs and atom IDs within a count/allocation budget. JtData_Model binds those pairs, validates table versions and duplicate entries, and polls cancellation. The original scene builder adapts supported values to typed Assimp metadata with explicit diagnostics for unsupported or unrepresentable values. See research/jt/JT_METADATA_VALIDATION.md for source-layout evidence, the independent scene-copy fix and application/export limitations.

## Draw-style follow-up, 19 October 2026

JtAttribute_DrawStyle.cxx/.hxx are original GPLv3 readers for the published JT 10.0 local-version-1 draw-style attribute. They reject unqualified versions and reserved data bits. The original scene builder resolves field-specific accumulation, maps culling/unlit hints, generates independent flat face normals, and retains canonical geometry separately from render variants. Unsupported renderer combinations are diagnosed explicitly. See research/jt/JT_DRAW_STYLE_VALIDATION.md for tests, provenance and pending viewer acceptance; no additional JT file version or XT conversion is enabled.

## Vertex-colour follow-up, 19 October 2026

JtDecode_VertexColors.hxx is an original GPLv3 reader for lossless JT 10.0 RGB/RGBA component packets. It consumes the colour hash without checking it, rejects quantized colours explicitly, and bounds counts/components and decoded values. JtElement_ShapeLOD_Vertex.cxx/.hxx now retain colours and assemble coordinate/attribute-record pairs, including colour seams without normals; count/index validation and cancellation polling are added at this boundary. The original scene builder preserves colours through material copies and flat-normal expansion and resolves the material's vertex-colour override flag. See research/jt/JT_VERTEX_COLOUR_VALIDATION.md for qualified scope, fixture limitations and pending UI checks. No additional file version, palette, texture or XT support is claimed.

JtData_Model.cxx now includes the detected major/minor version in unsupported-version errors, following inspection of the owner's JT 8.1 sample L81 sample. Acceptance remains JT 10.0 only; see research/jt/JT_81_CONTAINER_INVESTIGATION.md.

## JT 8.1 follow-up, 19 October 2026

Acceptance now includes the tested JT 8.1 lossless subset as well as JT 10.0. JtData_Model corrects the bounded final-graph length discrepancy seen in sample L81; JtData_Inflate adds complete stream/checksum/output validation and an explicit one-zero-padding allowance for legacy raw vertex blocks. Base/string/scalar property and LOD/RangeLOD guards dispatch older unversioned layouts. Material exposes its legacy layout for nonnegative Phong shininess preservation. The raw vertex decoder is rewritten with checked sizes/reads, explicit unsupported bindings, cancellation and winding reset per strip; CDP1 null/bitlength paths are bounded and unqualified legacy codecs are rejected. TriStrip/vertex LOD local versions are checked. Switch/draw-style readers reject unqualified pre-9 layouts. The original scene adapter diagnoses and reconstructs zero-length stored normals. See research/jt/JT_81_VALIDATION.md for exact coverage, evidence and pending UI checks. Existing upstream notices remain; this is not a blanket JT 8.x/9.x or exact-XT compatibility claim.

## JT 9.0 qualification, 20 October 2026

`JtData_Model.cxx` enables the independently supplied 9.0 export through the existing 9.x transitional container/record/codec family, retaining explicit rejection of unqualified intermediate revisions. `JtAttribute_Material.cxx` limits 9.0 to the sampled local material version 1, while retaining qualified 9.5 material versions 1/2. No new mesh codec, SDK or source package is introduced. Dated notices and existing upstream GPL terms remain. Original LE/BE graph/malformed-version tests and unchanged authentic export checks are documented in `research/jt/JT_90_VALIDATION.md`; owner UI acceptance is pending.

## Explicitly empty shape follow-up, 20 October 2026

`JtNode_Shape_Base.cxx/.hxx` retain whether count ranges were successfully read and explicitly advertise zero vertex/polygon ranges. `JtElement_ShapeLOD_Vertex.cxx/.hxx` distinguish a zero-coordinate topological payload only when every serialized topology stream is empty, avoiding access to the absent modern upper-four-bit stream. Nonempty decoding remains shared. The original scene adapter retains only these doubly evidenced empty nodes alongside real geometry; arbitrary/contradictory empty output and empty-only LODs remain errors. No empty render mesh is created. Original LE/BE parser and scene regressions, authentic additional 9.0 assembly evidence and pending UI retest are documented in `research/jt/JT_90_VALIDATION.md`.

## JT 10.5 qualification, 20 October 2026

`JtData_Model.cxx` enables the authentic 10.5 export through the existing modern container/record/codec implementation. No additional reader class, codec, dependency or translator source is introduced. Twelve Sabot-wrapped indexed material attributes and six palette maps in the supplied assembly exercise the shared implementation against the public Prostep JT 10.5-based reference and owner data. Original LE/BE attribute, palette-tail, material-order and malformed-record cases now include 10.5. See `research/jt/JT_105_VALIDATION.md` for scope, native results, cross-version comparison and pending owner UI acceptance. Intermediate 10.4/10.6 remain unqualified.

## JT 10.7/10.8 qualification, 20 October 2026

`JtData_Model.cxx` admits the independently supplied 10.7 and 10.8 exports through the existing modern reader. No new codec, scene model, SDK or source package is introduced. Original endian, attribute/palette, fallback rendering and malformed-record tests now exercise these versions. Authentic data and comparisons with the accepted 10.9 export are documented in `research/jt/JT_107_108_VALIDATION.md`; owner application checks are pending. Existing GPL notices and upstream source hashes are retained. No blanket coverage for other revisions/encodings is claimed.

## JT 10.10 qualification, 20 October 2026

`JtData_Model.cxx` enables independently supplied authentic 10.10 assemblies through the existing modern reader. Original endian, malformed attribute/palette and fallback scene tests now include the two-digit minor revision. No new codec, SDK or translator source is introduced. See `research/jt/JT_1010_VALIDATION.md` for native evidence and the pending Gate 5a owner checks. Existing upstream notices and GPL terms remain. JT 10.11 remains unqualified. The separate exporter-material profile extension is in original scene-adapter code, not the third-party decoder.

## Bounded legacy Huffman, 20 October 2026

`JtDecode_Int32CDP` admits the original fixture-qualified single-context CDP1 Huffman path for 8.x. `JtDecode_Int32CDP_Huffman.cxx/.hxx` replace leaking raw tree/code objects with charged vector ownership while retaining the upstream heap tie order; handle singleton alphabets, exact declared-bit limits, unsigned frequency sums, escaped-value bounds/consumption and cancellation. The unused code-assignment recursion is removed. Multiple-context Huffman stays explicit unsupported. Existing upstream copyright/GPL notices and original source hashes are retained. No new decoder source package or dependency is introduced. See `research/jt/JT_HUFFMAN_VALIDATION.md` for original fixture scope, regression results and owner acceptance status.

## Modern packed quantized colours, 21 October 2026

The ModelViewer-original `JtDecode_VertexColors.hxx` adds bounded packed RGB/HSV colour transport for the modern family. It checks component/count/width/range limits, reads the documented predictor-free packed codes, applies the inverse round-to-nearest uniform quantizer, maps hue sectors and retains existing lossless Lag1 behaviour. Zero-bit nonconstant quantizers and the different older component-array form remain explicit unsupported; vertex translucency rendering remains separate. The implementation is original under GPL-3.0-only, with no added dependency or source package. See `research/jt/JT_QUANTIZED_COLOUR_VALIDATION.md` for synthetic and private fixture scope, regression results and owner gate.

## Transitional vertex colours, 21 October 2026

The ModelViewer-original `JtDecode_VertexColors.hxx` dispatches 9.x lossless fields to paired raw exponent/mantissa packets and quantized fields to four Lag1 component vectors, including the serialized alpha vector when only RGB is exposed. It checks field widths, counts, component ranges and packet bounds. Quantizer validation/dequantization and HSV conversion are shared with the modern packed reader; existing modern byte/field tests remain regression coverage. No new dependency or third-party source package is introduced. `research/jt/JT_9X_COLOUR_VALIDATION.md` records the reference, original vectors, private geometry-template controls and pending owner gate.

## Topological UV arrays, 21 October 2026

The ModelViewer-original `JtDecode_VertexUV.hxx` reads bounded channel-0 two-component UV arrays: 9.x raw exponent/mantissa fields, modern binary IEEE/Lag1 fields and quantized per-component Lag1 codes. UVs may be negative or tiled; only nonfinite values, invalid quantizers/counts and unqualified bindings are rejected. `JtElement_ShapeLOD_Vertex.cxx/.hxx` retain UV records by the same coordinate/attribute pair as colours and normals, including UV-only seams. Additional channels/dimensions are explicitly unsupported rather than silently discarded. No new dependency or third-party source package is introduced; original upstream notices and source hashes remain. See `research/jt/JT_TOPOLOGICAL_UV_VALIDATION.md` for reference and test boundaries and the owner gate.

## Repository directory alignment, 21 October 2026

Decoder implementation and metadata moved to `src/ThirdParty/occt-jt`; decoder headers moved to `include/ThirdParty/occt-jt`, retaining package subdirectories. The dedicated `mv_jt_codec` target remains; the main application's recursive scan excludes these files to avoid compiling them twice. Build, test, packaging and documentation references follow the new layout. The provenance checker covers both roots using repository-relative manifest paths, preserving upstream paths/hashes and licence notices. Decoder logic is unchanged; source-comment document paths are updated. Installed licence paths are unchanged. C1d was owner-accepted before this separate structural change.

Relocation validation: the isolated MSVC AddressSanitizer build and full test suite plus the five prior 10.0 part regressions pass. Application CMake configuration passes; generated build rules contain exactly 49 decoder implementation compilations in `mv_jt_codec`, none directly in ModelViewer, and all decoder objects on the application link. Installed notice inputs and updated document links resolve. All 119 source/header contents match the accepted C1d commit except the documented source-comment path replacement; upstream provenance identities/hashes remain unchanged. The full viewer was not rebuilt by the agent.

## Unused deprecated OCCT includes, 21 October 2026

Removed the unused `TColStd_SequenceOfInteger.hxx` include from `JtData_Model.cxx` and unused `TColStd_HArray1OfCharacter.hxx` include from `JtElement_XTBRep.cxx`. The latter transitively included the deprecated character-array alias as well. Neither translation unit uses these types; no replacement container or decoding change is needed. Upstream notices and provenance identities remain intact.

## Multiple UV sets, 21 October 2026

`JtElement_ShapeLOD_Vertex.cxx/.hxx` retain four contiguous two-component texture-coordinate sets through the original topological UV reader. The legacy path still uses set 0. Binding layout checks reject sparse sets, sets above 3 and other component counts explicitly. Corner assembly preserves seams in any supported set; original scene-adapter code now copies all four sets through material variants, face-group splitting and flat-normal expansion. No additional codec/dependency/source package is introduced. Upstream identities/notices are retained. `research/jt/JT_MULTI_UV_VALIDATION.md` records original and private fixture coverage and the owner gate.

## Scalar texture-coordinate bindings, 21 October 2026

The original `JtDecode_VertexUV.hxx` reads one or two coordinate components, including one quantizer per scalar collection rather than reading an absent V quantizer. `JtElement_ShapeLOD_Vertex` validates mixed scalar/UV bindings and preserves each component count through corner expansion. The original scene adapter converts scalar U into explicit `(U,0)` pairs so existing rendering, mesh variants and GLB retain values consistently. Three-/four-component collections remain unqualified; no source texture-resource or shader semantics are inferred. No new dependency or third-party code is added. See `research/jt/JT_SCALAR_UV_VALIDATION.md` for qualification and pending owner checks.

## Sparse texture-coordinate bindings, 21 October 2026

`JtElement_ShapeLOD_Vertex.cxx` validates independent scalar/2D bindings in slots 0–3 and reads only present collections. Corner assembly retains absent decoder slots, discovers attribute counts from the first present set when needed and preserves UV-only higher-set seams. Original scene-adapter code zero-pads internal gaps to retain source set numbers through Assimp copy/export. No new dependency or third-party code is added; original notices and upstream hashes remain intact. See `research/jt/JT_SPARSE_UV_VALIDATION.md` for the exact representation boundary and owner gate.

## Declared arithmetic symbol counts, 21 October 2026

`JtDecode_Int32CDP.cxx` passes bounded explicit/implicit CDP1 symbol counts to `JtDecode_Int32CDP_Arithmetic.cxx/.hxx`. The driver consumes declared non-emitting context transitions as well as emitting symbols, checks output bounds/count and OOB completion, and polls cancellation throughout. CDP2/CDP3 retain their previous driver. No new package or third-party code is introduced; upstream notices and source hashes remain. Original field/full-model tests and the owner gate are in `research/jt/JT_ARITHMETIC_COUNT_VALIDATION.md`.

## Rounded topological coordinates, 21 October 2026

`JtDecode_VertexData.cxx` selects the original rounded inverse mode in `JtDecode_VertexData_Quantized.cxx/.hxx` for 9.x/10.x topological coordinates, with early count/component/equal-width checks. The inverse uses the published code range ending at `2^bits-1`, including 32-bit widths, while retaining the previously qualified legacy midpoint path. No new source package/dependency is introduced; upstream notices and identities are retained. Original fields and private controls, with source/oracle boundaries and the owner gate, are recorded in `research/jt/JT_COORDINATE_QUANTIZATION_VALIDATION.md`.

## Texture descriptor foundation, 21 October 2026

22 October follow-up: the same original attribute reader adds the distinct published JT9.5 basic local-version-1 payload for enabled 9.0/9.5: I16 local version, legacy type values, implicit UV0, U8 shared-image flag and advanced-block diagnostic skip. The original adapter retains raw source metadata while normalizing only the supported effective mapping and compression-inhibit bit. No upstream-derived file or file count changed. `research/jt/JT_9X_TEXTURE_VALIDATION.md` records reference diagrams and pending owner qualification.

The original `JtAttribute_TextureImage.hxx/.cxx` registers the published texture-image GUID and reads modern local-version-1 descriptors under the existing element envelope. It validates finite mapping parameters, image/mipmap counts, declared byte totals and cancellation. The owner-requested image follow-up retains bounded base-mip bytes and the owning filename for the original scene adapter's supported 2D image mapping; higher mips are skipped. Older-family payloads remain explicitly unqualified and skipped within their envelope. New files carry ModelViewer GPLv3-only notices and manifest entries; inherited source is unchanged. See `research/jt/JT_TEXTURE_DESCRIPTOR_VALIDATION.md` for the exact scope and pending combined image-rendering owner gate. The agent has not built or executed the C++ changes.
## C2j advanced 9.x texture continuation — 22 October 2026

The ModelViewer-original `JtAttribute_TextureImage.cxx` now consumes complete empty lower-version compatibility blocks before active v9 local-version-2/3 descriptors and reads the version-3 coordinate channel after image data. Existing image bounds/cancellation and raw metadata are retained; future layouts remain diagnosed. No upstream-derived source/hashes or file counts change. [Validation and reference provenance](../../../research/jt/JT_9X_ADVANCED_TEXTURE_VALIDATION.md) records the retained public JT9.5 reference, original tests and pending owner gate. No vendor implementation was used.

## C2k texture source byte order � 22 October 2026

The ModelViewer-original texture header/implementation retain the owning file byte order alongside unchanged raw image payloads, allowing the original scene adapter to normalize unsigned 16-bit colour components safely into the existing RGBA8 image path. No upstream-derived source/hashes or decoder file counts change. [C2k validation](../../../research/jt/JT_TEXTURE_UINT16_VALIDATION.md) records source references, prepared original tests and pending owner gate. No vendor implementation was used.
