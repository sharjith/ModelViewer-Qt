# JT 10.2 sampled-subset qualification

13 November 2026, `dev`. This gate adds explicit JT 10.2 support from one
owner-supplied authentic file, identified here as **T102-A**. The original name,
path, geometry and texture pixels remain private and untracked.

## Evidence and scope

T102-A is a little-endian JT 10.2 file, 94,453,840 bytes, SHA-256
`e4da4aa46616308a9981bc34ab5dda5af3239b515527780cb7c54024ab9c26ee`.
Read-only structural inspection finds 45 TOC entries: one LSG segment, fourteen
stored triangle-strip shape-LOD segments, fourteen type-3 PMI segments, fifteen
metadata segments and one type-31 segment. No XT or JT B-Rep segment is present.
The graph has fourteen part/group/range-LOD/shape paths and uses record GUIDs
already registered by the reader, plus one unregistered metadata GUID repeated
fourteen times. Unknown unwrapped metadata remains envelope-bounded and is not
claimed as decoded.

The LSG contains fourteen distinct inline texture images. Thirteen are
2048 x 2048 RGB unsigned-byte images and one is a 1024 x 1024 RGBA unsigned-byte
image. Each has one mip level, alignment four, no image border and modern
texture local version 1. Their raw payloads total 160 MiB and have fourteen
different hashes. Unlike qualified 10.3-and-later texture fixtures, these
authentic 10.2 attributes use base version 1 without Sabot wrappers, matching
the qualified 10.0 envelope profile.

Every image is type 2 on texture unit zero with coordinate channel `-1`.
Automatic coordinate-channel assignment is outside the accepted explicit-set
texture profile. This gate therefore enables the sampled 10.2 container and
stored-geometry family while preserving geometry fallback and diagnostics for
the unapplied images. Automatic assignment is a separate appearance gate; a
successful untextured load must not be reported as textured fidelity.

This is representative sampled-subset evidence, not universal JT 10.2 support.
It does not qualify other tessellation codecs, exact geometry, every PMI record,
the unknown metadata atom, external partitions or other texture layouts.

## Prepared implementation and checks

The file-version whitelist adds only minor version 2; it is not replaced by a
numeric range. Existing original LE/BE container, coordinate, UV, quantized
colour and texture tests add 10.2. Texture fixtures use base-v1/unwrapped records
for 10.0 and 10.2, and retain base-v2/Sabot records for qualified 10.3 and later
families. The unsupported-version test continues to reject 10.1 and 10.4.

Native validation passed on 13 November 2026 using a clean Release build with
MSVC 19.52.37029. The registered CTest suite passed 1/1 (`jt_codec_tests`) in
134.34 seconds. `jt_probe --validate` read unchanged T102-A in 7.976 seconds and
reported fourteen meshes and occurrences, 2,771,106 vertices, 1,690,666
triangles, no fallback parts, finite bounds and UV channels on all fourteen
meshes. Charged decoder allocations were 186,795,057 bytes. The probe retained
geometry while diagnosing the unqualified automatic texture mapping, PMI
Manager version 2 and one unresolved metadata pair per part. UI comparison is
the remaining owner gate.

```text
<jt-test-build>/jt_tests
<jt-test-build>/jt_probe <private-T102-A.jt> --validate
```

For the remaining application gate, open unchanged T102-A and verify that all fourteen
parts are present, hierarchy and transforms are usable, fitting/selection work,
and no missing-display-content error occurs. Compare geometry against a trusted
viewer. Record texture appearance separately: until automatic coordinate
assignment is qualified, absent textures with a clear diagnostic are expected.

## Reproducible read-only inventory

The general structural inventory can be regenerated without copying the input:

```text
python -B tools/jt/inspect_jt_structure.py <private-T102-A.jt> --output out/jt-research/jt102-candidate.structure.json
```

The deeper local inventory used an ignored scratch parser under
`out/jt-research/` to decode only element headers and texture descriptors. Its
JSON and script remain ignored because they contain private-file working
context. No texture image was exported, rendered or added to the repository.
