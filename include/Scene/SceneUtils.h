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
	static void deleteNodeRecursive(aiNode* node);
	static glm::mat4 aiMatrixToGlm(const aiMatrix4x4& from);
	static aiMatrix4x4 glmToAiMatrix(const glm::mat4& mat);
};