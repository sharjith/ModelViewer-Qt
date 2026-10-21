# Native JT importer

**Current checkpoint, 20 October 2026:** Gates 0, 1 and 2a are owner-accepted. The [10.9 palette follow-up](JT_109_PALETTE_VALIDATION.md) passes production-path native validation on the unchanged original and has owner-confirmed 10.9 exported-assembly UI acceptance; the original production-sample comparison remains pending. Enabled subsets are 8.0-8.2, 9.0, 9.5, 10.0, 10.3, 10.5, 10.7, 10.8, 10.9 and 10.10. The owner selected the available [9.5 input for Gate 3a](JT_95_VALIDATION.md) while newer-version samples are unavailable; the owner accepted Gate 3a on 20 October 2026 with “9.5 works”, separately from pending Gate 2b. The latest immediate target is [9.0–9.5 and 10.0–10.8](JT_POINT_VERSION_EXPANSION.md). Gate 3b older material-mask normalization and the supplied-export high-degree topology correction are native-tested and await owner display checks; no additional file-version identifier is enabled by that adapter.

## Historical milestones

Work on `feature/JT-importer`, 18 October 2026. Uncommitted implementation; exact Parasolid conversion is not implemented.

The tested assembly/display-selection milestone was committed as `b72d2be`, the [scalar metadata pass](JT_METADATA_VALIDATION.md) as `c435335`, and the [draw-style pass](JT_DRAW_STYLE_VALIDATION.md) as `8e36e48`, following the owner's UI acceptance. The subsequent [per-material wireframe follow-up](JT_WIREFRAME_VALIDATION.md) replaces the filled wireframe-hint fallback and has passed the owner's application tests, with visible selection highlighting clarified in that document. The [shininess follow-up](JT_SHININESS_VALIDATION.md) preserves the stored Phong exponent and provides approximate PBR roughness. The owner authorized committing both follow-ups after automated validation; the additional shininess UI check remains pending. Earlier uncommitted-status statements below are historical.

## Follow-up: assembly and attribute work (19 October 2026)

The subsequent [Switch and RangeLOD pass](JT_DISPLAY_SELECTION_VALIDATION.md) adds authored Switch choices and optional distance-based import selection, with transactional display fallback. The viewer retains static first-child RangeLOD selection unless an API caller supplies an eye point; runtime LOD switching is not implemented.

See [assembly/attribute validation](JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md) for the implementation and test matrix. It adds field-aware material and transform inheritance, resource sharing, nested external-partition tests and transactional representation fallback. The historical initial-scope descriptions below should be read with the subsequent follow-ups. JT 8.1 lossless support is now added as described below; no XT conversion has been added.

## Current behavior

See [development phases and owner gates](JT_DEVELOPMENT_PHASES.md) for the active delivery plan. The owner accepted Gate 0. The [legacy quantized follow-up](JT_LEGACY_QUANTIZED_VALIDATION.md) now decodes the supplied 8.2 sample Q82 and has passed Gate 1 UI acceptance.

The [version compatibility policy](JT_VERSION_COMPATIBILITY.md), checked 19 October 2026, sets a staged visualization target of JT 6.4 through 10.11, with a 7.0 lower cap if 6.4 requires substantial viewer scene-model changes. The shared lossless/qualified-quantized 8.0–8.2 family and 10.0/10.3/10.9 subsets are enabled. Gate 0 is accepted; the quantized sample Q82 follow-up has passed Gate 1 UI acceptance. The public reference baseline is 10.6 Revision D; later shipping versions and unresolved vendor reports of 11.0 are distinguished in that policy. The target does not imply acceptance of all intervening versions.

The [JT 10.3 sample A103 follow-up](JT_103_ASSEMBLY_VALIDATION.md) reuses the existing modern container/record/mesh layouts and scene graph. Sample A103 was qualified with the then-revised 4 GiB cumulative charged-allocation budget; the subsequent [10.9 palette follow-up](JT_109_PALETTE_VALIDATION.md) sets the current default to 6 GiB; the 256 MiB per-allocation limit remains. The counter includes freed scratch arrays and is not peak resident memory. Callers can lower it through `Options::decodedAllocationBudget`. Its native result is 2,468 mesh resources and 2,853 mesh occurrences, with all emitted indices, normalized normals and finite local transforms checked.

The [lossless vertex-colour follow-up](JT_VERTEX_COLOUR_VALIDATION.md) adds JT 10.0 RGB/opaque-RGBA streams, corner seams and material colour overrides. Automated tests pass, and the owner reports successful viewer tests; that follow-up remains uncommitted. The legacy follow-up adds the observed constant quantized RGB(A) form. Nonconstant quantized colours and active vertex translucency remain unsupported. The subsequently supplied [sample L81 sample](JT_81_CONTAINER_INVESTIGATION.md) drives the new [JT 8.1 lossless path](JT_81_VALIDATION.md), which passes isolated native tests and has the owner's application confirmation. The later [JT 10.9 sample](JT_109_SAMPLE_INVESTIGATION.md) uses newer base attributes, palette maps and Sabot wrappers; the [palette follow-up](JT_109_PALETTE_VALIDATION.md) now imports it through the production path and awaits owner testing.

`Jt::Importer` in `include/Import/Jt/JtImporter.h` and `src/Import/Jt/JtImporter.cpp` produces an owned Assimp scene. The normal open/import dispatch is XCAF, 3DXML, JT, then Assimp. JT is added to the file dialog. The existing loader builds render meshes and the scene tree from this scene and counts mesh occurrences for progress. JT cancellation uses the native-import rollback path.

The native decoder accepts the JT 8.0–8.2 lossless/qualified-quantized layout family plus JT 9.0, JT 9.5, JT 10.0, JT 10.3, JT 10.5, JT 10.7, JT 10.8, JT 10.9 and JT 10.10 subsets. The 9.5 path preserves 32-bit offsets and 16-bit record versions, Mk.2 packets, earlier topology masks, exponent/mantissa lossless coordinates and separate Deering-normal streams. Whole-attribute Final flags now lock accumulated fields and shininess; Force remains a one-shot override. See [8.x family validation](JT_8X_FAMILY_VALIDATION.md): minor versions share the reader, while unsupported geometry encodings are rejected at their records. The sampled modern paths handle LZMA/XZ segments, 64-bit TOC offsets, logical scene graph and TopoMesh tessellation, including mark-3 integer packets, binary coordinates and packed Deering normals. JT 8.1 handles 32-bit directories, legacy graph/property/material layouts, null/bitlength CDP1 strip packets and lossless raw positions/normals, including zlib streams. The legacy quantized follow-up adds positions, stored normals, per-vertex UVs, constant RGB(A), checked index remapping and CDP1 arithmetic/OOB/multiple contexts. The subsequent [C1a gate](JT_HUFFMAN_VALIDATION.md) qualifies single-context legacy Huffman with original fixtures; multiple-context Huffman, additional bindings and nonconstant quantized colours remain unsupported. Only one usable display LOD is selected, trying children in source order. An ordinary JT LOD list does not imply a highest-detail order. All three LODs in each originally downloaded JT 10.0 part were separately decoded during development.

Scene construction retains local transforms, group/instance nodes, names, inherited diffuse appearance and hidden state. Geometry can be shared across occurrences with the same appearance. External partitions are restricted to the assembly folder and its descendants and resolved relative to the referring file. Synthetic and local-part-backed assembly fixtures now exercise these paths; diverse real assemblies and independent comparison remain required for broader qualification. Per-face material palettes, PMI, textures and universal JT version coverage are outside this initial implementation.

## Exact geometry and fallback

The product registers no XT backend. Parts with B-Rep references use their stored display tessellation and emit a diagnostic. A part with no usable supported display tessellation fails explicitly.

`Options::exactDecoder` is an optional future integration seam. Complete neutral-binary XT payload extraction is exercised on the five samples; subordinate/MultiXT body resolution is unsupported. A callback result must pass OCCT B-Rep validation and triangulation. Exceptions or invalid shapes select stored tessellation instead. Successful exact output replaces the display alternative, avoiding duplicate meshes. Cancellation and allocation failures propagate.

The accepted-backend test injects an OCCT box to test selection and ownership only. It is not an XT translation test or evidence of geometric agreement. An eventual real backend also needs source-unit, tolerance and geometry-agreement checks before activation.

## Local corpus and provenance

Samples: `out/jt-research/samples/{Bolt,Brake_Disk,Caliper,Rim,Tire}.jt`.

Downloaded from NVIDIA's [VFI workflow dataset](https://developer.download.nvidia.com/USD/dataset/vfi-guide/vfi_workflow_data.zip), linked by NVIDIA's [VFI samples repository](https://github.com/NVIDIA-Omniverse/vfi-samples). Original archive: `out/jt-research/nvidia-vfi.zip`. The corpus stays outside version control; no redistribution permission is inferred from availability. These are five related part files, not a diverse assembly corpus.

Code provenance and dependencies: [vendored decoder notice](../src/ThirdParty/occt-jt/README.md). The selected source is the official Open CASCADE repository, not the earlier mirror candidate. Published JT permissions and their limitations remain in [feasibility research](JT_IMPORT_FEASIBILITY.md).

The [JT 10.0 reference](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf) was obtained alongside 10.6 Revision D. The samples use a nested shape-envelope GUID and an XT transmit-format byte not clearly described by the 10.0 diagrams; these paths are sample-qualified and must not be generalized to arbitrary layouts by guessing.

## Validation

Isolated native decoder/importer tests run with MSVC AddressSanitizer:

| Sample | Render vertices | Triangles | Selected meshes |
| --- | ---: | ---: | ---: |
| Bolt | 913 | 904 | 1 |
| Brake_Disk | 13,146 | 13,324 | 1 |
| Caliper | 1,632 | 1,400 | 1 |
| Rim | 10,526 | 14,156 | 1 |
| Tire | 16,301 | 16,018 | 1 |

Checks include triangle index range, finite coordinates, normalized stored normals, no duplicate LOD geometry, absent/rejected/invalid exact-backend fallback, injected accepted-backend selection, cancellation and a GLB export/reimport round trip. Portable tests construct original integer-codec packets and exercise truncation, negative counts and allocation rejection without bundling vendor data.

Twenty-three deterministic malformed-file probes completed without sanitizer reports or timeouts; seventeen were rejected. A further 48 mutations inside decoded graph/geometry payloads completed without crashes or timeouts. Corruption confined to unused payloads can leave the selected display geometry readable. This small mutation pass is not exhaustive fuzzing. Input/segment lengths, decompression sizes, vector counts, bit reads, graph depth/cycles and decoding allocations are bounded.

These counts are regression observations, not independent geometry ground truth. Source topology/hash verification and visual comparison with an independent JT reader remain outstanding. The user built the application and reported that the first UI loading test passed. Assembly transforms/instancing and external partitions require further corpus validation. An include-name collision found during development was addressed by naming the new files `JtImporter.h` and `JtImporter.cpp`.

## Opacity correction after first UI test

The first UI test revealed unintended transparency in all five parts. The decoder had omitted JT 10's four-byte `Field Final Flags` word in Base Attribute Data (JT 10.0 reference, section 6.1.2.1.1). This shifted material-version, flag and color reads: the blue color channel was mistaken for diffuse alpha. The shared header also precedes geometric transforms, so its alignment is now checked with a synthetic transform test.

Raw sample inspection confirms that all five materials have diffuse alpha `1.0` and flags `0x3980` (blending disabled). Correct parsing restores their intended RGB values and full opacity. Mapping also honors the material blending-enable bit; disabled blending produces an opaque render material even if an unused alpha is below one. Opacity is stored once in `AI_MATKEY_OPACITY`, with diffuse-color alpha neutralized to one, avoiding multiplication of alpha during Assimp GLB export. Arbitrary JT source/destination blend-factor combinations remain outside this initial mapping.

Regression checks cover exact sampled RGB/opacity, synthetic material flags and fractional alpha, and transform-header alignment. Local modified Tire fixtures exercise fractional alpha with blending enabled and disabled through import and GLB export. Rebuild and visual confirmation of this correction are pending with the user.

Standalone test build: configure `tests/jt` against the application's dependency prefix, optionally `-DMV_JT_SANITIZE=ON`; run `jt_tests` for portable cases or `jt_tests <sample-folder>` for the local corpus. `jt_probe <input.jt> [output.glb]` provides a console import/export diagnostic.


Current 20 October 2026 checkpoint: see [10.9 qualification](JT_109_PALETTE_VALIDATION.md); 10.9 passes the native production path and the owner-confirmed exported-assembly UI test; the original production-sample comparison remains pending.

Additional 20 October appearance check: see the [cross-version investigation](JT_CROSS_VERSION_APPEARANCE_INVESTIGATION.md). E109 has a native-tested, narrowly identified Solid Edge duplicate-material compatibility profile, with owner UI acceptance on 20 October ("That fixed"); general JT/palette ordering remains unchanged. The E95 rough oil-pan appearance is confirmed in the source reimport, and its normals/geometry are retained.

**Gate 3c1, ready for owner UI testing:** [JT 9.0 qualification](JT_90_VALIDATION.md) reuses the transitional reader and passes the authentic export and original regression cases. The owner explicitly authorized continuing implementation after accepting the colour fix; outstanding Gate 3b fixture checks are carried into the next handoff, not presumed passed.

**Current gate:** the owner accepted Gate 3c1 with "Tests passed". [JT 10.5 / Gate 3e1](JT_105_VALIDATION.md) now reuses the modern decoder and passes authentic assembly/palette data plus original endian/malformed-input checks; owner UI acceptance is pending. No 10.4 or 10.6 version is enabled by this qualification.

**Latest Gate 3f handoff:** the owner confirmed "Fixed" for the additional 10.5 material correction. [10.7/10.8 qualification](JT_107_108_VALIDATION.md) passes native authentic assembly/palette and original endian/malformed-input checks through the existing modern decoder; owner application checks remain pending. The measured duplicate-material profile also covers the subsequently observed 10.7/10.8 engine signatures; see the latest appearance follow-up in that validation report.

**Current forward gate:** the owner confirmed the reported 10.7/10.8 material issues "Fixed". [Gate 5a / JT 10.10](JT_1010_VALIDATION.md) covers the supplied palette assembly and newly supplied duplicate-material assembly through the shared modern reader. The owner confirmed both authentic 10.10 assemblies look good; sampled Gate 5a display is accepted. 10.11 is not enabled.

**Feature-first checkpoint, 21 October 2026:** all twelve enabled revisions pass the current native regression run. [C1a](JT_HUFFMAN_VALIDATION.md) bounded legacy Huffman is owner-accepted. [C1b](JT_QUANTIZED_COLOUR_VALIDATION.md) modern packed RGB/HSV colours are owner-accepted. [C1c](JT_9X_COLOUR_VALIDATION.md) adds the different 9.x colour-array transports and is owner-accepted on 21 October 2026. The [feature-completion plan](JT_FEATURE_COMPLETION_PLAN.md) supersedes earlier forward-first sequencing; 10.11 stays deferred.

**C1d acceptance, 21 October 2026:** [Topological UV arrays](JT_TOPOLOGICAL_UV_VALIDATION.md) pass native field, seam, copy/export and full twelve-version regression checks. Owner application build/UI tests passed. The qualified addition is channel-0 two-component UVs in enabled 9.x/10.x topological meshes; texture resources/shaders and other UV bindings remain later work. No JT file revision is enabled.

**ThirdParty layout, 21 October 2026:** decoder implementation and build/licence metadata live in `src/ThirdParty/occt-jt`; headers live in `include/ThirdParty/occt-jt`. The dedicated `mv_jt_codec` target remains. The main source scan excludes both JT subtrees, avoiding duplicate compilation. Manifest file paths are repository-relative across both roots; original upstream paths/hashes and installed `licenses/occt-jt` notices remain intact. This structural change follows owner acceptance of C1d and does not change decoding.
