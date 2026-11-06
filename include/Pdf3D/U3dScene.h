#pragma once

#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

// In-memory scene description for the Save as 3D PDF export. Plain data (no Assimp, no u3d types): SceneToU3d.* fills it from
// the application's export scene, U3dWriter.* turns it into a Universal 3D (.u3d) byte stream, Pdf3DExporter.* embeds that
// stream in a PDF. See research/3dpdf/3D_PDF_EXPORT_RESEARCH.md (sections 8 and 9).
namespace Pdf3D
{

struct U3dMaterial
{
    QString   name;
    QVector3D diffuse{0.8f, 0.8f, 0.8f};
    QVector3D emissive{0.0f, 0.0f, 0.0f};
    float     opacity = 1.0f;
    bool      vertexColors = false;   // the mesh carries per-vertex colours that replace the diffuse colour
};

// A triangle mesh. positions and normals are 3 floats per vertex; normals may be empty (flat per-face normals are then
// generated); indices are 3 per triangle.
struct U3dMesh
{
    QString          name;           // unique, ASCII (becomes the U3D model resource name)
    QVector<float>   positions;
    QVector<float>   normals;
    QVector<float>   colors;         // optional: 4 floats (RGBA) per vertex, empty when the mesh has one colour
    QVector<quint32> indices;
    int              materialIndex = 0;
};

// One visible part: a mesh instance placed in world space. name is unique and ASCII; displayName is the user-facing name.
struct U3dNode
{
    QString    name;
    QString    displayName;
    int        meshIndex = -1;
    bool       overlay = false;      // a measurement or annotation graphic, not a part (kept out of the parts list and count)
    QString    parent;               // name of the group node this node sits in (empty: the scene root); the world matrix is then relative to it
    int        overlayItem = -1;     // for an overlay node: which measurement/annotation it belongs to (several nodes share one item)
    QStringList follows;             // for an overlay node: the part nodes (names) its item is attached to, so it can move with them
    QMatrix4x4 world;                // local-to-world; Qt layout, written as the row-vector matrix u3d expects
};

// A key frame animation of part nodes (U3D motion). Verified in Acrobat (research doc, animations): a key frame is NOT an absolute
// transform. It is a transform in the node's own frame that Acrobat applies BEFORE the node's own (rest) transform, so the node's
// resulting world matrix is  world = rest * key  in column-vector terms (p_world = rest * key * p_local). To make a node follow a pose
// M(t), the key at time t is  rest^-1 * M(t). `keys` holds those key matrices (Qt layout, column vectors); the writer decomposes them
// into displacement, rotation quaternion and scale.
struct U3dMotionTrack
{
    QString             node;      // the part node (U3dNode::name)
    QVector<float>      times;     // seconds, ascending
    QVector<QMatrix4x4> keys;      // one key matrix per time
};

struct U3dAnimation
{
    QString               name;
    QVector<U3dMotionTrack> tracks;
};

struct U3dScene
{
    QVector<U3dMaterial> materials;
    QVector<U3dMesh>     meshes;
    QVector<U3dNode>     nodes;
    // Group nodes: pure folders of the model tree (identity transform, so the world matrices of the nodes inside stay as they are).
    // groups[i] is the name, groupParents[i] the name of the group it sits in ("" = the root); a parent comes before its children.
    QStringList          groups;
    QStringList          groupParents;
    int                  overlayItems = 0;   // how many measurement/annotation items were added (OverlayToU3d)
    U3dAnimation         animation;          // at most one: Acrobat plays one animation per 3D annotation; no tracks = none

    bool hasAnimation() const { return !animation.tracks.isEmpty(); }

    bool isEmpty() const { return nodes.isEmpty() || meshes.isEmpty(); }

    // World-space axis aligned bounds of all nodes. Returns false when the scene has no geometry.
    bool computeBounds(QVector3D* outMin, QVector3D* outMax) const;
};

} // namespace Pdf3D
