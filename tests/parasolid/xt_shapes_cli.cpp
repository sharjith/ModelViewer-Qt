// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Owner inspection tool for stages X3-X5: builds OCCT shapes from XT data and prints, per
// body, what was built and how well it checks out. Output is meant to land under out/.
//
//   parasolid_xt_shapes <layouts.txt> <file.x_t> [--deviation N] [--dump out.brep]
//   parasolid_xt_shapes --jt-binary <raw-xt-element-file> [--deviation N] [--dump out.brep]
#include "XtJtLayouts.h"
#include "XtModel.h"
#include "XtOcct.h"
#include "XtReader.h"

#include <BRepBndLib.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeExtend_Status.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepCheck_Status.hxx>
#include <map>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Geom_BSplineSurface.hxx>
#include <cmath>
#include <ShapeFix_Face.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt2d.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <set>
#include <tuple>
#include <unordered_map>

static std::string readAll(const char* p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool analyzerValid(const TopoDS_Shape& s) { return BRepCheck_Analyzer(s).IsValid(); }

// BRepCheck can fault inside OCCT on some faces (an access violation, not a Standard_Failure). Returns 1 valid,
// 0 invalid, -1 when the check itself crashed.
static int checkGuarded(const TopoDS_Shape& s) {
#ifdef _MSC_VER
    __try {
        return analyzerValid(s) ? 1 : 0;
    } __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
        return -1;
    }
#else
    return analyzerValid(s) ? 1 : 0;
#endif
}

static double wireUvArea(const TopoDS_Face& face, const TopoDS_Wire& wire) {
    double area = 0;
    gp_Pnt2d first, prev;
    bool have = false;
    for (BRepTools_WireExplorer we(wire, face); we.More(); we.Next()) {
        const TopoDS_Edge e = we.Current();
        double p0, p1;
        const auto pc = BRep_Tool::CurveOnSurface(e, face, p0, p1);
        if (pc.IsNull()) return 0;
        const bool fwd = e.Orientation() == TopAbs_FORWARD;
        const int n = 16;
        for (int k = 0; k <= n; ++k) {
            const double t = fwd ? p0 + (p1 - p0) * k / n : p1 - (p1 - p0) * k / n;
            const gp_Pnt2d q = pc->Value(t);
            if (!have) { first = q; have = true; }
            else area += 0.5 * (prev.X() * q.Y() - q.X() * prev.Y());
            prev = q;
        }
    }
    if (have) area += 0.5 * (prev.X() * first.Y() - first.X() * prev.Y());
    return area;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int deviation = 0;
    std::string dump;
    std::string faceTable; // --face-table <out.tsv>: one row per XT face (origin, reason, BRepCheck, triangles); use with --mesh
    std::vector<std::string> positional;
    bool jt = false;
    bool faces = false;
    bool mesh = false;
    bool attribs = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--jt-binary") jt = true;
        else if (a == "--deviation" && i + 1 < argc) deviation = std::atoi(argv[++i]);
        else if (a == "--faces") faces = true;
        else if (a == "--mesh") mesh = true;
        else if (a == "--attribs") attribs = true;
        else if (a == "--dump" && i + 1 < argc) dump = argv[++i];
        else if (a == "--face-table" && i + 1 < argc) faceTable = argv[++i];
        else positional.push_back(a);
    }
    if ((jt && positional.size() != 1) || (!jt && positional.size() != 2)) {
        std::fprintf(stderr, "usage: %s <layouts.txt> <file.x_t> | --jt-binary <elem.bin> [--deviation N] [--dump out.brep]\n", argv[0]);
        return 2;
    }

    Xt::File file;
    const auto t0 = std::chrono::steady_clock::now();
    if (jt) {
        const std::string bytes = readAll(positional[0].c_str());
        const size_t at = bytes.find(std::string("PS\0\0", 4));
        if (at == std::string::npos) {
            std::fprintf(stderr, "no neutral-binary flag sequence found\n");
            return 1;
        }
        file = Xt::readNeutralBinary(std::string_view(bytes).substr(at), Xt::jtEmbeddedBaseLayouts());
    } else {
        Xt::LayoutTable table;
        std::string error;
        if (!table.parse(readAll(positional[0].c_str()), &error)) {
            std::fprintf(stderr, "layouts: %s\n", error.c_str());
            return 2;
        }
        const std::string bytes = readAll(positional[1].c_str());
        file = Xt::readText(bytes, table);
    }
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("decode: %s, %zu records, %.2f s\n", file.status == Xt::Status::Complete ? "complete" : "FAILED",
                file.nodes.size(), std::chrono::duration<double>(t1 - t0).count());
    if (file.status != Xt::Status::Complete) {
        for (const auto& d : file.diagnostics) std::printf("  %s: %s\n", d.code.c_str(), d.message.c_str());
        return 1;
    }

    const Xt::Model model(file);
    if (const char* bs = std::getenv("XT_DUMP_BSURF")) {
        // Research aid: the stored description of one B-spline surface (node index of the B_SURFACE).
        const Xt::Node* surf = model.find(std::atoll(bs));
        const Xt::Node* nurbs = surf ? model.find(model.pointer(*surf, model.slot("B_SURFACE", "nurbs"))) : nullptr;
        if (!nurbs) {
            std::printf("surface %s: no B_SURFACE/NURBS_SURF found\n", bs);
        } else {
            auto flag = [&](const char* f) { return static_cast<char>(model.value(*nurbs, model.slot("NURBS_SURF", f))); };
            auto numv = [&](const char* f) { return model.value(*nurbs, model.slot("NURBS_SURF", f)); };
            std::printf("surface %s: u_periodic=%c v_periodic=%c u_closed=%c v_closed=%c u_degree=%.0f v_degree=%.0f n_u=%.0f n_v=%.0f u_knot_type=%.0f v_knot_type=%.0f form=%.0f rational=%c\n",
                        bs, flag("u_periodic"), flag("v_periodic"), flag("u_closed"), flag("v_closed"), numv("u_degree"), numv("v_degree"),
                        numv("n_u_vertices"), numv("n_v_vertices"), numv("u_knot_type"), numv("v_knot_type"), numv("surface_form"), flag("rational"));
            for (const char* which : {"u_knots", "v_knots", "u_knot_mult", "v_knot_mult"}) {
                const Xt::Node* k = model.find(model.pointer(*nurbs, model.slot("NURBS_SURF", which)));
                std::printf("  %s:", which);
                for (size_t i = 0; k && i < k->valueCount && i < 24; ++i) std::printf(" %g", file.values[k->valueOffset + i]);
                std::printf("%s\n", k && k->valueCount > 24 ? " ..." : "");
            }
        }
    }
    if (std::getenv("XT_CHART_STATS")) {
        // Research aid: the chart of every INTERSECTION curve states its own chordal error (metres); report the spread.
        const Xt::Slot chordal = model.slot("CHART", "chordal_error"), count = model.slot("CHART", "chart_count"),
                       angular = model.slot("CHART", "angular_error");
        std::vector<double> errs, cnt;
        size_t nullErr = 0;
        for (const Xt::Node* c : model.nodesNamed("CHART")) {
            const double e = model.value(*c, chordal), n = model.value(*c, count);
            if (std::isnan(e) || e <= 0 || e > 1e10) { ++nullErr; continue; }
            errs.push_back(e * 1000.0); // millimetres
            cnt.push_back(n);
        }
        std::sort(errs.begin(), errs.end());
        std::printf("charts %zu, with chordal error %zu, null %zu\n", errs.size() + nullErr, errs.size(), nullErr);
        if (!errs.empty()) {
            auto q = [&](double f) { return errs[std::min(errs.size() - 1, static_cast<size_t>(f * errs.size()))]; };
            std::printf("chordal error (mm): min %.3g  p50 %.3g  p90 %.3g  p99 %.3g  max %.3g\n", errs.front(), q(0.5), q(0.9), q(0.99), errs.back());
            size_t above = 0;
            for (double e : errs) above += e > 1e-3;
            std::printf("charts with chordal error above 0.001 mm: %zu\n", above);
        }
    }
    if (const char* dumpOffset = std::getenv("XT_DUMP_OFFSET")) {
        const Xt::Node* o = model.find(std::atoll(dumpOffset));
        if (o) {
            const Xt::Node* u = model.find(model.pointer(*o, model.slot("OFFSET_SURF", "surface")));
            std::printf("offset %lld distance %.6g underlying %s(%lld)\n", static_cast<long long>(o->index), model.value(*o, model.slot("OFFSET_SURF", "offset")),
                        u ? model.typeName(u->type).c_str() : "none", u ? static_cast<long long>(u->index) : 0LL);
        }
    }
    if (const char* dumpBlend = std::getenv("XT_DUMP_BLEND")) {
        // Research aid: parameters of a BLENDED_EDGE surface node.
        const Xt::Node* b = model.find(std::atoll(dumpBlend));
        if (b) {
            auto kind = [&](double idx) { const Xt::Node* n = model.find(static_cast<int64_t>(idx)); return n ? model.typeName(n->type) : std::string("none"); };
            std::printf("blend %lld type '%c' sense %.0f range %.6g %.6g\n", static_cast<long long>(b->index), static_cast<int>(model.value(*b, model.slot("BLENDED_EDGE", "blend_type"))),
                        model.value(*b, model.slot("BLENDED_EDGE", "sense")), model.value(*b, model.slot("BLENDED_EDGE", "range"), 0), model.value(*b, model.slot("BLENDED_EDGE", "range"), 1));
            std::printf("  supports %s(%lld) %s(%lld) spine %s\n", kind(model.value(*b, model.slot("BLENDED_EDGE", "surface"), 0)).c_str(), static_cast<long long>(model.value(*b, model.slot("BLENDED_EDGE", "surface"), 0)),
                        kind(model.value(*b, model.slot("BLENDED_EDGE", "surface"), 1)).c_str(), static_cast<long long>(model.value(*b, model.slot("BLENDED_EDGE", "surface"), 1)),
                        (kind(model.value(*b, model.slot("BLENDED_EDGE", "spine"))) + "(" + std::to_string(static_cast<long long>(model.value(*b, model.slot("BLENDED_EDGE", "spine")))) + ")").c_str());
        }
    }
    if (const char* dumpEdge = std::getenv("XT_DUMP_EDGE")) {
        // Research aid: kind of curve an EDGE node uses and, for an intersection, its two surfaces and chart size.
        const Xt::Node* e = model.find(std::atoll(dumpEdge));
        if (e) {
            const Xt::Node* c = model.typeName(e->type) == "INTERSECTION" ? e : model.find(model.pointer(*e, model.slot("EDGE", "curve")));
            std::printf("edge %lld curve node %lld type %s\n", static_cast<long long>(e->index), c ? static_cast<long long>(c->index) : 0LL, c ? model.typeName(c->type).c_str() : "none");
            if (c && model.typeName(c->type) == "TRIMMED_CURVE") {
                const Xt::Node* basis = model.find(model.pointer(*c, model.slot("TRIMMED_CURVE", "basis_curve")));
                std::printf("  trimmed basis %s node %lld parm %.9g..%.9g\n", basis ? model.typeName(basis->type).c_str() : "none", basis ? static_cast<long long>(basis->index) : 0LL,
                            model.value(*c, model.slot("TRIMMED_CURVE", "parm_1")), model.value(*c, model.slot("TRIMMED_CURVE", "parm_2")));
                if (basis) c = basis;
            }
            if (c && model.typeName(c->type) == "INTERSECTION") {
                for (int k = 0; k < 2; ++k) {
                    const Xt::Node* sf = model.find(static_cast<int64_t>(model.value(*c, model.slot("INTERSECTION", "surface"), k)));
                    std::printf("  surface %d: %s node %lld\n", k, sf ? model.typeName(sf->type).c_str() : "none", sf ? static_cast<long long>(sf->index) : 0LL);
                }
                const Xt::Node* ch = model.find(model.pointer(*c, model.slot("INTERSECTION", "chart")));
                if (ch) std::printf("  chart count %.0f chordal_error %.6g\n", model.value(*ch, model.slot("CHART", "chart_count")), model.value(*ch, model.slot("CHART", "chordal_error")));
            }
        }
    }
    if (const char* dumpFace = std::getenv("XT_DUMP_FACE")) {
        // Research aid: which shell holds the given FACE node, how many faces and which faces share its surface.
        const int64_t want = std::atoll(dumpFace);
        const Xt::Slot shellFace = model.slot("SHELL", "face"), faceNext = model.slot("FACE", "next"), faceSurface = model.slot("FACE", "surface"),
                       faceLoop = model.slot("FACE", "loop"), loopNext = model.slot("LOOP", "next"), faceSense = model.slot("FACE", "sense");
        int64_t wantSurface = 0;
        {
            // All shells whose faces have node indexes close to the wanted face: a body can hold several shells.
            std::printf("shells near face %lld:", static_cast<long long>(want));
            for (const Xt::Node* sh : model.nodesNamed("SHELL")) {
                size_t n = 0;
                int64_t lo = INT64_MAX, hi = 0;
                for (const Xt::Node* f = model.find(model.pointer(*sh, shellFace)); f && n < 100000; f = model.find(model.pointer(*f, faceNext)), ++n) {
                    lo = std::min(lo, f->index);
                    hi = std::max(hi, f->index);
                }
                if (n && lo < want + 3000 && hi > want - 3000) std::printf(" [shell %lld: %zu faces, nodes %lld..%lld]", static_cast<long long>(sh->index), n, static_cast<long long>(lo), static_cast<long long>(hi));
            }
            std::printf("\n");
        }
        for (const Xt::Node* sh : model.nodesNamed("SHELL")) {
            std::vector<const Xt::Node*> faces;
            for (const Xt::Node* f = model.find(model.pointer(*sh, shellFace)); f && faces.size() < 100000; f = model.find(model.pointer(*f, faceNext)))
                faces.push_back(f);
            bool has = false;
            for (const Xt::Node* f : faces) has = has || f->index == want;
            if (!has) continue;
            std::printf("shell %lld: %zu faces\n", static_cast<long long>(sh->index), faces.size());
            for (const Xt::Node* f : faces)
                if (f->index == want) wantSurface = model.pointer(*f, faceSurface);
            std::map<int64_t, std::vector<const Xt::Node*>> bySurface;
            for (const Xt::Node* f : faces) bySurface[model.pointer(*f, faceSurface)].push_back(f);
            std::printf("surface of face %lld: node %lld; faces on it: ", static_cast<long long>(want), static_cast<long long>(wantSurface));
            for (const Xt::Node* f : bySurface[wantSurface]) {
                size_t loops = 0;
                for (const Xt::Node* l = model.find(model.pointer(*f, faceLoop)); l && loops < 10000; l = model.find(model.pointer(*l, loopNext))) ++loops;
                std::printf("[%lld sense=%.0f loops=%zu] ", static_cast<long long>(f->index), model.value(*f, faceSense), loops);
            }
            if (const char* big = std::getenv("XT_DUMP_SURFACE")) {
                std::printf("\nfaces on surface %s:", big);
                for (const Xt::Node* f : bySurface[std::atoll(big)]) std::printf(" %lld", static_cast<long long>(f->index));
            }
            std::printf("\nsurfaces shared by more than one face in this shell: ");
            for (const auto& kv : bySurface) if (kv.second.size() > 1) std::printf("%lld(x%zu) ", static_cast<long long>(kv.first), kv.second.size());
            std::printf("\n");
        }
    }
    if (attribs) {
        auto str = [&](const Xt::Node* n, const Xt::Slot& s) {
            std::string out;
            if (!n) return out;
            for (size_t i = 0; i < n->valueCount; ++i) {
                const double v = model.value(*n, s, i);
                out += (v >= 32 && v < 127) ? static_cast<char>(v) : '?';
            }
            return out;
        };
        const Xt::Slot defOf = model.slot("ATTRIBUTE", "definition"), ownerOf = model.slot("ATTRIBUTE", "owner"),
                       fieldsOf = model.slot("ATTRIBUTE", "fields"), ident = model.slot("ATTRIB_DEF", "identifier"),
                       idStr = model.slot("ATT_DEF_ID", "string");
        std::map<std::string, size_t> hist;
        std::map<std::string, int> shown;
        for (const Xt::Node* a : model.nodesNamed("ATTRIBUTE")) {
            const Xt::Node* def = model.find(model.pointer(*a, defOf));
            const Xt::Node* id = def ? model.find(model.pointer(*def, ident)) : nullptr;
            const Xt::Node* owner = model.find(model.pointer(*a, ownerOf));
            const std::string key = str(id, idStr) + " on " + (owner ? model.typeName(owner->type) : std::string("?"));
            ++hist[key];
            if (key.find("TRANSLUC") != std::string::npos || key.find("ALPHA") != std::string::npos || key.find("OPAC") != std::string::npos) {
                const Xt::Node* fv = a->valueCount > fieldsOf.offset ? model.find(static_cast<int64_t>(model.value(*a, fieldsOf, 0))) : nullptr;
                char b[64];
                std::snprintf(b, sizeof b, " value=%.6g", fv && fv->valueCount ? file.values[fv->valueOffset] : -1.0);
                ++hist["VALUE " + key + b];
            }
            if (shown[key]++ < 2) {
                std::string sample;
                const size_t nf = a->valueCount > fieldsOf.offset ? a->valueCount - fieldsOf.offset : 0;
                for (size_t k = 0; k < nf && k < 4; ++k) {
                    const Xt::Node* fv = model.find(static_cast<int64_t>(model.value(*a, fieldsOf, k)));
                    if (!fv) { sample += " [-]"; continue; }
                    sample += " [" + model.typeName(fv->type) + ":";
                    for (size_t i = 0; i < fv->valueCount && i < 12; ++i) {
                        const double v = file.values[fv->valueOffset + i];
                        char b[40];
                        if (model.typeName(fv->type) == "CHAR_VALUES") std::snprintf(b, sizeof b, "%c", (v >= 32 && v < 127) ? static_cast<char>(v) : '?');
                        else std::snprintf(b, sizeof b, " %.6g", v);
                        sample += b;
                    }
                    sample += "]";
                }
                std::printf("  e.g. %s ->%s\n", key.c_str(), sample.c_str());
            }
        }
        for (const auto& kv : hist) std::printf("attr %-50s %zu\n", kv.first.c_str(), kv.second);
        return 0;
    }
    Xt::BuildOptions options;
    options.deviationSamples = deviation;
    const Xt::BuildResult r = Xt::buildShapes(model, options);
    const auto t2 = std::chrono::steady_clock::now();
    std::printf("build: %zu bodies (%zu solid, %zu sheet, %zu wire), %.2f s\n", r.stats.bodies, r.stats.solidBodies,
                r.stats.sheetBodies, r.stats.wireBodies, std::chrono::duration<double>(t2 - t1).count());
    std::printf("faces: %zu built of %zu; edges: %zu built of %zu; tolerant-skipped %zu; direction conflicts %zu\n",
                r.stats.facesBuilt, r.stats.faces, r.stats.edgesBuilt, r.stats.edges, r.stats.tolerantEdgesSkipped,
                r.stats.edgeDirectionConflicts);
    for (const auto& kv : r.stats.surfacesBuilt) std::printf("  surfaces built %-14s %zu\n", kv.first.c_str(), kv.second);
    for (const auto& kv : r.stats.facesSkippedBySurface) std::printf("  faces skipped  %-14s %zu\n", kv.first.c_str(), kv.second);
    for (const auto& kv : r.stats.orientationFlips) std::printf("  orientation corrected %-14s %zu\n", kv.first.c_str(), kv.second);
    for (const auto& kv : r.stats.edgesSkippedByCurve) std::printf("  edges skipped  %-14s %zu\n", kv.first.c_str(), kv.second);
    for (const auto& kv : r.stats.maxSurfaceDeviation) std::printf("  max edge-to-surface deviation %-12s %.6g\n", kv.first.c_str(), kv.second);
    for (const auto& kv : r.stats.seconds) std::printf("  time %-14s %.2f s\n", kv.first.c_str(), kv.second);
    for (const auto& d : r.diagnostics) std::printf("  diag %s: %s\n", d.code.c_str(), d.message.c_str());

    TopoDS_Compound all;
    BRep_Builder bb;
    bb.MakeCompound(all);
    size_t shown = 0, valid = 0, closedSolids = 0;
    double totalVolume = 0;
    for (const auto& body : r.bodies) {
        bb.Add(all, body.shape);
        {
            Bnd_Box one;
            BRepBndLib::Add(body.shape, one);
            if (!one.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                one.Get(x0, y0, z0, x1, y1, z1);
                if (std::abs(x0) > 1e7 || std::abs(x1) > 1e7 || std::abs(y0) > 1e7 || std::abs(y1) > 1e7 || std::abs(z0) > 1e7 || std::abs(z1) > 1e7)
                    std::printf("  HUGE body %lld bbox (%g %g %g)-(%g %g %g)\n", static_cast<long long>(body.index), x0, y0, z0, x1, y1, z1);
            }
        }
        if (std::getenv("XT_CHECK_TRACE")) {
            // Names the step and face that crash BRepCheck (an access violation is not a Standard_Failure).
            int n = 0;
            for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next(), ++n) {
                const TopoDS_Face cf = TopoDS::Face(ex.Current());
                std::printf("  check face #%d surf=%d\n", n, static_cast<int>(BRepAdaptor_Surface(cf).GetType()));
                for (TopExp_Explorer ee(cf, TopAbs_EDGE); ee.More(); ee.Next()) {
                    const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                    double a = 0, b = 0, c0 = 0, c1 = 0;
                    const auto pc = BRep_Tool::CurveOnSurface(e, cf, a, b);
                    const auto c3 = BRep_Tool::Curve(e, c0, c1);
                    std::printf("    edge tol=%.4g sp=%d deg=%d pc=%s[%.5g,%.5g] c3=%s[%.5g,%.5g]\n", BRep_Tool::Tolerance(e), BRep_Tool::SameParameter(e) ? 1 : 0,
                                BRep_Tool::Degenerated(e) ? 1 : 0, pc.IsNull() ? "null" : pc->DynamicType()->Name(), a, b,
                                c3.IsNull() ? "null" : c3->DynamicType()->Name(), c0, c1);
                }
                BRepCheck_Analyzer(ex.Current()).IsValid();
            }
            std::printf("  per-face check done\n");
        }
        const int bodyCheck = checkGuarded(body.shape);
        const bool ok = bodyCheck == 1;
        valid += ok;
        if (bodyCheck < 0) std::printf("  body %lld: BRepCheck crashed\n", static_cast<long long>(body.index));
        if (bodyCheck == 0 && shown < 3) {
            BRepCheck_Analyzer an(body.shape);
            std::map<std::string, size_t> why;
            const TopAbs_ShapeEnum kinds[] = {TopAbs_VERTEX, TopAbs_EDGE, TopAbs_WIRE, TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID};
            const char* names[] = {"vertex", "edge", "wire", "face", "shell", "solid"};
            for (int k = 0; k < 6; ++k)
                for (TopExp_Explorer ex(body.shape, kinds[k]); ex.More(); ex.Next()) {
                    if (an.IsValid(ex.Current())) continue;
                    const auto& res = an.Result(ex.Current());
                    if (res.IsNull()) continue;
                    for (const auto& st : res->Status()) ++why[std::string(names[k]) + " status " + std::to_string(static_cast<int>(st))];
                }
            for (const auto& kv : why) std::printf("    invalid: %s x %zu\n", kv.first.c_str(), kv.second);
        }
        GProp_GProps vol, area;
        BRepGProp::VolumeProperties(body.shape, vol);
        BRepGProp::SurfaceProperties(body.shape, area);
        NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edgeFaces;
        TopExp::MapShapesAndAncestors(body.shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        size_t freeEdges = 0;
        for (int i = 1; i <= edgeFaces.Extent(); ++i)
            if (edgeFaces.FindFromIndex(i).Extent() == 1 && !BRep_Tool::IsClosed(TopoDS::Edge(edgeFaces.FindKey(i)), TopoDS::Face(edgeFaces.FindFromIndex(i).First()))) ++freeEdges;
        totalVolume += vol.Mass();
        if (freeEdges == 0 && body.facesBuilt) ++closedSolids;
        if (faces) {
            Bnd_Box bx;
            BRepBndLib::Add(body.shape, bx);
            double x0, y0, z0, x1, y1, z1;
            bx.Get(x0, y0, z0, x1, y1, z1);
            const gp_Pnt centre((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
            for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face f = TopoDS::Face(ex.Current());
                try {
                BRepAdaptor_Surface s(f);
                double u0, u1, v0, v1;
                BRepTools::UVBounds(f, u0, u1, v0, v1);
                gp_Pnt p;
                gp_Vec du, dv;
                s.D1((u0 + u1) / 2, (v0 + v1) / 2, p, du, dv);
                gp_Vec n = du.Crossed(dv);
                if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
                GProp_GProps fa;
                BRepGProp::SurfaceProperties(f, fa);
                std::printf("    face type=%d orient=%d area=%.5g outward=%+.3g uv=[%.4g %.4g]x[%.4g %.4g]%s\n", static_cast<int>(s.GetType()),
                            static_cast<int>(f.Orientation()), fa.Mass(), n.Normalized().Dot(gp_Vec(centre, p).Normalized()), u0, u1, v0, v1,
                            BRepCheck_Analyzer(f).IsValid() ? "" : " INVALID");
                } catch (const Standard_Failure& e) {
                    std::printf("    face diag failed: %s\n", e.GetMessageString());
                }
            }
        }
        if (shown++ < 100000)
            std::printf("  body %lld: faces %zu/%zu valid=%d free_edges=%zu volume=%.6g area=%.6g type=%d\n",
                        static_cast<long long>(body.index), body.facesBuilt, body.facesTotal, ok, freeEdges, vol.Mass(), area.Mass(),
                        body.bodyType);
    }
    Bnd_Box box;
    BRepBndLib::Add(all, box);
    if (!box.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        std::printf("bbox: (%.4g %.4g %.4g) - (%.4g %.4g %.4g)\n", x0, y0, z0, x1, y1, z1);
    }
    {
        size_t fv = 0, fi = 0;
        for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next())
            (checkGuarded(ex.Current()) == 1 ? fv : fi)++;
        if (std::getenv("XT_FACE_STATUS")) {
            std::map<std::string, size_t> why;
            for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
                BRepCheck_Analyzer an(ex.Current());
                if (an.IsValid()) continue;
                std::string key;
                const auto fr = an.Result(ex.Current());
                if (!fr.IsNull()) for (const auto& st : fr->Status()) key += "face" + std::to_string(static_cast<int>(st)) + " ";
                for (TopExp_Explorer ew(ex.Current(), TopAbs_WIRE); ew.More(); ew.Next()) {
                    const auto wr = an.Result(ew.Current());
                    if (!wr.IsNull()) for (const auto& st : wr->Status()) if (st != BRepCheck_NoError) key += "wire" + std::to_string(static_cast<int>(st)) + " ";
                }
                ++why[key];
            }
            for (const auto& kv : why) std::printf("  invalid face: %-40s %zu\n", kv.first.c_str(), kv.second);
        }
        std::printf("face check: %zu valid, %zu invalid\n", fv, fi);
    }
    std::printf("summary: bodies %zu, BRepCheck valid %zu, bodies without free edges %zu, total volume %.6g\n", r.bodies.size(),
                valid, closedSolids, totalVolume);
    if (mesh && std::getenv("XT_MESH_PER_BODY")) {
        IMeshTools_Parameters params;
        params.Deflection = 0.1; params.Angle = 0.3; params.Relative = true; params.InParallel = false; params.AllowQualityDecrease = true;
        size_t bad = 0, good = 0;
        for (const auto& body : r.bodies) {
            std::fprintf(stderr, "meshing body %lld\n", static_cast<long long>(body.index));
            if (std::getenv("XT_MESH_PER_FACE")) {
                int k = 0;
                for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next(), ++k) {
                    std::fprintf(stderr, "  face #%d\n", k);
                    {
                        const TopoDS_Face ff = TopoDS::Face(ex.Current());
                        BRepAdaptor_Surface sa(ff);
                        int nw = 0, ne = 0, nopc = 0, ndeg = 0;
                        for (TopExp_Explorer ew(ff, TopAbs_WIRE); ew.More(); ew.Next()) ++nw;
                        for (TopExp_Explorer ee(ff, TopAbs_EDGE); ee.More(); ee.Next()) {
                            ++ne;
                            const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                            double c0, c1;
                            if (BRep_Tool::Degenerated(e)) ++ndeg;
                            if (BRep_Tool::CurveOnSurface(e, ff, c0, c1).IsNull()) ++nopc;
                        }
                        std::fprintf(stderr, "    type=%d wires=%d edges=%d nopcurve=%d degenerated=%d valid=%d\n", static_cast<int>(sa.GetType()), nw, ne, nopc, ndeg, BRepCheck_Analyzer(ff).IsValid() ? 1 : 0);
                        for (TopExp_Explorer ee(ff, TopAbs_EDGE); ee.More(); ee.Next()) {
                            const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                            double p0, p1, q0, q1;
                            const auto pc = BRep_Tool::CurveOnSurface(e, ff, p0, p1);
                            const auto c3 = BRep_Tool::Curve(e, q0, q1);
                            if (pc.IsNull() || c3.IsNull()) { std::fprintf(stderr, "      edge pcnull=%d c3null=%d\n", pc.IsNull(), c3.IsNull()); continue; }
                            const gp_Pnt2d a = pc->Value(p0), b = pc->Value(p1);
                            const gp_Pnt sa0 = sa.Value(a.X(), a.Y()), sb0 = sa.Value(b.X(), b.Y());
                            std::fprintf(stderr, "      edge orient=%d pc[%.5g %.5g] uv0=(%.5g %.5g) uv1=(%.5g %.5g) c3[%.5g %.5g] d0=%.3g d1=%.3g seam=%d\n", static_cast<int>(e.Orientation()), p0, p1, a.X(), a.Y(), b.X(), b.Y(), q0, q1,
                                         sa0.Distance(c3->Value(q0)), sb0.Distance(c3->Value(q1)), BRep_Tool::IsClosed(e, ff) ? 1 : 0);
                        }
                    }
                    BRepMesh_IncrementalMesh one(ex.Current(), params);
                }
            }
            BRepMesh_IncrementalMesh mesher(body.shape, params);
            size_t tris = 0;
            for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next()) {
                TopLoc_Location loc;
                const auto tri = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
                if (!tri.IsNull()) tris += static_cast<size_t>(tri->NbTriangles());
            }
            (tris ? good : bad)++;
        }
        std::printf("per-body mesh: %zu bodies meshed, %zu without triangles\n", good, bad);
    }
    if (mesh) {
        const auto m0 = std::chrono::steady_clock::now();
        IMeshTools_Parameters params;
        params.Deflection = std::getenv("XT_MESH_DEFL") ? std::atof(std::getenv("XT_MESH_DEFL")) : 0.1;
        params.Angle = std::getenv("XT_MESH_ANGLE") ? std::atof(std::getenv("XT_MESH_ANGLE")) : 0.3;
        params.Relative = true;
        params.InParallel = true;
        params.AllowQualityDecrease = true;
        BRepMesh_IncrementalMesh mesher(all, params);
        size_t tris = 0, facesMeshed = 0, facesTotal = 0;
        for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
            ++facesTotal;
            TopLoc_Location loc;
            const auto tri = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
            if (!tri.IsNull() && tri->NbTriangles() > 0) { ++facesMeshed; tris += static_cast<size_t>(tri->NbTriangles()); }
        }
        {   // the viewer rejects a triangulation that strays outside its edges (XtSceneImporter hasUsableTriangulation)
            size_t rejected = 0;
            for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face face = TopoDS::Face(ex.Current());
                TopLoc_Location loc;
                const auto tri = BRep_Tool::Triangulation(face, loc);
                if (tri.IsNull() || tri->NbTriangles() == 0) continue;
                Bnd_Box tb, eb;
                for (int i = 1; i <= tri->NbNodes(); ++i) tb.Add(tri->Node(i).Transformed(loc.Transformation()));
                for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) BRepBndLib::AddOptimal(ee.Current(), eb, false, false);
                bool ok = true;
                if (tb.IsVoid() || tb.IsOpen()) ok = false;
                else if (!eb.IsVoid()) {
                    if (eb.IsOpen()) ok = false;
                    else {
                        double x0, y0, z0, x1, y1, z1;
                        eb.Get(x0, y0, z0, x1, y1, z1);
                        eb.Enlarge(0.5 * std::max({x1 - x0, y1 - y0, z1 - z0}) + 0.01);
                        tb.Get(x0, y0, z0, x1, y1, z1);
                        ok = !eb.IsOut(gp_Pnt(x0, y0, z0)) && !eb.IsOut(gp_Pnt(x1, y1, z1));
                    }
                }
                if (!ok) ++rejected;
            }
            std::printf("viewer usable-triangulation check rejects %zu faces\n", rejected);
        }
        std::printf("mesh: %zu of %zu faces triangulated, %zu triangles, %.2f s\n", facesMeshed, facesTotal, tris,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - m0).count());
        std::map<int, size_t> untriByType;
        for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
            TopLoc_Location loc;
            const auto tri = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
            if (tri.IsNull() || tri->NbTriangles() == 0)
                ++untriByType[static_cast<int>(BRepAdaptor_Surface(TopoDS::Face(ex.Current())).GetType())];
        }
        for (const auto& kv : untriByType)
            std::printf("  untriangulated surface type %d: %zu\n", kv.first, kv.second);
        if (std::getenv("XT_UNTRI")) {
            // Classify why faces were left without triangles.
            std::map<std::string, size_t> why;
            for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face face = TopoDS::Face(ex.Current());
                TopLoc_Location loc;
                const auto tri = BRep_Tool::Triangulation(face, loc);
                if (!tri.IsNull() && tri->NbTriangles() > 0) {
                    if (BRepAdaptor_Surface(face).GetType() == GeomAbs_BSplineSurface) {
                        const TopoDS_Wire outer = BRepTools::OuterWire(face);
                        ++why[std::string("MESHED t6") + (wireUvArea(face, outer) > 0 ? " ccw" : " cw") + (face.Orientation() == TopAbs_REVERSED ? " frev" : " ffwd")];
                    }
                    continue;
                }
                std::string key = "t" + std::to_string(static_cast<int>(BRepAdaptor_Surface(face).GetType()));
                BRepCheck_Analyzer an(face);
                if (!an.IsValid()) {
                    const auto fr = an.Result(face);
                    if (!fr.IsNull()) for (const auto& st : fr->Status()) key += " face" + std::to_string(static_cast<int>(st));
                    for (TopExp_Explorer ew(face, TopAbs_WIRE); ew.More(); ew.Next()) {
                        const auto wr = an.Result(ew.Current());
                        if (!wr.IsNull()) for (const auto& st : wr->Status()) if (st != BRepCheck_NoError) key += " wire" + std::to_string(static_cast<int>(st));
                    }
                } else key += " valid";
                double worst = 0;
                int nw = 0;
                for (TopExp_Explorer ew(face, TopAbs_WIRE); ew.More(); ew.Next()) ++nw;
                BRepAdaptor_Surface sa(face);
                for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                    const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                    if (BRep_Tool::Degenerated(e)) continue;
                    double p0, p1, q0, q1;
                    const auto pc = BRep_Tool::CurveOnSurface(e, face, p0, p1);
                    const auto c3 = BRep_Tool::Curve(e, q0, q1);
                    if (pc.IsNull() || c3.IsNull()) {
                        worst = std::max(worst, 1e9);
                        if (std::getenv("XT_UNTRI_EDGE") && !c3.IsNull()) {
                            const char* kind = c3->DynamicType()->Name();
                            std::printf("  nopc-edge: surf=%d curve=%s tol=%.3g len=%.4g\n", static_cast<int>(sa.GetType()), kind, BRep_Tool::Tolerance(e), q1 - q0);
                        }
                        continue;
                    }
                    for (int k = 0; k <= 4; ++k) {
                        const double s = k / 4.0;
                        const gp_Pnt2d uv = pc->Value(p0 + (p1 - p0) * s);
                        worst = std::max(worst, sa.Value(uv.X(), uv.Y()).Distance(c3->Value(q0 + (q1 - q0) * s)));
                    }
                }
                if (std::getenv("XT_UNTRI_WIRE") && worst < 1e8) {
                    static int shown = 0;
                    if (shown < 40) {
                        ++shown;
                        for (TopExp_Explorer ew(face, TopAbs_WIRE); ew.More(); ew.Next()) {
                            double gapUv = 0, gap3 = 0;
                            int ne = 0;
                            gp_Pnt2d prevEnd;
                            gp_Pnt prevEnd3;
                            gp_Pnt2d firstStart;
                            gp_Pnt firstStart3;
                            bool first = true;
                            for (BRepTools_WireExplorer we(TopoDS::Wire(ew.Current()), face); we.More(); we.Next()) {
                                const TopoDS_Edge e = we.Current();
                                double p0, p1;
                                const auto pc = BRep_Tool::CurveOnSurface(e, face, p0, p1);
                                if (pc.IsNull()) continue;
                                const bool fwd = e.Orientation() == TopAbs_FORWARD;
                                const gp_Pnt2d a = pc->Value(fwd ? p0 : p1), b = pc->Value(fwd ? p1 : p0);
                                const gp_Pnt a3 = sa.Value(a.X(), a.Y()), b3 = sa.Value(b.X(), b.Y());
                                if (first) { firstStart = a; firstStart3 = a3; first = false; }
                                else { gapUv = std::max(gapUv, a.Distance(prevEnd)); gap3 = std::max(gap3, a3.Distance(prevEnd3)); }
                                prevEnd = b; prevEnd3 = b3;
                                ++ne;
                            }
                            if (!first) { gapUv = std::max(gapUv, firstStart.Distance(prevEnd)); gap3 = std::max(gap3, firstStart3.Distance(prevEnd3)); }
                            double u0, u1, v0, v1;
                            BRepTools::UVBounds(face, u0, u1, v0, v1);
                            std::printf("  untri-wire: key=%s edges=%d uvgap=%.3g gap3d=%.3g uv=[%.4g %.4g]x[%.4g %.4g] uclosed=%d vclosed=%d uper=%d vper=%d\n", key.c_str(), ne, gapUv, gap3, u0, u1, v0, v1,
                                        sa.IsUClosed(), sa.IsVClosed(), sa.IsUPeriodic(), sa.IsVPeriodic());
                        }
                    }
                }
                if (std::getenv("XT_UNTRI_STATUS")) {
                    auto attempt = [&](const char* name, auto configure, bool fix) {
                        IMeshTools_Parameters params;
                        params.Deflection = 0.1; params.Angle = 0.3; params.Relative = true; params.InParallel = false; params.AllowQualityDecrease = true;
                        configure(params);
                        TopoDS_Face copy = TopoDS::Face(BRepBuilderAPI_Copy(face, true).Shape());
                        BRepTools::Clean(copy);
                        if (fix) {
                            ShapeFix_Face sff(copy);
                            sff.Perform();
                            copy = sff.Face();
                        }
                        BRepMesh_IncrementalMesh one(copy, params);
                        TopLoc_Location l2;
                        const auto t2 = BRep_Tool::Triangulation(copy, l2);
                        if (!t2.IsNull() && t2->NbTriangles()) key += std::string(" ok:") + name;
                    };
                    attempt("base", [](IMeshTools_Parameters&) {}, false);
                    attempt("abs0.5", [](IMeshTools_Parameters& p) { p.Relative = false; p.Deflection = 0.5; }, false);
                    attempt("abs0.05", [](IMeshTools_Parameters& p) { p.Relative = false; p.Deflection = 0.05; }, false);
                    attempt("noctrl", [](IMeshTools_Parameters& p) { p.ControlSurfaceDeflection = false; p.InternalVerticesMode = false; }, false);
                    attempt("fixface", [](IMeshTools_Parameters&) {}, true);
                    attempt("coarse", [](IMeshTools_Parameters& p) { p.Relative = false; p.Deflection = 5; p.Angle = 1.0; }, false);
                }
                {
                    double u0, u1, v0, v1, s0, s1, t0, t1;
                    BRepTools::UVBounds(face, u0, u1, v0, v1);
                    TopLoc_Location sl;
                    const auto hs = BRep_Tool::Surface(face, sl);
                    hs->Bounds(s0, s1, t0, t1);
                    const double eu = 1e-6 * (s1 - s0), ev = 1e-6 * (t1 - t0);
                    const bool uOut = !hs->IsUPeriodic() && (u0 < s0 - eu || u1 > s1 + eu);
                    const bool vOut = !hs->IsVPeriodic() && (v0 < t0 - ev || v1 > t1 + ev);
                    key += uOut || vOut ? " uv-outside" : " uv-inside";
                    key += hs->IsUPeriodic() || hs->IsVPeriodic() ? " periodic" : "";
                }
                {
                    int notSameParam = 0, notSameRange = 0, rangeMismatch = 0;
                    for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                        const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                        if (BRep_Tool::Degenerated(e)) continue;
                        notSameParam += BRep_Tool::SameParameter(e) ? 0 : 1;
                        notSameRange += BRep_Tool::SameRange(e) ? 0 : 1;
                        double p0, p1, q0, q1;
                        const auto pc = BRep_Tool::CurveOnSurface(e, face, p0, p1);
                        const auto c3 = BRep_Tool::Curve(e, q0, q1);
                        if (!pc.IsNull() && !c3.IsNull() && (std::abs(p0 - q0) > 1e-9 * (1 + std::abs(q0)) || std::abs(p1 - q1) > 1e-9 * (1 + std::abs(q1)))) ++rangeMismatch;
                    }
                    if (notSameParam) key += " notSameParam";
                    if (notSameRange) key += " notSameRange";
                    if (rangeMismatch) key += " rangeMismatch";
                }
                {
                    TopLoc_Location sl;
                    const auto hs = BRep_Tool::Surface(face, sl);
                    if (const auto bs = Handle(Geom_BSplineSurface)::DownCast(hs)) {
                        bool badPole = false, badWeight = false;
                        for (int i = 1; i <= bs->NbUPoles(); ++i)
                            for (int j = 1; j <= bs->NbVPoles(); ++j) {
                                const gp_Pnt q = bs->Pole(i, j);
                                if (!std::isfinite(q.X()) || !std::isfinite(q.Y()) || !std::isfinite(q.Z()) || std::abs(q.X()) > 1e5 || std::abs(q.Y()) > 1e5 || std::abs(q.Z()) > 1e5) badPole = true;
                                if (bs->IsURational() || bs->IsVRational()) if (!(bs->Weight(i, j) > 0)) badWeight = true;
                            }
                        key += " deg" + std::to_string(bs->UDegree()) + "x" + std::to_string(bs->VDegree());
                        key += bs->IsURational() || bs->IsVRational() ? " rational" : "";
                        key += bs->IsUPeriodic() || bs->IsVPeriodic() ? " bsperiodic" : "";
                        if (badPole) key += " BAD-POLE";
                        if (badWeight) key += " BAD-WEIGHT";
                        double a0, a1, b0, b1;
                        BRepTools::UVBounds(face, a0, a1, b0, b1);
                        const double su0 = bs->UKnot(1), su1 = bs->UKnot(bs->NbUKnots()), sv0 = bs->VKnot(1), sv1 = bs->VKnot(bs->NbVKnots());
                        const double fu = (a1 - a0) / (su1 - su0), fv = (b1 - b0) / (sv1 - sv0);
                        key += (fu < 0.01 || fv < 0.01) ? " tiny-uv-fraction" : " uv-fraction-ok";
                    }
                }
                {
                    TopLoc_Location sl;
                    const auto hs = BRep_Tool::Surface(face, sl);
                    double u0, u1, v0, v1;
                    hs->Bounds(u0, u1, v0, v1);
                    TopoDS_Face natural = BRepBuilderAPI_MakeFace(hs, u0, u1, v0, v1, 1e-6);
                    IMeshTools_Parameters params;
                    params.Deflection = 0.1; params.Angle = 0.3; params.Relative = true; params.InParallel = false; params.AllowQualityDecrease = true;
                    BRepMesh_IncrementalMesh one(natural, params);
                    TopLoc_Location l2;
                    const auto t2 = BRep_Tool::Triangulation(natural, l2);
                    key += !t2.IsNull() && t2->NbTriangles() ? " natural-ok" : " natural-fail";
                }
                {
                    const TopoDS_Wire outer = BRepTools::OuterWire(face);
                    const double a = wireUvArea(face, outer);
                    key += a > 0 ? " ccw" : " cw";
                    key += face.Orientation() == TopAbs_REVERSED ? " frev" : " ffwd";
                }
                key += worst > 1e8 ? " nopcurve" : worst > 0.05 ? " pcdev>0.05" : worst > 0.005 ? " pcdev>0.005" : " pcdev-ok";
                key += nw > 1 ? " multiwire" : "";
                ++why[key];
            }
            for (const auto& kv : why) std::printf("  untri: %-60s %zu\n", kv.first.c_str(), kv.second);
        }
    }
    if (mesh && std::getenv("XT_CONVERTER_LIKE")) {
        // What the viewer's converter does for faces the pre-pass left without triangulation.
        size_t retried = 0, fixed = 0;
        for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
            TopoDS_Face face = TopoDS::Face(ex.Current());
            TopLoc_Location loc;
            if (!BRep_Tool::Triangulation(face, loc).IsNull()) continue;
            ++retried;
            std::fprintf(stderr, "retry face %zu\n", retried);
            BRepTools::Clean(face);
            BRepLib::BuildCurves3d(face);
            BRepMesh_IncrementalMesh mesher(face, 0.5, false, 0.3, true);
            if (!BRep_Tool::Triangulation(face, loc).IsNull()) ++fixed;
        }
        std::printf("converter-like retry: %zu faces retried, %zu triangulated\n", retried, fixed);
    }
    if (mesh && std::getenv("XT_STRAY")) {
        // Faces whose triangles reach far outside the box of their own edges.
        std::map<int, size_t> byType;
        size_t stray = 0, shownStray = 0;
        for (TopExp_Explorer ex(all, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face face = TopoDS::Face(ex.Current());
            TopLoc_Location loc;
            const auto tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull() || tri->NbNodes() == 0) continue;
            Bnd_Box tb, eb;
            for (int i = 1; i <= tri->NbNodes(); ++i) tb.Add(tri->Node(i).Transformed(loc.Transformation()));
            for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) BRepBndLib::AddOptimal(ee.Current(), eb, false, false);
            if (tb.IsVoid() || eb.IsVoid()) continue;
            double a0, a1, a2, a3, a4, a5, b0, b1, b2, b3, b4, b5;
            tb.Get(a0, a1, a2, a3, a4, a5);
            eb.Get(b0, b1, b2, b3, b4, b5);
            const double tsz = std::max({a3 - a0, a4 - a1, a5 - a2}), esz = std::max({b3 - b0, b4 - b1, b5 - b2});
            if (esz > 3000 && shownStray < 12) {
                for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                    Bnd_Box one;
                    BRepBndLib::Add(ee.Current(), one, false);
                    if (one.IsVoid()) continue;
                    double c0, c1, c2, c3, c4, c5;
                    one.Get(c0, c1, c2, c3, c4, c5);
                    if (std::max({c3 - c0, c4 - c1, c5 - c2}) < 3000) continue;
                    BRepAdaptor_Curve ac(TopoDS::Edge(ee.Current()));
                    std::printf("    big edge curve-type=%d range=[%.5g %.5g] bbox-size=%.4g\n", static_cast<int>(ac.GetType()), ac.FirstParameter(), ac.LastParameter(), std::max({c3 - c0, c4 - c1, c5 - c2}));
                }
            }
            if (tsz > 4 * esz + 1.0 || tsz > 700 || esz > 700) {
                ++stray;
                BRepAdaptor_Surface s(face);
                ++byType[static_cast<int>(s.GetType())];
                if (shownStray++ < 8)
                    std::printf("  stray face type=%d tri-size=%.4g edge-size=%.4g tri-bbox=(%.4g %.4g %.4g)-(%.4g %.4g %.4g)\n", static_cast<int>(s.GetType()), tsz, esz, a0, a1, a2, a3, a4, a5);
            }
        }
        std::printf("stray faces: %zu\n", stray);
        for (const auto& kv : byType) std::printf("  stray by surface type %d: %zu\n", kv.first, kv.second);
    }
    if (std::getenv("XT_PRINT_TREE")) {
        // The scene tree the viewer will build: one line per placed part, indented by assembly depth.
        std::vector<int64_t> shown;
        for (const auto& inst : r.instances) {
            size_t common = 0;
            while (common < shown.size() && common < inst.path.size() && shown[common] == inst.path[common].key) ++common;
            shown.resize(common);
            for (size_t d = common; d < inst.path.size(); ++d) {
                std::printf("%*s[asm] %s\n", static_cast<int>(2 * d), "", inst.path[d].name.c_str());
                shown.push_back(inst.path[d].key);
            }
            const auto& colouredBody = r.bodies[inst.body];
            std::set<std::tuple<double, double, double>> distinct;
            for (const auto& kv : colouredBody.faceColours) distinct.emplace(kv.second.r, kv.second.g, kv.second.b);
            size_t translucent = 0;
            for (const auto& kv : colouredBody.faceColours) translucent += kv.second.a < 1.0 ? 1 : 0;
            std::printf("%*s%s  body-colour=%s(%.2f %.2f %.2f) face-colours=%zu on %zu of %zu faces, translucent faces=%zu (body opacity %.2f)\n", static_cast<int>(2 * inst.path.size()), "",
                        inst.name.c_str(), colouredBody.hasBodyColour ? "" : "none", colouredBody.bodyColour.r, colouredBody.bodyColour.g,
                        colouredBody.bodyColour.b, distinct.size(), colouredBody.faceColours.size(), colouredBody.faceOutcomes.size(), translucent,
                        colouredBody.bodyColour.a);
        }
    }
    if (const char* dumpFace = std::getenv("XT_DUMP_FACE_TRIM")) {
        // Research aid: surface periodicity, parameter bounds and the wire of one built face, in wire order.
        const int64_t want = std::atoll(dumpFace);
        for (const auto& body : r.bodies) {
            const auto o = body.faceOutcomes.find(want);
            if (o == body.faceOutcomes.end() || !o->second.shape) continue;
            for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next()) {
                if (ex.Current().TShape().get() != o->second.shape) continue;
                const TopoDS_Face f = TopoDS::Face(ex.Current());
                TopLoc_Location loc;
                const auto s = BRep_Tool::Surface(f, loc);
                double su0, su1, sv0, sv1;
                s->Bounds(su0, su1, sv0, sv1);
                double u0, u1, v0, v1;
                BRepTools::UVBounds(f, u0, u1, v0, v1);
                std::printf("face %lld: %s origin=%s\n  surface %s u[%.6g,%.6g] v[%.6g,%.6g] uperiodic=%d vperiodic=%d uclosed=%d vclosed=%d\n  face uv bounds u[%.6g,%.6g] v[%.6g,%.6g]\n",
                            static_cast<long long>(want), o->second.surface.c_str(), o->second.origin.c_str(), s->DynamicType()->Name(), su0, su1, sv0, sv1,
                            s->IsUPeriodic(), s->IsVPeriodic(), s->IsUClosed(), s->IsVClosed(), u0, u1, v0, v1);
                try {
                    // Area of the full uv rectangle of the face on its surface: the face can be no larger.
                    const TopoDS_Face rect = BRepBuilderAPI_MakeFace(s, std::max(u0, su0), std::min(u1, su1), std::max(v0, sv0), std::min(v1, sv1), 1e-6);
                    GProp_GProps rp;
                    BRepGProp::SurfaceProperties(rect, rp);
                    std::printf("  uv-rectangle surface area %.6g\n", rp.Mass());
                } catch (const Standard_Failure&) {}
                {
                    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> ef;
                    TopExp::MapShapesAndAncestors(body.shape, TopAbs_EDGE, TopAbs_FACE, ef);
                    std::map<int64_t, int> neighbours;
                    int unshared = 0;
                    for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                        const int idx = ef.FindIndex(ee.Current());
                        if (idx < 1) continue;
                        int others = 0;
                        for (const TopoDS_Shape& g : ef.FindFromIndex(idx)) {
                            if (g.IsSame(f)) continue;
                            ++others;
                            int64_t node = -1;
                            for (const auto& [n, oc] : body.faceOutcomes)
                                if (oc.shape == g.TShape().get()) { node = n; break; }
                            ++neighbours[node];
                        }
                        if (!others) ++unshared;
                    }
                    std::printf("  edges not shared with another face: %d; neighbours (node:edges):", unshared);
                    for (const auto& kv : neighbours) {
                        const auto on = body.faceOutcomes.find(kv.first);
                        std::printf(" %lld:%d(%s)", static_cast<long long>(kv.first), kv.second, on == body.faceOutcomes.end() ? "?" : on->second.surface.c_str());
                    }
                    std::printf("\n");
                }
                int wi = 0;
                for (TopExp_Explorer ew(f, TopAbs_WIRE); ew.More(); ew.Next(), ++wi) {
                    const TopoDS_Wire w = TopoDS::Wire(ew.Current());
                    std::printf("  wire %d closed=%d uv-area=%.6g\n", wi, w.Closed() ? 1 : 0, wireUvArea(f, w));
                    for (BRepTools_WireExplorer we(w, f); we.More(); we.Next()) {
                        const TopoDS_Edge e = we.Current();
                        double p0, p1;
                        const auto pc = BRep_Tool::CurveOnSurface(e, f, p0, p1);
                        if (pc.IsNull()) continue;
                        const bool fwd = e.Orientation() == TopAbs_FORWARD;
                        const gp_Pnt2d a = pc->Value(fwd ? p0 : p1), b = pc->Value(fwd ? p1 : p0);
                        std::printf("    %s uv (%.5g,%.5g)->(%.5g,%.5g) tol=%.3g free=%d\n", fwd ? "F" : "R", a.X(), a.Y(), b.X(), b.Y(), BRep_Tool::Tolerance(e),
                                    0);
                    }
                }
            }
        }
    }
    if (!faceTable.empty()) {
        std::FILE* ft = std::fopen(faceTable.c_str(), "w");
        if (!ft) {
            std::fprintf(stderr, "cannot write %s\n", faceTable.c_str());
        } else {
            std::fprintf(ft, "body\tface\tsurface\torigin\treason\tbrepcheck\ttriangles\tcx\tcy\tcz\tdiag\tstatus\tarea\tsurftype\tfree_edges\tmaxdih\n");
            std::map<std::string, size_t> summary; // "origin brepcheck meshed" -> faces
            for (const auto& body : r.bodies) {
                NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edgeFacesMap;
                TopExp::MapShapesAndAncestors(body.shape, TopAbs_EDGE, TopAbs_FACE, edgeFacesMap);
                std::unordered_map<const TopoDS_TShape*, TopoDS_Face> byShape;
                for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next())
                    byShape.emplace(ex.Current().TShape().get(), TopoDS::Face(ex.Current()));
                for (const auto& [node, o] : body.faceOutcomes) {
                    const char* check = "-";
                    std::string statusKey; // BRepCheck status codes: faceN wireN edgeN (distinct, sorted)
                    size_t tris = 0;
                    double cx = 0, cy = 0, cz = 0, diag = 0, faceArea = 0;
                    int freeEdges = 0; // edges of this face that no other face of the body shares (not seam or degenerate)
                    int surfType = -1;
                    double maxDihedral = 0; // largest angle (degrees) between normals of neighbouring triangles; a crumpled face scores high
                    const auto it = o.shape ? byShape.find(o.shape) : byShape.end();
                    if (it != byShape.end()) {
                        const TopoDS_Face& f = it->second;
                        try {
                            const int guarded = checkGuarded(f);
                            check = guarded == 1 ? "valid" : (guarded == 0 ? "invalid" : "check-crashed");
                            if (guarded == 0) {
                                BRepCheck_Analyzer an(f);
                                std::set<std::string> codes;
                                const auto fr = an.Result(f);
                                if (!fr.IsNull()) for (const auto& st : fr->Status()) if (st != BRepCheck_NoError) codes.insert("face" + std::to_string(static_cast<int>(st)));
                                for (TopExp_Explorer ew(f, TopAbs_WIRE); ew.More(); ew.Next()) {
                                    const auto wr = an.Result(ew.Current());
                                    if (!wr.IsNull()) for (const auto& st : wr->Status()) if (st != BRepCheck_NoError) codes.insert("wire" + std::to_string(static_cast<int>(st)));
                                    // The checks that depend on the face (closure and self-intersection in parameter space) are stored on the face context.
                                    if (!wr.IsNull() && wr->IsStatusOnShape(f)) for (const auto& st : wr->StatusOnShape(f)) if (st != BRepCheck_NoError) codes.insert("wireOnFace" + std::to_string(static_cast<int>(st)));
                                }
                                for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                                    const auto er = an.Result(ee.Current());
                                    if (!er.IsNull()) for (const auto& st : er->Status()) if (st != BRepCheck_NoError) codes.insert("edge" + std::to_string(static_cast<int>(st)));
                                    if (!er.IsNull()) for (const auto& st : er->StatusOnShape(f)) if (st != BRepCheck_NoError) codes.insert("edgeOnFace" + std::to_string(static_cast<int>(st)));
                                }
                                for (const auto& c : codes) statusKey += (statusKey.empty() ? "" : ",") + c;
                            }
                        } catch (const Standard_Failure&) {
                            check = "check-failed";
                        }
                        TopLoc_Location loc;
                        const auto tri = BRep_Tool::Triangulation(f, loc);
                        if (!tri.IsNull()) tris = static_cast<size_t>(tri->NbTriangles());
                        if (!tri.IsNull() && tri->NbTriangles() > 1) {
                            std::vector<gp_Vec> nrm(static_cast<size_t>(tri->NbTriangles()) + 1);
                            std::map<std::pair<int, int>, int> owner;
                            for (int t = 1; t <= tri->NbTriangles(); ++t) {
                                int n1, n2, n3;
                                tri->Triangle(t).Get(n1, n2, n3);
                                const gp_Vec nv = gp_Vec(tri->Node(n1), tri->Node(n2)).Crossed(gp_Vec(tri->Node(n1), tri->Node(n3)));
                                nrm[static_cast<size_t>(t)] = nv.Magnitude() > 1e-300 ? nv / nv.Magnitude() : gp_Vec(0, 0, 0);
                                const int ids[3] = {n1, n2, n3};
                                for (int k = 0; k < 3; ++k) {
                                    const int u = ids[k], v = ids[(k + 1) % 3];
                                    const std::pair<int, int> key(std::min(u, v), std::max(u, v));
                                    const auto found = owner.find(key);
                                    if (found == owner.end()) {
                                        owner.emplace(key, t);
                                    } else {
                                        if (nrm[static_cast<size_t>(t)].SquareMagnitude() < 0.5 || nrm[static_cast<size_t>(found->second)].SquareMagnitude() < 0.5)
                                            continue; // a zero-area triangle has no normal
                                        const double dot = std::clamp(nrm[static_cast<size_t>(t)].Dot(nrm[static_cast<size_t>(found->second)]), -1.0, 1.0);
                                        const double deg = std::acos(dot) * 180.0 / 3.14159265358979323846;
                                        maxDihedral = std::max(maxDihedral, deg);
                                        if (const char* wantFace = std::getenv("XT_DIHEDRAL_FACE"); wantFace && std::atoll(wantFace) == static_cast<long long>(node) && deg > 30.0) {
                                            const gp_Pnt c0 = tri->Node(u).Transformed(loc.Transformation());
                                            gp_Pnt2d uv(0, 0);
                                            if (tri->HasUVNodes()) uv = tri->UVNode(u);
                                            std::printf("fold %.0f deg at (%.1f %.1f %.1f) uv(%.4f %.4f)\n", deg, c0.X(), c0.Y(), c0.Z(), uv.X(), uv.Y());
                                        }
                                    }
                                }
                            }
                        }
                        for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                            const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                            if (BRep_Tool::Degenerated(e)) continue;
                            const int idx = edgeFacesMap.FindIndex(e);
                            if (idx > 0 && edgeFacesMap.FindFromIndex(idx).Size() == 1 && !BRep_Tool::IsClosed(e, f)) ++freeEdges;
                        }
                        try {
                            surfType = static_cast<int>(BRepAdaptor_Surface(f).GetType());
                            GProp_GProps ar;
                            BRepGProp::SurfaceProperties(f, ar);
                            faceArea = ar.Mass();
                        } catch (const Standard_Failure&) {
                        }
                        Bnd_Box fb;
                        BRepBndLib::Add(f, fb);
                        if (!fb.IsVoid()) {
                            double x0, y0, z0, x1, y1, z1;
                            fb.Get(x0, y0, z0, x1, y1, z1);
                            cx = 0.5 * (x0 + x1); cy = 0.5 * (y0 + y1); cz = 0.5 * (z0 + z1);
                            diag = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
                        }
                    }
                    std::fprintf(ft, "%lld\t%lld\t%s\t%s\t%s\t%s\t%zu\t%.4g\t%.4g\t%.4g\t%.4g\t%s\t%.8g\t%d\t%d\t%.1f\n", static_cast<long long>(body.index),
                                 static_cast<long long>(node), o.surface.c_str(), o.origin.c_str(), o.reason.c_str(), check, tris, cx, cy, cz, diag,
                                 statusKey.c_str(), faceArea, surfType, freeEdges, maxDihedral);
                    ++summary[o.origin + " | " + check + (tris ? " | meshed" : " | no-triangles")];
                }
            }
            std::fclose(ft);
            std::printf("face table: %s\n", faceTable.c_str());
            for (const auto& kv : summary) std::printf("  %-60s %zu\n", kv.first.c_str(), kv.second);
        }
    }
    if (std::getenv("XT_TRY_SHAPEFIX")) {
        // Experiment: run OCCT's own face repair, with the settings its STEP reader uses (automatic fix modes, precision from the
        // environment in millimetres, maximum tolerance 1 mm), on every face that BRepCheck rejects and count what it achieves.
        const double precision = std::getenv("XT_SHAPEFIX_PRECISION") ? std::atof(std::getenv("XT_SHAPEFIX_PRECISION")) : 1e-3;
        size_t invalid = 0, nowValid = 0, areaChanged = 0, dropped = 0, crashed = 0;
        std::map<std::string, size_t> byStatus, repairedByKind, invalidByKind;
        for (const auto& body : r.bodies) {
            for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face face = TopoDS::Face(ex.Current());
                if (checkGuarded(face) != 0) continue;
                ++invalid;
                std::string kind = "?";
                try { kind = std::to_string(static_cast<int>(BRepAdaptor_Surface(face).GetType())); } catch (...) {}
                ++invalidByKind[kind];
                double before = 0;
                try { GProp_GProps g; BRepGProp::SurfaceProperties(face, g); before = g.Mass(); } catch (...) {}
                try {
                    ShapeFix_Face fix(face);
                    fix.SetPrecision(precision);
                    fix.SetMaxTolerance(1.0);
                    fix.SetMinTolerance(1e-7);
                    fix.Perform();
                    const TopoDS_Shape result = fix.Result();
                    TopExp_Explorer fe(result, TopAbs_FACE);
                    if (!fe.More()) { ++dropped; continue; }
                    const TopoDS_Face repaired = TopoDS::Face(fe.Current());
                    const int ok = checkGuarded(repaired);
                    if (ok < 0) { ++crashed; continue; }
                    double after = 0;
                    try { GProp_GProps g; BRepGProp::SurfaceProperties(repaired, g); after = g.Mass(); } catch (...) {}
                    if (before > 1e-9 && std::abs(after - before) > 0.01 * std::abs(before)) ++areaChanged;
                    if (ok == 1) { ++nowValid; ++repairedByKind[kind]; }
                    std::string st;
                    for (int k = 1; k <= 8; ++k) if (fix.Status(static_cast<ShapeExtend_Status>(ShapeExtend_DONE1 + (k - 1)))) st += "D" + std::to_string(k);
                    ++byStatus[st.empty() ? "none" : st];
                } catch (const Standard_Failure&) { ++crashed; }
            }
        }
        std::printf("shapefix experiment (precision %.3g mm): %zu invalid faces; now valid %zu; area changed >1%% %zu; dropped %zu; failed %zu\n",
                    precision, invalid, nowValid, areaChanged, dropped, crashed);
        for (const auto& kv : invalidByKind)
            std::printf("  surface type %s: %zu invalid, %zu repaired\n", kv.first.c_str(), kv.second, repairedByKind[kv.first]);
        for (const auto& kv : byStatus) std::printf("  fix status %-12s %zu\n", kv.first.c_str(), kv.second);
    }
    if (!dump.empty()) BRepTools::Write(all, dump.c_str());
    return 0;
}
