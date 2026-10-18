#pragma once
#include "Document.h"
namespace ThreeDXml {
class RepresentationReader {
public:
    static Geometry read(const QByteArray& member, const Options& options);
};
}
