# Native 3DXML importer implementation plan

## Scope and architectural choice

Implement the owner-validated tessellated UVR layout directly in C++, producing
an owned in-memory `aiScene`. Reuse the existing loader, mesh conversion,
scene graph and rendering pipeline. No Python runtime, temporary OBJ/GLB,
vendor SDK or OCC B-Rep reconstruction is needed. Other 3DXML encodings must
produce explicit unsupported-layout diagnostics, not an apparent success.

The supplied sample has owner validation for geometry, hierarchy, local/world
placements, default visibility, face colors and candidate stored-normal shading.
Exact normal quantization and full source material semantics remain unproven.

## Proposed modules

Use namespace `ThreeDXml`, with headers under `include/Import/ThreeDXml` and
sources under `src/Import/ThreeDXml`:

| Class / data type | Responsibility |
|---|---|
| `Importer` | Entry point; cancellation/progress, diagnostics, returns owned scene and import metadata. |
| `ArchiveReader` | Bounded ZIP-member access, manifest resolution, safe member paths and decompression limits. ZIP implementation isolated here; reuse a suitable declared dependency or add a direct portable dependency after checking the installed build. Avoid private Qt APIs. |
| `ProductStructureReader` | Qt streaming XML parser; reference/representation tables, occurrence links, local matrices and visibility overrides. |
| `RepresentationReader` | Port compound catalogs, fragmented logical streams, complete Skeleton graph/cache traversal, finest primitives, face styles and normal decoding. |
| `SceneBuilder` | Cache representation/body mesh resources, build materials and indexed `aiMesh` objects, expand XML occurrences into uniquely parented `aiNode`s. |
| `Document` / `ImportResult` | Value types for references, occurrences, mesh resources, source IDs, normal policy, units and visibility mappings; `ImportResult` owns its `aiScene` explicitly. |

Small checked `ByteReader` / `PackedBitReader` helpers belong inside the binary
reader module. Packed-bit state must survive intervening raw/count/float reads.
Do not introduce a new general importer framework or extend the XCAF factory
for a tessellated archive format.

## Integration

Existing flow:
`ModelViewer::loadFile -> ViewportWidget::loadAssImpModel -> AssImpModelLoader::loadModel`.
Preserve the existing XCAF-first dispatch sequence: try the XCAF processor
factory first, then route `.3dxml` to the new importer, and otherwise use
Assimp file reading. This ordering follows the owner's explicit preference.
The new importer returns
an in-memory scene to the existing post-read processing and DFS mesh traversal.
Keep cancellation, progressive loading, UV decisions, error reporting and scene
merge behavior. Add the extension to all open/import/support filters, including
the application-level and document-level lists and extension checks used by
drag-and-drop/recent-file entry points.

The custom scene needs explicit lifetime ownership in the worker, distinct from
`Assimp::Importer`-owned scenes. Keep it alive through mesh conversion, graph
construction and the existing scene copy/merge. Reset it on cancellation,
subsequent loads and destruction; never pass it to `Importer::FreeScene()`.
Do not run normal generation, infacing-normal fixes or graph-flattening passes
over the decoded source mesh data.

## Scene graph and visibility

Build the original XML root and 251 occurrence nodes, copying names and local
matrices. Expand reference definitions into occurrences; share mesh resources,
not `aiNode` objects (each occurrence has one parent). Multibody parts receive
identity body children; material primitives attach to their owning body.

Keep positions local. Compose `world = parentWorld * local`; never bake XML
world placements into positions. Reuse `SceneGraph::appendFromScene` to create
the application tree and bind mesh UUIDs in exactly the loader's DFS order.
Maintain source reference IDs and occurrence paths as stable import metadata;
duplicate display names are not identifiers. Keep all geometry-less nodes.

Unlike the GLB inspection workaround, retain hidden helper geometry in native
import so the owner can reveal it. Propagate effective XML visibility through
ordinary import/mesh visibility metadata, apply it before progressive meshes
first display, and preserve it when tree/UI visibility is initialized. Do not
route static visibility through the glTF animation system. Check persistence,
Show All and assembly checkbox behavior using the existing visibility model.

Native source XYZ is Z-up for this sample; use the existing orientation policy
once, without GLB's Y-up compensation. Record millimetres as an inferred sample
unit, not a universal 3DXML fact. Respect explicit source units when supported
and user unit overrides. Keep unit interpretation separate from display fitting.

## Meshes, normals and colors

Decode each representation once. Preserve all body occurrences, finest stored
triangles and per-face BGR-to-RGB material bindings. Group primitives by body
and material while retaining face-packet ranges/identities. Emit indexed vertex
buffers; split vertices at differing normal bindings, including constant-face
overrides. Do not weld by position alone or average across sharp boundaries.

Prefer the visually accepted dominant-axis normal candidate with normalized
constant-face overrides. Retain flat shading as an alternative and fallback
for missing/invalid normals; unsupported record layouts remain diagnostic
failures. Do not silently flip valid source normals or triangle winding.
Initial diffuse colors/roughness match the validated GLB numeric policy.
Full analytic CAD surfaces, textures and comprehensive materials are deferred.

Share source `aiMesh` resources across repeated occurrences. The current runtime
creates `SceneMesh` data per occurrence: source caching does not by itself
provide shared GPU buffers or hardware instancing. Avoid expanding scope into
a renderer memory architecture change; measure peak memory on the sample.

## Progress and acceptance

Distinguish archive/representation decoding, unique mesh resources and placed
mesh processing. Use total node-to-mesh references for the traversal progress
denominator, correcting the existing unique-resource/occurrence mismatch.
Hidden occurrences count as work even though they do not count as visible
objects; the viewport's visible count should remain 538 for the sample.

Implement in reviewable stages:

1. Archive/XML reader and checked binary primitives; synthetic nested transform,
   reference reuse, fragmented stream and persistent bit-reservoir fixtures.
2. Complete representation decoder and mesh builder; compare source bytes and
   decoded results against the Python reference, including multiple bodies,
   unequal position/normal counts, indices, face styles and normal overrides.
3. In-memory scene integration, filters, scene lifetime, visibility, units and
   progress. Keep existing OBJ/glTF/STEP routes working.
4. Native sample acceptance and targeted regressions: names/parent links, every
   local/world transform, 152 UVR entries, 188 source body leaves, 4,152,382
   resource triangles including hidden helpers, 276 visible body occurrences,
   538 visible material splits and 5,608,912 visible triangles. Compare visible
   bounds and normal/color bindings with the validated GLB.
5. Exercise cancellation, malformed/truncated archives and packed data,
   missing members/references, unsupported layouts, nested visibility, memory
   use, save/reload and hierarchy-aware export. Perform the owner visual check
   on direct `.3dxml` loading before calling the native importer accepted.

Small synthetic decoder fixtures can be tracked. Keep the supplied archive and
large source-derived geometry outside version control. Record reproducible
sample validation separately. Implementation work is not started by this plan.
