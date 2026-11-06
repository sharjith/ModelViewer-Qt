# Native 3DXML importer

The C++ importer implements the supplied archives' researched binary UVR and
XML TESSELLATED geometry layouts. XCAF dispatch is preserved first, followed by native `.3dxml`,
then ordinary Assimp formats. No intermediate GLB/OBJ, Python runtime, vendor
SDK or B-Rep conversion is involved at runtime.

The owner has now confirmed that the original GETRAG archive loads directly
and looks perfect in ModelViewer. Four additional archives now pass native import and independent triangle-count
checks; their visual acceptance is pending. See [additional layouts and validation](3DXML_additional_layouts.md).
Measured sample coverage is not universal format coverage.

ZIP packages and standalone `Model_3dxml` assembly documents are supported.
Loose documents resolve companion files within their containing directory.
Material/image support documents alone are not renderable assemblies; see
[loose XML loading and validation](3DXML_loose_xml.md).

## Code and integration

`include/Import/ThreeDXml` and `src/Import/ThreeDXml` contain `Importer`,
`ArchiveReader`, `ProductStructureReader`, `RepresentationReader`, `TessellatedReader`, `SceneBuilder`
and their value types. ZIP access uses a declared minizip dependency; XML uses
QtCore. `PackedReader` preserves its packed-bit reservoir across raw fields.

The importer owns its `aiScene` independently of `Assimp::Importer`. Existing
worker-thread loading, mesh conversion, progressive upload, scene copying and
`SceneGraph::appendFromScene` are reused. Mesh resources are shared at the
source scene level; the current renderer still creates mesh data per occurrence.
No GPU-instancing architecture change is included.

The scene retains all original XML nodes and local matrices. Native positions
stay in source XYZ, with the supported sample's Z-up convention. Multibody
representations get identity body children. Indexed vertices are split at
normal-binding boundaries and grouped by body/material; no coordinate welding,
normal averaging or winding correction occurs. Exact zero-area source triangles
are omitted with a diagnostic.

Candidate dominant-axis pool normals are used with normalized constant-face
overrides. `Options::parsedNormals=false` selects derived flat normals for
programmatic comparison; the existing viewport flat-shading mode also remains
available. Invalid constant normals can fall back to a triangle normal; unknown
encodings or missing referenced pool records are rejected. Exact exporter
quantization is still unproven, despite visual acceptance for the sample.

Default-view occurrence RGBA colors now override mesh colors at their addressed
product paths; descendants inherit the nearest override. Unoverridden occurrences
retain source face colors. Material variants are cached per source mesh/binding,
so recoloring one instance cannot alter another. See
[occurrence-color validation](3DXML_occurrence_colors.md).

The 45 GETRAG BGR face colors use the same numeric RGB/255 and roughness policy as the
accepted GLB. Full source material properties, textures, analytic CAD surfaces,
source curves/points and source cameras are outside this implementation.
TECHREP review attachments are skipped with diagnostics. The measured
annotation-only V4V5_FDT container is accepted without annotation rendering.

## Visibility, units and progress

Hidden geometry is retained, unlike the GLB inspection workaround. Effective
XML visibility travels with each imported mesh and is applied before its first
progressive display; the ordinary visibility/tree state handles it thereafter.
No animation clips or glTF visibility extension are required. Native archive
imports roll back their partial progressive meshes on failure/cancellation.
Interactive cancellation/visibility behavior still needs an owner GUI check.

For the supplied layout, millimetres are explicitly marked as the inferred
file unit and a warning records that the source has no declared units. This
is a sample-backed assumption, not a general format guarantee; the normal
import-unit controls can override it. No GLB metre conversion is applied.

Traversal progress now counts node-to-mesh references rather than distinct
mesh resources, including for existing instanced GLB files. Source statistics
still count unique resources, while bounds include every placement. For native
loading of this sample there are 553 total mesh occurrences and 538 initially
visible occurrences; progress can correctly finish at 553/553 while the visible
mesh counter shows 538.

## Validation

The independent GUI-free `threedxml_tests` executable builds with QtCore,
Assimp and minizip. Synthetic checks cover nested noncommuting transforms,
shared-resource occurrence counts, persistent bit state, XML errors/duplicate
IDs/DTD rejection, ZIP CRC errors/duplicate members/missing members, Unicode
archive filenames, product cycles and cancellation. Source reads are bounded;
compressed UVR extents and unknown layouts fail with diagnostics.

The complete native sample audit checks every visible primitive's expanded
triangle positions exactly and normals within 1e-6 against the owner-accepted
candidate-normal GLB. It also checks material base-color values, original XML
names, parent links, local/world transforms and initial visibility. Results:

| Quantity | Native result |
|---|---:|
| Original XML nodes | 252 |
| Total nodes including body children | 338 |
| Hidden XML occurrences | 17 |
| Unique mesh/material resources including hidden helpers | 388 |
| Total placed mesh/material occurrences | 553 |
| Visible bodies | 276 |
| Visible mesh/material occurrences | 538 |
| Unique-resource triangles including hidden geometry | 4,152,382 |
| Visible placed triangles | 5,608,912 |
| Compared visible GLB primitives | 385 |
| Materials | 45 |

Machine-readable results: `out/3dxml-research/3DXML_native_import_validation.json`.
The source archive and generated geometry remain outside version control.
The archive's SHA256 is
`6f95b1a5bd3e5a51db842eef3793ed492c2aa3db5a00c8b37ac33434b79b11b9`.

Standalone build (with the dependency installation in `CMAKE_PREFIX_PATH`):

```powershell
cmake -S tests/3dxml -B out/3dxml-native-build -DCMAKE_BUILD_TYPE=Release
cmake --build out/3dxml-native-build
ctest --test-dir out/3dxml-native-build --output-on-failure
out/3dxml-native-build/threedxml_tests.exe 'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml' out/3dxml-research/export/ASM-GETRAG_V160_stored_normals_candidate.glb out/3dxml-research/scene_graph.json out/3dxml-research/native-import-audit.json
```

The target is also registered under the application's `MV_BUILD_TESTS` option.
Only the supplied layout has whole-file validation; additional 3DXML variants
need their own fixtures. A direct native load, Show All/hide, cancellation in
the GUI, session save/reload and hierarchy-aware export remain the final
interactive acceptance checks. A successful core audit is not GUI acceptance.
