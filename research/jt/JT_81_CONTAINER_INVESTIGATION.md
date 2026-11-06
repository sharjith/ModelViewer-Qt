# sample L81: JT 8.1 compatibility investigation

19 October 2026, `feature/JT-importer`. The owner supplied the local path after the vertex-colour viewer tests passed and subsequently made backward compatibility an explicit product requirement. The subsequent [JT 8.1 implementation](JT_81_VALIDATION.md) now loads sample L81 in the isolated native probe; application UI checks remain pending. The initial observations and task list below record the investigation before that implementation.

## Observed container

Local file: `<local-samples>/sample-L81.jt`, downloaded by the owner from GrabCAD. No exact download URL or redistribution permission is recorded. The file remains external to the repository.

- Header: JT **8.1**, TechSoft3D JT writer **2.2**.
- Size: **3,516,358 bytes**.
- SHA-256: `7e0bdcccaaa844052ee53897908ce524316ee5653874d5dbb1f2295d0ef33a28`.
- Little endian, 32-bit TOC offset **105**, **1,085 TOC entries**.
- The directory lists one type-1 logical scene graph segment and **1,084 type-7 shape-LOD segments**. This is not a mesh or occurrence count; multiple LODs and repeated instances require graph traversal to interpret.
- All directory segment ranges fit within the file under the legacy 28-byte TOC-entry interpretation. This is only a container sanity check, not proof that the contents are valid or renderable.

The reader initially rejected this file at the version gate, before decoding its graph or geometry. The screenshot therefore identified a supported-version boundary; it did not diagnose corruption, a colour bug, or a missing XT backend. The subsequent implementation accepts JT 8.1 and JT 10.0 and reports the actual version in errors for other versions.

## Separate compatibility work required

The vendored legacy reader contains pre-9 branches, but their existence is not qualification. Enabling the header alone would expose untested paths. Review the matching authoritative layout and establish a bounded JT 8.1 path for:

1. The 32-bit file/TOC layout, segment compression, element envelopes and object identifiers.
2. Older graph, property and attribute layouts, including differences from JT 10.0 field-final and local-version handling.
3. CDP1 packets, legacy quantized/lossless vertex arrays, normal/colour/UV bindings and triangle-strip reconstruction. The old raw-vertex path currently ignores colour/UV bindings and does not check every array read; it must be bounded and corrected before exposure.
4. Sample counts/bounds/placements, usable-LOD selection, malformed/truncated data, cancellation, and independent visual comparison. The owner should compare with an independent viewer before accepting geometry fidelity.

The implementation scope already requires separate version qualification and a check of the applicable published permissions for older layouts; see [feasibility research](JT_IMPORT_FEASIBILITY.md) and [implementation scope](JT_PARASOLID_IMPLEMENTATION_SCOPE.md). The references currently archived for JT 9.5/10.0/10.6 are not treated as a matching JT 8.1 specification. No new clearance claim or automatic support for other 8.x/9.x versions follows from this sample.

## Initial legacy-path inspection

The pinned decoder already contains pre-9 file, graph and vertex-array branches, so reuse and correction are preferable to treating JT 10.0 as the permanent scope. The initial review identified concrete work before exposing those paths:

- The newer base/string property guards currently check a local version even when the older layout has no such field. The original scalar additions also unconditionally read local versions. These must dispatch by layout rather than apply JT 10 assumptions to older records.
- The sample L81 graph uses zlib compression. Its directory/header segment length is 239,012 bytes, while the compression field and complete zlib stream require 239,013 bytes through the file end. Reading only the directory length truncates the zlib checksum. This discrepancy must be resolved against the older layout/exporter behavior with strict physical bounds; it is not a reason to remove segment bounds globally.
- Read-only graph-envelope inspection found group/part/RangeLOD/TriStrip nodes, materials, transforms and string/float/date/late-loaded properties. This inventory does not prove correct scene traversal or geometry decoding.

This investigation establishes specific compatibility tasks. It does not yet establish sample L81 mesh counts, dimensions, fidelity, or blanket support for all older JT versions.
