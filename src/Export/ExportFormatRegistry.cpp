#include "ExportFormatRegistry.h"

#include "GltfWriter.h"

#include <QByteArray>

bool ExportFormatRegistry::ownGltfExportEnabled()
{
    return qEnvironmentVariable("MODELVIEWER_OWN_GLTF_EXPORT") == QLatin1String("1");
}

const IModelWriter* ExportFormatRegistry::ownWriterFor(const QString& formatId)
{
    static const GltfWriter gltfWriter{}; // stateless: everything per export lives in write()
    if (ownGltfExportEnabled() && (formatId == QLatin1String("glb2") || formatId == QLatin1String("gltf2")))
        return &gltfWriter;
    return nullptr;
}
