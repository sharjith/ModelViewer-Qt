# Combined appearance gate C2v

23 October 2026, `feature/JT-importer`, after owner-accepted C2u `b4c7d64`. **Owner built and accepted the combined visual gate on 23 October 2026. Native-suite execution was not separately reported.** This groups related combiner, unit-state, inheritance and preservation work into one owner gate. Original controls were authored and their hashes/envelope offsets/resource paths inspected under ignored `out/`. No C++ build/native suite execution by the agent. After the owner build, the agent launched all 34 positive documents once in the installed application; the owner reported tests passed.

## Delivered scope and remaining limits

| Area | Prepared implementation | Explicit boundary |
| --- | --- | --- |
| Colour combiners | Modulate retained; wider Replace including luminance/luminance-alpha/alpha and external decoded RGB/RGBA; RGB/RGBA Decal; qualified colour Blend and Add; None contributes no stage | Decal, Blend, Add and multiple-stage composition require nearest magnification/minification without mips. Decal is undefined for luminance/alpha internal formats and is diagnosed. Combine lacks qualified operand/function parameters and remains diagnosed |
| Units | Sparse explicit unit identifiers, ascending application order, per-unit replacement and deterministic automatic allocation without displacing explicit assignments; 64 accumulated units maximum | Colour rendering handles decoded pre-lit 2D stages only. Automatically assigned normal/light/etc roles are retained in state and diagnosed as unrendered, not painted as base colour. Independent coordinate mappings, resolutions, border footprints or wraps cannot be flattened by this path |
| Inheritance/reset | Per-unit inhibit/final/forced handling, Blend Colour grouped with Blend Type; whole-attribute final; separator reset with excepted role retention; final-reset defaults also fence units introduced by descendants | Modern field-final words and legacy accumulation-final flags retain their distinct envelopes. Reset controls apply only to envelopes that support type 26; no fabricated basic-9.x reset record |
| Preservation | Source descriptors remain separate from derived images/materials; effective assignments recorded as `JTTextureUnits`; scene-copy and GLB native checks prepared | `Active` in unit metadata describes accumulated source state, not proof that its role is rendered. Authentic textured exports and full renderer/filter parity remain unqualified |

This advances packages 1 and 3 and adds package-7 preservation checks from the [remaining C2 scope](JT_C2_REMAINING_SCOPE.md). It does not declare those packages universally complete: arbitrary multi-texturing, vertex-colour interactions with the new combiners, Combine, other roles and the remaining evidence dependencies stay visible. No supported-version or About change, new dependency, vendored decoder change or renderer-specific shader extension.

## Representation and colour/filter contract

The adapter retains source unit states in an ordered map; the unit identifier is independent of the UV coordinate-set number. New automatic roles choose the lowest unused slot; an explicit assignment relocates an automatic occupant rather than overwriting it. Repeated inheritance of the same automatic attribute keeps its assignment. This deterministic slot policy is an application choice consistent with the non-displacement requirement, not a prescribed Siemens slot-allocation algorithm. Invalid role/channel combinations are diagnosed. A separator clears applicable states but preserves the source roles excepted by the published reset rule; final state and forced override remain in effect.

The existing renderer has one base-colour image binding. The new adapter composes eligible stages into an ordinary canonical image, in ascending unit order. For multiple stages, selected UV channel, generated-coordinate parameters, affine transform, wraps, decoded dimensions and effective border mapping must agree. Nearest non-mipmapped sampling makes each source lookup use the same texel, avoiding the incorrect assumption that bilinear filtering commutes with nonlinear colour operations. Unsupported stacks disable their colour texture mapping with a diagnostic and retain geometry/material colour; they do not silently display a partial stack. Vertex colours remain present on fallback.

Material factors are linear in the existing application. Base-colour PNG samples are decoded from sRGB by `main_scene.frag` and `CpuPathTracer.cpp`. Composition therefore decodes RGB into that linear working space, applies the qualified colour/alpha operations, then encodes the derived RGB back into PNG. Alpha stays linear. Results use the existing nearest-byte RGBA8 display precision. Original incoming factors are retained in private aiMaterial properties (`$mat.jt.sourceDiffuse` and source blend flags); the derived material uses neutral factors when its colour/alpha has been composed. Their cache identity retains distinct source factors even when display pixels coincide. The scene-copy check covers these private properties; lossless private-material metadata export through GLB is not claimed. Existing single-stage Modulate and qualified RGB(A) Replace paths remain direct mappings. Wider single-stage Replace does not require the nonlinear-composition nearest restriction; its source RGB or constant incoming RGB and alpha mapping are affine, with the stated display quantization.

Decal retains incoming alpha while using source texture alpha for its RGB mixture, including when framebuffer blending is disabled. Blend retains its inherited environment colour and multiplies applicable alpha. Add clamps RGB and multiplies applicable alpha. The existing blend-enable/factor policy determines final display opacity; disabled/overwrite blending remains opaque. None leaves lower units intact. Resource limits, cancellation, source-data validation, memory charging and canonical PNG deduplication remain in use. Composition caches include source images, internal formats, operation order, incoming material colour/alpha, blend colours and blend policy; effective mapping remains appearance-specific.

Raster texture binding carries imported nearest settings. The CPU path tracer currently uses its existing bilinear/trilinear sampler; it does not provide complete authored nearest-filter parity. That pre-existing renderer limitation was found by source inspection and is recorded here, not silently claimed fixed. No ray-tracer code changes in this batch. Full renderer parity and arbitrary per-unit sampling need later renderer work. The inherited source-image/border display conventions are not replaced by a claim of authentic exporter colour-space agreement.

## Prepared native suite

`jt_texture_batch_tests.cpp` and `JtTextureStackFixture.h` add original family-specific graphs and literal expected colour/alpha vectors. They cover:

- Decal/Blend/Add, broader Replace, enabled/disabled framebuffer blending and linear-factor/sRGB preservation.
- Reversed source attribute-list order versus ascending explicit-unit order, sparse identifiers through the signed-32-bit limit where allowed, and overriding one inherited unit while retaining another.
- Inhibit/final/forced rules, modern field-final versus legacy whole-final, reset, excepted light-map state, forced reset and a final-reset fence for newly introduced units.
- Automatic assignment, non-displacement and repeat assignment, role/channel rejection, explicit diagnostics for mismatched filtering/UVs/transforms/resolution and Combine, and the 64-unit cap.
- Unchanged geometry/normals/UVs, source descriptor/effective unit metadata, scene copy and a GLB composed-image round trip.

Applicable checks span enabled modern minors and accepted 9.0/9.5 local texture versions 1/2/3 in both byte orders. Unsupported legacy reset/automatic-role envelopes are not invented. Earlier portable checks remain in the same suite; C2u now includes Blend among its deliberately incompatible linear-filter rejection cases. These are prepared assertions, not agent-executed results.

Owner commands, from the existing developer environment/configured standalone build:

```powershell
cmake --build out/jt-selection-build --target jt_tests
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
$jtCompilerBin = Split-Path (Get-Command cl.exe).Source
$env:PATH = "$jtPrefix/bin;$jtCompilerBin;$env:PATH"
./out/jt-selection-build/jt_tests.exe *> out/jt-research/c2v-native-validation.log
python tests/jt/check_provenance.py
```

If the standalone folder needs configuring, use the existing [configuration instructions](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls). Keep logs under ignored `out/`; report a failing command before treating the native gate as passed. Viewer acceptance and native-suite execution are separate records.

## Original authoring and shared resources

Exact fresh regeneration:

```powershell
python -B tools/jt/make_texture_border_component_controls.py out/jt-research/texture-border-components-ui-fixtures
python -B tools/jt/make_texture_replace_controls.py out/jt-research/texture-border-components-ui-fixtures out/jt-research/texture-replace-ui-fixtures
python -B tools/jt/make_texture_batch_controls.py out/jt-research/texture-replace-ui-fixtures out/jt-research/texture-batch-ui-fixtures
```

Authoring produced 110 positive original JT roots for 16 cases, with twelve unique glTF references. Gate `authoring.json` contains file hashes, case labels and reference mappings. Expected recipes independently state the effective stages after inheritance/reset; the writer/importer is not used to resolve its own expected result. Native literal vectors provide a separate colour oracle. Original nested cubes use a quarter/half/three-quarter linear material tint and half material alpha, with an opaque red inner cube to expose transparency. The common glTF geometry buffers and inner geometry companion are reused. Only the changed outer appearance and small images/reference documents need distinct content; no per-gate resource copies.

Modern 10.0 and representative 9.x records are serialized originally with both byte orders, referencing original 8.2 geometry companions. They are mixed-family synthetic controls, not authentic same-version textured exports or evidence obtained by reheadering downloaded files. No negative/fail control is in the visual authoring set. The automatic-role case intentionally carries a diagnostic for an unrendered normal role while its qualified colour stages remain visible.

## One combined owner visual session

Rebuild normally. All physical files are in `out/jt-research/texture-common-ui-fixtures/`; mappings are in `out/jt-research/texture-batch-ui-fixtures/authoring.json`. The prepared launch list has 34 positive documents: sixteen modern little-endian cases, one big-endian multi-unit case, three representative 9.x cases, twelve unique glTF references and two regressions. Launch once with all paths; no failure fixtures. Match up-axis/camera settings when comparing JT/glTF, and rotate to inspect the inner cube.

| Group | Case keys / modern file suffix `<case>_10_0_le.jt` after `texture-batch_` | Check against the corresponding glTF reference key |
| --- | --- | --- |
| Combiners | `decal_rgba`, `blend_rgba`, `add_rgba` | Pattern colour/coverage and inner-cube visibility match; these operations should not all look like Modulate |
| Wider Replace | `replace_luma`, `replace_alpha`, `replace_external` | Luminance-alpha is grayscale with image alpha; alpha-only preserves the material tint with image alpha; external RGBA replaces incoming tint/alpha |
| Ordered units | `unit_order`, `unit_accumulate`, `inherit_override` | Highest-unit Replace wins despite reversed source-list order; accumulated output differs; replacing a lower unit retains the upper one |
| Attribute rules | `inhibit_combiner`, `final_combiner`, `forced_override` | Inhibit/final preserve the inherited operation/colour while applicable image fields change; forced override uses the replacement image/operation |
| Reset/no-op | `reset_units`, `final_reset_fence`, `none_upper` | Resets show the untextured material; final reset also blocks a new descendant image; None leaves the lower texture intact |
| Automatic conflicts | `automatic_conflict` | Colour output matches `unit_accumulate`; the unrendered normal role must not displace explicit colour units. Effective assignment checks are in the native suite |

Additional family checks: `texture-batch_unit_accumulate_10_0_be.jt`, `texture-batch_blend_rgba_9_0_v1_be.jt`, `texture-batch_inherit_override_9_5_v2_le.jt`, and `texture-batch_reset_units_9_5_v3_be.jt` match their corresponding references. Regressions: `texture-border-components_alpha_partial_10_0_le.jt` and `texture-border-formats_rgb_constant_external_le.jt` retain their accepted appearance.

Owner acceptance, 23 October 2026: after the completed build and single-instance launch of the 34 positive documents, the owner reported "Tests passed". This records visual acceptance; native-suite execution was not separately reported. Remaining limits above are unchanged.

## References and provenance

Consulted retained Siemens JT10.0 Rev-B section 6.1.2.3 on per-channel accumulation, Blend Type/Colour, channel assignment and reset; retained JT9.5 Rev-A hierarchy; and retained public OpenGL2.1 texture functions/internal-component definitions. Exact URLs and retained PDF SHA-256 values are in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). Source method/limits from earlier passes remain historical, not blanket conformance claims.

Existing original adapter, [material processing](../../src/Materials/MaterialProcessor.cpp), [raster colour helpers](../../shaders/main_scene.frag), [path-tracer colour helpers](../../src/RayTracing/CpuPathTracer.cpp), viewport sampler/upload code and test/writer code were inspected. Existing vendored base-attribute accessors were consulted for accumulation flags; their notices and files remain unchanged. No new download, third-party asset, third-party implementation, vendor software, SDK/schema, disassembly or licence interpretation was introduced. Cubes, graphs, tiny pixels, oracle recipes and glTF references are original. No vendor document or generated artifact is tracked. This records method and qualification limits, not legal clearance.
