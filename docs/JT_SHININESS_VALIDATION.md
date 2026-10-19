# JT shininess and PBR roughness

19 October 2026, `feature/JT-importer`. The owner authorized committing this follow-up after automated validation, before the additional application material check below. That UI check remains pending; commit authorization is not recorded as a test result.

## Change and source evidence

The existing decoder reads `JtAttribute_Material::Shininess()`, but the original scene builder omitted it. The importer now retains it as `AI_MATKEY_SHININESS` and emits `AI_MATKEY_ROUGHNESS_FACTOR` using `sqrt(2 / (shininess + 2))`. This matches the application's existing fallback in `MaterialProcessor.cpp` and gives Assimp's PBR/glTF exporter an explicit roughness value.

The published JT 10.0 reference, section 6.1.2.2, defines shininess as a Phong exponent in the range [1,128]. Table 16 assigns no inhibit/final flag to this field: bit 4 controls vertex-colour override. Participating descendant materials replace the inherited exponent; an ignored attribute leaves it unchanged. No undocumented field flag is assigned. Authored nonfinite or out-of-range resolved values are rejected. A scene without a participating material does not acquire a synthetic authored exponent.

PBR roughness is an approximation to the original Phong response. This change does not reproduce a JT viewer's lighting, environment, or complete material model. Geometry, stored normals, opacity, and placements are unchanged. No third-party source files or dependencies were changed for this follow-up.

## Automated evidence

The isolated MSVC AddressSanitizer JT suite passed with all five local part regressions. Synthetic cases cover exponents 1, 4, 96 and 128; out-of-range and nonfinite rejection; inherited/replaced/ignored values; inhibit/final flags; distinct materials for otherwise identical instances; and the production scene-copy utility. Tests check both the stored exponent and its calculated roughness.

The five local part samples all resolve to shininess **15** and roughness approximately **0.342997**. Existing geometry and opacity regressions pass. The Bolt GLB export/reimport test additionally verifies roughness preservation. CTest passes. Local logs remain in ignored `out/jt-research/shininess-jt-tests.log`.

Reproduce using the existing standalone `tests/jt` build and `jt_tests <sample-folder>`. `jt_probe <file.jt> --materials` now prints `shininess` and `roughness` alongside the existing draw-style fields. Downloaded sample geometry remains outside the repository.

## Application check

After rebuilding, load `out/jt-research/samples/Bolt.jt` in a lit raster mode. Open its material editor and inspect roughness: the imported value should be approximately **0.343**, rather than the previous missing-field default of **0.8**. Specular highlights may become tighter; lighting and environment affect the visible difference. Check that it remains opaque, with 913 vertices and 904 triangles. Reopening an MVF saved from this build should retain the roughness through the existing runtime material serializer.

The full application was not built by the agent. The production material adapter was inspected; the isolated tests exercise the importer, scene copy and Assimp export/reimport, rather than compiling the complete material-editor/viewport path. Application and MVF document checks remain with the owner.
