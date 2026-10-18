#include "MeshChartSegmenter.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <glm/glm.hpp>
#include <cmath>

// Hashable edge
struct Edge
{
    uint32_t a, b;
    Edge(uint32_t a_, uint32_t b_)
    {
        a = std::min(a_, b_);
        b = std::max(a_, b_);
    }
    bool operator==(const Edge& other) const
    {
        return a == other.a && b == other.b;
    }
};

namespace std
{
    template <>
    struct hash<Edge>
    {
        std::size_t operator()(const Edge& e) const
        {
            return (std::hash<uint32_t>()(e.a) << 1) ^ std::hash<uint32_t>()(e.b);
        }
    };
}

static glm::vec3 toVec3(const aiVector3D& v)
{
    return glm::vec3(v.x, v.y, v.z);
}

static glm::vec3 computeFaceNormal(const aiMesh* mesh, unsigned int faceIndex)
{
    const aiFace& face = mesh->mFaces[faceIndex];
    glm::vec3 a = toVec3(mesh->mVertices[face.mIndices[0]]);
    glm::vec3 b = toVec3(mesh->mVertices[face.mIndices[1]]);
    glm::vec3 c = toVec3(mesh->mVertices[face.mIndices[2]]);
    return glm::normalize(glm::cross(b - a, c - a));
}

std::vector<MeshChart> splitByNormalDeviation(const aiMesh* mesh, float maxAngleDegrees)
{
    std::vector<MeshChart> charts;
    if (!mesh || mesh->mNumFaces == 0) return charts;

    const float angleThreshold = glm::radians(maxAngleDegrees);
    const float cosThreshold = std::cos(angleThreshold);

    // Precompute face normals
    std::vector<glm::vec3> faceNormals(mesh->mNumFaces);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        faceNormals[i] = computeFaceNormal(mesh, i);

    // Build edge-to-face map
    std::unordered_map<Edge, std::vector<unsigned int>> edgeToFaces;
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
    {
        const aiFace& f = mesh->mFaces[i];
        edgeToFaces[Edge(f.mIndices[0], f.mIndices[1])].push_back(i);
        edgeToFaces[Edge(f.mIndices[1], f.mIndices[2])].push_back(i);
        edgeToFaces[Edge(f.mIndices[2], f.mIndices[0])].push_back(i);
    }

    std::vector<bool> visited(mesh->mNumFaces, false);

    for (unsigned int startFace = 0; startFace < mesh->mNumFaces; ++startFace)
    {
        if (visited[startFace]) continue;

        MeshChart chart;
        std::unordered_map<unsigned int, unsigned int> globalToLocal;
        std::queue<unsigned int> q;
        q.push(startFace);

        while (!q.empty())
        {
            unsigned int f = q.front(); q.pop();
            if (visited[f]) continue;
            visited[f] = true;

            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < 3; ++j)
            {
                uint32_t vi = face.mIndices[j];
                if (globalToLocal.find(vi) == globalToLocal.end())
                {
                    globalToLocal[vi] = static_cast<uint32_t>(chart.positions.size());
                    chart.positions.push_back(toVec3(mesh->mVertices[vi]));
                }
                chart.indices.push_back(globalToLocal[vi]);
            }

            // Explore adjacent faces via shared edges
            for (int e = 0; e < 3; ++e)
            {
                Edge edge(face.mIndices[e], face.mIndices[(e + 1) % 3]);
                for (unsigned int neighbor : edgeToFaces[edge])
                {
                    if (!visited[neighbor] &&
                        glm::dot(faceNormals[f], faceNormals[neighbor]) >= cosThreshold)
                    {
                        q.push(neighbor);
                    }
                }
            }
        }

        charts.push_back(std::move(chart));
    }

    return charts;
}
