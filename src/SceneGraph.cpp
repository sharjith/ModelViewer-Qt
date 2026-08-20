#include "SceneGraph.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QVector4D>
#include <cmath>
#include <functional>

namespace
{
// Version history:
//   1 — initial
//   2 — added importCorrection matrix to fileNode serialization
//   3 — added autoOrientApplied / autoScaleApplied flags to fileNode serialization
constexpr quint32 SCENEGRAPH_SESSION_VERSION = 3;

void writeMatrix(QDataStream& out, const aiMatrix4x4& m)
{
    out << m.a1 << m.a2 << m.a3 << m.a4
        << m.b1 << m.b2 << m.b3 << m.b4
        << m.c1 << m.c2 << m.c3 << m.c4
        << m.d1 << m.d2 << m.d3 << m.d4;
}

bool readMatrix(QDataStream& in, aiMatrix4x4& m)
{
    in >> m.a1 >> m.a2 >> m.a3 >> m.a4
       >> m.b1 >> m.b2 >> m.b3 >> m.b4
       >> m.c1 >> m.c2 >> m.c3 >> m.c4
       >> m.d1 >> m.d2 >> m.d3 >> m.d4;
    return in.status() == QDataStream::Ok;
}

QMatrix4x4 aiToQMatrix(const aiMatrix4x4& m)
{
    QMatrix4x4 out;
    out.setRow(0, QVector4D(m.a1, m.a2, m.a3, m.a4));
    out.setRow(1, QVector4D(m.b1, m.b2, m.b3, m.b4));
    out.setRow(2, QVector4D(m.c1, m.c2, m.c3, m.c4));
    out.setRow(3, QVector4D(m.d1, m.d2, m.d3, m.d4));
    return out;
}
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SceneGraph::SceneGraph(QObject* parent)
    : QObject(parent)
{
    _root           = new SceneNode();
    _root->nodeUuid = QUuid::createUuid();
    _root->name     = QStringLiteral("__SceneRoot__");
}

SceneGraph::~SceneGraph()
{
    freeSubtree(_root);
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void SceneGraph::appendFromScene(const aiScene*                   scene,
                                 const QString&                   sourceFile,
                                 const QList<QUuid>&              meshUuidsInOrder,
                                 const aiMatrix4x4&               importCorrection,
                                 bool                             autoOrientApplied,
                                 bool                             autoScaleApplied)
{
    if (!scene || !scene->mRootNode)
        return;

    // Store per-file punctual lights (full GltfLightData with names, already
    // set by the caller before or after this call via setLightData()).
    // appendFromScene itself does not touch _lightDataByFile — the caller
    // (ViewportWidget) calls setLightData() separately, which emits lightDataChanged().

    // --- Synthetic file-level node ------------------------------------------
    // This sits directly under _root and provides a clean per-import boundary
    // in the tree.  Its display name is the bare filename (e.g. "Model1.obj").
    SceneNode* fileNode        = new SceneNode();
    fileNode->nodeUuid         = QUuid::createUuid();
    fileNode->name             = QFileInfo(sourceFile).fileName();
    fileNode->isSynthetic        = true;
    fileNode->sourceFile         = sourceFile;
    fileNode->importCorrection   = importCorrection;
    fileNode->autoOrientApplied  = autoOrientApplied;
    fileNode->autoScaleApplied   = autoScaleApplied;
    // localTransform stays default-constructed (identity).
    fileNode->parent           = _root;
    _root->children.append(fileNode);

    // --- Mirror the aiNode tree under the file node -------------------------
    // Option B: always preserve the full aiNode hierarchy verbatim, including
    // the root node even when its name matches the filename.  This guarantees
    // that all node transforms are preserved for a lossless export round-trip.
    int cursor = 0;
    SceneNode* subtreeRoot = buildSubtree(scene->mRootNode,
                                          fileNode,
                                          meshUuidsInOrder,
                                          cursor);
    fileNode->children.append(subtreeRoot);

    emit structureChanged();
}

void SceneGraph::rebuildFlat(const QString& sessionName,
                             const QList<QUuid>& meshUuids)
{
    _meshUuidToNode.clear();
    _lightDataByFile.clear();
    _variantDataByFile.clear();
    _activeVariantByFile.clear();
    _animationDataByFile.clear();
    _activeAnimationClipByFile.clear();
    freeSubtree(_root);

    _root           = new SceneNode();
    _root->nodeUuid = QUuid::createUuid();
    _root->name     = QStringLiteral("__SceneRoot__");

    SceneNode* fileNode    = new SceneNode();
    fileNode->nodeUuid     = QUuid::createUuid();
    fileNode->name         = sessionName;
    fileNode->isSynthetic  = true;
    fileNode->sourceFile   = sessionName;
    fileNode->parent       = _root;
    _root->children.append(fileNode);

    for (const QUuid& uuid : meshUuids)
    {
        fileNode->meshUuids.append(uuid);
        _meshUuidToNode[uuid] = fileNode;
    }

    emit structureChanged();
}

SceneNode* SceneGraph::buildSubtree(const aiNode*       ainode,
                                    SceneNode*          parent,
                                    const QList<QUuid>& uuids,
                                    int&                cursor)
{
    SceneNode* node       = new SceneNode();
    node->nodeUuid        = QUuid::createUuid();
    node->name            = QString::fromUtf8(ainode->mName.C_Str());
    node->localTransform  = ainode->mTransformation;
    node->parent          = parent;

    // Assign mesh UUIDs in the same order that processNode() encountered them:
    // inner loop over mMeshes before recursing into children.
    for (unsigned int i = 0; i < ainode->mNumMeshes; ++i)
    {
        if (cursor < uuids.size())
        {
            const QUuid& uuid = uuids.at(cursor++);
            node->meshUuids.append(uuid);
            _meshUuidToNode[uuid] = node;
        }
    }

    for (unsigned int i = 0; i < ainode->mNumChildren; ++i)
    {
        SceneNode* child = buildSubtree(ainode->mChildren[i],
                                        node,
                                        uuids,
                                        cursor);
        node->children.append(child);
    }

    return node;
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void SceneGraph::clear()
{
    _meshUuidToNode.clear();
    _lightDataByFile.clear();
    _variantDataByFile.clear();
    _activeVariantByFile.clear();
    _animationDataByFile.clear();
    _activeAnimationClipByFile.clear();
    freeSubtree(_root);

    _root           = new SceneNode();
    _root->nodeUuid = QUuid::createUuid();
    _root->name     = QStringLiteral("__SceneRoot__");

    emit structureChanged();
}

void SceneGraph::rebuildFromMvf(const QJsonArray& documentNodes,
                                const QJsonArray& sceneRootNodes)
{
    _meshUuidToNode.clear();
    _lightDataByFile.clear();
    _variantDataByFile.clear();
    _activeVariantByFile.clear();
    _animationDataByFile.clear();
    _activeAnimationClipByFile.clear();
    _gltfCameraDataByFile.clear();
    freeSubtree(_root);

    _root           = new SceneNode();
    _root->nodeUuid = QUuid::createUuid();
    _root->name     = QStringLiteral("__SceneRoot__");

    std::function<SceneNode*(int, SceneNode*)> buildNode =
        [&](int idx, SceneNode* parent) -> SceneNode*
    {
        if (idx < 0 || idx >= documentNodes.size())
            return nullptr;

        const QJsonObject obj = documentNodes[idx].toObject();

        SceneNode* node    = new SceneNode();
        node->parent       = parent;

        const QString idStr = obj[QStringLiteral("id")].toString();
        node->nodeUuid = idStr.isEmpty() ? QUuid::createUuid()
                                         : QUuid::fromString(idStr);
        node->name = obj[QStringLiteral("name")].toString();
        node->isSynthetic = obj[QStringLiteral("isSynthetic")].toBool(false);
        node->sourceFile = obj[QStringLiteral("sourceFile")].toString();

        auto readJsonMatrix = [](const QJsonArray& mat, aiMatrix4x4& m) {
            if (mat.size() != 16) return;
            m.a1 = (float)mat[0].toDouble();  m.a2 = (float)mat[1].toDouble();
            m.a3 = (float)mat[2].toDouble();  m.a4 = (float)mat[3].toDouble();
            m.b1 = (float)mat[4].toDouble();  m.b2 = (float)mat[5].toDouble();
            m.b3 = (float)mat[6].toDouble();  m.b4 = (float)mat[7].toDouble();
            m.c1 = (float)mat[8].toDouble();  m.c2 = (float)mat[9].toDouble();
            m.c3 = (float)mat[10].toDouble(); m.c4 = (float)mat[11].toDouble();
            m.d1 = (float)mat[12].toDouble(); m.d2 = (float)mat[13].toDouble();
            m.d3 = (float)mat[14].toDouble(); m.d4 = (float)mat[15].toDouble();
        };

        readJsonMatrix(obj[QStringLiteral("matrix")].toArray(), node->localTransform);

        // Restore autoOrient+autoScale correction (written since MVF format v2).
        // Older MVF files omit this key; in that case importCorrection stays identity,
        // meaning the exporter will not factor it out on the next export — acceptable
        // for old files since the correction data is simply unavailable.
        const QJsonArray corrMat = obj[QStringLiteral("importCorrection")].toArray();
        if (!corrMat.isEmpty())
            readJsonMatrix(corrMat, node->importCorrection);
        node->autoOrientApplied = obj[QStringLiteral("autoOrientApplied")].toBool(false);
        node->autoScaleApplied  = obj[QStringLiteral("autoScaleApplied")].toBool(false);

        const QJsonArray bindings = obj[QStringLiteral("meshBindings")].toArray();
        for (const QJsonValue& b : bindings)
        {
            const QString uuidStr = b.toObject()[QStringLiteral("uuid")].toString();
            if (!uuidStr.isEmpty())
            {
                const QUuid uuid = QUuid::fromString(uuidStr);
                node->meshUuids.append(uuid);
                _meshUuidToNode[uuid] = node;
            }
        }

        const QJsonArray children = obj[QStringLiteral("children")].toArray();
        for (const QJsonValue& c : children)
        {
            SceneNode* child = buildNode(c.toInt(-1), node);
            if (child)
                node->children.append(child);
        }

        return node;
    };

    for (const QJsonValue& rootIdx : sceneRootNodes)
    {
        SceneNode* child = buildNode(rootIdx.toInt(-1), _root);
        if (child)
            _root->children.append(child);
    }

    // --- Recover importCorrection from assimpRoot localTransform ---
    //
    // PRIMARY PATH (new MVF with flags):
    //   If autoOrientApplied || autoScaleApplied is set, the loader recorded that it applied a
    //   correction.  The matrix was pre-multiplied into assimpRoot->localTransform; when
    //   importCorrection is still identity (e.g. the matrix key was absent), recover it from there.
    //
    // LEGACY FALLBACK (old MVF without flags or importCorrection key):
    //   Old MVF files pre-date both keys.  We apply a narrow structural heuristic: if
    //   assimpRoot->localTransform matches one of the seven known auto-orient patterns
    //   (identity or +-90 degree cardinal-axis rotation with optional uniform scale), treat it
    //   as the correction.  User gizmo transforms live in RenderableMesh::_transformation and
    //   never reach SceneNode::localTransform, so there is no collision with user edits.
    auto looksLikeAutoOrientCorrection = [](const aiMatrix4x4& m) -> bool
    {
        // NOTE: Do NOT reject on translation (a4/b4/c4).  The globalSceneTransform that is
        // stored in assimpRoot->localTransform includes a centering translation in addition
        // to the autoOrient rotation and autoScale.  Old MVF files carry the full combined
        // matrix there, so we must accept translations when checking for auto-orient patterns.
        const float sx = std::sqrt(m.a1*m.a1 + m.b1*m.b1 + m.c1*m.c1);
        const float sy = std::sqrt(m.a2*m.a2 + m.b2*m.b2 + m.c2*m.c2);
        const float sz = std::sqrt(m.a3*m.a3 + m.b3*m.b3 + m.c3*m.c3);
        if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f) return false;
        if (std::abs(sx - sy) > sx * 0.01f || std::abs(sx - sz) > sx * 0.01f) return false;
        const float r11 = m.a1/sx, r21 = m.b1/sx, r31 = m.c1/sx;
        const float r12 = m.a2/sy, r22 = m.b2/sy, r32 = m.c2/sy;
        const float r13 = m.a3/sz, r23 = m.b3/sz, r33 = m.c3/sz;
        auto near = [](float v, float t) { return std::abs(v - t) < 0.02f; };
        // identity
        if (near(r11,1) && near(r22,1) && near(r33,1) && near(r12,0) && near(r13,0) && near(r21,0) && near(r23,0) && near(r31,0) && near(r32,0)) return true;
        // Rx(+90): [1,0,0 / 0,0,-1 / 0,1,0]
        if (near(r11,1) && near(r12,0) && near(r13, 0) && near(r21,0) && near(r22,0) && near(r23,-1) && near(r31,0) && near(r32,1) && near(r33, 0)) return true;
        // Rx(-90): [1,0,0 / 0,0,1 / 0,-1,0]
        if (near(r11,1) && near(r12,0) && near(r13,0) && near(r21,0) && near(r22,0) && near(r23,1) && near(r31,0) && near(r32,-1) && near(r33,0)) return true;
        // Ry(+90): [0,0,1 / 0,1,0 / -1,0,0]
        if (near(r11,0) && near(r12,0) && near(r13, 1) && near(r21,0) && near(r22,1) && near(r23, 0) && near(r31,-1) && near(r32,0) && near(r33,0)) return true;
        // Ry(-90): [0,0,-1 / 0,1,0 / 1,0,0]
        if (near(r11,0) && near(r12,0) && near(r13,-1) && near(r21,0) && near(r22,1) && near(r23, 0) && near(r31,1) && near(r32,0) && near(r33, 0)) return true;
        // Rz(+90): [0,-1,0 / 1,0,0 / 0,0,1]
        if (near(r11,0) && near(r12,-1) && near(r13,0) && near(r21,1) && near(r22, 0) && near(r23,0) && near(r31,0) && near(r32, 0) && near(r33,1)) return true;
        // Rz(-90): [0,1,0 / -1,0,0 / 0,0,1]
        if (near(r11, 0) && near(r12,1) && near(r13,0) && near(r21,-1) && near(r22,0) && near(r23,0) && near(r31, 0) && near(r32,0) && near(r33,1)) return true;
        return false;
    };

    for (SceneNode* fileNode : _root->children)
    {
        if (!fileNode->isSynthetic)
            continue;
        // If importCorrection is already populated (new MVF with matrix key), nothing to do.
        if (!fileNode->importCorrection.IsIdentity())
            continue;

        SceneNode* assimpRoot = fileNode->children.isEmpty()
                                ? nullptr : fileNode->children.first();
        if (!assimpRoot)
            continue;

        if (fileNode->autoOrientApplied || fileNode->autoScaleApplied)
        {
            // Flags confirm a correction was applied at load time.
            // Copy the correction into importCorrection so the exporter can factor it out.
            // Do NOT zero assimpRoot->localTransform — the renderer needs it for correct
            // display, and the exporter already handles it with a save/restore around the
            // hierarchy build (corrInv * localTransform = identity temporarily during export).
            fileNode->importCorrection = assimpRoot->localTransform;
        }
        else
        {
            // Legacy fallback for old MVF files that have neither flags nor the correction key.
            if (looksLikeAutoOrientCorrection(assimpRoot->localTransform))
                fileNode->importCorrection = assimpRoot->localTransform;
            // Same principle: do not zero localTransform — leave it for the renderer and
            // let the exporter's save/restore mechanism handle the temporary removal.
        }
    }

    emit structureChanged();
}

void SceneGraph::serialize(QDataStream& out) const
{
    out << SCENEGRAPH_SESSION_VERSION;

    std::function<void(const SceneNode*)> writeNode = [&](const SceneNode* node) {
        out << node->nodeUuid
            << node->name
            << node->isSynthetic
            << node->sourceFile;
        writeMatrix(out, node->localTransform);
        writeMatrix(out, node->importCorrection);  // v2: persists autoOrient+autoScale correction
        out << node->autoOrientApplied << node->autoScaleApplied;  // v3

        out << static_cast<quint32>(node->meshUuids.size());
        for (const QUuid& uuid : node->meshUuids)
            out << uuid;

        out << static_cast<quint32>(node->children.size());
        for (const SceneNode* child : node->children)
            writeNode(child);
    };

    out << static_cast<quint32>(_root->children.size());
    for (const SceneNode* child : _root->children)
        writeNode(child);
}

bool SceneGraph::deserialize(QDataStream& in)
{
    quint32 version = 0;
    in >> version;
    if (in.status() != QDataStream::Ok || version != SCENEGRAPH_SESSION_VERSION)
        return false;

    _meshUuidToNode.clear();
    _variantDataByFile.clear();
    _activeVariantByFile.clear();
    _animationDataByFile.clear();
    _activeAnimationClipByFile.clear();
    freeSubtree(_root);

    _root           = new SceneNode();
    _root->nodeUuid = QUuid::createUuid();
    _root->name     = QStringLiteral("__SceneRoot__");

    std::function<SceneNode*(SceneNode*)> readNode = [&](SceneNode* parent) -> SceneNode* {
        auto* node = new SceneNode();
        node->parent = parent;

        quint32 meshCount = 0;
        quint32 childCount = 0;
        in >> node->nodeUuid
           >> node->name
           >> node->isSynthetic
           >> node->sourceFile;
        if (!readMatrix(in, node->localTransform))
        {
            delete node;
            return nullptr;
        }
        if (!readMatrix(in, node->importCorrection))  // v2
        {
            delete node;
            return nullptr;
        }
        in >> node->autoOrientApplied >> node->autoScaleApplied;  // v3

        in >> meshCount;
        for (quint32 i = 0; i < meshCount; ++i)
        {
            QUuid uuid;
            in >> uuid;
            node->meshUuids.append(uuid);
            _meshUuidToNode[uuid] = node;
        }

        in >> childCount;
        for (quint32 i = 0; i < childCount; ++i)
        {
            SceneNode* child = readNode(node);
            if (!child)
            {
                delete node;
                return nullptr;
            }
            node->children.append(child);
        }

        return node;
    };

    quint32 topLevelCount = 0;
    in >> topLevelCount;
    for (quint32 i = 0; i < topLevelCount; ++i)
    {
        SceneNode* child = readNode(_root);
        if (!child)
        {
            clear();
            return false;
        }
        _root->children.append(child);
    }

    emit structureChanged();
    return in.status() == QDataStream::Ok;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

SceneNode* SceneGraph::findNodeForMesh(const QUuid& meshUuid) const
{
    return _meshUuidToNode.value(meshUuid, nullptr);
}

SceneNode* SceneGraph::findFileNode(const QString& sourceFile) const
{
    for (SceneNode* child : _root->children)
    {
        if (child && child->sourceFile == sourceFile)
            return child;
    }
    return nullptr;
}

QStringList SceneGraph::allSourceFiles() const
{
    QStringList files;
    if (!_root)
        return files;

    for (SceneNode* child : _root->children)
    {
        if (child && child->isSynthetic && !child->sourceFile.isEmpty())
            files.append(child->sourceFile);
    }
    return files;
}

QList<QUuid> SceneGraph::collectMeshUuids(const SceneNode* node) const
{
    QList<QUuid> result;
    collectUuidsRecursive(node, result);
    return result;
}

SceneGraphWorldTransforms SceneGraph::evaluateWorldTransforms(const SceneNode* subtreeRoot) const
{
    SceneGraphWorldTransforms result;

    const SceneNode* traversalRoot = subtreeRoot ? subtreeRoot : _root;
    if (!traversalRoot)
        return result;

    std::function<void(const SceneNode*, const QMatrix4x4&)> eval =
        [&](const SceneNode* node, const QMatrix4x4& parentWorld)
    {
        if (!node)
            return;

        const QMatrix4x4 local = aiToQMatrix(node->localTransform);
        const QMatrix4x4 world = parentWorld * local;
        result.nodeWorldByUuid.insert(node->nodeUuid, world);
        for (const QUuid& meshUuid : node->meshUuids)
            result.meshWorldByUuid.insert(meshUuid, world);

        for (const SceneNode* child : node->children)
            eval(child, world);
    };

    eval(traversalRoot, QMatrix4x4());
    return result;
}

SceneGraphWorldTransforms SceneGraph::evaluateWorldTransformsForFile(const QString& sourceFile) const
{
    SceneGraphWorldTransforms result;

    const SceneNode* fileNode = findFileNode(sourceFile);
    if (!fileNode)
        return result;

    std::function<void(const SceneNode*, const QMatrix4x4&)> eval =
        [&](const SceneNode* node, const QMatrix4x4& parentWorld)
    {
        if (!node)
            return;

        const QMatrix4x4 local = aiToQMatrix(node->localTransform);
        const QMatrix4x4 world = parentWorld * local;
        result.nodeWorldByUuid.insert(node->nodeUuid, world);
        for (const QUuid& meshUuid : node->meshUuids)
            result.meshWorldByUuid.insert(meshUuid, world);

        for (const SceneNode* child : node->children)
            eval(child, world);
    };

    const QMatrix4x4 identity;
    result.nodeWorldByUuid.insert(fileNode->nodeUuid, identity);
    for (const QUuid& meshUuid : fileNode->meshUuids)
        result.meshWorldByUuid.insert(meshUuid, identity);
    for (const SceneNode* child : fileNode->children)
        eval(child, identity);

    return result;
}

void SceneGraph::collectUuidsRecursive(const SceneNode* node,
                                       QList<QUuid>&    out) const
{
    out.append(node->meshUuids);
    for (const SceneNode* child : node->children)
        collectUuidsRecursive(child, out);
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

SceneNode* SceneGraph::removeMeshUuid(const QUuid& meshUuid, int& outPosition)
{
    SceneNode* node = _meshUuidToNode.value(meshUuid, nullptr);
    if (!node)
    {
        outPosition = -1;
        return nullptr;
    }

    outPosition = node->meshUuids.indexOf(meshUuid);
    node->meshUuids.removeAll(meshUuid);
    _meshUuidToNode.remove(meshUuid);

    emit structureChanged();
    return node;
}

void SceneGraph::restoreMeshUuid(SceneNode*   node,
                                 const QUuid& meshUuid,
                                 int          position)
{
    if (!node)
        return;

    // Clamp to valid range in case concurrent removes shifted the list.
    position = qBound(0, position, static_cast<int>(node->meshUuids.size()));
    node->meshUuids.insert(position, meshUuid);
    _meshUuidToNode[meshUuid] = node;

    emit structureChanged();
}

SceneNode* SceneGraph::detachEmptyFileNode(const QString& sourceFile, int& outPosition)
{
    outPosition = -1;

    SceneNode* fileNode = findFileNode(sourceFile);
    if (!fileNode)
        return nullptr;

    // Only detach when the whole subtree is mesh-less; a partially deleted
    // file must keep its node so the remaining meshes stay anchored.
    if (!collectMeshUuids(fileNode).isEmpty())
        return nullptr;

    outPosition = _root->children.indexOf(fileNode);
    if (outPosition < 0)
        return nullptr;

    _root->children.removeAt(outPosition);
    fileNode->parent = nullptr;
    // Mesh UUIDs were already deregistered by removeMeshUuid(), so no lookup
    // table work is needed here.

    emit structureChanged();
    return fileNode;
}

void SceneGraph::reattachFileNode(SceneNode* fileNode, int position)
{
    if (!fileNode)
        return;

    fileNode->parent = _root;
    position = qBound(0, position, static_cast<int>(_root->children.size()));
    _root->children.insert(position, fileNode);

    emit structureChanged();
}

void SceneGraph::insertChildNode(SceneNode* parent, SceneNode* newChild, int position)
{
    if (!parent || !newChild)
        return;

    newChild->parent = parent;
    if (position < 0 || position >= parent->children.size())
        parent->children.append(newChild);
    else
        parent->children.insert(position, newChild);

    registerSubtreeUuids(newChild);

    emit structureChanged();
}

void SceneGraph::removeChildNode(SceneNode* parent, SceneNode* child, int& outPosition)
{
    if (!parent || !child)
    {
        outPosition = -1;
        return;
    }

    outPosition = parent->children.indexOf(child);
    if (outPosition < 0)
        return;

    parent->children.removeAt(outPosition);
    child->parent = nullptr;

    deregisterSubtreeUuids(child);

    emit structureChanged();
}

SceneNode* SceneGraph::findNodeByUuid(const QUuid& nodeUuid) const
{
    std::function<SceneNode*(SceneNode*)> search = [&](SceneNode* n) -> SceneNode*
    {
        if (n->nodeUuid == nodeUuid)
            return n;
        for (SceneNode* child : n->children)
        {
            if (SceneNode* found = search(child))
                return found;
        }
        return nullptr;
    };
    return search(_root);
}

void SceneGraph::deleteDetachedSubtree(SceneNode* root)
{
    if (!root)
        return;
    for (SceneNode* child : root->children)
        deleteDetachedSubtree(child);
    delete root;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SceneGraph::registerSubtreeUuids(SceneNode* node)
{
    for (const QUuid& uuid : node->meshUuids)
        _meshUuidToNode[uuid] = node;
    for (SceneNode* child : node->children)
        registerSubtreeUuids(child);
}

void SceneGraph::deregisterSubtreeUuids(SceneNode* node)
{
    for (const QUuid& uuid : node->meshUuids)
        _meshUuidToNode.remove(uuid);
    for (SceneNode* child : node->children)
        deregisterSubtreeUuids(child);
}

void SceneGraph::freeSubtree(SceneNode* node)
{
    if (!node)
        return;
    for (SceneNode* child : node->children)
        freeSubtree(child);
    delete node;
}

// ---------------------------------------------------------------------------
// KHR_materials_variants
// ---------------------------------------------------------------------------

void SceneGraph::setVariantData(const QString& sourceFile, const GltfVariantData& data)
{
    _variantDataByFile[sourceFile] = data;
    _activeVariantByFile.insert(sourceFile, -1);
    emit variantDataChanged();
}

void SceneGraph::clearVariantData(const QString& sourceFile)
{
    if (_variantDataByFile.remove(sourceFile) != 0)
    {
        _activeVariantByFile.remove(sourceFile);
        emit variantDataChanged();
    }
}

GltfVariantData SceneGraph::variantDataForFile(const QString& sourceFile) const
{
    return _variantDataByFile.value(sourceFile, GltfVariantData{});
}

QStringList SceneGraph::filesWithVariants() const
{
    return _variantDataByFile.keys();
}

void SceneGraph::setActiveVariant(const QString& sourceFile, int variantIndex)
{
    _activeVariantByFile[sourceFile] = variantIndex;
}

int SceneGraph::activeVariantForFile(const QString& sourceFile) const
{
    return _activeVariantByFile.value(sourceFile, -1);
}

void SceneGraph::setAnimationData(const QString& sourceFile, const GltfAnimationData& data)
{
    _animationDataByFile[sourceFile] = data;
    _activeAnimationClipByFile.insert(sourceFile, data.clips.isEmpty() ? -1 : 0);
    emit animationDataChanged();
}

void SceneGraph::clearAnimationData(const QString& sourceFile)
{
    if (_animationDataByFile.remove(sourceFile) != 0)
    {
        _activeAnimationClipByFile.remove(sourceFile);
        emit animationDataChanged();
    }
}

GltfAnimationData SceneGraph::animationDataForFile(const QString& sourceFile) const
{
    return _animationDataByFile.value(sourceFile, GltfAnimationData{});
}

QStringList SceneGraph::filesWithAnimations() const
{
    QStringList files;
    for (auto it = _animationDataByFile.cbegin(); it != _animationDataByFile.cend(); ++it)
    {
        if (!it.value().clips.isEmpty())
            files.append(it.key());
    }
    return files;
}

void SceneGraph::setActiveAnimationClip(const QString& sourceFile, int clipIndex)
{
    _activeAnimationClipByFile[sourceFile] = clipIndex;
}

int SceneGraph::activeAnimationClipForFile(const QString& sourceFile) const
{
    return _activeAnimationClipByFile.value(sourceFile, -1);
}

// ---------------------------------------------------------------------------
// glTF cameras
// ---------------------------------------------------------------------------

void SceneGraph::setGltfCameraData(const QString& sourceFile, const GltfCameraData& data)
{
    _gltfCameraDataByFile[sourceFile] = data;
    emit gltfCameraDataChanged();
}

void SceneGraph::clearGltfCameraData(const QString& sourceFile)
{
    if (_gltfCameraDataByFile.remove(sourceFile) != 0)
        emit gltfCameraDataChanged();
}

GltfCameraData SceneGraph::gltfCameraDataForFile(const QString& sourceFile) const
{
    return _gltfCameraDataByFile.value(sourceFile, GltfCameraData{});
}

QStringList SceneGraph::filesWithGltfCameras() const
{
    QStringList files;
    for (auto it = _gltfCameraDataByFile.cbegin(); it != _gltfCameraDataByFile.cend(); ++it)
    {
        if (!it.value().isEmpty())
            files.append(it.key());
    }
    return files;
}

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

void SceneGraph::addMeasurement(const Measurement& measurement)
{
    _measurements.append(measurement);
    emit measurementsChanged();
}

void SceneGraph::removeMeasurementAt(int index)
{
    if (index < 0 || index >= _measurements.size())
        return;
    _measurements.removeAt(index);
    emit measurementsChanged();
}

void SceneGraph::clearMeasurements()
{
    if (_measurements.isEmpty())
        return;
    _measurements.clear();
    emit measurementsChanged();
}

// ---------------------------------------------------------------------------
// KHR_lights_punctual
// ---------------------------------------------------------------------------

void SceneGraph::setLightData(const QString& sourceFile, const GltfLightData& data)
{
    _lightDataByFile[sourceFile] = data;
    emit lightDataChanged();
}

void SceneGraph::clearLightData(const QString& sourceFile)
{
    if (_lightDataByFile.remove(sourceFile) != 0)
        emit lightDataChanged();
}

GltfLightData SceneGraph::lightDataForFile(const QString& sourceFile) const
{
    return _lightDataByFile.value(sourceFile, GltfLightData{});
}

QStringList SceneGraph::filesWithLights() const
{
    QStringList files;
    for (auto it = _lightDataByFile.cbegin(); it != _lightDataByFile.cend(); ++it)
    {
        if (!it.value().isEmpty())
            files.append(it.key());
    }
    return files;
}

void SceneGraph::setLightEnabled(const QString& sourceFile, int lightIndex, bool enabled)
{
    auto it = _lightDataByFile.find(sourceFile);
    if (it == _lightDataByFile.end())
        return;
    if (lightIndex < 0 || lightIndex >= it->lights.size())
        return;
    it->lights[lightIndex].enabled = enabled;
    emit lightDataChanged();
}

std::vector<GPULight> SceneGraph::buildEnabledLightList() const
{
    std::vector<GPULight> result;
    // Iterate in source-file insertion order so the GPU list is deterministic.
    for (auto it = _lightDataByFile.cbegin(); it != _lightDataByFile.cend(); ++it)
    {
        for (const GltfLightEntry& entry : it->lights)
        {
            if (entry.enabled)
                result.push_back(entry.gpuLight);
        }
    }
    return result;
}
