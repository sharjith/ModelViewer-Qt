#pragma once
class SceneMesh;


#include <functional>
#include <vector>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <assimp/scene.h>

class SceneGraph;
class SceneNode;
class RenderableMesh;
class SceneGraphExporter
{
public:
    using MeshResolver = std::function<SceneMesh* (const QUuid&)>;

    // Rebuilds an export-only aiScene from:
    //  - SceneGraph hierarchy
    //  - current live meshes resolved by UUID
    //
    // This first-pass implementation:
    //  - uses identity node transforms
    //  - exports positions, normals, UV0, and triangle faces
    //  - creates one default material and assigns materialIndex = 0 to all meshes
    //
    // Later we can extend this to:
    //  - rebuild full materials from current Material state
    //  - include lights
    //  - support multiple UV channels / vertex colors / tangents as needed
    // flattenTransforms: when true, every aiNode gets an identity transform and
    // vertices are left in world space.  Required for formats whose Assimp writer
    // ignores node transforms (OBJ, PLY, STL).  Default false for hierarchy-aware
    // formats (glTF, FBX, COLLADA).
    // allowedSourceFiles: if non-empty, only file nodes whose sourceFile path is in
    // this list are exported.  An empty list means "export all loaded scenes".
    // outAnimMatRemap: optional output map filled with
    //   key   = "originalMatIdx@sourceFile"  (e.g. "2@/path/to/model.glb")
    //   value = export material index in the returned aiScene
    // Callers can use this to remap GltfAnimationChannel::targetMaterialIndex so that
    // KHR_animation_pointer paths reference the correct material after re-ordering.
    static aiScene* buildExportScene(
        const SceneGraph* sceneGraph,
        const MeshResolver& resolveMesh,
        bool flattenTransforms = false,
        const QStringList& allowedSourceFiles = QStringList(),
        QMap<QString, unsigned int>* outAnimMatRemap = nullptr);

private:
    static aiNode* buildNodeRecursive(
        const SceneNode* srcNode,
        const MeshResolver& resolveMesh,
        std::vector<aiMesh*>& outMeshes,
        std::vector<aiMaterial*>& outMaterials,
        QMap<QString, unsigned int>& materialKeyToIndex,
        QHash<QUuid, QString>& exportedNodeNameByUuid,
        QHash<QUuid, QString>& exportedMeshNodeNameByUuid,
        QSet<QString>& usedExportNodeNames,
        const aiMatrix4x4& parentWorldTransform,
        bool flattenTransforms,
        const aiMatrix4x4& importCorrection = aiMatrix4x4(),
        QMap<QString, unsigned int>* animMatRemap = nullptr
    );

    static aiMesh* buildMeshFromSceneMesh(const SceneMesh* mesh, unsigned int materialIndex);

    static aiMaterial* buildMaterialFromSceneMesh(const SceneMesh* mesh);
};
