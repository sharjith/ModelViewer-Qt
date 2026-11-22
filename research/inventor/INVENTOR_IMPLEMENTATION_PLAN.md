# Inventor importer implementation plan

19 November 2026 · `feature/inventor3d`

ModelViewer decodes Inventor files directly into its OCCT and scene pipelines. Paired STEP files are independent validation references. The runtime uses the existing ModelViewer libraries.

| Stage | Implementation and automated checks | Owner UI gate |
| --- | --- | --- |
| 1. Analytic IPT geometry | Read compound storage and saved ASM bodies. Reconstruct analytic surfaces, trims, seams and supported tangent cavities. Preserve independent solids. Maintain I01–I05 and G01 as regression controls. | Done. The owner confirmed the five controls load and the G01 geometry looks correct alongside its reference. |
| 2. Native IPT appearance | Read saved diffuse colors and carry body and face assignments through sewing and tessellation. Preserve the default material for missing or unsupported appearances. The qualified layout is the G01 single-body profile. | Done. The owner approved the native G01 appearance and supplied a screenshot showing the yellow wheel and black rim. |
| 3. Complex IPT geometry | Decode the observed spline surfaces and intersection curves, with units, trim domains and seam handling. G02 passes the paired STEP checks. G07 passes solid, tessellation and graphics envelope checks. | Done for G02 and G07. The owner supplied both native screenshots and approved the G07 display. |
| 4. IPT foundation and compatibility | Establish a versioned working specification and corpus baseline. Qualify source pcurves, orientation, tolerances, procedural helices, singular tori, rational spline variants, and body classifications against the reported samples. | Open representative repaired lathe and pump parts after automated geometry checks pass. |
| 5. IAM assemblies | Resolve external and saved component references, recurse into subassemblies, and apply occurrence transforms and visibility. Reuse part definitions for repeated occurrences. Handle suppressed or unresolved references explicitly. Verify a complete nested sample with a repeated rotated part and a reference for placement. | Open the complete assembly and check tree structure, instance placement and independent visibility. |
| 6. Release qualification | Check the supported Inventor versions using original files, exercise malformed and unsupported input, cancellation and resource limits, and verify packaging and dependencies. Document supported profiles and known gaps. | Smoke-test representative IPT and IAM files using the release package. |

The implementation owner for each stage runs the authorized headless probes and repository tests, records the evidence, and stages a viewer build. The repository owner performs the listed UI checks. A stage that depends on owner acceptance starts after that gate is confirmed. If a validation case requires a missing input, record which evidence is missing and what sample would close the gap. The current assembly packages support partial reference research. A complete nested Pack and Go with a reference is still needed for the assembly gate.

The first appearance target is saved solid and face colors. Textures, physical material properties, and feature history editing are separate from these gates. Document any supported transparency, occurrence overrides, and other appearance properties at the point where evidence qualifies them.

The first IAM transform control, A01, is available locally under `out/inventor-research/iam-controls/`. The Inventor-generated report gives four occurrences of one asymmetric rectangular solid, with distinct rotations and translations. The native IPT probe measured the component at 20 × 10 × 5 mm. A local experimental IAM probe matched all four names, references and placement matrices from the report. The probe and its source snapshot are under ignored `out/inventor-research/iam-wip/` for resuming IAM after the IPT foundation pass. Nested placement still needs a separate case. The local acquisition manifest records the pinned URLs and hashes. A01 IAM SHA-256 `94a083f09ca1facb8db6eba901653d0b528c6930aa5a47f6e3fa86c035664b0f`. Report SHA-256 `1ebb5562955119169a920850c039790323aaa21826b026eb2859ad4c5e2dae79`. Component IPT SHA-256 `83ace091378834d2abe28124b2cb96ca18fe354965cffcb43be1e8301d35cf57`. [Pinned assembly corpus](https://github.com/CNCaddict/CAD-Convert/tree/a3cbe9f5f41acff810b5006d964d116e805e042b/corpus/generated)

```powershell
$iamControl = 'out/inventor-research/iam-controls'
Get-FileHash "$iamControl/A01.iam", "$iamControl/A01.json", "$iamControl/<local-component>.ipt"
& out/inventor-research/compound-build/inventor_compound_probe.exe --part-solid "$iamControl/<local-component>.ipt" `
    > "$iamControl/A01-component-solid.json"
& out/inventor-research/iam-wip/inventor_assembly_probe.exe --assembly "$iamControl/A01.iam" `
    > "$iamControl/A01-occurrences.json"
(Get-Content -Raw "$iamControl/A01.json" | ConvertFrom-Json).occurrences | Select-Object name,matrix
```

[Current handover](INVENTOR_HANDOVER.md) · [IPT working specification](INVENTOR_FOUNDATION.md) · [Analytic geometry evidence](INVENTOR_ANALYTIC_GEOMETRY.md) · [Appearance evidence](INVENTOR_APPEARANCE.md) · [Complex geometry evidence](INVENTOR_COMPLEX_GEOMETRY.md) · [Feasibility and provenance](INVENTOR_IMPORT_FEASIBILITY.md)
