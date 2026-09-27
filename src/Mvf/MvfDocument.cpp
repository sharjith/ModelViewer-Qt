#include "MvfDocument.h"

#include <QJsonParseError>

namespace Mvf
{
QJsonObject toJson(const AssetInfo& asset)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("version"), asset.version);
    obj.insert(QStringLiteral("minReaderVersion"), asset.minReaderVersion);
    obj.insert(QStringLiteral("generator"), asset.generator);
    return obj;
}

QJsonObject toJson(const Document& document)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("asset"), toJson(document.asset));
    obj.insert(QStringLiteral("scene"), document.scene);
    obj.insert(QStringLiteral("scenes"), document.scenes);
    obj.insert(QStringLiteral("nodes"), document.nodes);
    obj.insert(QStringLiteral("meshes"), document.meshes);
    obj.insert(QStringLiteral("materials"), document.materials);
    obj.insert(QStringLiteral("textures"), document.textures);
    obj.insert(QStringLiteral("images"), document.images);
    obj.insert(QStringLiteral("samplers"), document.samplers);
    obj.insert(QStringLiteral("accessors"), document.accessors);
    obj.insert(QStringLiteral("bufferViews"), document.bufferViews);
    obj.insert(QStringLiteral("buffers"), document.buffers);

    if (!document.extensionsUsed.isEmpty())
        obj.insert(QStringLiteral("extensionsUsed"), document.extensionsUsed);
    if (!document.extensionsRequired.isEmpty())
        obj.insert(QStringLiteral("extensionsRequired"), document.extensionsRequired);
    if (!document.mvfSession.isEmpty())
        obj.insert(QStringLiteral("mvfSession"), document.mvfSession);

    return obj;
}

QByteArray toJsonBytes(const Document& document)
{
    return QJsonDocument(toJson(document)).toJson(QJsonDocument::Indented);
}

Document fromJson(const QJsonObject& obj)
{
    Document doc;

    const QJsonObject asset = obj[QStringLiteral("asset")].toObject();
    doc.asset.version          = asset[QStringLiteral("version")].toString(QStringLiteral("3.0"));
    doc.asset.generator        = asset[QStringLiteral("generator")].toString(QStringLiteral("ModelViewer"));
    doc.asset.minReaderVersion = asset[QStringLiteral("minReaderVersion")].toString(QStringLiteral("3.0"));

    doc.scene            = obj[QStringLiteral("scene")].toInt(0);
    doc.scenes           = obj[QStringLiteral("scenes")].toArray();
    doc.nodes            = obj[QStringLiteral("nodes")].toArray();
    doc.meshes           = obj[QStringLiteral("meshes")].toArray();
    doc.materials        = obj[QStringLiteral("materials")].toArray();
    doc.textures         = obj[QStringLiteral("textures")].toArray();
    doc.images           = obj[QStringLiteral("images")].toArray();
    doc.samplers         = obj[QStringLiteral("samplers")].toArray();
    doc.accessors        = obj[QStringLiteral("accessors")].toArray();
    doc.bufferViews      = obj[QStringLiteral("bufferViews")].toArray();
    doc.buffers          = obj[QStringLiteral("buffers")].toArray();
    doc.extensionsUsed   = obj[QStringLiteral("extensionsUsed")].toArray();
    doc.extensionsRequired = obj[QStringLiteral("extensionsRequired")].toArray();
    doc.mvfSession       = obj[QStringLiteral("mvfSession")].toObject();

    return doc;
}

Document fromJsonBytes(const QByteArray& bytes)
{
    QJsonParseError err;
    const QJsonDocument jdoc = QJsonDocument::fromJson(bytes, &err);
    if (jdoc.isNull() || !jdoc.isObject())
    {
        qWarning() << "Mvf::fromJsonBytes: JSON parse error:" << err.errorString();
        return {};
    }
    return fromJson(jdoc.object());
}
}

