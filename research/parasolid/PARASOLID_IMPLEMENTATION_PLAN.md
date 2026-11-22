# Parasolid X_T importer: staged implementation plan

15 November 2026, branch `feature/parasolid-importer`. A plan, not an implementation. Evidence behind it: [PARASOLID_XT_FORMAT_COVERAGE.md](PARASOLID_XT_FORMAT_COVERAGE.md). Validation: [PARASOLID_VALIDATION_PLAN.md](PARASOLID_VALIDATION_PLAN.md). Sources and limits: [PARASOLID_PROVENANCE.md](PARASOLID_PROVENANCE.md). Complements [PARASOLID_XT_PHASES.md](PARASOLID_XT_PHASES.md) (Codex's gates) and the shared-module sketch in [JT_PARASOLID_IMPLEMENTATION_SCOPE.md](../jt/JT_PARASOLID_IMPLEMENTATION_SCOPE.md).

## 1. Recommendation

Build a native C++ text X_T reader in `namespace Xt` (`include/Import/Parasolid`, `src/Import/Parasolid`), schema-driven rather than hand-coded per record, producing an OCCT-independent source model first and OCCT shapes second. Target **embedded-schema files (V13 base) first**: they carry their own edit scripts, 8 of the 9 local examples decode fully with a throwaway probe, and they include every local exporter family except the legacy-key SolidWorks files. Treat legacy-key files as a separate, later, evidence-gated stage. Keep `.x_b` behind the same record interface. Do not ship until the standalone-permissions question is answered.

A change from Codex's earlier proposal: Codex suggested a first target of the exact key `SCH_3000000_30000` (documented by a third party). The local evidence says the **key is not the unit of dispatch**: all 9 embedded files use the same base and differ only in their edit scripts, and the dispatch rule "exact full key" would reject every local file. Dispatch on *transport form* (embedded over base 13006, legacy unsuffixed, binary) and let the file's own script define the layout. The exact key remains a diagnostic and a regression label.

> **Codex recommendation, 15 Nov 2026 (recommendation only; the owner has NOT yet decided).** Choose text X_T with embedded schema edits over base 13006 as the first target, since the local probe reaches the terminator on 8 of 9 files in that family. The proposed fixed key `SCH_3000000_30000` has no local sample. Select the reader by transport, resolve layouts from the qualified base plus each file's edits, and keep the full internal key in diagnostics and regression records. The file that stops at node type 151 is a concrete refusal case. The probe results are exploratory; it was not rerun for this note.

## 2. Architecture

| Component | Responsibility |
|---|---|
| `Header` | Common header and prefix: modeller version, internal schema key, user-field size, optional base suffix and node-type limit. Tolerates missing keys and non-ASCII bytes. |
| `TextTokenizer` | Newline/trailing-space normalisation, space-terminated numbers, `c`/`l` single characters, `?` unset with no trailing space, short strings, escapes. Bounded, restartable, no whole-file copy where avoidable (115 MB files exist). |
| `SchemaBase` | The V13 base layouts (see §6 on how they are produced). Immutable, versioned. |
| `SchemaEdit` | Applies copy/delete/insert/append scripts to produce each type's effective fields; **asserts the script consumes the base exactly**; supports types new in the file. |
| `NodeReader` | Generic reader driven by effective fields (scalars, arrays, variable-length last field, vectors, intervals). Produces typed nodes keyed by index, with unresolved-reference accounting. No semantic knowledge. |
| `SourceModel` | Typed, OCCT-free graph: parts, assemblies, instances, transforms, bodies, regions, shells, faces, loops, fins, edges, vertices, geometry, attributes. Preserves source node ids. |
| `GeometryMapper` | Source curve/surface to OCCT geometry. Analytic first; B-spline next; procedural last. |
| `TopologyBuilder` | Shared vertices and edges, oriented fins, ordered loops, faces, shells, region/void handling, sheet versus solid. |
| `XdeBuilder` | Assembly definitions and instances, names, colours, units; hands off to the existing XCAF-to-scene stage. |
| `Diagnostics` | Typed diagnostics with node index and cause; a result is "complete", "partial" or "failed", never silently successful. |

Registration: an `XCAFParasolidProcessor` behind `XCAFDocProcessorFactory`, as the JT scope document proposes, so the existing XCAF stage and scene conversion are reused. The current processors return only an `aiScene*` and close the document; retaining exact shapes needs an owned result sidecar and is **out of scope** until the owner decides it is a product requirement.

## 3. Stages and gates

Each gate is a point where **the owner builds and/or tests and confirms**, per `AGENTS.md`. No stage starts before the previous gate is confirmed.

| Stage | Content | Output | Gate (owner confirms) |
|---|---|---|---|
| X0 | Decisions: standalone permission, whether transcribed layouts may be tracked (§6), units policy, legacy-key policy. Corpus completion (§7). | Written decisions in the research folder | **G0**: answers to the open questions in §8 |
| X1 | Header, tokenizer, schema edit, generic node reader, source model skeleton, diagnostics. No OCCT. | Unit tests with synthetic fixtures; probe-equivalent inventory CLI | **G1**: owner builds/tests; all 8 decodable local files reach the terminator with exact stream consumption; hostile-input tests pass (truncation, huge counts, bad edit op, wrong type) |
| X2 | Source-model validation: reference resolution, ring/chain consistency, loop/fin/edge/face counts, ownership. Per-file statistics report. | Validation report under `out/` | **G2**: counts match the independent recomputation (§ validation); legacy-key and unknown-type policy confirmed |
| X3 | OCCT topology for **analytic** solids: points, lines, circles, planes, cylinders, cones, spheres, tori; ordered fins; shared edges; no healing. | Shapes with expected closure | **G3**: `BRepCheck_Analyzer`, bounds, area, volume agree with authored/STEP controls on the Onshape-class small sample and the new controls |
| X4 | B-spline curves/surfaces (rational, periodic, knot multiplicities), trimmed curves, SP-curves/pcurves, intersection charts treated as **qualified approximations**, recording source chart error and measured reconstruction error separately (§4). | Shapes for the mid-size files | **G4**: per-file closure and measured deviation report; owner view check |
| X5 | Procedural surfaces: offset, swept, spun, **rolling-ball blends** (very common locally), helix forms. Admit only mappings that pass distance checks; otherwise emit an explicit unsupported diagnostic and keep a documented fallback. | Wider coverage | **G5**: coverage table accepted, including what is deliberately skipped |
| X6 | Assemblies and transforms, names, colours, units, sheet/wire handling; XDE, then viewer integration via the existing factory; cancellation, reload, progress units. | Viewer import path | **G6**: owner opens the local assemblies; placement, colours, instancing confirmed |
| X7 | Legacy-key schemas (if a published or lawfully derivable layout exists) and unknown node types. | Extra files readable or clearly refused | **G7** per layout family |
| X8 | `.x_b`: neutral and bare binary, short-index encoding, reusing `NodeReader` through a second tokenizer. | Binary path | **G8** with genuine binary samples |

**Owner requirement (15 Nov 2026): the same XT reading must also serve JT files that embed XT B-Rep**, and the whole effort must avoid infringement (see the provenance document). Embedded JT XT (JT 10.x) shares `SourceModel` and below, not the transport. Keep its header and segmentation separate.

## 4. Topology and geometry mapping (design)

- **Parts**: root node decides: body, assembly, or pointer-list of parts. A file with several roots yields several top-level parts.
- **Topology**: build from fins. Each edge has two fins (one possibly dummy); a loop is an ordered ring; the sense chars on face, surface and curve combine (face normal parallel to surface normal iff both or neither senses are positive). Never rely on node order.
- **Bodies**: map by body type. Solid → solid or shell by region content; sheet → shell/face compound; wire → wire; general → refuse with a diagnostic initially. Do not label every body a closed solid. Void regions bound the solid regions, one infinite void per body.
- **Tolerance**: vertices and edges carry optional tolerances (unset means half the modeller resolution). Preserve them. Resolution values (size box 1000, linear 1e-8) are read from the body.
- **SP-curves and fins**: tolerant edges carry per-fin SP-curves on the face surface; map to 2D curves on surfaces. Accurate edges: 3D curve only, build pcurves via projection with a recorded approximation.
- **Intersection curves**: file stores a chart (piecewise-linear approximation with errors) plus limits. Convert to an interpolating B-spline and record **two separate quantities**: (a) the **source chart error**, the chordal/angular error the file itself carries (it may be unset), and (b) the **measured reconstruction error** of the interpolated curve against the chart points and, where available, against an independent reference. Interpolation can add its own deviation, so never merge them into one tolerance and never report (a) as the achieved accuracy. Do not claim exactness.
- **Blends/offset/swept/spun**: first measure how often each is *edge-adjacent* (needed) versus unused; admit by evidence, as in X5.
- **Foreign geometry (PE)**: did not occur locally; diagnose and skip.
- **Transforms**: x' = (R x + t) · scale with a flag word. Local files carry only translation and rotation. Reject scale/reflection/general-affine initially with a diagnostic, then add rigid-plus-mirror via OCCT transformation types only after a control exists. Never coerce.
- **Healing**: none in X3. Any later sewing, fix or tolerance increase is optional, logged and bounded; reject unbounded inflation.

## 5. Assemblies, attributes, colour, units, diagnostics

- **Assemblies**: assembly nodes hold instances; instances hold part references and optional transforms; parts are shared. Map part definitions once, instance by location. Cycle detection and depth limit; count definitions, instances, and render occurrences separately for progress.
- **Names**: a name attribute exists in every embedded-schema sample, but the reference does not define its meaning. Treat as a hypothesis (attached to parts, bodies or instances, carried in a character-value node) and settle with the controls in the validation plan before use.
- **Colour**: system colour attribute with three reals in 0–1; present on faces and edges, possibly on bodies. Resolution order (face over body) must be confirmed on a control with known colours. Translucency/reflectivity are separate attributes and optional.
- **Other attributes**: density, hatching and the producer-specific ones are read through their self-describing definitions and ignored by default. An unknown attribute is never an error.
- **Units**: no source in the reference. Policy: default to metres as an explicit, logged assumption **only after** a control with known dimensions confirms it; expose a user override; record the unit evidence in the result. Do not silently scale.
- **Diagnostics**: for each file report transport form, internal key, decoder status, counts per node type, unknown types skipped by structure, unsupported geometry kinds with counts, body types, tolerance statistics. Reject on truncation, bad edit script, unknown *required* base layout, or a stream that does not end at the terminator.

## 6. Where the base layouts come from (owner decision 15 Nov 2026: do not track the transcription)

The V13 base layouts are field names and types in a fixed order. Options:

1. **Transcribe into tracked source.** Fastest. Reproduces the structure of a copyrighted publication in code; needs owner/counsel approval. Mitigation: the reference states that readers are the intended audience.
2. **Generate at build time from a local, untracked copy of the reference.** Keeps the document out of the repo, but makes builds depend on a file the owner holds, which breaks clean checkouts and CI.
3. **Derive from the files' own embedded definitions.** Each file's edit script names the fields it inserts and appends, so a *minimal* hand-written base (only the fields every script copies) plus the in-file definitions covers the types that carry scripts. Records whose script is the "unchanged" flag still need the base layout, so this does not eliminate the table for common types.

Decision (owner, 15 Nov 2026): the transcription is **not** tracked. Current tooling follows option 2 (local untracked layout file). The importer's shipping route remains open: option 3 plus a minimal table, or a reviewed option 1. This stays a Gate G0 item.

## 7. Gaps that block stages, and what would close them

| Gap | Blocks | What closes it |
|---|---|---|
| Three legacy-key files (8008, 9008) | X7 only | A published layout for those schema numbers, or a re-export of the same models with an embedded or V12/V13 schema. Check whether the JT reference's XT annex documents older layouts before any inference. |
| ~~One node type (151) absent from the reference, seen once in one file~~ **Resolved 15 Nov 2026 (observation, one sample):** types 151 and 152 are the analytic-form nodes that a B-surface's SURFACE_DATA points to when `analytic_form_type` is `C` (cylinder) or `N` (cone). Observed once each in one file; the layouts (point, axis, radius[, sin/cos half-angle], sense char) were inferred from the stream and confirmed by the file then decoding to its terminator (645,928 records). Neither the 2022 reference nor Annex F documents them. Sphere/torus/plane forms (153/154/150) were not observed and are not added. Layouts stay in the local, untracked file. |
| No sample with scale/mirror/affine instance transforms | X6 | A small authored mirrored-instance control (§ validation). |
| No sample with wire or general bodies | X3–X6 | Authored controls. |
| No independent reference geometry for any local file | G3 and later | Same-model STEP exports and authored dimensions (the evidence gap Codex already recorded). |
| Units | X6 | A control with known dimensions. |

## 8. Questions for the owner at G0

1. May field layouts be transcribed into tracked source (option 1), and is a formal review of the reference's notice wanted before that?
2. Is the standalone-XT permissions position acceptable to ship under, or must it be resolved first?
3. Are legacy-key SolidWorks files in scope (they are three of twelve)? If yes, is a re-export request acceptable instead of inference?
4. Should an unsupported or approximate construct (blends, intersection charts) fail the whole import, or import with a visible warning and a tolerance report?
5. Is retention of exact shapes for later operations a product requirement now, or deferred?

## 9. Owner decisions at G0 (15 Nov 2026)

| Question | Decision | Effect |
|---|---|---|
| Standalone X_T permissions | ~~Resolve first~~ **Changed 15 Nov 2026 (later): proceed, resolve before release** | Development and tests proceed under the hygiene rules (original code, no Siemens schema files, no copied tables, neutral trademark use). The permission and patent question must be settled and recorded in the provenance document **before any release or distribution**. The earlier "resolve first" answer was reconsidered after the risk discussion of that date. |
| Source of base layouts | Minimal hand-written table plus each file's in-file definitions | Replaces the earlier "transcribe" option; types whose script is the unchanged flag still need a base entry, to be kept to field names and types only and reviewed against the provenance statement. The untracked local layout file remains the development aid. |
| Legacy-key files (8008/9008) | Refuse with a precise diagnostic; ask for re-exports | Stage X7 is dropped from the first release scope. |
| Approximate or unsupported geometry | Import with a warning and a report of skipped parts and measured tolerances | Diagnostics must list skipped constructs and the two separate tolerance quantities (§4). |
| Exact-shape retention | Assumed deferred (display-only, as STEP) | Not asked explicitly; revisit if wanted. |

Implementation may proceed from X1, test-only and not wired into the application until gate G1. Release remains blocked on the first row.

## 10. Codex review points (15 Nov 2026)

Recorded from the Codex review; see [PARASOLID_XT_IMPLEMENTATION_RIGHTS.md](PARASOLID_XT_IMPLEMENTATION_RIGHTS.md) for its sources and hashes. This is a documentation-only review: no signed agreements were examined and no patent search was done.

- **Proceed with an independent reader; seek targeted IP review before publishing.** Counsel should review the proposed minimal base layout, its provenance, any applicable agreements and relevant patent claims before release. The transcribed table stays local until then.
- **Correction to the AI guide and to our earlier wording on JT.** JT 10.6 Annex F specifies **neutral binary** XT in segment 17. Decompressing the segment does **not** yield a standalone `.x_t` text stream. Embedded JT therefore needs the **binary transport reader** (stage X8), sharing only the normalised record and geometry layers. This raises the priority of X8 for your JT requirement: standalone text X1 does not by itself deliver JT-embedded XT.
- **Rights scope.** Siemens' conditional JT reader permission and limited patent grant apply within their JT scope. Whether they extend to XT content in JT is still a question for counsel.
- **Legal statements in the AI answers are not authority.** The claims that interoperability layouts are categorically patent-free, and that US law requires a strict clean room, are unsupported. US copyright excludes functional methods (17 USC 102(b)); the EU *SAS Institute* judgment supports independent compatibility while leaving original expression protected; patent and contract questions remain.
- **Document the source of each wire-format fact** used in the layout table.

Plan consequence: after G1, consider doing the neutral-binary tokenizer (X8) before the OCCT stages if JT-embedded XT is the priority. Not yet decided.

## 11. After reading JT Annex F (15 Nov 2026)

Read directly (see the provenance document): Annex F is a full neutral-binary XT description under the JT reader grant. It changes the plan as follows.

- **Layout source for JT-embedded XT.** The base and current layouts for the JT path can be taken from Annex F, with the Siemens copyright notice, within the grant. This is a cleaner route than the 2008 reference for that path, and it makes a **tracked** layout table plausible for it, subject to counsel confirming the scope.
- **Standalone `.x_t` remains separate.** The standalone text path should not silently reuse a table whose licence basis is the JT grant. Keep the two table sources distinguishable until counsel advises.
- **Order of work.** JT-embedded XT needs the neutral-binary tokenizer (X8) and the layouts for embedded schemas over base 13006 plus unsuffixed current keys. Since this is the owner's stated requirement, consider doing X8 on Annex F layouts **before** the text OCCT stages. Not decided.
- **Still open:** type 151 (absent from Annex F and from the 2022 reference), legacy 8008 and 9008 (absent from both).

## 12. Direction taken after the owner's relay (15 Nov 2026)

The owner (relayed) deferred the Siemens clarification to release as an open gate (see the provenance document), and asked that the Annex F table stay tied to the JT-embedded path and standalone layouts come from observed files. The owner has **not yet answered** the order question; the agent proceeded on its own recommendation: **neutral-binary reader (X8) and the Annex F-checked table for JT-embedded XT first**, since JT is the stated requirement. The owner can redirect.

Done (uncommitted, not built): `readNeutralBinary` in the shared record decoder, the tracked JT table `XtJtLayouts`, binary tests, a `--jt-binary` mode in the inspector, and `tools/parasolid/xt_decode_binary.py` as a reference decoder. Details and the G1 checks: [PARASOLID_X1_STATUS.md](PARASOLID_X1_STATUS.md).

Evidence: the reference decoder reads the local JT-embedded XT element (906 records) to its terminator with the tracked table. Only one JT XT sample is local; more are needed.
