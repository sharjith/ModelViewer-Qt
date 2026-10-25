# 3DXML published documentation: findings and open questions

24 October 2026, `feature/JT-importer`. Research only; no importer code was changed. This supersedes the statement in [specification research](3DXML_specification_research.md) that no vendor guide or schema reference could be reached: Dassault publishes a user guide and a schema reference without a login. They define the XML-level semantics below. They do not define the binary UVR / CFV3 container.

## Sources and provenance

Downloaded on 24 October 2026 with the owner's approval, kept local only (not in the repository, not redistributed):

| Document | URL | Size | SHA-256 |
|---|---|---:|---|
| 3D XML User Guide, version 4.3 edition 0 | https://media.3ds.com/3dxml/3dxmldoc/3D_XML_User_Guide_4.3.pdf | 2,638,274 | `9acf533f7c6073eb4218bd571c5e09bddf5dba08531741f73fa56e3abed8ef28` |
| 3D XML User Guide, version 3 edition 0 (2006) | https://media.3ds.com/3dxml/3dxmldoc/3DXML_User_Guide.pdf | 2,528,067 | `7fd6bc5f85d44069b071634e349972e5cd13848e3c8e90bcc8206003a8d62e5f` |
| 3D XML Reference Guide (schema reference, HTML) | https://media.3ds.com/3dxml/3dxmldoc2/Ref_Guide/3DXML_Reference_Guide.html | 1,332,364 | `46151c4e9f2f9c1dfd2df409431a21e0993b378b58b48b2a24da64bfb637086a` |

Also reviewed, obtained by the owner through their own 3DEXPERIENCE login (stored outside the repository, not quoted here because its notice restricts use to authorized users' internal use): the 2024 white paper "3DXML Import Export on 3DEXPERIENCE R2024x" (https://media.3ds.com/support/white-papers/adv_ent/external/2024/DS_WhitePaper_3DXML_Import_Export_on_3DEXPERIENCE_R2024x.pdf, 3,663,223 bytes, SHA-256 `06a215c331817a2a69ae0c17890c928d189fb8723289854b4fc23ed32ea692eb`). It is a 53-page platform workflow guide (importing and exporting 3DXML in 3DEXPERIENCE, attribute mapping, splitting large files, review versus authoring exports). It contains no information on the file's mesh, colour, material, texture or UVR structure and does not change any finding below. No account was created by the assistant and no login was attempted by it. The schema files `3DXML.xsd` and `3DXMLMesh.xsd` at their former `www.3ds.com/xsd/` addresses return 404. Other public sources found (xeokit guide, AGPL-3.0; the Apache-2.0 `cad-in-unity` reader and example files; search results) contain no statement on these questions beyond sample XML. The documents are Dassault copyright; this note paraphrases them and records the method, not a legal clearance or a licence reading. Text was extracted locally for reading only.

## What the documents establish (XML level)

- **Colours.** `RGBAColorType` has required `red`, `green`, `blue` and an optional `alpha`, all floats, with no range or default declared in the schema. The guide states the channels range from 0 to 1, alpha 1 is opaque and alpha 0 fully transparent. A value of -1 is therefore outside the documented range; its meaning is not defined anywhere reachable.
- **Occurrences.** An occurrence is a node of the unfolded instance tree. It is addressed by an *instance path*: an ordered list of `Instance3D` ids from the root (the guide's example: the front-left wheel is the path 1,3). `OccurenceId` and the material "dress by" link both carry such a path in one `<Ids>`/`<OccurenceId>` list; several `<id>` elements are one path, not several occurrences. The schema allows each id to carry an optional `urn:3DXML:<document>.3dxml#` prefix (cross-document paths). In the owner's three-wheel-assembly sample the dress-by list 17, 12, 9 is a valid chain (path 17/12/9 exists in its product structure), which fits the path reading. The schema text says a one-element path should be a *reference* id, but CATIA exports one-element paths holding instance ids (observed in the samples).
- **Default view.** `DefaultViewProperty` assigns colour, opacity, visibility and a position to an occurrence. `GeneralAttributes` has `visible` and `selectable`, both defaulting to true. Graphic attributes set on a mesh apply to its primitive sets unless a set or primitive overrides them.
- **Mesh primitives.** Faces (independent triangles, strips, fans), polylines for edges, and point primitives are separate primitive sets with their own graphic attributes. A vertex buffer holds positions, optional normals, texture coordinate sets and up to two optional colour sets. `PolygonalLOD` carries coarser levels with an accuracy value. TESSELLATED 1.2 is the XML mesh; UVR 1.0 is described only as a compressed triangular mesh for small, fast files, with no layout.
- **Coordinates.** Counter-clockwise (right-handed) system; `RelativeMatrix` is a 3x3 rotation plus a 3x1 translation. No length unit is stated.
- **Materials.** `CATMaterialRef.3dxml` holds `CATMatReference`, a `MaterialDomain` (only the Rendering domain, format TECHREP, rendering XML in a `*_Rendering.3DRep`) and its `MaterialDomainInstance`. The product document's `CATMaterial` section holds `CATMatConnection` records linking a material to an occurrence path: "DressBy" (covering, preferred for rendering) or "MadeOf" (core), a layer number (higher layers cover lower, alpha-blend-like), a 3x4 matrix and the two `PLMRelation` links. On a mesh, `SurfaceAttributes > MaterialApplication` (`mappingChannel`, optional `mappingSide` FRONT/BACK/FRONT_AND_BACK, optional `blendType` REPLACE/ADD/ALPHA_TRANSPARENCY/LIGHTMAP/BURN/INVERT) references a material by id; several applications stack in order.
- **Rendering material fields** (all optional, limits and defaults as published): AmbientCoef 0..1 (0.5), AmbientColor (1, .75, .07), DiffuseCoef (1), DiffuseColor (same default), SpecularCoef (1), SpecularColor (1,1,1), SpecularExponent 0..1 (0), EmissiveCoef (0), EmissiveColor (1,1,1), BlendColor, Transparency 0..1 (0), Reflectivity 0..1 (0.18), Refraction 0..1 (1). Texture fields: MappingType (0 environment, 1 implicit from mesh UVs, 2 operator), PreviewType operator (0 planar, 1 spherical, 2 cylindrical, 3 cubical, 4 adaptive), TranslationU/V, Rotation, ScaleU/V, FlipU/V, TextureDimension, TextureFunction (0 decal, 1 modulate, 2 blend, 3 replace), AlphaTest, Filtering (0 none, 1 linear, 2 trilinear, 3 anisotropic), WrappingModeU/V (0 clamp, 1 repeat), `TextureImage` (required for textured materials) and optional `ReflectionImage`, both URNs into `CATRepImage.3dxml`. The published 3DXML specular exponent relates to the OpenGL exponent, once brought into 0..1, by `3dxml = 1 - (openGL)^3`.
- **Images.** `CATRepImage.3dxml` lists `CATRepresentationImage` records (id, `associatedFile`, format, mime type, file name) for image files stored in the archive.

## Not covered by the documents (still empirical)

These come only from the owner's sample archives and earlier decoding work. They are hypotheses until checked against the exporting application:

1. The meaning of -1 colour channels (reading consistent with "no override").
2. The V5_CFV3 / UVR byte layout, including the face-style record, its fifth byte (observed ff, 7f, 00, 37), the first byte (30, b0, 20), the finest-face container byte (02, 03, 04) and the third tail-group kind. The fifth byte may be a 0-255 opacity (consistent with the guide's alpha semantics and with CATIA's 0-255 transparency range); unverified.
3. The 256x256 image inside the UVR attribute block (possibly the material's texture or reflection image; unverified).
4. Length units.

## Owner observations (sample comparison status)

- Observation (read-only byte scan, 24 October 2026): in the three sample archives the face-style head byte is `20` or `30` with fifth byte `ff` everywhere except one part (a cylinder block in the engine sample), where it is `b0` with fifth byte `37`. The owner states that part is transparent in CATIA, value unknown.
- First hypothesis, replaced: byte as opacity (byte/255, about 0.22). The owner recalls CATIA showing the block more opaque than that (from memory, not a side-by-side check).
- Current hypothesis: head bit `0x80` marks a transparency byte, opacity = 255 - byte (about 0.78). A read-only scan of the first owner sample (the gearbox assembly) found `b0` only with byte `7f`, on two helper parts (a rotation axis and a displacement vector); 255 - 127 = 128, so those records cannot separate the two readings. No `b0` record with byte `00` was found in any scanned sample (an earlier note of one is not supported). The owner reports that a third-party web viewer (GrabCAD) shows the helper spheres of that sample as transparent, which agrees with a `b0` record marking transparency, but gives no amount. The inverse reading's amount therefore rests on the owner's recollection of the engine block alone. **Not yet confirmed** against CATIA; the owner plans a side-by-side look.

## Materials in the samples (observation, 25 October 2026)

- Only the skate-board sample carries a material document: one reference with four rendering definitions ("Plastic"), each linked by a covering (DressBy) connection to an instance path of three ids (for example 17/12/9, root to leaf), layer 1, identity matrix. The rendering representation is an Osm file with the fields listed above; in all four the values are identical (diffuse 0.783 grey, diffuse coefficient 0.8, specular coefficient 0.25, specular exponent 0.37, transparency 0, no texture).
- Implemented: connections are read as paths; a covering material replaces the shading of the occurrence and its descendants (highest layer wins). An explicit occurrence colour replaces only the base colour. The specular exponent is mapped with the published formula. Not applied: texture fields, reflectivity, refraction, blend colour, layer blending, the 3x4 connection matrix. The face opacity byte is not combined with material transparency (the material's value is used).
- Open: whether CATIA gives a user colour priority over a covering material (the sample does not contain both on one occurrence).

## Tail index groups are edge polylines (observation, 25 October 2026)

- Method: a scratch copy of the earlier Python decoder, relaxed to accept the newer style variants, read every binary representation of the three samples and the first owner sample (188 meshes decoded there). For each tail group it tested whether every consecutive index pair is an edge of the mesh's own finest triangles.
- Result: for kind 1 and kind 2 groups, 100% of segments are triangle edges in every decoded mesh. Groups are runs of 2 to about 45 indices, none closed (first index equals last). Kind 2 therefore holds polylines too, not points as earlier notes assumed. Style bytes are `10 24 04 ff ff 00 00 00` (and `10 14 …` once); the colour is black.
- Interpretation (hypothesis): these are the exporter's B-Rep edge lines resampled onto mesh edges; the difference between kinds 1 and 2 is not established. Kind 0 occurs only in one tire part, whose scratch decode failed, so it is not interpreted.
- Implemented: kinds 1 and 2 are read as polylines and registered with the viewer's precomputed-edge store (the one STEP/IGES/BREP use), so they appear only in the edge and wireframe display modes, not as scene geometry; a segment present in both kinds is stored once. `Options::edges` switches reading off. A first attempt drew them as line meshes, which showed edges in every shading mode and exposed an unrelated viewer bug (the mesh optimizer reordered line indices as triangles when the index count was a multiple of 3).

## Consequences for the importer (observations of the current code)

- Occurrence colour parsing rejects channels outside 0..1; the XML tessellation reader applies the same rule. CATIA writes -1 for "unset".
- Alpha is ignored by both readers; the default-view alpha and face opacity are not used.
- Occurrence ids keep only the part after `#`, so cross-document paths cannot be distinguished.
- `CATMaterialRef`, `CATMaterial` / `CATMatConnection`, `MaterialApplication`, `CATRepImage`, polyline edges and point primitives are not read.
- Material-to-occurrence links are paths; they must be resolved as paths and applied per layer.

## Reproduction

No generated evidence is cited here. The three documents can be re-fetched from the URLs above; compare hashes before relying on the content.
