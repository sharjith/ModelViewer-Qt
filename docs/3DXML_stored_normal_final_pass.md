# Stored normals: final focused research pass

## Owner visual acceptance

2026-10-18: the owner loaded the candidate-normal GLB in ModelViewer and
reported "The stored normals look fine", with a screenshot showing smooth
assembly shading. This accepts the candidate reconstruction for practical
rendering of this sample. It does not establish the exact exporter quantizer,
original unquantized values, or behavior on other 3DXML datasets. The known
normal/winding diagnostics remain recorded and must not be silently repaired.

## Owner-requested smooth-normal GLB comparison

The separate artifact
`out/3dxml-research/export/ASM-GETRAG_V160_stored_normals_candidate.glb`
uses the candidate pool reconstruction on nonconstant faces and normalized
stored constant-face normals on constant faces. No averaging, normal flipping,
winding changes or geometric-normal fallback is applied. This permits visual
assessment without promoting the quantizer to proven status.

Size: 297,468,052 bytes. SHA256:
`f0710d96c2a2eec265343722d23f5c1ae61a1faaaf4adc08cc7bf0edeb84c7f9`.
The owner-verified flat GLB (`ASM-GETRAG_V160_hierarchy_v2.glb`) is unchanged.
Both files retain 538 body/material primitive occurrences, 5,608,912 visible
triangles, the original 252 XML nodes plus 86 body nodes, the same local
transforms and 45 material colors, with hidden helper geometry omitted.

Validation compares the entire scene/material/geometry JSON and every
non-normal buffer byte against the flat baseline; only NORMAL accessor data
and its min/max metadata may differ. It also independently reconstructs the
candidate mapping from the raw source words/tags and checks every corner's
position, color/face binding and normal, including constant-face overrides.
Khronos validator `2.0.0-dev.3.10` reports zero errors, zero warnings and 217
informational messages (explicit identity matrices and visibility metadata
unsupported by that validator version). File-format validation and binding
checks do not establish exact source-normal quantization.

```powershell
python -I tools/3dxml/export_decoded_glb.py 'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml' --occurrences out/3dxml-research/scene_graph.json --output out/3dxml-research/export/ASM-GETRAG_V160_stored_normals_candidate.glb --stored-normal-candidate
python -I tools/3dxml/verify_exported_glb.py out/3dxml-research/export/ASM-GETRAG_V160_stored_normals_candidate.glb --occurrences out/3dxml-research/scene_graph.json --obj-manifest out/3dxml-research/export/ASM-GETRAG_V160_face_colors.manifest.json --flat-baseline out/3dxml-research/export/ASM-GETRAG_V160_hierarchy_v2.glb --stored-normal-source 'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml'
```

## Research findings

2026-10-18. This pass uses only the supplied archive, its XML metadata, our
sample-derived decoder, and mathematical comparisons. It does not use vendor
software, SDKs, private schemas, third-party binary decoders or decompilation.
No application code or owner-verified export was changed.

## Outcome

The dominant-axis interpretation remains strongly corroborated, but the exact
coefficient quantizer is still unresolved. Treat this as a useful optional
source-normal candidate, not an exact decoder or recovery of original floats.

The new result is that normal/winding disagreements cannot all be blamed on
the unknown quantizer. Seventy-two opposed corners use exact axis tags with
no coefficient words. They occur in `Linearfuehrung_D20.3DRep`, where their
dot product with the decoded triangle normal is approximately -0.542. A small
change in coefficient scaling cannot repair these cases.

Of 333 opposed pool-normal corners, 329 have dot < -0.01. These disagreements
are much larger than the 0.00554–0.01148 degree maximum direction differences
on the analytic pin/ring/sphere fixtures. That fixture range is geometric
evidence, not a universal error bound. The four nearly tangent cases remain
sensitive to tiny direction changes; the other 329 require another explanation.

## Quantizer investigation

For tags 0–5, two signed little-endian 16-bit words reconstruct the two
non-dominant components provisionally as `word/32767`. The omitted component
is the signed square root needed for unit length; `axis=tag//2`, with even
positive and odd negative. Tags 6–11 represent exact positive/negative axis
directions and consume no words. Constant face normals override the pool.

The final reproducer checks every normal record and every finest triangle.
It also compares the pin, ring and two spherical fixtures with integer scales
32760, 32763, 32764, 32765, 32766, 32767 and 32768, using both truncation and
nearest-integer quantization. None matches all coefficients. The source bytes
show magnitude residuals approaching three integer units against the analytic
reference times 32767, with a structured distribution rather than simple
nearest-integer noise. Exploratory comparisons of word-bit correlations and
fixed-step float truncation did not identify a reliable replacement formula.
No empirical coefficient offset is promoted into the decoder.

Analytic position-derived normals are independent geometric references, but
do not reveal the exact exporter inputs. More fitting against them cannot
alone prove an encoder's rounding, lookup-table or pre-quantization behavior.
An independent source-normal oracle would be needed to distinguish those
possibilities reliably.

## Whole-sample disagreement evidence

Across all 152 UVR members and 188 leaves:

* 2,627,746 normal records reconstruct as finite unit vectors.
* 11,485,434 triangle corners use the pool on nonconstant faces.
* 333 such corners oppose winding, across 112 triangles in ten mesh leaves.
* Opposed corners occur in lists (52), strips (59) and fans (222); they are
  not confined to one primitive expansion path.
* 72 opposed corners use exact axis tags and do not involve the quantizer.
* One stored constant-face normal also opposes a triangle, independently of
  the pool encoding.

Most opposed corners (216) are in the linear-guide representation. The
remaining members include housings, a bearing and detent parts. Source normals,
normal binding and source tessellation cannot yet be completely separated.
The observations do not authorize flipping normals or changing triangle
winding. Existing geometry/topology checks and owner validation remain valid
evidence of the rendered sample; they do not prove original smooth normals.

## Implementation decision

Following owner visual acceptance, use the parsed dominant-axis candidate
normals as the preferred shading path for this supported sample layout, with
stored constant-face normals taking precedence and normalized for rendering.
Retain derived flat normals as an explicit alternative and fallback for missing
or invalid normal data. Preserve raw words/tags, validate counts and references,
and report unsupported encodings rather than assuming the sample codec applies.
Do not flip valid source normals merely because they oppose triangle winding.
The exact quantizer remains unproven; do not describe the original floats as
recovered. Further coefficient fitting need not block the mesh importer.

Detailed measured results, per-member counts, all 333 corner diagnostics and
fixture/model comparisons are in `3DXML_stored_normal_limits.json`.

```powershell
python -I tools/3dxml/analyze_stored_normal_limits.py 'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml' --report docs/3DXML_stored_normal_limits.json
```
