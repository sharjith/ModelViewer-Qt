# Modern attribute foundations: Gate 2a

19 October 2026, `feature/JT-importer`. Gate 1 was accepted by the owner and committed/pushed as `fb2360f`. Phase 2 is split into attribute-envelope foundations (Gate 2a) and palette/face-group rendering (Gate 2b). This document records Gate 2a; full JT 10.9 remains unsupported.

## Implemented boundary

- Base attribute versions 1 and 2 share the leading state/inhibit/final fields. Version 2 adds a palette index **after the derived payload**. Material, geometric-transform and draw-style readers retain that index without shifting the existing fields.
- Sabot records are discarded after reading the embedded attribute header. The embedded header contains GUID, base type and object ID, without a nested element-length word. The inner object ID is used for graph binding; disposable outer IDs do not collide with each other or replace the inner IDs.
- Every element payload is read through a view bounded by its declared envelope. Sabot inner headers, derived data and trailing fields cannot consume bytes from the next element. Unknown/non-attribute/nested wrapper payloads and unsupported wrapped versions are rejected. Known wrapped payloads must consume their whole envelope.
- Fallback palette index -1 uses the existing appearance/transform accumulation. Indexed palette attributes and palette-map records are explicitly rejected until Gate 2b implements state accumulation, fallback/inhibition and face-group rendering. They are never applied globally as a substitute for palette support.
- The accepted file-version list is unchanged: 8.0-8.2, 10.0 and 10.3. About continues to list only supported versions and licence credits.

## References and provenance

The archived public Siemens JT 10.6 Revision D reference, sections 5.4, 5.4.8, 5.4.9 and Annex E, provides the attribute-tail and wrapper layout basis. The source and permissions are recorded in [compatibility policy](JT_VERSION_COMPATIBILITY.md) and [licensing research](JT_IMPORT_FEASIBILITY.md). Local read-only envelope observations from sample A109 corroborate the nested-header ordering. Neither those observations nor an independently authored synthetic 10.3 container establish full 10.9 compatibility.

The adapters and fixtures are original changes to the existing audited reader; upstream notices remain. No additional translator source, restricted SDK or downloaded geometry is bundled. See [modification history](../../src/ThirdParty/occt-jt/MODIFICATIONS.md).

## Native validation

The original `jt_modern_attribute_tests.cpp` exercises both byte orders, all three supported attribute types, version-1 regression, fallback and nonfallback indices, Sabot replacement/inner identity, repeated outer IDs, duplicate inner IDs, truncated headers/payloads/tails, unknown versions/types, nested/non-attribute wrappers, trailing wrapper junk, negative reserved indices and cancellation. Complete synthetic containers test green fallback material, translation, stored normals and GLB scene preservation using independently generated geometry.

The isolated MSVC AddressSanitizer build and full portable suite passed on **20 October 2026**, including the new malformed-input tests, prior geometry/appearance/scene tests and GLB preservation. Native probes retain the accepted counts for L80, L81, Q82 and all five 10.0 parts. Both generated display fixtures validate at one mesh, seven vertices and three triangles; the nonfallback fixture fails with the intended palette-scoped diagnostic.

The temporary diagnostic copy of A109 (header changed to 10.0 only for investigation) now proceeds past version-2 attribute and Sabot decoding to the explicit unsupported palette-map boundary. This is diagnostic progress, not production 10.9 support or successful model rendering; the original input remains unchanged and rejected by the production version gate.

The large 10.3 assembly regression also passed with its accepted counts: 2,468 render resources, 2,853 occurrences, 4,450,421 vertices and 5,844,669 triangles. Source-provenance, whitespace, documentation-link and community-model-name checks pass. Gate 2a is ready for the owner build/UI check. Logs and generated files remain under ignored `out/jt-research/modern-attribute-*`. The owner builds the full application. Phase 2 changes remain uncommitted pending owner acceptance.

## Gate 2a viewer checklist

Generate original acceptance files with the standalone test executable:

```text
jt_tests --modern-attribute-fixtures out/jt-research/modern-attribute-ui-fixtures
```

After rebuilding the current branch:

1. Open `out/jt-research/modern-attribute-ui-fixtures/attribute_v2_le.jt` and `attribute_v2_be.jt`. Keep `geometry.jt` beside them. Both should show the same green unit square and separate triangle, with smooth +Z shading: one mesh, seven vertices, three triangles. The modern parent adds +10 along X to the generated legacy child's (+10, -3) placement, giving (+20, -3) overall; fit-to-screen should work normally.
2. Open `palette_not_enabled.jt` in that folder. It should fail explicitly because palette-scoped attribute rendering is not enabled. This is an intentional boundary check, not a new supported geometry case.
3. Reopen known-working legacy and modern parts/assemblies. Appearance, placement, hierarchy, selection and counts should remain unchanged.

Gate 2b will implement the indexed appearance semantics and qualify sample A109 before adding 10.9 to the supported-version list. Stop at Gate 2a for the owner build/check before that implementation.

## Owner acceptance

The owner reported “All tests passed” on 20 October 2026. Gate 2a is accepted. Palette/face-group implementation may proceed at Gate 2b. Current-version qualification (10.9, then 10.10/10.11) takes priority over older-format expansion. Earlier descriptions of the Gate 2a unsupported palette boundary record that checkpoint and are superseded as Gate 2b progresses.
