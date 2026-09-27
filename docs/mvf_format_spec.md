# MVF Format Specification

## Overview

`.mvf` is the native session format for ModelViewer.

The current implementation is:

- self-contained
- glTF-inspired in structure
- chunked and binary
- scene-oriented rather than runtime-dump-oriented
- the default and only supported native session storage method

The file stores:

- scene hierarchy
- geometry
- materials
- textures and embedded image payloads when available
- mesh/session identity
- visibility and selection session state

The implementation is based conceptually on the glTF 2.0 scene model:

- scenes contain nodes
- nodes carry transforms and mesh bindings
- meshes contain primitives with typed attribute streams
- materials reference textures
- textures reference images and samplers
- buffers / bufferViews / accessors describe binary payloads

Reference model:

- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)

## Design Goals

The implemented MVF format is intended to:

1. Preserve imported geometry as part of the scene
2. Reopen without depending on the original imported file
3. Avoid serializing transient OpenGL/runtime state directly
4. Store hierarchy and session state alongside assets
5. Rebuild runtime meshes from saved asset data on load

## Format Identity

Extension:

- `.mvf`

Magic:

- `MVF3`

Endianness:

- little-endian

Container style:

- GLB-inspired chunked binary container

## Container Layout

Each `.mvf` file contains:

1. fixed header
2. JSON chunk
3. `GEOM` chunk
4. `IMGS` chunk
5. optional future chunks

Current chunk types:

- `JSON`
- `GEOM`
- `IMGS`
- `AUX0`

### Header

Implemented shape:

```c
struct MvfHeader
{
    uint32_t magic;       // "MVF3"
    uint32_t version;     // current format version
    uint32_t fileLength;  // total bytes
    uint32_t flags;       // reserved
};
```

### Chunk Header

Implemented shape:

```c
struct MvfChunkHeader
{
    uint32_t chunkLength;
    uint32_t chunkType;
};
```

## Top-Level JSON Model

The JSON chunk follows a glTF-like top-level structure.

Current top-level fields:

- `asset`
- `scene`
- `scenes`
- `nodes`
- `meshes`
- `materials`
- `textures`
- `images`
- `samplers`
- `buffers`
- `bufferViews`
- `accessors`
- `extensionsUsed`
- `extensionsRequired`
- `mvfSession`

This is implemented in:

- [MvfDocument.h](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/include/MvfDocument.h)
- [MvfDocument.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfDocument.cpp)

## Scene Graph Model

MVF stores a real scene hierarchy and restores it directly on load.

The implementation is aligned with:

- [SceneGraph.h](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/include/SceneGraph.h)
- [SceneNode.h](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/include/SceneNode.h)

### Scenes

`scenes[]` stores scene root node indices.

`scene` stores the active/default scene index.

### Nodes

Each node currently stores:

- `id`
- `name`
- `matrix`
- `children`
- `meshBindings`

### Mesh Bindings

MVF differs intentionally from plain glTF here.

Instead of forcing one node to reference one mesh, a node can store multiple `meshBindings`, because ModelViewer’s scene graph already tracks multiple mesh UUIDs under one structural node.

Each mesh binding currently stores:

- `uuid`
- `mesh`
- `materialOverride`
- `visible`

This is what allows:

- node hierarchy restore
- stable mesh identity
- visibility/session restore

Hierarchy restore on load is implemented in:

- [SceneGraph.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/SceneGraph.cpp)

via `rebuildFromMvf(...)`.

## Geometry Model

Geometry is stored as typed attribute streams, not as direct runtime object dumps.

This is the core architectural shift from the older MVF approach.

### Meshes

Each `mesh` stores:

- `id`
- `name`
- `primitives`

### Primitives

Each primitive stores:

- `attributes`
- `indices`
- `material`
- `mode`
- `extras`

### Supported Attributes

The current writer emits these attributes when present:

- `POSITION`
- `NORMAL`
- `TANGENT`
- `TEXCOORD_0`
- `TEXCOORD_1`
- `TEXCOORD_2`
- `TEXCOORD_3`
- `COLOR_0`

Index data is written separately as `indices`.

Geometry packing is implemented in:

- [MvfSceneBuilder.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfSceneBuilder.cpp)

Current packing source:

- live `AssImpMesh` vertex/index data

### Buffer Model

Geometry uses a glTF-like indirection model:

- `buffers`
- `bufferViews`
- `accessors`

Current buffer usage:

- buffer `0` -> `GEOM`
- buffer `1` -> `IMGS`

This avoids coupling the format to C++ runtime layout and lets the loader reconstruct vertices from typed streams.

## Material Model

Materials are stored as a glTF-like core plus ModelViewer-specific extensions.

### Core Material Fields

Current material JSON includes:

- `id`
- `name`
- `shadingModel`
- `blendMode`
- `doubleSided`
- `alphaCutoff`
- `opacity`
- `pbr`

Current `pbr` block includes at least:

- `baseColorFactor`
- `metallicFactor`
- `roughnessFactor`

### Implemented MVF Extensions

Current implementation writes:

- `MVF_material_ads`
- `MVF_material_pbr`

`MVF_material_ads` currently carries:

- ambient
- diffuse
- specular
- emissive
- shininess

`MVF_material_pbr` currently carries:

- ior
- transmission
- clearcoat
- clearcoatRoughness
- sheenColor
- sheenRoughness
- texture references for supported material slots

Material reconstruction on load is implemented in:

- [GLWidget.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/GLWidget.cpp)

via `reconstructMvfMaterial(...)`.

## Simulation Results

A document that contains simulation results (see `simulation_results_design.md`) stores them in
`mvfSession.simulationResults`, one entry per result mesh. Older builds ignore the key and open the mesh as an
ordinary mesh (its `COLOR_0` still shows the colours the result had when saved). Design and rationale:
`simulation_mvf_persistence_design.md`.

The result mesh itself is an ordinary mesh (the visible boundary surface, possibly in its deformed pose). Two
things are added for results:

- **`COLOR_0`** holds the colours the result was displayed with (colormap and contour bands applied), so other
  viewers show the coloured result. The app replaces it on load with its own live overlay.
- **The snapshot**, an entry of `simulationResults`:

```json
{
  "meshUuid": "...",
  "content": "shown" | "all",
  "blobViews": [12, 13, 14],
  "snapshot": {
    "version": 1,
    "vertexCount": 2646, "triangleCount": 5292,
    "solver": "", "lengthUnit": "", "sourcePath": "...",
    "restPositions": 0, "nodeIds": 1,
    "steps":  [ { "time": 0.5, "label": "", "timeUnit": "", "src": 0 } ],
    "fields": [ { "name": "DISP", "components": 3, "componentNames": ["D1","D2","D3"],
                  "kind": "length", "fileUnit": "mm", "displayUnit": "mm", "unitConfirmed": false,
                  "stepData": [2, 3, -1] } ],
    "ranges": [ { "name": "DISP", "data": [lo, hi, ...] } ],
    "view":   { "field": "STRESS von Mises", "component": -1, "customRange": false, "step": 0,
                "colormap": 0, "bands": 0, "allStepsRange": true, "deform": false, "deformScale": 1,
                "markExtrema": false },
    "blobs":  [ { "i": 0, "n": 7938, "elem": 4, "enc": "shuffle-zlib" } ]
  }
}
```

- **Per surface vertex, not per solver node.** Every array has one entry per vertex of the mesh (in the mesh's
  vertex order, which MVF preserves). The volume is not stored.
- **`blobViews`** lists `bufferViews` (buffer 0, the `GEOM` chunk); `snapshot.blobs[i]` describes the blob stored in
  `bufferViews[blobViews[i]]`. `restPositions` (VEC3) and `nodeIds` (int64, the solver's own node ids) are blob
  indices, as is every non-negative entry of a field's `stepData` (one blob per kept step; -1 = no data at that
  step). Field values are float32 with the components of a tuple interleaved; NaN marks "no value at this node".
- **Blob encoding** `raw` or `shuffle-zlib`: the bytes of all elements are regrouped into planes (all first bytes,
  then all second bytes, ...) and deflated with zlib (Qt's `qCompress`, i.e. a 4-byte big-endian length, then the
  zlib stream). Lossless. A blob that would not shrink is stored `raw`; blobs under 512 bytes always are.
- **Derived fields** (von Mises, principal stresses, max shear of a stored stress tensor) are not stored; they are
  rebuilt on load. Their units are those of the source field.
- **`ranges`** holds, per stored or derived field and kept step, the min and max over ALL solver nodes for each
  selector (each component, plus the magnitude for a 3-component field; `null` = unknown), in file units. It keeps
  the legend range identical to that of the full result although interior nodes are not stored.
- **`steps[].src`** is the step's index in the original result; more than 100 steps are subsampled evenly (first and
  last kept). `view.step` indexes the kept steps.
- **`content`**: `shown` stores the field on display (its source tensor for a derived one) plus the displacement
  field, `all` every source field. A "geometry only" save writes no `simulationResults` at all.
- **Validation on load:** a different vertex or triangle count than at save time (the mesh was edited), a newer
  `version`, a missing or damaged blob, or a field that does not match the steps makes that result fail to restore.
  The user is told, and the mesh stays as a plain coloured surface. Nothing is shown from mismatched data.

Implemented in `ResultSnapshot.h/.cpp` (codec, unit-tested in `tests/result_tests.cpp`),
`ModelViewerSimulation.cpp` (`appendSimulationSnapshots`, `simulationBakedColors`, `promptSimulationSaveOptions`,
`restoreSimulationSessions`), and the `colorOverrides` parameter of `Mvf::buildMVFPackage`.

## Texture, Image, And Sampler Model

MVF follows the glTF-style split:

- `textures` reference `images` and `samplers`
- `images` reference embedded payload through `bufferView` or retain source-uri metadata
- `samplers` define wrapping and filtering

### Images

Current image records include:

- `name`
- `originalUri`
- `bufferView`
- `mimeType`
- `byteLength`

The authoritative embedded image payload is stored in the `IMGS` chunk when the writer can resolve and read the image source.

### Image Embedding Behavior

Current writer behavior:

- external images are read and appended to `IMGS`
- `glb://...` image sources are resolved through `TextureLocationManager`
- resolved image bytes are embedded into `IMGS`
- corresponding image `bufferView`, `byteLength`, and `mimeType` are updated
- `.ktx2` files are currently skipped and remain path-based

This is implemented in:

- [MvfSceneBuilder.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfSceneBuilder.cpp)

### Texture Deduplication

Current writer deduplicates:

- images by resolved/original path key
- samplers by wrap/filter signature
- textures by `(image, sampler)` signature

## Session State

Session-specific state lives in `mvfSession`.

Current stored session state includes:

- `visibleMeshUuids`
- `selectedMeshUuids`
- `geometryChunkPresent`
- `imageChunkPresent`

Visibility and selection are stored using mesh UUIDs, not UI row indices.

This aligns with the current viewer architecture:

- [ModelViewer.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/ModelViewer.cpp)
- [SceneGraph.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/SceneGraph.cpp)

## Save Pipeline

The current save pipeline is:

1. Build a full `Mvf::MVFPackage` from current scene state
2. Serialize JSON metadata
3. Serialize packed geometry chunk
4. Serialize packed image chunk
5. Write header + chunks

This is implemented through:

- [ModelViewer.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/ModelViewer.cpp)
- [MvfSceneBuilder.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfSceneBuilder.cpp)
- [MvfFormat.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfFormat.cpp)

## Load Pipeline

The current load path is worker-thread based and deliberately mirrors the modern AssImp load architecture.

Implemented load phases:

1. Read header and chunks on a worker thread
2. Parse JSON into `Mvf::Document`
3. Prepare CPU-side mesh/material data from JSON + `GEOM` + `IMGS`
4. Upload meshes one-by-one on the UI thread via blocking queued calls
5. Restore visibility and selection
6. Rebuild `SceneGraph` from saved node data

This is implemented in:

- [ModelViewer.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/ModelViewer.cpp)
- [GLWidget.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/GLWidget.cpp)

### Mesh Reconstruction

`GLWidget` reconstructs runtime meshes by:

- reading typed attribute streams from accessors/bufferViews
- rebuilding `Vertex` arrays
- rebuilding materials from material JSON
- resolving embedded images to temporary files when needed
- reusing the normal runtime texture-resolution path afterward

Key load helpers include:

- `prepareMvfMeshes(...)`
- `uploadOneMvfMesh(...)`
- `reconstructMvfMaterial(...)`

## What MVF Does Not Store

MVF does not store transient runtime state such as:

- OpenGL object ids
- shader program handles
- renderer caches
- temporary extracted image file locations

These are reconstructed on load.

## Current Limitations

The current implementation still has some intentional limits:

- `ktx2` payloads are not embedded into the `IMGS` chunk
- chunk compression is not implemented yet
- the format is glTF-inspired, but not glTF-compatible byte-for-byte
- material/session extensibility is present, but not yet exhaustive for every future ModelViewer feature

## Relationship To Code

The key code areas are:

- container I/O:
  - [MvfFormat.h](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/include/MvfFormat.h)
  - [MvfFormat.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfFormat.cpp)
- document model:
  - [MvfDocument.h](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/include/MvfDocument.h)
  - [MvfDocument.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfDocument.cpp)
- writer/packer:
  - [MvfSceneBuilder.h](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/include/MvfSceneBuilder.h)
  - [MvfSceneBuilder.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/MvfSceneBuilder.cpp)
- load/reconstruction:
  - [ModelViewer.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/ModelViewer.cpp)
  - [GLWidget.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/GLWidget.cpp)
  - [SceneGraph.cpp](/D:/work/progs/Qt6/vcpkg/ModelViewer-Qt/src/SceneGraph.cpp)

## Summary

The implemented `.mvf` format is:

- a self-contained native ModelViewer session container
- structurally inspired by glTF 2.0
- based on scenes, nodes, mesh primitives, materials, textures, images, and accessors
- rebuilt into runtime OpenGL objects on load rather than storing runtime objects directly

That means imported geometry now truly becomes part of the saved ModelViewer scene, while hierarchy, material state, visibility, and selection round-trip through the native format. 

