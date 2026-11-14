# JT 9.4 sampled-subset qualification

14 November 2026, `dev`. This gate is based on one authentic private assembly, neutral ID **P94-A**. It enables only the observed transitional triangle-display subset; it does not infer support for JT 9.1-9.3 or every JT 9.4 representation.

## Evidence and provenance

P94-A is 1,309,570 bytes, little-endian, with SHA-256 `77e18dedc5049a268bd3955b86e9df945b3e5854d9fde846e4d73097e7e71e2f`. The original remains outside the repository. Read-only inventory was produced with:

```powershell
python tools/jt/inspect_jt_structure.py <local-samples>/sample-P94-A.jt --output out/jt-research/c3-jt95-owner/P94-A.structure.json
```

The inventory contains 83 TOC entries: one logical scene graph segment, 27 JT B-Rep segments, 28 metadata segments and 27 shape-LOD segments. The logical graph contains 27 groups, 27 instances, 27 parts, 27 RangeLOD nodes and 27 triangle-strip shape nodes. It has no polyline or point shape nodes and no XT segment.

A separate read-only field audit under ignored `out/jt-research/c3-jt95-owner/` found one uniform profile across the display records:

- base/vertex LOD local versions 1/1, mesh version 2 and topology version 2;
- triangle-shape base/shape/vertex local versions 1/1/2, binding value 10 and zero quantization parameters;
- base/material local versions 1/2 for all 54 material attributes;
- base/draw-style local versions 1/1 for all 26 draw-style attributes.

The retained public Siemens JT 9.5 Rev-A reference, SHA-256 `e32a56fabea9ebf72908b83c5302c9c2534514405376627c0a33f45fa9de359b`, was used only as an adjacent transitional-layout reference. It is not relabelled as a JT 9.4 specification. The existing audited GPL decoder and ModelViewer's original tests were consulted. No vendor SDK, restricted schema, disassembly, new dependency or third-party decoder implementation was used. This provenance statement describes the method, not legal clearance for the private model.

## Implementation boundary

The explicit file-version gate adds 9.4 without enabling 9.1-9.3. The existing 32-bit container, graph, Mk.2/topological tessellation, coordinate, normal, allocation and cancellation checks remain active. Material local version 2 is accepted for 9.4 as observed; 9.0 continues to reject it, and unknown local versions remain errors.

The exact JT B-Rep payload is not decoded or claimed. P94-A contains separate registered display tessellation, which is the viewer path under qualification. Textures, PMI, polyline/point records, alternate codecs and unobserved bindings are not qualified by this sample.

## Automated and native validation

Original synthetic 9.4 LE/BE graphs extend the existing 9.0/9.5 transitional tests. They cover repeated external-part occurrences, transforms, colours, material versions 1/2, invalid base/material versions and truncated containers. Unsupported-version tests continue to reject 9.1-9.3 and other unqualified revisions. These mixed-version graph controls reference original 8.2 geometry and therefore test the container/graph/material boundary, not authentic 9.4 tessellation.

The isolated Release `jt_tests` and `jt_probe` targets built successfully using MSVC 19.52.37029. The registered suite passed 1/1 (`jt_codec_tests`) in 128.92 seconds. The unchanged P94-A native probe completed in 0.066 seconds and validated **27 mesh resources, 27 occurrences, 54,743 vertices, 66,449 triangles and 137 nodes**, with no fallback parts. Local resource bounds are `(-110.287, -175, -58.2)` to `(321.25, 175, 195)`; positions, transforms, normals, triangle indices and scene references passed the probe's validation checks. Charged decoding allocation was 640,872 bytes. The ignored log is `out/jt-research/jt94-native-probe.log`.

## Owner UI gate

After the native checks pass, open P94-A using the installed viewer executable. Confirm complete geometry, assembly placement, colours, normals/shading, hierarchy, selection and fit-to-screen in ADS and PBR. The presence of 27 display resources is a native structural expectation, not by itself visual acceptance.

The owner reported on 14 November 2026 that the model loads after the change. Record the application load gate as accepted. No detailed ADS/PBR appearance comparison was reported, so do not infer that additional visual checklist from the successful load. No commit or push was requested.
