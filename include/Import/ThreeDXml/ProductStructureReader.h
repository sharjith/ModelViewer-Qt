#pragma once
#include "Document.h"
namespace ThreeDXml {
class ProductStructureReader {
public:
    static QString rootMember(const QByteArray& manifest);
    static Document read(const QByteArray& xml, const Options& options);
};
}
