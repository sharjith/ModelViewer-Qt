# JT PMI study: what a Solid Edge export contains and how to show it

1 November 2026, `feature/JT-importer`. **Read-only study; no importer or viewer code changed, no owner acceptance.** It follows the [C4 audit](JT_METADATA_PMI_AUDIT.md) (split C4a to C4d; C4a and C4b are committed). Statements are labelled *observed* (read from the control file by script), *derived* (computed from observations) or *hypothesis*.

## Evidence and method

| ID | Source | SHA-256 (prefix) | Content |
| --- | --- | --- | --- |
| C4-M1 | Solid Edge 2026 JT 10.10 export, metres, "Save PMI data" on | `5a421157b8743bf6` | One part (100 mm cube-like, stepped, with a through hole), 8 linear dimensions and 1 diameter dimension |
| C4-M2 | Same part and PMI, millimetres, PMI on | `8431acda33a1b5df` | Same 9 dimensions; coordinates and values in millimetres |
| C4-A2 | Assembly of two instances of the C4-M3 part (second rotated 90 degrees about Z and moved 300 mm), millimetres, PMI on | master `5904c9cd2bd17859`, part file `7d02e24ca15485a2` | Master JT with the assembly structure and an external part JT with the PMI (third round, below) |
| C4-M3 | Same part with more geometry (136 vertices against 102) and more PMI, millimetres, PMI on | `4357ee192c09a5eb` | The 9 dimensions plus radius, angle, surface texture symbol, callouts, notes, datum points and a balloon (second round, below) |

One exporter and version, one part family. The controls are owner-made and stay under ignored `out/jt-research/c4-samples/`; the exported file also embeds a local path and a user name, so it must not be published.

Regenerate the evidence (read-only; output stays under `out/`):

```
python -B tools/jt/inspect_pmi_segment.py out/jt-research/c4-samples/c4block_pmi.jt --output out/jt-research/c4-samples/c4block_pmi.pmi.json
out/build/ninja_release_vcpkg/tests/jt/jt_probe.exe --validate out/jt-research/c4-samples/c4block_pmi.jt
```

The tool is exploratory: it decompresses the PMI segment, lists the string table and groups the property strings into one entity each. It is **not a decoder** and does not prove the vendored reader correct. The published JT format reference was not available locally for this study, so the binary layout between the strings is not decoded here.

## Observations

1. **Where PMI lives.** With "Save PMI data" on, the file gains one extra segment of type 3 (LZMA-compressed, about 44 KB decoded). The same part exported without PMI has no such segment; the metadata segment of every file carries a PMI type table, and the logical scene graph carries `JT_PMI_CONTENT_HINT` (`VisiblePMI` with PMI, `none` without) and, with PMI, `JT_LLPROP_PMI`.
2. **Structure.** A string table lists the entity names (`Linear_1` to `Linear_8`, `Radial_Diameter_1`) plus the generic words `Dimension` and `PMI`. Each entity then carries about 50 property pairs stored as length-prefixed UTF-16 key/value strings, separated by binary blocks.
3. **Properties per dimension** (observed names): `value`, `unit`, `precision`, `tolerancePrecision`, `standard`, `type`, `font`, `textHeight`, `textOrigin`, `textRotationPoint`, `textDirection`, `justification`, colours, and per leader (`Leader[0]`, `Leader[1]`): `terminator`, `arrowType`, `arrowPlacement`, `extensionLineGap`, `extensionLineExtension`, `lineTextGap`, `Reference`.
4. **Recorded values and end points.** All values are written as plain numbers:

   | Entity | `type` | `value` | Distance between leader terminators (derived) |
   | --- | --- | --- | --- |
   | Linear_1, Linear_2 | 1 | 0.1 | 0.1 |
   | Linear_3 to Linear_8 | 1 | 0.025 | 0.025 |
   | Radial_Diameter_1 | 3 | 0.025 | no second terminator |

   For every linear dimension the stored value equals the distance between its two terminators to within 1e-9. The terminators are 3D points in the part's coordinates.
5. **Units (settled by C4-M2).** In the metre file `value` is 0.1 and the part spans 0 to 0.1; in the millimetre file `value` is 100 and the part spans 0 to 100. So `value` and every point are in the file's coordinate unit, and the `unit` property reads `millimeters` in both files, so it is a display unit that the importer must not use to rescale. The regenerated text would be "100.00 mm" (value 100, precision 2) once the file unit is applied. Observed on one exporter.
6. **No displayed text.** No "100.00" style string is stored. The text is regenerated from `value`, `unit`, `precision` and the display flags. The dimension text is drawn from stored stroke polylines (see next point), so no font is needed.
7. **Drawable geometry is stored.** Before each entity's properties the segment holds 3D polylines in the part frame: arrowheads, the dimension line, the extension lines and the glyph strokes of the text (for example segments near `textOrigin`). Each dimension's polylines lie in a plane parallel to the part face it measures (the front face for most; the side face for `Linear_2`), offset outward from the outline.
8. **No geometry references.** `Leader[0].Reference` and `Leader[1].Reference` read `-1 0 -1` for every entity. These files contain tessellation only, so the association to faces or edges was not saved; the dimension is positional.
9. **Diameter entity.** `type` 3 carries `diameterPlacement` and `style`, one terminator and no second one. The terminator lies on the hole's rim: the tessellated hole is a 12-gon whose vertices lie on a circle of radius 12.5 mm, and the terminator is 12.5 mm from the fitted centre (derived, 4 significant figures).
10. **Tessellation and measured size.** The 12-gon spans 25.0 mm vertex to vertex but about 24.15 mm across flats, so a value measured from the mesh could differ from the recorded diameter by roughly 3 percent depending on where it is picked (derived).

## Vendored reader

`JtElement_MetaData_PMIManager` is vendored, and nothing in the application instantiates it: the loader does not dispatch segment type 3, so it has never run on a real file here. **Reading its source against the decoded controls, it does not match their layout** (derived by reading, not by running it):

| Aspect | Vendored reader | Observed in the controls and in the JT 10.6 reference |
| --- | --- | --- |
| Start | I16 version (JT 9 and later), I16 local version, I16 reserved | U8 version (5), I16 empty field (-1) |
| Before the string table | 13 entity vectors (dimensions, notes, ...), then associations and user attributes | three U32 counts only (design groups, associations, user attributes), all 0 |
| Property atom | string, then a U32 hidden flag only when the local version is above 6 | string followed by one byte, at local version 5 |
| Entities | the 13 typed collections, then generic entities when the version is above 5 | generic entities only, every PMI kind in one collection |

The vendored reader models an older layout in which each PMI kind had its own typed collection; these Solid Edge 10.10 files use only the generic entity collection described in section 8.3 of the 10.6 reference. Reusing it for these files would misparse them. The practical path is a new reader for the 10.6 layout, written against `decode_pmi_segment.py` and the reference, with the vendored reader left untouched. The handoff from the other contributor recommended routing segment 3 through the vendored reader first; this finding argues against that, and the point is flagged for confirmation by running it, which needs a build.

## Measured and recorded dimensions

Recommended vocabulary for the two kinds of measurement, as the owner proposed:

- **Measured:** computed by the viewer from picked anchors on the mesh (today's Tools → Measure). The number follows the tessellation and any edit.
- **Recorded:** read from the source file: the authoring system's value, text and placement, kept as authored.

Where they differ matters. A recorded value is authoritative; a measured value of the same feature is a check of the mesh, and a difference means either a coarse mesh (as the 12-gon hole shows) or a wrong reading. The two must therefore never be mixed in one label.

### Mapping onto the existing tools (hypothesis, not built)

| Need | Existing tool | Fit |
| --- | --- | --- |
| Notes and free text | Annotate (point anchor on a mesh triangle, leader, MVF-saved) | Good fit. Each terminator or text origin can be converted to a triangle anchor by closest-point projection (they lie on or against the surface) |
| Linear dimension with a stored value | Measure, type Distance (two anchors) | Anchors fit; the value must be taken from the file, not recomputed |
| Diameter | Measure (needs a cylindrical face anchor) | Does not fit directly; a recorded diameter needs one rim point and a stored value |

Proposed data model (sketch): add an *origin* (`Measured` or `Recorded`) to a measurement. A recorded one keeps its anchors (so it follows the mesh when parts move), the recorded value, unit, precision and the text, plus the source entity name and a flag for the unit reading. The dialog list shows the origin and, for a recorded dimension with two anchors, a read-only "measured here" check value beside the recorded one. Persisting in MVF needs a schema extension.

### Rendering

- **Text first:** draw each recorded dimension as the existing dimension graphic with the recorded text. No dependency on the C3 line decoder.
- **Authored look later:** draw the stored arrow, extension and glyph polylines. These need the same polyline drawing that C3 is waiting on authentic samples for (C4d).

## Risks and unknowns

- Position-only association: if a part is moved, recorded points anchored by closest-point projection follow it only if they land on a mesh triangle. A terminator in empty space (extension lines can end off the part) would need a plain point in the part's frame instead of a triangle anchor.
- Assemblies: see the third round. One exporter, one nesting depth, and the instance transform has not been compared with the stored matrices.
- Entity coverage is partial: see the second round. Feature control frames, welds, chamfers, datum targets and regions have no record seen yet.
- Several codes are still unlisted: the angle's `type` 2 and the balloon's `type` 4 are not in the property table.
- Model views (named PMI views) probably exist in the segment but were not examined.
- PMI-on and PMI-off exports differ in more than the PMI segment (segment ordering and offsets); nothing else was checked.

## Second round: the extended control (C4-M3) and the millimetre control (C4-M2)

Run on 1 November 2026 with the same tool. The owner supplied a screenshot of the authoring system's item tree for C4-M3: eleven dimensions (Linear 1 to 8, Radial 1, Angular 1, Radial Diameter 1) and six annotations (Callout 1, Balloon 2, Surface Texture Symbol 1, Datum Target 1, Datum Point 1, Datum Point 2), plus a chamfer feature added to the part. That is 17 items, matching the 17 record groups the tool finds. The tool's printed names are positional and wrong for the annotations; the pairing below comes from comparing each group's own properties with the screenshot.

**What the string table is.** It holds both instance names (`Linear_1`, `Callout 1`, `Balloon 2`, `Datum Target 1`, ...) and entity-type names (`Dimension`, `PMI`, `SurfaceFinish`, `Note`, `Balloon`, `DatumTarget`, `Region`). *Hypothesis:* each record refers to its own type and parent-type names by table index, as the vendored reader's `EntityTypeStringID` and `ParentTypeStringID` fields suggest; not yet confirmed by reading those fields.

| Item in the authoring tree | Record properties observed | Notes |
| --- | --- | --- |
| Linear 1 to 8 | `type` 1, `value`, two terminators | Value equals terminator distance (observed) |
| Radial Diameter 1 | `type` 3, `style` 0, `diameterPlacement` | Value 25 |
| Radial 1 | `type` 3, `style` 1, `radialPlacement` | Value 12.5; `style` separating diameter and radius rests on one sample each |
| Angular 1 | `type` 2, `angleFormat`, two terminators | Value 2.356 is radians, shown as 135 degrees. The terminators read (0,0,0) and (2,2,0) and are not understood |
| Surface Texture Symbol 1 | `stubDirection`, `stubLength`, `precision` 4, no text | |
| Callout 1 | `Text[0].Item[0].string` = "This is a sample callout text" with font, height, colour | Callouts store their own text |
| Datum Target 1 | `Text[0].Item[0].string` = "Hole", `balloonId` "Hole", `radius` 2000 | Top half "Hole"; its lower half "A" was not found as a string |
| Datum Point 1, 2 | `Description` = "Datum Point 1" / "Datum Point 2", `attachmentType` 8, no leader | The letters P and S shown in the drawing were not found as strings; likely stroke polylines only |
| Balloon 2 | `type` 4, `label` "A", `index` 2, `point1` | Drawn as "A2": label plus index (derived) |

Dimensions do not store their text; callouts and the datum target do. The datum points' and datum target's displayed letters are unresolved.

The leader end points of the diameter, radius, callout, surface-finish and texture-symbol records each lie exactly on the tessellated surface (nearest-triangle distance 0.0000 mm, derived); only the balloon point does not. A closest-point projection onto the mesh therefore works for these, with a tolerance for the rest.

Consequences for the design: some PMI carries its own text and some must be regenerated, so the recorded record needs both a stored-text and a computed-text form; `attachmentType` (2, 3, 6, 7, 8 seen) and `type` (1 to 4 seen) are open enumerations to be mapped from more samples; the unit conversion is only to the file unit and the viewer's unit stays the file node's `importUnit` from C4b.

## Third round: an assembly (C4-A2)

Built by the generator script `tools/jt/solid_edge_make_assembly.py` through Solid Edge's COM interface (the bridge cannot create documents) and exported through the bridge. The export is a **master JT** of 3,084 bytes plus one **external part JT** in a folder beside it; the master has no PMI segment and the part file carries the whole PMI segment.

- **Observed:** the master file records two occurrences of one part (`jt_probe --validate`: 2 occurrences, 1 mesh), its own unit property `millimeters` and `JT_PMI_CONTENT_HINT` `none`; the part file records `VisiblePMI` and `millimeters`.
- **Observed:** the PMI of the part file has the same 17 records as the standalone control, with identical values and terminators. Only 23 text-placement and text-height properties differ (placements by up to about 0.4 mm; text height 6.5 against 6.74). The reason is not understood and is probably a re-layout of the text.
- **Derived:** PMI is stored once, in the part's own coordinate frame, not once per instance and not in the assembly frame. A viewer has to apply each instance's transform to the PMI, the same transform as its mesh.
- **Not checked:** the instance matrices themselves, nested assemblies, and whether a part referenced by an assembly keeps its PMI when the part file is renamed or moved.

Consequence for the design: a recorded item should be created per scene mesh instance (the viewer already creates one mesh per instance), anchored to that instance, rather than shared. The PMI must also be read from the external part file, which the importer already opens for geometry.

## External references (fourth round)

Both files were downloaded on 1 November 2026 into the ignored `out/jt-research/pmi-refs/` and stay local; this document records only their provenance and what was used.

| Source | URL | Size | SHA-256 |
| --- | --- | --- | --- |
| JT-IF Implementation Guidelines for PMI in JT, version 1.0, 12 December 2023 (12 pages) | `https://www.prostep.org/fileadmin/prod-download/JT-IF_ImplGuide_PMI_v1.pdf` | 634,576 bytes | `ca8bac92bd04979deec0629e12192b9334ddba8836ba6bded013550e231f925c` |
| NIST MBE PMI test-case STEP files (AP203 and AP242, March 2024 update) | `https://www.nist.gov/document/nist-pmi-step-files` | 13,976,599 bytes | `8fa78429e6d8d9b0d7681d223b6aa9ec98c3772185c55b1a0e3679b21c181911` |

NIST states the test cases, CAD models and STEP files can be used without restriction and asks for acknowledgement; the `NIST-PMI-STEP-Files` archive contains no JT files.

**What the JT-IF guide says that bears on the controls** (it is a guideline, not a format specification; the PMI data model is deferred to the JT file specification):

- Semantic PMI is always an extension of graphic PMI, with key/value parameters, styling and links to model elements. This matches the observed stored polylines plus about 50 properties per record.
- Four dimension kinds exist: angular, curve length, linear and radial. Linear is `type` 1 and radial is `type` 3, and **both diameters and radii are stored as radial (type 3)**. This confirms what the controls show (diameter and radius both `type` 3) and means `style` is the discriminator, not `type`.
- The `standard` property is a string enumeration: "0" ASME Y14.5M-1994, "1" JIS, "2" ISO, "3" BS, "4" ANSI Y14.5M-1982, "5" ASME Y14.41M-2003, "6" DIN, "7" GM addendum 1994, "8" ASME Y14.5-2009. **The controls' `standard` 4 is ANSI Y14.5M-1982.** The enumeration applies to feature control frames, dimensions, datum feature symbols, datum targets and several note kinds.
- Hole and thread callouts are a separate class (`JtkPMICalloutDimension`) whose properties are written as `ParameterDimension[#].<name>`, found so far only in NX output. None of the controls contains one.
- PMI is stored at the level it applies to (part or assembly node); sub-nodes are not used for PMI, and a reader should check all nodes. A part's PMI applies to every instance. This matches the assembly control.
- Counted PMI is dimensions, feature control frames, datum targets, datum feature symbols and notes; model views, reference coordinate systems and axes are not counted. A separate JT-IF validation-properties guideline defines counts stored in the file for checking an import; not examined here.
- The guide does not say which numeric codes are angular (observed 2 on one record) or curve length, and does not explain the balloon record's `type` 4. Those stay open.
- The guide points to the JT specification (ISO 14306:2017, paid; the Siemens reference, public) and to the prostep JT Industrial Application Package. The IAP address printed in the guide returned HTTP 404 on 1 November 2026, so it was not downloaded.

**NIST route to a JT with weld symbols or frames, tried.** Two AP242 files with semantic PMI (`nist_ftc_06`, `nist_ctc_01`) were imported through the Solid Edge bridge and exported to JT. Both JT files contain **no PMI segment**: the bridge imports STEP geometry only into a part template, so PMI in a STEP file is dropped. The route is closed unless the import is done by a method that reads PMI (not the bridge). The NIST files remain useful as STEP PMI references, not as JT samples.

## The JT 10.6 reference and its PMI property list (fifth round)

Downloaded on 1 November 2026 from `https://static.sw.cdn.siemens.com/media/published/4Zc4NsBnmaFlEd0mtssp7f/siemens-jt-v-10-6-file-format-reference-rev-d-10-feb-2026.zip` (4,855,350 bytes, SHA-256 `71b2a73e90094586b099250665a30149c0f52a6c792ed282d4fe95dbea4d93d3`): a 493-page format description (Revision D, 10 February 2026) and `PMIPropertiesStrings_alphabetic_list-JT_V10_6.xlsx`, one table per property key with the entity types it applies to and its value format. Kept local under `out/jt-research/pmi-refs/jt-spec/`. The spec says implementers should follow the description of the PMI Manager Meta Data Element rather than the PMI data segment, and gives the Generic PMI Entities layout in section 8.3.6 (about page 124). That section has **not been read yet**; only the property list and a few narrative pages were.

Things the reference settles about the controls (each checked against the observed values):

| Property | Reference says | Controls |
| --- | --- | --- |
| colours | `0x00bbggrr` (blue, green, red) | `0x00b43c78` is red 0x78, green 0x3c, blue 0xb4 |
| `standard` | 4 = ANSI Y14.5M-1982 (from the JT-IF guide) | 4 |
| `attachmentType` | 0 angular, 1 on geometry, 2 linear, 3 leader, 4 stacked, 5 ordinate, 6 radial, 7 diametral, 8 no leader | angular 0, linear 2, leader 3, radial 6, diametral 7, datum points 8 (all consistent) |
| dimension `style` | 0 linear-diametral, 1 radial, 3 diametral, others | diameter 0, radius 1 |
| `valid` | 0 true, 1 false, 2 unknown | 2 on every record |
| `justification` | 0 right, 1 centre, 2 left, ... | 2 |
| `angleFormat` | 0 degrees, 1 seconds, 2 minutes, 3 whole degrees | 0 |
| `Leader[n].Reference` | `"%d %d %d %s"`: target id, target type, reason, path | `-1 0 -1` (no target) |
| `value` | `%.16g`, no unit stated | see the unit finding |
| `unit` | `%s`, no enumeration given | "millimeters" |
| `index`, `label`, `point1` | datum target (0x0088) properties | on the record I called Balloon 2 |
| `balloonId`, `radius` | balloon note (0x0235) properties | on the record I called Datum Target 1 |

The last two rows cut against the names in the authoring tree: by the reference, the record with `label`/`index`/`point1` is a **datum target** and the one with `balloonId`/`radius` is a **balloon note**, the opposite of the names Solid Edge shows for them. The decoder in the sixth round read each record's entity type and settled it in favour of the reference.

The reference also lists the entity type codes (hexadecimal here; the file's own `PMI_TYPE_TABLE` uses decimal): 0x0004 spot weld, 0x0008 line weld, 0x0081 feature control frame, 0x0082 dimension, 0x0084 datum feature symbol, 0x0088 datum target, 0x0100 note, 0x0235 balloon note, with further codes for coordinate notes (0x0239), attribute notes (0x0240) and dressing notes (0x0241) from the JT-IF guide; the callout dimension carries `ParameterDimension` keys under 0x030D. The code for surface finish was not identified in what was read. The property table also shows that weld symbols, feature control frames and datum feature symbols are built from nested keys (`FCFText[n]`, `Secondary.Datum[n]`, `DatumFeatureSymbolText[n]`), so those would be readable from the same key/value mechanism as the dimensions.

**Units in the reference.** `JT_PROP_MEASUREMENT_UNITS` is required, should appear once per part, and where it appears on several nodes the lowest node along a path wins; values are millimeters, centimeters, meters, inches, feet, yards, micrometers, decimeters, kilometers, mils and miles, and a reader must accept the first letter in either case. This was used to widen the C4b mapping (see the audit).

## Sixth round: a spec-based decode (all controls)

`tools/jt/decode_pmi_segment.py` reads the PMI Manager element by the JT 10.6 layout (section 8.3). It decodes all four PMI controls (C4-M1, C4-M2, C4-M3 and the assembly's part file) with no unplaced bytes before the trailing sections, and its property counts, user labels, vertex counts and values agree with the earlier string-based dumps. This is evidence about this exporter and version only. Regenerate:

```
python -B tools/jt/decode_pmi_segment.py out/jt-research/c4-samples/c4block_pmi_extended.jt --output out/jt-research/c4-samples/c4block_pmi_extended.decoded.json --no-coordinates
```

**Layout as it was read (JT 10.10 file, PMI element version 5)**, after the segment's element header (length I32, type GUID `ce357249-38fb-11d1-a506-006097bdc6e1`, base type U8, object id I32):

| Order | Field | Observed |
| --- | --- | --- |
| 1 | version U8, empty field I16 | 5, -1 |
| 2 | design-group count, association count, user-attribute count (three U32) | all 0 |
| 3 | string table: count U32, then MbStrings (I32 count of UTF-16 units, then the units) | 11 or 24 strings |
| 4 | model-view count U32 | 0 (a file with views is not yet seen) |
| 5 | generic entity count U32, then each entity | 9 or 17 |
| 6 | trailing sections | 214 or 294 bytes; partly seen, not decoded |

**One generic entity**, in the order read:

1. I32 user label; U8 frame flag; if non-zero, a 2D reference frame of three 3-float points (origin, X-axis point, Y-axis point; it is a real frame in these files, not the dummy zeros the spec mentions); F32 text height; U8 symbol-valid flag.
2. I32 text-entity count (0 in every control); each text entity would carry a string id, font, a text box and text polylines.
3. Non-text polylines: I32 index count and I32 indices; I32 type count and I16 types; I32 width count and I16 widths; then the vertex array (I32 count, then floats, 3 per vertex for generic entities).
4. I32 property count, then each property as key and value MbStrings, each followed by a **single byte** (the hidden flag; the spec figure says U32 but the files carry one byte).
5. U32 type-name string id, U32 parent-type-name string id, U16 entity type, U16 parent type, U16 user flags.

**The records in C4-M3, by what each one says it is** (entity type from the spec's Table 56):

| Record(s) | Entity type | Parent | Content seen |
| --- | --- | --- | --- |
| 0 to 7 | Dimension 0x0082 | PMI 0x0001 | Linear: values 100, 100 and six of 25 (mm) |
| 8 | Dimension | PMI | Diameter 25; `style` 0, `diameterPlacement` |
| 9 | Dimension | PMI | Radius 12.5; `style` 1, `radialPlacement` |
| 10 | Dimension | PMI | Angle, value 2.356194490 (= 135 degrees in radians), `angleFormat` 0 |
| 11 | Surface Finish 0x0041 | PMI | Stub direction and length; no text |
| 12 | Note 0x0100 | PMI | Text "This is a sample callout text" |
| 13 | Balloon Note 0x0235 | Note 0x0100 | Text "Hole"; `balloonId` "Hole", `radius` |
| 14, 15 | Region 0x0246 | PMI | The two datum points (description "Datum Point 1/2"); no leader |
| 16 | Datum Target 0x0088 | PMI | `label` "A", `index` 2, `point1`: the "A2" symbol |

So all eleven dimensions are one entity type (Dimension 0x0082), and diameter, radius and angle differ only in properties (`type` 3 with `style` 0 or 1, and `type` 2). Solid Edge's own item names do not match the JT types: its "Callout" is a JT Note, its "Datum Target" a JT Balloon Note, its "Balloon" a JT Datum Target and its "Datum Point" a JT Region. A reader must go by the entity type, never the names.

**Settled by these decodes:** values and all points are in the file's unit (`value` 0.1 in the metre file, 100 in the millimetre file, with `unit` constant "millimeters"); polyline vertices are in the part's frame; the 2D frame places each dimension in its own plane; the PMI of an assembly's part is in the part file.

**Not decoded:** the trailing sections (polyline attribute overlays for 10.6 and later, with an entity index vector 0 to 8 visible; model-view sort orders; association properties), CAD tags (flag absent at element version 5), and the polygon data. Also not seen: any file with model views, associations, user attributes or design groups, any text-entity (2D text) record, and any feature control frame, datum feature symbol or weld record.

## Staging

1. **Read:** dispatch segment type 3 to a reader for the 10.6 layout (the vendored reader does not match, see above), compare with `decode_pmi_segment.py` on every control, keep the entities as importer metadata with provenance. No UI. Literal in-memory tests plus the controls.
2. **Recorded model:** add the origin to the measurement data, anchor terminators by closest-point projection, show the recorded text with the existing dimension graphic, list them with their origin. MVF schema change; owner to decide the persistence.
3. **Check value:** show the measured value of the same two anchors beside the recorded one.
4. **Authored look:** stored polylines, after C3.

## Samples still needed

| ID | Content | Settles |
| --- | --- | --- |
| C4-M3b | Feature control frame, datum feature symbol, datum target, weld symbol, chamfer, region, each created singly and named in a list from the owner | The entity kinds and the exact name-to-record pairing |
| C4-M4 | The C4-M1 part with named PMI views | Model-view records |

Owner acceptance has not been given for any statement here.
