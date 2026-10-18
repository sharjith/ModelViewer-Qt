#pragma once
#include <assimp/scene.h>
#include <limits>
#include <stdexcept>
#include <vector>

// Traversal work counts occurrences, even when several nodes share one aiMesh.
inline int countSceneMeshOccurrences(const aiNode* root)
{
    std::vector<const aiNode*> pending;
    if (root) pending.push_back(root);
    unsigned long long total = 0;
    while (!pending.empty()) {
        const aiNode* node = pending.back(); pending.pop_back();
        total += node->mNumMeshes;
        if (total > std::numeric_limits<int>::max()) throw std::runtime_error("Scene has too many mesh occurrences");
        for (unsigned i=0;i<node->mNumChildren;++i) pending.push_back(node->mChildren[i]);
    }
    return static_cast<int>(total);
}
