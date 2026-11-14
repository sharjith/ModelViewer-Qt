# JT 9.5 production texture profile

14 November 2026, `dev`. This production compatibility gate uses three
owner-supplied JT 9.5 variants, identified here as **P95-A**, **P95-B** and
**P95-C**. Their original names, paths, geometry and sample-derived payload
bytes remain private and untracked.

| ID | Size (bytes) | SHA-256 |
|---|---:|---|
| P95-A | 54,628,360 | `1892349e0097abdd864c0ca95fd1644bdfdddf37e5e1311c88bf99a6271d6ef4` |
| P95-B | 54,554,146 | `668c7672fff0004bc6eff386ca9b0a1990f27abc6462a5362c28ee7fe268e11d` |
| P95-C | 55,865,564 | `098cb58c4d36403e889dfff24a7949e8e83b3cbc59474c5d2b009c92cf7f1fec` |

## Failures and measured profile

The files initially stopped at `Invalid JT texture image storage/count`.
Read-only byte inspection showed nominal JT 9.x U8 texture storage and shared
flags written in aligned U32 slots. The documented U8 layout remains primary;
the alternate layout is accepted only when the U8 interpretation is impossible
and all alternate fields validate.

After that correction, the importer reached lossless vertex colours and UVs.
The retained Siemens JT 9.5 figures describe one exponent packet and one
mantissa packet per component, followed by a combined hash. These samples
instead have a separator U32 after every exponent/mantissa pair. For every
non-final component, that separator is followed by the exact next CDP2 value
count. This repeated count makes the alternate envelope structurally
distinguishable from the published combined-hash layout.

The same producer repeats some already-set exponent bits in the nominal
mantissa word. Reconstruction by bitwise OR produces finite colour values in
`[0,1]` and finite UVs. The compatibility path permits high mantissa bits only
when every such bit is already set in the reconstructed exponent word, and
only after the per-component separator envelope has been proven. The published
layout retains the strict 23-bit mantissa rule. No value clamping, record skip,
filename heuristic or whole-version relaxation is introduced.

The CDP2 reader can now consume a value count already read while distinguishing
the two envelopes. Modern JT transports, quantized JT 9.x transports and all
other CDP2 callers retain their previous entry point.

## Regression and native evidence

Original fixtures cover:

- U8 and aligned-U32 legacy texture flags, both byte orders, including invalid
  storage, shared flags, truncation and unsupported layouts;
- published and per-component JT 9.5 lossless colour envelopes, RGB/RGBA,
  both byte orders, redundant exponent bits, malformed overlap and truncation;
- published and per-component JT 9.5 lossless UV envelopes, both byte orders,
  redundant exponent bits, malformed overlap and truncation; and
- existing modern, quantized, cancellation, allocation and LOD-fallback paths.

On 14 November 2026 the isolated Release targets built, the registered
`jt_codec_tests` test passed in 135.07 seconds, and all three authentic probes
completed without LOD fallback. P95-A produced 10 meshes, 512,432 vertices and
384,348 triangles; P95-B and P95-C each produced 10 meshes, 520,935 vertices
and 384,348 triangles. The full Release `ModelViewer` target also built. Probe
warnings report unsupported PMI Manager version 2 and UV-only appearances;
those are independent of this transport correction. The owner confirmed on
14 November 2026 that all three models open and that their appearance is
correct in ADS and PBR.

Exact verification commands, after loading the Visual Studio x64 environment
and placing the vcpkg and Qt runtime directories on `PATH`, are:

```text
cmake --build out/jt102-build --target jt_tests jt_probe --parallel 4
ctest --test-dir out/jt102-build -R "^jt_codec_tests$" --output-on-failure
out/jt102-build/jt_probe <private-P95-A.jt>
out/jt102-build/jt_probe <private-P95-B.jt>
out/jt102-build/jt_probe <private-P95-C.jt>
cmake --build out/build/ninja_release_vcpkg --target ModelViewer --parallel 4
```

The completed UI gate opened all three unchanged files and confirmed complete
geometry and the intended visual differences in ADS and PBR. A later failure
in a different record remains new evidence, not permission to skip display
content.

## Method, provenance and limits

Consulted the owner's screenshots and local private samples; existing
GPL-noticed packet, texture, colour and UV readers; original repository tests;
and the retained public Siemens *JT File Format Reference, Version 9.5,
Revision A*, especially the texture-image definitions and Figures 229-230.
Retained PDF SHA-256:
`e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`.

As an independent layout comparison, the MIT-licensed JCadLib texture-coordinate
and colour readers at commit `27a12d1970e7684ef369d0a815aee0b7a194b02b`
were inspected. They confirmed raw exponent/mantissa reconstruction and did not
document the sampled separator envelope; no implementation code was copied.
Bob Jenkins' original `lookup2.c` was inspected to verify that JT 9.x `hash2`
adds byte length, not word count. The authentic separator profile is qualified
from the private bytes and repeated exact-count structure, not inferred from
either external implementation.

No vendor SDK, translator, schema package or disassembly was used. No
sample-derived bytes entered tracked tests. This records method and limits, not
legal clearance, redistribution permission or universal JT 9.5 compatibility.
