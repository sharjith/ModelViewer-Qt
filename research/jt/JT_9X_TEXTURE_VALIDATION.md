# Basic 9.x image textures — Gate C2i


Current control storage, 22 October 2026: [shared texture resources](JT_SHARED_TEXTURE_CONTROLS.md) replaces per-gate companion copies. Regeneration commands are unchanged; file lists below use logical case names. Current JT filenames use the documented gate prefix in the common directory; glTF locations come from each gate manifest. Earlier acceptance records do not imply viewer requalification of every relocated file.

Owner acceptance, 22 October 2026: the owner reported "C2i tests passed" and authorized commit/push. This records the requested gate acceptance without inferring separate optional-suite execution or authentic textured-export conformance. The agent did not build/run the native importer or viewer.

22 October 2026, `feature/JT-importer`, following owner-accepted image formats `d757c2f`. **Prepared for owner build/test; no C++ build, native suite or viewer run by the agent.** Original control authoring ran successfully under ignored `out/`.

## Distinct family layout

The retained JT9.5 reference describes a texture-specific hierarchy of local versions 1/2/3. Basic local-version-1 data shares many environment and image fields with the later modern layout, but cannot be read as a 10.x record. The original reader now handles the 9.x base header and 16-bit local version, basic texture type values, absence of a coordinate-channel field, and one-byte shared-image flag. The modern reader's field widths/order are unchanged.

Basic source type 2 (two-dimensional image) is mapped into the supported effective channel-0 2D MODULATE profile. The adapter uses UV0 for that basic channel-0 profile; it does not claim automatic channel routing or multi-texturing. Other old type values do not inherit unrelated modern meanings. Raw source type/version/inhibit flags stay in node metadata. The legacy compression-inhibit bit 8 is mapped to the adapter's internal bit 7 without rewriting source flags. Local versions 2/3 or higher remain unparsed, diagnosed and bounded by the element envelope; their compatibility stubs are not interpreted as complete image support. Unknown modern local versions retain their previous explicit rejection.

Supported basic images reuse accepted inline/external resource handling, unsigned-byte colour formats, row orientation/alignment, affine texture matrices and model-linear S/T generation. Pixel/coordinate limits and cancellation still apply. Non-colour/unsupported images warn and preserve geometry. No new dependency or file revision is enabled.

The published reference is 9.5. Enabled 9.0 receives the same explicitly checked basic family layout in original controls, consistent with the existing transitional container/base attribute path. No independent 9.0 texture specification or authentic textured 9.0/9.5 export has been supplied; synthetic qualification must not be presented as blanket vendor-export agreement. JT8.x authored texture payloads and advanced 9.x blocks remain open, alongside broader C2 and later feature stages.

## Prepared checks

`tests/jt/jt9_texture_tests.cpp` is included in `jt_tests`. Original records cover enabled 9.0/9.5 in both byte orders, inline and external pixels, exact known pixel orientation, type normalization without source-metadata loss, affine reflection, model-plane generation on geometry without UVs, invalid channel/type/base/local versions/shared flag, every truncated basic record prefix, budgets and cancellation. Advanced blocks and other legacy texture types must retain geometry with diagnostics. These tests have not been run by the agent.

Fixture writers serialize original v9 graph/base/partition/attribute headers and 32-bit TOC entries directly. They do not change the header of a 10.x graph. Geometry companions use the previously authorized original/source-derived 8.2 geometry path, so the controls are mixed-family external assemblies; they do not establish authentic 9.x geometry/texture co-export conformance. Existing authentic 9.x assemblies remain regression inputs rather than texture oracles.

For isolated native checks the owner uses the existing [image-gate commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls); the new tests run automatically. The owner builds the viewer normally. Authoring of already available visual controls is reproduced by:

```powershell
python -B tools/jt/make_jt9_texture_controls.py out/jt-research/texture-source-ui-fixtures out/jt-research/texture-transform-ui-fixtures out/jt-research/texture-planar-ui-fixtures out/jt-research/texture9-ui-fixtures
```

The tool authored 24 original JT roots and six reference comparisons, reusing accepted GTEX01 geometry/images. Source-derived companions, licence copy and `authoring.json` remain ignored. The command did not invoke the reader, renderer or a native test executable.

## Owner visual gate

Rebuild the viewer and use `out/jt-research/texture9-ui-fixtures/`. Each revision directory (`9_0`, `9_5`) has three self-contained folders: `stored`, `mirror`, `planar`. Keep each folder's companions beside its roots.

1. In each of the six folders, compare `texture_inline_le.jt` with `reference.gltf`. Stored mapping should match the original orientation; mirror mapping should reflect horizontally; planar mapping should show the same image despite the geometry companion having no stored UVs.
2. In `9_5/stored`, also open `texture_external_le.jt` and `texture_inline_be.jt`; both should match the same reference.
3. In `9_0/planar`, open `texture_external_be.jt`; it should match its planar reference without manually generating UVs.
4. Every control should retain one mesh, four vertices and two triangles. Compare orientation, pattern and coverage; lighting brightness may differ.
5. Reopen accepted authentic 9.0/9.5 assemblies and one modern image-format control as regressions.

Stop for owner results before committing this gate. No additional About support-version claim is warranted; the existing version list remains unchanged.

## Reference and provenance

Read-only research consulted the already retained **Siemens JT File Format Reference Version 9.5 Rev-A**, section 7.2.1.1.2.3 and subsections through the basic image format description, printed pages 63–74. PDF SHA-256 `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`. Its archival availability, permission terms and broader provenance are recorded in [9.5 qualification](JT_95_VALIDATION.md), [feasibility/licensing research](JT_IMPORT_FEASIBILITY.md) and [notices](../../THIRD_PARTY_NOTICES.md). The original publisher download is not newly verified in this pass; no speculative URL is substituted for the retained document.

The PDF skill rendered and inspected pages 64/65/72 under ignored research storage. Figures 44/48 confirm the missing coordinate-channel field and one-byte shared flag. The overall version diagram contains an erroneous label where the basic block is expected; the surrounding text and detailed basic-block diagram establish the version-1 flow. No PDF or diagram/specification text is redistributed. No new licence interpretation or legal clearance is claimed.

Existing original ModelViewer reader/adapter and v9 container fixtures at `d757c2f` supply integration conventions. GTEX01 repository/checkout, source hashes and CC0 attribution remain in [image validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#owner-authorized-gltf-derived-control--gtex01). No vendor tool, SDK/schema, disassembly or new decoder implementation was consulted or introduced. Existing stored repository-tree metadata was searched while locating the reference; no code from it was used. Only the two existing ModelViewer-original decoder files change; upstream-derived source/hashes/notices and the 104 upstream/17 original file counts remain unchanged. The provenance manifest and modification note record this continuation. Follow the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).
