#pragma once

#include <QString>

class IModelWriter;

// Maps a format id from the export dialog to the writer that handles it. Formats without an own writer are written by
// Assimp (AssImpMeshExporter), which stays the path for every format we do not write ourselves.
class ExportFormatRegistry
{
public:
    // The own writer for a format id, or nullptr when Assimp writes that format.
    //
    // While the glTF writer is being built (stages 2a-2e of research/export/EXPORTERS_PLAN.md) it is opt-in: set the
    // environment variable MODELVIEWER_OWN_GLTF_EXPORT=1 to route "glb2" and "gltf2" to it. When the writer reaches parity
    // (stage 2f) it becomes the default and this switch goes away together with the legacy path.
    static const IModelWriter* ownWriterFor(const QString& formatId);

    static bool ownGltfExportEnabled();
};
