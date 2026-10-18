#include "Importer.h"
#include "ArchiveReader.h"
#include "ProductStructureReader.h"
#include "SceneBuilder.h"

namespace ThreeDXml {
ImportResult Importer::read(const QString& path, const Options& options)
{
    options.check();
    ArchiveReader archive(path, options);
    const QString root = archive.rootDocument().isEmpty()
        ? ProductStructureReader::rootMember(archive.read(QStringLiteral("Manifest.xml")))
        : archive.rootDocument();
    const auto document = ProductStructureReader::read(archive.read(root), options);
    auto result = SceneBuilder::build(document, archive, options);
    bool hasUVR = false;
    for (const auto& rep : document.representations) hasUVR = hasUVR || rep.format == QLatin1String("UVR");
    if (hasUVR) result.warnings.append(QStringLiteral("3DXML tessellated UVR: candidate stored normals; exact normal quantizer unproven."));
    result.warnings.append(QStringLiteral("Source length unit not declared in supported layout; millimetres inferred. Use import units to override."));
    return result;
}
}
