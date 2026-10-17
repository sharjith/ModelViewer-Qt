# Handout: decoding binary CATIA V5 `.3DRep` (3DXML) for ModelViewer-Qt

> **Current research:** Start at the [research index](docs/3DXML_RESEARCH_INDEX.md).
> The [scene graph specification](docs/3DXML_scene_graph_spec.md) corrects the
> earlier coordinate-axis interpretation and documents matrix composition,
> instances, visibility and the sample-backed unit inference.

> **Continuation, 2026-10-17:** The current user requested resuming research.
> See [corrected findings and importer plan](docs/3DXML_reverse_engineering.md)
> and the reproducible measurements in `docs/3DXML_sample_analysis.json`.
> The continuation corrects the two-bit stream alignment/bit order, derives
> exact float-consumption counts, and withdraws the apparent interior cap
> points below. A further pass identifies triangle/strip/fan headers,
> constant cap normals, and finest-detail totals of 276 triangles for the pin
> and 944 for the ring. Individual indices and exact per-vertex normal
> semantics remain unresolved.
> The original notes are preserved below as historical hypotheses.

> **Latest:** [Mesh decoding specification](docs/3DXML_mesh_decoding_spec.md):
> individual indices and strip/fan lengths now decode through a persistent
> bit reservoir. Exact pin/ring meshes and rendering OBJ fixtures exist;
> 140 representations pass reference/count checks. Per-vertex normal
> quantization remains provisional; derived face normals are available.

Goal: an **exact** importer for `.3dxml` that displays the lightweight (tessellated) geometry in ModelViewer.
Hard rules from the owner:
- **No point-cloud / surface-reconstruction approximation.** Triangles must be decoded exactly.
- **Clean room.** Work only from the raw bytes of the sample file. No Dassault software, APIs, SDKs, decompilers, XSD schema files, or code. No viewer of theirs for reference output. Write a functional spec from observed bytes, then code from the spec.
- Do **not** run build/test commands in this repo (owner builds and tests himself). Python analysis scripts on extracted files are fine. Do not commit before the owner has tested and confirmed.
- Work is paused until after the 2026.11.0 release (2026-10-19); resume afterwards.

Sample: `D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml` (CATIA V5 export, 54 MB, 157 zip entries, 153 `.3DRep`, all binary, none XML).

## 1. What is already solved (easy part)

The `.3dxml` is a ZIP:
- `Manifest.xml` → `<Root>ASM_GETRAG_V160.3dxml</Root>`.
- The root file `ASM_GETRAG_V160.3dxml` is plain XML (namespace `http://www.3ds.com/xsd/3DXML`, `SchemaVersion` 4.2): `ProductStructure root="1"` containing `Reference3D`, `Instance3D` (`IsAggregatedBy`, `IsInstanceOf`, `RelativeMatrix` = 12 numbers: 3x3 rotation then translation, `1 0 0 0 1 0 0 0 1 0 0 0` is identity), `ReferenceRep` (`format="UVR"`, `associatedFile="urn:3DXML:X.3DRep"`), `InstanceRep` (links Reference3D → ReferenceRep).
- Each `ReferenceRep` points at a `X.3DRep` zip entry. Those are the binary files to decode.
- `Preview2D.png`, `.IntegrityCertificat` are irrelevant.

Plan for the importer (not started): parse the XML structure → assembly tree with matrices; per rep call a pluggable decoder (XML 3DRep decoder for V6-style files, binary V5 decoder for this one). Until the binary decoder is exact, the importer must report "binary CATIA V5 reps not supported" rather than approximate.

## 2. Container format of a binary `.3DRep`

Starts with ASCII `V5_CFV3` then `CATIA_V5 CB0001`; ends with `CB__END`. It is a compound file: a UTF-16LE directory/tree at the tail (names such as `Skeleton`, `SurficicReps`, `Applicative Containers`, `MainDataStream`, `V4V5_FDT`, `Options`, `MAIN`) with stream sizes, and one big body stream starting around file offset `0x200` (its length is stored just before, e.g. `ff <len>`; for the dowel pin 3783 bytes, for the ring 10908).

Body layout is a sequence of **arrays introduced by `ff` + u32 little-endian count** (5 bytes), e.g. `ff 0c 01 00 00` = count 268. Small headers (`ff 03 00 00 00`, `ff 01 00 00 00`, ...) also appear. The directory also repeats some counts (`946`, `233`, `237` for the ring).

A tail section (ring: from ~`0x2d65`) holds per-face records with exact analytic surface parameters (floats: radii 22.5 and 34.0, axis `-1.0`, z origins) — not tessellation, but useful to learn face structure.

## 3. Files extracted for analysis (re-create with Python)

```python
import zipfile
z = zipfile.ZipFile(r'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml')
for n in ('Distanzring_1_R.3DRep','ISO_2338_5x25_Zyl_Stift.3DRep','DIN_ISO_4762_M5x12.3DRep',
          'j_18_fitted.3DRep','sw_38_fitted.3DRep','Innenring_1.3DRep'):
    open(n,'wb').write(z.read(n))
```
Run Python with `-I`. Best targets (small, simple): `ISO_2338_5x25_Zyl_Stift.3DRep` (dowel pin Ø5×25, 6564 bytes) and `Distanzring_1_R.3DRep` (spacer ring 13588 bytes, outer r=34, inner r=22.5, 4 mm thick at z=515.3..519.3 in part coordinates). `Kugel.3DRep` / `Rotationsachse_Koppelstange.3DRep` (1469 bytes) contain no mesh. `DIN_ISO_4762_M5x12.3DRep` is a free-form screw (more complex).

## 4. Findings so far

### 4.1 Dowel pin `ISO_2338_5x25_Zyl_Stift.3DRep` (file offsets hex)
| offset | item |
|---|---|
| `0x2e0` | `ff c7 0e 00 00` → body stream length 3783 |
| `0x30d`, `0x312` | `ff 0c 01 00 00` twice → count 268 |
| `0x31a`..`0x361` | flag/symbol stream: 4-byte header `24 18 78 f0` + **exactly 268 two-bit symbols** (67 bytes, MSB-first pairs) |
| `0x361` | `ff 04 01 00 00` → 260 float32 words, data `0x366`..`0x776` |
| `0x776` | `ff b8 01 00 00` → count 440, data `0x77b`..`0xb7a` (1023 bytes) |
| `0xb7a` | count 170 (216 bytes, entropy-looking) |
| `0xc57` | count 102 (337 bytes) |
| `0xda8`, `0xe24`, `0xee0`, `0xf5c` | count 105 each |
| ... | later `255`, `511`, `1020`, `12369`.. headers (face/analytic records) |

**Float array (260 words) is decoded.** It is a stream of tokens: a `z` float (exactly one of 0.0, 1.0, 24.0, 25.0) sets the current z level; every other value comes in `(x, y)` pairs belonging to that z. 18 z-words + 121 points = 260 ✔. Validated geometry: cylinder points r=2.5 at 10° steps at z=1 and z=24 (36 each), chamfer rings r≈2.232 at z=0 and z=25, plus interior triangulation points on the end caps (r<2.232, e.g. (−0.8063, 0.8063)). Tiny floats like `1ce80d29` are real ≈0 values (cos 90° rounding), not tags. Points are mirror/rotation-symmetric in groups; the stream order is not geometric but looks hierarchical.
Caveat: ambiguity when a coordinate equals exactly 0.0/1.0/24.0/25.0 — the symbol stream (below) probably resolves it.

**Flag stream (268 symbols)**: counts after the header → `1:126, 3:82, 2:42, 0:18`. Pattern runs like `1313131...` suggest alternating bottom/top copies. `0` count (18) equals the z-word count in the pin, but NOT in the ring (ring has 23 zeros vs 14 z-words), so this is only a hypothesis. Meaning unsolved.

**440 array**: the first ~1020 bytes are a token list of 16-bit big-endian-looking pairs. Observed values: `e1de` and its mirror `1f21` (they sum to 0x100FF: hi bytes sum 0x100, lo bytes sum 0xFF), `99f4`/`670b` (same relation), `c7e9`/`3916`, `3cd4`/`c42b`, `472a`/`b8d5?`, `0000`, `ff3f`/`01c0` (±0x3fff = 16383). Likely quantized normals or UVs (±0x3fff suggests unit = 16383). After ~`0xae0` the data switches to nibble-ish bytes (`37 33 57 47 27 22 44 55 ...`) — a second coding. Structure not decoded.

### 4.2 Spacer ring `Distanzring_1_R.3DRep`
- `0x31a`/`0x31f`: `ff d9 02 00 00` ×2 → 729; symbol stream = 4-byte header `18 50 c0 00` + 729 two-bit symbols (`0x328`..`0x3e2`). Counts: `3:453, 1:253, 0:23`, no `2`.
- `0x3e2`: `ff ca 03 00 00` → 970 words from `0x3e7`: 14 z-words (these are plain floats 515.3 = `33 d3 00 44` and 519.3 = `33 d3 01 44`) + 478 (x,y) pairs. Pairs have radius exactly 34.0 (outer, 257) or 22.5 (inner, 216+…). Each distinct (angle, radius) appears exactly twice, once per z level. Outer circle: 130 equally spaced angles; inner: 106.
- Point blocks (between z-words) grow roughly geometrically: pair counts 4,3,3,6,12,6,14,17,28,31,43,75,135,~100, alternating z levels. Suggests progressive / hierarchical (LOD-like) vertex ordering.
- `0x130f`: `ff d2 03 00 00` → 978: stored as 489 × 4-byte records `00 00 HH LL` (big-endian u16 in the low half). 234 distinct values, each normally appearing exactly twice (same-position duplicates); two values (`5d3d`, `c2a3`) appear 10 times. The duplicate-gap histogram matches the point list's (113 most common, then 130, 85, 135, 76) → it is a per-vertex-ish attribute with ~12 extra entries. NOT angle, NOT 8+8-bit octahedral normals (tested), not correlated with float values.
- Then `0x1ab8`..: nibble-looking data, then arrays `780 (0x1c2e)`, `360`, `204`, `104`, `110`, `110`, `132`, `132`, `310`, `312` at `0x1c2e, 0x2057, 0x224c, 0x2371, 0x24be, 0x2552, 0x25e6, 0x2692, 0x2744, 0x2906`. Each begins with a short table of ~5-byte records (e.g. `03 12 38 e0 00  03 0c 30 c0 00  03 12 38 e0 80 ...`) followed by high-entropy bytes → very likely an entropy-coded (Huffman-like) index/connectivity stream, probably progressive-mesh / strip data. **Not decoded — this is the main blocker.**

### 4.3 Screw `DIN_ISO_4762_M5x12.3DRep`
Float array at `0xc9b` (`ff fd 2e 00 00`, 12029 words) starts as plain `(x,y,z)` triples, apparently sorted by x (e.g. (−4.179,0.773,−0.300), (−3.934,−0.356,0.0), ...), then the layout changes after ~15 triples (multiple per-face sub-blocks). Needs its own analysis once the pin/ring are understood.

## 5. Working hypotheses (unverified)
1. The symbol stream (2 bits per output vertex/primitive) encodes how each output vertex is formed (explicit coordinates vs copy/mirror/z-flip) — counts do not yet reconcile (pin: 268 symbols vs 121 explicit points).
2. The vertex order is a hierarchical/progressive order and the entropy-coded arrays are the matching split/connectivity records.
3. The 16-bit tables are per-vertex attributes (normals or surface parameters) quantized with a bias/sign-mirror scheme.
4. Faces carry analytic surface data in the tail section; tessellation per face uses the shared vertex pool.

## 6. Suggested next steps
1. Fully map every array of the dowel pin: for each `ff <count>` header record offset, byte length, bytes-per-element; plot entropy per array. Decide which arrays are plain tables vs bit-coded.
2. Use the pin's known geometry as a checksum: 36-gon cylinder (z=1, z=24), chamfer rings, two end caps. Expected: triangle count and connectivity are strongly constrained (cylinder = 72 verts / 72 tris if separate, caps are triangulated discs with interior points).
3. Decode the 440 array's first part (16-bit tokens) and relate token groups to vertices (try groups of 2 and 3 values; test unit length with scale 16383 and 32767).
4. Reverse the code-table + bitstream (`ff 0c 03 00 00` style arrays in the ring): determine bit order, whether canonical Huffman, what the 5-byte records mean (len/code/symbol?).
5. Reconcile the 268 symbols with the vertex pool and index counts; then apply to the ring and the screw.
6. Write a functional spec (markdown, bytes→meaning) before writing C++. Implement as `Import/…/ThreeDXmlImporter` + `V5RepDecoder` behind the existing importer pattern in the repo (look at how other importers register and how assemblies/instances with matrices are built), with unit tests only the owner runs.

## 7. Reference tooling notes
- Windows Python is on PATH (`python -I script.py`); MSYS paths (`/c/...`) do not work inside Python, use `C:/...`.
- Whole-tree ripgrep/glob in this repo times out (build/vcpkg dirs). Use `git ls-files | grep` or restrict to `src/`, `include/`, `ui/`.
- Memory notes from the previous session: `project_3dxml_importer_binary_3drep_re.md` (in the Claude project memory dir) mirrors this handout.
