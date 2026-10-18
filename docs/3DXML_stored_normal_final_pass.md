# Stored normals: final focused research pass

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

Retain derived flat normals as the owner-verified default. Preserve raw normal
words/tags and constant-face overrides so an experimental source-smooth path
can be added without reparsing or discarding evidence. Keep that option
explicitly labeled until an independent comparison validates the mapping.
Do not block the complete mesh importer on further coefficient fitting.

Detailed measured results, per-member counts, all 333 corner diagnostics and
fixture/model comparisons are in `3DXML_stored_normal_limits.json`.

```powershell
python -I tools/3dxml/analyze_stored_normal_limits.py 'D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml' --report docs/3DXML_stored_normal_limits.json
```
