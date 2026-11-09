# Exact homogeneous mapping reductions - C2ae

24 October 2026, `feature/JT-importer`, after accepted C2ad `2c27d7c` and scope reconciliation `7eace39`. **Owner-accepted bounded viewer/export gate.** No C++ build, native suite or importer/viewer test was run by the agent. This is a bounded part of the generated/projective mapping package, not completion of every mapping mode.

## Representation decision

The retained JT10.0 Rev-B texture definitions describe a four-component texture matrix and independent S/T/R/Q generation modes. Model-linear generation uses model-coordinate reference planes; view-linear, sphere, reflection and normal modes depend on view/environment state. Existing source metadata already retains these descriptors. The standard application/export path carries two-component UVs, not a complete dynamic JT mapping program.

Dividing a varying homogeneous denominator only at vertices produces a different texture field from interpolation followed by division. This gate requires a finite, nonzero denominator and a conservative **per-triangle proof that every input contributing to Q is independently constant**. A stored contributing S/T component must be equal at the triangle vertices. For a generated contributing component, each position axis with a nonzero plane coefficient must be equal at those vertices. This proves the denominator constant throughout the triangle without letting offset rounding hide tiny variation. Different disconnected triangles may have different constants. No epsilon is used. Cancellation-only cases (between inputs or oblique plane terms) remain unsupported even when their mathematical Q is constant; unsupported mappings retain geometry and source UVs with a warning.

## Prepared behavior

- Stored 2D coordinates enter as `(S,T,0,1)`. Unit model-linear planes may independently generate any of S/T/R/Q, retaining the previously qualified mesh-local reference-plane convention and squared normal-length tolerance `1e-5`.
- The full finite serialized row-vector matrix is evaluated. Its S/T results are divided by transformed Q. R output is ignored for a 2D image, but generated R may contribute to S, T or Q through the matrix. Positive and negative nonzero constant denominators are handled.
- Resulting UVs must fit finite float coordinates. Existing border scale/offset is applied after division; existing legacy-clamp refinement follows the exact UV mapping.
- All mapped values and the triangle proof are computed before selected UVs are committed. Absent lower UV slots are padded only on success, preserving selected channel numbers and the existing scene-copy/export contract. Both S and T must be generated when their selected stored channel is absent.
- The source geometry cache, positions, normals, topology, other UV sets, pixels, material brightness and renderer shaders are unchanged. Effective transforms/generation/planes already participate in appearance identity. Allocation charging and cancellation polling cover vertex evaluation and face proof.

`JtTextureMapping.h` is new original code outside the vendored decoder. `JtSceneBuilder.cpp` retains image/combiner/border qualifications and narrows acceptance using the actual mesh. Existing non-unit plane and unsupported view-generation diagnostics remain. Old blanket rejection of changes confined to the unused R output is replaced with the domain proof; varying/zero-Q failures remain covered.

## Original controls

The independent reference geometry consists of two disjoint XY panels at Z=0 and Z=1. Original RGBA stripes and independently stated UV expressions avoid deriving the expected image from importer output. There is no downloaded model or authentic exporter control in this pass.

| Case | Expected appearance in matching Y-Up ADS/PBR views |
| --- | --- |
| `constant_q_half` | Texture coverage agrees after dividing stored S/T by two |
| `negative_q` | Both coordinate directions reflect/repeat according to the negative denominator |
| `homogeneous_shear` | Offset/sheared pattern agrees; unused R output does not change a 2D texture |
| `generated_r_to_st` | Model X contributes to both texture axes through generated R |
| `generated_q_two_planes` | Different pattern scales on the two panels agree with the reference |
| `generated_stq_no_uv` | Both panels are textured despite absent stored UVs; generated X/Y divided by Z+2 agrees |
| `generated_r_to_q` | Generated R contributes to the denominator; panels have the expected different scales |
| `generated_rq_mix` | Combined generated R and Q produces denominators 2 and 3.5 |
| `negative_generated_q` | Denominators -2 and -1 preserve the reflected/repeated pattern |
| `constant_q_border_s` | Constant S border and mirrored T agree after homogeneous division and padding |

Names are `texture-homogeneous_<case>_10_0_le.jt` and `reference_homogeneous_<case>.gltf` in the existing shared folder. Sixty positive roots cover original 10.0, 9.0 basic and 9.5 local-version-3 records in both byte orders, referencing original 8.2 geometry. Two 9.x big-endian representatives and two accepted border source/reference pairs bring the grouped list to **26 documents**. Nine explicitly unsupported controls are generated but excluded from the launch list: zero Q, varying Q, near-varying Q, view-linear/sphere/reflection/normal generation, non-unit Q plane and partial generation without stored UVs.

All generated payloads, controls, references and evidence stay ignored under `out/`. References share content-addressed geometry/image resources rather than duplicating them into gate folders.

```powershell
python -B tools/jt/make_texture_homogeneous_controls.py out/jt-research/texture-border-batch-ui-fixtures out/jt-research/texture-homogeneous-ui-fixtures
```

Manifest: `out/jt-research/texture-homogeneous-ui-fixtures/authoring.json`; one-invocation launch list: `out/jt-research/c2ae-visual-files.json`. After the owner builds and runs the install target, verify the installed executable matches the build before launching. Compare each pair with the same Y-Up view, ADS/PBR mode and zoom. These base-level controls do not encode the authored rainbow mip transitions from C2ad. Inspect pattern placement, scale, orientation, transparency and the selected-axis border.

After visual acceptance, export the JT sources `generated_stq_no_uv` and `generated_r_to_st` as both GLB and glTF, using the four filenames under `export_pairs` in the manifest. Reopen and compare with the independent references; export the JT, not the reference. This gate needs no private projective export extension because its accepted maps reduce exactly to ordinary UVs.

## Prepared native checks and qualification limits

`jt_texture_homogeneous_tests.cpp` prepares literal value checks, disconnected-face denominators, strict rejection of tiny nonzero denominator variation, including variation hidden by floating-point offset rounding, signed Q, R-to-S contributions, generated STQ without stored UVs, selected UV2/prefix padding, source preservation, modern shared-instance separation, scene copy and representative GLB round trips. Record checks span enabled 10.x minors in both byte orders and 9.0/9.5 basic/advanced local versions. Existing affine and model-linear negative cases are updated only where the old blanket R rejection no longer applies. No native execution is claimed.

Still open in the mapping package: cancellation-only constant-denominator proofs and varying-denominator interpolation; view-linear reference-plane timing; sphere/reflection/normal evaluation in the correct space; cube/environment target interpretation; original stored 3D/4D coordinate transport; dynamic mapping in scene copy, GLB/glTF and individual renderer backends; authentic exporter comparison. These require a richer retained material/coordinate evaluation contract or additional evidence. Camera-dependent maps are not baked against an arbitrary initial camera. No JT version, decoder revision or support claim is broadened by the original controls. JT8 authored textures remain unqualified. Batch 1 comeback items and Batch 2 residual limits remain open.

## Evidence and provenance

Original project source/serializers and the retained JT10.0 Rev-B texture matrix/generation definitions (printed pages 64-65) and OpenGL 2.1 homogeneous-coordinate, matrix and interpolation definitions were consulted. Local PDF SHA-256 hashes were rechecked: JT10 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`; GL2.1 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`. Primary URLs and retained-source provenance are in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance). The exact constant-denominator reduction is an explicit mathematical/application qualification, not claimed authentic vendor-export identity.

No new external download, third-party implementation/asset, vendor tool/SDK, schema, disassembly or licence interpretation was introduced. The new helper and controls are original project code. Read-only authoring inventory and syntax/whitespace review do not establish native or visual acceptance. This records method and limits, not legal clearance.

## Owner build and visual pass (24 October 2026)

The owner reported successful build/install. The installed executable was verified byte-identical to the build output before launching the grouped 26 positive documents in one invocation, with opt-in diagnostics under ignored out/jt-research/c2ae-visual-diagnostics.jsonl. The owner reported 'Looks good' after the requested ADS/PBR source/reference comparisons. The grouped visual gate is accepted; the two representative JT sources still require four GLB/glTF export/reopen comparisons before C2ae is complete. Native/backend-specific execution remains unreported.


## Final owner acceptance (24 October 2026)

The owner reported the grouped visual controls looked good, then confirmed the four requested GLB/glTF export/reopen comparisons passed. The two original sources are generated_r_to_st and generated_stq_no_uv. Outputs were saved in the existing ignored texture-export-ui-fixtures folder. Read-only inspection reports 4/4 image/carrier/sampler/sidedness comparisons and 4/4 effective-UV comparisons against the independent references. Maximum UV difference was 7.62939453125e-06, within the original controls' 16-bit geometry/UV quantization allowance. The UV inspection compares corresponding vertices and is limited to these order-preserving controls.

The bounded C2ae gate is accepted. Prepared native-suite execution, individual CPU/OptiX and authentic exporter qualification remain unreported. Earlier pending statements above are historical. This acceptance does not extend to cancellation-only Q proofs, varying-Q or camera-dependent mappings, or the other recorded C2 residuals. No source pixel, material factor or reference brightness correction was required.

Regenerate read-only export evidence from the retained local outputs:

```powershell
@'
import json
from pathlib import Path
r=Path('out/jt-research').resolve()
m=json.loads((r/'texture-homogeneous-ui-fixtures/authoring.json').read_text())
cases=[dict(id=p['source'],reference=str(r/'texture-common-ui-fixtures'/p['reference']),exports={f:str(r/'texture-export-ui-fixtures'/Path(p[f]).name) for f in ('glb','gltf')}) for p in m['export_pairs']]
(r/'c2ae-export-checklist.json').write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
'@ | python -B -
python -B tools/jt/inspect_texture_exports.py out/jt-research/c2ae-export-checklist.json out/jt-research/c2ae-export-inspection.json
python -B tools/jt/inspect_texture_mapping_exports.py out/jt-research/c2ae-export-checklist.json out/jt-research/c2ae-export-uv-inspection.json
```
