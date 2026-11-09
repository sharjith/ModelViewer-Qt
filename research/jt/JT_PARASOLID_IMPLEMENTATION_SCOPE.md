# JT and Parasolid implementation scope

Scope based on research completed 18 October 2026. An initial JT 10.0 tessellation importer is now implemented on `feature/JT-importer`; see [implementation and validation status](JT_NATIVE_IMPORTER.md). Changes remain uncommitted. Exact Parasolid conversion remains future work.

## Deliverables and order

The active feature sequence and owner build/acceptance stops are in [JT development phases](JT_DEVELOPMENT_PHASES.md). **Gate 0 is accepted; the shared legacy quantized display follow-up has passed Gate 1 UI acceptance.** The numbered stages below describe the original broad work breakdown, not the current acceptance-gate numbering.

| Stage | Deliverable | Completion gate |
| --- | --- | --- |
| 0 | Corpus, provenance and compatibility inventory | Representative JT inputs, small XT fixtures and their reference results; explicit version/schema matrix; selected reusable-code/dependency notices |
| 1 | JT inspection tool/library | Validated versioned headers, TOC, segment inventory and graph references, with bounded reads/decompression and cancellation |
| 2 | JT mesh/assembly reader | One qualified version/representation path renders positions, triangle indices and stored normals with correct nested occurrences and LOD |
| 3 | JT viewer integration and coverage expansion | Normal open/import works; appearance, external partitions, additional codecs/versions and regression cases pass the published matrix |
| 4 | Separate XT-to-OCCT feasibility spike | A bounded schema/geometry subset produces checked OCCT topology with quantified agreement to independent reference geometry |
| 5 | Optional production exact-geometry support | Permissions/provenance resolved, sufficient version/geometry coverage, owned exact-shape lifetime and accepted accuracy/robustness results |

Stages 0-3 are the first product scope. Stages 4-5 are separate from successful JT mesh display. Stage 4 may be researched alongside JT development. The existing large local XT assemblies are stress/coverage cases after small fixtures work. Five public JT 10.0 part samples have now been obtained and exercised; this does not qualify assembly or cross-version coverage.

JT 10.6 Revision D is the verified public documentation baseline. The [version compatibility policy](JT_VERSION_COMPATIBILITY.md), checked 19 October 2026, sets the staged visualization target at JT 6.4 through 10.11, with a 7.0 lower cap if 6.4 requires substantial viewer scene-model changes. See the [legacy assessment](JT_LEGACY_COMPATIBILITY_ASSESSMENT.md) for the decision gate, pre-8 container boundary and sample requirements. The [shared lossless 8.0–8.2 family](JT_8X_FAMILY_VALIDATION.md), qualified 10.0 subset and native-tested [10.3 sample A103 path](JT_103_ASSEMBLY_VALIDATION.md) are enabled; the owner accepted the 8.0/10.3 baseline at Gate 0. The [quantized follow-up](JT_LEGACY_QUANTIZED_VALIDATION.md) now imports sample Q82 and has passed Gate 1 UI acceptance. Acquire lawful references and representative samples and qualify each additional revision separately. Do not infer support for every 8.x/9.x/10.x file from a common header or an old reader. Check the grant scope when extending beyond the documented 9.5-and-later patent notice. Vendor reports of 11.0 remain unresolved and outside this target.

## JT module boundary

### Backward-compatibility requirement (19 October 2026)

The owner explicitly requires older-version support as part of a useful JT importer. JT 10.0 was the initial qualified implementation, not the intended product compatibility boundary. The implementation target is version-dispatched visualization support across JT 6.4 through 10.11, including 9.x coverage with 9.5 prioritized. Apply the owner-approved 7.0 lower cap if 6.4 requires substantial viewer scene-model changes. Reuse compatible layout families rather than introducing a parser per minor revision. Do not infer support for every intervening version/minor revision from these targets. Keep version-specific container, attribute and codec adapters behind the existing scene boundary; richer face-group/palette records need not replace the viewer scene graph.

For each family, qualify its container and element layouts, integer/geometry codecs, assembly transforms, normals and appearance against matching references and real/synthetic regression cases. Preserve stored tessellation fallback when exact geometry is unavailable. Reuse audited legacy decoder branches where appropriate, with input limits and existing notices retained. Keep JT 10.0 regressions running while developing the legacy paths. Unsupported encodings within an accepted family must be diagnosed or use a qualified display alternative; accepting a version header alone is insufficient.

The first JT 8.1 lossless path now passes isolated regression tests, including sample L81 and original little/big-endian fixtures, and is enabled alongside JT 10.0. The owner confirms the 8.1 application load works. Coverage remains representation-specific rather than a promise to read every file carrying either version header. See [JT 8.1 validation](JT_81_VALIDATION.md) and [sample L81 investigation](JT_81_CONTAINER_INVESTIGATION.md).

Use `namespace Jt` under `include/Import/Jt` and `src/Import/Jt`. Names below define responsibilities; they need not all become separate classes immediately.

| Component | Responsibility |
| --- | --- |
| `Importer`, `Options`, `ImportResult` | File entry point, diagnostics, progress/cancellation and explicit owned results |
| `FileReader` | Version-specific file header/TOC, byte order, GUID lookup, checked offsets/counts |
| `SegmentReader` | Segment/element envelopes, bounded zlib/LZMA decompression, supported element versions |
| `SceneReader` | Logical scene graph, nodes/instances, local matrices, partition references, names, attributes and LOD references |
| `MeshDecoder` | Version-dispatched integer/topology codecs, position quantization, index expansion, stored-normal decoding and bindings |
| `SceneBuilder` | `aiScene` adaptation, occurrence tree, shared geometry references, material splits/overrides and metadata |

Maintain typed source records and stable source IDs before adapting to Assimp. Preserve double precision when supplied by the source until the render conversion boundary. Associate a part with available representation descriptors so a future exact source can coexist with tessellation; do not build the XT implementation into the JT mesh decoder.

Initial geometry contract:

- Explicit supported triangle/strip/polygon tessellation paths converted into indexed triangles without losing corner-normal discontinuities.
- Correct normal/index binding, with provenance for generated normals when stored normals are absent. Do not merge vertices across hard-normal or appearance boundaries indiscriminately.
- One deterministic supported LOD per representation at first, with diagnostics identifying the choice. Do not render all LODs on top of one another.
- Authored hierarchy and local transforms; preserve definition identity separately from occurrence paths. Handle normal inverse-transpose and mirrored winding appropriately at the render boundary.
- Partition resolution with canonical paths, cycle detection, duplicate-file caching and clear missing-file errors. Keep traversal within an approved model root; no automatic network fetches.
- Material/vertex/face attributes as supported by each encoding. Resolve inherited overrides from the actual JT rules; basic colors/transparency follow geometry and placement.
- Explicit B-Rep-only/unknown-representation diagnostics. Missing required geometry cannot be silently counted as a fully successful import.

Defer PMI rendering, animation, shader programs, texture fidelity, point/line/implicit-primitive representations and runtime LOD switching until sample requirements justify them. Diagnose required unsupported content; safely ignore optional content only when its envelope and relationships permit it.

## Existing application integration

Keep dispatch order in `src/Import/AssImpModelLoader.cpp`:

1. Existing XCAF processor factory.
2. Existing native 3DXML branch.
3. New native JT branch.
4. Existing Assimp fallback.

Own the JT `ImportResult` for at least as long as `_scene` and any node-keyed metadata are used. Do not hand an importer-owned scene to Assimp's ownership/destruction mechanisms. Clear old results between loads and on failure. Reuse existing world-transform traversal and scene-tree emission, rather than flattening to OBJ or round-tripping through GLB.

Files/areas identified in the current repository:

- `include/Import/AssImpModelLoader.h` and `src/Import/AssImpModelLoader.cpp`: result ownership, dispatch, metadata, progress and cancellation.
- `src/App/ModelViewerApplication.cpp`: supported extensions and file-dialog filters, currently extended manually for 3DXML.
- `src/Viewport/ViewportWidget.cpp`: loader worker, cancellation, scene-tree population and format-specific handling. Inspect its 3DXML special cases before assuming the loader branch is sufficient.
- `CMakeLists.txt`, packaging/dependency manifests and `tests/CMakeLists.txt`: importer target, compression libraries, compatible notices and focused tests. Do not rely on a transitive zlib link or assume LZMA is already configured.

Report separately the number of source geometry resources, render primitives/material splits and mesh occurrences. The progress denominator must count the same unit as its numerator; use separate read/decode/build stages. The previous GLB unique-resource/occurrence discrepancy must not recur.

## Reuse decision

Prefer a native C++ module using the published reference. Evaluate adapting selected audited legacy TKJT codecs as an accelerator, with attribution and GPL obligations retained. Avoid importing its entire viewer/build system or assuming its old file layouts are current.

The inspected TKJT snapshot has GPLv2-or-later notices in all 106 scanned C/C++ source/header files, but that scan is not a complete dependency/legal audit. It also has concrete compatibility gaps: 32-bit TOC-offset handling where the current reference specifies 64-bit offsets, and an explicit unimplemented TopoMesh path. OCE, old TBB and build-system modernization would also be needed. A parser that returns success while leaving an unsupported representation empty is unsuitable as our product behavior.

The newer XT candidates are useful research/reference options; a Rust FFI boundary or Python production dependency is not accepted by this plan. Decide original C++ versus an audited library boundary after a small benchmark demonstrates actual benefit. Do not bundle proprietary schema catalogs or appearance libraries merely because the surrounding project has an open-source license.

## Shared future XT module

Proposed `namespace Xt` under `include/Import/Parasolid` and `src/Import/Parasolid`:

- `HeaderReader` / `TextReader` / `BinaryReader`: distinct transports, common normalized records and checked input limits.
- `SchemaRegistry`: explicit base layouts, qualified embedded edits and provenance/version keys. Unknown layouts fail explicitly.
- `RecordGraph` / `BRepModel`: source topology, geometry, attributes, IDs, tolerances and unit evidence, independent of OCCT.
- `GeometryConverter`: analytic/qualified NURBS mapping with sense, range, weight and parameter conversion.
- `TopologyBuilder`: cached shared vertices/edges, pcurves, oriented fins/loops, faces, shells and regions; explicit sheet/solid distinction.
- `XdeBuilder` / `ValidationReport`: assembly definitions/instances, names/colors, owned OCCT document/shapes, quality checks and incomplete-result policy.

Standalone XT and embedded JT XT may share normalized records and geometry mapping. Keep their headers, compression/container rules, segmentation and effective-schema selection separate. Avoid applying a standalone transport reader blindly to a decompressed JT segment.

Initial XT spike: one qualified text schema, small planar/analytic solids with holes and repeated instances; then a neutral-binary counterpart, bounded NURBS cases, periodic seams and curved trimming. Record unsupported procedural/foreign geometry instead of substituting an approximate mesh and calling it exact. Units require source evidence or an explicit caller choice.

For eventual standalone integration, register an `XCAFParasolidProcessor` with `XCAFDocProcessorFactory` when that backend is qualified, so it uses the XCAF stage. Embedded JT exact geometry uses the same XT conversion service under the native JT importer. Preserve JT occurrence structure when attaching exact parts.

The current `IXCAFDocProcessor` exposes only `aiScene*`, and the STEP path closes its XCAF document after conversion. Retaining precise geometry for export or downstream operations therefore requires an additional owned result/sidecar design. Do not advertise precise-source retention merely because a transient `TopoDS_Shape` was tessellated.

## Validation and acceptance

JT tests must include known integer/topology/normal values; quantization bounds; all emitted indices in range; finite normalized normals; repeated geometry; nested transforms including mirrors; inherited appearance; LOD selection; external/missing partitions; and unsupported element diagnostics. Use independent JT2Go viewing under its own terms plus quantitative bounds/count/placement checks. A second decoder derived from the same code is not independent proof.

XT tests must distinguish transport parsing from valid B-Rep construction. Check source graph references/ownership, curve endpoints and pcurves, orientation, `BRepCheck_Analyzer`, and closure only for expected solids. Compare known dimensions, area, volume and sampled surface/edge distances against authored geometry or independently produced STEP/oracle results with a stated tolerance. Record healing changes and reject unbounded tolerance inflation.

Both routes require truncation/corrupt-count/invalid-reference tests, checked decompression and allocation limits, deep/cyclic graph cases, cancel/reload tests and cleanup on failure. Compare regular and progressive load results. Keep redistributable synthetic fixtures in tests; keep licensed/private sample data external.

Commit only after the implemented milestone's focused tests and relevant viewer checks pass, consistent with the user's existing instruction. This research pass has no production code changes and makes no render-validation claim.

## Unresolved decisions with explicit gates

- **JT corpus:** obtain representative actual files and select the first version/codec path; do not freeze an all-version promise first.
- **Native codec reuse:** finish dependency/provenance review and benchmark selected TKJT portions before vendoring.
- **Standalone XT permissions:** establish the applicable independent-implementation rights/patent position before distribution; this does not prevent continuing public-document research or bounded feasibility work.
- **XT schema coverage:** qualify exact keys needed by real models; the seven local nonempty samples are not advertised exact-profile matches for the inspected newer reader.
- **Exact geometry lifetime:** add retained source shapes/documents only when exact geometry is accepted as a product requirement.

Supporting research: [JT permissions and feasibility](JT_IMPORT_FEASIBILITY.md), [Parasolid evidence and limitations](../parasolid/PARASOLID_IMPORT_RESEARCH.md), local header inventory (`out/jt-research/JT_PARASOLID_SAMPLE_INVENTORY.json`, local artifact).

Current-version priority (20 October 2026): the 10.9 production path now passes native geometry/scene validation; stop at [Gate 2b](JT_109_PALETTE_VALIDATION.md) for owner testing, then qualify 10.10/10.11 before older formats. A95 provides a deferred 9.5 input; no 9.5/10.10/10.11 acceptance follows from its header inventory.
