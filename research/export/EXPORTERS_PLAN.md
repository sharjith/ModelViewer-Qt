# Own model exporters: plan

Status: plan only, written 24 November 2026 on `feature/exporters` (rebased onto `dev` at e27098f; the branch holds the per-phase timing commit). No application code in this document's commit. Nothing below was built or tested in this session.

Labels used: **[Obs]** observed in this repository's code or recorded measurements, **[Hyp]** hypothesis or inference not yet measured, **[Rec]** recommendation, **[Open]** needs an owner decision.

## 1. Why

- **[Obs]** Export profile on sample S-A (a vehicle assembly of 2,410 meshes), from the `[ExportProfile]` log lines: OBJ 61 s, GLB 499 s, about 99 % of both inside Assimp's writers. Our own code takes about 1 s for the scene build and about 4 s for the glTF post-processor. Reproduce: export S-A to OBJ and to GLB from the installed build and read the `[ExportProfile]` lines in the log (`%APPDATA%\Sharjith N\ModelViewer\logs`).
- **[Obs]** Recorded cause of the GLB figure: `glTF2::Buffer::Grow` in Assimp 6.0.4 reallocates and copies the whole buffer on every append (quadratic). OBJ: a global `std::map` de-duplicates vertices, normals and UVs.
- **[Obs]** Assimp loses information we then patch back in: the glTF writer drops texture transforms, the extension set, variants, cameras, punctual lights, node visibility and animation pointers, and flattens instancing. `GltfPostProcessor` (5.8 k lines) re-injects them by re-reading the written file and matching its materials back to ours by a signature (`MaterialSignature`, name / index / texture-score fallbacks).
- **[Obs]** The `.deb` uses the system Assimp, so an overlay patch of Assimp would not reach Linux. Own writers fix both platforms. The overlay patch was considered and rejected on 7 November 2026.

## 2. Current pipeline (the contract to preserve)

1. `ModelViewer::exportModelDialog()` builds the format list **from Assimp's registered exporters** plus "3D PDF"; the chosen filter gives the extension.
2. `SceneGraphExporter::buildExportScene()` builds an `aiScene` from the scene graph and live `SceneMesh` objects: hierarchy, positions, normals, UV0, vertex colours (or the Plot3D / simulation colour overrides), one material per distinct material key, an animation-material remap. `flattenTransforms` is on for OBJ / PLY / STL, whose Assimp writers ignore node transforms.
3. `AssImpMeshExporter::exportScene()` runs: package textures, sync scene to the mesh store, apply materials, variant materials, embed textures (GLB) or rewrite paths (other formats), OBJ vertex-colour baking (`ObjColourBaking`), Assimp write, patch GLB image names / MTL, `GltfPostProcessor`, delete temp textures.
4. **[Obs]** `SceneGraphExporter::buildExportScene()` is also used by the 3D PDF path (`SceneToU3d`), so the scene builder is shared and must not change behaviour for it.
5. **[Obs]** Extensions the post-processor writes today (the GLB writer must reproduce each): `KHR_texture_transform`, `KHR_materials_specular`, `clearcoat`, `transmission`, `sheen`, `anisotropy`, `volume`, `volume_scatter`, `iridescence`, `diffuse_transmission`, `ior`, `emissive_strength`, `dispersion`, `unlit`, `pbrSpecularGlossiness`, `KHR_lights_punctual`, `KHR_materials_variants`, `KHR_animation_pointer`, `KHR_node_visibility`; standard cameras, node and morph-weight animation; tangents are removed on purpose.
6. **[Obs]** The post-processor and exporter hold per-export state in process-wide statics (`_pathMapping`, `_embeddedIndexMapping`, `_materialToSourceMeshIndex`, `_variantMatByJsonIdx`, `_pointerAnimNodeNames`, `_textureSubfolder`, `_isGlbExport`). See section 7.

### 2.1 Finding: the chosen format variant is ignored (checked 24 November 2026)

**[Obs]** The export dialog lists one entry per Assimp exporter, so several share an extension: OBJ and "OBJ without material file" (`.obj`), ASCII and binary STL (`.stl`), ASCII and binary PLY (`.ply`), glTF v2 and v1 (`.gltf`), and glb v2 and v1 (`.glb`). Assimp itself does distinguish them: each has its own format id (`obj`, `objnomtl`, `stl`, `stlb`, `ply`, `plyb`, `gltf2`, `glb2`, `gltf`, `glb`). Our code throws that away:
- `exportModelDialog()` uses the selected filter only to look up the **extension** (`filterToExtension`) and appends it to the file name; only the file path reaches the exporter.
- `AssImpMeshExporter::findExportFormat()` matches **on the extension alone and returns the first exporter in Assimp's list**; `exporter.Export(scene, format->id, ...)` then runs that one. A `.glb` is forced to `glb2` (line 1046).
- **[Hyp]**, from the list order shown in the dialog (same enumeration) and not yet confirmed by looking at output files: "OBJ without material file" is unreachable (always `obj` with MTL), "STL (binary)" and "PLY (binary)" give the ASCII file, and the glTF v1 entries give v2. The owner can confirm cheaply: export one part as "STL (binary)" and open the file in a text editor; an ASCII file starts with `solid`.
- **[Rec]** The format choice must travel as an **identity**, not an extension. The registry in step 1 keys writers by a format id (the Assimp ids for Assimp formats, our own ids for own writers) and the dialog returns that id to the exporter. For Assimp formats this alone fixes the bug (call `Export(scene, selectedId, ...)`), before any own writer exists.
- **[Rec]** For the own writers, variants become writer options on one writer instead of separate list entries where that is clearer: OBJ "with / without material file", STL and PLY "binary / ASCII" (owner decision 3). glTF v1 is obsolete and is not written by the own writer: the two glTF v1 entries are dropped from the dialog (owner agreed, 24 November 2026: our importer and every current viewer read v2).

## 3. Design

**[Rec] Writer interface.** A small `IModelWriter` (`write(const ExportRequest&) -> Result`) with the request bundling what the exporter already receives: the `aiScene` from `buildExportScene()` (kept as a neutral geometry/hierarchy container, so the scene builder and the 3D PDF path stay untouched), the ordered `SceneMesh*` list, and `ExportSettings` (lights, cameras, variants, animation data, texture options). A registry maps extension to writer; the dialog's format list becomes "own writers + the Assimp formats not covered", de-duplicated by extension.

**[Rec] Materials straight from `SceneMesh`.** Own writers read the `Material` of each source mesh directly. This removes the whole signature-matching layer, because identity is known instead of reconstructed from the written file. It is the main correctness gain after speed.

**[Rec] One session object per export** (`ExportSession`) owns the texture package, path and index maps, variant data and timing. It replaces the statics, so two exports cannot interfere and nothing needs a manual clear. The agreed namespace-state clean-up (memory note of 23 November 2026) lands here for the export half.

**[Rec] Output building.** A growable binary buffer with geometric growth and one write at the end; JSON built once. **[Hyp]** `QJsonDocument` is fast enough for a few thousand meshes; measure on S-A before choosing a streaming JSON writer.

**[Hyp] Instancing.** Assimp flattens repeated definitions. An own GLB writer could share one mesh / accessor set between instances. Whether the scene graph carries a reliable definition identity at export time is not verified; check before promising it. Default plan: write per-instance geometry first (parity), then add sharing as a measured optimisation.

**[Rec] Fallback.** Keep Assimp as the path for formats we do not write (FBX, Collada, 3DS, X, X3D, and so on) and as a switchable legacy path for GLB / OBJ during the transition (setting or environment variable; per owner decision 2 in section 6 it is removed before the branch merges to `dev`).

**Non-goals (owner decision, 24 November 2026): Assimp is not being removed.** All reading of formats that Assimp loads stays on Assimp. Assimp's writers stay for every format we do not write ourselves (FBX, Collada, 3DS, X, X3D and the rest), and the export dialog keeps listing them. Only the writers for GLB / glTF, OBJ, STL, PLY and 3MF are replaced, and USD / USDZ is new. The switchable legacy GLB / OBJ path above is a safety net that exists only while the branch is open (removed before merging to `dev`), not a step towards dropping Assimp.

## 4. Order of work and gates

Each step ends at a gate: the owner builds and tests; nothing dependent starts before the result is confirmed. Commits only after the owner confirms.

| Step | Content | Gate (owner) |
|---|---|---|
| 0 | Baseline: export the test set (section 5) with the current exporter into `out/export-baseline/`; record timings. Because of section 2.1 the baseline can only contain the variants the current code actually writes (OBJ with MTL, ASCII STL / PLY, glTF v2); binary and no-MTL baselines come after step 1 | Owner runs the exports and confirms the set is complete |
| 1 | **Format id plumbing (fixes section 2.1: the selected variant reaches the exporter).** Narrowed on 24 November 2026: the dialog keeps each entry's Assimp format id and passes it in `ExportSettings::formatId`; the lookup uses it (extension lookup only when empty); the two glTF v1 entries are dropped; "OBJ without material file" skips colour baking, MTL patching and texture copying. `IModelWriter`, the registry and `ExportSession` move to step 2, where the GLB writer is their first user (an interface with a single Assimp adapter would be speculative). Assimp remains the only writer. Behaviour changes only where 2.1 says: binary STL / PLY, OBJ without MTL are now what the dialog says | Build; export each listed variant and check it (binary STL has no `solid` header, no `mtllib` line in the no-MTL OBJ); other formats unchanged |
| 2 | **GLB / glTF writer** (largest): geometry, indices (16/32-bit), nodes and transforms, materials with all extensions in 2.5, textures (embedded for GLB, folder for `.gltf`), samplers, texture transforms, lights, cameras, variants, node visibility, node / morph / pointer animation | Structural diff against the baseline; re-import round trip; validator; S-A timing |
| 3 | OBJ + MTL (keeps `ObjColourBaking`, unique material names) | Round trip; S-A timing |
| 4 | STL (binary and ASCII) | Round trip |
| 5 | PLY (colours) | Round trip |
| 6 | 3MF (needs a zip writer; **[Obs]** minizip is already a dependency of the 3D PDF work) | Round trip; a slicer opens it |
| 7 | USD / USDZ (new capability, own research note first) | Separate plan |

## 5. Verification

- **Test set (neutral IDs, files stay local under `out/` or outside the repo):** S-A (2,410-mesh assembly, timing); a glTF sample set from the Khronos sample assets (materials / extensions, already the project's reference suite); one file each with variants, cameras and punctual lights, node animation, morph targets, KHR_animation_pointer, texture transforms, vertex colours, and an STEP / JT / 3DXML import (colours and hierarchy from the CAD importers). **[Open]** which exact local files make up each entry; the owner names them.
- **Structural diff tool** `tools/export/` (to be written): compares two glTF / GLB files ignoring array order and generated names, reports missing extensions, materials, nodes, accessors. Regression fixtures generator under `tests/export/`.
- **Round trip:** export with the new writer, re-import with our importer, compare mesh / material / node counts and bounds with the original and with the baseline output.
- **External validation:** the Khronos glTF-Validator, release `2.0.0-dev.3.10` (22 October 2024), asset `gltf_validator-2.0.0-dev.3.10-win64.zip` (2,320,898 bytes, SHA-256 `c5068f51205deedc28acc3529ee7e11ee60e853454f673093398eba80142202c`) from `https://github.com/KhronosGroup/glTF-Validator/releases/tag/2.0.0-dev.3.10`, downloaded with the owner's authorisation on 24 November 2026 into the ignored `out/tools/gltf-validator/`. Run `out/tools/gltf-validator/bin/gltf_validator.exe <file>`; a clean Khronos sample reports 0 errors.
- **Structural diff / profile tool:** `tools/export/gltf_profile.py` (`profile`, `diff`, `summary` subcommands; read-only, stdlib only).
- **Timing:** `[ExportProfile]` lines on S-A for every new writer, compared with 61 s / 499 s.

## 6. Owner decisions (24 November 2026)

1. **GLB and plain `.gltf` both** from the own writer in step 2 (same JSON and buffer code; `.gltf` writes the `.bin` and the texture folder).
2. **Legacy Assimp GLB / OBJ path is removed before `feature/exporters` merges to `dev`.** It exists only while the branch is open, as the baseline and the safety net; its removal is the owner's robustness signal. (This supersedes the "one release after" wording in section 3.)
3. **STL: binary and ASCII both**, as two choices in the format list or an option in the export dialog (binary default). **[Open]** UI shape, decided in step 4.
4. **3MF: the Materials extension** (the 3MF Materials and Properties extension, so PBR-ish and textured materials survive), not only basic colour groups. **[Open]** which parts of the extension the common slicers read; test with at least two readers in step 6 and fall back to base materials / colour groups for readers that ignore the extension.
5. **Units and axes: owner is unsure which formats carry units.** See the table below; decision recorded in 6.1.
6. **glTF validator:** the owner may fetch it or authorise me to. **[Open]** before the first download I state the file name, source and size and wait for a yes (a download needs explicit permission).

### 6.1 Units and axes by format

**[Recall]** (from the specifications, not re-verified against a source in this session; confirm each when its writer is written):

| Format | Length unit in the file | Up axis / handedness |
|---|---|---|
| glTF / GLB | Fixed by the spec: 1 unit = 1 metre; no unit field | +Y up, right-handed |
| OBJ / MTL | None (unitless; convention is whatever the author used) | Not defined (commonly +Y up) |
| STL | None (unitless; slicers and CAD assume millimetres) | Not defined (commonly +Z up) |
| PLY | None | Not defined |
| 3MF | Explicit `unit` attribute on the model: micron, millimeter, centimeter, inch, foot, meter | +Z up, right-handed |
| USD / USDZ | `metersPerUnit` and `upAxis` metadata on the stage | Declared (`Y` or `Z`) |
| Assimp's FBX, Collada | FBX has a unit scale, Collada a `<unit>` element; Assimp's writers handle them | Declared |

**[Obs]** The scene already knows a real-world length unit per imported file: `SceneNode::importUnit`, with the document's default and a millimetre fallback (`resolveEffectiveImportUnit()`, `LengthUnits.h`; the "Import Units..." dialog). Coordinates in the scene are the file's own numbers; the unit says what they mean.

**[Rec]** One export-units choice in the export dialog, default "as in the scene" (no scaling, today's behaviour):
- Formats with a unit field (3MF, USD, glTF where it is fixed to metres) always write the correct field or scale the coordinates to the spec'd unit, from each file's resolved import unit, so the file is right whatever the option says.
- Unitless formats (OBJ, STL, PLY) write the numbers as they are by default, and the option can scale them to a chosen unit (millimetre, centimetre, metre, inch) for the receiving tool.
- A scene with files of different import units is brought to one unit before writing (per-file scale on the node transform), which is also what makes mixed-unit assemblies come out consistent.
**[Obs] Baseline (24 November 2026, measured on vertex data).** The current exporter writes the scene's numbers unchanged in every format (no scaling): a 2.5 m JT assembly is written as about 2,500 in a glTF file (glTF numbers are metres), and a 3MF declares `unit="millimeter"` whatever the source unit was (that 3MF export is also empty, section 2.2 of `FINDINGS`, local). For the STL sphere sample, the app's bounding-box display is the same whichever import unit is chosen (millimetre or inch).

**[Obs] How the app treats units.** `SceneNode::importUnit` is a label on the file node ("what do these numbers mean"), "never a geometry scale"; it is used to convert Mass Properties and Surface Analysis results to millimetres. It is **declared** by only some importers (JT from its own header, 3DXML and Parasolid / XT as millimetre, the CAD loaders at `AssImpModelLoader.cpp` 1663 / 1699) and is `Unknown` for formats that carry none (STL, OBJ, glTF, FBX), where analysis falls back to millimetres without recording it as known. The user can set it per file (Import Units...), which marks it `importUnitUserOverridden`.

**[Rec] Rule for the export-units option:** scale only from a unit that is **known** (declared by the importer or set by the user); a file whose unit is `Unknown` is written unchanged. Without this rule a glTF-sourced model (metres, `Unknown`) would be scaled by 0.001 when exported to glTF through the millimetre fallback. Consequence: to get a correctly scaled export of an STL / OBJ the user first sets its unit in Import Units..., which is exactly the existing "this file's real-world unit" control, and an export dialog option then only chooses the target unit (as in the table above). Formats that name a unit (3MF, USD) write the target unit; glTF writes metres; OBJ / STL / PLY write the chosen target (default: the source's own numbers, unchanged).

## 7. Related clean-up folded into this work

From the namespace-state audit: `AppContext::SessionId()` and the simulation load counter (small, separate commit, any time); the `GltfPostProcessor` statics become `ExportSession` state in step 1 / 2; the `BRepToAssimpConverter` and `AssImpModelLoader` load-side caches are import-side and stay a separate step after export.

## 8. Step 2 design: the glTF / GLB writer

Status: design for owner review (24 November 2026). No code until it is accepted. Everything marked **[Obs]** was read in the code on that date.

### 8.1 What the writer consumes

- **[Obs]** Geometry and hierarchy come from `SceneGraphExporter::buildExportScene()` as an `aiScene`: nodes, meshes (positions, normals, up to 4 UV sets, `COLOR_0`, bones, morph targets), animation clips (node TRS and morph weights). This stays the writer's input, so the scene builder and the 3D PDF path are untouched.
- **[Obs]** Materials come from the live `SceneMesh` / `Material` objects: one `Material` class with 25 texture types (each a `Texture` with path, `QImage`, UV transform, wrap and filter modes), scalar factors for every extension the post-processor writes today, `ShadingModel`, `BlendMode`, two-sided, alpha threshold, normal scale and occlusion strength.
- **[Obs]** Variants (`MeshVariantExportEntry`, per-mesh mappings and pre-built variant materials), cameras (`GltfCameraEntry`), punctual lights (`GPULight`) and pointer / visibility animation (`GltfAnimationData`) arrive in `ExportSettings`.
- **[Obs]** `buildMeshFromSceneMesh()` writes `COLOR_0` for **every** mesh (default white) and every UV set. This, not Assimp, is where the "invented `COLOR_0`" in the baseline comes from. The writer writes `COLOR_0` only when `RenderableMesh::hasVertexColors()` is true (or a colour override is supplied).

### 8.2 Components (new folder `src/Export/`, headers in `include/Export/`)

| Component | Job |
|---|---|
| `IModelWriter`, `ExportRequest`, `ExportResult` | The interface and its input / result (success, message, warnings). `ExportRequest` bundles the `aiScene`, the ordered `SceneMesh*` list, `ExportSettings`, the output path and the format id. |
| `ExportFormatRegistry` | Format id -> writer. Own writers first; the remaining Assimp formats are listed from Assimp's registry; the dialog is built from it. Env var `MODELVIEWER_LEGACY_GLTF_EXPORT=1` routes glb / gltf back to Assimp while the branch is open (removed before merge, owner decision 2). |
| `ExportSession` | Everything that is per export: texture store, warnings, `ExportProfile` timings, variant / light / camera data. Replaces the `GltfPostProcessor` statics for this path; nothing process-wide. |
| `GltfWriter` | Orchestrates one export; writes `.glb` or `.gltf` + `.bin` + texture folder from the same code. |
| `GltfBinaryBuffer` | One growable buffer with geometric growth and 4-byte alignment; accessors record exact `min` / `max` computed from the data they write (fixes the validator's `ACCESSOR_MIN_MISMATCH`). |
| `GltfTextureStore` | Images, samplers, textures; one entry per distinct image (content hash) and per distinct sampler; applies `KHR_texture_transform` only where a transform is not the identity. |
| `GltfMaterialWriter` | Maps `Material` to glTF (8.3). Extensions are written only when a value differs from the spec default (no invented `ior`, `specular`, `volume`, `emissive_strength`). |
| `GltfAnimationWriter` | Node TRS, morph weights, skins, `KHR_animation_pointer`, `KHR_node_visibility`. |

### 8.3 Mapping of our model to glTF (parity list, from the baseline and the post-processor)

- **Base:** albedo colour / opacity and texture -> `baseColorFactor` / `baseColorTexture`; metalness, roughness -> factors; separate metallic and roughness textures are **packed** into one metallic-roughness texture (G = roughness, B = metallic) and AO into the occlusion texture where the channel layout needs it (the existing packing code in `AssImpMeshExporter` is moved into a shared helper, not rewritten); normal (with scale), occlusion (with strength), emissive (colour, texture).
- **State:** `BlendMode` Opaque / Masked (with alpha cutoff) / Alpha -> `alphaMode`; Additive and Multiply have no glTF equivalent (written as BLEND, with a warning in the result); two-sided -> `doubleSided`; `ShadingModel::Unlit` -> `KHR_materials_unlit`.
- **Extensions (written when non-default):** `emissive_strength`, `ior`, `specular`, `clearcoat` (factor, roughness, normal), `sheen`, `transmission`, `volume`, `iridescence`, `anisotropy`, `diffuse_transmission`, `dispersion`, `pbrSpecularGlossiness` (our `Diffuse` and `SpecularGlossiness` texture types), `texture_transform` on every texture info that needs it, and the extension lists (`extensionsUsed`, and `extensionsRequired` only where the spec requires it).
- **Scene features:** `KHR_lights_punctual` (with the light names), cameras (perspective and orthographic), `KHR_materials_variants` (default material plus the mappings, written straight from the per-mesh data instead of patched in), node animation, morph targets and weight animation, skins (joints, inverse bind matrices, `JOINTS_0` / `WEIGHTS_0`), pointer animation, node visibility.
- **Geometry:** positions, normals, `TEXCOORD_0..3`, `COLOR_0` (conditional), tangents (**[Open]** D3 below), indices as 16-bit when the mesh has at most 65,535 vertices else 32-bit, primitive modes (points, lines, strips, fans as the source has them), sparse accessors not written.
- **Nodes:** the aiScene's synthetic root is not written; the scene lists its children (**[Rec]**, removes the `SceneRoot` wrapper seen in the baseline). Names are kept; unique-name handling stays in `buildExportScene()`.
- **Units:** glTF is metres (6.1): scale from a known import unit, none for an unknown one.

### 8.4 Stages inside step 2 (each ends at an owner build + test gate)

| Stage | Content | Pass criteria |
|---|---|---|
| 2a | Infrastructure (`IModelWriter`, registry with env-var fallback, `ExportSession`, `GltfBinaryBuffer`) and a geometry-only GLB **and** glTF writer: hierarchy, positions, normals, UVs, indices, conditional `COLOR_0`, default material | Validator: 0 errors on K01 and S-A; geometry equals the input (vertex / triangle counts, actual bounds from the buffer); S-A time recorded |
| 2b | Core PBR materials, textures, samplers, texture transform, alpha modes, two-sided, unlit, units (known unit only) | K-set core models validate with 0 errors; `gltf_profile.py diff` against the Khronos input shows no loss; image bytes policy (D1) in effect |
| 2c | Material extensions | The extension models of the K-set (anisotropy, clearcoat, sheen, transmission, iridescence, diffuse transmission, dispersion, emissive strength, spec / gloss, volume) validate and match the input |
| 2d | Lights, cameras, variants | Lights / cameras / variants models match the input; round trip through our importer keeps variants |
| 2e | Animation: node, morph, skin, pointer, visibility | Animation models match the input and play after a round trip |
| 2f | Cleanup: remove the legacy path and the now unused post-processing for glb / gltf (before merge to `dev`), final timings | Owner acceptance; legacy env var gone |

Targets (**[Hyp]**, to be measured): S-A GLB in well under a minute (the 536 s of the old path is the writer's quadratic growth; disk write of the 327 MB file is the floor), OBJ unchanged until step 3.

### 8.5 Decisions needed from the owner

- **D1 Texture bytes.** **[Obs]** The old exporter re-encodes every texture as a PNG (12.6 MB each for the helmet; 3.77 MB -> 63.9 MB), and the importer writes textures into a cache folder as PNG (path `...\ModelViewerCache\<id>\<model>\image_N.png`), so the original encoded bytes are no longer at hand at export time. **[Rec]** (a) the importer keeps the original encoded bytes of an embedded or file texture (file path, or the embedded blob) so the writer copies them unchanged (size = input, no quality loss); (b) where only a decoded image exists (generated, edited or packed textures), PNG when it has alpha and JPEG (quality 92) otherwise, with the choice recorded in the result. (a) touches the importer and is its own small step before 2b; if you prefer, 2b can start with (b) only.
- **D2 Node wrapper.** Drop the synthetic root (8.3, recommended) or keep a root node named after the document.
- **D3 Tangents.** Today they are removed on purpose. **[Rec]** keep that (viewers regenerate them; the validator reports only a warning), unless the source had authored tangents, in which case write them.
- **D4 Instancing.** Write per-instance geometry first (parity), then share mesh / accessor data between identical definitions as a measured optimisation in 2f. Agree?
- **D5 Where the export runs.** The export blocks the window ("Not responding", about 9 minutes for the old path on S-A). **[Rec]** keep it on the UI thread in 2a to 2e (so the writer can be compared with the old path under the same conditions) and move the write to a worker with a progress dialog and cancel in 2f, once the writer is fast enough that this is a comfort, not a necessity.
