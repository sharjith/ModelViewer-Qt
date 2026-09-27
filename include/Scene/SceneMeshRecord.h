#pragma once

#include <QUuid>

class SceneMesh;

// ---------------------------------------------------------------------------
// SceneMeshRecord
//
// One slot in the scene mesh store (_meshStore in SceneRuntime / ViewportWidget).
// Pairs a raw mesh pointer with its stable QUuid so callers can correlate
// meshes without extra lookups.
//
// Introduced in Phase 7 of the mesh/render/runtime separation refactor when
// _meshStore was changed from std::vector<SceneMesh*> to
// std::vector<SceneMeshRecord>.
//
// Callers should use the named fields/accessors so provenance stays explicit
// at the call site rather than treating a record as a raw pointer.
// ---------------------------------------------------------------------------
struct SceneMeshRecord
{
    SceneMesh* mesh = nullptr;
    QUuid      uuid;

    SceneMeshRecord() = default;
    SceneMeshRecord(SceneMesh* m, const QUuid& id) : mesh(m), uuid(id) {}

    SceneMesh* get() const { return mesh; }
};
