#pragma once

#include "U3dScene.h"

#include <assimp/scene.h>

namespace Pdf3D
{

// Converts the application's export scene (the aiScene SceneGraphExporter::buildExportScene() builds, hierarchy aware, already
// restricted to the meshes the caller's resolver returned, e.g. visible parts only) into a U3dScene:
//  - one U3dNode per mesh reference in the node tree, with the accumulated world transform (u3d wants flat, world-space nodes);
//  - one U3dMesh per distinct aiMesh (instances share the mesh), triangles only;
//  - one U3dMaterial per distinct used aiMaterial: diffuse (base colour), emissive and opacity. PBR terms and textures are not
//    carried over in the first version (research doc section 4.2).
// Node and mesh names are made unique and ASCII-only; the user-facing name is kept in U3dNode::displayName.
class SceneToU3d
{
public:
    // rootChildPreTransforms: optional, one matrix per direct child of the scene root (one per exported file); each is applied
    // before that child's own transforms. SceneGraphExporter factors the import correction (auto orient / auto scale) out of
    // the export scene; passing the files' corrections here puts the model back into the space the viewer shows it in.
    static U3dScene build(const aiScene* scene, const QVector<QMatrix4x4>& rootChildPreTransforms = QVector<QMatrix4x4>());
};

} // namespace Pdf3D
