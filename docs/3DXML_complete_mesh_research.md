# Complete sample mesh traversal and importer contract

2026-10-18, `feature/3dxml-importer`. This supersedes the former first-block
coverage limits. It resolves the geometry/traversal blockers for the owner's
supplied `ASM-GETRAG_V160.3dxml`, whose SHA-256 is
`6f95b1a5bd3e5a51db842eef3793ed492c2aa3db5a00c8b37ac33434b79b11b9`.
All findings come from supplied bytes, original Python analysis, and repository
integration inspection. No vendor software, decoder, SDK, schema, decompilation
or reference viewer was used. No application code or dependency was changed;
no application build/test commands were run.

## Readiness and precise scope

Complete mesh display of this sample can now be implemented with the file's
positions and exact triangle connectivity, using derived triangle normals.
Exact stored smooth-normal quantization remains unproven; this is a shading
fidelity limitation, not a reason to omit mesh geometry. Do not describe this
research as complete support for every 3DXML exporter/version or as recovery of
the original unquantized normal floats. Materials, analytic surface semantics
and TECHREP review data are outside the mesh-display milestone.

| Check | Result |
|---|---:|
| XML-identified UVR members | 152 |
| UVR compound catalogs and Skeleton streams fully consumed | 152 |
| Nonempty UVR members | 150 |
| Structurally mesh-free UVR members | 2 |
| Separately classified TECHREP attachment | 1 |
| Mesh leaves decoded across all UVR members | 188 |
| Position records across those mesh definitions | 2,627,779 |
| Finest-detail triangles across those mesh definitions | 4,152,382 |
| SurfacicReps blocks, fully partitioned by mesh links | 188 |
| Meshes with every exact-welded edge incidence 2 | 187 |
| Degenerate triangles (squared cross-product threshold 1e-16) | 0 |
| Candidate pool-normal triangle-corner comparisons with dot <=0 | 333 |
| Stored constant-normal/triangle disagreements | 1 |

These are definition counts, before XML occurrence reuse/visibility. The
previous 150 first blocks missed **36 further mesh leaves and 388,370
triangles**. The additional leaves are now reached by graph traversal, not
by marker guesses.

`Kugel.3DRep` and `Rotationsachse_Koppelstange.3DRep` each contain three
group nodes and zero mesh leaves. Each complete logical Skeleton consumes
141 bytes. This is stronger evidence than the previous absence of a marker;
no placeholder geometry should be invented for them. `Applications.3DRep`
is identified as TECHREP by the root XML and bypasses the UVR decoder.

`3DXML_complete_mesh_audit.json` records every member hash, all discovered
stream descriptors/extents, every node/link/mesh cursor, source IDs, all 188
geometry and normal checks, and the surface-block partition checks.

## Compound stream discovery and bounds

The prototype is `tools/3dxml/decode_representation.py`. It recognizes the
sample's `V5_CFV3\0` signature. Compound metadata uses big-endian integers,
whereas mesh floats/count escape words use little-endian values. Do not use
one global byte order for the entire member.

Observed header fields:

| Member-relative offset | Field / validated use |
|---|---|
| 8 | BE uint32 boundary at the end of the root catalog payload |
| 89 | BE uint16 maximum catalog identity |
| 91 | BE uint32 root-catalog address relative to observed origin 512 |

`rootAddress = BE32(91) + 512`. The root catalog's own length must end at
`BE32(8)`. Its identity is independently checked against the corresponding
storage descriptor. The prototype validates the resulting catalog identities
against the entire header identity range, including metadata-only storages.

A storage catalog begins with BE uint32 total bytes, identity and entry
count. Each child record contains:

```text
BE32 recordBytes
BE32 childID
BE16 kind                  // 1 storage, 2 stream in this sample
BE32 nameBytes
UTF16-BE name[nameBytes]    // includes terminating NUL
```

Require `recordBytes=14+nameBytes`, exact catalog consumption, valid even
name-byte lengths, terminated names, and unique identities.

A descriptor immediately follows a serialized length-prefixed UTF16-BE name:

| Descriptor-relative offset | Field |
|---|---|
| 0 | BE32 descriptor bytes = 34 + 20 * extentCount |
| 4 | BE32 catalog identity |
| 8 | BE16 kind |
| 10 | BE32 repeated kind |
| 14 | BE32 total logical bytes |
| 18 | BE32 reserved (observed 0) |
| 22 | 8 opaque timestamp bytes |
| 30 | BE32 extent count |
| 34 onward | Extents, five BE32 values each |

Each extent is `(physicalAddress, storedBytes, expandedBytes,
logicalAddress, flags)`. All measured extents are uncompressed:
storedBytes=expandedBytes, flags=0. Logical extents form an ordered contiguous
partition from zero through total logical bytes. Validate every physical
range against the member, reject physical overlap among streams, and assemble
the logical stream from its extents before decoding any mesh data.

Metadata-only storage records can have no catalog extent while children
are still announced by full serialized name headers. Those headers contain
BE32 parent identity, kind, logical size, reserved=0, eight timestamp bytes,
BE32 name size, UTF16-BE name, then the descriptor. The prototype resolves
missing identities from the header identity range and validates the full
parent/name/descriptor relationship. This accounts for Applicative Containers,
MainDataStream and V4V5_FDT metadata as well as the mesh-related streams.

Descriptor localization still uses bounded searches for catalog names or
missing metadata identities, followed by strict structural validation and
unique-match requirements. It does **not** search for mesh markers or use
sample-specific mesh offsets. This is a measured CFV3 subset, not a claim to
have decoded every container allocation/indexing variant. Reject compressed,
ambiguous or unfamiliar layouts instead of accepting arbitrary matches.

Example: the sensor's Skeleton has four physical extents totaling 333,613
logical bytes; its SurfacicReps has three extents. Reading the physical member
as one uninterrupted mesh stream crosses these boundaries incorrectly.

## Logical graph, caching and mesh leaves

Start one byte cursor and one empty packed-bit reservoir at the beginning of
the reconstructed Skeleton. Preserve that same reservoir across every graph
node, mesh leaf, raw attribute, style, line group and point group.

Group record `0x62`:

1. Preserve its next 35 ordinary header bytes. Its source identity is LE32
   at header offset 31. These headers include bounds/style metadata, not an
   established arbitrary transform matrix.
2. Read compact flags. The observed 0x10000 flag adds the narrowly measured
   attribute block described below.
3. Read compact child count.
4. Each child begins with an ordinary reference-mode byte. Mode 0 is an
   inline record. Mode 1 is followed by a compact cache index. A new sequential
   index defines the following inline record; a previously defined index
   references it without another payload. Preserve graph reuse, reject cycles,
   invalid indices and unfamiliar modes.

The root defines cached object 0. The M10 plug also defines cached object 1
and references it later. The reused target is a mesh-free group at logical
offset 237; treating it as another inline definition previously ran past EOF.

Mesh leaf `0x95`:

1. Remember the byte offset of the following compact span field and read it.
2. Preserve 35 ordinary header bytes, then require the observed five-byte
   layout marker `FF FF 02 00 01` at the current cursor. The marker is validated
   after reaching the leaf structurally; it is not an entry-point search.
3. Decode the position/normal counts, attributes, coarse primitives and finest
   faces using `decode_sample_meshes.decode(..., reader=sharedReader)`.
4. Read one eight-byte style per finest face.
5. Parse optional indexed tail sections. `01 01` introduces the measured
   line-group form; `01 02` introduces the measured point-group form. An absent
   section does not have a zero placeholder. Each section has compact group
   count, then groups of byte `02`, compact index count, and that many packed
   coordinate indices at this mesh's index width. All references must be <N.
   These groups consume the same bit reservoir used by triangle topology.
6. Each present tail section has an ordinary eight-byte style; observed
   prefixes are `10 24 04 FF FF` and `10 14 04 FF FF`, followed by RGB bytes.
7. At this point require `byteCursor = spanFieldOffset + spanValue`.
8. Read a compact offset into the logical SurfacicReps stream. That reference
   is outside the preceding span. Do not calculate record ends by assuming
   every compact field occupies four bytes.

The missing tail sections caused the earlier later-body failures: inheriting
only the preceding triangle endpoint's reservoir was insufficient. After
consuming the tails, every later mesh decodes without reservoir shifts,
guessed seeds, re-triangulation or inserted geometry.

All 152 Skeleton streams finish at their exact logical EOF, with any residual
packed bits equal to zero. All referenced SurfacicReps offsets form a complete
partition of that stream into 188 size-prefixed blocks. Their analytic surface
contents are not needed to render the already decoded tessellations.

## Attribute extension needed for traversal

The clutch housing's group flags include 0x10000. Its measured extension
contains a byte `01`, 15 float32 values, `01 01 01`, compact image identity,
byte `03`, compact width/height/payload bytes, `03 01`, two float32 values,
the raw image bytes, compact ASCII name length and name, a measured eight-byte
mapping prefix, 19 float32 mapping values, and `01 00` suffix. The payload is
256*256*4=262,144 bytes; dimensions and byte count reconcile exactly.

The prototype hashes/discards the pixel payload and records parameters, then
continues into the child-count field. It does not render the environment map
or claim to decode full material semantics. Unknown attribute forms must fail
explicitly rather than be skipped by a marker search.

## Normal results and supported rendering policy

All 188 leaves' normal word/tag counts reconcile. The existing dominant-axis
candidate reconstruction produces finite unit normals without missing pool
references on finest nonconstant faces. Constant face normals override pool
normals when using stored-normal shading; neither should be averaged across
that binding boundary.

Exact exporter quantization is **not proven** by these bytes. Independent
analytic checks on cylindrical sides and two spherical parts corroborate the
candidate directions: maximum angular discrepancies range from about 0.00554
to 0.01148 degrees. However, some stored coefficients differ by nearly three
integer units from the inferred analytic component times 32767, exceeding a
simple nearest-integer model. Dividing by 32768 does not establish a better
exact model. Analytic surface normals are not access to the exporter's original
unquantized vertex normals. The source-normal mapping remains a candidate,
and the original floats must not be claimed recovered.

The complete audit finds 333 opposed candidate triangle-corner normals across
ten mesh leaves; it is not 333 missing vertices or mesh failures. Their cause
is not isolated to quantization versus source normals/tessellation/binding.
The original file also has one constant face normal opposed to a triangle in
the clutch housing, independently of the candidate pool quantizer.

For the first complete-display implementation, compute normalized cross-product
normals from the exact stored triangles. This changes shading data only; it
does not reconstruct geometry, change winding or close holes. Keep source
normal words/tags available separately and expose candidate smooth shading
only as experimental until independently validated. Do not silently flip the
333 candidate corners or the one constant-normal disagreement.

If the renderer has one normal per indexed vertex, derived flat shading needs
corner splitting (or an equivalent per-face normal path). Plan memory use for
4.15 million unique-definition triangles and reuse representation resources
across XML occurrences. Avoid accidentally averaging flat normals or welding
source vertices across intended sharp/binding boundaries.

## Geometry evidence, placement and preview

187 meshes have every exact-welded edge incidence 2 and consistent directed
winding. The middle housing has six boundary edges, all other edges incidence
2. No hole filling, tolerance welding, triangulation replacement or normal
flipping was applied. Exact source topology is preserved, including anomalies.

XML placement follows `3DXML_scene_graph_spec.md`: retain native XYZ, construct
rotation basis columns explicitly, compose parent*local, share mesh resources,
and preserve path-specific inherited visibility. Millimetres remain a
sample-calibrated inference. The cached internal mesh-free node is preserved;
internal bounds headers must not be applied as invented transforms.

`3DXML_transform_fixture.json` adds a synthetic noncommuting nested-transform
case and an inverse-transpose normal case. Those mathematical checks pass in
Python; they are not additional sample evidence or C++ runtime verification.
Port the same expected values when implementing the loader.

`out/3dxml-research/complete_assembly.png` shows actual decoded triangles at XML
occurrence placements, with all visible geometry on the left and the same scene
with three housings hidden on the right. The generated preview manifest records
members/occurrence counts and drawn triangle counts. The preview uses arbitrary
research colors, derived face normals, back-face culling and approximate painter
ordering. It is an illustration rather than a reference-viewer comparison or
depth-buffer verification. No source mesh is decimated for this preview.

## Reproduction and implementation boundary

```powershell
python -I tools/3dxml/decode_representation.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --report docs/3DXML_complete_mesh_audit.json --geometry-qa
python -I tools/3dxml/analyze_normal_quantization.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --report docs/3DXML_normal_quantization_analysis.json
python -I tools/3dxml/verify_transform_conventions.py --report docs/3DXML_transform_fixture.json
python -I tools/3dxml/render_assembly_preview.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --occurrences out/3dxml-research/scene_graph.json --output-directory out/3dxml-research
```

Generate the occurrence fixture with `analyze_scene_graph.py` as documented in
the scene graph specification. Core decoding/audits use standard-library
Python; only the assembly preview requires NumPy and Pillow. The optional
`--qa-baseline` avoids repeating topology checks only when member SHA-256 and
every decoded leaf metadata field match an existing whole-geometry audit.
The final audit reused the earlier complete QA after adding independent
catalog-range, extent-overlap and surface-stream partition checks.

The C++ implementation should preserve these validation boundaries, support all
mesh leaves and zero-mesh definitions, use explicit scene ownership, and report
unsupported layouts instead of partially importing them silently. ZIP access
should stay in memory with size/count limits and exact member resolution.
Retain the distinction between geometry completeness, source-normal fidelity
and universal format/version coverage. Additional exporter samples and the
owner's runtime validation remain necessary before general-support claims.
