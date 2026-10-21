# Additional 3DXML layouts: 2026-10-18

> Consolidated provenance, dated 19 October 2026: [3DXML_PROVENANCE.md](3DXML_PROVENANCE.md). Early source restrictions describe their individual research phases, not the complete branch history.

The first native importer was validated against GETRAG only. The owner's next
four archives exposed independent format variants; their failures were not bad
files. All five supplied archives now pass native import and an independent
source-triangle audit with visible geometry attached to assembly nodes. New-file
appearance still needs the owner's GUI check.

## XML tessellation

`TESSELLATED` is an XML representation rather than a CFV3 binary stream.
`TessellatedReader` reads `BagRepType` / `PolygonalRepType`, comma-separated XYZ
triples in `VertexBuffer/Positions` and `Normals`, and `Face` triangle lists,
strips and fans. Strips alternate winding. `PolygonalLOD` children are excluded
in favor of the finest faces. Each polygonal representation becomes a body;
assembly names, hierarchy, visibility and transforms continue to come from the
product XML. No Unity coordinate or winding conversion is applied.

Colors inherit through representation / face containers, with local
`SurfaceAttributes/Color` overriding their parent. RGB floats are rounded to
8-bit channels for the existing palette. Alpha, texture and complete source
material behavior remain outside this pass. Missing normal buffers use derived
flat normals; partially populated buffers are rejected. Explicit source normals
are retained. XML DTD/entities, malformed vectors and invalid indices fail.

The NIST archive has 492 polygonal bodies and 61,317 source triangles. Exactly
527 collapse to zero area at the importer's float precision; those are omitted
with a diagnostic, leaving 60,790 renderable triangles. This is an exact zero
area check, not a small-triangle tolerance or geometry-repair operation.

## Binary variants

- **Root catalog location:** the original `header[91] + 512` bias does not hold
  for four press representations. Resolve root storage ID 2 through its
  validated descriptor instead. Its catalog size/identity and physical endpoint
  must agree with the independent header endpoint. All other catalog, extent,
  identity, bounds and overlap checks remain in place. No mesh-marker recovery
  or partial geometry fallback is used.
- **Extended group `0xa3`:** count field, ordinary 35-byte group header,
  flags/attributes, child links, then one float. In all 16 supplied occurrences
  the float is exactly 1.0 and `end = count_field_start + span + 1`. Both checks
  are enforced. The scalar's general semantics are unproven; non-unit values
  are rejected rather than guessed. Children retain ordinary cache behavior.
- **Material without environment image:** graph attribute prefix `00 01`,
  fifteen finite floats, `00 00 01 01 05 00 01 01 00`, nineteen finite floats,
  suffix `01 00`. This structurally skips the measured material form; per-face
  colors still supply the render palette.
- **Cache IDs:** graph definitions can start at ID 1, so IDs are not required to
  equal the number of graph entries seen. Existing IDs remain references and
  active references are rejected as cycles. Recursion and expansion limits stay.
- **Additional face style:** `b0 04 04 ff 00` is followed by the same BGR triplet
  as the two original measured styles. RGB is preserved. The differing style
  byte's complete opacity/material semantics have not been established.
- **Index width:** use `bit_length(position_count)`, not
  `bit_length(position_count - 1)`. The 128-position bush demonstrates an 8-bit
  stream. The wrong width leaves four bytes before the next primitive and fails
  full-leaf consumption; the corrected width consumes the entire Skeleton and
  linked surface stream. GETRAG's golden comparison remains unchanged.
- **Annotation-only container:** `PressRinkAxe_3DAnnotations.3DRep` has a
  `V4V5_FDT` storage but no Skeleton or SurfacicReps. Its validated container is
  accepted as non-mesh data. Annotation rendering is not implemented. A missing
  Skeleton with surface data or without that annotation storage still fails.

## Reproducible checks

`tools/3dxml/verify_import_variants.py` independently reads XML tessellation or
uses the Python research decoder to consume complete UVR graphs and surface
partitions. It compares renderable resource-triangle counts against native
`threedxml_tests --audit ARCHIVE REPORT.json`. The native audit checks triangle
indices, finite positions/normals, mesh occurrences and world bounds. Only
counts, hashes and bounds are checked in; no supplied geometry is redistributed.

| Archive | Unique mesh/material resources | Renderable resource triangles |
|---|---:|---:|
| GETRAG | 388 | 4,152,382 |
| `_fbc_001_dmg00005563053_dmg00005563053` | 9 | 58,996 |
| `_fbc_001_dmg00005921053_dmg00005921053` | 7 | 3,370 |
| `nist-ctc-02-part` | 492 | 60,790 |
| `press_rink_axe_fta` | 17 | 46,168 |

See `3DXML_variant_validation.json` for source hashes, occurrence counts and
bounds. The original GETRAG audit additionally compares every visible primitive
position exactly, normals within 1e-6, colors and all product placements against
the owner-accepted GLB. Synthetic tests cover XML winding, LOD exclusion,
inherited/face colors, malformed indices, normal counts and cancellation.
Synthetic CFV3 fixtures also cover descriptor-resolved catalogs, the extended
group (including span/scalar rejection), sparse cache IDs and image-free
material attributes.

Example (put runtime dependency DLLs on PATH where required):

```text
python tools/3dxml/verify_import_variants.py <threedxml_tests.exe> <sample-directory> --report <report.json>
```

## Reference provenance and limits

The owner supplied the local `cad-in-unity/CAD2Unity/3DXMLLoader` source as a
reference. Its root LICENSE is Apache-2.0. The inspected checkout is
[`i2e-haw-hamburg/cad-in-unity`, commit 43a9f0582ecafcdae5bb6f5919c0191467465f86](https://github.com/i2e-haw-hamburg/cad-in-unity/tree/43a9f0582ecafcdae5bb6f5919c0191467465f86).
`ParseReferenceRepUsecase.cs` confirms the XML tessellation concepts and finest
LOD selection; that parser supports TESSELLATED, not UVR. The new C++ reader is
an independent implementation based on the supplied XML bytes. No C# code was
copied, and Unity's handedness adjustments were not adopted. Binary additions
come from structural measurements of the owner's supplied archives. No vendor
SDK, executable disassembly or protection bypass was used.

These checks extend measured sample coverage, not a claim of universal 3DXML
support. Unknown binary versions, compressed CFV3 extents, non-unit extended
group scalars, other material layouts, B-Rep, animation and annotations remain
explicitly outside the decoded rendering subset.

The Release ModelViewer application and integrated test target build successfully.
Integrated 3DXML CTest, standalone synthetic tests and existing MVF tests pass.
The final GETRAG primitive/scene comparison also passes after these extensions.
