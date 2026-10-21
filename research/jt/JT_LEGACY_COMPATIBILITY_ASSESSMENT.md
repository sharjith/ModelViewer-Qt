# JT legacy compatibility assessment: 6.4, 7.0 and 8.x

19 October 2026, `feature/JT-importer`. The owner authorizes a staged JT **6.4–10.11** visualization target, with **7.0 as the lower cap if 6.4 requires a substantial viewer data-model change**. This assessment does not enable additional versions. Current native-tested subsets comprise lossless/qualified-quantized 8.0–8.2, 10.0 and 10.3. Gate 0 is accepted. The [quantized follow-up](JT_LEGACY_QUANTIZED_VALIDATION.md) now imports the 8.2 sample Q82 and has passed Gate 1 UI acceptance.

## Historical format boundaries

The official [Siemens JT Open Program technology fact sheet](https://www.plm.automation.siemens.com/en_us/Images/Siemens-PLM-Components-JT-Open-Program-Technology-fs_tcm1023-121669.pdf), page 3, describes 6.4 as containing scene graphs, attributes, properties and part/metadata nodes. It identifies 7.0 with zlib-based size reduction and 8.0 with a major restructuring of the format, more modular organization and additional compression choices. The same history identifies 9.0 with TopoMesh/face-group geometry coding. This is historical architecture evidence, not a byte-layout specification.

Consequently, 8.1 is not inherently the oldest renderable JT version, but neither 7.0 nor 6.4 can be declared compatible with our current 8.1 reader. The substantial source-format boundary may be **7.x to 8.x**, rather than 6.4 to 7.0. Capping at 7.0 would not necessarily remove the need for pre-8 container/record work.

## Audited code evidence and gaps

Inspection of the current audited decoder subset finds:

- `JtElement_ShapeLOD_Vertex::readVertexShapeLODData` distinguishes pre-8 and 8.x records: it reads the additional local-version field only for major versions above 7. This is an inherited compatibility clue, not a working pre-8 importer.
- `JtElement_ShapeLOD_Base` omits the newer base-LOD collection for major versions at or below 8. Shape-node bindings also have a pre-9 branch.
- `JtData_Model` currently uses the same 32-bit directory path for every major version below 10. That has been exercised for 8.1, not established as correct for 6.4 or 7.0. Header, segmentation, directory, element IDs and compression envelopes all need their own evidence.
- The adapted lossless vertex reader was deliberately qualified for 8.1. Integer-codec and local-version guards currently contain major-version-8 conditions; simply opening the file-version gate for 6/7 could expose unqualified upstream branches and bypass these restrictions. Generalize guards only after explicit family qualification.
- Legacy quantized arrays, additional bindings/codecs, Switch and draw-style handling remain incomplete. They must not become implicitly accepted through a lower version number.

The inherited source therefore provides reusable components but not proof of support for a complete older file. Preserve existing notices and validation limits when adapting it.

## Data-model decision gate

Assess whether positions, indices, normals, appearance groups, occurrence hierarchy, transforms and names can map faithfully into the current owned `aiScene` output. Separate source-version records from the viewer's data model.

**Acceptable adapter work:** a new header/directory reader, decompression envelope, source-record structs, strip/primitive expansion, quantization or integer-codec decoding, binding conversion, normal generation where absent, and material-group splits. These alone do not trigger the owner's 7.0 cap.

**Trigger the cap:** demonstrated 6.4-specific semantics essential to the agreed mesh/assembly display that require a substantial replacement or incompatible extension of the viewer scene/resource model, while 7.0 can meet that same contract without it. Record the concrete file/reference, lost semantics, required model change and comparison with 7.0. A missing reference, missing sample, or difficult codec is not evidence of this condition; record those as unresolved qualification limits.

Current evidence does **not** establish that 6.4 requires such a change. Retain 6.4 as the conditional target. Do not promise universal geometry coverage or exact JtBrep/XT conversion as part of that target. Stored tessellation remains the rendering fallback, and files without a qualified display representation must fail explicitly.

## Corpus and implementation sequence

A header-only inventory of the existing application data directory and `out/jt-research/samples` finds versions 8.0, 8.1, 8.2, 10.0, 10.3 and 10.9. The 8.0 sample L80 sample appeared during this research pass and now exercises the shared lossless path. No authentic 6.4 or 7.0 sample was found there. This inventory establishes headers only, not import success. Downloaded CAD data stays outside the repository until redistribution permission is established.

1. Obtain lawful matching layout evidence and authentic samples for 7.0 and 6.4; expand 8.x sample diversity. Record source, header/writer versions, hashes, rights and independent preview/reference evidence. The supplied 8.2 case identifies quantized-geometry work shared across 8.x.
2. Build a compatibility table keyed by container family, element GUID/local version, compression, integer/geometry encoding and attribute bindings. Reuse code across actual compatible layouts rather than branching for every minor version.
3. Retain the qualified lossless 8.0–8.2 family and extend its quantized encoding/binding coverage. Then establish 7.0 container/graph/tessellation adapters and compare 6.4, applying the decision gate above.
4. Add small original fixtures based on established layouts, including an assembly with repeated geometry, nonidentity transforms and differing appearances. Test bounds, indices, normals, sharing, malformed/truncated input, resource budgets and cancellation. Synthetic headers alone are not real-version qualification.
5. Validate real-file geometry and owner UI behavior before enabling each layout family. Retain all 8.1/10.0/10.3 regressions. Update the compatibility matrix and About when support changes; do not advertise the target range as implemented.

While older references/samples are being acquired, the documented 10.9 attribute/palette work can proceed independently. Keep exact Parasolid conversion separate. See [compatibility policy](JT_VERSION_COMPATIBILITY.md), [implementation scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md) and [permissions/provenance research](JT_IMPORT_FEASIBILITY.md).
