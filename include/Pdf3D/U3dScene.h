#pragma once

#include <QMatrix4x4>
#include <QString>
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
    QMatrix4x4 world;                // local-to-world; Qt layout, written as the row-vector matrix u3d expects
};

struct U3dScene
{
    QVector<U3dMaterial> materials;
    QVector<U3dMesh>     meshes;
    QVector<U3dNode>     nodes;

    bool isEmpty() const { return nodes.isEmpty() || meshes.isEmpty(); }

    // World-space axis aligned bounds of all nodes. Returns false when the scene has no geometry.
    bool computeBounds(QVector3D* outMin, QVector3D* outMax) const;
};

} // namespace Pdf3D
