#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <assimp/scene.h>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

// Sample-derived tessellated UVR support. No vendor SDK or B-Rep reconstruction.
namespace ThreeDXml {
struct Error : std::runtime_error {
    explicit Error(const QString& message) : std::runtime_error(message.toUtf8().constData()) {}
};
struct Options {
    std::function<bool()> cancelled;
    std::function<void(const QString&, int, int)> progress;
    bool parsedNormals = true;
    bool edges = true; // read the exporter's edge polylines (shown in the edge display modes)
    void check() const {
        if (cancelled && cancelled()) throw Error(QStringLiteral("Model loading cancelled by user."));
    }
};
struct Representation { QString id, name, format, member; };
struct Instance { QString id, name, parent, reference; aiMatrix4x4 local; };
struct Reference { QString id, name; QStringList instances, representations; };
// Fields of a material's rendering representation (Osm RenderingFeature); defaults are the published ones.
// Texture fields are not read: none of the supported samples uses a texture.
struct MaterialRendering {
    aiColor3D diffuse{1.f, .75f, .07f}, specular{1.f, 1.f, 1.f}, emissive{1.f, 1.f, 1.f};
    double diffuseCoef = 1, specularCoef = 1, emissiveCoef = 0, specularExponent = 0, transparency = 0;
};
// One CATMatConnection: a material reference dressing (covering) the occurrence at `path` (root-to-leaf instance ids).
struct MaterialLink { QString materialDocument, materialReference, path; int layer = 0; };
struct Document {
    QString root;
    std::vector<MaterialLink> materialLinks;
    QHash<QString, MaterialRendering> materials; // key: "<document>#<reference id>"
    QStringList warnings;
    QHash<QString, Reference> references;
    QHash<QString, Representation> representations;
    QHash<QString, Instance> instances;
    QSet<QString> hiddenPaths;
    QHash<QString, aiColor4D> occurrenceColors;
};
struct Face {
    std::vector<std::array<unsigned, 3>> triangles;
    bool constant = false;
    aiVector3D normal;
    unsigned rgb = 0;
    // Opacity 0-255 from the stored face style (255 unless the record carries the 0x80 head flag, then the stored
    // byte); confirmed on one part against CATIA (transparency 200 = 0x37), taken
    // from sample observation, see research/3dxml/3DXML_published_documentation_findings.md.
    quint8 alphaByte = 255;
};
// Tail index groups of a mesh. Kinds 1 and 2 are polylines whose every segment is an edge of the body's own triangles
// (checked on all sample meshes), i.e. the exporter's edge lines; kind 0 is not interpreted.
struct LineGroup {
    unsigned rgb = 0;
    std::vector<std::vector<unsigned>> polylines;
};
struct Body {
    std::vector<LineGroup> lines;
    // Per-vertex texture coordinates (texCoordDimension floats each), present only when a file texture material is
    // applied; read but not used for display yet.
    std::vector<float> texCoords;
    unsigned texCoordDimension = 0;
    quint64 offset = 0;
    std::vector<aiVector3D> positions, normals;
    std::vector<qint16> normalWords;
    std::vector<quint8> normalTags;
    std::vector<Face> faces;
};
struct BodyOccurrence { size_t body = 0; QString path; };
struct Geometry { std::vector<Body> bodies; std::vector<BodyOccurrence> occurrences; };
// Edge segments of one mesh in the viewer's precomputed-edge layout: flat {x0,y0,z0,x1,y1,z1,...} in mesh vertex space,
// bounds[i] = first vec3 index of edge i, bounds.back() = total vec3 count.
struct MeshEdges { std::vector<float> segments; std::vector<int> bounds; };
struct ImportResult {
    std::unique_ptr<aiScene> scene;
    // Pointer keys remain valid for the owned scene's lifetime. No name matching.
    QSet<const aiNode*> hiddenNodes;
    QHash<const aiNode*, QString> occurrencePaths;
    QHash<const aiMesh*, std::shared_ptr<const MeshEdges>> meshEdges;
    QStringList warnings;
};
}
