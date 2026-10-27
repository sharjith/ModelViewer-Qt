#include "Importer.h"
#include "ArchiveReader.h"
#include "ProductStructureReader.h"
#include "SceneBuilder.h"
#include <chrono>

namespace ThreeDXml {
ImportResult Importer::read(const QString& path, const Options& options)
{
    options.check();
    const auto startedAt = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(); };
    ArchiveReader archive(path, options);
    const double archiveMs = elapsed();
    const QString root = archive.rootDocument().isEmpty()
        ? ProductStructureReader::rootMember(archive.read(QStringLiteral("Manifest.xml")))
        : archive.rootDocument();
    auto document = ProductStructureReader::read(archive.read(root), options);
    const double structureMs = elapsed() - archiveMs;
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
    const double materialsMs = elapsed() - archiveMs - structureMs;
    auto result = SceneBuilder::build(document, archive, options);
    result.warnings.append(QStringLiteral("3DXML read timing: archive index %1 ms, product structure %2 ms, material documents %3 ms, scene build %4 ms.")
        .arg(qint64(archiveMs)).arg(qint64(structureMs)).arg(qint64(materialsMs)).arg(qint64(elapsed() - archiveMs - structureMs - materialsMs)));
    result.warnings.append(document.warnings);
    bool hasUVR = false;
    for (const auto& rep : document.representations) hasUVR = hasUVR || rep.format == QLatin1String("UVR");
    if (hasUVR) result.warnings.append(QStringLiteral("3DXML tessellated UVR: candidate stored normals; exact normal quantizer unproven."));
    result.warnings.append(QStringLiteral("Source length unit not declared in supported layout; millimetres inferred. Use import units to override."));
    return result;
}
}
