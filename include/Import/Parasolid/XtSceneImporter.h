// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Parasolid X_T files into an Assimp scene for the viewer: decode, build OCCT shapes, convert
// through the same B-Rep path STEP/IGES/BREP use.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

struct aiScene;

namespace Xt {

struct SceneImportOptions {
    std::function<bool()> cancelled;
    std::function<void(const QString&, int, int)> progress; // message, done, total
};

struct SceneImportResult {
    std::unique_ptr<aiScene> scene;
    QStringList warnings;
};

// Standalone X_T files need the base-layout table, which is not part of the repository (see
// research/parasolid/PARASOLID_PROVENANCE.md). Looks for it, in this order, in the environment
// variable MODELVIEWER_XT_LAYOUTS, the QSettings value "xtLayoutsFile", and a file named
// xt-layouts-v13.txt next to the application, in its data folder and in the per-user data folder.
// Returns an empty string when none exists; `tried` receives every path checked.
QString findBaseLayoutFile(QStringList* tried = nullptr);

// Throws std::runtime_error with a user-facing message when the file cannot be imported.
SceneImportResult importScene(const QString& path, const SceneImportOptions& options);

} // namespace Xt
