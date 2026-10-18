#pragma once
#include "Document.h"
namespace ThreeDXml {
class ArchiveReader;
class SceneBuilder {
public:
    static ImportResult build(const Document& document, ArchiveReader& archive, const Options& options);
};
}
