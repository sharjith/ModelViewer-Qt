#include "GltfWriter.h"

#include "ExportSession.h"
#include "GltfBinaryBuffer.h"

#include <assimp/scene.h>

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

#include <config.h>

namespace
{
constexpr int kArrayBuffer = 34962;        // bufferView target: vertex attributes
constexpr int kElementArrayBuffer = 34963; // bufferView target: indices
constexpr int kComponentFloat = 5126;
constexpr int kComponentUShort = 5123;
constexpr int kComponentUInt = 5125;
constexpr unsigned int kMaxUvChannels = 4;

QJsonValue num(double value) { return QJsonValue(value); }

bool nearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= 1e-5f * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
}

bool matricesClose(const aiMatrix4x4& a, const aiMatrix4x4& b)
{
    const ai_real* pa = &a.a1;
    const ai_real* pb = &b.a1;
    for (int i = 0; i < 16; ++i)
        if (!nearlyEqual(static_cast<float>(pa[i]), static_cast<float>(pb[i])))
            return false;
    return true;
}

bool isWhiteOpaque(const aiColor4D& c)
{
    return std::fabs(c.r - 1.0f) < 1e-6f && std::fabs(c.g - 1.0f) < 1e-6f
        && std::fabs(c.b - 1.0f) < 1e-6f && std::fabs(c.a - 1.0f) < 1e-6f;
}

QString toQString(const aiString& s)
{
    return QString::fromUtf8(s.C_Str(), static_cast<int>(s.length));
}

// Assimp names a glTF node that has no name "nodes[N]" (N = its index in that file). That is a placeholder, not a name, and
// it must not be written: the importer reads "nodes[N]" as "glTF node number N" (name-based primitive-mode fallback), which is
// a different node in the new file, so re-importing the export would pair meshes with the wrong primitives.
bool isGeneratedNodeName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^nodes\\[\\d+\\]$"));
    return pattern.match(name).hasMatch();
}

// The local transform of a node as glTF translation / rotation / scale; as a matrix when it cannot be split into those exactly
// (shear). Identity writes nothing, and default-valued parts are left out.
void writeNodeTransform(const aiMatrix4x4& m, QJsonObject& node)
{
    if (m.IsIdentity())
        return;

    aiVector3D scaling, position;
    aiQuaternion rotation;
    m.Decompose(scaling, rotation, position);
    const aiMatrix4x4 recomposed(scaling, rotation, position);

    const bool finite = std::isfinite(scaling.x) && std::isfinite(scaling.y) && std::isfinite(scaling.z)
        && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
        && std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z) && std::isfinite(rotation.w);

    if (finite && matricesClose(m, recomposed))
    {
        if (position.x != 0.0f || position.y != 0.0f || position.z != 0.0f)
            node.insert(QStringLiteral("translation"), QJsonArray{ num(position.x), num(position.y), num(position.z) });
        if (rotation.x != 0.0f || rotation.y != 0.0f || rotation.z != 0.0f || rotation.w != 1.0f)
            node.insert(QStringLiteral("rotation"), QJsonArray{ num(rotation.x), num(rotation.y), num(rotation.z), num(rotation.w) });
        if (scaling.x != 1.0f || scaling.y != 1.0f || scaling.z != 1.0f)
            node.insert(QStringLiteral("scale"), QJsonArray{ num(scaling.x), num(scaling.y), num(scaling.z) });
        return;
    }

    // glTF matrices are column-major; aiMatrix4x4 is row-major.
    node.insert(QStringLiteral("matrix"), QJsonArray{
        num(m.a1), num(m.b1), num(m.c1), num(m.d1),
        num(m.a2), num(m.b2), num(m.c2), num(m.d2),
        num(m.a3), num(m.b3), num(m.c3), num(m.d3),
        num(m.a4), num(m.b4), num(m.c4), num(m.d4) });
}

// Builds the JSON tables and the binary buffer of one glTF file from an aiScene.
class GltfBuilder
{
public:
    GltfBuilder(const aiScene* scene, ExportSession& session)
        : _scene(scene), _session(session)
    {
    }

    // Creates every node / mesh / accessor; afterwards the JSON object and the binary buffer are ready.
    void build();

    // The glTF JSON object; `binaryUri` is the .bin file name for .gltf output, empty for GLB.
    QJsonObject json(const QString& binaryUri) const;

    const GltfBinaryBuffer& binary() const { return _bin; }
    std::size_t meshCount() const { return _meshes.size(); }
    std::size_t nodeCount() const { return _nodes.size(); }
    std::size_t skippedMeshCount() const { return _skippedMeshes; }

private:
    struct CachedPrimitive
    {
        bool valid = false;
        QJsonObject json;
    };

    int addBufferView(std::size_t byteOffset, std::size_t byteLength, int target);
    int addAccessor(int bufferView, int componentType, std::size_t count, const char* type,
                    const std::vector<double>* minValues, const std::vector<double>* maxValues);
    int addFloatAttribute(const std::vector<float>& values, std::size_t count, int components, const char* type, bool withBounds);
    bool primitiveFor(unsigned int meshIndex, QJsonObject& out);
    int meshFor(const aiNode* node);
    int addNode(const aiNode* node);

    const aiScene* _scene;
    ExportSession& _session;
    GltfBinaryBuffer _bin;
    QJsonArray _bufferViews;
    QJsonArray _accessors;
    std::vector<QJsonObject> _meshes;
    std::vector<QJsonObject> _nodes;
    std::vector<int> _sceneRoots;
    std::unordered_map<unsigned int, CachedPrimitive> _primitiveCache;
    std::map<std::vector<unsigned int>, int> _meshCache;
    std::vector<float> _scratch;
    std::size_t _skippedMeshes = 0;
};

int GltfBuilder::addBufferView(std::size_t byteOffset, std::size_t byteLength, int target)
{
    QJsonObject view;
    view.insert(QStringLiteral("buffer"), num(0));
    view.insert(QStringLiteral("byteOffset"), num(static_cast<double>(byteOffset)));
    view.insert(QStringLiteral("byteLength"), num(static_cast<double>(byteLength)));
    view.insert(QStringLiteral("target"), num(target));
    _bufferViews.append(view);
    return _bufferViews.size() - 1;
}

int GltfBuilder::addAccessor(int bufferView, int componentType, std::size_t count, const char* type,
                             const std::vector<double>* minValues, const std::vector<double>* maxValues)
{
    QJsonObject accessor;
    accessor.insert(QStringLiteral("bufferView"), num(bufferView));
    accessor.insert(QStringLiteral("componentType"), num(componentType));
    accessor.insert(QStringLiteral("count"), num(static_cast<double>(count)));
    accessor.insert(QStringLiteral("type"), QString::fromLatin1(type));
    if (minValues && maxValues)
    {
        QJsonArray lo, hi;
        for (double v : *minValues)
            lo.append(num(v));
        for (double v : *maxValues)
            hi.append(num(v));
        accessor.insert(QStringLiteral("min"), lo);
        accessor.insert(QStringLiteral("max"), hi);
    }
    _accessors.append(accessor);
    return _accessors.size() - 1;
}

// `values` holds count * components floats. Bounds (min / max per component, exact) are written for the accessor when asked
// (glTF requires them for POSITION).
int GltfBuilder::addFloatAttribute(const std::vector<float>& values, std::size_t count, int components, const char* type, bool withBounds)
{
    const std::size_t bytes = count * static_cast<std::size_t>(components) * sizeof(float);
    const std::size_t offset = _bin.append(values.data(), bytes);
    const int view = addBufferView(offset, bytes, kArrayBuffer);

    if (!withBounds)
        return addAccessor(view, kComponentFloat, count, type, nullptr, nullptr);

    std::vector<double> lo(static_cast<std::size_t>(components), std::numeric_limits<double>::infinity());
    std::vector<double> hi(static_cast<std::size_t>(components), -std::numeric_limits<double>::infinity());
    for (std::size_t i = 0; i < count; ++i)
    {
        for (int c = 0; c < components; ++c)
        {
            const double v = values[i * static_cast<std::size_t>(components) + static_cast<std::size_t>(c)];
            lo[static_cast<std::size_t>(c)] = std::min(lo[static_cast<std::size_t>(c)], v);
            hi[static_cast<std::size_t>(c)] = std::max(hi[static_cast<std::size_t>(c)], v);
        }
    }
    return addAccessor(view, kComponentFloat, count, type, &lo, &hi);
}

// One glTF primitive for one aiMesh; cached, so a mesh used by several nodes is written once.
bool GltfBuilder::primitiveFor(unsigned int meshIndex, QJsonObject& out)
{
    const auto cached = _primitiveCache.find(meshIndex);
    if (cached != _primitiveCache.end())
    {
        if (!cached->second.valid)
            return false;
        out = cached->second.json;
        return true;
    }

    CachedPrimitive& entry = _primitiveCache[meshIndex];
    if (meshIndex >= _scene->mNumMeshes || !_scene->mMeshes[meshIndex])
    {
        _session.warn(QStringLiteral("a node refers to mesh %1, which does not exist").arg(meshIndex));
        ++_skippedMeshes;
        return false;
    }
    const aiMesh* mesh = _scene->mMeshes[meshIndex];
    const QString meshName = toQString(mesh->mName);
    if (mesh->mNumVertices == 0 || !mesh->mVertices)
    {
        _session.warn(QStringLiteral("mesh \"%1\" has no vertices and was left out").arg(meshName));
        ++_skippedMeshes;
        return false;
    }

    // One primitive type per glTF primitive: triangles first, then lines, then points.
    unsigned int perPrimitive = 0;
    int mode = 4;
    if (mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) { perPrimitive = 3; mode = 4; }
    else if (mesh->mPrimitiveTypes & aiPrimitiveType_LINE) { perPrimitive = 2; mode = 1; }
    else if (mesh->mPrimitiveTypes & aiPrimitiveType_POINT) { perPrimitive = 1; mode = 0; }
    else
    {
        _session.warn(QStringLiteral("mesh \"%1\" has no triangles, lines or points and was left out").arg(meshName));
        ++_skippedMeshes;
        return false;
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * perPrimitive);
    std::uint32_t maxIndex = 0;
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
    {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices != perPrimitive)
            continue;
        for (unsigned int k = 0; k < perPrimitive; ++k)
        {
            indices.push_back(face.mIndices[k]);
            maxIndex = std::max<std::uint32_t>(maxIndex, face.mIndices[k]);
        }
    }
    if (indices.empty() || maxIndex >= mesh->mNumVertices)
    {
        _session.warn(QStringLiteral("mesh \"%1\" has no usable faces (or an index outside its vertices) and was left out").arg(meshName));
        ++_skippedMeshes;
        return false;
    }

    const std::size_t n = mesh->mNumVertices;
    QJsonObject attributes;

    // POSITION
    _scratch.resize(n * 3);
    for (std::size_t i = 0; i < n; ++i)
    {
        _scratch[i * 3 + 0] = mesh->mVertices[i].x;
        _scratch[i * 3 + 1] = mesh->mVertices[i].y;
        _scratch[i * 3 + 2] = mesh->mVertices[i].z;
    }
    attributes.insert(QStringLiteral("POSITION"), num(addFloatAttribute(_scratch, n, 3, "VEC3", true)));

    // NORMAL: only for triangles, and only when the mesh has real normals. Points and lines have none (the scene builder
    // fills them with zeros, which glTF validators reject as non-unit normals).
    bool hasNormals = false;
    if (mode == 4 && mesh->mNormals)
    {
        for (std::size_t i = 0; i < n && !hasNormals; ++i)
            hasNormals = mesh->mNormals[i].x != 0.0f || mesh->mNormals[i].y != 0.0f || mesh->mNormals[i].z != 0.0f;
    }
    if (hasNormals)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            _scratch[i * 3 + 0] = mesh->mNormals[i].x;
            _scratch[i * 3 + 1] = mesh->mNormals[i].y;
            _scratch[i * 3 + 2] = mesh->mNormals[i].z;
        }
        attributes.insert(QStringLiteral("NORMAL"), num(addFloatAttribute(_scratch, n, 3, "VEC3", false)));
    }

    // TEXCOORD_n (the scene's values are the file's values: no flip)
    _scratch.resize(n * 2);
    for (unsigned int ch = 0; ch < kMaxUvChannels && ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
    {
        if (!mesh->mTextureCoords[ch])
            continue;
        for (std::size_t i = 0; i < n; ++i)
        {
            _scratch[i * 2 + 0] = mesh->mTextureCoords[ch][i].x;
            _scratch[i * 2 + 1] = mesh->mTextureCoords[ch][i].y;
        }
        attributes.insert(QStringLiteral("TEXCOORD_%1").arg(ch), num(addFloatAttribute(_scratch, n, 2, "VEC2", false)));
    }

    // COLOR_0 only when the mesh really has vertex colours: the scene builder fills every mesh with white by default, and a
    // white COLOR_0 only adds data to the file and to the viewer's shading.
    if (mesh->mColors[0])
    {
        bool anyColour = false;
        for (std::size_t i = 0; i < n && !anyColour; ++i)
            anyColour = !isWhiteOpaque(mesh->mColors[0][i]);
        if (anyColour)
        {
            _scratch.resize(n * 4);
            for (std::size_t i = 0; i < n; ++i)
            {
                _scratch[i * 4 + 0] = mesh->mColors[0][i].r;
                _scratch[i * 4 + 1] = mesh->mColors[0][i].g;
                _scratch[i * 4 + 2] = mesh->mColors[0][i].b;
                _scratch[i * 4 + 3] = mesh->mColors[0][i].a;
            }
            attributes.insert(QStringLiteral("COLOR_0"), num(addFloatAttribute(_scratch, n, 4, "VEC4", false)));
        }
    }

    // Indices: 16-bit when every index fits (a 16-bit index may not be 65535: that value is the restart marker).
    int indexAccessor = 0;
    if (n <= 65535u)
    {
        std::vector<std::uint16_t> narrow(indices.size());
        for (std::size_t i = 0; i < indices.size(); ++i)
            narrow[i] = static_cast<std::uint16_t>(indices[i]);
        const std::size_t bytes = narrow.size() * sizeof(std::uint16_t);
        const int view = addBufferView(_bin.append(narrow.data(), bytes), bytes, kElementArrayBuffer);
        indexAccessor = addAccessor(view, kComponentUShort, indices.size(), "SCALAR", nullptr, nullptr);
    }
    else
    {
        const std::size_t bytes = indices.size() * sizeof(std::uint32_t);
        const int view = addBufferView(_bin.append(indices.data(), bytes), bytes, kElementArrayBuffer);
        indexAccessor = addAccessor(view, kComponentUInt, indices.size(), "SCALAR", nullptr, nullptr);
    }

    QJsonObject primitive;
    primitive.insert(QStringLiteral("attributes"), attributes);
    primitive.insert(QStringLiteral("indices"), num(indexAccessor));
    if (mode != 4)
        primitive.insert(QStringLiteral("mode"), num(mode));

    entry.valid = true;
    entry.json = primitive;
    out = primitive;
    return true;
}

// The glTF mesh for a node's aiMesh list (one primitive per aiMesh); nodes with the same list share one glTF mesh.
// Returns -1 when the node has no usable mesh.
int GltfBuilder::meshFor(const aiNode* node)
{
    if (node->mNumMeshes == 0)
        return -1;

    const std::vector<unsigned int> key(node->mMeshes, node->mMeshes + node->mNumMeshes);
    const auto cached = _meshCache.find(key);
    if (cached != _meshCache.end())
        return cached->second;

    QJsonArray primitives;
    QString name;
    for (unsigned int meshIndex : key)
    {
        QJsonObject primitive;
        if (!primitiveFor(meshIndex, primitive))
            continue;
        primitives.append(primitive);
        if (name.isEmpty() && meshIndex < _scene->mNumMeshes && _scene->mMeshes[meshIndex])
            name = toQString(_scene->mMeshes[meshIndex]->mName);
    }
    if (primitives.isEmpty())
    {
        _meshCache[key] = -1;
        return -1;
    }

    QJsonObject mesh;
    if (name.isEmpty())
        name = toQString(node->mName);
    if (!name.isEmpty() && !isGeneratedNodeName(name))
        mesh.insert(QStringLiteral("name"), name);
    mesh.insert(QStringLiteral("primitives"), primitives);
    _meshes.push_back(mesh);
    const int index = static_cast<int>(_meshes.size()) - 1;
    _meshCache[key] = index;
    return index;
}

int GltfBuilder::addNode(const aiNode* node)
{
    const int index = static_cast<int>(_nodes.size());
    _nodes.emplace_back(); // reserve the slot first: the children come after their parent

    QJsonObject object;
    const QString nodeName = toQString(node->mName);
    if (!nodeName.isEmpty() && !isGeneratedNodeName(nodeName))
        object.insert(QStringLiteral("name"), nodeName);
    writeNodeTransform(node->mTransformation, object);

    const int meshIndex = meshFor(node);
    if (meshIndex >= 0)
        object.insert(QStringLiteral("mesh"), num(meshIndex));

    QJsonArray children;
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        if (node->mChildren[i])
            children.append(num(addNode(node->mChildren[i])));
    if (!children.isEmpty())
        object.insert(QStringLiteral("children"), children);

    _nodes[static_cast<std::size_t>(index)] = object;
    return index;
}

void GltfBuilder::build()
{
    // Rough size of the buffer, so it is allocated once (positions, normals, UVs, indices).
    std::size_t estimate = 0;
    for (unsigned int i = 0; i < _scene->mNumMeshes; ++i)
    {
        const aiMesh* m = _scene->mMeshes[i];
        if (m)
            estimate += static_cast<std::size_t>(m->mNumVertices) * 32u + static_cast<std::size_t>(m->mNumFaces) * 12u;
    }
    _bin.reserve(estimate);

    const aiNode* root = _scene->mRootNode;
    if (!root)
        return;

    // The scene's synthetic root only exists because an aiScene needs one: its children become the glTF scene's nodes. A root
    // that carries a transform or meshes is a real node and is kept.
    if (root->mNumMeshes == 0 && root->mTransformation.IsIdentity())
    {
        for (unsigned int i = 0; i < root->mNumChildren; ++i)
            if (root->mChildren[i])
                _sceneRoots.push_back(addNode(root->mChildren[i]));
    }
    else
    {
        _sceneRoots.push_back(addNode(root));
    }
}

QJsonObject GltfBuilder::json(const QString& binaryUri) const
{
    QJsonObject root;

    QJsonObject asset;
    asset.insert(QStringLiteral("version"), QStringLiteral("2.0"));
    asset.insert(QStringLiteral("generator"), QStringLiteral("ModelViewer-Qt %1").arg(QStringLiteral(APP_VERSION_STRING)));
    root.insert(QStringLiteral("asset"), asset);

    QJsonArray roots;
    for (int index : _sceneRoots)
        roots.append(num(index));
    QJsonObject sceneObject;
    if (!roots.isEmpty())
        sceneObject.insert(QStringLiteral("nodes"), roots);
    root.insert(QStringLiteral("scene"), num(0));
    root.insert(QStringLiteral("scenes"), QJsonArray{ sceneObject });

    if (!_nodes.empty())
    {
        QJsonArray nodes;
        for (const QJsonObject& node : _nodes)
            nodes.append(node);
        root.insert(QStringLiteral("nodes"), nodes);
    }
    if (!_meshes.empty())
    {
        QJsonArray meshes;
        for (const QJsonObject& mesh : _meshes)
            meshes.append(mesh);
        root.insert(QStringLiteral("meshes"), meshes);
    }
    if (!_accessors.isEmpty())
        root.insert(QStringLiteral("accessors"), _accessors);
    if (!_bufferViews.isEmpty())
        root.insert(QStringLiteral("bufferViews"), _bufferViews);

    if (_bin.size() > 0)
    {
        QJsonObject buffer;
        buffer.insert(QStringLiteral("byteLength"), num(static_cast<double>(_bin.size())));
        if (!binaryUri.isEmpty())
            buffer.insert(QStringLiteral("uri"), binaryUri);
        root.insert(QStringLiteral("buffers"), QJsonArray{ buffer });
    }
    return root;
}

void putU32(QByteArray& out, std::uint32_t value)
{
    const std::uint32_t le = qToLittleEndian<std::uint32_t>(value);
    out.append(reinterpret_cast<const char*>(&le), 4);
}

} // namespace

ExportResult GltfWriter::write(const ExportRequest& request) const
{
    const bool glb = (request.formatId == QLatin1String("glb2"));
    if (!glb && request.formatId != QLatin1String("gltf2"))
        return ExportResult::fail(QStringLiteral("GltfWriter does not write the format \"%1\"").arg(request.formatId));
    if (!request.scene || !request.scene->mRootNode)
        return ExportResult::fail(QStringLiteral("there is no scene to export"));

    ExportSession session(glb ? QStringLiteral("glb") : QStringLiteral("gltf"));

    GltfBuilder builder(request.scene, session);
    builder.build();
    session.phase("1 nodes, meshes, accessors");

    if (builder.meshCount() == 0)
        return ExportResult::fail(QStringLiteral("the scene has no mesh that can be written"), session.warnings());

    const QFileInfo outputInfo(request.outputPath);
    const QString binaryName = outputInfo.completeBaseName() + QStringLiteral(".bin");
    const QString binaryUri = glb ? QString() : QString::fromLatin1(QUrl::toPercentEncoding(binaryName));

    const QByteArray jsonText = QJsonDocument(builder.json(binaryUri)).toJson(QJsonDocument::Compact);
    session.phase("2 json");

    const GltfBinaryBuffer& bin = builder.binary();

    if (glb)
    {
        const std::uint64_t jsonPadded = (static_cast<std::uint64_t>(jsonText.size()) + 3u) & ~static_cast<std::uint64_t>(3u);
        const std::uint64_t binPadded = (static_cast<std::uint64_t>(bin.size()) + 3u) & ~static_cast<std::uint64_t>(3u);
        const std::uint64_t total = 12u + 8u + jsonPadded + (binPadded > 0 ? 8u + binPadded : 0u);
        if (total > 0xFFFFFFFFull)
            return ExportResult::fail(QStringLiteral("the scene is too large for one .glb file (4 GB limit); export it as .gltf instead"),
                                      session.warnings());

        QFile file(request.outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return ExportResult::fail(QStringLiteral("cannot write %1: %2").arg(request.outputPath, file.errorString()), session.warnings());

        QByteArray head;
        putU32(head, 0x46546C67u); // "glTF"
        putU32(head, 2u);
        putU32(head, static_cast<std::uint32_t>(total));
        putU32(head, static_cast<std::uint32_t>(jsonPadded));
        putU32(head, 0x4E4F534Au); // "JSON"
        bool good = file.write(head) == head.size();

        QByteArray jsonChunk = jsonText;
        jsonChunk.append(QByteArray(static_cast<int>(jsonPadded) - jsonText.size(), ' '));
        good = good && file.write(jsonChunk) == jsonChunk.size();

        if (good && binPadded > 0)
        {
            QByteArray binHead;
            putU32(binHead, static_cast<std::uint32_t>(binPadded));
            putU32(binHead, 0x004E4942u); // "BIN\0"
            good = file.write(binHead) == binHead.size();
            good = good && file.write(reinterpret_cast<const char*>(bin.data()), static_cast<qint64>(bin.size())) == static_cast<qint64>(bin.size());
            const int pad = static_cast<int>(binPadded - bin.size());
            if (good && pad > 0)
                good = file.write(QByteArray(pad, '\0')) == pad;
        }
        file.close();
        if (!good)
        {
            QFile::remove(request.outputPath);
            return ExportResult::fail(QStringLiteral("writing %1 failed (disk full?)").arg(request.outputPath), session.warnings());
        }
    }
    else
    {
        const QString binaryPath = outputInfo.absoluteDir().filePath(binaryName);
        QFile binFile(binaryPath);
        bool good = binFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if (good && bin.size() > 0)
            good = binFile.write(reinterpret_cast<const char*>(bin.data()), static_cast<qint64>(bin.size())) == static_cast<qint64>(bin.size());
        binFile.close();
        QFile jsonFile(request.outputPath);
        good = good && jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
        good = good && jsonFile.write(jsonText) == jsonText.size();
        jsonFile.close();
        if (!good)
        {
            QFile::remove(binaryPath);
            QFile::remove(request.outputPath);
            return ExportResult::fail(QStringLiteral("writing %1 failed").arg(request.outputPath), session.warnings());
        }
    }
    session.phase("3 write file");

    qDebug().noquote() << QStringLiteral("[Export] %1: %2 nodes, %3 meshes, %4 bytes of binary data, %5 skipped mesh(es)")
        .arg(request.outputPath).arg(builder.nodeCount()).arg(builder.meshCount()).arg(bin.size()).arg(builder.skippedMeshCount());
    return ExportResult::ok(session.warnings());
}
