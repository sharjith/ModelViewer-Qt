# Material and mass data in imported CAD/PLM files

Status: survey done 2026-11-08; data model agreed by the owner, not yet implemented. Purpose: a single, documented source of truth for the
material and mass values that the Save as 3D PDF sheet (`{material}`, `{mass}`) and the parts list show. A 3D PDF is a manufacturing
deliverable, so a value printed on it must say where it came from.

## 1. Method and qualification limits

Read-only surveys of local samples; nothing was imported, converted or executed. Output is under ignored `out/material-survey/`.
Samples have neutral IDs; names and local paths are not recorded here.

| Sample set | Count | Tool | What it reports |
|---|---|---|---|
| JT | 3 (JT-1..JT-3) | `jt_probe --properties` (the importer's own reader, `tests/jt/jt_probe.cpp`), aggregated by `tools/materials/survey_material_keys.py jtprops` | every node `{Key, Value}` pair, counted by key |
| 3DXML | 5 (X1..X5) | `survey_material_keys.py 3dxml` | element and attribute names matching material/mass/density/weight keywords, plus a list of all element and attribute names |
| STEP | 39 (S01..S39) | `survey_material_keys.py step` | entity counts and names matching the same keywords |

Limits: the keyword filter is English, French and German words only; STEP values, units and product linkage were **not** read; JT mass units are
not stated by the file (section 3). Counts describe these samples, not the formats in general.

Sample identification (SHA-256 prefixes): JT-1 `9374d1dcd7728b68`, JT-2 `5062b6b7d2e089ce`, JT-3 `f9db16346afa615b`;
X1 `6f95b1a5bd3e5a51`, X2 `3e8a572341665503`, X3 `92ea46f11be8550f`, X4 `0d480a20de89213c`, X5 `28778061da869c2a`.
STEP IDs are the sorted order of the `.stp`/`.step` files in the owner's local sample folder (mapping kept in `out/material-survey/step-files.txt`).

## 2. Observations

### JT

| Sample | Nodes | `CAD_MASS::` | Material or density key |
|---|---|---|---|
| JT-1 | 22,038 | 6,830 nodes | none |
| JT-2 | 4,590 named | 3,606 nodes | none |
| JT-3 | 2,442 named | none | none |

- Other keys: part name, `__CAD_INST_UID`, `__CAD_STABLE_BODY_UID`, PMI sorting keys, `LAYER::`, tessellation and LOD keys.
- Every node reports `JT_PROP_MEASUREMENT_UNITS = Millimeters` (a length unit). **No key states the unit of `CAD_MASS::`.**
- `JT-2` also has `__PLM_LEAFCOMPONENT` (1,510 nodes); its meaning is not established.
- Words such as STEEL inside part names are names, not declared materials, and are not counted as material data.
- `CAD_MASS::` values in JT-1 range from 0.00006 to 418 with a median near 0.0097.

### JT from Solid Edge (2026-11-08, the JT files up to 13 MB in the owner's sample folder, 17 files incl. test fixtures; output `out/material-survey/jt-F0*.properties.json`)

Four of them (F01 11.7 MB, F02 6.9 MB, F04 4.2 MB, F06 1.8 MB; `CAD_SOURCE::` = "Solid Edge") carry, per part record: `CAD_MASS::`, `CAD_MASS_UNITS::`
(= "kilograms" in all 2,266 records), `CAD_DENSITY::` (7.85e-06 for steel, 7.833e-06, 8.027e-06, 1.19e-06 ...: kilograms per cubic millimetre,
consistent with the file's length unit millimetres and with kg), `CAD_VOLUME::`, `CAD_SURFACE_AREA::`, `CAD_CENTER_OF_GRAVITY::` and more. Two of the
other files have the units key without masses. No material name key. Observations that matter for the sheet:
- The unit is stated, and it is kilograms: this settles hypothesis 1 for Solid Edge by the file itself (for NX it still rests on the washer check).
- Many records say `CAD_MASS::` 0 (F01: 543 of 544, F02: 121 of 1,103, F04: 8 of 172, F06: 4 of 447); with density 0 or absent they look like
  parts without a material. A zero is not a usable mass: the importer ignores it.
- An assembly record carries a mass too: F01's root says 1,305,053.8 (kg) with density 0.001 and surface area -1000000 (placeholders), F06's
  root 177.3 while the part records add up to 123.6 (not equal; why is unexplained: other parts without a record, or a different rule). So an
  assembly mass must never be given to a part below it. The importer now marks a node that declares a mass while a node below it does too
  as an aggregate and skips it (`NodePropertySet::massAggregate`, `AssImpModelLoader::markMassAggregates`).
- Not yet used: `CAD_DENSITY::` (a declared density in kg/mm3 could feed the material's density), `CAD_VOLUME::`.

### 3DXML (X1..X5)

No element or attribute matches material, mass, density or weight. The product-structure XML has only names, identifiers, matrices,
colour and surface/line attributes, and representation references.

### STEP (S01..S39)

- Schemas: 18 AP214, 1 AP242, 20 other or not recognised by the simple test.
- 10 files declare a material name and a density as property definitions named "material name" and "density of part"; their material strings
  are free text such as "Steel", "Steel, Mild", "Stainless Steel, 440C".
- 4 files have a `MASS_MEASURE_WITH_UNIT`; none have `DENSITY_MEASURE_WITH_UNIT`; none use `MATERIAL_DESIGNATION`.
- Not yet checked: whether a declaration belongs to the part shown, how it links to the product definition.
- Density values and units (read from the STEP text of the 10 files, 2026-11-08; these are file-level counts of `density measure` items):
  four files (S-1..S-4 by order of this list) state the density with an explicit derived unit of gram per cubic centimetre and values such as
  7.85, 7.75, 8.47 and 8 (g/cm3); six files give plain numbers without any unit entity, such as 7850, 4510, 7870, 8000 and 3400, which
  match kg/m3 for steel, titanium and similar. So the unit differs per file and must be taken from the file, not assumed.
- Observation to treat with care: in the four g/cm3 files many parts (31 of 69 in S-1, 14 of 16 in S-2, 10 of 11 in S-3, 19 of 25 in S-4) state a
  density of exactly 1 g/cm3. That is the density of water and looks like an unassigned default of the exporting CAD, not a material. It cannot be
  told from a real value by the file alone. Hypothesis, not established. Consequence for the sheet: a declared STEP density is shown with its
  source, and a density of exactly 1 g/cm3 deserves a visible "possibly a default" note rather than silent use in a mass total.
- **Probe results (2026-11-08, `step_material_probe` on two samples; output `out/material-survey/step-probe-*.txt`).** Correction to the two bullets
  above: both samples state their density with an explicit unit chain; my first text scan missed the SI-based form. ASSY-VTE (S-1): density
  items are gram per cubic centimetre (conversion-based gram, centi-metre), value 7.85 for steel; model length unit millimetre. GX200 (S-7):
  kilogram per cubic metre (SI kilogram and metre), value 7850; model length unit inch.
  What OCCT's reader stores: ASSY-VTE 87 materials, steel density 7.85 (the file's number unchanged) and `GetDensityForShape` 0.00785;
  GX200 7 materials, steel density 0.00785 (the file's 7850 divided by 1,000,000) and `GetDensityForShape` 7.85e-6. So the same steel arrives
  as 7.85 or as 0.00785 depending on how the file writes its unit: OCCT converts SI-based units and leaves a conversion-based unit (gram) as
  written. The stored density therefore cannot be turned into kg/m3 without knowing which case applied; observed, not explained.
  Names are reliable: ASSY-VTE has 87 materials, among them "Steel, Mild" (20), "Generic" (31, density 1), "Brass, Soft Yellow" (10), "Stainless
  Steel" (9); GX200 has 7 materials all named "Steel". "Generic" with density 1 g/cm3 is the exporting CAD's placeholder.
  Consequence: take the STEP material designation (name) from OCCT; take the density only after the unit chain has been read from the file
  itself (the STEP model's measure item and its derived unit), which needs its own verification step.
- **Unit setting test (2026-11-08, `--set xstep.cascade.unit=<MM|CM|M|INCH>`, both samples, output `out/material-survey/unit-runs/`).** OCCT accepts
  the parameter and the document length unit follows it (0.001, 0.01, 1 and 0.0254 metre). The steel density does not change: ASSY-VTE stays
  7.85 (`GetDensityForShape` 0.00785) and GX200 stays 0.00785 (`GetDensityForShape` 7.85e-6) for every setting, apart from rounding noise in the
  seventeenth digit. So the length-unit setting does not make the density consistent. Reading of the numbers (hypothesis): for an SI-based density
  unit OCCT converts to grams per cubic millimetre (7850 kg/m3 becomes 0.00785); for a conversion-based gram unit it keeps the file's number
  (7.85 stays 7.85, which is then g/cm3, not g/mm3).
- **Web check (2026-11-08), secondary sources only, not verified against OCCT's own source:** a forum reply quoting OCCT's source comments says
  `XCAFDoc_Material::GetDensity()` is g/cm3 and `XCAFDoc_MaterialTool::GetDensityForShape()` is g/mm3 (the tool divides by 1000)
  (<https://dev.opencascade.org/content/step-import-material-density>); the same thread reports one file giving 7.85 and another 0.00785 for the
  same steel, with no answer shown. A fork pull request says OCCT's STEP material reader ignores the exponents of the density unit and that the
  writer labels density with the wrong exponents (<https://github.com/taucad/opencascade.js/pull/44>). Neither source offers a way to set the density unit
  before import. This matches what the probe saw: the documented g/cm3 holds for ASSY-VTE (7.85) and fails for GX200 (0.00785).
- The importer's STEP reader runs with `SetMatMode` at OCCT's default; whether OCCT converts the density unit is not known. The evidence tool
  `tests/step/step_material_probe.cpp` (target `step_material_probe`) prints what OCCT's reader stores (name, density, density name and value
  type, and which shapes reference each material) so the conversion can be read off before any importer code relies on it.

## 3. Hypotheses (not yet verified)

1. JT `CAD_MASS::` is in kilograms. To test it: for several parts of different sizes made of steel, compare the mass with the mesh volume (Mass Properties)
   times 7,850 kg/m3, with the length unit taken as millimetres. A consistent ratio fixes the unit; an inconsistent one means a different unit or material.
   **Check done 2026-11-08 (JT-1, standard washers and a nut, nominal catalogue dimensions, not the sample's meshes):** the declared masses are
   ISO 7089 10x20 washer 0.003460 and ISO 7089 8x16 washer 0.001772. The nominal volumes (inner 10.5 and 8.4 mm, thickness 2.0 and 1.6 mm) times
   7,850 kg/m3 give 0.003573 and 0.001829 kg. Both declared values are 0.9686 of that, the same ratio for two sizes, which points to kilograms
   with a source density near 7,600 kg/m3 (not 7,850), and rules out grams or tonnes (factor 1,000). An ISO 4032 M12 nut declares 0.01684, about the
   17 g of a real M12 nut. Observation: unit is consistent with kg. Limits: nominal dimensions, not measured from the file's meshes; the source
   density is inferred. A comparison against the mesh volumes (Mass Properties) is still worth doing before the unit is hard-coded.
2. `CAD_MASS::` is the mass of the part definition, so every instance of the same part shares it. Observation supporting it: in the probe output
   the mass sits on its own record keyed by the part-file node label (`node`), separate from the instance record that carries the instance name and
   `JT_PROP_NAME`, so a reader must join the two by that label.
3. The STEP material strings are a designation by name only; a density value is attached separately and may be in a unit the file states.

## 4. Data model (agreed, not implemented)

- `Material` (shared by many meshes): keeps its density with the existing Unknown / Not applicable / value states; gains an optional declared
  designation (name or grade) and a source tag (declared by the file, assigned by the user, from the preset library).
- Part (mesh or its scene node, next to the importers' node metadata): the declared mass, its unit once verified, and the source key it came from.
  A mass belongs to a part, not to a shared material, and swapping a preview material must not lose it.
- Sheet and parts list values, in this order: user-assigned, then declared by the file, then computed from volume times density, which is
  labelled as an estimate. A total that mixes sources says so. A part with no data stays blank.
- Importer mapping: JT `CAD_MASS::` to declared mass; STEP "material name" and "density of part" to designation and density. Keys not
  recognised stay as plain node metadata. 3DXML has nothing to map in these samples.
- Open before coding: how the MVF session file and the exporters carry the new fields (they must survive save and reload).

### Step B1, STEP material name (2026-11-08, written, not built)

`XCAFDocProcessor::traverseXCAFAssembly` reads the XCAF material name of each part (definition label, else instance label) and writes it to the part
node's metadata as `STEP_MATERIAL_NAME` (a node that already had metadata keeps it as a nested `SourceMetadata` block); `readSTEPFile` sets
`SetMatMode(true)` explicitly. `NodeProperties::declaredMaterial()` takes the innermost non-empty value; the loader stores it on the mesh
(`declaredMaterialName`/`declaredMaterialSource`), MVF saves it as primitive extras, Mesh Info shows "Declared material". Deviation from the model
in section 4: the designation is kept per part, not on the shared `Material`, because the viewer's materials are shared by colour and can cover
parts with different designations. The name is shown as written ("Generic" included). The density is not read (unit problem above). Tests:
declared-material cases in `tests/jt/jt_node_properties_tests.cpp`. Also affects IGES and VRML through the shared traversal, where no XCAF
material exists, so nothing is added there.

Owner check of B1 (2026-11-08, viewer, screenshots): ASSY-VTE parts show their material ("Wood (Oak)" on the stand, "Generic" on a cylinder), the name
survives an MVF save and reload. The first GX200 part examined (the main body, named "SOLID") showed no declared material and no node properties.
Probe `--tree` on GX200 (output `out/material-survey/step-probe-GX200-tree.txt`) explains it: the file has 13 part definitions, 7 of which reference a
material (washer, nut, standoff, screw, hub, sprocket and one component), on the part definition labels, where the importer looks; the other six,
including the main body (1,192 sub-shapes), declare none. So the blank is the file's content, not a lookup miss. To confirm in the viewer: a small
GX200 part should show "Steel".

### Step C1, sheet values (2026-11-08, written, not built)

`{material}` and `{mass}` are scene values (`sceneSheetPlaceholders()`), filled from what the exported parts declare. `ModelViewer::exportAs3DPdf`
copies each mesh's declared mass, mass group and material designation onto its export node (`U3dNode`), `Pdf3D::summarisePartData()` (PartData.h)
boils them down: the material is the one designation, or "N materials"; the mass is the declared masses added up with each part counted once
(meshes of one part share a `declaredMassGroup`, a UUID given at import to every mesh under the property set that held the mass, saved in MVF);
when some exported parts declare nothing the text says "(k of n parts)", and nothing declared gives an empty value ("{mass|-}" shows a dash).
Precedence on the sheet: a custom field with the same id and a non-empty value (the user's own entry) wins; otherwise the declared value;
the standard placeholders (title, author, ...) are not overridable. The parts list's Material column shows the declared designation when
there is one; otherwise a material somebody chose in the viewer (a library preset); otherwise it is empty. The importers' part-named render
materials ("<file> (<part>)", with " (vertex colours)" on a colour variant, and "Material<n>") are not materials and give an empty entry
(`renderMaterialLabel()`); owner decision 2026-11-08 after the first PDFs showed "SOLID) (vertex colours)" in that column. No computed estimate
(volume times density) yet.
Owner check of C1 (2026-11-08, PDFs opened in a reader): GX200 title block "Steel (14 of 17 parts)", mass "-" (STEP declares none), parts list
shows Steel for the 14; ASSY-VTE "11 materials (305 of 314 parts)" (the count includes the CAD placeholder "Generic"), parts list shows the
declared designations. A 9,054-part JT export took about 4.5 minutes and the PDF would not open responsively in the reader (not a regression;
see the large-export note in the project memory). JT mass on the sheet is not yet checked on a smaller selection.
Test template to try it: `out/material-survey/material-mass-test-template.json` (copy into the templates folder, "Open folder" in the dialog).
The built-in templates and the sample are unchanged (the sample's own "material" choice field still overrides the declared value).

### Implementation status (2026-11-08, written, not built)

Step A, per-part declared mass from JT: `NodeProperties::declaredMass()` (key table: `CAD_MASS::` = kilograms; innermost property set first,
finite and >= 0 only) is evaluated once per node by `AssImpModelLoader` and stored on the mesh (`MeshImportAdaptor`/`SceneMesh`:
`declaredMassKg`, `declaredMassSource`). Persisted in MVF as primitive extras (`declaredMassKg`, `declaredMassSource`), copied by `clone()`,
shown in Mesh Info as "Declared mass (kg)". Deliberately not copied when a mesh is split, merged or combined (the declared value no longer
describes the result). Not yet done: the GLB/OBJ exporters do not write it; a part made of several meshes carries the part mass on each
mesh, so a total must count it once per part (the property set object is shared per node); the `Material` designation and source fields wait for
the STEP mapping (the STEP reader currently has `SetPropsMode(false)`, `SetMetaMode(false)` and does not read XCAF materials). Tests:
`tests/jt/jt_node_properties_tests.cpp` (declared-mass cases).

## 5. Regenerating the evidence

```
python -I tools/materials/survey_material_keys.py 3dxml <archive.3dxml> ... > out/material-survey/3dxml.json
python -I tools/materials/survey_material_keys.py step  <file.stp> ...      > out/material-survey/step.json
out/build/ninja_release_vcpkg/tests/jt/jt_probe.exe --properties <file.jt> > out/material-survey/<id>.properties.json
python -I tools/materials/survey_material_keys.py jtprops out/material-survey/<id>.properties.json
```

The probe prints diagnostics before the JSON on standard output; the aggregator skips them. Use a Windows-style path with the probe.
