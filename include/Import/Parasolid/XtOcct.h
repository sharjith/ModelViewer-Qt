// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Stages X3-X5 of the Parasolid X_T importer plan: OCCT shapes from a decoded XT model.
// Depends on OpenCASCADE only (no Qt). See research/parasolid/PARASOLID_IMPLEMENTATION_PLAN.md.
#pragma once

#include "XtModel.h"

#include <TopLoc_Location.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Xt {

struct BuildOptions {
    // XT lengths are metres. All lengths of the output shapes are multiplied by this
    // (1000 gives millimetres, the unit the rest of the viewer assumes).
    double lengthScale = 1000.0;
    // Samples per edge used to measure how far edge curves lie from their face surfaces.
    // 0 turns the measurement off.
    int deviationSamples = 0;
    bool retainDisplayCandidates = false;
    // Return true to cancel; polled between bodies and about every 256 faces.
    std::function<bool()> cancelled;
    // Called as faces are built: (faces done, faces in the file).
    std::function<void(size_t, size_t)> progress;
};

struct BuildStats {
    size_t bodies = 0, solidBodies = 0, sheetBodies = 0, wireBodies = 0;
    size_t faces = 0, facesBuilt = 0;
    size_t edges = 0, edgesBuilt = 0;
    size_t tolerantEdgesRebuilt = 0;     // 3D curve rebuilt from the SP-curve of a fin
    size_t tolerantEdgesSkipped = 0;     // edge has no 3D curve (geometry on its fins only)
    size_t edgeDirectionFlips = 0;       // vertex order disagreed with the curve sense; the reversed curve fitted
    size_t edgeDirectionConflicts = 0;   // vertex order disagrees with the curve direction
    size_t missingOccurrences = 0;        // occurrences whose part is missing, was not built, or was cut off
    size_t instancesWithUnsupportedTransform = 0; // reflection / general affine: placed untransformed
    size_t facesFilledFromBoundary = 0;  // faces whose own surface could not be built; a surface was filled through their boundary
    size_t approximatedGeometry = 0;     // fitted blends, fitted intersection charts, curves rebuilt from SP-curves
    size_t seamRepairedFaces = 0;        // faces on closed surfaces that went through seam repair
    size_t authoredPCurves = 0;          // qualified fin pcurves attached to plane or cylinder faces
    size_t rejectedAuthoredPCurves = 0;  // candidate fin pcurves sent to projection instead
    size_t pcurveProjectionAttempts = 0;
    size_t openTrimWires = 0;            // plane and cylinder wires whose OCCT topology is open
    std::map<std::string, size_t> facesSkippedBySurface; // surface node name (or "none") -> faces
    std::map<std::string, size_t> edgesSkippedByCurve;
    std::map<std::string, size_t> orientationFlips; // surface kind -> faces whose wire/normal sense was corrected from parameter space
    std::map<std::string, size_t> surfacesBuilt;         // surface node name -> unique surfaces
    // Measured, not assumed: largest distance of an edge sample from its face surface per
    // surface kind, in output units (only when deviationSamples > 0).
    std::map<std::string, double> maxSurfaceDeviation;
    std::map<std::string, double> seconds;               // time spent per phase (seconds)
    double maxEdgeTolerance = 0;         // after tolerance update, output units
};

// How one XT FACE came out of the builder. Filled for every face; the standalone tool writes it as a table.
struct FaceOutcome {
    std::string surface;  // XT surface node name
    // native | native+rescued | native+rescued-tol-high | fill-v1 | fill-v2 | fill-planar (each optionally
    // +holes-capped) | broken->fill | broken->lost | omitted
    std::string origin;
    std::string reason;   // builder counters raised while this face was built (why native construction failed)
    const TopoDS_TShape* shape = nullptr; // the face in BuiltBody::shape (null when the face is not in the body)
};

struct Rgb {
    double r = 0.7, g = 0.7, b = 0.7;
    bool operator<(const Rgb& o) const { return r != o.r ? r < o.r : g != o.g ? g < o.g : b < o.b; }
};

struct BuiltBody {
    int64_t index = 0;          // XT node index of the BODY
    bool hasBodyColour = false; // SDL/TYSA_COLOUR_2 (or SDL/TYSA_COLOUR) on the BODY node
    Rgb bodyColour;
    std::map<int64_t, Rgb> faceColours; // XT FACE node index -> SDL/TYSA_COLOUR; faces without one use the body colour
    std::map<int64_t, FaceOutcome> faceOutcomes; // XT FACE node index -> outcome
    std::string name;
    int bodyType = 0;           // value of the BODY body_type field (solid/sheet/wire/general)
    TopoDS_Shape shape;         // solid, shell, wire or compound; never null
    std::vector<TopoDS_Face> displayCandidates; // rejected trims, for boundary meshing only
    std::map<const TopoDS_TShape*, int64_t> tracedFaceNodes;
    size_t facesTotal = 0, facesBuilt = 0;
};

// One assembly level above a placed part. `key` identifies the occurrence: the INSTANCE node that placed the
// assembly, or the negated ASSEMBLY node index for a top-level assembly. Two occurrences of one assembly
// have different keys, so they stay separate nodes in the scene tree.
struct InstanceStep {
    int64_t key = 0;
    std::string name;
};

struct BuiltInstance {
    size_t body = 0;            // index into BuildResult::bodies
    std::string name;           // the part (body) name
    TopLoc_Location location;
    std::vector<InstanceStep> path; // assemblies from the top down to this part; empty for a body with no assembly
};

struct BuildResult {
    std::vector<BuiltBody> bodies;
    // Placed parts. A file without assembly nodes yields one identity instance per body.
    std::vector<BuiltInstance> instances;
    BuildStats stats;
    std::vector<Diagnostic> diagnostics;
    bool cancelled = false;
};

// Builds one OCCT shape per BODY node. Never throws for malformed models; problems are
// recorded in BuildResult::diagnostics and the statistics.
BuildResult buildShapes(const Model& model, const BuildOptions& options = {});

} // namespace Xt
