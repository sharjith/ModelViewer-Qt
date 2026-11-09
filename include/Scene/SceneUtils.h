#pragma once

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>

class SceneUtils
{

public:
	static void mergeScene(aiScene** globalScene, aiScene* source);
	static aiScene* deepCopyScene(const aiScene* source);
	static aiMaterial* copyMaterial(const aiMaterial* src);
	static void deleteScene(aiScene* scene);
	// Frees the vertex and face arrays of scene->mMeshes[firstMesh..]; names, material indices and the node tree stay. The
	// document's retained scene is only a hierarchy/index record (the SceneMesh objects and the scene graph hold the geometry
	// and exports rebuild their own scene), so the imported copy of every vertex is dead weight otherwise.
	static void releaseMeshGeometry(aiScene* scene, unsigned int firstMesh);
	static void deleteNodeRecursive(aiNode* node);
	static glm::mat4 aiMatrixToGlm(const aiMatrix4x4& from);
	static aiMatrix4x4 glmToAiMatrix(const glm::mat4& mat);
};