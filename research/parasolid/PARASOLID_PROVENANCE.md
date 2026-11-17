# Parasolid X_T provenance and licensing statement

15 November 2026, branch `feature/parasolid-importer`. This records **method**: what was consulted and how it was used. It is not legal clearance. Follows the pattern of [3DXML_PROVENANCE.md](../3dxml/3DXML_PROVENANCE.md).

## Sources consulted

| # | Source | Version / hash | What was used | What was not |
|---|---|---|---|---|
| 1 | Siemens *Parasolid XT Format Reference* (printed April 2008, 137 pp.), retrieved from a third-party mirror: `https://ww3.cad.de/foren/ubb/uploads/schulze/XT_Format_April_2008_tcm73-62642.pdf` | PDF SHA-256 `6a1c3b48aac8577dac96fda4a24b2aca460ff8897fa04915487a93af9ed7792a` (re-verified 15 Nov 2026). Local copy stays under ignored `out/`. | Read in full for: header, text transport, embedded-schema edit scheme, field types, topology, geometry node fields, transform, attribute system, node-type table. Facts (field names, orders, enums) were entered into a **local, untracked** probe. | Not redistributed. No text or tables quoted into tracked files. The mirror's availability is not a licence. |
| 2 | Siemens, Parasolid data access and translation page, `https://www.siemens.com/en-gb/products/plm-components/parasolid/data-access-translation/` | Local HTML copy SHA-256 `e17ffbaa7b58d2aed456539da75ac2f717ebc639809cbcb0d2bb3cd590948d7f` | Statement that XT is the shared exchange format and a translator toolkit is licensed separately. | No terms, EULA or SDK were downloaded or accepted. |
| 3 | Open Cascade, *Parasolid Interface User's Guide* (OCCT-Components repository, `Exchange/Parasolid_Import/Documentation/parasolid_interface.md`) | Read 15 Nov 2026 via a fetch agent; no commit hash or licence statement visible in the text read; hash not recorded | Architectural reference only: entity-to-OCCT mapping, XDE colour/name modes, healing parameters, listed limitations (§ below). | The commercial reader itself was not obtained or run. Only a summary of this guide was read in this pass, so **statements about its content are second-hand** and should be rechecked against the page before use in a decision. |
| 4 | parasolid-kit (MIT AND Apache-2.0), snapshot `2bc959687c8ca87145c4e0594828c3fa11aab912`, notes referenced by Codex | Licence text SHA-256 `f6e2a77034e7fd3c2d82b44600b45bf362d6c9b941299d696a7e234525979291` | Nothing in this pass. Codex used its profile notes to choose a candidate first schema key; that use is recorded in [PARASOLID_XT_PHASES.md](PARASOLID_XT_PHASES.md). | No code was read, copied or run in this pass. |
| 5 | The 12 local `.x_t` samples (neutral IDs in [PARASOLID_XT_FORMAT_COVERAGE.md](PARASOLID_XT_FORMAT_COVERAGE.md)) | Full SHA-256 in the local inventory `out/parasolid-research/inventory.json` | Read-only probe: header, embedded schemas, record decoding, counts. | Never copied, exported or named in tracked files. Redistribution rights unknown. |
| 6 | Siemens *Parasolid XT Format Reference*, **July 2022** edition (owner-supplied PDF, local copy placed by the owner under ignored `out/`; origin of the copy not recorded) | PDF SHA-256 `c1da284cacd299a4d7621b31393ff979b53f6def33615d5499df6c495eca8606`, 1,726,612 bytes | Read for gap checks only (15 Nov 2026): node-type table, unit and resolution recommendations, attribute appendix, embedded-schema text. Its notice is the same family: proprietary, all rights reserved. | No text or tables copied. Field layouts of the 2022 edition were **not** yet transcribed or compared. |
| 7 | parasolid-kit README and docs (`format-support.md`, `builtin-profiles.md`, `PARTIAL_READERS.md`, `LICENSE`), main branch, read 15 Nov 2026 through a fetch agent; parts of the larger docs were seen only as model summaries; no commit hash visible | Not hashed | Context only: its stated provenance and coverage (below). | No code read, copied or run. |

## Not used

No Parasolid kernel, SDK, schema file (`sch_*`), PK documentation beyond the XT reference, decompiled or disassembled code, third-party decoder source or CAD application was used. The probe is original code. It was **not** produced in a formally isolated clean-room process: the same author read the reference and wrote the probe.

## Licensing position (limits stated)

- The reference carries a copyright notice that prohibits reproduction without permission and disclaims warranties. It also states, in its introduction, that the format is published so that applications can access Parasolid models **without necessarily using the Parasolid kernel**, and names translator writers as the main audience. That supports independent **reading** as the intended use. It contains no explicit patent or reader-implementation grant of the kind the JT specification has.
- Consequences adopted: (a) do not paste or closely paraphrase its tables into tracked documents; (b) implementation code may encode field order and types, but a **verbatim-style transcription of its schema tables into tracked source** is a decision for the owner and counsel (an alternative is to build layouts from the files' own embedded definitions plus a minimal base, and to keep the transcribed table as a build-time-generated, untracked input); (c) do not extend the JT patent grant to standalone XT.
- Standalone-XT permissions question from the October note **remains open**. This pass did not resolve it.

## Open Cascade guide: claims relevant to comparison (second-hand)

As reported by the fetch agent from the guide: it covers Assembly, Body and pointer-list roots; maps points, lines, circles, ellipses, planes, cylinders, cones, spheres and tori to the matching OCCT types; maps intersection curves, B-curves, B-surfaces and blends to B-splines (rolling-ball blends built with a pipe construction, with U/V swapped); maps offset, swept and spun surfaces; leaves foreign (PE) curves/surfaces and blend-boundary surfaces untranslated; carries colour and name through XDE when enabled; ignores other attributes; applies shape healing driven by a resource file; and gives tolerance/precision controls. No licence or evaluation terms were visible in the text read.

## What the 2022 reference and parasolid-kit add (15 Nov 2026, observations)

- The 2022 node-type table confirms two of our empirical findings: the helix form numbers (163 and 184) and the in-stream node 204 (intersection data) are listed, and it lists newer types (polyline, mesh, lattice and others). Node type **151 is still not listed** there.
- The 2022 text recommends one metre as the default unit and gives the resolution values we saw in every file. This is an authoring recommendation, not a statement that every file is in metres.
- The attribute appendix defines the name attribute (legal on parts, bodies, instances, faces, edges and more, one string field), a second colour attribute, layer, a second translucency and a scale-factor attribute. Our "name attribute meaning" gap is therefore partly closed by the document, but a control file is still needed.
- parasolid-kit says its layout knowledge comes from the April 2008 reference, public and self-made files, and local "exact-key catalogs" used as comparison oracles whose origin the docs do not state. It lists a legacy `SCH_800000_8008` profile of 47 types. **Its provenance for that profile is unclear**, so it cannot be treated as a clean source for us. It declares MIT with Apache-2.0 adopted readers (no NOTICE file found) and makes no statement about the legal status of reading XT.

## AI-generated guide and its links (15 Nov 2026, observations)

The owner supplied a PDF transcript of an AI chat (local, `out/parasolid-research/`, SHA-256 `f418f8c4ed063819041583892c01240f798a52552393b95aa5d54c87db24f3e9`). It is **not an authority** and is not cited for any fact. Checks against what we verified: it calls parasolid-kit a C++ parser (the project is a Rust core with a Python package); it states unsourced claims about JT segment layout and ISO section numbers (not verified here); and its legal statement that reading needs no licence and has no inherent restrictions has no source. Links followed, through a fetch agent:

- **ps-parser** (MIT, Python, X_B only): read. It **bundles a Siemens-style base schema file** (`sch_13006.s_t`) and gives no origin for it. That file is the kernel-distribution schema the reference says is not needed to read XT. Do **not** use or study it until its origin is known.
- **JT-Assistant** (Open Cascade, GPL-2.0 per its licence file, which we have not checked for "or later"): only the top level was read; no XT code was visible. Compatibility with GPLv3 is unchecked.
- FreeCAD forum thread: blocked by an access-control page, unread. q-solid.com: connection refused, unread. ISO 14306 page: HTTP 403, unread. Spatial and CAD Exchanger pages: marketing text without licence terms (see above).

## Tracked tooling and the layout table (15 Nov 2026)

At the owner's direction, research tools and scripts are tracked. The decoder `tools/parasolid/xt_decode.py` is original code. The hand-entered base field layouts (derived from source 1) are deliberately **not** tracked: they live in the untracked `out/parasolid-research/layouts-v13.json`, so no transcription of the reference's tables enters the repository. A production importer must resolve the same question again (implementation plan §6) before shipping.

## Review confirmations (15 Nov 2026, Codex)

Codex confirmed that its October edit to the scoping note only removed private sample names and absolute local paths, and that both local probe scripts match the SHA-256 hashes then given in the coverage document (since superseded by the tracked `xt_decode.py`, which reproduces the same results). The probe was not rerun in that review; its results remain exploratory.

## Where this document must be updated

If any of the following happen, extend this table before relying on the result: a second Siemens document is consulted (for example the JT reference's XT annex, which is where a pre-V13 layout might be found); any third-party code is read or run; a vendor package is downloaded; or probe code is moved into `tools/`.

## JT 10.6 Revision D, Annex F (read myself, 15 Nov 2026)

Source: Siemens JT V10.6 Format Description Rev D (10 Feb 2026), local PDF SHA-256 `30e421356e67cfdee1860916f401b633639a3e8651817dd8ae6a360a5f43381a`; text extraction under ignored `out/jt-research/`. Annex F (about 18 pp. of XT description plus node tables) was read for its structure and for the licence terms on its first pages. Observations:

- **Annex F is a full XT format description, not a pointer.** It states the XT B-Rep segment (segment type 17) is a neutral binary definition, that "a proprietary kernel is not required" to read it, and that the transmit format value can only be neutral binary. It carries the same logical layout, embedded-schema edit mechanism, field types, topology and geometry definitions, and node tables as the standalone reference. It confirms Codex's point: **JT-embedded XT is neutral binary, not text.**
- **Real evidence:** a locally extracted XT element from a JT sample (`out/jt-research/bolt-xt-element.bin`) begins with the neutral-binary flag and an embedded-schema key over the V13 base, as Annex F describes.
- **Node table.** Annex F lists types up to 229, including 163, 184 and 204 as in the 2022 reference. **Type 151 is not listed there either.** It does not cover the legacy unsuffixed keys 8008 and 9008; its example schema key is unsuffixed (`SCH_3210000_32101`), so JT files can carry an unsuffixed key whose layouts must then come from this annex.
- **The grant (first pages).** Siemens gives copyright permission to write and distribute software that accepts input in the JT Data Format and interprets it, and to copy its "list of data structures and operators" **to the extent necessary** for that purpose, **provided an appropriate copyright notice is included**. It excludes copying the document itself or Siemens software, excludes Siemens patents except as an official patent clarification notice permits, and states that the permissions do **not** include incorporating the JT format, structure or schema into another data format, structure or schema.

Consequences (for counsel to confirm): for **JT-embedded XT**, the layout table can plausibly be sourced from Annex F under this grant, with a copyright notice, and tracked. The same table used for **standalone `.x_t`** goes beyond the JT grant's stated purpose, so standalone use still rests on the open XT questions. Whether Annex F's XT description counts as part of the JT Data Format for the grant is a question for counsel, as is the limited patent grant's reach to XT.

## Open release gate: Siemens clarification (owner decision relayed 15 Nov 2026)

The legal clarification with Siemens is **deferred until release and does not block development**. It is an open gate and must be answered, or consciously accepted by the owner, before any release or distribution:

1. Whether the Annex F layout table may be reused for standalone `.x_t` (the JT grant is stated for the JT Data Format).
2. Patent scope: the JT limited patent grant's reach to XT content, and any covenant for standalone XT.
3. Use of Siemens entity and field names in source code and documentation.
4. Fit of the above with distribution under GPLv3.

The request draft is `out/parasolid-research/siemens-request-draft.md` (local, not tracked). Counsel review of the minimal layout tables and their provenance is recommended at the same time.

### Two table sources, kept separate

| Table | Used for | Derivation | Tracked |
|---|---|---|---|
| `src/Import/Parasolid/XtJtLayouts.cpp` | XT data **embedded in JT files** (neutral binary) | Base layouts observed in real XT streams (edit scripts must consume them exactly) and cross-checked by field name and order against Annex F of the JT 10.6 Rev D reference (50 entries matched automatically; others read by hand or differing because Annex F describes a newer schema than the V13 base). Carries the Siemens copyright notice required by the JT grant. | Yes |
| Local layout file `out/parasolid-research/layouts-v13.txt` | **Standalone `.x_t`** text files | Observed in the local sample files, with the 2008 and 2022 references consulted for names and order | No |

The cross-check is reproducible: `python -I tools/parasolid/verify_layouts_annexf.py <layouts.txt> <annex-text.txt>` (the annex text is a local extraction of the JT PDF). Four foreign-geometry node types are absent from the tracked table because they were not matched in Annex F.

## October 2006 reference (added 15 Nov 2026)

Owner-supplied local copy, not tracked: *Parasolid XT Format Reference*, October 2006 edition (UGS Corp. notice,
all rights reserved), SHA-256 prefix `27dadddbfcfcdb46`, 363,627 bytes, read in full text form by the agent.

- It describes the schema-file / embedded-schema mechanism against the V13 base `SCH_13006` and the same node and
  field tables as the later editions for the types the importer uses.
- Its cone formula is the same as the 2022 edition's (`R = P - vA + ...(r + v tan a)`); the local samples
  contradict that sign (measured in [PARASOLID_X3_STATUS.md](PARASOLID_X3_STATUS.md)), so the discrepancy is in
  the documented formula across editions, not a later correction.
- It does **not** document the unsuffixed older schemas (`SCH_..._8008`, `_9008`) used by the three legacy sample
  files, and it names no node types 151 or 152. The three legacy files therefore stay refused.

## OCCT topology source comparison (15 Nov 2026)

Read the vcpkg source extraction of OCCT 8.0.1 for its STEP and IGES topology construction and public B-Rep APIs. The [comparison](PARASOLID_OCCT_TOPOLOGY_COMPARISON.md) lists the source URL, archive hash, files consulted and scope of the resulting XT change. No translator code was copied or executed for this pass.
## Parasolid-kit geometry comparison (17 November 2026)

On `feature/parasolid-importer`, reviewed the [upstream source at b91972a1ff86d8c8b55b07516923f7c35bfedf77](https://github.com/monozukuri-ai/parasolid-kit/tree/b91972a1ff86d8c8b55b07516923f7c35bfedf77). The distribution declares MIT AND Apache-2.0. This was a static review of its Rust text parser and Python OCCT conversion, compared against the existing local observations for `XT-ed43d4c8caf1`. No upstream code or schema declarations were imported, and the kit was not built or run. The review establishes implementation provenance and does not establish legal clearance.

- The [text parser](https://github.com/monozukuri-ai/parasolid-kit/blob/b91972a1ff86d8c8b55b07516923f7c35bfedf77/crates/parasolid-core/src/text_document.rs) reads embedded declarations and resolves inherited types through a base catalog. Loading the local sample with the kit remains unverified.
- The [geometry converter](https://github.com/monozukuri-ai/parasolid-kit/blob/b91972a1ff86d8c8b55b07516923f7c35bfedf77/src/parasolid_kit/interop/occt/geometry.py) maps a linear sweep directly to an OCCT extrusion. It constructs equal-radius, unit-weight rolling-ball and cliff-edge blends by sweeping a circle about the source spine, then uses the face loops to trim the carrier. It rejects a pipe approximation outside its error budget and handles periodicity for closed pipes.
- The [parametric converter](https://github.com/monozukuri-ai/parasolid-kit/blob/b91972a1ff86d8c8b55b07516923f7c35bfedf77/src/parasolid_kit/interop/occt/parametric.py) uses intersection-chart samples and authored endpoints to constrain contact curves. For a zero-radius supporting blend it uses the support spine as the cliff edge. Local face 4900 has this construction, equal 2 mm offsets and unit weights. A candidate local implementation would need to qualify the inferred spine against the authored rails, validate the selected periodic trim and verify that the final triangles reach both neighbours.

### Local execution (17 November 2026)

Ran the published Windows wheel for parasolid-kit 0.3.17, the release tag at the inspected revision, with CPython 3.13.2 and cadquery-ocp-novtk 7.9.3.1.1. With its default provider, `check` stopped at base node type 151. Pointing it at the installed Solid Edge 2026 catalog in place (`sch_13006.sch_txt`, SHA-256 `0dd291ea706fc306f16a78140e05e595e75c85ab63e4077e68941b127fcfdcc3`) yielded complete source mapping of 138 bodies and 18,385 faces. No catalog content was copied into the repository.

An ignored local driver selected body 919 by raw node index and ran the installed kit's parametric topology builder in partial mode at metres to millimetres. It converted 159 of 169 faces. Face 4900 was rejected at a curve-less edge requiring trimmed surface-parametric fins; 4891, 4907, 4918 and 4380 were also dropped. Face 3822 meshed. The partial OCCT shape was saved as B-Rep and written to STEP with OCCT's As-Is writer, with no round-trip validation. No full-assembly OCCT conversion was attempted.

```powershell
$outDir = 'out/parasolid-research'
& "$outDir/parasolid-kit-venv/Scripts/parasolid-kit.exe" check $sample --schema-dir $catalogDir --json > "$outDir/parasolid-kit-cbr-catalog-check.json"
& "$outDir/parasolid-kit-venv/Scripts/python.exe" "$outDir/parasolid-kit-exhaust.py" $sample $catalogDir (Resolve-Path $outDir).Path *> "$outDir/parasolid-kit-exhaust.log"
```

## Other public XT reader strategies (17 November 2026)

On `feature/parasolid-importer`, compared public reader documentation with local source records for `XT-ed43d4c8caf1`. No third-party implementation was built or incorporated in this pass.

- [Open CASCADE Parasolid Interface guide at 8a10cac](https://github.com/Open-Cascade-SAS/OCCT-Components/blob/8a10cac19240b1dc4ca70612288625bde4c62d97/Exchange/Parasolid_Import/Documentation/parasolid_interface.md#special-cases). The commercial component documents `GeomFill_Pipe` with a computed spine and two supporting contact curves. For surface intersections it computes candidate branches with `GeomInt_IntSS`, selects the nearest branch and falls back to a chart-point approximation. The guide documents subsequent pcurve and tolerance repair. It gives no qualification result for the local exhaust.
- [CadConvert XT engineering notes at d5d9a1b](https://github.com/omerbasavul/cadconvert/blob/d5d9a1b9948c575fd46b22840971360c67878362/docs/parasolid-xt-notes.md). The MIT project documents a second approach to obtaining blend contact rails. It traces a distance-to-second-support level set in the first support's parameters, then builds circular sections between contacts. It distinguishes grids constrained by contact solves from radius-only grids and boundary patches. For curve-less tolerant edges it tries both fins and reports that the alternate fin can recover edges whose first support cannot be evaluated. Its published measurements describe its own corpus.
- [akiselev/xt-parser at eced07b](https://github.com/akiselev/xt-parser/tree/eced07bacc0ef7486690092d35816e8118486854) reads transmit text into a B-Rep intermediate representation. Its public entry point and notes establish parser/topology comparison value. Reuse terms and provenance were not verified.
- [HOOPS Exchange Parasolid Reader](https://docs.techsoft3d.com/exchange/2024.6.0/start/format/parasolid_reader.html) exposes geometry evaluators and NURBS approximation. Its public guide documents coverage rather than the reconstruction algorithm, so the SDK is a possible geometry comparator.

The local decoder shows that both fins of curve-less edge 6450 on face 4900 reference SP-curves on blend 4920 itself, and neither names an original 3D curve. Trying the other fin cannot supply an independently evaluable support here. The extracted endpoints are about 0.048 mm apart. A candidate repair is to reconstruct this seam section against the blend spine and both authored endpoints, attach the two periodic pcurves to a shared edge, and check each pcurve's residual as well as the two long trim rails. This is an untested hypothesis.
