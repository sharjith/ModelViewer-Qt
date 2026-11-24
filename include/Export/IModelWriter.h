#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "AssImpMeshExporter.h"

struct aiScene;
class SceneMesh;

// What an own model writer needs for one export. The scene is the aiScene that SceneGraphExporter::buildExportScene()
// produced (the same one the Assimp path gets); meshes are in 1:1 order with scene->mMeshes. Nothing here is owned.
struct ExportRequest
{
    const aiScene* scene = nullptr;
    const std::vector<SceneMesh*>* meshes = nullptr;
    const AssImpMeshExporter::ExportSettings* settings = nullptr;
    QString outputPath;
    QString formatId; // Assimp-style format id, for example "glb2" or "gltf2"
};

struct ExportResult
{
    bool success = false;
    QString message;       // the failure reason when !success
    QStringList warnings;  // things the file could not represent, listed for the log

    static ExportResult ok(const QStringList& warnings = QStringList())
    {
        ExportResult r;
        r.success = true;
        r.warnings = warnings;
        return r;
    }
    static ExportResult fail(const QString& reason, const QStringList& warnings = QStringList())
    {
        ExportResult r;
        r.message = reason;
        r.warnings = warnings;
        return r;
    }
};

// A writer for one file format family. Writers keep no state between exports (everything per export lives in the call).
class IModelWriter
{
public:
    virtual ~IModelWriter() = default;
    virtual ExportResult write(const ExportRequest& request) const = 0;
};
