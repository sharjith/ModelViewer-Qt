# 3DXML scene graph, coordinates and transform evidence

Research branch: `feature/3dxml-importer`. This is a sample-derived functional
specification and integration plan, not application implementation. It uses
the supplied archive/XML/mesh bytes and this repository's existing loader.
No vendor software, schemas, SDKs, disassembly or reference viewer was used.

## Decisions supported by this sample

| Question | Decision / evidence level |
|---|---|
| Coordinate component order | Retain decoded triples as native XYZ. Corroborated by binary center candidates, differently oriented parts, camera and assembly placements. |
| XML rotation layout | The first three numbers are the local X basis column, the next three Y, the next three Z. Strong sample evidence from five pin placements and two screw placements. |
| Translation | Numbers 9, 10, 11 are parent-frame X, Y, Z translation. |
| Transform direction | Local/reference coordinates to parent coordinates. |
| Composition for column vectors | `worldChild = worldParent * localChild`. |
| Units | Millimetres strongly inferred for this sample from three ISO pin dimensions. No explicit XML unit metadata was found. Do not promote this into a universal exporter rule. |
| Up-axis | Preserve native axes. The supplied camera is predominantly Z-up; XML does not declare a universal up-axis. |
| Instance visibility | Apply `DefaultView` overrides by complete occurrence path and inherit hidden parent state. |
| Geometry reuse | Share representation meshes; create separate transform/visibility nodes for occurrences. |

The full measurements are in `3DXML_scene_graph_analysis.json`. Reproduce:

```powershell
python -I tools/3dxml/analyze_scene_graph.py D:\work\progs\qt5\ModelViewer-Github\data\ASM-GETRAG_V160.3dxml --inventory docs/3DXML_sample_analysis.json --report docs/3DXML_scene_graph_analysis.json --occurrences out/3dxml-research/scene_graph.json
```

The generated occurrence fixture includes each path, reference ID, attached
representation, local/world matrix, name and effective visibility. It is
research data under ignored `out/`; it is not a completed renderable assembly.

## Coordinate correction to earlier research

The original pin/ring decoding notes called the first component "z" because
those parts' axes run along their first coordinate. That did not establish a
global component order. It was a geometric label, and was wrong as an import
basis rule.

The decoded pin has native bounds X=0..25, Y/Z=-2.5..2.5. The ring is centered
at native X approximately 517.3. The screw is different: its native shaft
axis is Z, with Z=-5..12 and X/Y about -4.25..4.25.

Twelve raw bytes at `firstCountOffset - 21` act as a useful binary-center
candidate on these samples:

| Part | Decoded bounds center XYZ | Stored center candidate XYZ |
|---|---|---|
| Pin | (12.5, 0, 0) | (12.5, 0, 0) |
| Ring | (517.2999878, 0, 0) | (517.3000488, 0, 0) |
| Screw | (0, 0, 3.5) | (0, 0, 3.5) |

Across the 140 recognized members the median center discrepancy is about
0.0000238 with unchanged XYZ. The old cyclic permutation's median discrepancy
is about 310.5. Some irregular parts have larger unchanged-XYZ discrepancies
(up to 29.02): the exact bound-field semantics are not established, and this
is not a claim that every stored center must be an AABB midpoint.

The OBJ exporter now preserves native XYZ for both positions and normals.
Older generated OBJs used a cyclic display permutation; regenerate them
before using them for placement checks. The PNG preview deliberately turns
the pin/ring upright for presentation only. Its display transform is not
part of import or assembly coordinates.

If a future viewer basis conversion B is needed, either place B once above
the entire native scene, or consistently convert geometry and matrices:

```text
p_view = B * p_source
M_view = B * M_source * inverse(B)
```

Never permute vertices and then apply an unchanged XML transform to them.

## Exact matrix construction

For the twelve XML numbers `v0 ... v11`, use a column-vector affine matrix
shown here by rows:

```text
M = [ v0  v3  v6   v9  ]
    [ v1  v4  v7   v10 ]
    [ v2  v5  v8   v11 ]
    [  0   0   0    1  ]

p_parent = M * [x_local, y_local, z_local, 1]^T
```

Identity examples alone do not distinguish this from a transposed rotation.
The sample provides useful independent mechanical evidence:

- Pin reference 485 is reused at instance IDs 484, 498, 509, 520 and 531.
  Its native axis is +X. At all five translations, the first XML basis
  column points exactly inward along the parent frame's radial YZ direction:
  cosine approximately 1.0 in every case. Treating the numbers as rotation
  rows instead gives cosines from -0.623 to -1.0, pointing outward and often
  obliquely.
- Screw reference 161 is used by instances 160 and 164 on the same native
  X end plane. Both transform the screw's local +Z shaft direction to +X
  with the column interpretation. Transposing makes one screw point -X
  and the other +X.

For all 251 matrices, determinant ranges from approximately
0.999999999999992 to 1.0000000000000056; maximum orthogonality error is about
1.60e-14. Thus this sample has proper rigid transforms, without observed
mirrors, scale or shear. This does not justify silently orthogonalizing or
discarding scale/shear in other inputs.

Use explicit element assignment in the C++ implementation; do not depend on
a constructor's memory layout:

| Destination | Assignment |
|---|---|
| Assimp row a | `a1=v0, a2=v3, a3=v6, a4=v9` |
| Assimp row b | `b1=v1, b2=v4, b3=v7, b4=v10` |
| Assimp row c | `c1=v2, c2=v5, c3=v8, c4=v11` |
| Assimp row d | `0,0,0,1` |
| Qt matrix | Set the same four rows explicitly. |
| GLM matrix | Set columns `(v0,v1,v2,0)`, `(v3,v4,v5,0)`, `(v6,v7,v8,0)`, `(v9,v10,v11,1)`. |

`AssImpModelLoader::processNode()` already calculates
`parentTransform * node->mTransformation`, matching this convention. The
scene builder should populate local transforms and let that existing route
accumulate world transforms. Do not bake world coordinates and also retain
the same transforms on nodes.

For normals use normalized inverse-transpose of the world linear matrix;
translation has no effect. The sample's rigid matrices reduce this to normal
rotation. Define handling of negative determinants and singular transforms
before claiming those variants supported; they are not exercised here.

## Hierarchy and instances

Read `Manifest.xml` to locate the root member, then parse its XML namespace
and `ProductStructure root="1"`. ZIP member names must be resolved exactly,
without extracting untrusted paths to the filesystem.

- `Reference3D`: a reusable logical assembly/part definition.
- `Instance3D.IsAggregatedBy`: parent Reference3D ID.
- `Instance3D.IsInstanceOf`: instantiated child Reference3D ID.
- `Instance3D.RelativeMatrix`: child-local to parent transform.
- `ReferenceRep`: representation definition, format/version and member URI.
- `InstanceRep`: attaches a representation reference to a Reference3D via
  the same aggregation/instance links. No separate transform occurs on these
  representation attachments in this sample.

Start with a node for root reference 1. For each Instance3D aggregated by the
current reference, create an occurrence node and recurse into its target
reference. Attach that reference's representations to the occurrence. A
reference can have both representations and child instances.

Keep IDs as source strings. Use path-local cycle detection, not a global
"already visited" set that would discard reused parts. Each occurrence node
needs its own parent; sharing `aiNode` objects is inappropriate. Geometry
resources can be shared by representation identity and selected LOD.

Measured XML/graph counts:

| Item | Count |
|---|---:|
| Reference3D definitions | 173 |
| Instance3D records / expanded occurrences in this sample | 251 |
| ReferenceRep definitions | 153 |
| InstanceRep records | 153 |
| Maximum occurrence path length | 3 |
| Transform nodes including the synthetic/root-reference node | 252 |
| Visible occurrences after overrides | 234 |

No missing aggregation/instance target, duplicate product ID or reachable
reference cycle was found. The fact that expansion yields 251 occurrences
here is sample-specific: another file can reuse a reference containing an
entire subassembly, producing more occurrences than source Instance3D records.

Examples of geometry reuse include reference 485 (five pins), 325 (ten
instances), 688 (21 instances), and 714 (13 helper-vector instances).

Nested placements must remain nested. Path `2/173/175` inherits the
approximately 366.5 X translation of subassembly 173 despite the child's
identity local transform. Paths `290/366/368` and `290/366/372` inherit the
rotation and translation of assembly 366. This sample does not provide a
strong independent example of two noncommuting nested nonidentity matrices;
the future owner-run implementation checks should include such a synthetic
case, in addition to these captured paths.

## Visibility and nongeometry representations

`DefaultViewProperty/OccurenceId/id` lists instance IDs along a path, using
the spelling `OccurenceId` present in the sample. Strip the member URI prefix
and resolve the full ordered path, not only its final ID or reference ID.

Hidden paths in the file are:

```text
290/337
550/608
550/612
711
```

All resolve. Hiding path 711 hides its 13 descendants as well, giving 17
hidden occurrences overall. Keep these nodes in the logical graph so the
user can reveal them. Respect effective visibility for initial rendering and
framing; otherwise helper vectors/axes can distort the scene bounds.

Preserve `selectable` and other occurrence overrides as metadata for future
integration. The current research fixture evaluates visibility only.

Root reference 1 also attaches `Applications.3DRep`, whose XML format is
`TECHREP` and discipline/usage are Review. This is distinct from the 152
`UVR` representation references. Do not pass the TECHREP member to a UVR
mesh decoder or count it as an unexplained tessellated body. Its review
semantics remain out of scope.

## Units and camera

The XML has no element or attribute name containing "unit". The strongest
available dimensional cross-check is three named ISO 2338 parts:

| Named size | Decoded axial length | Maximum radial diameter |
|---|---:|---:|
| 5 x 25 | 25 | 5.00000019 |
| 10 x 20 | 20 | 10.00000043 |
| 8 x 14 | 14 | 8.00000022 |

This strongly supports millimetres for this archive. Preserve source numeric
coordinates and record the unit inference, rather than inserting an
unexplained 0.001 scale. General files without explicit supported unit
metadata need a documented default/user override. Unit conversion, if
applied, must scale both geometry and translation consistently, or be a
single root transform. Do not subsequently apply it again as viewer autoscale.

The stored default view is a parallel camera. Native position is approximately
(-24.18094,657.18719,215.93813), and position plus Sight*targetDistance gives
target (318.84669,-15.18932,-9.05502). `Sight cross Up` matches the stored
Right vector within the XML's rounding. Up has Z component 0.95718, supporting
a Z-up presentation for this particular view, not a universal file-format
up-axis rule.

Preserve the authored camera separately from any user-selected orientation.
Camera position and distances must receive the same units/basis transform as
the scene; direction vectors receive its linear/basis part and normalization.
Viewport aspect ratio is not provided by this viewpoint record. The embedded
preview image was not used as reference output.

## Integration boundary and remaining limitations

This closes the main scene-graph convention gap for the supplied sample.
It does not complete the format's unrecognized binary layouts or formally
establish units for every exporter/version.

Before claiming full support, remaining research includes compound-directory
entry discovery, later blocks/possible multiple bodies, exact stored-normal
quantization, and more geometry QA across the broadly audited meshes.
The [2026-10-18 continuation](3DXML_extended_layout_findings.md) resolves
unequal position/normal counts and the first large-housing blocks, increasing
first-block range/length coverage from 140 to 150 members. This does not
validate every body's topology or every bound field.

No C++ importer, application filters or dependencies were changed. No
application build/test commands were run. Research files are checkpointed
on the requested feature branch; generated geometry remains ignored and can
be regenerated by the documented scripts.
