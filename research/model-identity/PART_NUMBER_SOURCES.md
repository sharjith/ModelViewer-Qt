# Where a model declares its part number (drawing number default for Print and Save as 3D PDF)

Started 2026-11-10 on `feature/model-identity`. Observations come from local samples (not published): sample IDs are neutral, no model
names or values are recorded here.

## Owner decision (2026-11-10)

Take the number from the model's own property when the format has one; otherwise fall back to the last value typed in the dialog. No part
number is derived from node names: naming and revision schemes are a company's convention and differ between companies.

## Observations

- **3DXML, sample X-1 (a CATIA-style product structure, 188 references).** Each `Reference3D` carries `PLM_ExternalID` (the number with a `prd-`
  prefix), `V_Name` (the part number), and `V_description` (a text description); `V_nature`, `V_version`, `V_maturity`, `C_created`
  and `C_modified` are also present. The root reference has a `V_Name` and a `V_description`. In sample X-2 (a small assembly exported
  from a different system) `PLM_ExternalID` equals the name and there is no separate part number.
- **STEP, sample S-1 (Autodesk-exported AP214).** `PRODUCT(id, name, description)`: the id and the name differ (`id` shorter, `name` the
  same with a version suffix). The viewer's importer reads the XCAF label name, which comes from one of the two fields; the id is not
  kept yet. Reading it needs the STEP entity behind the root label (open item, see below).
- **JT, sample J-1 (a Teamcenter-style export).** JT defines the property mechanism (key/value pairs on a node) but no key for a part number;
  what a file carries depends on the exporter. This file's root and part nodes carry `JT_PROP_NAME` and internal identifiers
  (`__PLM_ITEMREV_UID`, `__PLM_BV_UID`, `__PLM_PACKING_ID`, `__PLM_CLONE_STABLE_INST_UID`) only. The part number and revision occur
  only inside `JT_PROP_NAME` (a string of the form `<id>/<revision>-<name>.<ext>;-1;<number>:`), which is a company convention, not a field.
  The four Solid Edge JT files of the material survey carry `CAD_PART_NAME::`, `SEFileName::`, `Name::` and `SEOccurrenceID::`, which are
  names and occurrence ids, not a part number.

## What is implemented

- 3DXML: the reader keeps `V_Name` and `V_description` of every reference; the scene builder writes them to the node as `3DXML_V_Name` and
  `3DXML_V_description`. `NodeProperties::declaredPartNumber()` returns the root node's values (the outermost node of a mesh's property
  chain), never a sub-assembly's.
- STEP: after the transfer, the reader finds the file's one root product (the product definition that is the relating side of assembly
  usages and never the related side; or the only product definition of a file without assembly structure) and puts its `PRODUCT` id on the
  label of the document's one free shape. `XCAFDocProcessor` writes it to the root node as `STEP_PRODUCT_ID` (the root assembly node, or the
  part when the root is a single part). With no unique root or more than one free shape nothing is written.
- Save as 3D PDF and Print pre-fill Drawing no. with that value (the user can change it); with no declared number the last typed one is offered.

## Open items

- STEP: checked on one Autodesk-exported file only (id `LD200 torsen`); other exporters, and AP242 files whose usages use the reference forms,
  are untested.
- JT and other formats: a verified key is needed. Teamcenter-style files with a per-property mapping may carry one; the viewer does not guess.
- Revision: 3DXML has `V_version` (a counter) but its meaning as a drawing revision is not established, so no revision is taken.
