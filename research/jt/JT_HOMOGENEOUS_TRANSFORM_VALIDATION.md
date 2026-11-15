# JT affine homogeneous transform normalization

15 November 2026, `dev`. Implementation prepared and the owner confirmed that the authentic JT9.5 assembly opens in the installed viewer. Native build, registered CTest and authentic probe remain unreported; successful loading alone does not independently verify exact occurrence placement.

## Evidence and scope

Neutral control T95-H1 is an owner-supplied private JT9.5 LE assembly, 79,415 bytes, SHA-256 `18833248421dac2fcaa7eefe1d0e0e062fd3c9a9fee62c017f5bc3523f2bc5c6`. Read-only container inspection found 38 segments: one LSG, thirteen type-4 segments, eighteen display-shape LOD segments and six XT segments. Its graph contains nine geometric-transform attributes, six parts, eighteen triangle-strip nodes and six RangeLOD nodes.

Eight transforms have the ordinary homogeneous row `(0, 0, 0, 1)`. The ninth has `(0, 0, 0, 0.9999999403953552)`, with otherwise finite affine rotation/translation values. The last value is the exact F64 representation of the preceding F32 value below one. The previous adapter compared it with one using only 64 double-precision epsilons and rejected the whole assembly as projective even though all three perspective coefficients are exactly zero.

The retained JT9.5 public transform definition and existing JT8/9/10 readers were consulted; no new reference, vendor SDK, decoder, disassembly or exporter was used. The normalization decision is standard homogeneous-coordinate mathematics applied in the ModelViewer scene adapter, not a claim about the producing application or universal JT conformance. The private sample, extracted matrices and logs remain outside Git or under ignored `out/`.

Reproduce the neutral container inventory:

```text
python -B tools/jt/inspect_jt_structure.py <local-samples>/T95-H1.jt --output out/jt-research/T95-H1.structure.json
```

## Implementation contract

Every enabled family already reaches the same scene adapter after its binary transform reader (JT8 uses F32 components; JT9/JT10 use F64). The adapter now:

1. requires all sixteen source components to be finite;
2. requires the bottom-right homogeneous scale `w` to be nonzero and have a finite reciprocal;
3. divides every retained affine component and the three perspective-row components by `w`;
4. accepts only negligible normalized perspective-row residue, preserving the existing strict rejection of actual perspective transforms;
5. rejects normalized values that overflow or underflow the renderer's F32 matrix representation.

Dividing a homogeneous matrix by nonzero `w` is exact projective-coordinate equivalence. It correctly handles both a globally scaled matrix and T95-H1's producer rounding without inventing a format-specific tolerance. It does not add projective rendering.

Original in-memory scene tests cover a globally scaled affine matrix, the exact F32 predecessor of one observed in T95-H1, retained projective rejection, zero scale and reciprocal overflow. Existing binary fixtures continue to cover JT8 F32 and JT9/JT10 F64 transform transport; those fixtures plus the shared adapter test establish code-path coverage, not authentic exporter evidence for every version.

## Gate status

Owner GUI result, 15 November 2026: T95-H1 opens after homogeneous normalization. Detailed placement comparison with the source CAD or another qualified viewer was not reported and remains unclaimed.

The remaining native gate is:

The owner-authorized build gate, when requested, is:

```text
cmake --build out/jt102-build --target jt_tests jt_probe --parallel 2
ctest --test-dir out/jt102-build --output-on-failure
out/jt102-build/jt_probe.exe <local-samples>/T95-H1.jt --validate
```

The commands above have not been reported as run for this change.
