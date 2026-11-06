# C2y: composed authored mips and border-aware filtering

23 October 2026, `feature/JT-importer`, after accepted C2x `7bb4abc`. **Owner build and visual gate accepted on 23 October 2026. No agent C++ build or native-suite execution; scoped GUI review is recorded below.**

## Implemented batch

| Area | Prepared addition | Boundary |
|---|---|---|
| Composed chains | Apply the existing qualified Decal/Blend/Add/Replace/Modulate rules independently at every matching authored level; preserve level alpha and ordered units | Co-sampled units require the same coordinate mapping, wraps, filter and dimensions/count/profile. Nonlinear composition is limited to nearest magnification and nearest or nearest-mip-nearest minification. Mixed generated/authored chains and independent filtering remain diagnosed |
| Sampled border chains | Both-axis constant borders and inline RGBA/BGRA rings with a complete square power-of-two interior chain, minimum 2x2 base interior | Single supported Modulate/direct Replace paths use existing nearest/linear/mip-linear filters. Composed border chains retain the composition filter restriction. Mixed sampled axes, rectangular/non-power-of-two sampled interiors, generated border mipmaps, external rings and legacy CLAMP remain unqualified |
| Unsampled rings | Strip the source ring at each authored level when wrapping does not sample it | Ordinary normalized full-chain rules still apply after cropping |
| Preservation | Versioned private PNG terminal profile, existing raster/CPU/OptiX transport, bounded decoding, source metadata and diagnostics | No new portable glTF mip extension, authentic textured-export conformance or backend-specific execution claim |

This changes original application adapter/carrier code and original tests/tools. No vendored decoder file, upstream-derived source/hash, dependency, enabled JT version, About text or licence notice changes. The accepted depth-policy fix is unchanged.

## Why level-by-level and nearest composition

Composing only the base image and regenerating its lower levels destroys authored data. The adapter now composes each matching source level separately and attaches the resulting chain. A material's incoming colour/alpha is restarted for each output texel and each level; alpha detection covers the whole chain. Existing linear-colour math is reused, with one sRGB display encoding at each output level.

Filtering a composed image is generally not equivalent to separately filtering source stages and then combining them. Nearest texel/nearest mip selection avoids that discrepancy in this bounded path. Linear/mip-linear combinations of nonlinear stages remain a diagnosed representation limit rather than being silently approximated. Single-stage paths whose existing mapping is already supported retain their filters.

## Border representation and validation

Source ring levels follow the OpenGL-compatible interior recurrence: halve each interior dimension to at least one, then add the declared one-texel ring on each side. Thus an original 4x4 interior with a ring has stored sizes 6x6, 4x4 and 3x3. This published-profile interpretation is exercised with original controls, not an authentic exporter comparison.

Ordinary one-texel padding changes its normalized interior footprint at every level. For the qualified sampled-square profile, each W-by-W interior is instead placed in the centre of a 3W-by-3W display image. Ring edges/corners are extended outward without filtering them into the interior; constant-border texels use the existing internal-format colour/alpha interpretation. The mapping is always `(sourceUV+1)/3`. Display levels therefore halve normally, with a final 3x3 level containing the source's final 1x1 interior. The coordinate derivative times image extent remains W, avoiding an artificial raster LOD bias from the expanded dimensions.

`ModelViewer.MipChain.Terminal.v1=border-3x3` qualifies this internal exception to the ordinary 1x1 terminal rule. A flagged chain must be square, start at `3*2^N`, contain exactly N higher levels and end at 3x3. Unknown, missing, incomplete, oversized and nested profiles are rejected. The existing raster uploader sets maximum level to the supplied count; CPU/OptiX sampling already clamps to that count. No extra generated 1x1 level is appended to erase the last border. Decoding/format conversion and PNG-in-GLB retain the text field. Other applications may use only the base PNG or regenerate their own levels.

Source payloads and raw metadata remain separate from display images. The existing byte/resource limits and cancellation checks remain active; expanded allocations and composition levels are charged. Bad chains or unsupported profiles retain geometry with a warning and no partially applied texture stack.

## Controls and local authoring evidence

Regenerate from the existing common controls:

```powershell
python -B tools/jt/make_texture_mip_batch_controls.py out/jt-research/texture-mip-ui-fixtures out/jt-research/texture-mip-batch-ui-fixtures
```

Authoring produced 60 positive JT roots and ten shared glTF references for ten cases: Decal, Blend, Add, two-unit Add, two-unit alpha Modulate, constant-border nearest/linear, explicit-ring nearest/linear, and composed explicit-ring Add. Original 64x64 interiors have deliberately different green/yellow/cyan/magenta/orange/white lower levels. Roots cover 10.0, 9.0 local-1 and 9.5 local-3, each byte order. They use independently authored modern/9.x descriptor envelopes and common original 8.2 geometry companions; this mixed-family synthetic setup is not an authentic same-version export.

Expected reference pixels are independently calculated by the Python writer. References use the same documented ModelViewer-private PNG carrier and renderer, so they compare expected pixels/mapping, not independent backend conformance. Geometry, buffers and images are content-addressed in `out/jt-research/texture-common-ui-fixtures/`; nothing is duplicated into each gate folder. Gate `authoring.json` records root hashes, case/reference mappings and provenance. `out/jt-research/c2y-visual-files.json` lists 25 positive documents, including comparisons, two 9.x cases and three accepted regressions. No failure case is included in the viewer launch list.

Original C2v and C2x writers were regenerated after optional writer extensions: all 110 and 30 existing JT root hashes respectively stayed unchanged. The comparison baseline is local at `out/jt-research/c2y-authoring-baselines.json`. This is authoring/static evidence, not importer execution. No sample-derived or generated asset is added to Git.

## Owner build/native gate

Rebuild the viewer normally. The prepared `textureMipBatchTests()` is part of `jt_tests`; it covers enabled 9.x/modern local families and byte orders, literal composed colours/alpha, each qualified border level, UV/geometry preservation, ignored-ring crops, composed rings, scene copy/GLB carriage, mismatched chains/filters and invalid-border fallback. The carrier/sampler target adds malformed terminal-profile, PNG/format-conversion and final 3x3 interior/border sampling checks. These checks are prepared, not executed by the agent.

```powershell
cmake --build out/jt-selection-build --target jt_tests
$jtPrefix = (Resolve-Path 'out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows').Path
$env:PATH = "$jtPrefix/bin;$env:PATH"
./out/jt-selection-build/jt_tests.exe *> out/jt-research/c2y-jt-native.log
cmake -S tests/render -B out/jt-filter-build -G Ninja "-DCMAKE_PREFIX_PATH=$jtPrefix"
cmake --build out/jt-filter-build --target texture_mip_chain_tests rt_texture_filter_tests blend_depth_policy_tests
./out/jt-filter-build/texture_mip_chain_tests.exe *> out/jt-research/c2y-mip-native.log
./out/jt-filter-build/rt_texture_filter_tests.exe *> out/jt-research/c2y-filter-native.log
$env:QT_PLUGIN_PATH = "$jtPrefix/Qt6/plugins"
./out/jt-filter-build/blend_depth_policy_tests.exe *> out/jt-research/c2y-depth-regression.log
```

## Owner visual gate after build

The agent will launch the 25 checklist documents together in one installed viewer invocation after the owner reports build completion. Judge deselected objects with matched camera/up-axis settings, initially in PBR. Compare each JT with its named reference from `authoring.json`; exact lighting is not the criterion.

| Cases | Check |
|---|---|
| `decal`, `blend`, `add` | JT and reference agree close up and change together as the cube is zoomed down through successive authored colours |
| `stack_add` | Added red contribution remains visible at the same level transitions in both documents |
| `stack_alpha` | Some coarse levels expose the red inner cube; other levels remain opaque, matching the reference |
| `constant_nearest`, `constant_linear` | Magenta outside the interior persists as the model shrinks. Nearest transitions are sharp; linear transitions soften and blend between levels |
| `ring_nearest`, `ring_linear` | Side/top/bottom border colours and white corners retain their positions at every level. Border placement must not stretch, drift or disappear at the smallest zoom |
| `ring_stack_add` | Both the border colours and interior level colours match the composed reference |
| Two 9.x controls | Match the corresponding 10.0 case/reference |
| Accepted alpha and filter regressions | Partial alpha still reveals the inner object; nearest/linear retain sharp/smooth distinction |

Rotate and zoom rather than judging only the initial fit. Also check ADS for appearance/visibility regressions; report any CPU/OptiX checks separately. Stop for owner confirmation before commit/push or dependent implementation.

## References and provenance

Consulted retained Siemens JT10.0 Rev-B texture environment, image/border/mip count and internal-format definitions; retained JT9.5 Rev-A compatible texture hierarchy; and retained OpenGL2.1 sections 3.8.1/3.8.7/3.8.8/3.8.13 for interior/border dimensions, filtering and combiner order. [Siemens JT10 reference](https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf), SHA-256 `620828f47cf3b450c73ceaf3737f4ae5d344b39e73200c7b388ecc5ff92f376b`. Retained JT9.5 SHA-256 `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`; no newly verified publisher URL. [Official OpenGL2.1 mirror](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/gl/glspec21.pdf), SHA-256 `996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f`.

Also read [Khronos glTexParameter source](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glTexParameter.xml) for maximum-level semantics; accessed 23 October 2026, retained XML SHA-256 `3d00e817dfb6fdbd599a6e3a54055ace79afe0a69e6cdc39f174e39966624404`. The adapter/renderer and original writers were inspected. No vendor software, SDK/schema, disassembly, third-party implementation or new asset was used; the public reference XML download stays under ignored out/. No licence interpretation or clearance is asserted. Other C1/C2/C3-C7 work and 10.11 remain separate.

## Owner build and checklist launch - 23 October 2026

The owner reported a successful viewer build. The agent then launched all 25 positive checklist documents together in one installed viewer invocation after verifying that its executable hash matched the build output. The local launch record is `out/jt-research/c2y-single-instance-launch.json`. This records launch/build status only: visual acceptance and prepared native-suite execution remain unreported. Do not commit or advance dependent work based on build completion alone.

## Owner observation and agent visual review - 23 October 2026

The owner reported that all open controls looked good and requested a second visual review. Using the installed viewer GUI, the agent visited all 25 checklist documents individually: ten JT/reference pairs, the two 9.x controls, and three earlier regression controls. Initial views showed the expected opaque/translucent distinction, coloured constant/explicit borders, and sharp nearest versus softened linear filtering. No obvious missing geometry, missing texture, or load error was observed.

Additional zoom-out spot checks on the 10.0 additive stack, alpha stack and linear explicit-ring controls showed lower-level colour changes; the explicit ring remained visible around the changing interior. These were representative observations, not a sweep of every mip level. JT and glTF initial cameras used different Z-up/Y-up conventions: the review does not establish pixel equality or certify border orientation on every face. ADS, CPU/OptiX, rotation-dependent sorting and the prepared native suites were not exercised by the agent. This GUI sanity check supports the owner's positive observation within that scope; it does not replace the remaining checklist/native evidence.

Method: open the positive list described above together, use the viewer Window menu/Next command to visit every document, then zoom the representative controls from fit to small on-screen size. No build, native test command, vendor software or additional third-party source was used for this review. Generated assets remain local under ignored out/.


## Acceptance - 23 October 2026

Following the positive owner observation and scoped agent GUI review above, the owner authorized the usual commit, push and next-gate workflow. C2y is accepted for the implemented scope. Native/backend-specific execution and the documented reference gaps remain unreported; acceptance does not broaden those claims.
