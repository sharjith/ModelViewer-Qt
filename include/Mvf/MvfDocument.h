#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QString>
#include <QVector3D>

#include <optional>

namespace Mvf
{
struct AssetInfo
{
    QString version = QStringLiteral("1.0");
    QString minReaderVersion = QStringLiteral("1.0");
    QString generator = QStringLiteral("ModelViewer");
};

struct Buffer
{
    QString name;
    QString chunk;
    quint64 byteLength = 0;
    QString compression;
    quint64 compressedByteLength = 0;
};

struct BufferView
{
    int buffer = -1;
    quint64 byteOffset = 0;
    quint64 byteLength = 0;
    quint32 byteStride = 0;
    QString name;
};

struct Accessor
{
    int bufferView = -1;
    quint64 byteOffset = 0;
    quint32 componentType = 0;
    quint64 count = 0;
    QString type;
    bool normalized = false;
    QString name;
};

struct Sampler
{
    QString name;
    int magFilter = 0;
    int minFilter = 0;
    int wrapS = 0;
    int wrapT = 0;
};

struct Image
{
    QString name;
    QString mimeType;
    int bufferView = -1;
    quint64 byteLength = 0;
    QString hash;
    QString originalUri;
};

struct Texture
{
    QString name;
    int image = -1;
    int sampler = -1;
};

struct TextureRef
{
    int texture = -1;
    int texCoord = 0;
    float scale = 1.0f;
    QVector3D transform = QVector3D(1.0f, 1.0f, 0.0f);
};

struct Material
{
    QString id;
    QString name;
    QString shadingModel;
    QString blendMode;
    bool doubleSided = false;
    QJsonObject pbr;
    QJsonObject extensions;
};

struct MeshPrimitive
{
    QJsonObject attributes;
    int indices = -1;
    int material = -1;
    int mode = 4;
};

struct Mesh
{
    QString id;
    QString name;
    QJsonArray primitives;
};

struct MeshBinding
{
    QString uuid;
    int mesh = -1;
    QString nameOverride;
    int materialOverride = -1;
    bool visible = true;
};

struct Node
{
    QString id;
    QString name;
    QJsonArray children;
    QJsonArray meshBindings;
    QMatrix4x4 matrix;
    bool hasMatrix = false;
};

struct Scene
{
    QString name;
    QJsonArray nodes;
};

struct SessionState
{
    QJsonArray visibleMeshUuids;
    QJsonArray selectedMeshUuids;
};

struct Document
{
    AssetInfo asset;
    int scene = 0;
    QJsonArray scenes;
    QJsonArray nodes;
    QJsonArray meshes;
    QJsonArray materials;
    QJsonArray textures;
    QJsonArray images;
    QJsonArray samplers;
    QJsonArray accessors;
    QJsonArray bufferViews;
    QJsonArray buffers;
    QJsonArray extensionsUsed;
    QJsonArray extensionsRequired;
    QJsonObject mvfSession;
};

QJsonObject toJson(const AssetInfo& asset);
QJsonObject toJson(const Document& document);
QByteArray toJsonBytes(const Document& document);

Document fromJson(const QJsonObject& obj);
Document fromJsonBytes(const QByteArray& bytes);
}

