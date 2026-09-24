#pragma once

#include <QVector>
#include <vector>

class SceneMesh;
class Material;

// Groups `indices` (into `meshes`) by the same (sourceFile, originalMaterialIndex,
// primitiveMode) IMPORT identity - i.e. "these meshes came from the same original
// material slot," used by Mesh Union/Merge by Adjacency's "Keep Materials Separate"
// choice, where that provenance is exactly what matters (seam continuity, shared
// UVs, etc.). NOT the right grouping for "do these meshes currently look the same
// material-wise" - a mesh re-materialed via the eyedropper/Apply keeps its ORIGINAL
// import identity even though its actual current material changed - see
// groupIndicesByCurrentMaterial() below for that question instead (confirmed real
// bug: Filter by Material used to call this function, so re-materialing one mesh
// left it grouped with its unrelated original import siblings, and two meshes given
// the identical current material via the eyedropper could remain in separate groups).
// originalMaterialIndex < 0 (untracked) never matches anything else, including
// another untracked mesh, so a mesh with no reliable match becomes its own singleton
// group rather than being silently lumped in with other untracked meshes. Groups are
// returned in first-seen order - deterministic result naming/placement for callers
// that create one result per group.
std::vector<std::vector<int>> groupIndicesByMaterial(const QVector<SceneMesh*>& meshes,
                                                       const std::vector<int>& indices);

// Resolves just the group containing meshIndex, per the same IMPORT-identity rule as
// groupIndicesByMaterial() above - for callers that only care about one mesh's
// import-provenance siblings rather than a full multi-group split of an arbitrary
// index list. Returns an empty vector if meshIndex is out of range.
std::vector<int> meshIndicesUsingSameMaterialAs(const QVector<SceneMesh*>& meshes, int meshIndex);

// Groups `indices` by whether their CURRENT material - not import identity -
// currently looks the same. This is the right question for Filter by Material
// ("select every mesh that currently uses this material"), and for anything else
// that cares about a mesh's actual live appearance rather than where it came from
// at import. Exhaustive over Material's full public getter surface (name; the
// PBR/glTF field set; the separate legacy Phong field set; the separate PBR
// Specular-Glossiness workflow field set; misc scalars/bools; channel packing for
// the 4 packable single-channel maps; every one of Material's ~25 texture slots'
// LIVE path, sampler settings, and LIVE UV transform) - see the .cpp's own doc
// comment on currentMaterialKey()/liveTexturePath() for the full breakdown, and for
// why "live" needed calling out twice: Material keeps two storage locations for
// both a texture's path and its UV transform - one populated at import
// (texture(type)'s own fields) and one that every live-editing code path in this
// app (MaterialPropertiesPanel, the eyedropper, etc.) actually writes through
// instead - and only the second is current for any material edited after import.
// Three earlier, narrower versions of this comparison each missed real state
// (transmission/IOR/clearcoat; then specularColorFactor/diffuseColor/
// glossinessFactor; then channel packing plus reading the stale texture-path/
// transform copy instead of the live one) and let materials that actually differ
// group together - confirmed real bugs each time, which is why this enumerates the
// type's complete getter list rather than a hand-picked subset. Groups are returned
// in first-seen order, same as groupIndicesByMaterial() above.
std::vector<std::vector<int>> groupIndicesByCurrentMaterial(const QVector<SceneMesh*>& meshes,
                                                              const std::vector<int>& indices);
