# Shared texture control resources

22 October 2026, `feature/JT-importer`. Owner-requested storage update during the C2n visual gate. Control authoring and read-only path checks completed; the owner built the viewer and the agent launched the positive C2n set at the owner request. The owner reported the C2n visual tests passed on 22 October 2026, accepting that gate and its shared-resource layout. No native build/suite run by the agent.

## Current layout

Numeric image and 9.x texture generators now place all JT roots in `out/jt-research/texture-common-ui-fixtures/`, with gate-prefixed filenames. Shared geometry, image and buffer resources are stored once per SHA-256/suffix under its `assets/` child. Canonically identical glTF reference documents are also stored once in the common root, named `reference_<SHA256>.gltf`. Each requested gate output folder retains its `authoring.json` manifest, mapping original logical case labels to shared files. Gate-folder companion copies are removed after publication; only explicitly named generated files in that gate are eligible for cleanup. No recursive delete or source-directory cleanup occurs.

JT restricts resource lookup to the opened root's directory tree. Co-locating roots above `assets/` preserves that restriction; sibling-folder traversal is not enabled in the importer. JT geometry/external-image references and glTF buffer/image URIs point into the shared assets directory. Necessary inline image payloads remain embedded in each inline JT case to exercise its declared numeric type and byte order. Different stored/no-UV geometries and genuinely different UV/reference buffers are retained as distinct assets.

The original source, transform and planar authoring input sets remain inputs to the generators; this pass consolidates the repeated copies formerly made for each numeric and 9.x gate. Those original input sets are not removed. No GLB copies are authored by these two generators.

| Gate manifest folder under `out/jt-research/` | Shared JT filename prefix |
| --- | --- |
| `texture-pixel-ui-fixtures` | `texture-pixel_` |
| `texture-u16-ui-fixtures` | `texture-u16_` |
| `texture-u32-ui-fixtures` | `texture-u32_` |
| `texture-float-ui-fixtures` | `texture-float_` |
| `texture-half-ui-fixtures` | `texture-half_` |
| `texture9-ui-fixtures` | `texture9_` |
| `texture9-advanced-ui-fixtures` | `texture9-advanced_` |
| `texture9-u16-ui-fixtures` | `texture9-u16_` |
| `texture9-u32-ui-fixtures` | `texture9-u32_` |
| `texture9-float-ui-fixtures` | `texture9-float_` |
| `texture9-half-ui-fixtures` | `texture9-half_` |
| `texture-s8-ui-fixtures`, `texture-s16-ui-fixtures`, `texture-s32-ui-fixtures` | `texture-s8_`, `texture-s16_`, `texture-s32_` |
| `texture9-s8-ui-fixtures`, `texture9-s16-ui-fixtures`, `texture9-s32-ui-fixtures` | `texture9-s8_`, `texture9-s16_`, `texture9-s32_` |

Modern logical `rgb_le.jt` becomes e.g. `texture-u32_rgb_le.jt`. A 9.x logical `9_5/v3/planar/texture_inline_le.jt` becomes `texture9-u32_9_5_v3_planar_texture_inline_le.jt`. Shared glTF filenames come from the manifest's `references` mapping, because multiple logical comparisons may resolve to the same physical document.

For example, list the C2n reference mappings without opening the viewer:

```powershell
$modern = Get-Content out/jt-research/texture-u32-ui-fixtures/authoring.json -Raw | ConvertFrom-Json
$legacy = Get-Content out/jt-research/texture9-u32-ui-fixtures/authoring.json -Raw | ConvertFrom-Json
$modern.shared_folder
$modern.references | Format-List
$legacy.references | Format-List
```

Existing regeneration commands in each validation record retain their arguments. The updated writers publish to the shared directory and write the named gate manifest. Earlier validation file lists describe logical case names; use the prefix/mapping above for current physical locations. Original acceptance records remain historical and do not imply all relocated controls have been requalified in the viewer.

## Observed migration and limits

Regenerated the five numeric/colour gate sets and six basic/advanced 9.x sets using their documented commands. The shared output contains 364 positive JT roots, nine unique glTF references and 19 unique resource files for the current local input set. Read-only analysis confirmed all shared glTF buffer/image URIs resolve inside the shared root. Authoring rebuilt original JT envelopes/lengths/TOCs with the new relative resource names; it did not reheader a downloaded file or execute the native reader. The C2n launch uses 19 unique documents for the former 20-document checklist because one identical reference is reused. No negative/native failure case was opened.

C2o subsequently authored 204 signed-image roots using the same shared store. Its signed-byte comparisons can require distinct expected images; identical geometry/buffers and matching wider-width references remain shared. The owner accepted its visual checks on 22 October 2026; see the [signed gate](JT_TEXTURE_SIGNED_VALIDATION.md).

## Provenance

Only existing original ModelViewer writers/fixture inputs and application resource-boundary code were consulted. The new original helper is `tools/jt/texture_fixture_store.py`; no new asset, vendor tool, SDK/schema, disassembly, third-party implementation or licence interpretation was used. GTEX01 URL, checkout, hashes and CC0 attribution remain in [image-control provenance](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). Source licence copies remain available as shared content-addressed assets. No importer, dependency or vendored decoder changes are needed for this layout. All assets/manifests/logs remain ignored under `out/`; tracked additions are code and this Markdown record. This describes method, not legal clearance. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
