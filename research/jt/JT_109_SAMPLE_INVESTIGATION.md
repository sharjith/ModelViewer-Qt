# JT 10.9 community sample: initial investigation

19 October 2026, `feature/JT-importer`. The owner confirms JT 8.1 works and reports a new unsupported-version error for this file. No JT 10.9 support is enabled by this investigation.

Local file: `<local-samples>/sample-A109.jt`. The original is unchanged and remains outside the repository. No exact download URL or redistribution permission is recorded.

## Observations

- Header: `Version 10.9 JT  DM 10.7.0.1`. The file version and writer build are distinct values.
- Size: **49,421,977 bytes**.
- SHA-256: `9374d1dcd7728b681d84ffe6edb63a87b3c4cefc01738a2d812daeb1721dbc89`.
- Little endian, 64-bit TOC offset 109, **19,467 entries**. Directory segment ranges fit within the file.
- Segment-type inventory: 1:1, 3:1073, 4:3938, 7:2865, 8:2865, 9:2865, 17:2865, 23:2863, 30:131, 31:1. These are segment counts, not rendered mesh/occurrence counts.
- The type-1 scene graph uses LZMA/XZ. Bounded decompression yielded **13,812,082 bytes**.
- Envelope inspection identifies base-attribute version 2, palette-map attributes (`10dd1106-2ac8-11d1-9b6b-0080c7bb5997`) and Sabot wrappers (`96603dd3-5a0f-40f5-97cb-1e9647cbd37e`), alongside groups, instances, parts, RangeLODs, TriStrip shapes, transforms, materials and properties.

To locate the first decoding incompatibility, a temporary diagnostic copy under ignored `out/jt-research` had only its textual header changed to 10.0. This is not a file conversion, a qualified compatibility mode, or a delivered workaround. At the initial investigation checkpoint the native probe stopped with **Unsupported JT base attribute version**. The original sample and production version gate are unchanged.

## Published layout basis and next work

The [version compatibility policy](JT_VERSION_COMPATIBILITY.md) places this sample within the staged 6.4–10.11 visualization target (conditional 7.0 lower cap for substantial 6.4-specific viewer model changes). It is the next forward-version qualification case; it remains unsupported until its attributes, face-group appearance and tessellation pass validation. Shipping 10.x evidence extends to 10.11, while the public Siemens reference located remains 10.6 Revision D.

The official [Siemens JT Open Program](https://www.siemens.com/en-gb/products/plm-components/jt/jt-open-program/) currently links the JT 10.6 reference. The already archived 10.6 Revision D reference documents base-attribute versions 1/2 and the observed palette/Sabot mechanisms. A matching 10.9 reference was not located in the initial search; this does not establish that one is unavailable.

In that published 10.6 layout, version-2 base fields include a palette index **after** the derived attribute data. It must not be consumed as part of the leading base header. Section 5.4.8 and Annex E describe palette mapping by face group, including fallback and inhibited groups. Section 5.4.9 describes Sabot wrappers: the wrapped attribute replaces the wrapper in the decoded graph. Merely accepting version 2 and skipping its trailing bytes would not implement those semantics.

The next compatibility work should therefore:

1. Decode and retain trailing palette fields without disturbing version-1 attributes or JT 8.1 records.
2. Unwrap Sabot records with checked inner/outer envelopes and correct object identity.
3. Accumulate palette entries and resolve face-group maps, with fallback/inhibition and occurrence overrides preserved.
4. Qualify the selected tessellation layouts/codecs and retain stored-mesh fallback when exact geometry is unavailable.
5. Add original binary fixtures, malformed-input cases and corpus checks before accepting the sample's file version. Do not infer support for every 10.x minor version from one successful sample.

No proprietary SDK or third-party translator implementation was inspected or added. Counts and attribute observations above are an input inventory, not a rendering-fidelity result or claim of complete JT 10.9 specification coverage.

## Active implementation checkpoint

Gate 1 is accepted. Phase 2 starts with [Gate 2a](JT_MODERN_ATTRIBUTE_VALIDATION.md): checked version-2 tails and Sabot identity/envelopes, while explicitly rejecting palette-scoped rendering. Gate 2b will resolve palette accumulation and face-group geometry/appearance before enabling 10.9. The production version gate is unchanged in Gate 2a.

On 20 October 2026 the same diagnostic copy passed the newly implemented attribute tails and Sabot replacement, then stopped at the explicit **unsupported palette map** boundary. This confirms the next required feature; it is not a geometry result or a production version-gate change.

## Native production-path qualification

The subsequent [palette follow-up](JT_109_PALETTE_VALIDATION.md) now imports the unchanged original with its actual 10.9 header and the normal 6 GiB cumulative budget. Native validation passes; owner production-system acceptance remains pending at Gate 2b. Earlier unsupported-version/attribute observations above describe the original investigation, not the current result.
