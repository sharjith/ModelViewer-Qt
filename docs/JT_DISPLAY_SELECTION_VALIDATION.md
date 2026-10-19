# JT Switch and RangeLOD display selection

19 October 2026, `feature/JT-importer`. This follows the assembly/attribute pass. The accepted file version remains JT 10.0; no XT conversion or additional JT version is enabled.

## Behavior

- Switch nodes now decode the published Group + local version + U32 selected-child layout. A selected child is traversed alone. `0xffffffff` means no child; unselected children and external files are not opened. An out-of-range selection or an unqualified local version fails explicitly. A failed selected child is not replaced with another Switch child: those children are authored choices, rather than equivalent display fallbacks.
- RangeLOD nodes support an optional import-time eye point through `Jt::Options::lodEyePoint`. The MCS centre is transformed through the occurrence hierarchy into WCS, then compared with the source's WCS distance thresholds. An exact threshold selects the earlier child. No limits select the first child; distances beyond all limits select the last child. Unequal numbers of limits and children are accepted, with intermediate indices bounded to available children.
- With no eye point, the viewer retains its deterministic first-child policy. This is a static import choice, not camera-driven runtime LOD switching or a claim to select the highest-detail mesh. Ordinary LOD groups continue in source order.
- A failed RangeLOD choice is rolled back using the existing transactional fallback, then the other alternatives are tried in source order. Only one successful alternative is attached. Cancellation and allocation failures propagate. Invalid, negative, nonfinite or decreasing thresholds and nonfinite centres/eye points fail explicitly.
- Imported Assimp nodes record `JTSelectedChild` as a zero-based index (`-1` for no selection); distance-selected RangeLOD nodes also record `JTRangeLODDistance`. The probe prints this information. Existing per-part B-Rep-to-tessellation fallback is unchanged.

The source specification describes range values as soft viewer hints. Rejecting invalid/nonmonotonic values is this importer's explicit policy. It does not imply those hints are geometry dimensions or that all viewers must use this policy.

## Source and provenance

The implementation uses the locally archived published **JT 10.0 Data Format Reference**, sections **6.1.1.8**, **6.1.1.9**, and **14.11**. The local research copies remain outside version control. Reference URL: [Siemens JT 10.0 Revision B](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf).

`JtNode_Switch.cxx/.hxx` are original ModelViewer additions under GPLv3. The existing upstream-derived LOD/RangeLOD files retain their copyright and GPLv2-or-later notices. The manifest and modification summary identify the additions and changes. No proprietary implementation, XT schema catalogue or licensed SDK has been added.

## Automated evidence

The isolated `tests/jt` targets were built from the main checkout with MSVC AddressSanitizer enabled. The portable suite and five local part regressions pass. New cases cover:

1. Selected Switch children, none selected alongside another mesh, invalid indices, an unselected cyclic subtree, and an unselected missing external file. A failed selected child must fail rather than substitute another choice.
2. Range threshold equality and neighbouring distances; empty, repeated and unequal-count limits; negative, decreasing and NaN values; nonfinite centres and eye positions.
3. Distance evaluation under nested translation, rotation and nonuniform scale, plus a failed distance-selected alternative and resource rollback.
4. Generated little- and big-endian JT containers, correct local header alignment, qualified local versions, external-file selection and stored tessellation fallback.
5. Unchanged vertex/triangle counts, stored normals, material RGB/opacity, exact-backend rejection/acceptance hooks, and GLB round trips for Bolt, Brake_Disk, Caliper, Rim and Tire. Accepted exact-hook tests inject an OCCT box; they remain ownership/selection tests, not XT conversion evidence.

Seven additional local fixture import/GLB-export cases passed mesh counts, selected-child metadata and opacity checks; colored cases also passed RGB checks. Invalid Switch selection and two invalid probe eye arguments returned the expected errors. These fixtures and exported GLBs remain under ignored `out/jt-research/selection-fixtures`, because they refer to the externally obtained Bolt sample.

Build: configure `tests/jt` against the application dependency prefix with `MV_JT_SANITIZE=ON`. Run `jt_tests` for portable fixtures, or `jt_tests out/jt-research/samples` for local regressions. The current isolated build is `out/jt-selection-build`; logs are `out/jt-research/selection-build.log` and `selection-tests.log`.

The probe accepts its existing input/optional-output arguments plus `--eye x,y,z`, for example:

```text
jt_probe range_near.jt far.glb --eye 11,0,0
```

## Viewer acceptance

Rebuild the application, then open the files in `out/jt-research/selection-fixtures`:

| File | Expected result |
| --- | --- |
| `switch_red.jt` | One red bolt at X=-30 |
| `switch_green.jt` | One green bolt at X=30 |
| `switch_green_big_endian.jt` | Same green bolt |
| `switch_none_with_context.jt` | One grey context bolt; neither switched bolt |
| `switch_invalid.jt` | Selected-child out-of-range error |
| `range_near.jt` | One red bolt using the default first-child policy |
| `range_fallback.jt` | One green bolt after the missing red choice is rejected |

Keep `parts/` next to these files. Each successful fixture has one placed mesh. The generated far-eye GLB shows the green alternative, but the viewer itself does not yet expose an import-time eye option.

A Switch selecting none with no other scene geometry remains a valid empty selection; ModelViewer cannot display an empty import, so the importer reports no supported geometry. Automated fixtures test this separately from invalid indices.

The owner rebuilt the application and reported that all viewer checks passed, explicitly confirming that `range_near.jt` displayed one red bolt and fitted it to the screen. The tested static first-child policy behaved as intended. This milestone is accepted for commit.

The original viewer-fixture generator is preserved as `tests/jt/generate_display_fixtures.py`; see `tests/jt/README.md` for reproduction with a locally supplied sample. The repository includes no downloaded geometry or vendor reference PDF. These tests exercise authored synthetic container layouts and existing local geometry, not independent JT-reader comparison or a diverse manufacturing assembly corpus.

## Remaining work

Real assembly corpora and independent placement/topology comparison; additional version/codec qualification; vertex/face colors and palettes; draw-style semantics and textures; richer properties and PMI. Runtime LOD switching and standalone/embedded XT conversion remain separate work.
