# JT scalar metadata preservation

19 October 2026, `feature/JT-importer`. Follows the accepted display-selection milestone `b72d2be`. The owner rebuilt the application and confirmed that both metadata fixtures open with the expected 913 vertices and 904 triangles. Together with the automated property-value and ownership checks, this pass is accepted for commit.

## Scope

The importer retains node property pairs whose keys are strings and whose values are strings, signed I32 integers or F32 numbers. The original property-table order, repeated keys, atom IDs and key/value state flags survive as indexed records under the Assimp node metadata key `JTProperties`:

```text
JTProperties -> "0" -> Key, Value, KeyAtomId, ValueAtomId, KeyStateFlags, ValueStateFlags
             -> "1" -> ...
```

`Value` keeps its Assimp string/int32/float type. Flags are stored as uint64 because Assimp metadata has no uint32 type. They are retained without assigning application-specific meaning. Properties remain attached to their authored node; they are not inherited as material attributes. Raw `JT_PROP_NAME` values remain available even when the display name is shortened at the existing semicolon delimiter.

The existing late-loaded representation reference path remains separate. Unknown/unresolved optional atom pairs are diagnosed and omitted. Long strings or strings with embedded nulls, and nonfinite numeric values, are diagnosed and omitted rather than silently truncated or passed on as valid values. The Assimp representation accepts UTF-8 strings shorter than `AI_MAXLEN` (1024 bytes including the terminator); the source decoder retains the original property atoms before this adaptation.

Property-table version 1, base-property versions 1/2, string-property versions 1/2, and integer/float local version 1 are qualified. The five local samples use base version 2 and string version 2 with the same exercised scalar layout; their real floating-point property uses local version 1. Generated fixtures cover these layouts and reject unqualified versions. This does not enable another JT file version: acceptance remains JT 10.0.

Property processing is cancellable, with limits of 100,000 pairs per node and 1,000,000 pairs per LSG property table, plus the existing per-import allocation budget. Repeated property-table entries for the same graph object fail explicitly. Repeated keys inside a valid entry are retained.

## Viewer integration and limits

`SceneUtils::deepCopyScene` previously discarded node metadata. It now deep-copies each node's metadata, including nested records, so the owned import scene retains it after source teardown. This is exercised using the actual application copy implementation in the isolated tests.

This pass does not add a property browser, fields to the separate editable `SceneNode` hierarchy, metadata persistence in MVF, or unit conversion from property strings. The data is preserved in the imported Assimp scene and can be inspected with `jt_probe --properties`. Source units and PMI hints present in the supplied sample are retained as metadata, without claiming complete semantics for them.

The probe can still export geometry as GLB. In a direct Assimp GLB export, child-node `JTProperties` records are emitted in `extras`, but the root node's metadata is omitted by the exporter. This pass does not claim lossless property export or fix that downstream exporter behavior. Probe JSON includes the source root properties.

Date, object-reference, vector, proxy and other property atom types, non-string keys, rich PMI and texture/face-palette interpretation remain unsupported in this pass. Unknown optional metadata does not invalidate otherwise readable tessellation.

## Evidence and reproduction

The standalone targets are configured from `tests/jt` with MSVC AddressSanitizer enabled for the native importer/test suite. Tests cover:

- Original little- and big-endian containers containing Unicode text, a negative integer, an F32 value, repeated keys, atom IDs/flags, source-local property ownership, long text, NaN and an unknown atom.
- Qualified and rejected base/string/integer/float/table versions and duplicate node tables.
- Per-node property count rejection and cancellation while processing properties.
- Deep-copy ownership of root and child nested metadata after the decoded source scene is destroyed.
- The existing assembly, Switch, RangeLOD, material, transform and fallback regressions, and the five local part models with unchanged geometry counts, normals and colors.

The fixture serializer and generator are in the repository. Build `jt_fixturegen`, then run:

```text
jt_fixturegen --samples out/jt-research/samples --output out/jt-research/metadata-fixtures
jt_probe out/jt-research/metadata-fixtures/metadata.jt --properties
```

The generator needs a locally supplied `Bolt.jt`; it does not download a model or convey redistribution rights. Both generated JT files (`metadata.jt` and `metadata_big_endian.jt`) should load one opaque Bolt with 913 vertices and 904 triangles. The root display name is `Metadata assembly`. Root JSON contains six supported records: the full source name, Unicode PartNumber, Count=-7, Mass=12.5 and two distinct Repeated values. A child has its own Repeated value. The fixture intentionally reports one unsupported pair and two unrepresentable pairs.

The generated folder, local geometry, logs and exported GLBs remain in ignored `out/`. The original generators, tests, format decisions and validation notes are in the repository. Full application build/UI acceptance is left to the owner; the probe is the current way to inspect metadata values.

## Reference and provenance

Layout basis: published [JT 10.0 Data Format Reference](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), sections 6.2.1–6.2.4 and 6.3, with the stated sample-qualified local version 2 layouts. The archived reference is not redistributed.

`JtProperty_Scalar.cxx/.hxx` are original ModelViewer GPLv3 additions. Existing adapted files retain their upstream notices. The modification summary and provenance manifest record this pass. No proprietary code, XT catalogue, exact-geometry backend or additional JT file-version claim is added.
