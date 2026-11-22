# Parasolid X_T: what the published sources specify, and how far a reader gets

15 November 2026, branch `feature/parasolid-importer`. Research only; no importer code exists. Sources and method limits: [PARASOLID_PROVENANCE.md](PARASOLID_PROVENANCE.md). Follows the October scoping note [PARASOLID_IMPORT_RESEARCH.md](PARASOLID_IMPORT_RESEARCH.md) and the inventory [PARASOLID_XT_PHASES.md](PARASOLID_XT_PHASES.md).

## 1. Answer in one paragraph

A reader built only from the public April 2008 *XT Format Reference* plus the embedded-schema information carried inside each file can decode text X_T completely, from the common header to the terminator, for files that carry an embedded schema over the V13 base. A throwaway probe (local only, see §6) did this for **8 of the 9 local embedded-schema files** (from about 1,200 to 1.4 million records; the largest file is 115 MB). It stopped on one file at an undocumented node type. It cannot start on the **3 local files that use an old unsuffixed schema key**, because the published reference does not contain those layouts. Decoding records is only the first of the separate achievements the October note lists. Nothing here says anything yet about building valid OCCT shapes.

## 2. What the reference actually specifies (observation)

| Area | Specified | Notes |
|---|---|---|
| Common header | Yes | Preamble, three keyword parts, trailer. Producers differ: one omits the format key, one writes a locale-formatted date containing non-ASCII bytes. A parser must tolerate both. |
| Text transport | Yes | Newlines and trailing spaces are not significant; numbers are space-terminated; `c`/`l` fields are not; unset is `?`. In real files `?` is **not** followed by a space (see the reference's own example and every sample). Escapes for NUL/CR/LF/backslash since V12.1. |
| Record stream | Yes | Type, optional variable count, index, fields; terminator is type 1 with index 0, written without a trailing space at end of file. |
| Embedded-schema edit script | Yes | Copy/Delete/Insert/Append against a base schema, introduced on the first node of each type. |
| Topology fields | Yes | Part, assembly, instance, body, region, shell, face, loop, fin, edge, vertex, with senses and ring/chain rules. |
| Curve/surface fields | Yes | Line, circle, ellipse, B-curve, intersection and chart, trimmed, SP-curve, plane, cylinder, cone, sphere, torus, blend, offset, B-surface, swept, spun, foreign (PE). |
| Transforms | Yes | Rotation, translation, scale and a flag word (translation, rotation, scaling, reflection, general affine). |
| Attribute machinery | Yes (generic) | Attribute definitions, field values, groups. Only a few system attributes are described (hatching, density, colour, reflectivity, translucency, transparency, region). |
| Node-type numbering | Yes, but see §3 | The printed table. Numbers are scrambled by text extraction. |
| **Units** | **No** | The extracted text contains no statement of length units. Parasolid is generally described as metre-based; this is **unverified here** (hypothesis). All 12 files carry the same resolution pair (size box 1000, linear 1e-8), which fits that convention. |
| **Pre-V13 schemas** | **No** | Only a V12 example and the V13 base idea; the unsuffixed older keys need schema files that ship with the kernel. |
| Node types after V12 | No | The reference stops at type 184. The newest local file declares 249 types. |
| X_B | Yes | Neutral and bare binary framing, short-index encoding. Not tested here. |

## 3. Discrepancies between the reference and real files (observation)

The reference describes a layout newer than the V13 base that embedded files are measured against. The embedded edit scripts expose this, because each one must consume the base fields exactly.

1. **Body**: the four trailing fields for indexed transmit (offset, index map, id map, embedding map) are *appended* by every file's edit script, so they are not in the base. The same four appear in the reference as ordinary fields.
2. **List**: three trailing fields (finger block, finger index, notransmit) are likewise later additions. The list type field also changes encoding.
3. **Pointer list block**: the index-map offset is an insertion, not a base field. The entries array is variable length (the count precedes the node index), although the C-like struct in the reference could be read as a fixed 20.
4. **Helix forms**: the two helix node numbers read from the printed table are swapped relative to the data (the form with an extra point field is 184, the other 163). Cross-checked against 13 form nodes.
5. **Node numbers**: the printed table's columns are not row-aligned after text extraction; every number used had to be confirmed on real nodes.

Consequences for design: transcribe the reference as the *newest known* layout, derive the V13 base by removing fields that edit scripts append, and let each file's script assert full consumption of the base. A mismatch must stop the reader with a precise diagnostic, never guess.

## 4. Local results (observation)

IDs are the first 12 hex digits of SHA-256. Producer strings are application names from the common header. "Decoded" means: run to the terminator with every record's fields read by the schema, no stray tokens, and the stream length consumed exactly.

| ID | Bytes | Producer (header) | Internal schema | Result | Records |
|---|---:|---|---|---|---:|
| XT-e62b10ff0279 | 53,282 | Onshape, kernel 38.1 | `SCH_3801231_38101_13006` | Decoded | 1,228 |
| XT-8ffda52db5e5 | 884,175 | Solid Edge | `SCH_1801333_18106_13006` | Decoded | 17,148 |
| XT-817da15a5a8f | 7,530,173 | SolidWorks 2020 | `SCH_3101284_31100_13006` | Decoded | 96,464 |
| XT-b774f7febff6 | 18,939,061 | Solid Edge | `SCH_2800143_28002_13006` | Decoded | 183,037 |
| XT-097375059234 | 23,078,297 | SolidWorks 2018 | `SCH_3000298_30000_13006` | Decoded | 260,520 |
| XT-380a3dbf46c9 | 24,920,096 | SolidWorks 2016 | `SCH_2800204_28002_13006` | Decoded | 309,364 |
| XT-8ea8be98e927 | 87,831,406 | Unigraphics (header) | `SCH_2500176_25001_13006` | Decoded | 938,284 |
| XT-5268a1dca7d6 | 115,561,052 | Unigraphics (header) | `SCH_2500248_25001_13006` | Decoded | 1,431,957 |
| XT-ed43d4c8caf1 | 58,008,359 | SolidWorks 2019 | `SCH_3100203_31001_13006` | **Stopped** at record 66,266 | one node type (151) is not in the reference |
| XT-950660d6bedb | 12,207,763 | SolidWorks 2025 | `SCH_800000_8008` | **Not started** | legacy key |
| XT-676618a88448 | 23,832,787 | SolidWorks 2025 | `SCH_800000_8008` | **Not started** | legacy key |
| XT-79bc2cd92b51 | 122,955,132 | SolidWorks 2012 | `SCH_900000_9008` | **Not started** | legacy key |

Reference resolution on the eight decoded files: of about 12 million checked scalar pointers, only 2 to 13 per file point at an index with no node. All are in the categories the reference says may be ignored (attribute-definition chain, body chain links) or are chain heads. This supports, but does not prove, that the field layouts are right.

Read time in the probe: under 11 s for the 115 MB file (Python, no geometry). Parsing cost will not be the bottleneck; geometry construction will.

Two further observations about the legacy-key group: they are all SolidWorks, and two are from 2025, so a current exporter still writes the old keyed schema in some configurations. The old schema also uses different node numbers for the first ten or so types (body is 10 there, not 12), so it cannot be handled by adding a few fields to the V13 base.

## 5. What the decoded files contain (observation, eight decoded files)

Counts are from the eight decoded files, except where stated. Totals live in `out/parasolid-research/probe/*.log` (§6).

- **Body types**: solid (1) and sheet (3) occur. A single Unigraphics-header file has about 1,770 sheet bodies and 118 solids. Wire and general bodies did not occur. Region types solid and void both occur.
- **Assemblies**: every decoded file contains assemblies and instances with transforms (up to ~1,900 instances). One Unigraphics-header file has nearly all instances without a transform (identity), another has all of them with one. Part sharing across instances is present.
- **Transforms**: flag values 0, 1, 2, 3 only (identity, translation, rotation, both). **No scale, reflection or general-affine transform occurs.** The scale field is always 1. A mirrored-instance case therefore has no local evidence.
- **Curves**: lines, circles, ellipses, B-curves (with NURBS data), trimmed curves, intersection curves with charts and limits, SP-curves. Intersection curves are very numerous in the large files (tens of thousands). Helix forms occur but rarely.
- **Surfaces**: planes, cylinders, cones, spheres, tori, B-surfaces, offset, swept, spun, and **many rolling-ball blends with blend-boundary surfaces** (over 20,000 in the largest file). Foreign geometry (PE curves/surfaces) did not occur.
- **Attributes**: system definitions seen are colour, name, density, translucency, reflectivity, hatching. Others (blend id, user name, edge no-merge) are not in the reference and are carried by self-describing definitions. Every embedded-schema file has a name attribute and a colour attribute. The meaning and owner of the name attribute are not defined in the reference. See [PARASOLID_VALIDATION_PLAN.md](PARASOLID_VALIDATION_PLAN.md) for how to settle it.
- **Types beyond the reference**: files from kernel 28 onward define further node types in-stream (for example an intersection-data node, and lattice/mesh/polyline pointer classes on the body). These can be read generically because their field lists are carried in the file, but their meaning is unknown. A reader should skip them by structure and report them.

## 6. Reproducing this evidence

The evidence comes from the tracked decoder [`tools/parasolid/xt_decode.py`](../../tools/parasolid/xt_decode.py). It contains no vendor tables: the base field layouts are read from a local, untracked JSON file the owner produces from the reference (see the [tool README](../../tools/parasolid/README.md)). The first run used an earlier throwaway probe with the layouts embedded; the tracked tool reproduces its results exactly (same status and record count on all 12 files).

```powershell
python -I tools/parasolid/xt_decode.py out/parasolid-research/layouts-v13.json "<local-samples>" > out/parasolid-research/decode.json
```

The prefix inventory is `python tools/parasolid/inspect_xt.py "<local-samples>" > out/parasolid-research/inventory.json` (Codex's tool, unchanged). The earlier probe stays under ignored `out/parasolid-research/scratch/` and is superseded.

## 7. Not established

- Any geometric correctness: no surface or curve was evaluated, no shape built or measured.
- The legacy keys (§4), the one undocumented node type, and the binary transports.
- Units, and the meaning of the name attribute and any assembly/part naming.
- Whether the three legacy-key files can be read at all from public information. A lead to check is whether the JT 10.6 reference's XT annex documents older layouts; this was **not** read for this pass.
- The effect on any of the above of a different producer, a different kernel version, or a corrupted file.

## 8. Gap check against the July 2022 reference (15 Nov 2026)

The owner supplied the July 2022 edition (hash in [PARASOLID_PROVENANCE.md](PARASOLID_PROVENANCE.md)). Observations from reading it for our gaps only:

| Gap in section 2 or 7 | 2022 edition |
|---|---|
| Helix node numbers | Confirms 163 and 184 as the data showed; the 2008 table's column reading was wrong. |
| Units | Recommends one metre as the default unit (resolution 1e-8, size box 1000). Supports the metre hypothesis; still a recommendation, so keep the control file. |
| Name and colour attributes | Defines name (many legal owners), colour 2, layer, translucency 2 and scale factor. Resolution order between them is still to be confirmed on a control. |
| Newer node types | Lists polyline, mesh, lattice, intersection data and more, by number; layouts are not yet compared. |
| Type 151 (stops one file) | **Not in the table.** Gap remains. |
| Pre-V13 schemas (8008, 9008) | No layouts. The text mentions older keys only in examples. Gap remains. |

Next step if the owner wants it: compare the 2022 field layouts with our base table. That reading is permitted by the same notice as the 2008 copy; nothing from it has been transcribed.
