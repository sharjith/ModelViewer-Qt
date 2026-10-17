# 3DXML research continuation and importer plan

> **Current result:** Connectivity is now decoded for the recognized layouts.
> See [the mesh decoding specification](3DXML_mesh_decoding_spec.md). A shared
> packed-bit reservoir explains the apparent variable-length index streams.
> Pin/ring meshes pass closed-surface checks; 140 members pass reference/count
> validation. The earlier research and rejected assumptions below are preserved
> as history, not the current decoder specification.

Date: 2026-10-17. Research only; no application importer has been added.

## Scope and provenance

This continues `3DXML_decoding_handout.md` using only the supplied archive's
bytes and this project's existing import integration. No vendor software,
SDKs, schemas, disassembly, third-party decoder implementations, or vendor
viewer output were consulted. No encryption, access control, or certificate
verification was bypassed. The integrity-certificate member was not used.
This describes the research method, not a legal determination.

The user's current request to continue supersedes the older handout's pause
until October 19. Its other boundaries remain: exact stored triangles, no
reconstructed substitute surfaces, no application build/test commands, and
no commits before owner validation. Analysis scripts on sample data are allowed.

Source archive:
`D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml`

SHA-256: `6f95b1a5bd3e5a51db842eef3793ed492c2aa3db5a00c8b37ac33434b79b11b9`.

Reproduce the measurements from the repository root:

```powershell
python -I tools/3dxml/analyze_3dxml.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --output docs/3DXML_sample_analysis.json
```

The script uses only the Python standard library, reads members in memory,
does not extract archive paths, and writes measurements without geometry.
The JSON records member hashes, offsets, histograms, and rejected hypotheses.
It is a discovery tool, **not a hardened file parser or production decoder**.
Its seven-byte prefix interpretation and marker search remain heuristic.

## Measured coverage

The ZIP has 157 entries and 153 `.3DRep` members. The root XML has:

| Record | Count |
|---|---:|
| Reference3D | 173 |
| Instance3D | 251 |
| ReferenceRep | 153 |
| InstanceRep | 153 |
| RelativeMatrix | 251 |

All matrices contain twelve numbers; 121 differ from identity. All 153 local
`associatedFile` references resolve to archive entries. Reused references mean
the number of assembly occurrences must not be inferred from the number of ZIP
members. Identity transforms alone cannot establish matrix orientation.

The first position/attribute layout can now be accounted for in **140 of 153
members**, covering 1,440,769 candidate vertex records:

- 137 use the two-bit coordinate-control layout.
- 3 store coordinate triples directly.
- 13 are unrecognized by this discovery tool. This does not classify them as
  empty or unsupported by the format, and coverage is not an importer success
  rate. The script only searches the first 4096 bytes and returns one layout
  per member; multiple bodies are not enumerated.

The unrecognized names are in the JSON. They include the three large housing
parts, spring variants, `Speed_Sensor`, and the two very small representations
previously described as empty. Several have unequal adjacent counts; those
cases are intentionally excluded rather than forced into this layout.

## Corrected count and coordinate layout

### Count encoding

At the fields examined, a byte below `FF` is the count itself; `FF` escapes to
a following little-endian uint32. This is an observed convention in these
fields, not a claim that every `FF` in the compound file begins an array.
Escaped counts below 255 also occur elsewhere in the handout, so a production
reader must not require the shortest encoding.

This explains why the earlier search for `FF + u32` missed the two small pins:
`Stift_Schaltwellenmitnahme` has adjacent counts `ED ED` (237), and its `_94`
variant has `F1 F1` (241).

The recognized layouts occur immediately after `FF FF 02 00 01`. Following
two equal counts N are **seven bytes whose full meaning is unknown**, then
the coordinate representation. The marker is a discovery anchor, not a
replacement for decoding the compound-file directory.

### Two-bit control stream

In the compressed cases, the payload after that seven-byte prefix contains
`ceil(N / 4)` bytes. Read two-bit symbols from the least-significant pair
first and truncate to N symbols. A compact count F and F little-endian
float32 values follow.

| Symbol | Stored float32 values consumed |
|---|---:|
| 0 | 3 |
| 1 | 0 |
| 2 | 1 |
| 3 | 2 |

Equivalently, `consume = (symbol + 3) mod 4`.

The consumption sum equals F exactly in all 137 recognized compressed
members. This establishes a storage relationship across this sample, not
general support for every 3DXML exporter or version.

For the pin and ring, a useful provisional decoder initializes three
components to zero, retains the first `3 - consume` components of the previous
tuple, and replaces the remaining components from the float pool. The
serialized component order behaves as `(z,x,y)` on these two targets. Global
axis permutation, initialization, and header-dependent choices still need
confirmation; this order must not yet be hard-coded for every representation.

### Precise offsets and corrected histograms

All offsets below are relative to the uncompressed member.

| Field | Dowel pin | Spacer ring |
|---|---:|---:|
| First count field | `0x30D` | `0x31A` |
| N | 268 | 729 |
| Seven-byte prefix | `00 00 00 24 18 78 F0` | `00 00 00 18 50 C0 00` |
| Actual two-bit payload start | `0x31E` | `0x32B` |
| Float count field | `0x361` | `0x3E2` |
| F | 260 | 970 |
| Symbols 0 / 1 / 2 / 3 | 18 / 126 / 42 / 82 | 14 / 251 / 0 / 464 |

Thus:

```text
pin:  18*3 + 126*0 + 42*1 + 82*2 = 260
ring: 14*3 + 251*0 +  0*1 + 464*2 = 970
```

The ring's last symbol byte is `03`: its low pair is the final symbol, and
the remaining six bits are padding. The handout's ring histogram included
bytes preceding the actual symbol payload and assumed the other bit order.

### Geometry checks and a withdrawn interpretation

The provisional prefix decoder produces:

| Measurement | Dowel pin | Spacer ring |
|---|---|---|
| Candidate vertex records | 268 | 729 |
| Distinct coordinate tuples | 140 | 472 |
| Axial levels | 0, 1, 24, 25 | 515.2999878, 519.2999878 |
| Radii, rounded to 4 decimals | 2.2321, 2.5 | 22.5, 34.0 |

Every decoded pin point lies on one of the two radii. The supposed interior
end-cap points in the handout arose from splitting floats into XY pairs
without honoring one-component updates. Withdraw that interpretation; do
not use it to constrain cap triangulation. Repeated coordinate tuples may
carry distinct attributes, so do not merge them during research.

### Raw triples

Three additional members have seven-byte prefix byte 3 with its low bit set,
and exactly N triples of float32 immediately after the prefix, followed by
the same attribute layout described below:

| Member | N | First count | Attribute count field |
|---|---:|---:|---:|
| Vorspannfeder_Pilotlager | 32913 | `0x31A` | `0x609F7` |
| Druckfeder_Gassenwahl | 30766 | `0x31A` | `0x5A553` |
| Rueckstellfeder | 29652 | `0x2EA` | `0x570EB` |

Their prefix byte-3 values are `0F`, `0D`, and `3D`; the recognized compressed
cases have that bit clear. Treat this as a sample-supported mode-selector
hypothesis. Raw-mode float count is implied as `3*N`; there is no intervening
float-count field at the location used in compressed mode.

## Adjacent attribute layout: strong size evidence, incomplete semantics

Immediately after the position floats:

1. Read a compact count W, then W little-endian 16-bit words.
2. Read `ceil(N / 2)` bytes of four-bit tags, low nibble first, truncating to N.
3. Nine more bytes precede the next candidate count field.

All 140 recognized members use tags 0 through 11 only, and satisfy exactly:

```text
W = 2 * number_of_tags_in_range_0_through_5
```

Tags 6 through 11 consume no words under this rule. This is much stronger
evidence than treating the words as big-endian angle codes. For example,
the pin byte pairs `E1 DE` and `1F 21` are signed little-endian integers
`-8479` and `+8479`: an ordinary signed pair.

| Field | Dowel pin | Spacer ring |
|---|---:|---:|
| Word-count field | `0x776` | `0x130F` |
| W | 440 | 978 |
| Tag payload start | `0xAEB` | `0x1AB8` |
| Nine-byte section start | `0xB71` | `0x1C25` |
| Next candidate count | `0xB7A` | `0x1C2E` |

All 140 nine-byte sections begin `0B D7 23 3C`, the float32 encoding of
approximately 0.01. Its purpose is unknown; do not name it a tolerance yet.

A plausible normal interpretation uses two signed components for tags 0..5
and cardinal normals for tags 6..11. Candidate axis pairs in serialized
order are Z, X, Y; odd tags suggest negative signs. However, this is
**not an exact normal decoder**:

- On the ring, a simple signed-component scale of 32767 leaves 17 of 489
  noncardinal records with component error above 0.001, maximum about 0.662.
- Four candidate cap-normal signs disagree with the candidate axial level.
- Error indices are retained in the JSON so subsequent work must explain
  them, not silently normalize, smooth, or discard them.

Possible causes include attribute ordering, quantization rules, vertex-update
semantics, or stream context. Progressive refinement is a hypothesis, not
an explanation established by these observations.

## Connectivity: still the release blocker

No exact triangle/index stream has been decoded. Analytic circles and
cylinders cannot substitute for the stored tessellation.

### Continuation: primitive headers and separate detail levels

The next research pass identified substantially more structure. Reproduce it:

```powershell
python -I tools/3dxml/probe_topology.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --output docs/3DXML_topology_analysis.json
```

This second script deliberately uses explicit measured offsets for the two
targets. It does not discover arbitrary members or output renderable indices.

An observed envelope byte `01` precedes a flags byte in the range `41..4F`.
The following interpretation reconciles all 24 measured primitive blocks
across the two targets:

| Flags bit | Interpretation supported by these blocks |
|---|---|
| `01` | A compact count T of independent triangles is present |
| `02` | A compact count S of triangle strips is present |
| `04` | A compact count F of triangle fans is present |
| `08` | A constant float32 normal vector occurs in the payload |
| `40` | Always set here; meaning not established |

Counts are read in the order T, S, F for the enabled bits. A compact count I
follows these primitive counts. When bit `08` is set, three little-endian
float32 normal components occur immediately after I, before the opaque
index data. Strip and fan lengths still need to be located and decoded.

For a block, the candidate number of triangles before checking degeneracy is:

```text
triangles = I - 2*(T + S + F)
strip_and_fan_index_budget = I - 3*T
```

This follows if I is the total vertex-reference count: a standalone triangle
uses three references, and each strip/fan yields two fewer triangles than
its reference count. It is supported by both whole-mesh checks below; it
does not establish which vertex each reference selects.

The four pin coarse blocks yield 138, 68, 34, 16 triangles. The six ring
coarse blocks yield 472, 236, 118, 58, 30, 12. They are followed by separate
finest-detail face blocks. The bytes previously described as one fixed
"nine-byte metadata section" include the first primitive header and its
counts: they are not a universal nine-byte record definition. The byte
after the approximately-0.01 float is 4 for the pin and 6 for the ring,
matching their numbers of coarse blocks. Other member variants need their
own accounting before this field is generalized.

The finest ring blocks reconcile as follows:

| I count offset | Flags | T | S | F | I | Candidate triangles |
|---|---|---:|---:|---:|---:|---:|
| `0x24BE` | `42` | 0 | 2 | 0 | 110 | 106 |
| `0x2552` | `42` | 0 | 2 | 0 | 110 | 106 |
| `0x25E6` | `42` | 0 | 1 | 0 | 132 | 130 |
| `0x2692` | `42` | 0 | 1 | 0 | 132 | 130 |
| `0x2744` | `4F` | 4 | 24 | 9 | 310 | 236 |
| `0x2906` | `4F` | 4 | 25 | 9 | 312 | 236 |

Total: **944 triangles**. This equals `2*472` for the candidate ring's 472
distinct coordinate tuples, consistent with a closed triangulated surface
of genus one. The 106/130/236 counts also match the inner-cylinder,
outer-cylinder, and annular-cap counts respectively under the candidate
106-inner/130-outer samples per axial level. This is an independent numeric
cross-check, not an instruction to reconstruct those surfaces.

The eight finest pin blocks yield 35, 35, 32, 35, 35, 32, 36, 36 triangles:
**276 total**, equal to `2*140 - 4` for its candidate 140 distinct positions,
consistent with a closed triangulated surface of genus zero. Actual winding,
degenerate triangles, manifold connectivity, and use of every point remain
unverified until the integer references are decoded.

The two ring cap blocks contain exact stored constant vectors `(-1,0,0)`
and `(1,-0,-0)` in serialized order. The two pin cap blocks contain
`(1,0,0)` and `(-1,-0,-0)`. Under the provisional `(z,x,y)` component order,
these are the expected axial normals. This resolves the constant-normal
payload location on those faces; it does not resolve the global per-vertex
normal quantization or earlier mismatches.

### Opaque integer payload boundaries and rejected simple packing

All **22 bounded index payloads** measured before a subsequent known envelope
have byte lengths divisible by four. For example:

- Ring coarse payloads: 1044, 480, 272, 140, 84, 44 bytes.
- Pin coarse payloads: 200, 124, 92, 56 bytes.
- Pin triangle-only face blocks with I=105 have alternating payload lengths
  of 116 and 120 bytes. A single fixed nine-bit value per reference would
  require at least 119 bytes, so it cannot explain every one of them.

The probe checks fixed widths 1..16, 0..7 leading bits, byte and 32-bit-word
bit-order variants, and up to 31 zero padding bits. It tries both I values
and I+S+F values (allowing one length per strip/fan). No such simple model
passes the pin blocks; one ring coarse block admits isolated fits, but
there is no common decoder. The final face payload end in each target is
left unresolved rather than chosen from an arbitrary FF byte.

The immediate decoding task is now the **word-oriented integer codec and
strip/fan lengths**, starting with the pure triangle-only pin blocks to
avoid mixing primitive-length coding with index coding. Require one rule
to explain both 116- and 120-byte I=105 blocks, then the coarse levels and
ring. Do not flatten all these arrays into one mesh or combine LODs.

The handout's suggested Huffman interpretation is unproven. As a useful
alternative probe, the ring bytes at `0x1C33`:

```text
03 12 38 E0 00 03 0C 30 C0 00
```

read as little-endian packed 10-bit integers:

```text
515, 516, 515, 3, 3, 3, 3, 3
```

In particular, the recurring five-byte record `03 0C 30 C0 00` is exactly four
packed 10-bit values of 3. This does not establish the stream's actual coding,
but it undermines using the apparent five-byte rhythm as evidence of a
Huffman table. Investigate fixed/variable-width integers and signed deltas
alongside entropy coding. Do not choose boundaries by scanning for FF inside
payloads: such bytes can be data.

## Next decoding work, in order

1. Decode the compound-file directory sufficiently to identify stream spans,
   rather than assuming a fixed body offset or the first matching marker.
   Account for multiple bodies and members with unequal counts.
2. Explain the complete seven-byte prefix. Verify component order, defaults,
   raw-mode selection, and prefix reuse against the other simple pins and
   differently oriented parts. Preserve original vertex/attribute order.
3. Explain the 17 normal residuals and four cap-sign disagreements before
   considering normals solved. Determine exact signed scaling, axis/sign
   mapping, and any shared update state.
4. For each later connectivity candidate, require a decoder to consume the
   declared count AND stop at an independently verified next field. Compare
   bit widths and deltas across pin, ring, and screw. Reject any decoder
   that only fits a visual impression or one stream prefix.
5. Once indices exist, check index ranges, finite coordinates, triangle
   winding, edge incidence and seams, duplicate attribute vertices, and
   face/LOD ownership. Closed-manifold checks are appropriate only where
   the decoded representation actually describes a closed surface.
6. Confirm matrix orientation using the 121 nonidentity sample transforms,
   nested instances, and local geometry. Do not derive orientation from an
   identity matrix. Record source units and up-axis evidence separately.
7. Turn only confirmed rules into a functional specification. Keep unresolved
   variants explicit. Implement from that specification; retain rejected
   hypotheses in research notes.

## Proposed importer architecture for this repository

The current route is `AssImpModelLoader::loadModel()` in
`src/Import/AssImpModelLoader.cpp`: XCAF factory first, then Assimp. The
loader already traverses `aiNode` transforms and emits mesh records with
`preserveNodeTransform` and `nodeWorldTransform`. Use that scene route rather
than flattening the assembly into world-space duplicate meshes.

Proposed components under `include/Import` and `src/Import`:

| Component | Responsibility |
|---|---|
| ThreeDXmlArchive | Bounded ZIP access; exact member lookup; manifest/root resolution |
| ThreeDXmlDocument | Namespace-aware XML; ID maps; reference/instance graph; source metadata |
| IThreeDRepDecoder | Representation bytes to geometry plus explicit supported/unsupported/error result |
| V5RepDecoder | Confirmed binary rules; bounded readers; preserved vertex/index/attribute ordering |
| XmlRepDecoder | Separate future format variant, requiring its own permitted sample/specification |
| ThreeDXmlSceneBuilder | Shared `aiMesh` resources and occurrence `aiNode` hierarchy |

This is a proposal, not existing classes. Keep 3DXML dispatch separate from
the STEP/IGES XCAF factory: tessellated representations do not require
pretending to be B-Rep documents. Make scene ownership explicit in the
loader; `_importer.FreeScene()` only frees Assimp-owned scenes. Route custom
parser errors directly rather than substituting `_importer.GetErrorString()`.

Archive/XML handling must bound member and total expanded sizes, avoid
filesystem extraction, reject ambiguous duplicate names and external URIs,
disable external entity resolution, detect missing IDs and recursion cycles,
and bound counts before allocating. Assembly references form a reusable graph;
use path-local cycle detection rather than rejecting every repeated reference.
Distinguish malformed data from an unsupported binary variant.

Do not infer analytic measurement metadata from visually circular meshes.
Keep authored geometry, node transforms, and units separate, then apply any
viewer orientation/scale once through the existing scene pipeline.

### Implementation gates

| Stage | Deliverable | Acceptance gate |
|---|---|---|
| A — current work | Research script, measured report, corrected notes | Reproducible counts/offsets; failures recorded |
| B — document reader | ZIP + manifest + XML graph and diagnostics | All sample links resolve; 251 instances remain instances; no geometry claimed |
| C — binary research completion | Byte-to-mesh specification | Exact indices, ordering, normals, stream boundaries and supported variants explained |
| D — decoder and scene builder | Own decoder + `aiScene` integration | Owner-run fixtures verify geometry and nested/nonidentity transforms |
| E — UI and release | Filters, progress, cancellation, errors | Owner build/visual validation; only verified variants advertised |

Stage B can proceed independently of connectivity research. Do not offer the
sample as successfully imported until stage C/D succeeds. If an explicit
future request permits partial import, expose omissions clearly; the current
default should fail clearly when required geometry is unsupported.

Production verification should include malformed/truncated counts, overflow,
ZIP expansion limits, duplicate names, missing references, cycles, shared
instances, nonidentity/nested transforms, and precise mesh fixtures once
connectivity is known. A second independently sourced, permitted sample is
needed before claiming general V5 or 3DXML coverage. Existing sample hashes
and observed counts are regression evidence, not the format definition.

No application source, dependencies, file filters, or build configuration were
changed in this continuation. No application build/test commands or commits
were run.
