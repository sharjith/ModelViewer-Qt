# C2aa: resident texture packaging for application export

24 October 2026, `feature/JT-importer`, after accepted C2z `64d64ef`. Prepared for the owner's next viewer build. No agent C++ build or native-suite execution.

## Reproduced defect and scope

During delegated C2z inspection the agent used File > Export on an original inline-image JT control. The application reported success, but the GLB image contained an unresolved `embedded://` identity rather than a bufferView. An external-image glTF reference exported a bufferView image successfully. Evidence stays local: `out/jt-research/c2z-blend-inspection.glb` and `out/jt-research/c2z-reference-inspection.glb`. These GUI exports are application-path evidence, unlike the earlier direct aiScene copy/export checks. Positions, normals, UVs and triangle indices were also inspected while resolving the separate C2z single-/two-sided reference mismatch.

The material loader retains a decoded QImage and assigns an embedded identity. The common texture packager previously resolved every path as a disk file (apart from its existing glb-source extraction). It could not resolve resident JT images. This is an application export seam exposed by JT, rather than a new JT descriptor or version requirement.

The packager now recognizes embedded identities, obtains image data from the typed material slot or a matching resident mesh texture, validates the private mip carrier, and writes a PNG into the ordinary export texture package. Content hashing includes the entire PNG, so different authored tails do not collapse into a base-only identity. The existing package mapping, material rebuild and GLB embedding/post-processing paths consume that file. Default and variant material loops use the same handling. Disk image paths keep their existing resolver. Missing/malformed resident payloads produce packaging warnings; no image is invented.

PNG encoding preserves top-row-first orientation, RGB/alpha and the application-private authored mip/terminal text. No extra vertical flip, renderer/depth change, new dependency or vendor decoder change is included. The same common package path serves portable glTF image files and binary GLB. Other applications may use only the base image: the private mip carrier is not a standard glTF extension. Other formats, arbitrary variants/roles and authentic textured exports are not qualified by this batch.

## Prepared checks

`embedded_texture_image_tests` checks asymmetric top/bottom texels, zero/partial/full alpha, distinct authored tails, the 3x3 border terminal, PNG round trip and missing/malformed payload rejection. It exercises the actual encoder used by packaging, without a window or graphics context. The package lookup and final application export remain GUI integration checks. Native tests are prepared, not executed.

Owner commands, if running the isolated native checks:

```powershell
cmake --build out/material-wireframe-build --target embedded_texture_image_tests
./out/material-wireframe-build/embedded_texture_image_tests.exe *> out/jt-research/c2aa-native.log
```

The recorded isolated render-test build is out/material-wireframe-build. Put the application dependency bin directory on PATH before running the executable. The owner can build the viewer normally first; no isolated test run is inferred from viewer build success.

## Original controls and next visual gate

Regenerate the accepted C2z controls first using [their documented command](JT_AFFINE_FILTER_VALIDATION.md#original-controls-and-owner-viewer-gate), then:

```powershell
python -B tools/jt/make_texture_export_checklist.py out/jt-research/texture-affine-filter-ui-fixtures out/jt-research/texture-export-ui-fixtures
```

This prepares eight source documents and sixteen target paths, without copying shared geometry/images. The checklist is `out/jt-research/texture-export-ui-fixtures/checklist.json`; the single-instance source list is `out/jt-research/c2aa-visual-files.json`. Seven original JT controls cover base linear Blend, authored Blend, basic 9.0 RGB Decal, modern RGBA/alpha/BGRA rings and advanced 9.5 luminance-alpha rings, including big endian representatives. The eighth source is the ordinary disk-image glTF reference. This is bounded cross-family transport coverage, not a claim about every enabled version or exporter.

After the owner reports build success, the agent will:

1. Open the eight positive sources together in one installed viewer invocation.
2. Use File > Export to write each GLB and glTF path in the checklist, all under ignored out/.
3. Inspect exported images, alpha mode, neutral factors, sampler descriptors, decoded RGBA pixels, authored levels and border terminal against the original independent reference pixels:

```powershell
python -B tools/jt/inspect_texture_exports.py out/jt-research/texture-export-ui-fixtures/checklist.json out/jt-research/c2aa-export-inspection.json
```

4. Reopen the exported documents and compare texture orientation/borders, transparency and representative mip zoom transitions to their sources. Check ADS and representative PBR appearance, with matching camera/up axes and two-sided materials. No fail cases need be opened.

The static inspection script reads exports and writes its report only; it does not invoke the viewer/importer. Pixel/hash agreement alone is not visual acceptance or independent-renderer conformance. This checkpoint remains uncommitted and unaccepted until the next owner build and delegated review. C2z is already accepted separately.

## Provenance and limits

This pass consulted existing original application code (material image loading, resident mesh textures, packaging, application export and private mip carrier), original C2z serializers/reference writers, and GUI output from the owner's built executable. No new external source, vendor tool, SDK/schema, disassembly, third-party implementation, downloaded/private asset or licence interpretation was used. All sources are project-original synthetic controls. This statement records method and evidence limits, not legal clearance. Existing unrelated production diagnostics and dependency changes are preserved. The [remaining C2 scope](JT_C2_REMAINING_SCOPE.md), authentic-export gap and C1/C3-C7 gates remain open.


## Static handoff record

Python syntax and all eight source/reference inventory paths were inspected. The read-only checker rejects the earlier broken embedded-URI GLB and reads the successful disk-reference GLB. Each current original reference's image/carrier can be inspected. This verifies checker reachability and the baseline defect only; it does not execute the prepared packaging implementation. `git diff --check` passed. New research content is Markdown only; existing unrelated owner-supplied production JSON records remain preserved. No new generated file is tracked. The agent closed the completed C2z GUI session before handing back for the next build.


## First owner build and failed GUI integration - 24 October 2026

The owner reported build success. The new build executable SHA-256 was d7553ec1c43d4fbb6b0511cf271fe3e0caef446881fd0324bf2984366417804c. The installed executable still had the earlier C2z hash. Launching directly from the build folder did not expose a usable viewer window; the agent stopped that task-created process, backed up the earlier executable under ignored out/jt-research, copied the owner-built executable into the existing ignored installed runtime, and opened the eight sources in one process. The launch inventory remains out/jt-research/c2aa-agent-launch.json. No build/install command or native test was run by the agent.

File > Export on the first original Blend source produced both checklist GLB and glTF files. The GLB still contained an unresolved embedded identity. The glTF contained a correctly packaged PNG, with exactly matching decoded RGBA pixels (including alpha), but its texture referenced sampler 0 while the sampler array was empty. The PNG's decoded RGBA SHA-256 was 572f860f40984d8d63296d00b3415e4d9ae566c13f9d6db2357e69b2355461c2. The local summary is out/jt-research/c2aa-first-review.json. Reproduce through the first checklist source and the two documented GUI export targets, then inspect with the read-only export tool. Neither result is an accepted round trip. The remaining fourteen exports and visual round trips were not attempted after this shared-path failure.

Source review identified the remaining seams: embedTexturesInScene attached the packaged images but never replaced resident material identities with their scene texture indices; SceneGraphExporter omitted glTF min/mag filter properties. The correction now binds every resolved material slot to its actual *N scene texture reference, reuses that index for repeated paths, and writes min/mag properties in both scene-graph and rebuilt-material export paths. This avoids relying on a later material-name match to recover sampler settings. Existing image paths, wrap descriptors and transforms remain carried by their existing paths. This is common application export code exposed by JT, not 3DXML-specific implementation.

The public Assimp 6.0.4 API headers from the installed vcpkg port revision 3 were additionally inspected for embedded-reference lookup and filter-property names: [scene.h](https://github.com/assimp/assimp/blob/v6.0.4/include/assimp/scene.h), SHA-256 0e2c55e31299e56d604610d6ce937511dd58a05872b00d6ef09a31a36397ae1c; [GltfMaterial.h](https://github.com/assimp/assimp/blob/v6.0.4/include/assimp/GltfMaterial.h), SHA-256 f39f0a025235dece61adb6890f6046a16b559cd6bea5905629447c86d0086cd7. Local installed headers were read; these URLs identify upstream API sources and were not newly downloaded. No vendor decoder or external model was consulted or copied. Existing Assimp notices apply; no third-party source was vendored by this correction.

C2aa remains unaccepted and uncommitted. The agent closed the inspection app for the next owner build. After the rebuild, repeat the first GLB/glTF pair before running the remaining batch and visual mip/alpha/orientation checks. No dependent next implementation gate has been started.


## Second owner build and post-processing sampler failure - 24 October 2026

The owner reported another successful build. Installed/build executable hashes now matched: eccc80e95e7152cd46b50728d7ccc25431e410380fedbaf82f335da1bbfeab38. The eight source documents opened in one installed viewer process, with the inventory under out/jt-research/c2aa-second-launch.json. The first GLB was exported through the GUI again. Its image is now correctly embedded as a bufferView, and decoded RGBA pixels exactly match the original independent reference. The preceding failed pair was preserved with first-review suffixes under the same ignored export folder.

The first GLB still referenced sampler 0 with an empty sampler array. Source review identified a separate post-processing failure: postProcessMaterials cleared all Assimp samplers before runtime-material matching, despite later code/comments using an append-and-preserve policy. An unmatched exported material retained its texture sampler index after the table was discarded. Matched PBR texture-variant changes also were not assigned back to the parent material JSON in the main sampler pass.

The correction removes the destructive sampler reset, preserving exporter samplers for unmatched materials, and writes the modified PBR object back after matched sampler updates. Matched source samplers continue to append with the existing base offset and texture-variant mechanism. No format/profile or renderer change is involved. Only the existing project post-processor was additionally consulted; no new third-party source was used in this pass.

Evidence is out/jt-research/c2aa-second-review.json. Reproduce using the first documented source and GLB target, then inspect the GLB image, texture sampler index and sampler-array length with tools/jt/inspect_texture_exports.py. The full sixteen-export and visual round-trip batch remains pending; no pass or commit is inferred from build success or the corrected embedded image alone. The agent did not run a build or native test. Another owner build is required before this same C2aa gate can continue.

## Third owner build and failed two-sided round trip - 24 October 2026

The owner built executable SHA-256 7936d250456ff0d1f5be60629129adc36dd689b4dc4f810d8a376023977576fe. All sixteen GUI exports were completed between the agent and owner. The owner found the first Blend linear reference, GLB and glTF visually different: the reference had additional diagonal transparent-face overlap patterns, and the exports also differed in brightness. This is a failed visual gate. Earlier image/carrier/sampler equality did not cover material sidedness and was insufficient for acceptance.

Read-only inspection found doubleSided=true on both independent reference materials, but the JT-derived GLB and glTF omitted it. Both export paths wrote a glTF-specific material property instead of Assimp's public AI_MATKEY_TWOSIDED input property. Both now use that public key. The inspector additionally compares textured-material sidedness and the material sidedness list, including the inner opaque cube. The revised report at out/jt-research/c2aa-third-review.json rejects fourteen JT-derived exports for lost sidedness; the two ordinary disk-reference exports pass. Regenerate this report with the same inspection command above. Geometry positions/normals and white vertex-colour payloads agree between the first GLB and glTF; their texture pixels and sampler descriptors also agree. The brightness difference remains unresolved pending a controlled visual comparison after the correction; no claim is made that sidedness alone explains it.

The additional consulted API source is the installed Assimp 6.0.4 port revision 3 [material.h](https://github.com/assimp/assimp/blob/v6.0.4/include/assimp/material.h), SHA-256 2fc5f778520ae64c0f96523a41a6a589d4286e59602ce136e117327164f533b4. It defines the public material key; no new third-party implementation or sample was acquired. The correction is common export code exposed by JT controls. No rendering depth/transparency policy was changed.

C2aa remains unaccepted and uncommitted. The owner must rebuild, then re-export and compare the first Blend linear GLB/glTF pair with the independent reference, using matching rendering mode, camera/up axis and zoom. If that pair matches, repeat the remaining JT-derived exports because the shared material defect affects their existing files too. No C++ build or native test was run by the agent. Independent renderer and native regression qualification remain unreported.

## Owner acceptance - 24 October 2026

After rebuilding the sidedness correction, the owner confirmed the Blend linear reference and both exports matched. The owner then re-exported and reopened all six remaining JT cases and explicitly reported each passed: authored Blend and RGB Decal, RGBA, luminance-alpha, alpha-only and BGRA rings. A seeming JT/reference discrepancy was resolved by matching Y-up camera modes; JT had initially been viewed Z-up. The ordinary disk-reference export pair had already been reported visually correct. This accepts the bounded C2aa GUI round trip, including the earlier brightness concern under matched viewing conditions. It does not qualify every renderer, native suite, variant or enabled JT revision.

The read-only inspection command above subsequently reported 16/16 comparisons passed, now including both material sidedness values in addition to image pixels/alpha, complete authored levels, border terminal and sampler descriptors. The report is out/jt-research/c2aa-export-inspection.json. The agent ran no C++ build or native test. The prior failed records above remain historical observations, superseded for acceptance by this owner confirmation.

At the owner's request, checklist generation now supplies descriptive reference documents such as reference_blend_trilinear.gltf alongside the shared content-addressed originals. They share the existing buffers/images; only small glTF documents are aliased. Hash identity remains in the generated checklist. These names and all controls/exports remain local under ignored out/. No downloadable/private models or generated evidence are committed. Other C2 packages and authentic-export evidence gaps remain open.
