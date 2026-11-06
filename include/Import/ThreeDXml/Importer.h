#pragma once
#include "Document.h"
namespace ThreeDXml {
class Importer {
public:
    static ImportResult read(const QString& path, const Options& options = {});
};
}
