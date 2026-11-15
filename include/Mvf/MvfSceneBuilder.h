#pragma once
class SceneMesh;


#include "MvfDocument.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QUuid>

#include <vector>

class SceneGraph;
class RenderableMesh;
struct GltfCameraData;

namespace Mvf
{
struct MVFPackage
{
    Document document;
    QByteArray geometryChunk;
    QByteArray imageChunk;
    // Files the session refers to that could not be embedded (missing on disk, unresolved or empty). The saved file keeps their
    // path only, so they will be missing wherever the original is not available. The caller reports them to the user.
    QStringList skippedImages;
};

MVFPackage buildMVFPackage(const SceneGraph& sceneGraph,
                                   const std::vector<SceneMesh*>& meshStore,
                                   const QSet<QUuid>& visibleMeshUuids,
                                   const QSet<QUuid>& selectedMeshUuids,
                                   const QVector<GltfCameraData>& cameraDataByFile = {},
                                   // Per-mesh RGBA (4 floats per vertex) written as COLOR_0 instead of the vertices'
                                   // own colours - the simulation results' displayed colours, so other glTF
                                   // viewers show them. The live meshes are not modified.
                                   const QHash<QUuid, std::vector<float>>& colorOverrides = {});
}
