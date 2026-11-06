#pragma once
#include "Document.h"
namespace ThreeDXml {
class TessellatedReader {
public:
    static Geometry read(const QByteArray& member, const Options& options);
};
}
