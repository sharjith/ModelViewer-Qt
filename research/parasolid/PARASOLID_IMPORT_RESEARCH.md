# Parasolid import research

Research date: 18 October 2026. Branch: `feature/JT-importer`. This document scopes research and future development; it does not claim a working Parasolid importer.

## Assessment

Independent Parasolid XT reading and translation to OCCT is technically credible. Siemens describes XT as open/published, and its published manual explicitly addresses translators that access models without using the Parasolid kernel. This is stronger evidence than an assertion that the format is merely understood by reverse engineers.

However, the standalone XT material inspected does not provide the explicit reader copyright permission and limited patent grant found for JT. Do not extend the JT grant to arbitrary standalone XT implementations by assumption. The present conclusion is a plausible independent-reader route with a permissions/provenance question to resolve before shipping, rather than either a guaranteed infringement-free implementation or a finding that a paid kernel license is mandatory.

## Owner-authored documentation

1. [Siemens Parasolid data access and translation](https://www.siemens.com/en-gb/products/plm-components/parasolid/data-access-translation/) describes the published XT format. Retrieved publisher HTML is retained locally for verification.
2. **Parasolid XT Format Reference, April 2008**, authored by Siemens, 137 PDF pages. The historical Siemens URL could not be retrieved as a PDF in this pass. The inspected copy is an owner-authored document hosted on a [third-party mirror](https://ww3.cad.de/foren/ubb/uploads/schulze/XT_Format_April_2008_tcm73-62642.pdf); mirror availability is not a license grant. Its introduction explains the intended independent-translator audience, while its notice reserves copyright in the publication and disclaims warranties. It does not contain a JT-style explicit implementation/patent grant. Do not redistribute the reference or copy its text/schema tables wholesale into our repository.
3. **JT 10.6 Revision D**, Annex F, documents XT B-Rep carried by JT, including serialization, schema mechanisms and geometry/topology. Use the [current Siemens download](https://static.sw.cdn.siemens.com/media/published/4Zc4NsBnmaFlEd0mtssp7f/siemens-jt-v-10-6-file-format-reference-rev-d-10-feb-2026.zip) for the embedded-JT research. Its JT-specific permissions are not automatic permission for every use of standalone XT.
4. [Open Cascade's Parasolid interface manual](https://github.com/Open-Cascade-SAS/OCCT-Components/blob/main/Exchange/Parasolid_Import/Documentation/parasolid_interface.md) documents translation into OCCT shapes and XDE, useful as an architectural reference. The actual reader is a [commercial component](https://occt3d.com/components/parasolid-import/), separate from the open-source OCCT installation.

No restricted Siemens SDK/schema distribution, decompiled kernel code, private documentation or proprietary binaries were used in this research. Publicly accessible source still requires license/provenance review before reuse. Original code alone does not establish patent clearance, and ordinary independent implementation should not be described as a formally isolated clean-room process.

## Important format distinctions

- `.x_t` is a text encoding of a typed record stream, not a convenient line-based list of named surfaces. Fields, string lengths, escapes, packed numeric representations and record layouts matter.
- `.x_b` needs its own transport decoding. Neutral binary and legacy machine-dependent bare binary must be distinguished; support for one does not establish the other.
- The common human-readable header's schema identifier can differ from the internal stream's schema identifier. Inspect both.
- Embedded schema edits describe differences from a base schema. They do not eliminate the need to know that base. A major Parasolid version match alone is not evidence of a matching record layout.
- Some records lack a general length envelope suitable for skipping an unknown layout. Reject an unknown required schema/type before losing stream alignment.
- Parsing records, mapping source B-Rep, evaluating geometry, constructing valid OCCT shapes and reconstructing a native CAD application's saved configuration are separate achievements.
- Source topology can include sheets, wires, cellular/non-manifold configurations and disconnected bodies. Never label every parsed body a closed solid.

## Source candidates inspected

| Candidate | Declared licensing / snapshot | Assessment |
| --- | --- | --- |
| [monozukuri-ai/parasolid-kit](https://github.com/monozukuri-ai/parasolid-kit) | Aggregate `MIT AND Apache-2.0`; main-tree snapshot `2bc959687c8ca87145c4e0594828c3fa11aab912` | Most relevant bounded parsing/OCCT research candidate found in this pass. Rust core with optional Python OCCT adapter; not a C++ drop-in or a complete Parasolid solution. |
| [omerbasavul/cadconvert](https://github.com/omerbasavul/cadconvert) | Root declares MIT; snapshot `6046d1369949a09fa236f6bcf5f804eedb328685` | Rust/C ABI mesh-conversion candidate. Does not by itself establish native OCCT B-Rep support. README also describes bundled SolidWorks material/appearance data, requiring a separate content-provenance audit; do not adopt the whole distribution by default. |
| OCCT Parasolid import component | Commercial | Existing exact-translation alternative if broad production coverage becomes more important than an independent implementation. Evaluation/redistribution terms and GPL compatibility would need review. |

For parasolid-kit, inspect its [support matrix](https://github.com/monozukuri-ai/parasolid-kit/blob/main/docs/format-support.md), [profile provenance](https://github.com/monozukuri-ai/parasolid-kit/blob/main/docs/builtin-profiles.md) and [adopted-reader notices](https://github.com/monozukuri-ai/parasolid-kit/blob/main/crates/parasolid-core/PARTIAL_READERS.md), rather than relying on GitHub's root MIT badge. The author explicitly separates exact schema profiles, raw decoding, source B-Rep mapping, numerical geometry and bounded OCCT conversion. Some conversion/metric cases remain unsupported or fail validation. Real-model validation is partly private. These are author-reported boundaries, not ModelViewer test results.

MIT and Apache-2.0 code can be candidates for this GPLv3 project, with their notices and other obligations preserved. Neither upstream license proves ownership of every bundled asset or clears third-party format patents. No source from these candidates has been incorporated or executed as part of this research.

## Local sample evidence

The bounded header inventory is recorded in out/jt-research/JT_PARASOLID_SAMPLE_INVENTORY.json (`out/jt-research/JT_PARASOLID_SAMPLE_INVENTORY.json`, local artifact). Two existing data trees were searched: `D:/work/progs/qt5/ModelViewer-Github/data` and `D:/work/progs/cad-in-unity`. No JT files were found there. Nine `.x_t` files were found in the former; seven contain data and two are empty. Nonempty sizes range from approximately 7.5 to 123 MB, so these are later stress cases, not ideal first fixtures.

| Model | Common-header key | Internal-stream key |
| --- | --- | --- |
| B4T-6.5 RC | `SCH_2500176_25001` | `SCH_2500176_25001_13006` |
| B4T-25.0-FB | `SCH_2500248_25001` | `SCH_2500248_25001_13006` |
| Honda CBR engine | `SCH_3100203_31001` | `SCH_3100203_31001_13006` |
| V-twin engine | `SCH_2800204_28002` | `SCH_2800204_28002_13006` |
| Kuka arm | `SCH_3101284_31100` | `SCH_3101284_31100_13006` |
| Volvo truck | `SCH_2400216_20000` | `SCH_900000_9008` |
| Yamaha XS650 | `SCH_3000298_30000` | `SCH_3000298_30000_13006` |

The Volvo discrepancy is a header observation, not proof of corruption or a valid complete stream. None of these seven internal keys appears in parasolid-kit's advertised exact-profile list at the inspected snapshot. This is not a runtime rejection result and does not prove that adding a qualified profile would be impossible. No complete node stream or geometry was decoded. Research read only the first 65,536 bytes of each file and interpreted the observed length-prefixed internal strings; no source files were changed.

Use local downloaded models for investigation without assuming permission to redistribute them as public test fixtures. Obtain or author small fixtures with known reference geometry and appropriate reuse rights for checked-in tests.

## OCCT reconstruction scope

Proposed order: decode transport and effective schema, resolve a typed record graph, map analytic/NURBS geometry, then build shared topology and an XDE assembly. Preserve source entity IDs throughout.

Key translation responsibilities:

- Analytic coordinate systems, surface/curve senses, parameter ranges and units.
- Rational/homogeneous B-spline coefficients, knot multiplicities, trimming, periodicity and surface extensions. A direct knot-vector copy is insufficient.
- Fins as oriented edge uses, ordered face loops, shared edges/vertices, inner loops, seam and degenerate edges, shell/region orientation and voids.
- Consistent three-dimensional curves and face-local parameter curves. An intersection chart/polyline cache is not automatically an exact curve.
- Foreign, procedural, blend, offset, swept and spun geometry. Admit only qualified mappings, with explicit unsupported diagnostics for the rest.
- Transformation types that OCCT locations cannot directly represent, including nonuniform/mirrored cases where applicable. Do not silently coerce arbitrary matrices into rigid placements.
- Tolerance accounting and validation. Preserve sharing before considering sewing. Sewing/healing cannot guarantee correct topology or recover missing geometry; record any approximation or tolerance increase.

For accepted exact results, use `BRepCheck_Analyzer`, appropriate curve-on-surface consistency checks and independent bounds/area/volume comparisons. A model that merely looks correct is not enough. Export/reimport through STEP is a useful additional check, not an independent proof if both stages share OCCT behavior.

Our existing XCAF pipeline currently closes the document and returns `aiScene`. Therefore, adding a B-Rep reader alone would not retain exact source shapes for future operations. Exact-source retention and source-ID/face mapping require an explicit owned result sidecar if that becomes part of the deliverable.

## Recommendation

Proceed with JT visualization first. Conduct a separate bounded XT feasibility spike using original code or audited compatible portions of existing projects. Select one documented/observed schema and small analytic solids first, then NURBS and difficult trimming. Establish standalone permissions and schema provenance before shipping or advertising general Parasolid support. Share XT semantics between standalone and embedded inputs only where their transport/schema rules actually match.

A full writer, feature-history recovery, general non-manifold translation, procedural-surface equivalence and arbitrary-version compatibility are outside the initial scope. See [JT_PARASOLID_IMPLEMENTATION_SCOPE.md](../jt/JT_PARASOLID_IMPLEMENTATION_SCOPE.md) for milestones and integration boundaries.

## Provenance

The XT PDF and extracted text are retained only in ignored `out/jt-research/`. PDF SHA-256: `6a1c3b48aac8577dac96fda4a24b2aca460ff8897fa04915487a93af9ed7792a`.

One attempted legacy JT 9.5 download returned an HTML page despite its `.pdf` URL. It is not treated as a valid reference or used to establish layout compatibility. Current JT technical findings use the verified 10.6 Revision D PDF.
