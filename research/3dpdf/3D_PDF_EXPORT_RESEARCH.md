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
