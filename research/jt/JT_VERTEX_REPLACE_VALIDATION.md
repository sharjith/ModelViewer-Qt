# Vertex-colour Replace - C2ab

24 October 2026, `feature/JT-importer`, following owner-accepted and pushed C2aa `a2debec`. **Owner accepted the bounded viewer gate on 24 October 2026.** No C++ build, importer execution or native test was run by the agent.

## Bounded implementation

This advances remaining C2 packages 1 and 7. A single qualified inline RGB/BGR/RGBA/BGRA Replace stage can now operate on a mesh with vertex colours. The appearance-specific mesh copy receives neutral white RGB after successful image and UV mapping. RGB/BGR retains incoming vertex alpha; RGBA/BGRA neutralizes incoming vertex alpha because the image replaces it. Existing blend-disable handling still forces opaque output. Material tint is already bypassed by the existing vertex-colour path; this change does not multiply a second material-alpha factor into vertex alpha.

The operation is exact for varying vertex RGB/alpha in the existing modulating renderer: discarded RGB becomes a constant white interpolant, and RGB Replace keeps the original alpha interpolant. These prepared visual controls exercise constant colours, not all varying-colour cases. Source files and the cached source geometry remain unchanged. Effective mesh arrays change only on the appearance copy; a shared Modulate instance retains its original colours. Flat-normal expansion follows this operation. Missing UVs, invalid mapping or rejected images preserve the existing fallback colours. The adapter change does not alter depth policy, enabled versions, vendor decoder, dependency or About information. Separate shared renderer corrections exposed during validation are recorded below.

Other vertex-colour combiners, independent texture stacks, Combine and other listed C2 roles/evidence remain open. See [remaining scope](JT_C2_REMAINING_SCOPE.md). This is not universal texture/vertex interaction support or authentic exporter qualification.

## Prepared native checks

The existing `jt_texture_replace_tests.cpp` now prepares RGB/BGR/RGBA/BGRA vertex-colour and alpha checks for both byte orders, enabled modern revisions and accepted 9.0/9.5 local texture versions. Incoming RGBA (.25,.5,.75,.25), separate material half alpha, and literal texels distinguish retained from replaced alpha and detect accidental double multiplication. Disabled blending, missing-UV fallback, unsupported vertex combiners and a shared Replace/Modulate geometry pair are included. The older non-vertex fallback assertion for Decal/Blend/Add was stale after later accepted checkpoints; only unsupported Combine remains in that assertion. Other vertex-colour combiners retain their explicit fallback checks. These are prepared checks, not execution results. Use the existing [owner-run native commands](JT_TEXTURE_DESCRIPTOR_VALIDATION.md#prepared-checks-and-controls) if native qualification is desired.

## Original controls and owner gate

Exact generation, after the accepted C2z common resources have been generated using [their command](JT_AFFINE_FILTER_VALIDATION.md#original-controls-and-owner-viewer-gate):

```powershell
python -B tools/jt/make_texture_vertex_replace_controls.py out/jt-research/texture-affine-filter-ui-fixtures out/jt-research/texture-vertex-replace-ui-fixtures
```

This authored 48 original positive roots and a 19-document checklist under ignored out/. The root envelopes cover modern 10.0, 9.0 v1 and 9.5 v3, both byte orders, with original 8.2 constant-colour geometry companions. These are original mixed-family controls, not same-version vendor exports. The common store reuses geometry/buffers/images by content; only the new coloured geometry requires new assets. Existing default geometry generation is unchanged when no vertex colour is supplied.

The launch list is `out/jt-research/c2ab-visual-files.json`; filenames/hashes and reference mappings are in `out/jt-research/texture-vertex-replace-ui-fixtures/authoring.json`. All source and reference documents live under `out/jt-research/texture-common-ui-fixtures/`. References have descriptive names `reference_vertex_<case>.gltf`, sharing the ordinary reference resources. Expected constant vertex factors are independently authored as equivalent glTF material factors; these references use the same viewer and do not establish independent-renderer conformance.

After the owner builds, open the checklist together in one viewer instance. Set matching Y-up and standard isometric views:

| Case suffix | Expected appearance |
| --- | --- |
| rgb_replace / bgr_replace | Untinted red/blue checker; quarter incoming vertex alpha reveals the red inner cube |
| rgba_replace / bgra_replace | Untinted checker; opaque red texels hide the inner cube, partial blue texels reveal it; quarter vertex alpha is discarded |
| rgb_replace_opaque / rgba_replace_opaque | Untinted fully opaque shell; blending is disabled |
| rgb_modulate / rgba_modulate | Checker retains the blue vertex tint and quarter vertex alpha; RGBA additionally multiplies texture alpha |

Each modern positive is named `texture-vertex-replace_<case>_10_0_le.jt`, beside its descriptive reference. The checklist also includes big-endian 9.0 RGB Replace, big-endian 9.5 RGBA Replace and the accepted C2z Blend linear regression. Match patterns, opacity and orientation, rather than accepting a load alone. Rotate if needed to see the inner cube. No fail dialog cases are in this visual list. Stop for owner confirmation before accepting or committing this gate.

## Provenance and limits

This pass consulted existing original adapter, fixture serializers and prior [Replace validation](JT_TEXTURE_REPLACE_VALIDATION.md). The retained OpenGL 2.1 table 3.21 was read locally: RGB Replace preserves incoming alpha, RGBA Replace selects image alpha. [Official reference](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf), retained through the [official repository mirror](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/gl/glspec21.pdf), SHA-256 996a4fb2c77f8b977f06217c76eb9d8b2c208bd445fcc3c71f57f1b862d9048f. Existing JT10/JT9 texture definition provenance and hashes remain in [C2q](JT_TEXTURE_CONSTANT_BORDER_VALIDATION.md#references-and-provenance); no new vendor specification or decoder was acquired or copied.

No external model, vendor tool, SDK/schema, disassembly, third-party implementation, new download or licence interpretation was used. This records method and qualification limits, not legal clearance. Native, individual CPU/OptiX and authentic textured-export evidence remain unreported. Generated controls and reports stay under ignored out/; only documents and original code belong in Git.

## First owner review - 24 October 2026

The owner built and visually inspected the batch with matching Y-up cameras. All cases were reported matching except a slight brightness difference in modern RGB Replace versus its reference. C2ab remains open and uncommitted. The reference represents constant quarter alpha in the material factor; JT represents it in the vertex stream. Source inspection shows both select depth writes off and the shader multiplies scalar and vertex alpha together, so different depth policy is not established as the cause. No importer or renderer change is justified by the screenshot alone.

The generator now additionally writes diagnostic_vertex_rgb_replace_attribute_alpha.gltf under the common ignored control folder. It preserves the original scalar-alpha reference and supplies equivalent white RGB/quarter-alpha COLOR_0 with a neutral material factor for a three-way owner comparison. Generate using the same command above; no app rebuild is required. If this attribute-alpha reference matches JT while differing from the scalar-alpha reference, investigate common material/vertex rendering parity separately rather than silently replacing the original oracle and declaring acceptance. This diagnostic has not yet been visually confirmed. Other owner passes remain recorded; no whole-batch rerun is requested at this point.

## Shared blend-state correction prepared - 24 October 2026

The owner confirmed the attribute-alpha diagnostic and JT match exactly with Y-up cameras. This supports the JT colour/alpha transport but does not resolve the original scalar-alpha comparison. Source review found a concrete common raster state conflict: main_scene.frag premultiplies complete fragment RGB by final scalar/vertex/texture alpha; the viewport transparent passes use ONE / ONE_MINUS_SRC_ALPHA, but SceneMesh::setRenderStateOptimized changes back to SRC_ALPHA / ONE_MINUS_SRC_ALPHA when its cached blend-enable value changes. That applies alpha again to an already premultiplied colour. Because the viewport and mesh both write blend state, the active factors can depend on preceding draws. The base RenderableMesh surface draw has the same straight-alpha override.

Both mesh surface paths now use glBlendFuncSeparate with ONE / ONE_MINUS_SRC_ALPHA for RGB and alpha, matching the shader and viewport. No depth-mask, transparent classification, sorting, wireframe, ray-tracing or JT decoding policy was changed. This is shared renderer maintenance exposed by the controls, not a JT format requirement. Source inspection establishes the conflict; it does not establish that this alone explains every observed brightness difference. No C++ build or native test was run.

After the owner rebuilds, recompare the RGB Replace JT, original scalar reference and attribute diagnostic in ADS and PBR with matching Y-up views. Switch among them repeatedly to check stability. Then smoke-check the RGBA Replace pair, the existing Blend linear control and the owner's previously affected opaque GLB interiors; opaque areas must remain opaque. No re-export or regeneration is required. C2ab and this shared correction remain unaccepted and uncommitted until owner results.

## Follow-up owner review - remaining RGBA discrepancy

The owner rebuilt and confirmed the RGB Replace JT, scalar-alpha reference and attribute-alpha diagnostic now look the same. The subsequent smoke-check session opened the RGBA Replace pair, Blend linear pair and the local opaque-interior regression. The owner reported the RGBA glTF slightly brighter than JT. This is still a failed comparison, not whole-gate acceptance; Blend linear and opaque-interior smoke results have not yet been separately confirmed in this session.

The rendering-mode split (ADS, PBR or both) was requested before another implementation change. Existing source inspection identifies additional reference limitations worth isolating: original JT fixture materials author zero ambient/specular and Phong exponent 16, while the core glTF reference authors metallic/roughness factors without explicit legacy ambient/specular fields. MaterialProcessor derives ambient/specular defaults when those fields are absent. This is a possible lighting difference, not a measured runtime cause. No extra lighting adjustment, reference replacement or importer fix has been made on this basis. No computer-control inspection, native test or agent build was run.

## Owner PBR comparison - unresolved lighting/shadow state

The owner subsequently reported the RGBA pair alike in ADS. In PBR, glTF appeared darker and cast a shorter floor shadow than JT. The previous ADS difference is therefore not a stable confirmed material-conversion failure. The PBR difference remains open; no whole-gate pass is inferred.

Source review shows updateMainLightPosition derives default light position from the current scene sphere, floor size and world-up axis. setCameraUpAxisZUp returns early when the requested axis is already active; an actual axis change refreshes the floor/default light. A controlled Z-Up then Y-Up cycle and the same standard isometric view was requested for both documents, to distinguish stale/default scene lighting from material interpretation. This is a diagnostic request, not an established cause or fix. Shadow extent differences should be resolved as scene/light/floor evidence before adjusting texture colour factors. No additional implementation change, native execution, build or computer-control action was made in this pass.

The owner reported both differences remained after that axis cycle, rejecting stale up-axis refresh as an adequate explanation. Source review confirms the raster shadow shader is depth-only, with no material colour/alpha sampling. The current evidence cannot identify the actual per-document light/bounds values, so no further appearance adjustment is justified yet.

An opt-in diagnostic in updateMainLightPosition now records scene bounds/sphere, model matrix, floor size/offset, effective light position/offset and visible mesh material values. It omits source paths, names, texture payloads and vertex data. It runs only when MODELVIEWER_APPEARANCE_DIAGNOSTICS supplies an absolute local output file; output is limited to 1 MiB and parent folders are not created. The ordinary logger is not required. This is observation only, not a lighting correction. The owner must build before collecting it; the agent has not executed the implementation.

After the owner rebuilds and closes any earlier viewer, the exact launch from the repository root is:

```powershell
$diagPrevious = $env:MODELVIEWER_APPEARANCE_DIAGNOSTICS
try {
    $env:MODELVIEWER_APPEARANCE_DIAGNOSTICS = Join-Path (Get-Location) 'out/jt-research/c2ab-lighting-diagnostics.jsonl'
    $viewerPath = Join-Path (Get-Location) 'out/install/ninja_release_vcpkg/bin/ModelViewer.exe'
    $commonPath = Join-Path (Get-Location) 'out/jt-research/texture-common-ui-fixtures'
    $comparePaths = @((Join-Path $commonPath 'texture-vertex-replace_rgba_replace_10_0_le.jt'), (Join-Path $commonPath 'reference_vertex_rgba_replace.gltf'))
    $compareArguments = ($comparePaths | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $viewerPath -WorkingDirectory (Split-Path $viewerPath) -ArgumentList $compareArguments
} finally {
    $env:MODELVIEWER_APPEARANCE_DIAGNOSTICS = $diagPrevious
}
```

Use PBR and cycle Z-Up then Y-Up on each document to record fresh comparable values. The agent can perform this launch without computer control and read the resulting ignored JSON lines. No native test, further code acceptance or commit is implied by this diagnostic build.

## Diagnostic evidence and light-range correction prepared

The owner built and launched, pointing to the ordinary application logs. Those logs showed loading but did not contain the opt-in snapshot; its destination is the explicit ignored output file above. The agent briefly closed the unmodified comparison session through the app UI and launched the two controls together with the diagnostic environment variable. No C++ build or native test was run. The local executable timestamp was 24 October 2026 10:39:42; the evidence file is out/jt-research/c2ab-lighting-diagnostics.jsonl.

Both controls report bounds [-1,1] on all axes, scene radius 1.7320507764816284 and matching recorded outer/inner material fields: metalness 0, roughness 0.3333333432674408, opacity 1, two-sided enabled and unlit disabled. The initial recorded axes differ (JT Z-up, glTF Y-up), so these rows alone are not a matched-camera visual comparison. However, JT's effective light offset changes from zero to [-1.25,-1.25,0.5773501396179199] during initialization; glTF retains zero. These offsets are generated by the environment panel, not the authored texture.

VisualizationEnvironmentPanel::updateLightPositionRanges resets sliders to range midpoints and writes their values back to the viewport. With range=4*sqrt(3) and offset=2*1.25, the asymmetric X/Y ranges have midpoint -1.25 and the Z range midpoint is range/12, matching the measured unwanted shift. ModelViewer calls this method while synchronizing geometry/visibility UI. Merely refreshing a UI range must not relocate the light, and differing import/initialization sequences can therefore produce different effective lighting for equivalent geometry. This explains a concrete lighting-state mismatch; the owner still needs to confirm its visual consequences.

The correction keeps the exact current viewport offset, blocks slider signals while updating ranges, places slider ticks at that offset and removes the implicit runtime write-back. Deliberate light-position edits and the explicit Reset Lighting action retain their existing handlers. Rounded/clamped slider display values no longer feed back during range refresh; an existing runtime offset outside a new range is preserved. No import material, shadow geometry, depth policy or colour factor is changed. No new external source was consulted.

Another owner build is required. Relaunch with the command above, compare the RGBA pair in matching Y-up/PBR views and inspect that both retain zero default offset. Recheck ADS and the pending Blend linear/opaque-interior smoke cases. A deliberate light-slider change followed by a visibility refresh should remain deliberate rather than snapping to a midpoint. C2ab and the shared corrections remain unaccepted/uncommitted.

## Owner isolation results - 24 October 2026

The owner confirmed that the light-range correction fixes the floor shadow extent. ADS matches, but JT remains brighter on top-facing PBR surfaces. Reversing document opening order does not move the discrepancy. Disabling shadows and self-shadows leaves it present; disabling Environment IBL makes it more prominent. The supplied screenshots show Default Lights enabled and Environment IBL, Shadows and Self Shadows disabled in both views. Environment reflection and shadow sampling are therefore not sufficient explanations of the remaining difference. No acceptance is inferred.

Read-only comparison of ignored diagnostic scene snapshots found identical saved viewer settings and matching material IOR, specular factor/colour, metalness, roughness, opacity and sidedness. Mesh positions, normals and indices match. These snapshots do not save the active default camera pose, and matching serialized settings do not prove matching final GPU uniforms. The existing lighting JSONL predates the light-range correction and has no post-fix matched Y-up JT run; it must not be treated as current evidence.

Source inspection identifies a specific remaining hypothesis: buildSurfaceFrame always calculates V from cameraPos minus fragment position, including orthographic projection. Camera::getRenderPosition uses a finite synthetic orbit eye distance for orthographic views. Different camera positions can therefore change PBR direct specular highlights despite equivalent parallel projection framing. This is an orthographic shading inconsistency, but has not been established as the cause of this comparison. No shader correction has been made on that hypothesis.

The opt-in diagnostics now additionally emit deduplicated draw-uniforms records from setCommonUniforms for the primary camera/main shader: camera position/direction, view/projection/model matrices, effective light position, default-light/IBL flags, exposure values and shadow flags. These are CPU values at uniform submission, not GPU readback. Output uses the same explicit absolute destination and 1 MiB cap, with no source names, paths or geometry recorded. An owner build is required. Relaunch with the preceding command, select matching Y-up/isometric PBR views and reproduce the IBL-off comparison. The capture will allow direct comparison of the actual submitted camera and light values before another rendering change. No build, native test or app control was performed by the agent in this pass. Only local source and owner evidence were consulted; no new third-party source was used.

## Draw evidence rejects camera mismatch; duplicate raster light correction

After the owner rebuilt, a separate CLI launch enabled capture at out/jt-research/c2ab-draw-diagnostics.jsonl (use the earlier launch command with that destination). Matched Y-up draw records contain identical camera position [11.879313468933105,11.879312515258789,11.879312515258789], direction, view matrix, model matrix and effective light position. Matching projection records also exist; later window resizing changes projection extents without changing the eye. This rejects a differing finite camera position as the explanation for this pair. The orthographic shading concern is separate and no shader change is warranted from this evidence.

Source tracing found an explicit format-dependent lighting policy previously missed: shouldUseFallbackLightForVisibleScene excludes glTF/GLB, while refreshFallbackLight creates and enables a white synthetic punctual light for JT. In PBR evaluatePBR accumulates both punctual lights and lightSource when their respective flags are enabled. The environment panel enables punctual lighting on display-mode synchronization. This is a concrete route to extra direct lighting for JT even when the camera, material, IBL and shadow settings match. The ordinary run log records synthetic light creation at the expected scene-scale position/intensity. It does not by itself prove the per-draw punctual flags; owner validation remains necessary.

The prepared correction keeps the synthetic buffer and other consumers unchanged but publishes zero raster punctual lights for that synthetic-fallback branch. Authored punctual lights retain their existing path. Raster default illumination continues through lightSource; the correction prevents a duplicate default source from being accumulated based on input format. No geometry, importer factors, orthographic shader or depth policy is changed. This is a raster-only correction; it makes no claim about the separate ray-tracing light assembly.

The owner also reports an ADS discrepancy in the latest session, with glTF brighter than JT, opposite to PBR. The synthetic punctual term is in the PBR path and is not established as an explanation of ADS. To investigate without another later diagnostic edit, the opt-in draw capture additionally reads back the main program's lightSource ambient/diffuse/specular uniforms. The earlier draw fields remain CPU submission values. Two source paths initialize those components differently (default colour synchronization versus saved render-setting intensity sliders); this is a candidate to measure, not a confirmed cross-document cause.

Owner build/visual verification is required for the raster correction and new light-colour capture. Compare both controls in matching Y-up/isometric ADS and PBR views and report each independently. No agent build/native test was run. Static diff whitespace review passed; C2ab remains unaccepted and uncommitted.


## Owner acceptance - 24 October 2026

The separate [shared raster corrections](JT_RASTER_COMPARISON_CORRECTIONS.md) record covers the blend and illumination fixes exposed by these controls.

The owner rebuilt and reported "That fixed it!" after the duplicate raster-light correction. This accepts the remaining comparison correction and closes the bounded C2ab viewer gate, following the earlier report that the other grouped controls matched. The earlier prepared, failed and pending statements above are historical investigation records. The matched camera evidence rejected the camera-state hypothesis; no orthographic shader change was made.

Acceptance covers the reported viewer comparisons, not execution of the prepared native suite, all varying vertex-colour inputs, individual CPU/OptiX backend coverage or authentic exporter qualification. The separate Blend linear and opaque-interior smoke checks requested during this investigation were not individually reported as passed; no such result is invented. These remain useful regression controls at the next owner gate. General transparent-layer ordering, synthetic ray-tracing light assembly, other vertex combiners and the previously listed evidence/representation gaps remain outside this acceptance.
