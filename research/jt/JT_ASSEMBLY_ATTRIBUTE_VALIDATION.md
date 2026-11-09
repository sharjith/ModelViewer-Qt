# JT assembly, attribute and fallback pass

19 October 2026, feature/JT-importer. This extends the initial JT 10.0 visualization importer. It adds no XT conversion and does not claim universal JT coverage.

## Implementation

- JtImporter.cpp retains bounded container loading and diagnostics. The internal JtSceneBuilder.cpp/.h boundary converts decoded graph objects to the owned Assimp scene. No upstream decoder types were added to the public importer interface. This separation permits original synthetic scene fixtures without a vendor CAD model or an exact-geometry backend.
- Material accumulation now honors attribute ignore and force flags, per-field inhibit bits, and inherited final bits. Force overrides final for the current attribute without clearing that state for descendants. Sibling branches retain independent state. The mapped fields are ambient, specular, emission, blending, reflectivity, diffuse RGB, diffuse alpha and bump scaling. Shininess is decoded but its inheritance mapping is deferred rather than assigning it an undocumented flag bit.
- JT 10 material payloads now consume reflectivity and bumpiness. The bumpiness bit-8 association follows the clarification in the published 10.6 reference; the older 10.0 table leaves the entry blank. This does not enable parsing JT 10.6. Base/material attribute versions other than the exercised version 1 fail explicitly.
- Common source-alpha / one-minus-source-alpha blending is supported. Disabled blending and one/zero replacement remain opaque. Other blend-factor combinations emit an approximation diagnostic. Per-vertex colors, per-face palettes and textures remain outside this pass.
- Local transforms preserve nested placement and negative/nonuniform scales. Attribute ignore/final/force handling is applied to transform accumulation. Projective transforms and accumulated singular, nonfinite or numerically unrepresentable render transforms fail explicitly. The hierarchy is preserved; transforms are not coerced into rigid OCCT locations.
- Equal resolved appearances share materials and render meshes. Different appearance variants reuse decoded geometry to create separate material-bound render meshes, avoiding a second decode of an unloaded payload. Geometry resources, render resources and placed occurrences remain distinct concepts.
- Display representations are tried in source order until one works. For LOD groups, only one successful alternative is attached. JT does not assign implicit detail ordering to an ordinary LOD child list, so this policy is not advertised as globally highest-detail selection. Bad or empty alternatives produce diagnostics, then permit another alternative. Cancellation and allocation failures propagate.
- A rejected LOD is rolled back atomically: partial mesh/material resources, cache entries, hidden-node references, exact-shape ownership and success counters are removed. Work/decode budgets are not refunded. No partial rejected subtree remains visible.
- External partitions resolve relative to their own source file, including nested references. Canonical paths must stay under the selected assembly directory. Cycle and cache identities use case-folding on Windows and case-sensitive paths elsewhere. File-stack cleanup occurs on failure as well as success.
- The container reader rejects duplicate graph/property object IDs and validates unknown-element length envelopes before skipping them.

The existing exact-backend hook and per-part stored-tessellation fallback are retained. No Parasolid parser or licensed SDK has been added.

## Evidence and tests

The isolated native importer and decoder were compiled with MSVC AddressSanitizer. Tests exercise:

1. Original synthetic triangle scenes: nested groups/instances, names, material sharing, different appearance variants, hidden state, rotation/translation order, negative/nonuniform scales, final/force interaction, ignored/inhibited attributes, singular transforms, cancellation and cycles.
2. LOD and late-load fallback: invalid preferred geometry, empty alternatives and rejection after a partial hidden mesh has already been built. Only the successful alternative survives in scene resources and counters.
3. Original generated JT 10.0 containers: little/big endian headers, TOCs, uncompressed LSG segments, graph references, string properties and names, duplicate IDs, missing external files, nested cycles and out-of-directory references.
4. End-to-end external assemblies: generated containers refer through a nested external partition to the local NVIDIA Bolt.jt sample. Two occurrences are placed at X=10 and X=20; ancestor final material fields preserve red/green occurrence overrides across partition boundaries. A big-endian assembly referencing the little-endian part also passes.
5. The five original part regressions: unchanged position/triangle counts, normalized stored normals, sampled RGB/opacity, unavailable/rejected/invalid exact-backend fallback, injected accepted-backend ownership and GLB scene round trip.
6. Replayed malformed-input checks: 23 structural/compressed-payload mutations and 48 decoded-payload mutations finish without sanitizer reports or timeouts. Sixteen structural cases reject; the remainder remain readable because the affected payload is unused or an alternate LOD is usable. These probes are not exhaustive fuzzing.

The preferred Bolt display payload mutation now selects the next readable alternative, producing 505 vertices and 448 triangles instead of aborting. The intact file still produces 913 vertices and 904 triangles. Fallback diagnostics identify the rejected alternative; reduced detail is expected when selecting a different representation.

The container fixtures are original serialization code and contain no vendor geometry. The optional end-to-end tests use the local, ignored NVIDIA corpus without committing or packaging it. These synthetic assemblies validate our selected layouts and traversal rules; they are not a substitute for independent JT-reader comparison or diverse real manufacturing assemblies.

## Reproduction and remaining work

Configure tests/jt against the existing dependency prefix, with MV_JT_SANITIZE=ON for the MSVC sanitizer run. Run jt_tests for portable fixtures, or jt_tests <local-sample-folder> to include the five samples and external-assembly geometry checks. CTest runs the portable suite. Full ModelViewer build and UI checks for this pass are left to the owner, as requested earlier.

The subsequent [Switch and RangeLOD pass](JT_DISPLAY_SELECTION_VALIDATION.md) implements authored Switch selection and an optional import-time RangeLOD eye policy. Remaining qualification includes real assembly corpora, additional JT versions/layouts, runtime LOD switching, textures, vertex/face color bindings, palettes, draw-style semantics, richer metadata/PMI, source topology/hash checks and independent geometry comparison. Standalone XT and embedded XT conversion remain separate future work; the current parser still accepts JT 10.0 only.
