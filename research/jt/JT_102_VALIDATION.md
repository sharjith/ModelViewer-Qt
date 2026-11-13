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

Every image is type 2 on texture unit zero with Tex Coord Channel `-1`, no
generated S/T/R/Q components and `MODULATE`. The retained Siemens JT 10.0
reference distinguishes the texture unit from Tex Coord Channel: the latter
associates a texture with a separate coordinate-generator attribute. `-1`
therefore does not select UV set zero automatically. For the sampled profile,
each mesh has exactly one stored UV set and no generated component, so the
adapter resolves that sole set without guessing. Zero stored sets remain
untextured; multiple stored sets remain ambiguous and are rejected with a
diagnostic.

JT type 2 is post-lit. The shared Assimp material, raster, path-tracing and
export contract can represent complete post-lit RGB/RGBA Replace as unlit, but
cannot express a post-lighting product exactly. The sampled `MODULATE` is kept
visible as ordinary pre-lit base-colour modulation and emits a deduplicated
approximation warning. This is a deliberate portable viewing approximation,
not a claim of exact post-lit specular equivalence.

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
geometry while diagnosing the then-unqualified texture mapping, PMI Manager
version 2 and one unresolved metadata pair per part. The texture-appearance
follow-up described above is prepared but has not yet been built or run.

```text
<jt-test-build>/jt_tests
<jt-test-build>/jt_probe <private-T102-A.jt> --validate
```

For the remaining application gate, open unchanged T102-A and verify that all fourteen
parts are present, hierarchy and transforms are usable, fitting/selection work,
and no missing-display-content error occurs. Compare geometry against a trusted
viewer. Record texture appearance separately. Compare image placement,
orientation and repeat behaviour against a trusted viewer in ADS and PBR, and
confirm the post-lit approximation warning is present. Exact post-lit
multiplication is outside this gate.

## Reference provenance for the appearance follow-up

The interpretation above uses the public *JT File Format Reference, Version
10.0, Revision B* published by Siemens PLM Software, retained locally with
SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`:

<https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf>

Sections 7.2.1.1 and 7.2.1.3 were consulted for Texture Image and Texture
Coordinate Generator attribute semantics. No vendor SDK, translator,
implementation source or disassembly was used. This records the method and
does not establish redistribution or legal clearance for the private sample.

## Owner texture-appearance acceptance

On 13 November 2026 the owner tested unchanged T102-A after the appearance
follow-up and reported that the textures had landed perfectly. This accepts the
sampled file's image assignment, placement and visible appearance. It does not
turn the diagnosed pre-lit approximation into exact post-lit multiplication,
qualify ambiguous multi-UV `-1` mappings, or replace the pending native
regression run for this follow-up.

## Reproducible read-only inventory

The general structural inventory can be regenerated without copying the input:

```text
python -B tools/jt/inspect_jt_structure.py <private-T102-A.jt> --output out/jt-research/jt102-candidate.structure.json
```

The deeper local inventory used an ignored scratch parser under
`out/jt-research/` to decode only element headers and texture descriptors. Its
JSON and script remain ignored because they contain private-file working
context. No texture image was exported, rendered or added to the repository.
