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

// One saved solid, or the reason it could not be built.
struct BodyOutcome {
    TopoDS_Solid solid;
    QString error; // Non-empty when the body (or one of its lumps) could not be built; solid is then null.
};

// Read the saved bodies in record order. With strict set the first body that fails to build throws, which is
// what readPartSolids does. Otherwise a failing body is reported in its place and the remaining bodies are still
// built; cancellation and running out of memory still throw. When no body can be built at all the first error
// is thrown. Appearance applies only to a single saved solid and is cleared when any body failed.
std::vector<BodyOutcome> readPartBodies(const QString& path, const std::function<bool()>& cancelled,
                                        PartAppearance* appearance, bool strict);

// Read the saved closed analytic solids in body record order, in millimeters.
// Unsupported profiles and invalid shapes throw instead of returning partial geometry.
// Appearance is optional and applies only to the supported single-solid profile.
std::vector<TopoDS_Solid> readPartSolids(const QString& path, const std::function<bool()>& cancelled = {},
                                       PartAppearance* appearance = nullptr);

} // namespace Inventor
