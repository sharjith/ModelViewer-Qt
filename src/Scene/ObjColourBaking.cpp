#include "ObjColourBaking.h"

#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
struct Rgba
{
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
};

uint32_t quantise(const Rgba& c, int bits)
{
    const int levels = (1 << bits) - 1;
    const auto q = [&](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * levels)); };
    uint32_t key = 0;
    key = (key << 8) | q(c.r);
    key = (key << 8) | q(c.g);
    key = (key << 8) | q(c.b);
    key = (key << 8) | q(c.a);
    return key;
}

bool isWhite(const aiColor4D& c)
{
    constexpr float eps = 1e-4f;
    return std::fabs(c.r - 1.f) < eps && std::fabs(c.g - 1.f) < eps && std::fabs(c.b - 1.f) < eps && std::fabs(c.a - 1.f) < eps;
}

// A mesh this baking may change: triangles only, no morph targets or bones, and at least one vertex that is not white.
bool hasVertexColours(const aiMesh* mesh)
{
    if (!mesh || !mesh->HasVertexColors(0) || mesh->mNumFaces == 0 || mesh->mNumVertices == 0)
        return false;
    if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE || mesh->mNumAnimMeshes != 0 || mesh->mNumBones != 0)
        return false;
    for (unsigned i = 0; i < mesh->mNumFaces; ++i)
        if (mesh->mFaces[i].mNumIndices != 3)
            return false;
    for (unsigned i = 0; i < mesh->mNumVertices; ++i)
        if (!isWhite(mesh->mColors[0][i]))
            return true;
    return false;
}

// A copy of `source` (may be null) whose diffuse and base colour are `colour`; its name gets a suffix naming the colour, so the names of the
// copies of one material differ and the MTL patch can still find the original (see AssImpMeshExporter::patchMtlWithPbrExtensions).
aiMaterial* colouredCopy(const aiMaterial* source, const Rgba& colour)
{
    auto* copy = new aiMaterial();
    if (source)
        aiMaterial::CopyPropertyList(copy, source);

    const aiColor3D rgb(colour.r, colour.g, colour.b);
    copy->AddProperty(&rgb, 1, AI_MATKEY_COLOR_DIFFUSE);
    copy->AddProperty(&rgb, 1, AI_MATKEY_BASE_COLOR);
    if (colour.a < 0.999f)
    {
        const float opacity = colour.a;
        copy->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
    }

    aiString name;
    if (!source || source->Get(AI_MATKEY_NAME, name) != AI_SUCCESS || name.length == 0)
        name = aiString("material");
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_vc%02X%02X%02X%02X",
        static_cast<unsigned>(std::lround(std::clamp(colour.r, 0.f, 1.f) * 255.f)),
        static_cast<unsigned>(std::lround(std::clamp(colour.g, 0.f, 1.f) * 255.f)),
        static_cast<unsigned>(std::lround(std::clamp(colour.b, 0.f, 1.f) * 255.f)),
        static_cast<unsigned>(std::lround(std::clamp(colour.a, 0.f, 1.f) * 255.f)));
    name.Append(suffix);
    copy->AddProperty(&name, AI_MATKEY_NAME);
    return copy;
}

// The faces `faces` of `source` as a mesh of its own: only the vertices they use, every attribute but the colours copied.
aiMesh* subMesh(const aiMesh* source, const std::vector<unsigned>& faces, unsigned materialIndex, const std::string& name)
{
    std::vector<int> remap(source->mNumVertices, -1);
    std::vector<unsigned> used;
    for (unsigned f : faces)
        for (unsigned k = 0; k < 3; ++k)
        {
            const unsigned v = source->mFaces[f].mIndices[k];
            if (remap[v] < 0)
            {
                remap[v] = static_cast<int>(used.size());
                used.push_back(v);
            }
        }

    auto* mesh = new aiMesh();
    mesh->mName = aiString(name);
    mesh->mMaterialIndex = materialIndex;
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    mesh->mNumVertices = static_cast<unsigned>(used.size());
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    if (source->HasNormals()) mesh->mNormals = new aiVector3D[mesh->mNumVertices];
    if (source->HasTangentsAndBitangents())
    {
        mesh->mTangents = new aiVector3D[mesh->mNumVertices];
        mesh->mBitangents = new aiVector3D[mesh->mNumVertices];
    }
    for (unsigned ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
        if (source->HasTextureCoords(ch))
        {
            mesh->mTextureCoords[ch] = new aiVector3D[mesh->mNumVertices];
            mesh->mNumUVComponents[ch] = source->mNumUVComponents[ch];
        }
    for (unsigned i = 0; i < mesh->mNumVertices; ++i)
    {
        const unsigned v = used[i];
        mesh->mVertices[i] = source->mVertices[v];
        if (mesh->mNormals) mesh->mNormals[i] = source->mNormals[v];
        if (mesh->mTangents)
        {
            mesh->mTangents[i] = source->mTangents[v];
            mesh->mBitangents[i] = source->mBitangents[v];
        }
        for (unsigned ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
            if (mesh->mTextureCoords[ch]) mesh->mTextureCoords[ch][i] = source->mTextureCoords[ch][v];
    }
    mesh->mNumFaces = static_cast<unsigned>(faces.size());
    mesh->mFaces = new aiFace[mesh->mNumFaces];
    for (unsigned i = 0; i < mesh->mNumFaces; ++i)
    {
        aiFace& face = mesh->mFaces[i];
        face.mNumIndices = 3;
        face.mIndices = new unsigned[3];
        for (unsigned k = 0; k < 3; ++k)
            face.mIndices[k] = static_cast<unsigned>(remap[source->mFaces[faces[i]].mIndices[k]]);
    }
    return mesh;
}

void remapNodeMeshes(aiNode* node, const std::vector<std::vector<unsigned>>& newIndices)
{
    if (node->mNumMeshes > 0)
    {
        std::vector<unsigned> out;
        for (unsigned i = 0; i < node->mNumMeshes; ++i)
        {
            const unsigned old = node->mMeshes[i];
            if (old < newIndices.size())
                out.insert(out.end(), newIndices[old].begin(), newIndices[old].end());
        }
        delete[] node->mMeshes;
        node->mMeshes = nullptr;
        node->mNumMeshes = static_cast<unsigned>(out.size());
        if (!out.empty())
        {
            node->mMeshes = new unsigned[out.size()];
            std::copy(out.begin(), out.end(), node->mMeshes);
        }
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i)
        remapNodeMeshes(node->mChildren[i], newIndices);
}

// An MTL file is read by name: two materials with the same name but different colours become two "newmtl" blocks of one name, and an OBJ reader
// keeps the first, so every mesh that uses the second takes the colour of the first. Give each later material of a name a "_m<index>" suffix
// (the MTL patch ignores that suffix, see AssImpMeshExporter::patchMtlWithPbrExtensions).
void uniquifyMaterialNames(aiScene* scene)
{
    std::unordered_map<std::string, unsigned> seen;
    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
    {
        aiMaterial* material = scene->mMaterials[i];
        if (!material)
            continue;
        aiString name;
        if (material->Get(AI_MATKEY_NAME, name) != AI_SUCCESS || name.length == 0)
            continue;   // unnamed: the OBJ writer names it "$Material_<index>", which is unique
        std::string text(name.C_Str());
        if (seen.emplace(text, i).second)
            continue;
        std::string unique;
        for (unsigned n = i; ; ++n)
        {
            unique = text + "_m" + std::to_string(n);
            if (seen.emplace(unique, i).second)
                break;
        }
        const aiString renamed(unique);
        material->AddProperty(&renamed, AI_MATKEY_NAME);
    }
}
} // namespace

namespace ObjColourBaking
{
Result bake(aiScene* scene)
{
    Result result;
    if (!scene || scene->mNumMeshes == 0)
        return result;

    std::vector<aiMesh*> meshes;                       // the new mesh list
    std::vector<aiMaterial*> materials(scene->mMaterials, scene->mMaterials + scene->mNumMaterials);
    std::vector<std::vector<unsigned>> newIndices(scene->mNumMeshes);
    std::vector<aiMesh*> replaced;                     // old meshes that were split and are deleted
    std::map<std::pair<unsigned, uint32_t>, unsigned> materialCache;   // (source material, colour) -> material index

    const auto materialFor = [&](unsigned sourceIndex, const Rgba& colour, uint32_t key) {
        const auto cached = materialCache.find({ sourceIndex, key });
        if (cached != materialCache.end())
            return cached->second;
        const aiMaterial* source = sourceIndex < scene->mNumMaterials ? scene->mMaterials[sourceIndex] : nullptr;
        materials.push_back(colouredCopy(source, colour));
        ++result.materialsCreated;
        const unsigned index = static_cast<unsigned>(materials.size() - 1);
        materialCache.emplace(std::make_pair(sourceIndex, key), index);
        return index;
    };

    for (unsigned m = 0; m < scene->mNumMeshes; ++m)
    {
        aiMesh* mesh = scene->mMeshes[m];
        if (!hasVertexColours(mesh))
        {
            newIndices[m].push_back(static_cast<unsigned>(meshes.size()));
            meshes.push_back(mesh);
            continue;
        }

        // the colour of each triangle: the mean of its three vertices
        std::vector<Rgba> faceColour(mesh->mNumFaces);
        for (unsigned f = 0; f < mesh->mNumFaces; ++f)
        {
            Rgba sum{ 0.f, 0.f, 0.f, 0.f };
            for (unsigned k = 0; k < 3; ++k)
            {
                const aiColor4D& c = mesh->mColors[0][mesh->mFaces[f].mIndices[k]];
                sum.r += c.r; sum.g += c.g; sum.b += c.b; sum.a += c.a;
            }
            faceColour[f] = { sum.r / 3.f, sum.g / 3.f, sum.b / 3.f, sum.a / 3.f };
        }

        // group the triangles by colour: 8 bits per channel, fewer when there are more than 256 colours
        std::unordered_map<uint32_t, std::vector<unsigned>> groups;
        for (int bits : { 8, 6, 5, 4, 3 })
        {
            groups.clear();
            for (unsigned f = 0; f < mesh->mNumFaces; ++f)
                groups[quantise(faceColour[f], bits)].push_back(f);
            if (groups.size() <= 256)
                break;
        }

        // the group colour is the mean of its triangles' colours; groups in order of their first triangle, so the output is deterministic
        struct Group { std::vector<unsigned> faces; Rgba colour; uint32_t key = 0; };
        std::vector<Group> ordered;
        ordered.reserve(groups.size());
        for (auto& entry : groups)
        {
            Group g;
            g.faces = std::move(entry.second);
            g.key = entry.first;
            Rgba sum{ 0.f, 0.f, 0.f, 0.f };
            for (unsigned f : g.faces)
            {
                sum.r += faceColour[f].r; sum.g += faceColour[f].g; sum.b += faceColour[f].b; sum.a += faceColour[f].a;
            }
            const float n = static_cast<float>(g.faces.size());
            g.colour = { sum.r / n, sum.g / n, sum.b / n, sum.a / n };
            ordered.push_back(std::move(g));
        }
        std::sort(ordered.begin(), ordered.end(), [](const Group& a, const Group& b) { return a.faces.front() < b.faces.front(); });

        ++result.meshesChanged;
        if (ordered.size() == 1)
        {
            // one colour: the mesh keeps its geometry and gets a coloured copy of its material
            mesh->mMaterialIndex = materialFor(mesh->mMaterialIndex, ordered.front().colour, ordered.front().key);
            newIndices[m].push_back(static_cast<unsigned>(meshes.size()));
            meshes.push_back(mesh);
            continue;
        }

        const std::string baseName(mesh->mName.C_Str());
        for (size_t g = 0; g < ordered.size(); ++g)
        {
            aiMesh* part = subMesh(mesh, ordered[g].faces, materialFor(mesh->mMaterialIndex, ordered[g].colour, ordered[g].key),
                baseName + "_c" + std::to_string(g + 1));
            newIndices[m].push_back(static_cast<unsigned>(meshes.size()));
            meshes.push_back(part);
            ++result.meshesAdded;
        }
        replaced.push_back(mesh);
    }

    if (result.meshesChanged == 0)
    {
        uniquifyMaterialNames(scene);
        return result;
    }

    // install the new lists
    delete[] scene->mMeshes;
    scene->mNumMeshes = static_cast<unsigned>(meshes.size());
    scene->mMeshes = new aiMesh*[meshes.size()];
    std::copy(meshes.begin(), meshes.end(), scene->mMeshes);
    delete[] scene->mMaterials;
    scene->mNumMaterials = static_cast<unsigned>(materials.size());
    scene->mMaterials = new aiMaterial*[materials.size()];
    std::copy(materials.begin(), materials.end(), scene->mMaterials);
    for (aiMesh* old : replaced)
        delete old;
    if (scene->mRootNode)
        remapNodeMeshes(scene->mRootNode, newIndices);
    uniquifyMaterialNames(scene);
    return result;
}
}
