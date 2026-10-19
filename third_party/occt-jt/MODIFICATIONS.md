# ModelViewer-Qt modifications to JT-Assistant

Recorded 19 October 2026; functional work performed 18 October 2026, notices added 19 October 2026. Upstream: Open-Cascade-SAS/JT-Assistant, commit e859e8c2d809f9d9468e6211eba0052f433978ee.

Original copyright, GPL-2.0-or-later permissions and warranty notices remain in the upstream-derived files. ModelViewer-Qt selects GPLv3 for the combined work. Dated notices identify modified upstream files. PROVENANCE.json maps each included source/header to its upstream path and original SHA-256, distinguishes original additions, and records whether its pre-notice contents differed (including whitespace). The original decoder additions use GPLv3.

Changes include:

- Modern OCCT handles, RTTI, hashing, standard stream types and API compatibility.
- Explicit JT 10.0 acceptance, 64-bit TOC addressing, corrected versioned graph/attribute and shape-envelope layouts, complete XT payload headers and material alignment.
- XZ/LZMA decompression, bounded file/segment/buffer access, allocation/count/recursion limits, cancellation and explicit diagnostics.
- Mark-3 integer bitlength and move-to-front decoding, modern topology packet masks, binary component streams and packed Deering normal handling.
- Corrected ownership, vector cleanup, integer/bit bounds, arithmetic packet validation and truncated-stream behavior.
- Disabled upstream decoder threading and original ModelViewer CMake integration using OCCT, zlib and LibLZMA. No viewer, Eigen copy or commercial XT converter is included.
- Source whitespace normalized during import. The manifest records provenance; the Git history and upstream comparison provide the exact changes, not a promise that every listed subsystem changed in every file.

The adapted reader supports the tested JT 10.0 tessellation subset. It does not implement standalone Parasolid conversion or provide IP indemnity. See docs/JT_NATIVE_IMPORTER.md and docs/JT_IMPORT_FEASIBILITY.md in the source tree for coverage and permission limitations.

## Assembly/attribute follow-up, 19 October 2026

JtAttribute_Base.hxx now exposes the decoded accumulation flags; JtAttribute_Base.cxx rejects unqualified base-attribute versions. JtAttribute_Material.cxx/.hxx consume/expose reflectivity and bumpiness, initialize their defaults and reject unqualified material versions. JtData_Model.cxx rejects duplicate IDs and invalid unknown-element envelopes. The new source-level assembly/material/LOD handling is original ModelViewer code under src/Import/Jt, outside this vendored subset. See docs/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md for tests and limits.

## Display-selection follow-up, 19 October 2026

JtNode_LOD.cxx and JtNode_RangeLOD.cxx reject unqualified local versions; RangeLOD also validates distance limits and centre values. JtNode_Switch.cxx/.hxx are original GPLv3 additions implementing the published JT 10.0 selected-child layout. The original ModelViewer scene builder implements traversal, optional eye-distance selection and rollback. See docs/JT_DISPLAY_SELECTION_VALIDATION.md for evidence, static-selection limits and pending viewer checks. No additional JT file version or XT conversion is enabled.
