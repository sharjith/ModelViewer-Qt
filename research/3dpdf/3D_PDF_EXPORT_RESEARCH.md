# Save as 3D PDF: groundwork research

Status: research only, written 3 November 2026 on `feature/JT-importer`. No application code, no builds, no downloads, no commit. Nothing below was tested against a real Acrobat install; every statement is tagged as one of:

- **[Obs]** what a fetched or searched source says (URL given),
- **[Recall]** from memory, not verified against a source in this session,
- **[Hyp]** hypothesis or inference,
- **[Rec]** recommendation.

Owner background: a STEP-export / 3D-PDF-composer path for measurements was investigated earlier and deferred (reasoning not recovered; this document re-investigates rather than assuming it).

---

## 1. The two embedding formats

A 3D PDF is an ordinary PDF containing a **3D annotation** (`/Subtype /3D`) whose `/3DD` entry is a stream holding either U3D or PRC data, plus optional `/3DV` view dictionaries and `/3DA` activation settings.

- **[Obs]** The 3D annotation is defined in ISO 32000-1:2008 (PDF 1.7) and ISO 32000-2:2017 (PDF 2.0). `3DD` is required and points at the 3D stream; `3DV` selects the initial view (a view dictionary, or an index/name into the stream's view array); `3DA` controls activation. Adobe's supplement adds extension-level entries (e.g. a `3DU` units dictionary, ExtensionLevel 3), and a PDF that embeds PRC must carry an Adobe Extensions dictionary in the catalog. Source: Adobe SDK docs, <https://opensource.adobe.com/dc-acrobat-sdk-docs/library/plugin/Plugins_3D_samples.html>, and abcpdf reference pages (search result summaries, not read in full).
- **[Obs]** PDF 1.7 introduced interactive 3D using U3D; PDF 2.0 extended 3D to PRC, STEP AP242 and glTF 2.0 (summary of a PDF Association item reached through a search result, <https://pdfa.org/resource/3d-formats/>; page not read in full).

### 1.1 U3D (ECMA-363)

- **[Obs]** Standardised by ECMA as ECMA-363, first edition December 2004; 4th edition June 2007 is the latest. It stores vertex-based geometry, colour, textures, lighting, bones and transform-based animation; edition 3 added vendor blocks (RHAdobeMesh, a more compressed mesh block); edition 4 added curved-surface (free-form) primitives. Sources: <https://en.wikipedia.org/wiki/Universal_3D>, <https://ecma-international.org/wp-content/uploads/ECMA-363_4th_edition_june_2007.pdf> (table of contents seen: view node, light node, line set, point set blocks). The ECMA PDFs are freely downloadable.
- **[Hyp]** It is a mesh/line/point format with a node hierarchy; there is no analytic B-Rep and no PMI/semantic data. The sources read do not say "no B-Rep" explicitly; this is an inference.
- **[Hyp]** Fits our data directly: triangle meshes, per-mesh materials, node tree, line sets for measurement graphics, view nodes.
- **[Recall]** Historically the most compatible format; Acrobat 7/8 era workflows used it. PMI would have to be drawn as lines/text geometry or as PDF-side annotations.

### 1.2 PRC (ISO 14739-1:2014, "PRC 10001")

- **[Obs]** ISO 14739-1:2014 "Document management - 3D use of Product Representation Compact (PRC) format - Part 1: PRC 10001", reviewed and confirmed 2021; a revision (ISO/CD 14739-1.3) is listed as in development. The standard text is paywalled. Sources: <https://www.iso.org/standard/54948.html>, <https://scc-ccn.ca/standardsdb/standards/8151929>.
- **[Obs]** PRC can store a tessellation only or exact B-Rep. Origin: TTF (2003), acquired by Adobe 2006. Adobe's older specification (version 8137) is archived at web.archive.org (link on <https://en.wikipedia.org/wiki/PRC_(file_format)>); not fetched, so freely-readable status is **unverified**.
- **[Obs]** HOOPS Exchange documentation lists PRC as the native model format and states the API gives access to B-Rep, PMI (visual and semantic), model tree, views, styles. ODA Publish describes "PMI including dimensions and tolerances" and "custom views". (Sources in section 2.)
- **[Hyp]** Strictly richer than U3D: assembly tree with product occurrences, per-node styles/materials, tessellation and B-Rep, semantic PMI, saved views, markups. For our data (meshes plus recorded and measured dimensions) the relevant parts are tessellation, product occurrences, materials/styles, camera/view objects and markup/annotation items.
- **[Obs]** Asymptote's open PRC writer implements: wire tessellation/polylines, tessellated faces (`PRC3DTess`), poly-B-Rep models, point sets, generic materials (ambient/diffuse/emissive/specular/shininess/alpha), textures, line styles, product occurrences/part definitions with transforms, B-Rep entities, and markup/annotation-item serialisers; only camera and scene-display type ids were seen, no view writer found (see 2.3).

### 1.3 Which format for what

| Need | U3D | PRC |
|---|---|---|
| Triangle meshes, colours, opacity | yes | yes |
| Assembly tree / transforms | node tree | product occurrences + transforms |
| Exact B-Rep (STEP-like) | no [Hyp] | yes [Obs] |
| Semantic PMI | no [Hyp] | yes via commercial SDKs [Obs] |
| Measurement graphics as lines/text | line sets (text must be vector-drawn) | polylines/markup |
| Open-source writer exists | yes, aged (2.2) | yes, Asymptote (2.3) |
| Freely readable spec | ECMA (free) | ISO (paid); Adobe older spec [unverified] |

---

## 2. Writer options

### 2.1 Fit with the existing build

**[Obs]** `vcpkg.json` baseline `ddd0023b...`; dependencies include Qt 6 base/widgets, assimp, Open CASCADE, CGAL, Eigen, zstd, OpenEXR, embree, OIDN, minizip (via CMake `find_package`). zlib is already available transitively [Hyp: via Qt/assimp/minizip; not confirmed from the lock]. Linux builds are supported (WSL / .deb), so a solution must build on both platforms. An `overlay-ports/` directory exists, so a project-local port for a small library is feasible.

### 2.2 U3D library (Intel/ECMA reference code, `u3d`)

- **[Obs]** SourceForge project "Universal 3D Sample Software": license Apache-2.0; last update 2016-05-01; set of libraries to write/read/render U3D per ECMA-363; two user reviews (3/5) cite poor documentation and memory issues. <https://sourceforge.net/projects/u3d/>
- **[Obs]** GitHub fork `DmytroSytnyk/u3d`: Apache-2.0, 17 commits, port of Intel U3D v1.4.5 to GNU build system and CMake, render part removed; separates `libIDTF` (library built from the IDTFConverter, dropping the IDTF text parser) so U3D can be written from code; bundles libpng, jpeg, zlib; build claims cover VS2008-2015, Linux and macOS x64; default quality is maximum, i.e. mesh compression suppressed; unreleased/untagged. <https://github.com/DmytroSytnyk/u3d>
- **[Obs]** No vcpkg port found: the vcpkg.link URL returned 404 and the ports listing could not be fully read, so **unverified**.
- **[Hyp]** Practical path: IDTF (verbose text) as an intermediate plus the converter, or the fork's library API. Needs a vendored or overlay-port build, plus a licence-notice update (Apache-2.0 plus bundled-library notices). Old code; modern MSVC/GCC warnings and bugs expected.
- **[Recall]** Other tools (MeshLab, IDTFConverter-based pipelines) use the same code; unverified here.

### 2.3 Asymptote PRC writer (`prc/`)

- **[Obs]** Header of `prc/oPRCFile.cc`: "tool for producing 3D content in the PRC format", copyright 2008 Orest Shardt and Michail Vidiassov, LGPL v3 or later. Repository root README: all sources LGPL-3.0-or-later unless stated; the Windows `asy` binary is GPL because of GSL/Readline (not the prc code). Other `prc/` file headers not read. <https://github.com/vectorgraphics/asymptote> (`prc/`)
- **[Obs]** `prc/CMakeLists.txt` builds a `PRC` library from `oPRCFile.cc`, `PRCbitStream.cc`, `PRCdouble.cc`, `writePRC.cc`; only external dependency is zlib. Standalone buildability without Asymptote headers: **unverified**.
- **[Obs]** Active project: tags 3.15 (14 Sep 2026), 3.14 (29 Aug 2026), 3.13 (9 Jul 2026), no GitHub Releases. <https://github.com/vectorgraphics/asymptote/tags>
- **[Obs]** Output is a standalone PRC byte stream; the PDF embedding (3D annotation, view dictionaries) is not in `prc/`. A TUGboat note (via search result) says Michail Vidiassov overhauled the driver to the latest PRC spec. media9 embeds PRC into PDFs on the LaTeX side.
- **[Hyp]** Candidate for a PRC mesh writer: LGPL-3 is workable if the library is linked dynamically or can be relinked, which a closed-source app must honour. The project's licence (GPL-3.0 application) needs checking against the app's own LICENSE: the repo contains `gpl-3.0.txt` and `LICENSE`, so the app looks GPL-3.0, which makes LGPL-3 and Apache-2.0 both compatible [Hyp: confirm the app's licence file].

### 2.4 Open Design Alliance (Publish / PRC SDK)

- **[Obs]** Product page <https://www.opendesign.com/products/publish>: 2D and "3D PRC-based" PDF export; 3D solids as meshes or B-reps, compressed or not; PMI "including dimensions and tolerances"; custom views with a view list; animation via JavaScript; parts lists; Windows, Mac and Linux; included in every ODA membership level at no extra cost; web/SaaS use only in Sustaining, Founding and Corporate levels; free technical support; free 60-day trial. U3D not mentioned; membership prices, source-code availability, language bindings, cross-section support: **not stated**. Roadmap: 2026 "Redesign of 3D PdfExport".
- **[Obs]** Search summary (not read): a 2017 ODA brochure describes fixed-fee, royalty-free licensing; <https://www.opendesign.com/files/white-papers/ODA%20Publish.pdf> returned unreadable bytes.
- **[Hyp]** Strongest feature coverage for PRC, but ODA membership is a paid, binary-SDK relationship; not a vcpkg dependency.

### 2.5 Tech Soft 3D HOOPS Exchange (includes HOOPS Publish)

- **[Obs]** HOOPS Publish is now part of HOOPS Exchange (FAQ); product page <https://www.techsoft3d.com/products/hoops/publish/overview> serves the Exchange page. Read/write list includes PRC, U3D, PDF, glTF, STEP, JT, etc.; B-Rep, PMI, model tree, views, styles are accessible through the API; APIs in C, C++, C#; Windows, Linux, macOS (limited iOS/Android); free 60-day evaluation; version 2026.8.0 [from a summary of truncated release notes]. Docs sections "Creating a PDF from scratch", "Defining 3D", Publish interactivity/JavaScript/templates were seen as headings only. Pricing and licence terms: **not stated**. <https://docs.techsoft3d.com/exchange/latest/release_notes.html>
- **[Obs]** Old trade report: Tech Soft 3D took over Adobe's 3D PDF development (2008). <https://gfxspeak.com/archives/adobe-hands-3d-pdf-development-to-tech-soft-3d/>
- **[Hyp]** Commercial, per-seat/OEM, heavy binary SDK. Would also solve a lot of import (JT, STEP) but that is out of scope and large overkill for an export-only feature.

### 2.6 CAD Exchanger SDK

- **[Obs]** Version 3.10.2 added U3D and 3D PDF export (polygonal representation, product structure, colours, names); the support site says PDF import works only for U3D-based PDFs. SDK in C++, C#, Java, JavaScript, Python. Licence terms not stated in the pages seen. <https://cadexchanger.com/blog/cad-exchanger-3-10-2-with-u3d-and-3d-pdf-export-and-a-special-treat-for-python-based-apps>
- **[Hyp]** Commercial; comparable to HOOPS in cost class.

### 2.7 Datakit, Spatial, others

- **[Obs]** Datakit CrossCad/Ware SDK is listed as a PRC producer on Wikipedia's PRC page. Nothing else verified: no licence or price. Spatial 3D InterOp: **not researched**.

### 2.8 Open CASCADE (already a dependency)

- **[Hyp]** OCCT has no PRC or U3D writer that I can confirm; none was found in this research. **[Recall]** STEP, glTF, VRML, OBJ writers exist in OCCT; none is PDF-embeddable directly. Its value here is a source of B-Rep for a future exact-geometry PRC path, not a writer.

### 2.9 LaTeX media9 / movie15

- **[Obs]** `media9` 1.30 (CTAN, 2025-06-24), LaTeX Project Public License; embeds U3D and PRC via the RichMedia annotation; Adobe Reader only supports U3D or PRC embedded in the PDF, not streamed at run time. <https://ctan.org/tex-archive/macros/latex/contrib/media9>
- **[Hyp]** Requires a TeX toolchain at the user's machine; not appropriate for an end-user Save As command. Useful as a **test harness** and as a reference for the RichMedia dictionary layout.

### 2.10 Hand-rolled PDF object writer

A 3D PDF needs only: the file header, an object table, a page, a 3D annotation, its stream (Flate-compressed U3D/PRC) and optional view dictionaries, xref and trailer. The set is small.

- **[Obs]** PoDoFo: LGPL-2.0-or-later library, GPL tools, dependencies OpenSSL/libxml2/zlib/jpeg/tiff/png/idn (several optional), builds through vcpkg. libharu: zlib/libpng licence. <https://github.com/podofo/podofo/>
- **[Obs]** iText exposes `Pdf3DAnnotation`; iText's licence is AGPL or commercial (**[Recall]**, not verified here), so unsuitable by default.
- **[Recall]** Qt's `QPdfWriter` and `QPainter` have no 3D annotation support. Not verified in this session; check Qt docs before any design relies on it.
- **[Hyp]** A ~300-500 line in-house writer is sufficient for the annotation, view dictionaries, an optional poster image and a `/AA` JavaScript-free setup. Risk: details of the Adobe 3D extensions (PRC extension dictionary, `3DU`) must be reproduced exactly; Acrobat is a strict reader.

---

## 3. Viewer support (important for the decision)

- **[Obs]** Adobe: 3D content autoplay is off by default; users enable it in Preferences > Multimedia & 3D, or accept a per-file prompt; files in privileged locations render directly; untrusted files show a yellow "3D content has been disabled" bar. <https://helpx.adobe.com/acrobat/using/enable-3d-content-pdf.html>. A CERT advisory on crafted embedded U3D explains the caution. <https://kb.cert.org/vuls/id/759307>
- **[Obs]** Foxit PDF Editor documents support for U3D, PRC and glTF. <https://resource-cms.foxit.com/foxit-pdf-editor-for-windows/work-3d-content-in-pdfs>. A community forum post says free Foxit Reader may not handle both encodings (user post, possibly dated).
- **[Obs]** A forum post (Bricsys) states PDF-XChange handled U3D but PRC poorly (shown black) at the time; a 2018 PDF-XChange post announced U3D support with PRC planned. Dates and current state unverified.
- **[Obs]** Tech Soft 3D's mobile "3D PDF Reader" supports PRC and U3D. <https://apps.apple.com/us/app/3d-pdf-reader/id569307672>
- **[Recall]** Browser PDF viewers (Chrome, Edge, Firefox, macOS Preview) show no 3D content. Not verified in this session; test before making any claim to users.
- **[Hyp]** Practical audience: Adobe Acrobat/Reader on desktop. Everything else is best effort. A static poster image on the 3D page is essential so non-3D viewers show something.
- **[Hyp]** U3D has the broadest historical compatibility; PRC compatibility outside Adobe/Foxit/Tech Soft is weaker.

---

## 4. Mapping the app's data

Code references checked in this session (read-only): `include/Measurement/MeasurementData.h`, `AnnotationData.h`, `LabelPlane.h`, `include/Scene/SceneGraphExporter.h`, `GltfCameraData.h`, `Material.h`, plus memory note on the planned glTF measurement export.

### 4.1 Scene, assembly tree and meshes

- **[Obs]** `SceneGraphExporter::buildExportScene()` already rebuilds an export-only `aiScene` from the `SceneGraph` hierarchy and live meshes by UUID, with a `flattenTransforms` option and a source-file filter. It writes positions, normals, UV0, triangles, and its header notes materials were first-pass.
- **[Hyp]** Reuse this as the single source for the PDF export: node tree to U3D node hierarchy / PRC product occurrences, `aiMesh` triangle lists to U3D mesh blocks / PRC tessellation. U3D wants unique mesh resources per node with the node carrying the transform; instanced meshes in the app should become instanced resources, not copies (large assemblies are an owner concern: 34k-part JT and vehicle samples were noted in memory).
- **[Hyp]** Size: a 3D PDF embeds everything uncompressed-by-default; large assemblies need decimation or a size warning, optional mesh compression (U3D compressed meshes, PRC compression), and the "hidden parts" rule (export visible only?). Open question O3.

### 4.2 Materials

- **[Obs]** `Material` carries both Phong-style (ambient/diffuse/specular/emissive/shininess) and PBR (albedo, metalness, roughness, opacity, clearcoat, sheen, transmission, IOR, textures).
- **[Hyp]** U3D and PRC materials are Phong-like (ambient, diffuse, specular, emissive, shininess, opacity). Mapping: diffuse from albedo, specular derived from metalness/roughness (lossy), opacity direct, emissive direct, textures via embedded images (U3D texture blocks; PRC texture definitions) with UV0 only. Metal/PBR looks will not survive; Acrobat's lighting is its own. Colour and opacity are the dependable part.

### 4.3 Cameras and views

- **[Obs]** The app stores glTF cameras (`GltfCameraEntry`: perspective yfov/near/far or orthographic xmag/ymag, world position/direction/up), saved Scene States, selection sets, and exploded-view presets.
- **[Hyp]** Each saved camera and scene state maps to a PDF 3D view dictionary (`/3DV`: camera matrix, projection, background, render mode, per-node visibility/transform overrides), shown in Acrobat's view list. The current view becomes the default view. Scene-state node visibility and transforms (exploded) map to per-view node overrides; this is plausible with `3DV` node dictionaries but untested.
- **[Hyp]** Coordinate conventions (up axis, units, handedness) need a documented conversion; PDF 3D has a `3DU` units extension so lengths can be tagged mm.

### 4.4 Measurements and annotations (PMI-like)

- **[Obs]** Measurements: ~19 types, each defined by mesh-relative anchors (mesh UUID, triangle + barycentric, vertex or edge index), plus layout fields: `orientation` (ModelPlane, creation view or face-camera), `offsetVector`, `offsetReferenceDir` (creation view direction), `visible`, and for imported PMI a `recordedValue`/unit/kind and a `recordedDrawing` (arrows, extension lines, leaders and text strokes in the annotation plane).
- **[Obs]** Annotations: free text, one mesh anchor, leader offset, orientation, and for imported PMI `recordedKind` and `recordedDrawing`.
- **[Obs]** The owner's 2026-11-01 plan for glTF export already settled: labels are drawn as plain lines and filled triangles in model-space planes, oriented for each item's creation view; face-camera items export in the creation-view plane; the capture is done by a scratch ortho camera, no OpenGL; opt-in checkbox, off by default.
- **[Hyp]** Three possible PDF representations, increasing in fidelity and cost:
  1. **Geometry-only (recommended first).** Reuse that same captured line/triangle output as extra U3D line-set and mesh nodes (or PRC polylines/triangles) in an "Annotations" group, each node named and carrying the value text in a metadata dictionary. Works in both formats, needs no PMI schema, matches the existing capture design. Limitation: text becomes outline geometry, not selectable/semantic text.
  2. **PRC markup items.** Real PRC markup/annotation entities with leader, text and linked-geometry references. Open-source writer serialisers exist for markup items (Asymptote) but no PMI writer was seen; commercial SDKs advertise PMI output. Larger effort; viewer rendering of PRC markup in Acrobat not verified.
  3. **PDF-side comments or 2D page overlays** (labels as PDF annotations or form fields next to the 3D area). Loses 3D anchoring.
- **[Hyp]** Distinguish recorded from measured values in node names, as the app does, so the PDF never presents a measured number as an authored one.

---

## 5. Constraints and open questions for the owner

| # | Question | Why it matters |
|---|---|---|
| O1 | Target readers: Adobe Acrobat/Reader only, or also Foxit and others? | Drives U3D versus PRC; U3D is the safer lowest common denominator [Hyp]. |
| O2 | Must the PDF carry exact B-Rep, or are tessellated meshes enough? | B-Rep needs a PRC writer with B-Rep entities (commercial SDK or substantial custom work) and OCCT-side data retention. |
| O3 | Size policy for large assemblies: warn, decimate, export visible only, or compress? | 3D PDFs get unwieldy; the app's 34k-part cases are common to the owner. |
| O4 | Licence/cost appetite: open source only (Apache-2.0/LGPL), or are ODA membership, HOOPS or CAD Exchanger licences acceptable? | The app appears to be GPL-3.0 [Hyp: confirm]; commercial SDK redistribution and GPL compatibility must be checked before adoption. |
| O5 | Measurement/annotation text: outline geometry acceptable for the first cut? | Option 1 in 4.4. |
| O6 | Should the export include a 2D page (title, poster, parts list) or only the 3D area? | Affects the PDF page writer scope. |
| O7 | Linux parity: required from day one? | Rules out anything Windows-only. |
| O8 | Branch name and base for the implementation session. | Owner names it; not created here. |

Constraints noted: no build or test runs in this research session; Adobe behaviour must be tested by the owner on a real Acrobat/Reader; every acceptance claim should be recorded as owner acceptance, not inferred.

---

## 5a. Owner decisions (3 November 2026)

Recorded from the owner's answers to section 5. These are decisions, not observations.

| # | Decision |
|---|---|
| O1 | Acrobat (Reader) is the target to begin with. |
| O2 | Tessellated geometry is enough; no exact B-Rep. |
| O3 | Export visible parts only; add compression if required. |
| O4 | Strictly open source; no commercial SDK (ODA, HOOPS, CAD Exchanger are out). The app is GPL-3.0 (`LICENSE`, `README.md`), so Apache-2.0, LGPL, zlib and similar licences are compatible. |
| O5 | Outline geometry for measurement/annotation text is acceptable in the first cut. |
| O6 | The result is a complete drawing-style document (title block, 2D page content such as poster/parts list, plus the 3D area), not only a bare 3D view. |
| O7 | Linux parity from the start, to avoid API/compatibility rework later. |
| O8 | Branch name: `feature/3DPDF`; the owner creates it. |

Owner acceptance (3 November 2026): all recommendations in this document are accepted. This was a brainstorming session only; implementation starts when the owner says so. `feature/3DPDF` will be branched from `dev` after the 3DXML and JT importers are merged to `dev`.

Consequences (hypotheses, for the implementation session):

- Commercial options in 2.4-2.7 are excluded. PRC is only reachable through the Asymptote writer (LGPL-3.0-or-later); U3D through the Apache-2.0 `u3d` library. Both need a vendored copy or overlay port that builds on Windows and Linux, so the first spike should build both.
- O6 widens scope: a real PDF page writer (text, vector lines, images, page layout, fonts) is needed, not just a 3D annotation. Qt's `QPdfWriter` and `QPainter` (Qt Gui, LGPL/GPL) can draw the 2D page cross-platform [Recall: unverified], but embedding the 3D annotation object into a `QPdfWriter` document is probably not supported [Recall]. Candidate designs: (a) in-house PDF writer for the whole file, with fonts embedded via Freetype (already a dependency); (b) `QPdfWriter` page, then post-process the file to add the 3D annotation, which means editing PDF objects anyway; (c) PoDoFo (LGPL-2.0+, vcpkg-buildable) for page plus 3D annotation. The spike must pick one. Recommendation: evaluate (c) first, fall back to (a).
- "Visible only" matches how the app already skips hidden meshes; export must read the same visibility the viewport uses.
- Acrobat-only target makes U3D and PRC equally acceptable for viewing; U3D remains the first pick because its writer is simpler and fully specified by a free standard. Revisit PRC only for compression needs.

---

## 6. Recommendation and phased plan

**[Rec] Format:** start with **U3D** for the first cut. Reasons: the app's data is meshes, materials, lines and cameras; U3D is freely specified; an Apache-2.0 reference implementation exists; broadest historical Acrobat compatibility [Recall]. Keep the internal export model neutral (a small `ExportScene` of nodes, meshes, materials, views, overlay line/triangle sets) so a PRC backend can be added behind the same interface without touching callers.

**[Rec] Writer:** U3D from a vendored or overlay-port copy of the `u3d` library (evaluating the fork) with a small in-house PDF writer. Before committing to it, run a one-day spike under `out/`: write a cube with IDTF/`u3d`, wrap it in a hand-built PDF, and have the owner open it in Acrobat Reader. Fall back to the media9 route for reference output if the in-house PDF structure is the problem. Avoid pulling PoDoFo/iText unless the spike shows the hand-built PDF is brittle; PoDoFo (LGPL-2.0+) is the fallback.

**[Rec] Phases:**

1. **Spike and gate.** Cube plus two coloured meshes to a 3D PDF, opened by the owner in Acrobat. Confirms the toolchain, licence notices and the Adobe "enable 3D" behaviour. Owner confirms before the next phase.
2. **Mesh-only export.** `Save as 3D PDF` command, scene tree to node hierarchy, instanced meshes, colour and opacity, poster image from the viewport, size warning, units tag. Linux and Windows build.
3. **Views.** Current camera as default view; saved cameras and scene states as additional named views, including visibility and exploded transforms.
4. **Measurements and annotations (geometry).** Reuse the planned capture of lines and triangles per item in creation-view orientation, as grouped, named nodes, behind an export checkbox (default off, like the glTF plan).
5. **Optional PRC backend.** Only if O2/O4 demand B-Rep, semantic PMI or better compression: evaluate the Asymptote PRC writer for tessellation and markup, or an ODA/HOOPS licence for B-Rep and PMI.

**[Rec]** Do not start the work on `feature/JT-importer`; the owner creates `feature/3DPDF` (decision O8) off the up-to-date integration branch. Given decision O6, phase 2 should include the 2D drawing page (title block, poster, parts list) and the spike should also test page content, not only the 3D annotation.

---

## 7. Sources

Fetched or surfaced in this session (dates 3 November 2026):

- ISO 14739-1: <https://www.iso.org/standard/54948.html>, <https://scc-ccn.ca/standardsdb/standards/8151929>
- PRC (Wikipedia): <https://en.wikipedia.org/wiki/PRC_(file_format)> (page footer: last edited 10 October 2026)
- U3D (Wikipedia, last edited 24 February 2026): <https://en.wikipedia.org/wiki/Universal_3D>; ECMA-363 4th edition: <https://ecma-international.org/wp-content/uploads/ECMA-363_4th_edition_june_2007.pdf>
- Adobe 3D annotation docs: <https://opensource.adobe.com/dc-acrobat-sdk-docs/library/plugin/Plugins_3D_samples.html>
- PDF Association 3D formats: <https://pdfa.org/resource/3d-formats/> (search summary only)
- Adobe enable 3D: <https://helpx.adobe.com/acrobat/using/enable-3d-content-pdf.html>; CERT: <https://kb.cert.org/vuls/id/759307>
- Foxit 3D: <https://resource-cms.foxit.com/foxit-pdf-editor-for-windows/work-3d-content-in-pdfs>
- PDF-XChange forum: <https://forum.pdf-xchange.com/viewtopic.php?p=120573>; Bricsys forum: <https://forum.bricsys.com/discussion/comment/45978/>
- u3d: <https://sourceforge.net/projects/u3d/>, <https://github.com/DmytroSytnyk/u3d>
- Asymptote PRC: <https://github.com/vectorgraphics/asymptote>, `prc/` files `oPRCFile.cc`, `writePRC.cc`, `CMakeLists.txt`, tags page
- ODA Publish: <https://www.opendesign.com/products/publish>
- HOOPS Exchange/Publish: <https://www.techsoft3d.com/products/hoops/publish/overview>, <https://docs.techsoft3d.com/exchange/latest/release_notes.html>; Adobe hand-off report: <https://gfxspeak.com/archives/adobe-hands-3d-pdf-development-to-tech-soft-3d/>
- CAD Exchanger: <https://cadexchanger.com/blog/cad-exchanger-3-10-2-with-u3d-and-3d-pdf-export-and-a-special-treat-for-python-based-apps>
- media9: <https://ctan.org/tex-archive/macros/latex/contrib/media9>
- PoDoFo: <https://github.com/podofo/podofo/>; libharu (search summary, <https://github.com/libharu/libharu>)

Not fetchable in this session: Wikipedia "3D PDF" (HTTP 403), `pdfa.org` root (404), `vcpkg.link/ports/u3d` (404). Not researched: Spatial 3D InterOp, Datakit licensing, Qt PDF 3D support, current Acrobat release notes on 3D.

---

## 8. Phase 1 spike log (5 November 2026)

Work is on `feature/3DPDF` in a separate worktree. Nothing here is committed. Regenerate the evidence from the repository root (all output under ignored `out/`):

```bash
git clone --depth 1 https://github.com/DmytroSytnyk/u3d.git out/3dpdf/src/u3d
cmake -S out/3dpdf/src/u3d -B out/3dpdf/build/u3d -G "Visual Studio 17 2022" -A x64 -DU3D_SHARED=OFF -Wno-dev
cmake --build out/3dpdf/build/u3d --config Release --target IDTFConverter IFXCore IFXExporting IFXImporting IFXScheduling
export U3D_LIBDIR="$(cygpath -w out/3dpdf/build/u3d/Release)"
out/3dpdf/build/u3d/Release/IDTFConverter.exe -input out/3dpdf/src/u3d/Samples/TestScenes/box.idtf -output out/3dpdf/run/box.u3d
python -I tools/3dpdf/make_u3d_pdf.py out/3dpdf/run/box.u3d out/3dpdf/run/spike_box.pdf --title "Spike: box"
```

### 8.1 u3d library observations

- **[Obs]** Fork commit `5c141d9`, Apache-2.0 (`COPYING`). Configures and builds with Visual Studio 2022 (MSVC 14.44) using the Visual Studio generator; the `IDTFConverter` tool, and the four runtime modules `IFXCore`, `IFXExporting`, `IFXImporting`, `IFXScheduling` all built. Build emitted many warnings, including C4302 "truncation from `char *` to `long`" in `IFXVoidStar.h` (a pointer-size hazard on 64-bit; effect not investigated).
- **[Obs]** `IDTFConverter` and `libIDTF` load the `IFX*.dll` modules at run time (`libIDTF/IFXOSLoader.cpp`, environment variable `U3D_LIBDIR`). Without them it exits with code `0x80000003` and writes nothing. This is a deployment constraint: the application would need to ship those modules or the code would need to be linked statically [Hyp: link-time change, not attempted].
- **[Obs]** `IDTFGen` writes only the two-line IDTF header (a stub), and `HelloU3DWorld` fails with `0x80000003` without the modules. Neither is useful as a model generator; the bundled `Samples/TestScenes/*.idtf` scenes are.
- **[Obs]** With the modules present, `IDTFConverter` converted the sample `box.idtf` to a 1,276-byte `.u3d` (exit code 0).
- **[Hyp]** The IDTF text format is therefore the practical input API (the fork's `libIDTF` exposes `IFXWriter` for emitting IDTF, then conversion is a second step). A production writer would generate IDTF in memory or on disk and convert it, or emit U3D blocks directly.

### 8.2 Hand-built PDF wrapper

- **[Obs]** `tools/3dpdf/make_u3d_pdf.py` (stdlib-only, throwaway) builds a one-page A4 landscape PDF: border, title block, small parts list (vector lines and Helvetica text) and a 3D annotation (`/Subtype /3D`, `/3DD` U3D stream, `/3DV` default view, `/3DA` activation, `/AP` poster text for readers without 3D). It was run on `box.u3d`; the output parses with pypdf (one page, one `/3D` annotation, expected text).
- **[Not verified]** Whether Acrobat opens it and renders the box. This needs the owner's check (see "owner gate" below). The `/C2W` camera values in the default view are placeholders.

### 8.3 What a commercial 3D PDF looks like (Tetra4D samples)

Source: the owner supplied <https://tetra4d.com/pdf-samples/>; the page lists 24 sample PDFs (6 made with their Enrich product, 18 with their Converter product), gives no licence for them and no feature descriptions. Eleven were downloaded for local study only into ignored `out/3dpdf/samples-tetra4d/`; they must not be committed or redistributed. Structure inspected read-only with pypdf (`python -I`).

- **[Obs]** Every file with a 3D annotation carries **PRC** (stream magic `PRC` plus a version byte), none carries U3D. 3D PDFs are 1-3 pages for these samples (a 7-page document and a 2-page document also seen); the files are 0.6-2.9 MB; the 3D payloads are 0.4-1.6 MB.
- **[Obs]** The Converter samples are single pages of Letter portrait or 960x540 landscape with one large 3D annotation covering most of the page, a header/branding text and (landscape ones) six form-field widgets (buttons/fields).
- **[Obs]** The 3D stream dictionaries carry a `/VA` array of named views (5-25 per file, e.g. Left/Top/Front/Right/Bottom/Back/Default, or engineering names such as "SECTION A-A", "DETAIL F", "NOTES"), an `/AN` entry (animation) and an `/OnInstantiate` stream (a script).
- **[Obs]** Each view dictionary carries: `/C2W` (12-number camera-to-world matrix), `/CO` (distance to the centre of orbit), `/MS /M`, `/P` projection, `/BG` background, `/RM` render mode, `/LS` lighting scheme (CAD, Headlamp), and `/NA`, a per-node array with `/N` node name, `/V` visibility, `/O` opacity, `/M` 12-number matrix and `/RM`. In other words, saved views encode part visibility, opacity and transforms per node, and "section" or "detail" views are implemented as named views.
- **[Obs]** Activation is `/A /XA` (activate when the page is visible), `/D /XD`, `/DIS /I`, `/Style /Embedded`, optionally `/TB false` (no toolbar); the Prendergast sample (2 pages, 25 views) uses `/A /PO /D /PC /NP false`.
- **[Obs]** Several Enrich-type files (Spare Part Catalog, Design Change Notification, Engineering Data Release, Design Review) contain no 3D annotation and no widgets in the downloaded copies (page content only), so they look like 2D templates without the model; they are layout references only. The Technical Data Package sample is a 7-page document (form page with ~133 widgets and stamps, then A4 landscape pages).
- **[Hyp]** The drawing-style target the owner described (decision O6) corresponds to: a page with border and title block, a large 3D viewport, a view list implemented through `/VA` views with node visibility, and optional form widgets for buttons. Our export can map saved cameras and scene states to `/VA` views with per-node `/NA` entries (visibility, opacity, matrix), consistent with section 4.3.
- **[Hyp]** Because these commercial files use PRC, whereas the first-cut plan here is U3D, a quick compatibility check is worthwhile: U3D streams are in the PDF 1.7 core and Acrobat has supported them since PDF 1.6 [Recall]; the spike will show whether the `/VA`/`/NA` view mechanism works the same for U3D streams (the spec places it at the stream dictionary level for both subtypes, but this is unverified).

### 8.4 Owner result (5 November 2026)

**[Owner observation, from a screenshot]** `spike_box.pdf` opens in Adobe Acrobat; the hand-built 3D annotation activates and shows the U3D box lit and shaded, with Acrobat's 3D toolbar (rotate/pan, home, Views list, model tree, render mode, lighting, section, background). The border, title block and parts list render. One defect, ours: the parts-list text overlapped the title text (layout bug in the spike script, fixed afterwards by moving the table right). The camera shows the box tilted, because the `/C2W` values are placeholders; real views are still to be written. **Phase 1 gate passed for U3D in Acrobat**: the toolchain (u3d library, IDTF-to-U3D conversion, hand-written PDF objects) is viable.

### 8.5 Owner gate for phase 1 (superseded by 8.4)

Please open `out/3dpdf/run/spike_box.pdf` (in the 3D PDF worktree) in Adobe Acrobat/Reader, enable 3D content if prompted, and report: (a) does the 3D annotation activate, (b) is the box visible and lit, (c) does the title block and parts list render, (d) the camera orientation and zoom (placeholder `/C2W`). Phase 2 does not start until this is answered.

### 8.6 Camera matrix findings (5 November 2026)

- **[Owner observation]** Named views written with `/MS /M` and a computed `/C2W` first showed the box off-centre, toward the top-left and far away, while a view that reuses the U3D file's own camera node (`/MS /U3D /U3DPath (DefaultView)`) was centred. The spec pages for the 3D coordinate systems could not be fetched (404), so this was settled by experiment.
- **[Owner observation]** A diagnostic PDF wrote the U3D file's own camera (eye on the -Y side, looking at the origin) as `/C2W` in six conventions; only the one in which **the camera looks along +Z of its own frame** showed the box (the others showed nothing in the viewport). So in Acrobat, for `/MS /M`, the rows of `/C2W` are the camera's right, up and **forward** axes in world coordinates, followed by the eye position. This is the opposite of a U3D view node (which looks along -Z).
- **[Hyp]** The matrix is not mirrored for a right-handed world (the right axis is forward x up), and the test box is symmetric, so a mirror error would not have been visible. It must be re-checked with an asymmetric model before this is relied on.
- **[Hyp]** Distance from the target to the eye for a bounding sphere of radius r is `r / sin(FOV/2) * margin` (with `/PS /Min`; margin 1.25 was used); not yet confirmed visually after the fix.
- **[Obs]** PDF numbers must not use exponent notation (e.g. `7e-15`); the writer prints plain decimals.
- **[Obs, caveat]** The Tetra4D PRC samples' camera rows read in the same way (right, up, then an axis) but their model placement could not be checked, so they are not independent evidence.

### 8.7 Node matrix override (6 November 2026)

- **[Owner observation]** With the forward-Z camera (8.6) the views were in the frame but not centred: with the box moved to the origin, Top was centred while Right and Iso were about 20 units too high; with the box in its original place (-3.3, -63, 0..40), Top was about 63 units too high and Iso was displaced to the upper right.
- **[Hyp, from those offsets]** Each view's `/NA` entry listed the box node with `/M` set to the identity. In a 3D view dictionary the node matrix replaces that node's own transform for that view, so the box jumped to its local origin while the camera was aimed at its authored position: the offsets above equal the box's own translation. Fix: leave `/M` out of `/NA` entries (keep `/V` visibility and `/O` opacity). To be confirmed in Acrobat with the v6 files.
- **[Hyp]** The same `/NA /M` entry is the mechanism for exploded or moved states per view; it must then contain the node's full intended transform, not an offset.

**[Owner observation, 6 November 2026]** The v6 files (box at origin and box in its original position) show the box centred in every computed view (Iso, Front, Top, Right). The 8.6 camera convention (rows: right, up, forward, then eye; distance `r / sin(FOV/2) * 1.25`) and the 8.7 fix (no `/M` in `/NA`) are therefore confirmed for a symmetric box in Acrobat. Still unchecked: mirroring (needs an asymmetric model) and per-view part visibility on more than one node.

### 8.8 Asymmetric multi-part test (6 November 2026)

Scene `out/3dpdf/run/lscene.idtf` (generator `tools/3dpdf/make_test_scene_idtf.py`): `LBracket` (L-shaped, tall leg at -X, base along +X, one mesh of two cuboids) and `Block` (separate blue cuboid at +X); PDF built with the same `make_u3d_pdf.py` and the computed views.

- **[Owner observation]** Front: tall leg on the left, block on the right. Top: leg at the left end, block at the right, axis triad X right and Y up. Right: block nearest and in front, bracket behind and above it, axis triad Z up and Y right. These are the correct, unmirrored images, so the 8.6 camera convention (right, up, forward rows plus eye) is right-handed and not mirrored.
- **[Owner observation]** "First part hidden" hides only `LBracket` and leaves `Block` visible, so per-node `/NA` visibility works with several nodes. The remaining block is off-centre in that view, as expected, because the camera is still framed on the full scene.
- **[Obs]** The U3D file's own DefaultView renders the same orientation as the computed Front-like view.
- Still unchecked: `/NA /M` (moved part per view), `/O` opacity below 1, line and point geometry for measurements, and large-model behaviour.

### 8.9 Line-set measurement layer (6 November 2026)

Scene `out/3dpdf/run/mscene.idtf` (same generator): the L bracket and block plus a third node `Measure`, an IDTF `LINE_SET` of 24 segments in the plane y = -10: a 60-unit dimension (two extension lines, dimension line, two arrowheads), the number "60" as seven-segment strokes, and a leader from the tall leg to a label point. The material was given a red emissive colour.

- **[Owner observation]** Line sets render in Acrobat and the dimension, arrowheads, leader and stroke-drawn "60" are all correct and unmirrored in Front and Iso; in Top the in-plane graphic is seen edge-on as a thin line, as expected.
- **[Owner observation]** The lines are thin and BLACK in every view. The red emissive material was not applied, so Acrobat ignores material colour for line sets (it draws them in a fixed colour). **[Hyp]** per-vertex diffuse colours (`MODEL_DIFFUSE_COLOR` with line shading) or PRC/other styling would be needed for coloured measurement lines; to be tested if colour matters. Line width is a fixed hairline and not controllable from the file.
- **[Owner observation]** The "Last node hidden" view hides the whole measurement layer and keeps both parts, so a measurement layer can be a single node that views toggle on and off.
- **[Consequence]** Outline-stroke text is legible but hairline-thin; for the real exporter consider text as filled triangle outlines (a mesh node, which does take colour) and keep lines only for dimension and leader geometry. One node per measurement would allow per-measurement visibility in views.
- Still unchecked: coloured line sets (vertex colours), filled-triangle text, point sets, `/NA /M` moved parts, opacity below 1.

### 8.10 Coloured lines and filled text (6 November 2026)

Scene `out/3dpdf/run/nscene.idtf`: the 8.9 line set now with per-vertex diffuse colours (`MODEL_DIFFUSE_COLOR_LIST`, `LINE_DIFFUSE_COLOR_LIST`, shader `ATTRIBUTE_USE_VERTEX_COLOR "TRUE"`; red dimension, blue leader) plus a `TextMesh` node: the "60" again as thin cuboids with a red material.

- **[Owner observation]** The line set is STILL drawn thin and black in Front: neither material emissive colour (8.9) nor per-vertex colours change it. Acrobat draws U3D line sets in a fixed colour, so dimension and leader colour cannot be set through line sets.
- **[Owner observation]** The filled "60" renders in red, crisp, readable and unmirrored (Front, and in Iso), at a clearly legible weight compared with the hairline version.
- **[Owner observation]** "Last node hidden" removes the line set and leaves the filled text, so line sets and meshes toggle independently per view.
- **[Consequence for the design]** Use meshes (thin cuboids or flat triangle strips, one per stroke) for anything that must be coloured or thick: measurement text, and probably the dimension and leader lines as well. Line sets stay available for plain hairlines. Cost: the mesh approach multiplies triangle counts (12 per cuboid stroke, 4 per double-sided flat quad), so keep strokes simple; a flat double-sided quad is the cheaper option to test next, together with its look edge-on.
- Still unchecked: point sets, `/NA /M` moved parts, opacity below 1, flat quads versus cuboids, Linux build.

### 8.11 Flat quads, moved part, opacity (6 November 2026)

Scene `out/3dpdf/run/oscene.idtf` / `spike_oscene.pdf`: the 8.10 scene plus a `TextQuads` node ("60" as double-sided flat quads in green, 4 triangles per stroke, 0.1 in front of the face) and three new views: "Block moved +Z 40" (`/NA /M [1 0 0 0 1 0 0 0 1 0 0 40]`), "Block 40% opaque" and "Bracket 40% opaque" (`/O 0.4`).

- **[Owner observation]** Flat quads render fine: the green "60" is crisp and readable, as heavy as the cuboid version in Front, and shows no z-fighting against the bracket face. In Iso it is shaded darker green (lit). Triangle cost is a third of the cuboid version, so flat quads are the choice for text and thick dimension strokes.
- **[Owner observation]** `/NA /M` with an identity rotation and a translation moves the block up by 40 as intended (Iso and Front), while the other nodes stay put. Because the test nodes' own transforms are identity, this only shows that `/M` takes effect; for the real exporter `/M` must hold the node's full transform (8.7).
- **[Owner observation]** `/O 0.4` makes the block, or the bracket, translucent with correct blending: the bracket's internal overlap and the block show through, and the other part is unaffected. Per-view opacity per node works.
- **[Consequence]** The view mechanism covers everything the app needs from saved scene states: camera, per-part visibility, per-part opacity and moved parts (exploded views).
- Still unchecked: point sets, many nodes, textured and metallic-looking materials, Linux build.

### 8.12 Point set and 400-node stress scene (6 November 2026)

Scenes `out/3dpdf/run/pscene.idtf` (adds an `Anchors` POINT_SET of 5 anchor points, magenta material) and `stress.idtf` (`tools/3dpdf/make_stress_scene_idtf.py`: 20 x 20 grid of separate cubes, each its own node, mesh, shader and material; U3D 300 KB, PDF 197 KB).

- **[Owner observation]** Point sets render only as single-pixel BLACK dots (visible at the leader end, the dimension ends and the block corner). Colour, size and the magenta material are ignored, as with line sets. They are not usable as visible anchor markers; use small meshes (a flat quad or tiny cuboid) for any visible dot or marker.
- **[Owner observation]** The 400-node scene opens and renders correctly in Iso, with all cubes and per-node colours. **[Owner observation]** Loading took no noticeably longer than the small scenes, so 400 nodes is not a load-time concern.
- **[Obs]** The 400-node U3D is about 300 KB uncompressed (about 750 bytes per cube with its own resources), so file size scales linearly and is dominated by per-node resource overhead, not geometry; merging parts that need no separate view control would shrink it.
- **[Consequence]** Fixed Acrobat styling for non-mesh primitives (lines and points: black, hairline and one pixel) means the exporter's visible measurement layer is best built from flat-quad meshes: stroke text, dimension lines as thin quads, arrowheads as triangles, anchor markers as small quads. Lines stay useful only as optional hairline extras.
- Still unchecked: orbit smoothness at much larger node counts (thousands), textures, Linux build.

### 8.13 Survey of three more sample sets (6 November 2026)

Read-only pypdf survey (scratch scripts, not in the repo) of `out/3dpdf/samples-techsoft3d` (6 files), `samples-pdfa.org` (10 files) and `samples-simlab-soft` (21 zips, each one PDF; unpacked each into its own folder after checking for non-PDF content). Local study only, no content reused.

- **TechSoft3D (SpinFire) and PDF Association samples: PRC.** One 3D annotation per page, `/MS /M` views, 3 to 30 named views, up to 245 `/NA` node entries per view, 300 KB to 4 MB of PRC. They carry 25 to 500 form fields (view buttons, BOM tables, text fields) and document-level JavaScript. Pages are 1 to 4 (cover, 3D, tables).
- **SimLab samples: U3D**, 1 to 119 named views, one 3D annotation per page, page sizes like 1136 x 640 (a full-page 3D window). Their views are mostly `/MS /U3D` with a `/U3DPath` to camera nodes inside the U3D file (the camera set lives in the file; `/NA` lists just that camera), the stream has `/OnInstantiate` JavaScript and `/AN` animation entries, and navigation uses form buttons (`/Btn`). U3D streams are 1 to 61 MB (large textured scenes); 3D toolbar on or off per file.
- **[Consequence]** Both approaches exist in the wild. For our exporter `/MS /M` with computed cameras (our spike) is the PRC-sample style and keeps the camera data in the PDF; `/MS /U3D` needs a camera node per view in the U3D file and has no advantage for us. View buttons are form widgets with JavaScript; that is phase 3 (interactivity), not phase 1.
- **[Open, camera handedness]** All 35 `/MS /M` views checked in the NX, Creo and SpinFire samples have a positive determinant for the three rotation rows of `/C2W`, but our `look_at` (rows right, up, forward) has a negative determinant and was confirmed unmirrored in Acrobat (8.8). The `spike_lscene_det.pdf` test (right, up or forward row negated) is meant to settle which row conventions Acrobat really uses.

### 8.14 Camera handedness test result (6 November 2026)

`out/3dpdf/run/spike_lscene_det.pdf`: the Front view of the L-bracket scene with the camera matrix variations below (all other rows and the eye unchanged).

- **[Owner observation]** Our matrix (right, up, forward; det -1): the normal, unmirrored Front image.
- **[Owner observation]** Right row negated (det +1): IDENTICAL image. Acrobat ignores the first row.
- **[Owner observation]** Up row negated (det +1): the image is rotated by 180 degrees in the screen plane (block on the left, L upside down and flipped). The second row is used.
- **[Owner observation]** Forward row negated: empty view (the camera looks away from the scene). The third row is used.
- **[Conclusion]** Acrobat takes the view direction from the third row and the up direction from the second, derives "right" itself, and takes the eye from the translation. The determinant of the matrix therefore does not matter, which resolves the 8.13 puzzle: commercial files write a right-handed-looking matrix, our right row has the opposite sign, both display identically. For the exporter, write (right, up, forward, eye) with `up` and `forward` exact; the right row can be either sign (write the right-handed one, `up x forward`, to match the commercial files).

### 8.15 Linux build of u3d (6 November 2026, WSL Ubuntu 26.04)

Built in the Linux dev checkout (`~/dev/ModelViewer-Qt/out/3dpdf-linux/`, git-ignored `out/`), same u3d commit `5c141d9`, g++ (GCC 16 default C++17), cmake 4.2.3, bundled jpeg/png/zlib.

- **[Obs]** Source needs NO change. Three build/run requirements:
  1. cmake 4: `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (the project declares `cmake_minimum_required(2.8)`).
  2. C++14: `-DCMAKE_CXX_STANDARD=14 -DCMAKE_CXX_FLAGS="-std=gnu++14 -fpermissive -w"`; with the C++17 default the build stops on `throw()` dynamic exception specifications in `IFXCorePluginStatic.cpp`.
  3. Runtime: the `IFX*` plugin libraries must sit in `$U3D_LIBDIR/Plugins/` (symlinks to the built `.so` files work), and the converter calls `setlocale(LC_CTYPE, "en_US.UTF-8")` at startup, failing with `Exit code = 80000000` and no output when that locale is missing (the WSL install only has C/C.utf8/POSIX). A private locale generated with `localedef -i en_US -f UTF-8 <dir>/en_US.UTF-8` plus `LOCPATH=<dir>` fixes it without root. A production exporter must not depend on this: either link the library into the app and bypass the converter's `main`, or patch `IFXSetDefaultLocale` to accept `C.UTF-8`.
- **[Result]** The Linux `IDTFConverter` produces BYTE-IDENTICAL `.u3d` files to the Windows build for the five small scenes (`lscene`, `mscene`, `nscene`, `oscene`, `pscene`), so the Windows-verified Acrobat behaviour carries over without a new Acrobat test. The 400-node scene differs by 388 bytes (301 260 vs 301 648 bytes), presumably an ordering or hashing detail with many resources; `out/3dpdf/run/spike_stress_linux.pdf` is the Linux-built variant for an optional Acrobat check.
- **[Consequence]** Linux parity for the writer is not a risk. The open design point is how the app calls the library (embedding `IFXCore`/`IDTF` as a static library with the plugin lookup replaced, versus invoking the converter binary), see the exporter design.

---

## 9. PDF writer decision and exporter design (draft, 6 November 2026)

### 9.1 PDF writer options, re-assessed after the spike

What the spike showed about the file we need: one page, one `/3D` annotation with a Flate stream and a `/VA` view array, an optional poster; everything else is ordinary 2D page content (border, title block, parts list, text).

- **A. In-house writer for the whole file** (what the Python spike does). Fine for the 3D part; the 2D page needs text, font embedding (Unicode, the app is translated), images and vector drawing, which is the large and error-prone part.
- **B. `QPdfWriter` for the 2D page, then an in-house incremental update that appends the 3D annotation.** The app already uses `QPdfWriter` (`ReportExportDialog.cpp:359`), so fonts, text layout, images, A3/A4 pages and Linux parity come for free from Qt with no new dependency or licence. The update appends the 3D stream, the annotation object, a new copy of the page object with `/Annots`, a new xref section and a trailer with `/Prev`. This needs a small reader for Qt's output: last `startxref`, trailer (`/Root`, `/Size`, `/ID`), the page object.
- **C. PoDoFo for everything** (LGPL-2.0-or-later or MPL-2.0 per the PDF Association release note for PoDoFo 1.0; vcpkg lists 1.0.x; upstream is at 1.1.1). Robust parsing and incremental saving, but it adds a sizeable dependency tree (openssl/libxml2/fontconfig/...) to a vcpkg build that is already large, and a search found no ready-made 3D annotation class (it would be built from raw dictionaries anyway).
- **D. `QPdfWriter` then PoDoFo to append the annotation.** Same parsing robustness as C but the same dependency cost as C.

**Recommendation: B**, with D as the fallback if Qt's output ever stops being simple to append to. Reasons: no new dependency, matches the existing report export, the 3D part is already proven by the spike, and Linux is identical.

- **[Recall, to verify first]** Qt's PDF engine writes a classic xref table and no object streams. The three existing PDFs in `docs/guide/` are Skia (not Qt), so they do not confirm it. First implementation step: write a page with `QPdfWriter`, inspect its structure with pypdf/a hex dump, and confirm the append works in Acrobat; the code must detect an xref stream or object streams and fail with a clear message rather than corrupt the file.
- **[Hyp, to test]** The poster can be drawn by `QPainter` onto the page inside the annotation rectangle, and the annotation can carry an empty appearance stream, so no image XObject has to be written by hand. Acrobat shows the page content until the 3D is activated. Needs one Acrobat check (the spike used a poster `/AP`).
- **[Obs]** Acrobat only needs the `/Rect`, `/3DD`, `/3DV`, `/3DA` and `/VA` entries seen in the spike; a PDF/A-style conformance claim is out of scope.

### 9.2 The U3D side: calling the library

The u3d library is Apache-2.0 and builds on Windows (VS2022) and Linux (GCC with `-std=gnu++14`, 8.15) with identical output, but it needs a one-line patch (`setlocale(LC_CTYPE, "en_US.UTF-8")` in `IFXSetDefaultLocale` must not fail on systems without that locale) and its plugin lookup (`Plugins/` next to `U3D_LIBDIR`).

- **Option 1: ship `IDTFConverter` plus the four plugin libraries as a helper and call it through `QProcess`** with an IDTF text file. Pro: no linking against a 2008-era code base, crash isolation, minimal code. Con: extra files in the installer and the `.deb`; text IDTF is slower than direct API for very large models (still fine at 400 nodes).
- **Option 2: link the library in-process** (`IFXCoreStatic` exists) and drive it through the `libIDTF` API. Pro: no temp files or helper. Con: more patching and a harder-to-debug 32-bit-era API surface.
- **[Superseded by 9.7: the owner chose Option 2, in-process.]** Earlier recommendation: Option 1 for the first version, keeping the IDTF generator behind an interface so Option 2 can replace it later. The one-line locale patch is carried as a patch file in a vendored `third_party/u3d` folder (Apache-2.0 notice added to `CREDITS.md`).
- **[Hyp]** U3D mesh compression is controlled by the converter's `-compress` and quality options; the export dialog should map "Compress" to those. To measure on a real model before committing to a default.

### 9.3 Mapping the app to the PDF (what the spike proved is possible)

| App data | PDF/U3D result | Spike evidence |
|---|---|---|
| Visible parts (scene graph export) | One U3D node per exported part (unique stable name, e.g. uuid), one mesh/shader/material each | 8.12 (400 nodes fine) |
| Material | Diffuse colour and opacity (Phong); no PBR; textures need a later test | 8.11, 8.12 |
| Saved cameras | `/VA` views with `/MS /M`, `/C2W` (up, forward, eye used), `/CO`, `/FOV` (FOV 30 min-dimension fit) | 8.6, 8.14 |
| Scene states (visibility, opacity, transforms, exploded) | Per-view `/NA` entries: `/V`, `/O`, `/M` full node transform | 8.8, 8.11 |
| Measurement/annotation graphics | Flat double-sided quad meshes for text, dimension lines, arrows, markers (colour and weight come from the material); one node per measurement so views can toggle it | 8.9-8.12 |
| 2D page (title block, parts list, border) | Drawn by `QPainter` via `QPdfWriter` | 9.1 |

Open items inside the mapping: unit/scale handling (the camera fit assumes model units), text outlines (the app must turn `QPainterPath` text into triangles; options: an ear-clipping header library or the CGAL triangulation the app already ships), textures and PBR (not tested), instances/duplicate meshes (each export node writes its own mesh, so repeated parts multiply size; a shared-mesh option needs a spike test).

### 9.4 Phased plan (revised)

1. **Phase 1, minimal exporter** (`Save as 3D PDF...`): visible parts to IDTF, helper converts, `QPdfWriter` page with border and title, append the 3D annotation with Iso/Front/Top/Right views.
2. **Phase 2, drawing page:** title block with editable fields, parts list from the scene, scale and sheet fields, A4/A3, language support.
3. **Phase 3, measurement and annotation layer:** flat-quad geometry, one node per item, layer toggles through views.
4. **Phase 4, saved views and scene states:** cameras to `/VA`, states to `/NA`, exploded view positions.
5. **Phase 5, interactivity:** form buttons with JavaScript to switch views (as in the commercial samples), per the survey in 8.13.
6. **Phase 6, packaging:** helper and plugins in the Windows installer and the `.deb`, Apache-2.0 notices, CI smoke test comparing converter output.

### 9.5 Decisions needed from the owner

1. Writer path: B (`QPdfWriter` plus own append, recommended) or C/D (PoDoFo).
2. U3D library call: helper process first (recommended) or in-process from the start.
3. Text outlines: which triangulation to use (CGAL, already a dependency, or a small header library).
4. Page template: fixed layout first (recommended) or user-defined templates from the start.
5. Where to vendor the u3d sources and the patch file (`third_party/u3d`).
6. When phase 1 starts, and whether the exporter code goes into this branch or a separate one off `feature/3DPDF`.

### 9.6 Sources for this section

- PoDoFo 1.0 release: <https://pdfa.org/podofo-10-released/>; vcpkg port: <https://vcpkg.io/en/package/podofo.html>.
- Qt forum thread on extending `QPdfWriter` with a widget annotation (needs Qt-internals changes, supports not using Qt's engine for annotations): <https://forum.qt.io/topic/153166/extend-qpdfwriter-for-adding-pdf-widget-annotation-field>.

### 9.7 Owner decisions on 9.5 (6 November 2026)

1. **Writer: `QPdfWriter` plus our own append** (9.1 option B). Fallback D (PoDoFo) only if Qt's output cannot be appended to safely.
2. **u3d call: in-process from the start** (9.2 option 2). The helper-process route is dropped. Consequences and a new first step below.
3. **Text triangulation: CGAL** (already a dependency): `QPainterPath` outlines to polygons to constrained triangulation to flat quad meshes for measurement/annotation text.
4. **Page template: a fixed layout first.** Design note for later (user-defined templates): keep the layout in one `DrawingTemplate` description (page size, border, a list of rectangles with type and anchor: title block cells, parts list, 3D area, logo area, free text with `{field}` placeholders) that the fixed layout is simply the built-in instance of; a later step loads the same description from JSON in the user data folder and adds an editor. The exporter must therefore ask the template for the 3D area rectangle and field values instead of hard-coding coordinates.
5. **Vendoring: a vcpkg overlay port** `overlay-ports/u3d/` (the repo already has `overlay-ports/openimagedenoise` and `vcpkg-configuration.json` points at it). Suggested layout: `portfile.cmake` that fetches the pinned upstream commit with `vcpkg_from_github` (`DmytroSytnyk/u3d`, `5c141d9`, with the archive SHA512), applies patches from `overlay-ports/u3d/patches/`, builds the static libraries with `vcpkg_cmake_configure` (`-DU3D_SHARED=OFF -DCMAKE_CXX_STANDARD=14`), and installs the headers, libs and the Apache-2.0 licence as `copyright`; `vcpkg.json` with version, license `Apache-2.0` and `supports` windows/linux; add `u3d` to the app's `vcpkg.json`. Patches: (a) `IFXSetDefaultLocale` must not fail when `en_US.UTF-8` is missing (use `C.UTF-8`, or ignore a failure), (b) static plugin registration or a plugin path next to the application so no `U3D_LIBDIR` environment variable is needed, (c) any C++17 fixes that remove the need for the `-std=gnu++14` flag (`throw()` specifications), (d) a CMake install of the library so it is a normal vcpkg package. This keeps third-party source out of the app repository.
6. **Branching: everything on `feature/3DPDF`** (research, spike tools and the exporter).

### 9.8 What the in-process choice changes

- **New phase 0 spike (before phase 1):** link the patched u3d static libraries into a small test program under `out/`, drive the conversion through the API from memory (no IDTF file and no converter `main`), write a `.u3d` and compare it byte for byte with the converter output for `lscene`, `mscene` and `stress`. This proves the patches in (b) work on Windows and Linux. If the converter's logic lives in the `IDTF`/`libIDTF` source tree rather than the static library, the test also shows how much of it must be reused.
- **Interface:** the exporter builds an in-memory scene description (nodes, meshes, materials, views); two back ends sit behind one interface: an IDTF text writer (kept for tests and debugging, since the spike files can be diffed against it) and the in-process API writer.
- **Packaging:** no helper executable and no plugin folder to ship; the Apache-2.0 notice and the u3d licence line go into `CREDITS.md`.
- **Risk:** the 2008-era API (COM-like interfaces, `IFXRESULT` codes, thread-unsafe globals); keep all calls behind one wrapper class, create and release the library per export, and run the export off the UI thread only after checking thread safety.

### 9.9 Phase 0 spike result: in-process u3d (6 November 2026)

`tools/3dpdf/inproc/inproc_u3d.cpp` (+ `CMakeLists.txt`, throwaway) builds the `lscene` scene (view, two meshes, light, shaders, materials, shading modifiers) in memory with the `libIDTF` classes (`SceneConverter` from `libIDTF/SceneConverterLib.h`, `NodeList`, `SceneResources`, `ModifierList`, the pattern of `Samples/SampleCode/IDTFGen.cpp`), calls `converter.Convert()` and `SceneUtilities::WriteSceneToFile()`, with no IDTF file and no converter `main`.

- **[Result]** The `.u3d` is BYTE-IDENTICAL to the converter's output for `lscene.idtf` on Windows (VS2022) and on Linux (WSL, GCC, `-std=gnu++14`), and identical across the two platforms. The in-process API is therefore a complete replacement for the IDTF text route; the IDTF writer stays useful only for tests and diffs.
- **[Obs]** Build notes: link the static `IDTF` library (it contains `IFXCoreStatic.cpp` and all the converters), plus `winmm.lib` on Windows; defines `U3D_NO_ASM` and (Windows) `UNICODE _UNICODE _CRT_SECURE_NO_DEPRECATE`, `LIN32` on Linux; the include list is the one in `inproc/CMakeLists.txt`. The C4311 pointer truncation warning in `IFXVoidStar.h` appears again (not investigated).
- **[Obs, important]** "In-process" does NOT yet mean "no extra files". `IFXCoreStatic.cpp` is only a stub that loads `IFXCore.dll`/`libIFXCore.so` at run time (`IFXLoadCoreLibrary`), and the core then scans a `Plugins/` folder and loads the exporting, importing and scheduling modules, each exporting an `IFXPluginRegister` function. The test program only works with `U3D_LIBDIR` pointing at the folder with these libraries. Without them the Windows build crashed with a segmentation fault instead of returning an error code (not retested on Linux); the exporter must check for the modules before calling in.
- **[Hyp, to test next]** Two ways to remove the run-time modules: (a) keep the modules as shared libraries but patch the loader to look next to the application binary instead of `U3D_LIBDIR` (small patch, still ships four libraries); (b) fuse everything into one static library: compile the `IFXCore` sources and the three plugin modules into the application, replace `IFXCoreStatic` by direct calls, and call each module's registration function directly, renaming the colliding `IFXPluginRegister` symbols per module (`-DIFXPluginRegister=IFXExporting_PluginRegister` style). (b) matches the in-process decision best (one binary, nothing to find at run time) but is a larger patch to the vcpkg overlay port; (a) is the cheap fallback.

### 9.10 Owner decision: loader patch (6 November 2026) and its spike result

The owner chose the loader patch (9.9, option a): keep the u3d modules as shared libraries and make them load from the application folder, rather than fusing everything into one static library.

- **[Result, Windows]** No patch is needed. With `IFXCore.dll` next to the executable, the plugin DLLs either next to it (flat) or in a `Plugins\` subfolder, no `U3D_LIBDIR`, and a different working directory, the in-process program produces byte-identical output (both layouts tested).
- **[Result, Linux]** Needed and done, saved as `tools/3dpdf/u3d-patches/0001-linux-static-idtf-and-module-dir.patch` (4 files): (1) `IDTF` is always built STATIC (it was SHARED on non-MSVC) so the executable's `RUNPATH $ORIGIN` governs the `dlopen` calls; (2) when `U3D_LIBDIR` is unset the module directory is the directory of the running executable (`/proc/self/exe`) instead of the compile-time `"."` default, in `libIDTF/IFXOSLoader.cpp`, `RTL/Platform/Lin32/Common/IFXOSLoader.cpp` and `RTL/Platform/Lin32/Common/IFXOSFileIterator.cpp`. The executable must be linked with `RUNPATH $ORIGIN` (`BUILD_RPATH`/`INSTALL_RPATH` in the spike's `CMakeLists.txt`). Result: both layouts (flat and `Plugins/`) give byte-identical output, started from another working directory with no `U3D_LIBDIR` and no `LD_LIBRARY_PATH`.
- **[Result, locale]** The application does not need `IFXSetDefaultLocale` and must not call it: it changes the process-wide `LC_CTYPE`, and with `INPROC_SKIP_LOCALE=1` (call skipped) both platforms still produce identical output, without the private `en_US.UTF-8` locale. **[Caveat, untested]** The library converts file names with `wcstombs`, which in the default "C" locale can fail for non-ASCII paths; the exporter should write to a temporary ASCII file name and move it with Qt, or hold the data in memory, and a test with a non-ASCII path should be added.
- **[Consequence for packaging]** Ship `IFXCore`, `IFXExporting`, `IFXImporting`, `IFXScheduling` (`.dll`/`.so`) next to the application (or in a `Plugins/` subfolder for the three plugins). The `.deb` puts them in the application's library folder; the exporter must verify they exist before calling in (a missing module crashes instead of returning an error code, 9.9). Overlay port tasks shrink to: apply the Linux patch, build `IDTF` static plus the four modules, install them with the headers; the locale patch is no longer needed.

---

## 10. Phase 1 implementation status (6 November 2026)

Written but NOT yet built or run (the owner builds and tests, see the standing no-build rule); nothing of this part is committed.

| Piece | Files | Notes |
|---|---|---|
| u3d vcpkg overlay port | `overlay-ports/u3d/` (`portfile.cmake`, `vcpkg.json`, `u3d-config.cmake`, `usage`, `patches/0001-*.patch`) | Pinned commit `5c141d9f...` with SHA512, builds `IDTF` static and the four IFX modules; installs headers, libs, modules and `find_package(u3d CONFIG)` giving `u3d::idtf` and the `U3D_MODULE_*` variables. Added to `vcpkg.json` and `packaging/linux/vendor/vcpkg.json`. |
| CMake | `CMakeLists.txt` | `find_package(u3d)`, link `u3d::idtf`, copy the modules next to the executable after every build and install them beside it (`CMAKE_INSTALL_BINDIR`), Linux `$ORIGIN` rpath. Sources are globbed, so `src/Pdf3D/*.cpp` and `include/Pdf3D/*.h` need no listing. |
| Scene description | `include/Pdf3D/U3dScene.h`, `src/Pdf3D/U3dScene.cpp` | Plain data: nodes, meshes, materials, bounds. |
| aiScene to U3dScene | `SceneToU3d.*` | World-space node per mesh reference, shared meshes, diffuse/emissive/opacity materials, unique ASCII names, per-file import correction put back. |
| u3d writer | `U3dWriter.*` | In-process libIDTF call from the spike (9.9), module presence check, temporary ASCII file name, no `IFXSetDefaultLocale`. |
| Views | `Pdf3DViews.*` | Seven named views (Isometric, Front, Back, Left, Right, Top, Bottom), Y-up mapping, camera matrix per 8.6/8.14. |
| PDF update | `PdfAppender.*` | Appends 3D stream, appearance, annotation and a rewritten `/Annots` array as an incremental update to a `QPdfWriter` page; refuses xref streams. |
| Sheet and orchestration | `Pdf3DExporter.*` | A4 landscape sheet (border, 3D area, title block with title, source names, part count, date) with the viewport grab as the non-3D poster. |
| UI | `ui/App/MainWindow.ui`, `MainWindow.cpp`, `ModelViewer.h/.cpp` | File > Save as 3D PDF... (`ModelViewer::exportAs3DPdf`): visible parts only (`isMeshDisplayed`), Y-up. |
| Tests | `tests/pdf3d_tests.cpp`, `tests/CMakeLists.txt` | Views, aiScene conversion, appender on a real `QPdfWriter` page (this also verifies the Qt structure assumption of 9.1), in-process conversion; `--full` runs the whole exporter. Offscreen Qt platform. Enabled with `-DMV_BUILD_TESTS=ON`. |

Known gaps in this first cut (all planned): no title-block fields editor or parts list (phase 2), no measurement or annotation layer (phase 3), saved cameras and scene states are not turned into views (phase 4), no view buttons (phase 5), textures and PBR are dropped, the up axis is fixed to Y, the file name is converted through the library only via a temporary ASCII path, packaging of the modules in the installer and `.deb` relies on the CMake `install(FILES ...)` added here and is untested.

**Installing the port (manual vcpkg step).** The Windows dev configurations are configured with `VCPKG_MANIFEST_INSTALL=OFF`, so adding `u3d` to `vcpkg.json` does not install it; CMake then stops with "Could not find a package configuration file provided by u3d". Install it once per install folder, from the repo root:

`.\vcpkg\vcpkg.exe install --triplet x64-windows --x-manifest-root=. --x-install-root=out\build\ninja_release_vcpkg\vcpkg_installed --overlay-ports=overlay-ports`

(repeat with the other install folders in use, e.g. a Debug configuration). On Linux the `.deb` path takes it from `packaging/linux/vendor` (`setup-wsl-build-env.sh` installs that manifest with `--overlay-ports=overlay-ports`, so it now includes `u3d`). If the port build fails, the logs are in `vcpkg\buildtrees\u3d\`.

**Phase 2 (title block fields and parts list).** The File > Save as 3D PDF action first shows `Pdf3DExportDialog` (title, drawing number, revision, author, company, scale, notes, "Add a parts list sheet"; everything except title and notes is remembered in QSettings `pdf3d/...`, the dialog keeps its geometry), then the file dialog. The title block is a left cell (title, source file, notes) plus a 2 x 3 field grid (drawing no., revision, author, date, company, scale | parts | sheet n/N) and is drawn identically on every sheet. The parts list is built from the exported nodes: one row per distinct (part name, mesh) pair in first-appearance order, with material name and quantity; it is paginated onto extra A4 sheets after the 3D sheet (`QPdfWriter::newPage()`), the 3D annotation stays on page 1. The menu action is enabled only while the active document has meshes. Also fixed in phase 1 testing: model nodes are written with visibility BOTH (the U3D default FRONT culls back faces, which made thin shells such as the skateboard tyres look see-through), and the standard views follow the viewport's up-axis mode (STEP opens Z Up).
