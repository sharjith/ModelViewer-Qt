# Default-view occurrence colors

The owner reported missing component colors in
`_fbc_001_dmg00005921053_dmg00005921053.3dxml`.
Its seven rendered UVR bodies use a common pale BGR mesh color, while the
assembly XML supplies seven distinct `DefaultViewProperty` surface colors.
The importer previously read only visibility from these properties.

`ProductStructureReader` now stores validated RGBA overrides by complete
occurrence path. `SceneBuilder` applies the nearest override during product
traversal, including generated body children. A more specific child override
wins; an occurrence without any override retains its original face colors.
Alpha defaults to 1 when absent. RGB is rounded to the existing 8-bit palette;
alpha is retained in base color, diffuse color and opacity material properties.

Material bindings are part of aiMesh. The first occurrence uses its source mesh
resource; additional distinct bindings get deep copies cached by source mesh
and material. This prevents changing other instances, preserves independent
ownership of geometry buffers and reuses identical variants. No positions,
normals, indices, placements or visibility rules are changed.

## Source-backed expected colors

| Occurrence | Component | RGB after palette conversion |
|---|---|---|
| 38 | bati | 128, 64, 64 |
| 42 | bielle_1 | 0, 128, 255 |
| 46 | bielle_2 | 0, 255, 0 |
| 50 | coulisseau_3 | 255, 128, 0 |
| 54 | glissiere_4 | 255, 255, 0 |
| 58 | bielle_5 | 128, 0, 255 |
| 62 | coulisseau_6 | 255, 0, 0 |

All seven overrides match the native scene's actual material bindings.
The other FBC archive also contains five occurrence overrides, which now match
its XML. GETRAG, NIST and press have no such overrides in their product XML.

## Checks

- Synthetic archive with a shared, multicolor part: inherited blue/alpha,
  child yellow, unoverridden original red/green, repeated same-color instance.
- Correct material variant reuse, independent geometry ownership and identical
  vertex/normal data across variants.
- Invalid channel values and unresolved occurrence paths rejected.
- All five supplied archives: valid geometry, actual scene attachment and
  unchanged resource/placed triangle counts.
- Independent Python XML extraction compares source RGBA overrides against
  native base/diffuse colors and opacity, covering all twelve explicit source
  overrides across both FBC archives.
- Original GETRAG golden comparison: positions, normals, materials and product
  graph still pass.

Results and source hashes are recorded in
`out/3dxml-research/3DXML_occurrence_color_validation.json`. The owner confirmed both FBC assemblies display correctly after reopening
with the color fix; the second missing-color report came from a model opened
before the fix was applied.

This implements occurrence colors, not the complete default-view property
system. Position overrides and the full material/texture model remain outside
this change.

The Release application rebuild and integrated 3DXML CTest both pass. Changes remain uncommitted; the owner has visually confirmed both FBC models.

Follow-up audit for the 5563053 archive also checks the wheel's unoverridden face colors: RGB 210/210/255 on 5,822 triangles; RGB 211/178/125 on 5,946; black on 31,188. All match the independent source decoder. The expanded audit now records every mesh's color binding, not just occurrence overrides. The owner subsequently confirmed the reported discrepancy was an already-open model from before the fix; no additional production change was needed. See 3DXML_fbc556_color_validation.json.
