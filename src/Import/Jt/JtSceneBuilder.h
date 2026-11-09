// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Internal scene boundary: decoded JT objects to an owned render scene.
#pragma once
#include "Jt/JtImporter.h"
#include <JtNode_Partition.hxx>
namespace Jt::Detail {
ImportResult buildScene(const Handle(JtNode_Partition)& root,const QString& sourcePath,const Options& options={});
}
