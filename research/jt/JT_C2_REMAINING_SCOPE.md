# Remaining C2 work after C2u

22 October 2026, `feature/JT-importer`. Owner accepted C2u after reporting tests passed. This is a scope audit of the existing [feature plan](JT_FEATURE_COMPLETION_PLAN.md) and accepted validation records, not a new implementation or qualification result.

## Counts and checkpoint policy

C2a through C2u comprise 21 accepted lettered checkpoints. The original plan does not specify a finite total number of C2 sub-gates. Seven work packages remain; they are not seven guaranteed builds or an estimate of seven equal-sized tasks. Several packages may require more than one owner checkpoint. Missing reference/sample evidence cannot be resolved by merely scheduling another build.

The image-component and border work was split into small checkpoints. For subsequent work, group related formats/semantics into broader coherent gates where their dependencies and review burden permit. Continue to stop for owner confirmation before dependent implementation and to distinguish unsupported, unvalidated and not-applicable features. Do not add versions or silently remove residual requirements to make C2 appear finished.

## Seven remaining packages

| Package | Remaining scope | Dependencies / qualification limits |
| --- | --- | --- |
| 1. Combiners and accumulation | Broader Replace formats/resources/vertex-colour interactions; Decal, Blend, Add and Combine; remaining reset/inhibit/final interactions | C2u qualifies only inline RGB/RGBA/BGR/BGRA Replace without active vertex colours. Some operations need renderer/material representation work rather than another pixel converter. Next implementation priority is a coherent extension of combiner coverage |
| 2. Image/filter/border completion | Authored mip preservation, mipmapped border filtering, legacy clamp with borders, remaining applicable pixel/internal-format cases and external explicit-ring interpretation | Accepted C2p-t border support is base-level nearest/linear for sampled borders. Bit/depth/stencil and other descriptors require applicability decisions and documented semantics; they are not ordinary albedo images by default |
| 3. Texture units | Automatic assignment, explicit multiple units, ordering and per-unit accumulation | Current display mapping is explicit channel 0. Texture units are distinct from UV coordinate sets; new unit support may need a broader material/renderer model |
| 4. Generated/projective mapping | View-linear, projective and applicable sphere/reflection/normal generation; unresolved reference-plane conventions | Current mapping covers stored UVs and bounded model-linear S/T followed by affine transforms. Camera-dependent mapping must not be approximated by a fixed import-time UV bake without a stated contract |
| 5. Other material/texture roles | Applicable normal/bump, gloss, environment/light-map and other pre-/post-lit roles | Requires role-specific colour-space, coordinate, lighting and renderer/export decisions; existing channel-0 colour mapping is not blanket support |
| 6. JT8 authored textures | Applicable 8.x texture layout and mapping across enabled 8.0/8.1/8.2 | Evidence dependency: no applicable layout/reference qualification is currently recorded. Do not copy a 9.x/10.x envelope or reheader a sample as proof |
| 7. End-to-end qualification | Authentic textured-export comparisons, inheritance/shared-instance coverage, scene copy, GLB and renderer/ray-tracing consistency, final family matrix and diagnostics | Synthetic controls cover bounded semantics but cannot establish authentic exporter agreement. Owner currently has no authentic textured JT export available; source/reference gaps remain explicit |

Packages group the existing residual scope; they do not assert that every texture/shader feature can be represented in the current application. Record a supported mapping, an evidence gap or a representation limitation for each applicable feature. Cross-family checks remain required for enabled versions, with explicit not-applicable entries where layouts differ.

## Boundaries and provenance

C1 residual mesh/UV transport, C3 wire/point representations, C4 PMI/properties, C5 runtime LOD, C6 XT/B-Rep and C7 release consolidation remain separate stages. JT10.11 remains deferred until the agreed feature review. Production wire-export failures are C3, not extra C2 sub-gates.

This audit used existing original project documents and code scope, including [C2u Replace validation](JT_TEXTURE_REPLACE_VALIDATION.md) and earlier records linked from the [research index](JT_RESEARCH_INDEX.md). No new external source, asset, vendor tool, SDK/schema, disassembly, third-party implementation, licence interpretation, build or test execution was used. It records planning status and evidence limits, not legal clearance or new feature conformance.

## Accepted combined checkpoint C2v - 23 October 2026

The owner requested implementing related packages together and testing them in one session. [C2v combined appearance validation](JT_APPEARANCE_BATCH_VALIDATION.md) now groups core combiner and unit/accumulation work (packages 1 and 3), with package-7 copy/export checks and explicit filtering constraints. It prepares a single owner build/native/visual gate rather than a separate gate per operation or field. No package is marked universally complete: arbitrary independent sampling, Combine/vertex-colour interactions, other texture roles, ray-tracer nearest parity and the existing evidence gaps remain. The seven-package table above is the historical scope audit after C2u, not a claim that none of its work has advanced. The owner built and accepted the combined visual batch on 23 October 2026. Native-suite execution was not separately reported; residual scope is unchanged.

## Accepted grouped checkpoint C2w - 23 October 2026

[C2w filtering](JT_TEXTURE_FILTER_PARITY_VALIDATION.md) groups common material sampler transport/cache identity and CPU/OptiX filter selection, advancing packages 2 and 7. It corrects the unconditional smoothing limitation identified in C2v; authored JT mip payloads, exact footprint parity and other evidence/representation gaps stay open. The owner built and accepted the viewer gate on 23 October 2026. Native execution and individual CPU/GPU backend coverage were not separately reported.

## Accepted grouped checkpoint C2x - 23 October 2026

[C2x authored mips](JT_AUTHORED_MIP_VALIDATION.md) advances packages 2 and 7 with full border-free 2D source chains, per-level conversion/validation and shared raster/CPU/GPU transport. Its versioned PNG carrier is internal ModelViewer metadata, not portable glTF authored-mip support. Source border mips, composed chains, authentic export comparisons and other scope remain open. Owner rebuild and reported raster mip/filter observations passed. Native/backend coverage remains unreported; the owner subsequently accepted the separate depth correction and reported no regressions in the reopened alpha/mip/filter session.
