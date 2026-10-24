# Batch 1 comeback list

24 October 2026, `feature/JT-importer`. The owner explicitly deferred these residuals and requested proceeding to Batch 2. This is a return list, not a declaration that all of Batch 1 is complete.

[C2ac](JT_COMBINER_UNIT_BATCH_VALIDATION.md), accepted and pushed as `ac1d1f3`, delivers constant-RGB vertex combiners, separable Modulate/Replace stacks, wider vertex Replace and bounded mixed-resolution nearest grids. Preserve that accepted subset and its shared-instance/cache, alpha, copy/export and ADS/PBR controls when returning.

| ID | Deferred work | Prerequisite / return check |
| --- | --- | --- |
| B1-R1 | Combine | Qualified RGB/alpha functions, sources, operands and scales from source/reference evidence. The currently parsed descriptor names the environment mode but does not establish this state; do not guess Modulate defaults as conformance |
| B1-R2 | Independent UV sets, transforms and wrapping per texture unit | A retained multi-stage material representation and evaluators, plus a deliberate copy/export contract. A single precomposed base-colour image cannot generally preserve these inputs |
| B1-R3 | Independent stage filtering and nonlinear linear/trilinear composition | Evaluate each stage at its own sampling footprint before combining; filtering a precomposed product, saturated sum or RGBA Decal is generally inequivalent |
| B1-R4 | Varying-RGB Decal/Blend/Add and other nonseparable vertex interactions | Retain the incoming colour term and texture-dependent contribution through rendering/export. Constant-RGB and component-separable cases are already implemented |
| B1-R5 | Broader framebuffer blend equations and remaining unqualified accumulation combinations | Establish supported material/renderer mappings and explicit family/reference expectations; keep unsupported approximations diagnosed |

Return in dependency order: resolve B1-R1 evidence separately; design B1-R2/R3/R4 as one material/evaluation/export change; qualify B1-R5 against explicit source semantics. Protect authored source metadata, original/shared geometry, incoming alpha precision, per-appearance cache identity and renderer/export consistency. Do not remove existing diagnostics merely to mark these rows complete.

Prepared native-suite execution, individual CPU/OptiX verification, varying-source transport qualification and authentic exporter comparisons remain qualification gaps. They belong in the final appearance matrix as well as any relevant return gate; accepted viewer results are not evidence that those suites/backends ran. See [remaining C2 scope](JT_C2_REMAINING_SCOPE.md).

Method/provenance: reconciliation of existing original source and accepted validation records only. No new external source, vendor tool, asset, build or test execution. This records the owner's deferral and technical prerequisites, not new conformance or legal clearance.
