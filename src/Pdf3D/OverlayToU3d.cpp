#include "OverlayToU3d.h"

#include <QString>

#include <algorithm>
#include <cmath>

namespace Pdf3D
{
namespace
{

struct ColorKey
{
    int r = 0, g = 0, b = 0;   // 0..255
    bool operator==(const ColorKey& o) const { return r == o.r && g == o.g && b == o.b; }
};

int toByte(float v)
{
    return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
}

QString asciiName(const QString& s)
{
    QString out;
    for (const QChar c : s)
    {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u == '-';
        out.append(ok ? c : QLatin1Char('_'));
    }
    if (out.size() > 40)
        out.truncate(40);
    return out;
}

struct Group
{
    ColorKey key;
    std::vector<float> positions;   // 3 floats per vertex, triangles in groups of three
};

// Index of the group for `key`, creating it on first use.
int groupIndex(std::vector<Group>& groups, const ColorKey& key)
{
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (groups[i].key == key)
            return static_cast<int>(i);
    }
    Group g;
    g.key = key;
    groups.push_back(g);
    return static_cast<int>(groups.size()) - 1;
}

void pushTriangle(Group& g, const QVector3D& a, const QVector3D& b, const QVector3D& c)
{
    for (const QVector3D& v : {a, b, c})
    {
        g.positions.push_back(v.x());
        g.positions.push_back(v.y());
        g.positions.push_back(v.z());
    }
}

QVector3D anyPerpendicular(const QVector3D& d)
{
    const QVector3D axis = (std::fabs(d.x()) < 0.9f) ? QVector3D(1.0f, 0.0f, 0.0f) : QVector3D(0.0f, 1.0f, 0.0f);
    return QVector3D::crossProduct(d, axis).normalized();
}

} // namespace

int OverlayToU3d::append(U3dScene& scene, const OverlayGraphicData& graphic, float lineWidth)
{
    std::vector<Group> groups;
    const float halfWidth = 0.5f * std::max(lineWidth, 1.0e-9f);
    const QVector3D view = graphic.viewDirection.lengthSquared() > 1.0e-10f ? graphic.viewDirection.normalized() : QVector3D(0.0f, 1.0f, 0.0f);

    // triangles: filled glyphs, arrowheads, fills
    for (size_t i = 0; i + 18 <= graphic.triangles.size(); i += 18)
    {
        const float* p = graphic.triangles.data() + i;
        const ColorKey key{toByte(p[3]), toByte(p[4]), toByte(p[5])};
        pushTriangle(groups[static_cast<size_t>(groupIndex(groups, key))],
                     QVector3D(p[0], p[1], p[2]), QVector3D(p[6], p[7], p[8]), QVector3D(p[12], p[13], p[14]));
    }

    // segments: two crossed ribbons each, one across the view direction, one across that
    for (size_t i = 0; i + 12 <= graphic.lines.size(); i += 12)
    {
        const float* p = graphic.lines.data() + i;
        const QVector3D a(p[0], p[1], p[2]);
        const QVector3D b(p[6], p[7], p[8]);
        const QVector3D d = b - a;
        if (d.lengthSquared() < 1.0e-14f)
            continue;
        const ColorKey key{toByte(p[3]), toByte(p[4]), toByte(p[5])};
        Group& g = groups[static_cast<size_t>(groupIndex(groups, key))];
        QVector3D n1 = QVector3D::crossProduct(d, view);
        n1 = (n1.lengthSquared() < 1.0e-14f) ? anyPerpendicular(d) : n1.normalized();
        const QVector3D n2 = QVector3D::crossProduct(d, n1).normalized();
        for (const QVector3D& n : {n1, n2})
        {
            const QVector3D o = n * halfWidth;
            pushTriangle(g, a - o, a + o, b + o);
            pushTriangle(g, a - o, b + o, b - o);
        }
    }

    // darker colours (the dark halo behind letters) sit furthest back, brighter ones are nudged towards the viewer, so coplanar
    // fills do not fight for the pixels
    std::vector<int> order(groups.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    const auto luminance = [&](int gi) { const ColorKey& k = groups[static_cast<size_t>(gi)].key; return 0.3f * k.r + 0.59f * k.g + 0.11f * k.b; };
    std::sort(order.begin(), order.end(), [&](int x, int y) { return luminance(x) < luminance(y); });

    int usedGroups = 0;
    for (const Group& g : groups)
        usedGroups += g.positions.empty() ? 0 : 1;
    if (usedGroups == 0)
        return 0;

    // One node per item (one entry in the model tree): every colour goes into the same mesh as per-vertex colours.
    const int item = scene.overlayItems++;

    // material shared by all items: the colours come from the vertices; a little emission keeps text and lines readable from any side
    // without washing out the dark halo behind letters
    const QString matName = QStringLiteral("Overlay (vertex colours)");
    int material = -1;
    for (int i = 0; i < scene.materials.size(); ++i)
    {
        if (scene.materials[i].name == matName)
        {
            material = i;
            break;
        }
    }
    if (material < 0)
    {
        U3dMaterial m;
        m.name = matName;
        m.diffuse = QVector3D(1.0f, 1.0f, 1.0f);
        m.emissive = QVector3D(0.15f, 0.15f, 0.15f);
        m.opacity = 1.0f;
        m.vertexColors = true;
        material = scene.materials.size();
        scene.materials.push_back(m);
    }

    U3dMesh mesh;
    mesh.name = QStringLiteral("OM%1").arg(scene.meshes.size());
    mesh.materialIndex = material;
    for (size_t rank = 0; rank < order.size(); ++rank)
    {
        const Group& g = groups[static_cast<size_t>(order[rank])];
        const QVector3D bias = -view * (static_cast<float>(rank) * 0.6f * std::max(lineWidth, 1.0e-9f));
        const float r = g.key.r / 255.0f, gr = g.key.g / 255.0f, b = g.key.b / 255.0f;
        for (size_t i = 0; i + 3 <= g.positions.size(); i += 3)
        {
            mesh.positions.push_back(g.positions[i] + bias.x());
            mesh.positions.push_back(g.positions[i + 1] + bias.y());
            mesh.positions.push_back(g.positions[i + 2] + bias.z());
            mesh.colors.push_back(r);
            mesh.colors.push_back(gr);
            mesh.colors.push_back(b);
            mesh.colors.push_back(1.0f);
        }
    }
    const int vertexCount = mesh.positions.size() / 3;
    mesh.indices.reserve(vertexCount);
    for (int i = 0; i < vertexCount; ++i)
        mesh.indices.push_back(static_cast<quint32>(i));

    U3dNode node;
    node.displayName = graphic.name;
    node.name = QStringLiteral("O%1_%2").arg(item, 4, 10, QLatin1Char('0')).arg(asciiName(graphic.name));
    if (!graphic.folder.isEmpty())
    {
        const QString folder = asciiName(graphic.folder);
        if (!scene.groups.contains(folder))
        {
            scene.groups.append(folder);
            scene.groupParents.append(QString());
        }
        node.parent = folder;
    }
    node.meshIndex = scene.meshes.size();
    node.overlay = true;
    node.overlayItem = item;
    node.follows = graphic.followsNodes;
    scene.meshes.push_back(mesh);
    scene.nodes.push_back(node);
    return 1;
}

} // namespace Pdf3D
