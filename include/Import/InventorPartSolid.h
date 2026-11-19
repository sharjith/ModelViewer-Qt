// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <TopoDS_Solid.hxx>

#include <functional>
#include <vector>

namespace Inventor {

// Read the saved closed analytic solids in body record order, in millimeters.
// Unsupported profiles and invalid shapes throw instead of returning partial geometry.
std::vector<TopoDS_Solid> readPartSolids(const QString& path, const std::function<bool()>& cancelled = {});

} // namespace Inventor
