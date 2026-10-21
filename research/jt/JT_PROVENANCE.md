# JT implementation provenance and research method

21 October 2026, `feature/JT-importer`. This is a pass-specific method record, not legal clearance or a statement that every historical branch change used the same sources.

## C2a — vertex alpha adapter pass

Consulted:

- Siemens **JT 10.0 File Format Reference, Revision B**, publisher document obtained during earlier research: [publisher PDF](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf). Locally retained PDF SHA-256: `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. Section 6.1.2.2 defines the material blending flag/factors and vertex-colour override; the previously qualified RGBA transports supply the values. The compressed coordinate/colour/UV work in earlier passes has its own source/oracle boundaries in the linked validation records.
- Existing ModelViewer scene adapter, material processor, raster shader and CPU/OptiX rendering code, from repository baseline `6433da9` plus the changes described for C2a. Code inspection identifies the existing raster alpha multiplication and explicit blend-mode classification, as well as the remaining ray-traced vertex-alpha gap and fixed-alpha dedicated wireframe path.
- Existing audited vendored OPEN CASCADE JT-Assistant subset: upstream [repository](https://github.com/Open-Cascade-SAS/JT-Assistant), pinned commit `e859e8c2d809f9d9468e6211eba0052f433978ee`, recorded archive SHA-256 `9aae63d78e25c586b8a92c1edbc5c6221539bcb0cbedd5be2c82322aa1650fa2`. The existing [manifest](../../src/ThirdParty/occt-jt/PROVENANCE.json) records individual source hashes and modifications. This pass reuses its qualified data structures/material decoding without modifying the vendored decoder or introducing another decoder dependency.

No vendor application, SDK, proprietary schema package, disassembly or binary decoder implementation was used in this pass. No vendor package was downloaded and no terms were accepted on the owner's behalf. This is not a new legal interpretation of the Siemens terms or the upstream licence; earlier legal-scope documents and [third-party notices](../../THIRD_PARTY_NOTICES.md) remain applicable. Existing credits/dependencies do not change.

Implementation is in the original scene adapter, the shared raster shader (unshadowed vertex coverage in MASK/BLEND and premultiplication), and the renderer depth-write policy for explicit BLEND materials; regression/fixture code is original ModelViewer test code. Visual controls use original nested cubes; the earlier planar unit cases remain for corner-alpha checks. Existing test filenames are preserved when regenerating the revised geometry. No downloaded CAD geometry, generated fixture, exported GLB, vendor document, external repository snapshot or machine-generated evidence is added to Git.

Under [the repository handout](../../RESEARCH_FOLDER_HANDOUT.md), the owner normally builds/tests. C2a was initially prepared without executing those commands. On 21 October 2026 the owner explicitly authorized a local control build and fixture generation; this limited exception does not authorize building the full viewer. [C2a validation](JT_VERTEX_ALPHA_VALIDATION.md) records the resulting status and owner viewer gate. Generated files and machine evidence stay ignored under `out/`; only written research belongs under `research/`. No vendor-reader comparison or all-version RGBA conformance claim is inferred from synthetic scene controls.

## C2b follow-up — 21 October 2026

The dedicated wire/feature-edge alpha pass consulted only existing ModelViewer code and original controls at `3523fcf`. Original shader and scoped render-state changes are described in [wire alpha validation](JT_WIREFRAME_ALPHA_VALIDATION.md). No additional vendor reference, software, schema, SDK, downloaded geometry or third-party implementation was used; no vendored source or notice changed. Owner build/viewer acceptance was recorded on 21 October 2026; the agent did not run a new build or rendering test.

## C2c follow-up — 21 October 2026

The ray-traced vertex-alpha pass consulted only existing ModelViewer snapshot/Embree/CPU/OptiX upload, refit and hit-shader code at `a343467` and original controls. [C2c validation](JT_RAYTRACED_ALPHA_VALIDATION.md) describes original changes and prepared tests. No new vendor reference, SDK, schema, application, third-party source or dependency was consulted or introduced; existing application CUDA/OptiX build dependencies are reused. No vendored source or notices changed. The agent has not built/run C2c; the owner reported the requested C2c tests passed on 21 October 2026 and authorized commit/push. Separate optional-regression execution is not inferred.

## C2d texture descriptor foundation — 21 October 2026

Consulted the retained JT 10.0 Rev-B section 6.1.2.3, figures 49–54 and compound matrix definitions (publisher URL/hash above), using PDF rendering for diagram field order, and ModelViewer baseline `8f416ca`. The original descriptor reader and metadata adapter, synthetic-control limits, overlapping reference label and pending authentic export are recorded in [C2d validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md). Two new ModelViewer-original decoder files are added to the manifest/notices summary; upstream-derived files and hashes are unchanged. No vendor software, new schema/SDK/package, disassembly or other decoder source was used. No new licence interpretation or legal clearance is claimed. The agent has not built/run C2d; owner checks and authentic texture-export qualification remain pending.

### GTEX01 authoring follow-up

The owner authorized synthetic textured JT controls from their local glTF collection because no textured JT export is available. The original Python writer reads one locally available CC0-1.0 asset/image (creator Marco Hutter) from the Khronos sample-assets checkout `92160912f77d4204e5f4712d877d8594b70f93b8`. Source hashes, repository URL, exact regeneration command and comparison limits are recorded in [C2d validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md). All source-derived geometry/images, copied licence and detailed source-path manifest stay ignored. Pillow is used only by the fixture writer, not added to application dependencies. The authoring command executed; no C++ build, native JT reader test, vendor tool or GPU run was performed.

### Combined C2d/C2e image mapping follow-up

On 21 October 2026 the owner requested visible image mapping before testing. The continuation consults the same retained public JT reference and existing application image/material/shader/scene-copy paths. Bounded base pixels and source filename are retained for supported channel-0 2D mapping. The original adapter resolves local images or inline pixels into embedded scene PNGs; generic material loading consumes those images and scene copying uses compressed byte lengths. Existing Qt image support is reused; no new dependency, proprietary source, vendor tool or licence interpretation is introduced. Prepared pixel/UV/resource/copy regressions and the current visual handoff are in [validation](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#current-handoff-combined-c2dc2e-visible-image-mapping). No C++ build/run or owner acceptance is claimed; authentic textured-export qualification remains open.

The owner subsequently confirmed visible images but reported a vertical reflection. Diagnosis also consulted existing Assimp v6.0.4 glTF importer UV extraction, without copying/modifying dependency code. Its version, upstream URL and local file hash are recorded in the validation document's owner image report. Removing redundant JT-specific runtime compensation awaits owner retest; no full texture-gate acceptance is inferred.
