// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <Quantity_Color.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Inventor {

struct PartAppearance {
    std::optional<Quantity_Color> body;
    std::vector<std::pair<TopoDS_Face, Quantity_Color>> faces;
};

// Read the saved closed analytic solids in body record order, in millimeters.
// Unsupported profiles and invalid shapes throw instead of returning partial geometry.
// Appearance is optional and applies only to the supported single-solid profile.
std::vector<TopoDS_Solid> readPartSolids(const QString& path, const std::function<bool()>& cancelled = {},
                                       PartAppearance* appearance = nullptr);

} // namespace Inventor
