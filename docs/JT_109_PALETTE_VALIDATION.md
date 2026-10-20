# JT 10.9 palette and face-group qualification

20 October 2026, `feature/JT-importer`. The owner accepted Gate 2a and prioritized current-version coverage: finish Gate 2b (10.9), then qualify 10.10/10.11 before older-format expansion. No 10.10/10.11 input is currently available; the owner will test 10.9 on a separate production system. The owner subsequently confirmed successful UI testing of the supplied Solid Edge 10.9 export; see the acceptance record below.

## Implementation

- Read palette maps and accumulate independent material, draw-style and transform states. A new palette entry starts from the current fallback state. Later fallback attributes accumulate into every existing entry. Preserve final/inhibit/force flags and isolate sibling occurrences.
- Retain topological face-group IDs alongside emitted triangles. Resolve group maps against the palette. A map with a different length from the shape's group count is ignored with a fallback diagnostic, as specified in Annex E.
- Merge groups with equal appearance, transform and visibility before generating compact render subsets. Cache the resulting resources independently of occurrence placement. Stored normals, supported colours and UVs remain aligned with their vertices.
- Map entry -1 selects fallback appearance. Entry -2 retains the geometry in a hidden viewer node. **Limitation:** the viewer's existing visibility controls also affect picking; this approximates render-only inhibition. Retain a diagnostic rather than claiming exact inhibition/picking parity.
- Read modern arithmetic escape values as a raw vector in externally compressed segments, and as nested CDP data in ordinary mesh segments. Do not infer this distinction from a minor version number.
- Accept the published four-byte palette-map fallback tail and the observed omitted redundant tail only at an exact bounded packet end. Reject partial or unexplained trailing bytes. Other version-2 attributes retain their required tails.
- Normalize numerical noise in an affine matrix's final row only within 64 double-precision epsilons. Larger projective components remain unsupported. All 1,964 inspected A109 transform records have a maximum final-row residual of 2.220446049250313e-16.

## Native evidence and remaining checks

The isolated AddressSanitizer suite passes palette inheritance, material groups, fallback/inhibition, final/force/inhibit flags, maps, transforms, sibling isolation, shared render resources, binary tail/packet checks, arithmetic raw/nested escapes and GLB preservation. See `jt_palette_tests.cpp` and the existing modern attribute tests. The suite uses original synthetic geometry.

Sample A109 identification (size/hash/header) remains in the [initial investigation](JT_109_SAMPLE_INVESTIGATION.md); source geometry and original filenames remain outside the repository. The diagnostic header copy is only an investigation tool. It is not a converted file or production workaround.

The initial diagnostic import exhausted the previous 4 GiB cumulative budget. A measured run completed at 4,639,648,781 charged bytes after optimization. This counter includes released scratch arrays and copied state; it is not live memory. Unchanged palette state is shared until modified, groups with identical resolved state are merged, and sorted index vectors replace per-vertex tree remapping. The default cumulative budget is now **6 GiB**; callers can still lower `Options::decodedAllocationBudget`. The **256 MiB per-allocation limit**, bounded segment/packet reads and cancellation remain unchanged. An observed instrumented working set of about 3.1 GB is an observation, not a guaranteed peak or app memory requirement.

The diagnostic run produced 3,254 render resources, 606,493 vertices, 685,079 triangles and 9,587 mesh occurrences, with 49,916 nodes before redundant-node purging and 9,054 parts using stored-tessellation fallback. Local resource bounds were approximately (-2202.14, -2708, -219) to (1703.11, 1143.73, 1092). These are regression observations, not independently established ground truth or transformed world bounds.

The unchanged original input now passes the production 10.9 path with the normal 6 GiB cumulative budget and no header change or override. Its SHA-256 remains unchanged. Native counts and bounds match the diagnostic observations above. Full portable tests, all five 10.0 parts and the 8.1 geometry/cancellation regression pass. The large 10.3 regression also passed unchanged: 2,468 resources, 2,853 occurrences, 4,450,421 vertices and 5,844,669 triangles. Additional 8.0 and 8.2 probes pass. Source-provenance, whitespace and local documentation-link checks pass. No 10.10/10.11 version is enabled.

Logs stay under ignored `out/jt-research/palette-*`. The owner builds the full application. Gate 2b has owner-confirmed sampled 10.9 display acceptance; the original A109 production-system palette/face-group comparison remains a separate pending check. The owner authorized independent subsequent work; the selected next available input is now covered by [Gate 3a](JT_95_VALIDATION.md).

## References and provenance

Use the archived public Siemens JT 10.6 Revision D reference: attribute/palette and Sabot sections 5.4.8/5.4.9, compressed packets section 10.2.1 and per-face-group Annex E. Observations of the original sample corroborate packet boundaries and matrix residuals; they do not constitute full 10.9 specification coverage. See [compatibility policy](JT_VERSION_COMPATIBILITY.md), [licensing research](JT_IMPORT_FEASIBILITY.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).

The new palette-map adapter and tests are original ModelViewer work. Existing upstream GPL notices remain; no additional translator source, restricted SDK or downloaded CAD geometry is bundled. The decoder provenance manifest covers 104 upstream-derived and fourteen original source/header files.

## Gate 2b: sampled 10.9 display accepted; production comparison pending

Rebuild `feature/JT-importer` on the production system. Open the unchanged original A109 JT input. Confirm complete geometry, plausible authored placement, face-group colours, normals/shading, hierarchy, selection and fit-to-screen. Compare to an independent source preview where available. Native **render-resource** counts and UI **occurrence** counts are different: the current measured baseline is 3,254 resources and 9,587 occurrences. Optional redundant-node purging may change hierarchy depth.

`out/jt-research/palette-ui-fixtures/palette_three_colours.glb` contains only original synthetic geometry: three separate red, green and blue triangles, three meshes, nine vertices and three triangles. This checks the generated scene/material contract; it is not an independent JT format-conformance oracle. Generate it with `jt_tests --palette-fixtures out/jt-research/palette-ui-fixtures`.

Reopen known-working 8.x, 10.0 and 10.3 inputs for placement/colour/normal regressions. The owner's 9.5 input is identified as A95 and is now the separately authorized Gate 3a target. The owner authorized committing and pushing this implementation on 20 October 2026 so it can be pulled, built and tested on the production system. This distribution commit does not record Gate 2b acceptance. Record the production-system outcome independently; subsequent research or Gate 3a results do not accept Gate 2b.

## Owner acceptance update — 20 October 2026

The owner confirmed successful viewer testing of the supplied Solid Edge JT 10.9 assembly export (neutral alias SE10_9). Gate 2b therefore has sampled 10.9 assembly/display acceptance. Its native baseline is 23 render resources, 30 occurrences, 210,257 vertices and 184,618 triangles. The export excludes precise geometry and exercises stored tessellation with external part references. This confirmation does not establish that every palette/face-group variant was exercised. The original A109 production-system check remains pending as additional coverage, rather than leaving all 10.9 UI qualification pending. Gate 3b acceptance is independent and remains pending.

## Additional appearance regression — 20 October 2026

A second owner-exported 10.9 assembly differs in colour from its Solid Edge reimport. The [cross-version investigation](JT_CROSS_VERSION_APPEARANCE_INVESTIGATION.md) records repeated fallback material attributes in three parts and the published list-order rule. Source-derived controls subsequently confirmed that Solid Edge selects the earlier material in the observed duplicate-material shape. A narrow writer/author/record compatibility profile passes native checks, and the owner accepted the ModelViewer colour correction on 20 October with "That fixed"; see the investigation for its guards, opt-out and test paths. This acceptance is specific to the colour correction. The owner confirmed the rough 9.5 oil-pan appearance also occurs in Solid Edge and is an export characteristic. Gate 3b remains pending independently.
