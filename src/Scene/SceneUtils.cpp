#include "SceneUtils.h"

#include <iostream>
#include <algorithm>
#include <functional>

namespace
{
aiBone* cloneBone(const aiBone* srcBone)
{
	if (!srcBone)
		return nullptr;

	aiBone* dstBone = new aiBone();
	dstBone->mName = srcBone->mName;
	dstBone->mOffsetMatrix = srcBone->mOffsetMatrix;
	dstBone->mNumWeights = srcBone->mNumWeights;

	if (srcBone->mNumWeights > 0 && srcBone->mWeights)
	{
		dstBone->mWeights = new aiVertexWeight[srcBone->mNumWeights];
		std::memcpy(dstBone->mWeights, srcBone->mWeights, sizeof(aiVertexWeight) * srcBone->mNumWeights);
	}

	return dstBone;
}

aiAnimMesh* cloneAnimMesh(const aiAnimMesh* srcAnimMesh)
{
	if (!srcAnimMesh)
		return nullptr;

	aiAnimMesh* dstAnimMesh = new aiAnimMesh();
	dstAnimMesh->mName = srcAnimMesh->mName;
	dstAnimMesh->mNumVertices = srcAnimMesh->mNumVertices;
	dstAnimMesh->mWeight = srcAnimMesh->mWeight;

	const unsigned int count = srcAnimMesh->mNumVertices;
	if (srcAnimMesh->mVertices)
	{
		dstAnimMesh->mVertices = new aiVector3D[count];
		std::memcpy(dstAnimMesh->mVertices, srcAnimMesh->mVertices, sizeof(aiVector3D) * count);
	}
	if (srcAnimMesh->mNormals)
	{
		dstAnimMesh->mNormals = new aiVector3D[count];
		std::memcpy(dstAnimMesh->mNormals, srcAnimMesh->mNormals, sizeof(aiVector3D) * count);
	}
	if (srcAnimMesh->mTangents)
	{
		dstAnimMesh->mTangents = new aiVector3D[count];
		std::memcpy(dstAnimMesh->mTangents, srcAnimMesh->mTangents, sizeof(aiVector3D) * count);
	}
	if (srcAnimMesh->mBitangents)
	{
		dstAnimMesh->mBitangents = new aiVector3D[count];
		std::memcpy(dstAnimMesh->mBitangents, srcAnimMesh->mBitangents, sizeof(aiVector3D) * count);
	}

	for (unsigned int colorIndex = 0; colorIndex < AI_MAX_NUMBER_OF_COLOR_SETS; ++colorIndex)
	{
		if (srcAnimMesh->mColors[colorIndex])
		{
			dstAnimMesh->mColors[colorIndex] = new aiColor4D[count];
			std::memcpy(dstAnimMesh->mColors[colorIndex], srcAnimMesh->mColors[colorIndex], sizeof(aiColor4D) * count);
		}
	}

	for (unsigned int texCoordIndex = 0; texCoordIndex < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++texCoordIndex)
	{
		if (srcAnimMesh->mTextureCoords[texCoordIndex])
		{
			dstAnimMesh->mTextureCoords[texCoordIndex] = new aiVector3D[count];
			std::memcpy(dstAnimMesh->mTextureCoords[texCoordIndex], srcAnimMesh->mTextureCoords[texCoordIndex], sizeof(aiVector3D) * count);
		}
	}

	return dstAnimMesh;
}

aiNodeAnim* cloneNodeAnim(const aiNodeAnim* srcChannel)
{
	if (!srcChannel)
		return nullptr;

	aiNodeAnim* dstChannel = new aiNodeAnim();
	dstChannel->mNodeName = srcChannel->mNodeName;
	dstChannel->mPreState = srcChannel->mPreState;
	dstChannel->mPostState = srcChannel->mPostState;
	dstChannel->mNumPositionKeys = srcChannel->mNumPositionKeys;
	dstChannel->mNumRotationKeys = srcChannel->mNumRotationKeys;
	dstChannel->mNumScalingKeys = srcChannel->mNumScalingKeys;

	if (srcChannel->mNumPositionKeys > 0 && srcChannel->mPositionKeys)
	{
		dstChannel->mPositionKeys = new aiVectorKey[srcChannel->mNumPositionKeys];
		std::memcpy(dstChannel->mPositionKeys, srcChannel->mPositionKeys, sizeof(aiVectorKey) * srcChannel->mNumPositionKeys);
	}
	if (srcChannel->mNumRotationKeys > 0 && srcChannel->mRotationKeys)
	{
		dstChannel->mRotationKeys = new aiQuatKey[srcChannel->mNumRotationKeys];
		std::memcpy(dstChannel->mRotationKeys, srcChannel->mRotationKeys, sizeof(aiQuatKey) * srcChannel->mNumRotationKeys);
	}
	if (srcChannel->mNumScalingKeys > 0 && srcChannel->mScalingKeys)
	{
		dstChannel->mScalingKeys = new aiVectorKey[srcChannel->mNumScalingKeys];
		std::memcpy(dstChannel->mScalingKeys, srcChannel->mScalingKeys, sizeof(aiVectorKey) * srcChannel->mNumScalingKeys);
	}

	return dstChannel;
}

aiMeshAnim* cloneMeshAnim(const aiMeshAnim* srcChannel)
{
	if (!srcChannel)
		return nullptr;

	aiMeshAnim* dstChannel = new aiMeshAnim();
	dstChannel->mName = srcChannel->mName;
	dstChannel->mNumKeys = srcChannel->mNumKeys;

	if (srcChannel->mNumKeys > 0 && srcChannel->mKeys)
	{
		dstChannel->mKeys = new aiMeshKey[srcChannel->mNumKeys];
		std::memcpy(dstChannel->mKeys, srcChannel->mKeys, sizeof(aiMeshKey) * srcChannel->mNumKeys);
	}

	return dstChannel;
}

aiAnimation* cloneAnimation(const aiAnimation* srcAnimation)
{
	if (!srcAnimation)
		return nullptr;

	aiAnimation* dstAnimation = new aiAnimation();
	dstAnimation->mName = srcAnimation->mName;
	dstAnimation->mDuration = srcAnimation->mDuration;
	dstAnimation->mTicksPerSecond = srcAnimation->mTicksPerSecond;
	dstAnimation->mNumChannels = srcAnimation->mNumChannels;
	dstAnimation->mNumMeshChannels = srcAnimation->mNumMeshChannels;

	if (srcAnimation->mNumChannels > 0 && srcAnimation->mChannels)
	{
		dstAnimation->mChannels = new aiNodeAnim*[srcAnimation->mNumChannels];
		for (unsigned int channelIndex = 0; channelIndex < srcAnimation->mNumChannels; ++channelIndex)
			dstAnimation->mChannels[channelIndex] = cloneNodeAnim(srcAnimation->mChannels[channelIndex]);
	}

	if (srcAnimation->mNumMeshChannels > 0 && srcAnimation->mMeshChannels)
	{
		dstAnimation->mMeshChannels = new aiMeshAnim*[srcAnimation->mNumMeshChannels];
		for (unsigned int channelIndex = 0; channelIndex < srcAnimation->mNumMeshChannels; ++channelIndex)
			dstAnimation->mMeshChannels[channelIndex] = cloneMeshAnim(srcAnimation->mMeshChannels[channelIndex]);
	}

	return dstAnimation;
}
}

void SceneUtils::mergeScene(aiScene** globalScene, aiScene* source)
{
	if (!source || !source->mRootNode)
		return;

	if (!(*globalScene))
	{
		(*globalScene) = source;
		return;
	}

	// --- Merge meshes ---
	unsigned int oldMeshCount = (*globalScene)->mNumMeshes;
	unsigned int newMeshCount = source->mNumMeshes;
	aiMesh** mergedMeshes = new aiMesh * [oldMeshCount + newMeshCount];
	std::copy((*globalScene)->mMeshes, (*globalScene)->mMeshes + oldMeshCount, mergedMeshes);
	std::copy(source->mMeshes, source->mMeshes + newMeshCount, mergedMeshes + oldMeshCount);
	delete[] (*globalScene)->mMeshes;
	(*globalScene)->mMeshes = mergedMeshes;
	(*globalScene)->mNumMeshes += newMeshCount;

	// --- Merge materials ---
	unsigned int oldMatCount = (*globalScene)->mNumMaterials;
	unsigned int newMatCount = source->mNumMaterials;
	aiMaterial** mergedMats = new aiMaterial * [oldMatCount + newMatCount];
	std::copy((*globalScene)->mMaterials, (*globalScene)->mMaterials + oldMatCount, mergedMats);
	std::copy(source->mMaterials, source->mMaterials + newMatCount, mergedMats + oldMatCount);
	delete[] (*globalScene)->mMaterials;
	(*globalScene)->mMaterials = mergedMats;
	(*globalScene)->mNumMaterials += newMatCount;

	// --- Merge animations ---
	const unsigned int oldAnimationCount = (*globalScene)->mNumAnimations;
	const unsigned int newAnimationCount = source->mNumAnimations;
	if (oldAnimationCount + newAnimationCount > 0)
	{
		aiAnimation** mergedAnimations = new aiAnimation * [oldAnimationCount + newAnimationCount];
		for (unsigned int index = 0; index < oldAnimationCount; ++index)
			mergedAnimations[index] = (*globalScene)->mAnimations ? (*globalScene)->mAnimations[index] : nullptr;
		for (unsigned int index = 0; index < newAnimationCount; ++index)
			mergedAnimations[oldAnimationCount + index] = source->mAnimations ? source->mAnimations[index] : nullptr;
		delete[] (*globalScene)->mAnimations;
		(*globalScene)->mAnimations = mergedAnimations;
		(*globalScene)->mNumAnimations += newAnimationCount;
	}

	// --- Remap mesh indices in source nodes ---
	std::function<void(aiNode*)> remapMeshIndices = [&](aiNode* node) {
		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
			node->mMeshes[i] += oldMeshCount;
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
			remapMeshIndices(node->mChildren[i]);
		};
	remapMeshIndices(source->mRootNode);

	// --- Merge root nodes into a new one ---
	aiNode* newRoot = new aiNode("MergedRoot");

	// Add old and new roots as children
	newRoot->mChildren = new aiNode * [2];
	newRoot->mChildren[0] = (*globalScene)->mRootNode;
	newRoot->mChildren[1] = source->mRootNode;
	newRoot->mNumChildren = 2;

	// Assign the new root
	(*globalScene)->mRootNode = newRoot;
}

#ifdef _WIN32
#include <windows.h>

// Safe memory copy function with exception handling
bool SafeCopyTextureData(aiTexel* dest, const aiTexel* src, size_t byteSize)
{
	__try
	{
		std::memcpy(dest, src, byteSize);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ?
		EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		printf("SafeCopyTextureData: Access violation caught - invalid source data at %p\n", src);
		return false;
	}
}
#else
// For non-Windows platforms
bool SafeCopyTextureData(aiTexel* dest, const aiTexel* src, size_t byteSize)
{
	if (!dest || !src) return false;
	try
	{
		std::memcpy(dest, src, byteSize);
		return true;
	}
	catch (...)
	{
		printf("SafeCopyTextureData: Access violation caught - invalid source data at %p\n", src);
		return false;
	}
}
#endif

aiScene* SceneUtils::deepCopyScene(const aiScene* source)
{
	if (!source) return nullptr;

	aiScene* copy = new aiScene();

	// Copy flags
	copy->mFlags = source->mFlags;

	// ---- Copy Meshes ----
	copy->mNumMeshes = source->mNumMeshes;
	if (copy->mNumMeshes > 0)
	{
		copy->mMeshes = new aiMesh * [copy->mNumMeshes];
		for (unsigned int i = 0; i < copy->mNumMeshes; ++i)
		{
			const aiMesh* srcMesh = source->mMeshes[i];
			aiMesh* dstMesh = new aiMesh();

			dstMesh->mPrimitiveTypes = srcMesh->mPrimitiveTypes;
			dstMesh->mNumVertices = srcMesh->mNumVertices;
			dstMesh->mNumFaces = srcMesh->mNumFaces;
			dstMesh->mMaterialIndex = srcMesh->mMaterialIndex;
			dstMesh->mName = srcMesh->mName;

			// Copy vertex attributes
			if (srcMesh->mVertices)
			{
				dstMesh->mVertices = new aiVector3D[dstMesh->mNumVertices];
				std::memcpy(dstMesh->mVertices, srcMesh->mVertices, sizeof(aiVector3D) * dstMesh->mNumVertices);
			}

			if (srcMesh->mNormals)
			{
				dstMesh->mNormals = new aiVector3D[dstMesh->mNumVertices];
				std::memcpy(dstMesh->mNormals, srcMesh->mNormals, sizeof(aiVector3D) * dstMesh->mNumVertices);
			}

			if (srcMesh->mTangents)
			{
				dstMesh->mTangents = new aiVector3D[dstMesh->mNumVertices];
				std::memcpy(dstMesh->mTangents, srcMesh->mTangents, sizeof(aiVector3D) * dstMesh->mNumVertices);
			}

			if (srcMesh->mBitangents)
			{
				dstMesh->mBitangents = new aiVector3D[dstMesh->mNumVertices];
				std::memcpy(dstMesh->mBitangents, srcMesh->mBitangents, sizeof(aiVector3D) * dstMesh->mNumVertices);
			}

			for (int c = 0; c < AI_MAX_NUMBER_OF_COLOR_SETS; ++c)
			{
				if (srcMesh->HasVertexColors(c))
				{
					dstMesh->mColors[c] = new aiColor4D[dstMesh->mNumVertices];
					std::memcpy(dstMesh->mColors[c], srcMesh->mColors[c], sizeof(aiColor4D) * dstMesh->mNumVertices);
				}
			}

			for (int t = 0; t < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++t)
			{
				if (srcMesh->HasTextureCoords(t))
				{
					dstMesh->mNumUVComponents[t] = srcMesh->mNumUVComponents[t];
					dstMesh->mTextureCoords[t] = new aiVector3D[dstMesh->mNumVertices];
					std::memcpy(dstMesh->mTextureCoords[t], srcMesh->mTextureCoords[t], sizeof(aiVector3D) * dstMesh->mNumVertices);
				}
			}

			dstMesh->mNumBones = srcMesh->mNumBones;
			if (srcMesh->mNumBones > 0 && srcMesh->mBones)
			{
				dstMesh->mBones = new aiBone*[srcMesh->mNumBones];
				for (unsigned int boneIndex = 0; boneIndex < srcMesh->mNumBones; ++boneIndex)
					dstMesh->mBones[boneIndex] = cloneBone(srcMesh->mBones[boneIndex]);
			}

			dstMesh->mNumAnimMeshes = srcMesh->mNumAnimMeshes;
			if (srcMesh->mNumAnimMeshes > 0 && srcMesh->mAnimMeshes)
			{
				dstMesh->mAnimMeshes = new aiAnimMesh*[srcMesh->mNumAnimMeshes];
				for (unsigned int animMeshIndex = 0; animMeshIndex < srcMesh->mNumAnimMeshes; ++animMeshIndex)
					dstMesh->mAnimMeshes[animMeshIndex] = cloneAnimMesh(srcMesh->mAnimMeshes[animMeshIndex]);
			}

			// Copy faces
			dstMesh->mFaces = new aiFace[dstMesh->mNumFaces];
			for (unsigned int j = 0; j < dstMesh->mNumFaces; ++j)
			{
				const aiFace& srcFace = srcMesh->mFaces[j];
				aiFace& dstFace = dstMesh->mFaces[j];

				dstFace.mNumIndices = srcFace.mNumIndices;
				dstFace.mIndices = new unsigned int[dstFace.mNumIndices];
				std::memcpy(dstFace.mIndices, srcFace.mIndices, sizeof(unsigned int) * dstFace.mNumIndices);
			}

			copy->mMeshes[i] = dstMesh;
		}
	}

	// ---- Copy Materials ----
	copy->mNumMaterials = source->mNumMaterials;
	if (copy->mNumMaterials > 0)
	{
		copy->mMaterials = new aiMaterial * [copy->mNumMaterials];
		for (unsigned int i = 0; i < copy->mNumMaterials; ++i)
		{
			const aiMaterial* src = source->mMaterials[i];
			aiMaterial* dest = new aiMaterial();
			dest->mNumAllocated = src->mNumAllocated;
			dest->mNumProperties = src->mNumProperties;

			if (src->mNumProperties > 0)
			{
				dest->mProperties = new aiMaterialProperty * [src->mNumProperties];
				for (unsigned int p = 0; p < src->mNumProperties; ++p)
				{
					const aiMaterialProperty* sprop = src->mProperties[p];
					aiMaterialProperty* prop = new aiMaterialProperty();

					prop->mKey = sprop->mKey;
					prop->mSemantic = sprop->mSemantic;
					prop->mIndex = sprop->mIndex;
					prop->mType = sprop->mType;
					prop->mDataLength = sprop->mDataLength;

					if (sprop->mDataLength > 0 && sprop->mData)
					{
						prop->mData = new char[sprop->mDataLength];
						std::memcpy(prop->mData, sprop->mData, sprop->mDataLength);
					}
					else
					{
						prop->mData = nullptr;
					}

					dest->mProperties[p] = prop;
				}
			}
			else
			{
				dest->mProperties = nullptr;
			}

			copy->mMaterials[i] = dest;
		}
	}

	// ---- Copy Embedded Textures ----
	copy->mNumTextures = source->mNumTextures;
	if (copy->mNumTextures > 0)
	{
		copy->mTextures = new aiTexture * [copy->mNumTextures];

		// Initialize all pointers to nullptr for safe cleanup
		for (unsigned int i = 0; i < copy->mNumTextures; ++i)
		{
			copy->mTextures[i] = nullptr;
		}

		try
		{
			for (unsigned int i = 0; i < copy->mNumTextures; ++i)
			{
				const aiTexture* srcTex = source->mTextures[i];
				if (!srcTex)
				{
					continue; // Skip null source textures
				}

				aiTexture* dstTex = new aiTexture();
				dstTex->mWidth = srcTex->mWidth;
				dstTex->mHeight = srcTex->mHeight;
				dstTex->pcData = nullptr; // Initialize to safe state

				// Safe format hint copying
				dstTex->achFormatHint[0] = '\0';
				std::memcpy(dstTex->achFormatHint, srcTex->achFormatHint, sizeof(dstTex->achFormatHint));
				dstTex->achFormatHint[sizeof(dstTex->achFormatHint) - 1] = '\0'; // Ensure null termination

				// Texture data copying with strong guards
				if (srcTex->pcData && srcTex->mWidth > 0)
				{
					size_t width = static_cast<size_t>(srcTex->mWidth);
					size_t height = static_cast<size_t>(srcTex->mHeight);
					size_t size = (height == 0) ? width : width * height;

					// Sanity checks for reasonable texture size
					if (size > 0 && size < SIZE_MAX / sizeof(aiTexel) && size < 100000000) // 100M texel limit
					{
						size_t byteSize = size * sizeof(aiTexel);
						dstTex->pcData = new(std::nothrow) aiTexel[size];

						if (dstTex->pcData)
						{
							// Strong guard - attempt safe copy
							if (!SafeCopyTextureData(dstTex->pcData, srcTex->pcData, byteSize))
							{
								// Copy failed due to invalid source data - clean up and continue
								delete[] dstTex->pcData;
								dstTex->pcData = nullptr;
								printf("Warning: Skipping corrupted texture %u in model (invalid source data)\n", i);
							}
						}
						else
						{
							printf("Warning: Failed to allocate memory for texture %u\n", i);
						}
					}
					else
					{
						dstTex->pcData = nullptr;
						printf("Warning: Invalid texture size (%zu) for texture %u, skipping\n", size, i);
					}
				}

				copy->mTextures[i] = dstTex;
			}
		}
		catch (...)
		{
			// Cleanup on any exception during the loop
			for (unsigned int i = 0; i < copy->mNumTextures; ++i)
			{
				if (copy->mTextures[i])
				{
					delete[] copy->mTextures[i]->pcData;
					delete copy->mTextures[i];
				}
			}
			delete[] copy->mTextures;
			copy->mTextures = nullptr;
			copy->mNumTextures = 0;
			throw; // Re-throw the exception
		}
	}

	// ---- Copy Animations ----
	copy->mNumAnimations = source->mNumAnimations;
	if (copy->mNumAnimations > 0)
	{
		copy->mAnimations = new aiAnimation*[copy->mNumAnimations];
		for (unsigned int animationIndex = 0; animationIndex < copy->mNumAnimations; ++animationIndex)
			copy->mAnimations[animationIndex] = cloneAnimation(source->mAnimations[animationIndex]);
	}

	// ---- Copy Node Hierarchy ----
	std::function<aiNode* (const aiNode*)> cloneNode = [&](const aiNode* src) -> aiNode* {
		aiNode* dst = new aiNode();
		dst->mName = src->mName;
		dst->mTransformation = src->mTransformation;
		dst->mNumMeshes = src->mNumMeshes;
		dst->mNumChildren = src->mNumChildren;

		if (src->mNumMeshes > 0)
		{
			dst->mMeshes = new unsigned int[dst->mNumMeshes];
			std::memcpy(dst->mMeshes, src->mMeshes, sizeof(unsigned int) * dst->mNumMeshes);
		}

		if (src->mNumChildren > 0)
		{
			dst->mChildren = new aiNode * [dst->mNumChildren];
			for (unsigned int i = 0; i < dst->mNumChildren; ++i)
			{
				dst->mChildren[i] = cloneNode(src->mChildren[i]);
				dst->mChildren[i]->mParent = dst;
			}
		}

		return dst;
		};

	copy->mRootNode = cloneNode(source->mRootNode);

	return copy;
}


aiMaterial* SceneUtils::copyMaterial(const aiMaterial* src)
{
	if (!src) return nullptr;

	aiMaterial* dest = new aiMaterial();
	dest->mNumAllocated = src->mNumAllocated;
	dest->mNumProperties = src->mNumProperties;

	if (src->mNumProperties > 0)
	{
		dest->mProperties = new aiMaterialProperty * [src->mNumProperties];
		for (unsigned int i = 0; i < src->mNumProperties; ++i)
		{
			const aiMaterialProperty* sprop = src->mProperties[i];
			if (!sprop) continue;

			aiMaterialProperty* prop = new aiMaterialProperty();
			prop->mKey = sprop->mKey;
			prop->mSemantic = sprop->mSemantic;
			prop->mIndex = sprop->mIndex;
			prop->mType = sprop->mType;
			prop->mDataLength = sprop->mDataLength;

			if (sprop->mDataLength > 0 && sprop->mData)
			{
				prop->mData = new char[sprop->mDataLength];
				std::memcpy(prop->mData, sprop->mData, sprop->mDataLength);
			}
			else
			{
				prop->mData = nullptr;
			}

			dest->mProperties[i] = prop;
		}
	}
	else
	{
		dest->mProperties = nullptr;
	}

	return dest;
}



void SceneUtils::deleteScene(aiScene* scene)
{
	if (!scene)
		return;

	// Assimp scene objects own their nested allocations and release them through
	// their destructors. Manual recursive teardown here double-frees animated
	// payloads such as aiBone::mWeights and aiAnimation channels.
	delete scene;
}

void SceneUtils::deleteNodeRecursive(aiNode* node)
{
	if (!node) return;

	try
	{
		// Delete children first
		if (node->mChildren)
		{
			for (unsigned int i = 0; i < node->mNumChildren; ++i)
			{
				if (node->mChildren[i])
				{
					deleteNodeRecursive(node->mChildren[i]);
					node->mChildren[i] = nullptr;
				}
			}
			delete[] node->mChildren;
			node->mChildren = nullptr;
		}
		node->mNumChildren = 0;

		// Delete mesh indices array
		delete[] node->mMeshes;
		node->mMeshes = nullptr;
		node->mNumMeshes = 0;

		// Delete metadata
		if (node->mMetaData)
		{
			delete node->mMetaData;
			node->mMetaData = nullptr;
		}

		// Finally delete the node itself
		delete node;

	}
	catch (...)
	{
		// Even if something goes wrong, try to delete the node
		delete node;
	}
}

glm::mat4 SceneUtils::aiMatrixToGlm(const aiMatrix4x4& from)
{
	return glm::mat4(
		from.a1, from.b1, from.c1, from.d1,
		from.a2, from.b2, from.c2, from.d2,
		from.a3, from.b3, from.c3, from.d3,
		from.a4, from.b4, from.c4, from.d4
	);
}

aiMatrix4x4 SceneUtils::glmToAiMatrix(const glm::mat4& mat)
{
	aiMatrix4x4 result;
	result.a1 = mat[0][0]; result.a2 = mat[1][0]; result.a3 = mat[2][0]; result.a4 = mat[3][0];
	result.b1 = mat[0][1]; result.b2 = mat[1][1]; result.b3 = mat[2][1]; result.b4 = mat[3][1];
	result.c1 = mat[0][2]; result.c2 = mat[1][2]; result.c3 = mat[2][2]; result.c4 = mat[3][2];
	result.d1 = mat[0][3]; result.d2 = mat[1][3]; result.d3 = mat[2][3]; result.d4 = mat[3][3];
	return result;
}
