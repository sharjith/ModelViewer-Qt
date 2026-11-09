#pragma once
#include "Document.h"
namespace ThreeDXml {
class ProductStructureReader {
public:
    static QString rootMember(const QByteArray& manifest);
    static Document read(const QByteArray& xml, const Options& options);
    // Reads a material document (CATMaterialRef) into material key -> rendering member name (not yet loaded).
    static QHash<QString, QString> materialRenderingMembers(const QString& document, const QByteArray& xml);
    // Parses a material's rendering representation (Osm).
    static MaterialRendering materialRendering(const QByteArray& xml);
};
}
