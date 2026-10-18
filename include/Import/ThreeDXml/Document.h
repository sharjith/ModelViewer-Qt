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
    void check() const {
        if (cancelled && cancelled()) throw Error(QStringLiteral("Model loading cancelled by user."));
    }
};
struct Representation { QString id, name, format, member; };
struct Instance { QString id, name, parent, reference; aiMatrix4x4 local; };
struct Reference { QString id, name; QStringList instances, representations; };
struct Document {
    QString root;
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
};
struct Body {
    quint64 offset = 0;
    std::vector<aiVector3D> positions, normals;
    std::vector<qint16> normalWords;
    std::vector<quint8> normalTags;
    std::vector<Face> faces;
};
struct BodyOccurrence { size_t body = 0; QString path; };
struct Geometry { std::vector<Body> bodies; std::vector<BodyOccurrence> occurrences; };
struct ImportResult {
    std::unique_ptr<aiScene> scene;
    // Pointer keys remain valid for the owned scene's lifetime. No name matching.
    QSet<const aiNode*> hiddenNodes;
    QHash<const aiNode*, QString> occurrencePaths;
    QStringList warnings;
};
}
