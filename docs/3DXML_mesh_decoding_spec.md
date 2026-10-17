# Sample-derived 3DXML mesh decoding specification

2026-10-17. This supersedes the earlier connectivity hypotheses in
`3DXML_reverse_engineering.md`. All rules below come from the supplied archive
and original analysis scripts, without vendor software, source, schemas or SDKs.

## Current result

Renderable vertex/index meshes are now available for the pin, ring and screw, with
derived flat normals and separate candidate stored smooth normals. No surface
reconstruction, replacement triangulation, or guessed connectivity was used.
Triangle lists, strips and fans are decoded from the file's own references.

The same decoder passes every primitive-length sum and vertex-index range
check in **140 of the archive's 153 representations**, covering **1,440,769
vertex records and 2,190,652 finest-detail triangles**. This audit covers the
first recognized equal-count layout per member. Full topology checks have
been performed on the pin, ring and a more complex screw, not on all 140 members.

| Check | Dowel pin | Spacer ring |
|---|---:|---:|
| Vertex records | 268 | 729 |
| Distinct positions, all used | 140 | 472 |
| Triangles | 276 | 944 |
| Distinct welded edges | 414 | 1416 |
| Edge incidence | Every edge twice | Every edge twice |
| Winding inconsistencies | 0 | 0 |
| Degenerate triangles | 0 | 0 |
| Euler characteristic | 2 | 0 |
| Stored constant-normal/winding disagreements | 0 | 0 |
| Signed enclosed volume | 484.3689824 | 8163.0690024 |

The two previews use the decoded triangles and face normals computed from
them. Their component permutation to OBJ is explicitly documented below;
assembly placement and global axis conventions still need confirmation.

An independent check on `DIN_ISO_4762_M5x12.3DRep` produces 8,909 vertex
records, 6,268 distinct positions, and **12,532 triangles** across 132 face
blocks. Every one of its 18,798 welded edges has two incident triangles;
winding is consistent, there are no degenerate triangles, its Euler
characteristic is 2, and signed volume is approximately 403.9383533. Its
candidate pool normals also have no opposed triangle-corner normals, but
their exact quantization and smoothing across complex faces remain unconfirmed.

## Critical rule: two independent cursors

The stream combines ordinary byte fields with a **persistent packed-bit
reservoir**. Reading ordinary bytes, counts, floats, coordinate-control bytes,
or normal-tag bytes must not clear unused packed bits.

When more packed bits are needed, read a little-endian uint32 from the current
ordinary byte cursor. Append it above the reservoir's remaining low bits.
Consume requested bits least-significant first. Advance the ordinary cursor
only when loading another word, not once per logical packed field.

```text
readBits(k):
    while available < k:
        reservoir |= readRawU32LE() << available
        available += 32
    result = reservoir & ((1 << k) - 1)
    reservoir >>= k
    available -= k
    return result
```

This explains the previous apparent variable-length integer encoding and
32-bit payload alignment. For example, the pin's 105-reference triangle
blocks can occupy 116 or 120 newly loaded bytes because unused bits carry
across intervening face headers. No per-index entropy code is required here.

The earlier standalone-block codec probes had a false precondition: each
block was decoded as if its packed reservoir started empty. Their negative
results must not be cited as evidence of Huffman, Rice, or variable-width
index coding.

## Supported entry layout

The prototype starts at measured count fields supplied explicitly for the
three fixtures, or by the earlier sample inventory for the wider audit. A
compound-directory parser is still required for a production importer.

1. Read two compact counts N and M. The supported path requires N=M.
2. Read three ordinary bytes. These are `00 00 00` in the supported cases;
   their semantics remain unnamed.
3. Set index bit width `b = bit_length(N - 1)`.
4. Read one packed bit: `rawPositions`. This loads the first packed word.
   Its other 31 bits remain available for later topology, across all of the
   coordinate and attribute fields that follow.

This corrects the former "seven-byte unknown prefix": three bytes remain
unknown, while the following four bytes are the first packed word, not an
independent index-array header.

A compact count is an ordinary byte below `FF`, or `FF` followed by uint32
little-endian. Escaped values below 255 are permitted. Count parsing does
not touch the packed reservoir.

## Coordinates

If `rawPositions` is one, read N ordinary triples of float32 little-endian.
Three recognized members use this path.

Otherwise:

1. Read `ceil(N/4)` ordinary bytes. Extract two-bit symbols low pair first,
   stopping after N symbols.
2. Read compact float count Q, then Q ordinary float32 values.
3. Initialize a three-component previous tuple to zero.
4. For each symbol s, consume `(s+3) mod 4` floats, replacing that many
   trailing components of the previous tuple. Emit the whole tuple.
5. Require exact consumption of Q floats; reject nonfinite positions.

The serialized tuple's first component is axial for the pin/ring. Exported
research OBJs permute `(p0,p1,p2)` to `(p1,p2,p0)` and label it as the
candidate `(z,x,y)` to `(x,y,z)` mapping. This is a cyclic permutation and
preserves handedness. Do not assume this establishes assembly matrix
orientation, source units or global up-axis.

Keep repeated positions as separate vertex records. They can carry distinct
attributes and can be shared differently across detail levels and face groups.

## Normal data and face binding

After coordinates, read compact word count W and W ordinary signed int16
little-endian words. Then read `ceil(N/2)` ordinary tag bytes, low nibble
first, stopping after N tags. In all 140 recognized members:

```text
tags are in 0..11
W = 2 * count(tags in 0..5)
```

The rendering candidate is:

- For tag 0..5, the omitted component is `tag // 2` in serialized order.
  Read two stored components for the other axes, provisionally divide by
  32767, and reconstruct the omitted component's magnitude as
  `sqrt(1-u*u-v*v)`. Odd tags give a negative omitted component.
- Tags 6..11 give exact cardinal vectors: axis `(tag-6)//2`, positive for
  even tags and negative for odd tags.
- A face's explicit constant normal, when present, overrides the vertex pool
  normals for that face. Do not average across this binding boundary.

The exact integer-to-component scaling/quantization remains unconfirmed;
the stored smooth-normal OBJ is explicitly marked as a candidate. Both
sample candidates are unit length within floating-point roundoff, and no
normal used on a nonconstant face opposes its triangle:

| Check | Pin | Ring |
|---|---:|---:|
| Vertex IDs using pool normals | 212 | 472 |
| Minimum dot with triangle normal | 0.9959869 | 0.9995573 |
| Opposed normals | 0 | 0 |

The ring's previous 17 large analytic-normal residuals do not occur on
vertices using pool normals in the finest nonconstant faces. Earlier checks
incorrectly required every vertex's normal to describe the final analytic
surface, including vertices used only in coarser levels and faces whose
constant normal overrides the pool. The small remaining component differences
do not establish the exact quantization rule.

Derived face normals are available now for dependable rendering independent
of the unresolved stored-normal scaling. These are computed from the exact
decoded triangle references, without changing geometry.

## Primitive packets

Read ordinary envelope byte `01`, then flags `40..4F` on this supported path.
Enabled low bits declare compact counts, in this order:

| Flag | Field |
|---|---|
| `01` | T standalone triangles |
| `02` | S strips |
| `04` | F fans |
| `08` | Constant normal present |

Bit `40` is always set here; its broader meaning is not named. Read compact
total vertex-reference count I after the primitive counts. If bit `08` is
set, read three ordinary float32 values for the constant normal now.

Using the persistent reservoir and width b:

1. Read S strip lengths, b bits each.
2. Read F fan lengths, b bits each.
3. Require each length to be at least three and
   `3*T + sum(stripLengths) + sum(fanLengths) == I`.
4. Read I vertex references, b bits each; require every reference below N.
5. The first `3*T` references are triangle-list triples. The next references
   are the strips in length-table order, then the fans in length-table order.

Expand a strip `v0,v1,...` to `(v0,v1,v2)`, `(v2,v1,v3)`, `(v2,v3,v4)`, etc.
Expand a fan to `(v0,v1,v2)`, `(v0,v2,v3)`, etc. Preserve triangle-list order.

The triangle count before checking degenerates is
`I - 2*(T+S+F)`. No synthetic primitive is added to close surfaces.

## Detail levels and finest faces

After normal tags the supported layout has one ordinary float32 scalar,
then compact count C of coarse primitive blocks. For each:

1. Decode a primitive packet using the shared packed reservoir.
2. Read an ordinary float32 scalar and an eight-byte style record.

The two observed style forms start `30 04 04 FF FF` or `B0 04 04 FF 7F`,
followed by three color bytes. The prototype consumes these records but does
not claim material semantics. Other forms are rejected explicitly.

After the C coarse blocks, the observed finest-face container is either
`03 02 0A` or `02 02 0A`, followed by compact face count and that many primitive
packets. The distinction between these two container forms remains unnamed;
both reconcile in the supplied corpus. Do not concatenate coarse and finest
triangles into a single rendering mesh.

For the pin, the decoder finishes the geometry at its measured byte cursor
while retaining unused reservoir bits. Those bits must not be discarded if
future work continues into later records. The prototype exports the finest
faces only and preserves coarse packets in its research JSON.

## Reproduction and artifacts

Run from the repository root:

```powershell
python -I tools/3dxml/decode_sample_meshes.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --output-directory out/3dxml-research --audit-inventory docs/3DXML_sample_analysis.json
python -I tools/3dxml/render_mesh_preview.py out/3dxml-research
```

The decoder uses Python's standard library. The optional preview uses Pillow.
Generated source-derived geometry stays under ignored `out/3dxml-research`:

- `ISO_2338_5x25_Zyl_Stift.obj` and `Distanzring_1_R.obj`: decoded geometry with
  derived flat normals.
- `DIN_ISO_4762_M5x12.obj`: the more complex screw, also with derived normals.
- `*.smooth-candidate.obj`: same geometry with the candidate stored smooth
  normals and exact stored constant face normals.
- `*.mesh.json`: original decoded vertex/index order, primitive groups, and
  stored normal words/tags.
- `decoded_meshes.png`: an inspection preview of the actual triangles.

Aggregate evidence is retained in `3DXML_mesh_verification.json` and
`3DXML_mesh_inventory_audit.json`. Archive/member hashes link this work to the
original sample. No original archive or extracted vendor representation is
copied into source control.

## Remaining work

1. Decode the 13 layouts not recognized by the original inventory, including
   unequal counts, large housings and possible multiple-body cases. Do not
   classify small members as empty solely because this decoder skips them.
2. Decode the compound directory and body graph so entry points come from
   structure rather than sample offsets or marker discovery.
3. Confirm exact stored-normal quantization and axis conventions on more
   shapes. Candidate smooth normals are useful evidence, not a completed
   format specification for all variants.
4. Check geometry on more of the 140 members, especially open surfaces,
   multiple components and nonmanifold cases. This audit only establishes
   consistent counts and in-range indices for that broader set.
5. Confirm assembly matrix orientation, nested occurrences and units before
   translating this specification into the planned C++ importer.

No application build/test commands or commits were run. The application
sources, dependencies and import filters remain unchanged.
