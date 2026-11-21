// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

#include <assimp/scene.h>

#include <functional>
#include <memory>

namespace Inventor {

struct AssemblySceneOptions {
    // Strict: a component that cannot be found or loaded fails the whole import. Otherwise it is shown in the model tree as
    // an item that carries the reason, and the rest of the assembly loads.
    bool strict = false;
    std::function<bool()> cancelled;
};

struct AssemblySceneResult {
    std::unique_ptr<aiScene> scene;
    QStringList warnings; // shown under "Last Import Warnings"
};

// Builds the scene of an Inventor assembly (.iam): the occurrence tree with its sub-assemblies, every part placed where the
// assembly puts it. Each distinct part file is read and tessellated once; its occurrences share the geometry. Suppressed
// occurrences, and the ones hidden in the saved design view, are not loaded (the warnings say how many). Throws
// std::runtime_error when the assembly cannot be read at all, when no component can be loaded, in strict mode on the first
// component that cannot be, and on cancellation.
AssemblySceneResult buildAssemblyScene(const QString& assemblyPath, const AssemblySceneOptions& options);

} // namespace Inventor
