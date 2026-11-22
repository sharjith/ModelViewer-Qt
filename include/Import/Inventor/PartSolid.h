// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <Quantity_Color.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
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
    QString error; // Non-empty when the body (or one of its lumps) could not be built as a solid; solid is then null.
    // Set when the faces that could be built are still available: a compound of faces, not a solid and without volume.
    // error then says what is missing. Null when nothing could be built.
    TopoDS_Shape incompleteShape;
    // True when incompleteShape is an open surface (a sheet body) rather than a solid that failed to build: every face
    // was built and the faces simply do not enclose a volume. error is then empty; there is nothing to warn about.
    bool isSurface = false;
    // The saved body record this outcome came from (0-based, among the part's bodies). A part keeps one record per body of
    // its browser (a solid body, the surface bodies of derived surfaces), and one record holds many lumps and shells.
    int bodyRecord = 0;
    // The body as the part's model browser names it ("Твердое тело1", "SRF5") and the derived-surface feature that owns it
    // ("SRF1::Производный"), when the browser lists exactly the bodies the file holds and this one matches by size. Empty
    // otherwise; the importer then falls back to numbered names.
    QString bodyName;
    QString featureName;
};

// Read the saved bodies in record order. With strict set the first body that fails to build throws, which is
// what readPartSolids does. Otherwise a failing body is reported in its place and the remaining bodies are still
// built; cancellation and running out of memory still throw. When no body can be built at all the first error
// is thrown. Appearance applies only to a single saved solid and is cleared when any body failed.
// progress, when given, is called with the fraction done (from 0 up to, not including, 1; rising, at most once per thousandth,
// though calls racing on worker threads may arrive slightly out of order). It may be called from a worker thread of the reader
// and must be thread-safe. It is not called with 1: the caller knows when it is done.
std::vector<BodyOutcome> readPartBodies(const QString& path, const std::function<bool()>& cancelled,
                                        PartAppearance* appearance, bool strict,
                                        const std::function<void(double)>& progress = {},
                                        const std::function<void(const QString&)>& stage = {});
// stage, when given, is called from the calling thread with a short, translated description of what the reader is doing now
// ("Building body 3 of 12", "Sewing 65034 faces", ...), for a status bar. It is called when the stage changes.

// Read the saved closed analytic solids in body record order, in millimeters.
// Unsupported profiles and invalid shapes throw instead of returning partial geometry.
// Appearance is optional and applies only to the supported single-solid profile.
std::vector<TopoDS_Solid> readPartSolids(const QString& path, const std::function<bool()>& cancelled = {},
                                       PartAppearance* appearance = nullptr);

} // namespace Inventor
