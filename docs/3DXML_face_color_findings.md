# Face colors and the corrected inspection export

2026-10-18. The owner loaded the first OBJ successfully, then supplied screenshots
of the original GrabCAD preview and the exported assembly in ModelViewer. The
first OBJ used two deliberately assigned neutral inspection materials; those
gray/teal colors were not decoded source face colors. The geometry decoder had
already consumed the face-style array, but the inspection exporter discarded it.

The new pass uses only the supplied binary/XML bytes and the owner-provided
images for visual corroboration. No website, external decoder, vendor code or
reference-viewer software was accessed. No application code or build changed.

## Binding and byte order

Immediately after the finest-face primitive packets, the stream has exactly
one eight-byte style record per finest face packet. Assign the style to every
triangle expanded from that packet's lists, strips and fans. A single packet
can contain many triangles. A body/part need not have one uniform color.

Across all 188 mesh definitions:

- 60,908 face style records reconcile with their face packets.
- 60,904 begin `30 04 04 FF FF`; four begin `B0 04 04 FF 7F`.
- The final three bytes encode 45 distinct colors.
- 100 mesh definitions contain more than one such color.

The color-byte ordering is sample-corroborated **BGR**, rather than RGB:

| Stored bytes | Decoded RGB | Independent visual corroboration |
|---|---|---|
| `FF D2 D2` | `D2 D2 FF` | Light lavender main housing surfaces in the owner's reference |
| `FF FF 00` | `00 FF FF` | Cyan linkage in the reference |
| `00 FF 00` | `00 FF 00` | Green localized faces, including housing bores; invariant under channel reversal |

This is a channel-order interpretation supported by the supplied sample and
rendered reference, not a universal version/exporter guarantee. The measured
root XML has no color/material/surface-attribute overrides. Full graph-level
material inheritance, textures, transparency flags, physical shading parameters
and color-management behavior are not claimed decoded by this pass. In
particular, the four alternate-prefix records are recorded without inventing
their transparency/material semantics; the inspection materials are opaque.

`3DXML_face_style_analysis.json` preserves per-mesh face and triangle color
histograms in stored BGR and decoded RGB order. Source mesh definition totals
remain 4,152,382 triangles. The default-visible, occurrence-expanded OBJ remains
5,608,912 triangles in 276 named body objects, using 45 source-color materials.

## Export and validation

The corrected artifact is
`out/3dxml-research/export/ASM-GETRAG_V160_face_colors.obj` with a same-named
MTL, manifest and verification report. Keep OBJ and MTL together. The old
`ASM-GETRAG_V160_decoded.obj` and its two-color MTL remain unchanged.

The exporter now preserves the decoded face-style array and writes `usemtl`
at face-packet material boundaries. Materials are deduplicated by decoded
RGB, with `Kd=RGB/255`; it does not create a material for every triangle or
interpolate colors across differently colored faces. Derived flat normals,
source triangle ordering, placements, visibility and ModelViewer OBJ basis
compensation remain the same as in the owner-validated export.

The independent verifier checks every face against its expected face-packet
material run and validates MTL diffuse values. It also compares the entire
sequence of `v`, `vn` and `f` records with the old export using SHA-256. Those
geometry records were verified byte-identical: this is a color-only change.
Their shared geometry-record SHA-256 is
`2174dc1b6f48b8256edbf01a5d690a998a1a3942dd81ab163cd4233681f0746f`.
All 5,608,912 triangle material bindings and all 45 diffuse colors passed.

```powershell
python -I tools/3dxml/analyze_face_styles.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --report docs/3DXML_face_style_analysis.json
python -I tools/3dxml/export_decoded_assembly.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --occurrences out/3dxml-research/scene_graph.json --output out/3dxml-research/export/ASM-GETRAG_V160_face_colors.obj --face-colors
python -I tools/3dxml/verify_exported_obj.py out/3dxml-research/export/ASM-GETRAG_V160_face_colors.obj --geometry-baseline out/3dxml-research/export/ASM-GETRAG_V160_decoded.obj
python -I tools/3dxml/render_assembly_preview.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --occurrences out/3dxml-research/scene_graph.json --output-directory out/3dxml-research --face-colors
```

The generated preview is `out/3dxml-research/face_colors_assembly.png`, using
the same decoded geometry and derived normals with the source face colors.
It is still a simplified research renderer. Lighting, source materials and
the reference viewer's black edge display are separate from diffuse face
colors. The OBJ export does not add source edge polylines, and it still
flattens the hierarchy into named body objects.

For native integration, preserve logical assembly/part nodes and split or
batch each leaf's triangles by effective face material underneath that node.
Reused representations should share those material-specific mesh resources.
Do not attach a single part-wide diffuse color when its face packets differ.
