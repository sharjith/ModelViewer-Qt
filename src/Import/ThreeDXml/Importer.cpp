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
    auto document = ProductStructureReader::read(archive.read(root), options);
    // Material documents and their rendering representations are optional companions: a missing or unreadable one
    // leaves the affected occurrences with their stored colours and is reported as a warning.
    QSet<QString> materialDocuments;
    for (const auto& link : document.materialLinks) materialDocuments.insert(link.materialDocument);
    for (const auto& materialDocument : materialDocuments) {
        options.check();
        try {
            const auto members = ProductStructureReader::materialRenderingMembers(materialDocument, archive.read(materialDocument));
            for (auto it = members.cbegin(); it != members.cend(); ++it) {
                try { document.materials.insert(it.key(), ProductStructureReader::materialRendering(archive.read(it.value()))); }
                catch (const Error& e) {
                    if (options.cancelled && options.cancelled()) throw;
                    document.warnings.append(QStringLiteral("Skipped material rendering %1: %2").arg(it.value(), QString::fromUtf8(e.what())));
                }
            }
        } catch (const Error& e) {
            if (options.cancelled && options.cancelled()) throw;
            document.warnings.append(QStringLiteral("Skipped material document %1: %2").arg(materialDocument, QString::fromUtf8(e.what())));
        }
    }
    auto result = SceneBuilder::build(document, archive, options);
    result.warnings.append(document.warnings);
    bool hasUVR = false;
    for (const auto& rep : document.representations) hasUVR = hasUVR || rep.format == QLatin1String("UVR");
    if (hasUVR) result.warnings.append(QStringLiteral("3DXML tessellated UVR: candidate stored normals; exact normal quantizer unproven."));
    result.warnings.append(QStringLiteral("Source length unit not declared in supported layout; millimetres inferred. Use import units to override."));
    return result;
}
}
