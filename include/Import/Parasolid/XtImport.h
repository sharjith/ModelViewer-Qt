// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Entry points that tie the XT reader, the model view and the OCCT builder together.
// OpenCASCADE and the standard library only (no Qt), so they can be tested standalone.
#pragma once

#include "XtOcct.h"
#include "XtReader.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <string_view>

namespace Xt {

struct ImportReport {
    Status decodeStatus = Status::Failed;
    std::string error;                     // user-facing reason when nothing could be built
    std::vector<Diagnostic> decodeDiagnostics;
    size_t records = 0;
    size_t unresolvedPointers = 0;
    std::string internalSchema;
    BuildResult build;
};

// Decodes a text X_T file held in memory (layouts: the local base-layout table) and builds shapes.
ImportReport importText(std::string_view bytes, const LayoutTable& baseLayouts, const BuildOptions& options = {},
                        const Options& readOptions = {});

// Decodes the XT data of a JT XT B-Rep element (neutral binary, searched for the 'P' 'S' 0 0 flag
// sequence) with the tracked JT table. Returns a compound of all bodies with their placements
// applied, or a null shape with `report.error` set.
TopoDS_Shape shapeFromJtElement(const char* data, size_t size, const BuildOptions& options, ImportReport& report);

} // namespace Xt
