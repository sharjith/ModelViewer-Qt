# Unequal counts and wider mesh coverage

2026-10-18. Research only on `feature/3dxml-importer`, after merging
`origin/dev` at `5f5decb`. The two STEP fixes brought in by that merge are
owner work; this investigation changes only Python research and documentation.
The existing dirty `vcpkg` submodule was not modified by this work.

## Discovery

The former requirement that the two leading counts match was too strict.
The first count N describes positions, while the second M describes the
normal-tag records. Coordinate control bytes still use `ceil(N/4)`; normal
tag bytes use `ceil(M/2)`. Index width remains `bit_length(N-1)`.
The normal-word constraint remains `W=2*count(tags<6)`.

This change, with no changes to primitive decoding or stored indices, makes
ten additional first blocks reconcile completely through their finest-face
container. Their decoded positions are finite, every primitive length sum
matches, and every coordinate index is below N. On all ten, every finest
nonconstant face's vertex reference is also below M, so the candidate normal
checker needs no padding, invented normal records or shifted binding.
Vertices outside the normal pool must only use a supported explicit binding;
do not assume missing normal records can be filled arbitrarily.

| Member (without .3DRep) | Positions N | Normal records M | Finest triangles |
|---|---:|---:|---:|
| Feder_3_4 | 4,547 | 4,546 | 8,674 |
| Feder_3_4_52 | 4,573 | 4,572 | 8,674 |
| Feder_3_4_53 | 4,561 | 4,560 | 8,674 |
| 233_0_0376_98_Kupplungsgehaeuse_FINALPART | 295,883 | 295,882 | 512,716 |
| 233_0_0376_98_Getriebedeckel_FINALPART | 310,951 | 310,937 | 535,796 |
| 233_0_0376_98_Mittelgehaeuse_FINALPART | 210,056 | 210,055 | 362,482 |
| Rastierung_5_6 | 15,312 | 15,311 | 29,964 |
| Anschlagblech_Schalthebel | 2,008 | 2,006 | 2,524 |
| Rastierung_Schaltwelle | 53,622 | 53,621 | 95,484 |
| Speed_Sensor | 5,242 | 5,241 | 8,372 |

The clutch housing's marker is at count offset 264,051, far beyond the old
4,096-byte search window. Both the unequal counts and the search window had
prevented its discovery. A full scan now finds a decodable first block in
150/153 members, totaling 2,347,524 position records and 3,764,012 finest
triangles. These totals count first blocks, not a completed assembly.

## Geometry checks and limits

All ten additional first blocks have zero degenerate triangles under the
existing squared-cross-product threshold `1e-16`. Checks weld only exactly
equal position tuples; no tolerance-based repair is applied.

- All three spring meshes have 4,339 used distinct positions and 8,674
  triangles, every edge incidence 2, consistent winding and Euler value 2.
- Clutch housing: 512,716 triangles, every welded edge incidence 2, no edge
  winding inconsistencies, Euler value -36. One triangle disagrees with its
  stored constant normal. This disagreement is preserved in the JSON report.
- Gearbox cover: 535,796 triangles, every welded edge incidence 2, consistent
  winding, Euler value -50, no constant-normal disagreement.
- Middle housing: 362,482 triangles, 543,720 welded edges incidence 2 and six
  edges incidence 1. The orientation metric reports those same six boundary
  edges as unmatched directed edges; it does not establish six internal winding
  reversals. Euler value -68. No constant-normal disagreement.
- The other four new first blocks have every edge incidence 2 and consistent
  winding. Euler values vary; do not equate Euler 2 with the only valid shape.

The larger meshes expose candidate smooth-normal disagreements that were not
present on the pin/ring/screw fixtures. Counts below are triangle-corner
comparisons with dot product <=0, not distinct vertex counts:

| Mesh | Opposed candidate normal corners |
|---|---:|
| Clutch housing | 6 |
| Gearbox cover | 29 |
| Middle housing | 3 |
| Rastierung_5_6 | 6 |
| Rastierung_Schaltwelle | 3 |
| Other five new first blocks | 0 |

This does not isolate quantization, binding, source tessellation or a possible
additional normal mode as the cause. The `32767` scaling remains provisional.
Do not flip these normals, reorder triangles or claim exact smooth shading.
Derived triangle normals remain available from the exact stored connectivity.

## Later blocks and small representations

Ten members contain multiple occurrences of the measured marker:
`Synchro_5_6_233_0_0376_98`, `Synchro_1_2_233_0_0376_98`,
`Synchro_R_233_0_0376_98`, `Linearfuehrung`, `Linearfuehrung_D20`,
`INAFAG_607`, `Entluefter`, `DIN908_M10x1_Verschlussschraube`,
`DIN908_M22x1_5_Verschlussschraube`, and `Speed_Sensor` (all .3DRep).
Later candidates do not pass coordinate-float reconciliation when decoded
with a fresh reservoir. Markers alone do not prove independent mesh bodies.

An exploratory check carrying the preceding first block's residual bits,
with each possible number of those bits consumed, also failed on the next
candidate in the sensor, vent and two plugs. Thus merely reusing the previous
geometry end state does not solve traversal; intervening records or a separate
stream entry point must be understood. No guessed index stream was accepted.

`Kugel.3DRep` and `Rotationsachse_Koppelstange.3DRep` are each 1,469 bytes
and contain no recognized mesh marker. They contain compound metadata names
including MainDataStream; their full semantics remain unresolved. They are
not declared empty. `Applications.3DRep`, the third marker-free member, is
the XML-identified TECHREP review attachment rather than a UVR mesh.

## Reproduce and next step

```powershell
python -I tools/3dxml/probe_remaining_layouts.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --output docs/3DXML_extended_layout_audit.json --check-unequal-geometry
```

The report preserves member hashes, every marker candidate and failure,
counts, end offsets and the ten new geometry/normal checks. The earlier
140-member audit is retained as the previous equal-count baseline.

The next structural task is to decode the compound directory/stream graph
and intervening records, so mesh entry points and reservoir initialization
come from the format structure rather than marker search. Then enumerate all
bodies before claiming complete per-representation or assembly coverage.
Exact smooth-normal handling remains a separate open task. No proprietary
software, external decoder, SDK, schema, decompilation or reference viewer
was used. No application builds or application tests were run.
