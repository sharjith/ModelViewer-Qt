# Image/border Batch 2 - C2ad

24 October 2026, `feature/JT-importer`, after accepted C2ac `ac1d1f3`. **Accepted bounded viewer/export gate.** The owner built/installed and accepted the grouped comparisons, then the focused ADS/PBR retest after the shared light-uniform correction. Native/backend-specific execution remains unreported; the agent ran read-only export/log inspection only.

The owner deferred Batch 1 residuals in the [dedicated comeback list](JT_BATCH1_COMEBACK.md). This gate advances Batch 2 and does not reopen or silently complete that list.

## Prepared scope

| Area | Implementation | Qualification boundary |
| --- | --- | --- |
| Mixed sampled axes | Authored borders may sample only S or T, with repeat/mirror/edge behavior on the other axis | Retain the ring only on sampled axes; keep the same normalized interior footprint at every level |
| Rectangular/non-power-of-two interiors | A sampled axis is a longest power-of-two dimension; the unsampled axis may be shorter and non-power-of-two | For example, sampled S with a 64x35 interior becomes 192x35, 96x17, 48x8, 24x4, 12x2, 6x1, 3x1. Both sampled axes still require a square power-of-two interior |
| Generated border mips | Generate canonical RGBA8 interior levels, border edges and corners separately, then expand the display footprint | Deterministic straight-component area-box reduction, nearest-byte rounding; no border bleed into the interior. This is a documented application filter, not bit identity with every driver's unspecified mip-generation kernel |
| External explicit rings | Border Mode Explicit selects a one-texel ring in decoded PNG/JPEG pixels; crop unsampled axes and retain sampled ones | Original PNG controls qualify the bounded interpretation. External private authored-mip carriers remain rejected; authentic exporter and JPEG decoder identity are not claimed |
| Legacy CLAMP | For explicit/constant borders and equal non-mip min/mag filters, split display triangles at UV interval boundaries and clamp on each resulting region | Fixed vertex clamping without subdivision would distort interior interpolation. Mipmapped CLAMP and unequal min/mag filters remain diagnosed because the UV rewrite does not preserve original derivatives outside the interval |
| Other image formats | Existing accepted colour/component conversions are reused before border processing | One-bit transport and unresolved single-channel constant-border interpretation remain unqualified; depth/stencil are not silently mapped to albedo |

No new JT revision, dependency, vendor decoder or About change. Shared renderer lighting/blending and depth-policy code are unchanged. The private mip-carrier validator is extended; existing raster/CPU/OptiX carrier consumers retain their current sampling implementations.

## Border footprint and private terminal profiles

Each sampled dimension is displayed at three times its source interior extent, with `(UV+1)/3` mapping. An unsampled dimension keeps the source interior extent and ordinary UV mapping. Source ring bytes and source descriptor metadata remain unchanged. The sampled axis must stay large enough to contain two border texels and an interior through the last source level.

Existing `border-3x3` remains unchanged. New private terminal markers `border-3x1` and `border-1x3` describe single-axis chains. Each sampled base dimension must be `3*2^N`; each unsampled dimension must be at most `2^N`; there must be exactly N supplied higher levels, ending at the named dimensions. N is 1..15. Unknown, incomplete, incorrectly shaped, oversized and nested carriers remain rejected. Maximum texture level is the supplied tail count, so automatic generation cannot erase the terminal border.

A sampled dimension shorter than the longest one, or an odd/non-power-of-two sampled dimension, generally cannot retain that fixed footprint through the complete ordinary mip recurrence. A 4x8 image sampled on both axes is one example: the shorter display axis would shrink from 3 to 1 before the longer one reaches its last source level. Such cases remain diagnosed, not rescaled or given a truncated chain. A generated sampled-border chain with a 1x1 base is also outside this profile.

Generated chains average canonical straight RGBA8 components. Fractional source areas on an odd unsampled dimension use integer overlap weights and half-up rounding. Vertical/horizontal ring edges are reduced only along their length; corners retain their original values. Alpha forcing for blending-disabled appearances happens after reduction, with original incoming/source blend rules retained. Co-sampled nearest composed stages may consume matching generated border levels using the existing per-level composer; independent sampling and nonlinear filtered composition remain Batch 1 residuals.

The PNG base image remains readable by other applications, but authored/generated private levels are ModelViewer metadata, not a portable glTF mip extension. ModelViewer GLB/glTF round trips must preserve those fields; another viewer may use only the base image or regenerate different lower levels.

## Portable legacy CLAMP

The qualified path works after the supported affine/model-linear UV mapping and border scale/offset. A triangle crossing a legacy S/T boundary is split into convex pieces at those boundaries. All retained UV sets, positions, normals and vertex RGBA are interpolated; only the selected UV components are clamped. This preserves the original surface shape and piecewise texture-coordinate field and exports through ordinary geometry/UVs without an application-specific shader extension.

The cached source mesh is untouched. Derived display topology can gain triangles and wireframe edges. The refinement preserves material/source identity, does not claim original triangle-index identity and is bounded to four million display vertices. Nondegenerate source triangles and nonopposing interpolated normals are required when refinement is needed. Unsupported refinement diagnoses the texture and restores the appearance copy's prior UVs instead of displaying a partial clamp. Cancellation and allocation-limit failures propagate normally. No B-Rep reconstruction is involved.

Equal non-mip min/mag filters are deliberate: a clamped region has zero UV derivative, so a UV-only export cannot preserve pre-clamp mip/anisotropic footprints or a derivative-dependent choice between different min/mag filters. This gate qualifies standard nearest/linear isotropic cases only.

## Remaining Batch 2 items

- Sampled non-power-of-two axes and two-axis rectangular full mip chains require a richer per-level sampling representation. Do not flatten them by nearest stretching or drop source levels.
- Mipmapped legacy CLAMP and unequal min/mag legacy CLAMP need the original derivatives plus clamped coordinates at evaluation time and an explicit export contract.
- Pixel Data Type 4 (one-bit transport) remains unimplemented/unqualified. Existing signed/unsigned byte/word, float/half and accepted colour layouts retain their prior scope.
- Constant borders for red-/green-/blue-only transport remain unqualified because the storage component alone does not settle the legacy internal-format interpretation.
- Pixel formats 5/6/13 describe stencil, depth and depth-stencil. They remain retained/diagnosed outside base-colour display, rather than being assigned invented RGB values. Other applicable roles remain the later texture-role batch.
- Authentic inline/external exporter agreement and individual native/CPU/OptiX execution are still separate evidence requirements.

## Prepared native checks

`jt_texture_border_batch_tests.cpp` adds known-value full-scene controls across enabled 9.x/10.x envelopes/local versions and both byte orders: S/T/both authored profiles, generated constant-border anti-bleed, a literal odd-length ring area average, equivalent inline/external rings, legacy clamp surface-area/UV bounds and common single-axis carrier copy/GLB preservation. Existing obsolete blanket mixed-axis/generated-border rejection cases are narrowed to still-invalid profiles. Existing other border/image/family checks remain in the suite.

`texture_mip_chain_tests.cpp` adds rectangular single-axis PNG profiles, wrong-axis rejection and representative maximum-LOD interior/border samples through the existing CPU texture sampler. These are prepared assertions, not execution. Use the existing [owner native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls) and renderer test configuration; do not infer their execution from viewer acceptance.

## Original controls and owner gate

From the repository root, after the accepted C2ac controls exist:

```powershell
python -B tools/jt/make_texture_border_batch_controls.py out/jt-research/texture-combiner-batch-ui-fixtures out/jt-research/texture-border-batch-ui-fixtures
```

The generator uses original cubes and literal images, reusing [common assets](JT_SHARED_TEXTURE_CONTROLS.md). It writes 90 positive JT roots, 15 independent comparison pairs, a 34-document launch list and three representative export pairs. Modern 10.0 and representative 9.0 version-1/9.5 version-3 texture records reference original 8.2 geometry, not reheadered vendor data. No fail case enters the viewer list.

Reference mip reduction uses exact-rational area integration independently of the C++ implementation. Legacy references use an independently authored regular cube-face grid at the known UV breakpoints, rather than importing or duplicating the triangle-clipping result. Reference pixels, mesh buffers and canonical source images remain under ignored `out/`.

- Manifest: `out/jt-research/texture-border-batch-ui-fixtures/authoring.json`.
- Launch list: `out/jt-research/c2ad-visual-files.json`.
- Common sources/references: `out/jt-research/texture-common-ui-fixtures/`.
- Export destination: `out/jt-research/texture-border-batch-ui-fixtures/exports/`.

Each ordinary source is `texture-border-batch_<case>_10_0_le.jt`; its reference is `reference_border_batch_<case>.gltf` in the common folder.

| Controls | What to inspect in matching Y-Up/isometric ADS and PBR |
| --- | --- |
| `authored_constant_s_repeat`, `authored_ring_t_mirror` | Border only on the selected axis; repeat/mirror remains on the other. Zoom out for authored colour/alpha level changes |
| `authored_ring_s_rect`, `authored_constant_t_npot` | Rectangular/odd unsampled-axis mapping stays aligned with the reference through zoom transitions; the terminal border remains |
| `authored_ring_both_square` | Existing both-axis border behavior remains intact |
| `generated_constant_both` | Source checker averages on zoom-out without the constant border bleeding into the interior |
| `generated_ring_s_rect_npot`, `generated_ring_t_rect` | Generated border and interior reductions agree with the reference; single-axis terminal sampling persists |
| `external_ring_both`, `external_ring_s_nearest`, `external_ring_ignored` | Decoded external ring is sampled only on requested axes; ignored rings disappear without cropping the interior |
| `legacy_constant_linear`, `legacy_ring_nearest`, `legacy_external_linear` | Outside UVs freeze at the boundary appearance rather than stretching the interior pattern or continuing out to full-border colour |
| `constant_luma_alpha_s_generated` | Luminance interpretation stays grayscale; alpha remains visible through border and mip transitions |

The grouped list also includes two big-endian 9.x representatives and accepted RGBA Replace/source-reference regressions. After the owner builds, launch all positives together in one installed-viewer invocation. The owner checks pair agreement, axis conventions, transparency and representative zoom-out levels. Static authoring inventory alone does not accept this gate.

After visual acceptance, export the JT sources `generated_ring_s_rect_npot`, `external_ring_both` and `legacy_constant_linear` to both GLB and glTF. The manifest's `export_pairs` gives six full output filenames. Reopen and compare with the independent references. This checks private single-axis mip transport, external-ring packaging and portable legacy clamp geometry; export the JT source, not the independent reference.

## Evidence and provenance

Authoring completed. Read-only inventory verified 90 root hashes, 15 reference resource/carrier sets and 34 launch paths. Python syntax and diff whitespace were checked. No importer/native execution, build or visual pass is claimed.

Consulted existing original project code and validation records, retained JT10.0 Rev-B border/wrap/image definitions and retained OpenGL 2.1 clamp/mipmap definitions. The latter specifies filtered mip reduction without requiring one particular kernel; this gate declares its deterministic area-box choice. Primary URLs and retained hashes are in [C2q references](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance): JT10 SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`, OpenGL 2.1 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`. No new download, third-party implementation, asset, vendor software/SDK, disassembly or licence interpretation. Source documents and generated evidence remain local. This records method and limits, not legal clearance or authentic exporter conformance.

## Owner build and launch correction (24 October 2026)

The owner reported build success. The first visual launch mistakenly used the installed viewer dated 16:15 rather than the build-tree viewer dated 17:35. The owner reported solid-grey JT controls; the 17:39 session log contains the superseded both-axis/square border restriction, confirming that this session exercised the older importer. This is a launch error, not acceptance or evidence about the current implementation. Relaunched all 34 positive documents from the newer build-tree executable in one invocation. Visual and export acceptance remain pending. Future launches must compare executable timestamps with the current build before selecting the viewer.


The owner subsequently ran the install target. Verified the installed and build-tree executables have identical SHA-256 hashes and the same 17:35 build timestamp, then launched all 34 positive documents together from the refreshed installed viewer. The earlier direct build-tree launch did not establish a working session; no visual result is claimed for it. Owner visual acceptance remains pending.


## Owner visual acceptance (24 October 2026)

After the refreshed installed-viewer launch and clarification of the five authored colour-transition controls, the owner reported 'All pass'. This accepts the grouped 34-document visual check; it does not establish native/backend-specific test execution. The three representative JT sources still require GLB and glTF export/reopen comparison (six outputs) before this gate is complete.


## Reopened visual discrepancy (24 October 2026)

During the focused three-pair export preparation, the owner reported a slight source/reference colour mismatch for generated_ring_s_rect_npot in both ADS and PBR; external_ring_both and legacy_constant_linear were reported matching. The supplied source/reference screenshots show the reference brighter. This supersedes the earlier blanket visual acceptance for the affected pair. Cause is not established; no fix, export pass or gate completion is claimed. Next evidence: export the affected JT to GLB and inspect its resident texture levels, sampler and material against the independent reference before requesting another build.


The owner reported the affected source's exported GLB and glTF match its appearance. The outputs were found in the prior texture-export-ui-fixtures folder. Read-only comparison of the exported glTF with the independent reference established all seven resident RGBA levels match pixel-for-pixel (192x35 through 3x1), terminal border-3x1 and sampler values match, positions/normals/outer UVs/indices match, and base factors/metalness/roughness match within float precision. The export additionally has neutral white COLOR_0 arrays on both meshes and unused inner UVs; the reference omits these. No causal conclusion follows yet. Two local diagnostic glTF variants were authored: independent reference plus white COLOR_0, and export minus COLOR_0. These isolate colour-attribute presence; visual results remain pending. No C++ build or native tests were run.


## Shared raster light-uniform correction prepared (24 October 2026)

The owner observed the red inner cube also differed between the independent reference and its neutral-white-vertex variant. Screenshot interior samples were uniformly red 119 versus 148, so the white-attribute hypothesis is not treated as established. Existing opt-in draw diagnostics were enabled for a fresh two-reference session, saved locally as out/jt-research/c2ad-reference-vertex-diagnostics.jsonl. Both documents have matching logged material scalars, transforms and bounds. Draw evidence exposes two light-intensity triplets: ambient/diffuse/specular 0.20/0.80/0.50 versus 0.12/1.00/1.00. These correspond exactly to loadRenderSettings versus initial/default-light synchronization; unlike light position, the intensities were not reapplied in setCommonUniforms. The observed GPU mismatch is established; deferred context/timing is the implementation explanation supported by those source paths, not a claim of exhaustive scheduling reproduction.

Prepared a separate shared-renderer correction in ViewportWidget.cpp: upload stored ambient/diffuse/specular every common-uniform draw, where the drawing program is bound. Opt-in diagnostics now record stored vectors alongside actual GPU uniforms. No vertex colours, source image pixels, reference brightness factors or path-tracer light assembly were changed. Owner rebuild/install and focused ADS/PBR comparison remain required. The affected export/reopen appearance was reported preserved by the owner; this does not resolve source/reference appearance qualification until the renderer correction is tested.


## Final owner acceptance (24 October 2026)

After rebuilding/installing the [shared raster light-uniform correction](JT_RASTER_LIGHT_UNIFORM_VALIDATION.md), the owner reported all five focused documents matching in both ADS and PBR: source JT, independent reference, reopened GLB/glTF and white-vertex diagnostic reference. The preceding grouped 34-document pass and matching external-ring/legacy-clamp pairs remain recorded above. Read-only inspection of all six representative application exports reports 6/6 image/carrier/sampler/sidedness comparisons matching the independent references. Outputs were saved in the existing ignored texture-export-ui-fixtures folder, rather than the newly proposed exports subfolder. No source/reference pixel or brightness adjustments were needed.

The bounded C2ad gate is accepted. Prepared native assertions and separate CPU/OptiX runs remain unreported. The image/border representation/evidence limits listed above remain open; acceptance does not complete all C2 work or authentic exporter qualification. Historical pending and reopened-discrepancy entries above are superseded by this acceptance.

Read-only export evidence regeneration from the retained local outputs:

```powershell
@'
import json
from pathlib import Path
r=Path('out/jt-research').resolve()
m=json.loads((r/'texture-border-batch-ui-fixtures/authoring.json').read_text())
cases=[dict(id=p['source'],reference=str(r/'texture-common-ui-fixtures'/p['reference']),exports={f:str(r/'texture-export-ui-fixtures'/Path(p[f]).name) for f in ('glb','gltf')}) for p in m['export_pairs']]
(r/'c2ad-export-checklist.json').write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
'@ | python -B -
python -B tools/jt/inspect_texture_exports.py out/jt-research/c2ad-export-checklist.json out/jt-research/c2ad-export-inspection.json
```
