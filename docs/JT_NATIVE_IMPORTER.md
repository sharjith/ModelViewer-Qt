# Native JT importer: initial implementation

Work on `feature/JT-importer`, 18 October 2026. Uncommitted implementation; exact Parasolid conversion is not implemented.

## Follow-up: assembly and attribute work (19 October 2026)

The subsequent [Switch and RangeLOD pass](JT_DISPLAY_SELECTION_VALIDATION.md) adds authored Switch choices and optional distance-based import selection, with transactional display fallback. The viewer retains static first-child RangeLOD selection unless an API caller supplies an eye point; runtime LOD switching is not implemented.

See [assembly/attribute validation](JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md) for the current implementation and test matrix. It adds field-aware material and transform inheritance, resource sharing, nested external-partition tests and transactional representation fallback. The historical initial-scope descriptions below should be read with that follow-up. Version acceptance remains JT 10.0; no XT conversion has been added.

## Current behavior

`Jt::Importer` in `include/Import/Jt/JtImporter.h` and `src/Import/Jt/JtImporter.cpp` produces an owned Assimp scene. The normal open/import dispatch is XCAF, 3DXML, JT, then Assimp. JT is added to the file dialog. The existing loader builds render meshes and the scene tree from this scene and counts mesh occurrences for progress. JT cancellation uses the native-import rollback path.

The native decoder accepts JT 10.0 only. It handles the sampled LZMA/XZ segments, 64-bit TOC offsets, logical scene graph and TopoMesh tessellation, including mark-3 integer packets, binary coordinates and packed Deering normals. Only one usable display LOD is selected, trying children in source order. An ordinary JT LOD list does not imply a highest-detail order. All three LODs in each downloaded part were separately decoded during development.

Scene construction retains local transforms, group/instance nodes, names, inherited diffuse appearance and hidden state. Geometry can be shared across occurrences with the same appearance. External partitions are restricted to the assembly folder and its descendants and resolved relative to the referring file. Synthetic and local-part-backed assembly fixtures now exercise these paths; diverse real assemblies and independent comparison remain required for broader qualification. Per-face material palettes, PMI, textures and universal JT version coverage are outside this initial implementation.

## Exact geometry and fallback

The product registers no XT backend. Parts with B-Rep references use their stored display tessellation and emit a diagnostic. A part with no usable supported display tessellation fails explicitly.

`Options::exactDecoder` is an optional future integration seam. Complete neutral-binary XT payload extraction is exercised on the five samples; subordinate/MultiXT body resolution is unsupported. A callback result must pass OCCT B-Rep validation and triangulation. Exceptions or invalid shapes select stored tessellation instead. Successful exact output replaces the display alternative, avoiding duplicate meshes. Cancellation and allocation failures propagate.

The accepted-backend test injects an OCCT box to test selection and ownership only. It is not an XT translation test or evidence of geometric agreement. An eventual real backend also needs source-unit, tolerance and geometry-agreement checks before activation.

## Local corpus and provenance

Samples: `out/jt-research/samples/{Bolt,Brake_Disk,Caliper,Rim,Tire}.jt`.

Downloaded from NVIDIA's [VFI workflow dataset](https://developer.download.nvidia.com/USD/dataset/vfi-guide/vfi_workflow_data.zip), linked by NVIDIA's [VFI samples repository](https://github.com/NVIDIA-Omniverse/vfi-samples). Original archive: `out/jt-research/nvidia-vfi.zip`. The corpus stays outside version control; no redistribution permission is inferred from availability. These are five related part files, not a diverse assembly corpus.

Code provenance and dependencies: [vendored decoder notice](../third_party/occt-jt/README.md). The selected source is the official Open CASCADE repository, not the earlier mirror candidate. Published JT permissions and their limitations remain in [feasibility research](JT_IMPORT_FEASIBILITY.md).

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
