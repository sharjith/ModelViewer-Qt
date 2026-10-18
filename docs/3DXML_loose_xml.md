# Loose 3DXML assembly documents

The `cad-in-unity/3D XML Example` folder is an unpacked multi-file model.
`quad.3dxml` is its product-structure document. `CATMaterialRef.3dxml` and
`CATRepImage.3dxml` describe companion materials and images; neither contains
an assembly or standalone triangle geometry. Treating every `.3dxml` as a ZIP
produced a misleading error for all three files.

`ArchiveReader` now detects ZIP packages versus standalone `Model_3dxml`
documents. A loose assembly resolves representation files relative to its own
containing directory. The selected assembly is loaded directly, rather than
silently redirecting it to another root through a sibling manifest. ZIP
packages continue to use their manifest root.

Loose reads enforce the same member-path restrictions, per-file size limit,
cumulative read limit and cancellation checks. Canonical resolved companions
must remain within the model directory, including when resolving links.
Malformed XML, DTD/entity documents, missing companions and paths outside the
model directory fail explicitly. The reader does not fetch network resources.

Selecting a material/image-only document now explains that it has no product
structure and directs the user to the assembly named in `Manifest.xml`.
It does not silently substitute another model or invent preview geometry.

## Validation

The supplied `quad.3dxml` imports with:

- 8 unique mesh/material resources, 12 placed mesh references;
- 12 XML nodes, 14 nodes including representation bodies;
- 25,406 resource triangles and 35,014 placed triangles.

An independent XML reader agrees on the finest resource-triangle count.
The native audit for the loose model matches a ZIP repackaging of the same
files, including counts, bounds, material bindings and occurrence paths.
Source geometry and repackaged samples remain ignored under `out/`.
See `3DXML_loose_xml_validation.json` for measurements and source hash.

Synthetic tests cover loose XML with UTF-8 BOM, sibling representation loading,
missing/unsafe references, invalid input, DTD rejection, cancellation and the
companion-only diagnostic. A symlink escape fixture is enabled on Unix; that
conditional fixture was not executed in this Windows session. Existing ZIP,
XML tessellation and occurrence-color tests pass, as do the five-archive
regression and GETRAG golden geometry/normal/material/placement comparison.
The Release application and integrated 3DXML test target build and pass.

Full linked CATMaterial/texture rendering is still outside the existing
material subset. This change enables loose model loading; it does not claim
complete material-library or image-document rendering. The owner confirmed Quad opens in the GUI and both companion documents show
the intended explanation.
