#include "SceneToU3d.h"

#include <assimp/material.h>
#include <assimp/mesh.h>

#include <QHash>
#include <cmath>
#include <QString>

namespace Pdf3D
{
namespace
{

QMatrix4x4 toQt(const aiMatrix4x4& m)
{
    // aiMatrix4x4 is row-major (a1..a4 is the first row); QMatrix4x4's value constructor also takes row-major order.
    return QMatrix4x4(m.a1, m.a2, m.a3, m.a4,
                      m.b1, m.b2, m.b3, m.b4,
                      m.c1, m.c2, m.c3, m.c4,
                      m.d1, m.d2, m.d3, m.d4);
}

// ASCII letters, digits, '_' and '-' only; everything else becomes '_'.
QString asciiName(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
    {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u == '-';
        out.append(ok ? c : QLatin1Char('_'));
    }
    if (out.size() > 48)
        out.truncate(48);
    return out;
}

struct Builder
{
    const aiScene*        scene = nullptr;
    U3dScene              out;
    QHash<unsigned, int>  meshMap;      // aiMesh index -> U3dMesh index (-1 when the mesh is unusable)
    QHash<unsigned, int>  materialMap;  // aiMaterial index -> U3dMaterial index
    QHash<QString, int>   variantMap;   // derived (tinted / vertex colour) materials
    int                   nodeCounter = 0;

    int materialFor(unsigned aiIndex)
    {
        auto it = materialMap.constFind(aiIndex);
        if (it != materialMap.constEnd())
            return it.value();

        U3dMaterial mat;
        mat.name = QStringLiteral("Material%1").arg(aiIndex);
        if (aiIndex < scene->mNumMaterials)
        {
            const aiMaterial* am = scene->mMaterials[aiIndex];
            aiString name;
            if (am->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS)
                mat.name = QString::fromUtf8(name.C_Str());

            aiColor4D c;
            if (am->Get(AI_MATKEY_BASE_COLOR, c) == aiReturn_SUCCESS || am->Get(AI_MATKEY_COLOR_DIFFUSE, c) == aiReturn_SUCCESS)
                mat.diffuse = QVector3D(c.r, c.g, c.b);

            aiColor4D e;
            if (am->Get(AI_MATKEY_COLOR_EMISSIVE, e) == aiReturn_SUCCESS)
            {
                // U3D emissive adds light on top of the shading; keep it small so parts stay readable
                mat.emissive = QVector3D(qBound(0.0f, e.r, 1.0f), qBound(0.0f, e.g, 1.0f), qBound(0.0f, e.b, 1.0f)) * 0.5f;
            }

            float opacity = 1.0f;
            if (am->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS)
                mat.opacity = qBound(0.0f, opacity, 1.0f);
        }

        const int index = out.materials.size();
        out.materials.push_back(mat);
        materialMap.insert(aiIndex, index);
        return index;
    }

    // A material derived from material `base`: either switched to per-vertex colours, or tinted by one uniform mesh colour
    // (vertex colour times base colour, the glTF COLOR_0 rule). Variants are shared by key.
    int materialVariant(int base, bool vertexColors, const aiColor4D& tint)
    {
        const QString key = QStringLiteral("%1|%2|%3,%4,%5,%6").arg(base).arg(vertexColors ? 1 : 0)
                                .arg(tint.r, 0, 'f', 4).arg(tint.g, 0, 'f', 4).arg(tint.b, 0, 'f', 4).arg(tint.a, 0, 'f', 4);
        auto it = variantMap.constFind(key);
        if (it != variantMap.constEnd())
            return it.value();

        U3dMaterial mat = out.materials[base];   // copy: push_back below may reallocate
        if (vertexColors)
        {
            mat.vertexColors = true;
            mat.name += QStringLiteral(" (vertex colours)");
        }
        else
        {
            mat.diffuse = QVector3D(mat.diffuse.x() * tint.r, mat.diffuse.y() * tint.g, mat.diffuse.z() * tint.b);
            mat.opacity = qBound(0.0f, mat.opacity * tint.a, 1.0f);
        }
        const int index = out.materials.size();
        out.materials.push_back(mat);
        variantMap.insert(key, index);
        return index;
    }

    int meshFor(unsigned aiIndex)
    {
        auto it = meshMap.constFind(aiIndex);
        if (it != meshMap.constEnd())
            return it.value();

        int result = -1;
        if (aiIndex < scene->mNumMeshes)
        {
            const aiMesh* am = scene->mMeshes[aiIndex];
            if (am && am->mNumVertices >= 3 && am->mNumFaces >= 1 && am->mVertices)
            {
                U3dMesh m;
                m.name = QStringLiteral("M%1").arg(out.meshes.size());
                m.positions.resize(static_cast<int>(am->mNumVertices) * 3);
                for (unsigned i = 0; i < am->mNumVertices; ++i)
                {
                    m.positions[3 * i] = am->mVertices[i].x;
                    m.positions[3 * i + 1] = am->mVertices[i].y;
                    m.positions[3 * i + 2] = am->mVertices[i].z;
                }
                if (am->mNormals)
                {
                    m.normals.resize(static_cast<int>(am->mNumVertices) * 3);
                    for (unsigned i = 0; i < am->mNumVertices; ++i)
                    {
                        m.normals[3 * i] = am->mNormals[i].x;
                        m.normals[3 * i + 1] = am->mNormals[i].y;
                        m.normals[3 * i + 2] = am->mNormals[i].z;
                    }
                }
                m.indices.reserve(static_cast<int>(am->mNumFaces) * 3);
                for (unsigned f = 0; f < am->mNumFaces; ++f)
                {
                    const aiFace& face = am->mFaces[f];
                    if (face.mNumIndices != 3)
                        continue;   // points and lines are skipped; polygons are triangulated at import
                    m.indices.push_back(face.mIndices[0]);
                    m.indices.push_back(face.mIndices[1]);
                    m.indices.push_back(face.mIndices[2]);
                }
                if (!m.indices.isEmpty())
                {
                    int material = materialFor(am->mMaterialIndex);

                    // Vertex colours: the viewer draws parts coloured per vertex (CAD imports colour whole parts or single faces
                    // this way). One colour for the whole mesh becomes a tinted material; varying colours stay per vertex.
                    if (am->mColors[0])
                    {
                        const aiColor4D first = am->mColors[0][0];
                        bool uniform = true;
                        for (unsigned i = 1; i < am->mNumVertices && uniform; ++i)
                        {
                            const aiColor4D& c = am->mColors[0][i];
                            uniform = std::fabs(c.r - first.r) < 1.0e-4f && std::fabs(c.g - first.g) < 1.0e-4f &&
                                      std::fabs(c.b - first.b) < 1.0e-4f && std::fabs(c.a - first.a) < 1.0e-4f;
                        }
                        if (!uniform)
                        {
                            const QVector3D base = out.materials[material].diffuse;
                            m.colors.resize(static_cast<int>(am->mNumVertices) * 4);
                            for (unsigned i = 0; i < am->mNumVertices; ++i)
                            {
                                const aiColor4D& c = am->mColors[0][i];
                                m.colors[4 * i] = c.r * base.x();
                                m.colors[4 * i + 1] = c.g * base.y();
                                m.colors[4 * i + 2] = c.b * base.z();
                                m.colors[4 * i + 3] = c.a;
                            }
                            material = materialVariant(material, true, aiColor4D(1, 1, 1, 1));
                        }
                        else if (first.r != 1.0f || first.g != 1.0f || first.b != 1.0f || first.a != 1.0f)
                        {
                            material = materialVariant(material, false, first);
                        }
                    }
                    m.materialIndex = material;
                    result = out.meshes.size();
                    out.meshes.push_back(m);
                }
            }
        }
        meshMap.insert(aiIndex, result);
        return result;
    }

    void visit(const aiNode* node, const QMatrix4x4& parentWorld)
    {
        if (!node)
            return;
        const QMatrix4x4 world = parentWorld * toQt(node->mTransformation);

        for (unsigned i = 0; i < node->mNumMeshes; ++i)
        {
            const int mesh = meshFor(node->mMeshes[i]);
            if (mesh < 0)
                continue;
            U3dNode n;
            const QString display = QString::fromUtf8(node->mName.C_Str());
            n.displayName = display;
            n.name = QStringLiteral("N%1_%2").arg(nodeCounter++, 4, 10, QLatin1Char('0')).arg(asciiName(display));
            n.meshIndex = mesh;
            n.world = world;
            out.nodes.push_back(n);
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c)
            visit(node->mChildren[c], world);
    }
};

} // namespace

U3dScene SceneToU3d::build(const aiScene* scene, const QVector<QMatrix4x4>& rootChildPreTransforms)
{
    Builder b;
    b.scene = scene;
    if (scene && scene->mRootNode)
    {
        const aiNode* root = scene->mRootNode;
        const QMatrix4x4 rootWorld = toQt(root->mTransformation);
        for (unsigned i = 0; i < root->mNumMeshes; ++i)   // meshes directly on the root (not expected, handled for completeness)
        {
            const int mesh = b.meshFor(root->mMeshes[i]);
            if (mesh < 0)
                continue;
            U3dNode n;
            n.displayName = QString::fromUtf8(root->mName.C_Str());
            n.name = QStringLiteral("N%1_%2").arg(b.nodeCounter++, 4, 10, QLatin1Char('0')).arg(asciiName(n.displayName));
            n.meshIndex = mesh;
            n.world = rootWorld;
            b.out.nodes.push_back(n);
        }
        for (unsigned c = 0; c < root->mNumChildren; ++c)
        {
            const QMatrix4x4 pre = (static_cast<int>(c) < rootChildPreTransforms.size()) ? rootChildPreTransforms[static_cast<int>(c)] : QMatrix4x4();
            b.visit(root->mChildren[c], rootWorld * pre);
        }
    }
    return b.out;
}

} // namespace Pdf3D
