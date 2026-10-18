#include "Importer.h"
#include "ArchiveReader.h"
#include "ProductStructureReader.h"
#include "SceneBuilder.h"

namespace ThreeDXml {
ImportResult Importer::read(const QString& path, const Options& options)
{
    options.check();
    ArchiveReader archive(path, options);
    const QString root = ProductStructureReader::rootMember(archive.read(QStringLiteral("Manifest.xml")));
    auto result = SceneBuilder::build(ProductStructureReader::read(archive.read(root), options), archive, options);
    result.warnings.append(QStringLiteral("3DXML tessellated UVR: candidate stored normals; exact normal quantizer unproven."));
    result.warnings.append(QStringLiteral("Source length unit not declared in supported layout; millimetres inferred. Use import units to override."));
    return result;
}
}
