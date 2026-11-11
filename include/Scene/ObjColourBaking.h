#pragma once

struct aiScene;

// Wavefront OBJ has no vertex colours (Assimp's OBJ writer drops the colour channel), so a colour that lives in the vertices (JT vertex and face
// colours, XT face colours, baked simulation or plot colours) would come out as the material's base colour. Before an OBJ is written, this turns
// the colours into materials: the triangles of a mesh are grouped by colour, each group becomes a mesh of its own with a copy of the mesh's
// material whose diffuse colour (Kd) is that colour. Vertex colours take precedence over the material's colour.
//
// A mesh whose vertices are all white (1,1,1,1) has no colours in this sense (that is what the viewer stores when a mesh has none) and is left
// alone, as are meshes that are not triangle lists, have morph targets or bones. Colours are taken per triangle (the mean of its three
// vertices): colours that are constant per face, as JT and XT stored face colours are, come out exactly; a smooth gradient is approximated, and
// the number of colours per mesh is limited by coarser quantisation (at most 256 groups at 8 bits, fewer bits when there are more colours).
namespace ObjColourBaking
{
struct Result
{
    int meshesChanged = 0;       // meshes that were recoloured in place or split by colour
    int meshesAdded = 0;         // meshes created by splitting
    int materialsCreated = 0;    // coloured copies of materials
};

// Changes `scene` in place (its mesh and material arrays are replaced, its nodes' mesh lists updated). Safe to call on any scene.
Result bake(aiScene* scene);
}
