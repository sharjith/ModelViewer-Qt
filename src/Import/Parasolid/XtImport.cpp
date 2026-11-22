// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "XtImport.h"

#include "XtJtLayouts.h"
#include "XtModel.h"

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

namespace Xt {

namespace {

std::string failureText(const File& file) {
    if (file.diagnostics.empty())
        return "the XT data could not be decoded";
    const Diagnostic& d = file.diagnostics.front();
    if (d.code == "legacy-schema-unsupported")
        return "this X_T file does not carry its own schema (" + d.message.substr(0, d.message.find(" has no")) +
               "), so its record layouts must come from the matching Parasolid schema file (sch_<number>.sch_txt). "
               "Either no schema folder is configured (Settings > Import/Export > Parasolid Importer, or the "
               "MODELVIEWER_XT_SCHEMA_DIR environment variable), or the configured folder has no file for this schema number. "
               "Alternatively re-export the model from the CAD system with an embedded schema, or as STEP";
    if (d.code == "header-missing" || d.code == "unknown-transport")
        return "this is not a text X_T file (or it is a binary X_B file, which is not supported yet)";
    return d.message + " (" + d.code + ")";
}

void finish(const File& file, const BuildOptions& options, ImportReport& report) {
    report.decodeStatus = file.status;
    report.decodeDiagnostics = file.diagnostics;
    report.records = file.nodes.size();
    report.unresolvedPointers = file.unresolvedPointers;
    report.internalSchema = file.header.internalSchema;
    if (file.status != Status::Complete) {
        report.error = failureText(file);
        return;
    }
    const Model model(file);
    report.build = buildShapes(model, options);
    if (report.build.cancelled)
        report.error = "cancelled";
    else if (report.build.bodies.empty())
        report.error = "the file decoded, but no geometry could be built from it";
}

} // namespace

ImportReport importText(std::string_view bytes, const LayoutTable& baseLayouts, const BuildOptions& options,
                        const Options& readOptions) {
    ImportReport report;
    const File file = readText(bytes, baseLayouts, readOptions);
    finish(file, options, report);
    return report;
}

TopoDS_Shape shapeFromJtElement(const char* data, size_t size, const BuildOptions& options, ImportReport& report) {
    const std::string_view all(data, size);
    const size_t at = all.find(std::string_view("PS\0\0", 4));
    if (at == std::string_view::npos) {
        report.error = "no XT flag sequence found in the JT element";
        return {};
    }
    const File file = readNeutralBinary(all.substr(at), jtEmbeddedBaseLayouts());
    finish(file, options, report);
    if (!report.error.empty())
        return {};
    // The caller replaces the JT file's stored tessellation with this shape, so it must be complete and
    // exact: every face built, nothing fitted or approximated, every body placed.
    {
        const BuildStats& s = report.build.stats;
        const bool complete = s.facesBuilt == s.faces && s.facesSkippedBySurface.empty() && s.edgesSkippedByCurve.empty() &&
                              s.tolerantEdgesSkipped == 0 && s.edgeDirectionConflicts == 0 && s.instancesWithUnsupportedTransform == 0 &&
                              s.approximatedGeometry == 0 && s.missingOccurrences == 0 && report.build.diagnostics.empty() &&
                              s.bodies == report.build.bodies.size() && !report.build.instances.empty();
        if (!complete) {
            report.error = "the XT data could not be rebuilt completely and exactly; the stored JT tessellation is used instead";
            return {};
        }
    }
    BRep_Builder bb;
    TopoDS_Compound compound;
    bb.MakeCompound(compound);
    for (const BuiltInstance& inst : report.build.instances) {
        TopoDS_Shape s = report.build.bodies[inst.body].shape;
        if (!inst.location.IsIdentity())
            s.Move(inst.location);
        bb.Add(compound, s);
    }
    return compound;
}

} // namespace Xt
