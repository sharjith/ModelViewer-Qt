// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// OCCT shape construction from a decoded XT model. Conventions follow the Parasolid XT
// Format Reference (topology and geometry chapters); see
// research/parasolid/PARASOLID_IMPLEMENTATION_PLAN.md for the staging.
#include "XtOcct.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBndLib.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <ShapeAnalysis_Edge.hxx>
#include <BRep_TEdge.hxx>
#include <BRep_CurveRepresentation.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepGraph.hxx>
#include <BRepGraph_EditorView.hxx>
#include <BRepGraph_ShapesView.hxx>
#include <BRepGraph_Validate.hxx>
#include <GeomProjLib.hxx>
#include <Geom2d_Line.hxx>
#include <NCollection_LinearVector.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepCheck_Status.hxx>
#include <BRepLib.hxx>
#include <BRepLib_FindSurface.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_IntSS.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomAbs_Shape.hxx>
#include <BRepFill_Filling.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <optional>
#include <Geom_BSplineCurve.hxx>
#include <GeomLProp_SLProps.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Circle.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_Curve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_Surface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <NCollection_HArray1.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <BRepTools.hxx>
#include <ShapeFix.hxx>
#include <ShapeFix_Edge.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <ShapeFix_Wire.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Vec2d.hxx>

#include <algorithm>
#include <memory>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace Xt {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kXtTolerance = 1e-8; // modeller linear resolution, metres
constexpr double kMaxVertexGap = 2.5e-3; // metres; larger gaps between a curve end and its vertex are not absorbed
constexpr double kBlendContactTolerance = 2e-5; // maximum nested contact residual, metres

// Signed area (parameter space) enclosed by a wire of a face, from sampled pcurves. Positive means the wire
// runs counter-clockwise in (u, v), which is what OCCT expects of an outer wire stored on a face. Zero when
// a pcurve is missing.
double wireParameterArea(const TopoDS_Face& face, const TopoDS_Wire& wire) {
    double area = 0;
    gp_Pnt2d first, prev;
    bool have = false;
    for (TopoDS_Iterator it(wire); it.More(); it.Next()) {
        const TopoDS_Edge e = TopoDS::Edge(it.Value());
        double p0, p1;
        const Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(e, face, p0, p1);
        if (pc.IsNull())
            return 0;
        const bool forward = e.Orientation() == TopAbs_FORWARD;
        constexpr int n = 12;
        for (int k = 0; k <= n; ++k) {
            const gp_Pnt2d q = pc->Value(forward ? p0 + (p1 - p0) * k / n : p1 - (p1 - p0) * k / n);
            if (!have) {
                first = q;
                have = true;
            } else {
                area += 0.5 * (prev.X() * q.Y() - q.X() * prev.Y());
            }
            prev = q;
        }
    }
    if (have)
        area += 0.5 * (prev.X() * first.Y() - first.X() * prev.Y());
    return area;
}
static double pcurvePrecisionFromEnv() { const char* e = std::getenv("XT_PCURVE_PREC"); return e ? std::atof(e) : 1e-3; }

struct SurfaceInfo {
    Handle(Geom_Surface) surface;
    int flip = 1;         // OCCT natural normal = flip * XT natural normal
    int senseSign = 1;    // XT sense field of the surface node
    std::string kind;     // node name
    bool ok = false;
};

struct CurveInfo {
    Handle(Geom_Curve) curve; // natural XT parameterisation
    int senseSign = 1;
    bool trimmed = false;
    double parm1 = 0, parm2 = 0;
    std::string kind;
    bool ok = false;
};

struct PCurve {
    Handle(Geom2d_BSplineCurve) curve;
    double first = 0;
    double last = 0;
};

struct ScopedTimer {
    double& slot;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit ScopedTimer(double& s) : slot(s) {}
    ~ScopedTimer() { slot += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }
};

class Builder {
public:
    Builder(const Model& model, const BuildOptions& options, BuildResult& result)
        : m(model), opt(options), res(result), scale(options.lengthScale) {}

    void run();

private:
    // ---- model access -------------------------------------------------------------
    const Slot& sl(const char* node, const char* field) {
        std::string key = std::string(node) + "." + field;
        auto it = m_slots.find(key);
        if (it == m_slots.end())
            it = m_slots.emplace(std::move(key), m.slot(node, field)).first;
        return it->second;
    }
    double num(const Node& n, const char* node, const char* field, size_t k = 0) {
        return m.value(n, sl(node, field), k);
    }
    int64_t ptr(const Node& n, const char* node, const char* field, size_t k = 0) {
        const double v = m.value(n, sl(node, field), k);
        return (std::isnan(v) || v <= 0) ? 0 : static_cast<int64_t>(v);
    }
    int signOf(const Node& n, const char* node, const char* field) {
        return num(n, node, field) == '-' ? -1 : 1;
    }
    gp_Pnt pnt(const Node& n, const char* node, const char* field) {
        return gp_Pnt(num(n, node, field, 0) * scale, num(n, node, field, 1) * scale, num(n, node, field, 2) * scale);
    }
    bool dir(const Node& n, const char* node, const char* field, gp_Dir& out) {
        const gp_Vec v(num(n, node, field, 0), num(n, node, field, 1), num(n, node, field, 2));
        if (std::isnan(v.X()) || v.Magnitude() < 1e-12)
            return false;
        out = gp_Dir(v);
        return true;
    }
    const Node* node(int64_t index) { return index ? m.find(index) : nullptr; }
    const std::string& nameOf(const Node& n) { return m.typeName(n.type); }

    void diag(const std::string& code, const std::string& message, int64_t nodeIndex = -1) {
        if (res.diagnostics.size() < 200)
            res.diagnostics.push_back({code, message, -1, nodeIndex});
    }

    // ---- geometry -----------------------------------------------------------------
    const SurfaceInfo& surfaceFor(int64_t index);
    SurfaceInfo blendSurfaceForFace(const Node& blend, const Node& face);
    int m_blendWhy = 0;
    bool blendSupportContact(const Node& blend, const gp_Pnt& centre, double offset, gp_Pnt& contact);
    const CurveInfo& curveFor(int64_t index);
    Handle(Geom_BSplineCurve) nurbsCurve(const Node& nurbs);
    Handle(Geom2d_BSplineCurve) nurbsCurve2d(const Node& nurbs);
    Handle(Geom2d_BSplineCurve) spCurve2d(const Node& sp);
    bool isSplineOffset(int64_t index);
    PCurve authoredPCurve(const Node& sp, const Node* trim, const SurfaceInfo& si,
                         const TopoDS_Edge& edge);
    Handle(Geom_BSplineCurve) liftSpCurve(const Node& sp, const Node* trim);
    Handle(Geom_BSplineCurve) tolerantCurve(const Node& edge);
    Handle(Geom_Curve) transverseBlendCurve(const Node& edge, const gp_Pnt& start, const gp_Pnt& end);
    Handle(Geom_BSplineSurface) nurbsSurface(const Node& nurbs);
    Handle(Geom_BSplineCurve) chartCurve(const Node& intersection);
    Handle(Geom_BSplineCurve) rebuildBlendContact(const Node& intersection, const gp_Pnt& start, const gp_Pnt& end);
    // Contact curve of a rolling-ball blend on one support (intersection of the support with the blend boundary), built from the
    // exact spine: the foot of each spine point on the support. Null when the pair is not of that kind or anything disagrees.
    Handle(Geom_BSplineCurve) contactCurveFromSpine(const Node& intersection);
    // A blend spine is an intersection of two surfaces (offsets of the supports) but its stored chart stops where the chart
    // stops, while the blend may reach further. Extend the cached curve at either end, by marching along the two surfaces, until
    // it covers the given points. Returns true when the cached curve was changed.
    bool extendSpine(int64_t spineIndex, const std::vector<gp_Pnt>& points);
    // Replace the cached spine of a rolling-ball blend between two buildable supports by one whose points are exactly the
    // ball-centre distance from both supports (the stored chart can run on past the surface it was cut from).
    void correctSpineAgainstSupports(const Node& blend);
    std::unordered_set<int64_t> m_spineCorrected;
    Handle(Geom_BSplineCurve) rebuildSplineBlendRail(const Node& intersection, const Handle(Geom_Curve)& seed,
        double first, double last, const gp_Pnt& start, const gp_Pnt& end);
    Handle(Geom_BSplineCurve) rebuildPlaneSplineIntersection(const Node& intersection,
        const Handle(Geom_Curve)& seedCurve, double first, double last, const gp_Pnt& start, const gp_Pnt& end,
        bool spine = false);
    bool knotVectors(const Node& multNode, const Node& knotNode, int degree, int nVertices, bool periodic,
                     NCollection_Array1<double>& knots, NCollection_Array1<int>& mults);

    // ---- topology -----------------------------------------------------------------
    TopoDS_Vertex vertexFor(int64_t index);
    // Null edge when its geometry is not supported; `ok` is false then.
    TopoDS_Edge edgeFor(const Node& edge);
    TopoDS_Edge edgeForImpl(const Node& edge);
    TopoDS_Face faceFor(const Node& face, TopAbs_Orientation use);
    TopoDS_Face cliffRingFace(const Node& face, const Node& blend);
    TopoDS_Face faceForImpl(const Node& face, TopAbs_Orientation use);
    TopoDS_Face fillFaceFromBoundary(const Node& face);
    void buildBody(const Node& body);
    void buildBodyViaGraph(const Node& body); // spike: topology assembled with OCCT 8 BRepGraph (XT_GRAPH=1)
    void placeInstances();
    // Text of the named attribute on `owner` (UTF-8), empty when there is none. Builds the owner index on first use.
    std::string attributeText(int64_t owner, const char* identifier);
    // The numbers of the first field of the named attribute (empty when there is none or it holds no numbers).
    std::vector<double> attributeReals(int64_t owner, const char* identifier);
    void assignColours(BuiltBody& out, const Node& body);
    // Name for a BODY or ASSEMBLY: SDL/TYSA_UNAME, else SDL/TYSA_NAME, else "<fallback> <node index>".
    std::string displayName(const Node& n, const char* fallback);
    std::string explicitName(const Node& n); // the name attribute only; empty when the node has none
    std::unordered_map<int64_t, std::vector<const Node*>> m_attributesByOwner;
    bool m_attributesIndexed = false;
    std::vector<const Node*> chain(const Node* head, const char* nodeName, const char* nextField);
    bool transformOf(const Node* transform, gp_Trsf& out);
    bool cancelled() { return opt.cancelled && opt.cancelled(); }
    void trace(const char* stage, int64_t index) const {
        if (m_trace) {
            std::fprintf(stderr, "XT build %s node=%lld\n", stage, static_cast<long long>(index));
            std::fflush(stderr);
        }
    }

    const Model& m;
    const BuildOptions& opt;
    BuildResult& res;
    const double scale;
    const size_t m_totalFaces = m.nodesNamed("FACE").size();
    const double pcurvePrecision = pcurvePrecisionFromEnv(); // mm-scale precision for projected pcurves
    const bool m_trace = std::getenv("XT_TRACE_IMPORT") != nullptr;
    std::vector<TopoDS_Face> m_displayCandidates;
    std::map<int64_t, FaceOutcome>* m_outcomes = nullptr; // the body being built; null outside buildBody
    std::string m_fillKind;                                // which fill produced the last filled face

    std::unordered_map<std::string, Slot> m_slots;
    std::unordered_map<int64_t, SurfaceInfo> m_surfaces;
    std::unordered_map<int64_t, CurveInfo> m_curves;
    std::unordered_map<int64_t, TopoDS_Vertex> m_vertices;
    std::unordered_set<int64_t> m_microEdges; // edges shorter than the modeller resolution, merged into one vertex
    std::unordered_map<const void*, std::string> m_edgeKind; // debugging: curve kind per edge
    std::unordered_map<int64_t, TopoDS_Edge> m_edges; // null edge cached for failures
    BRep_Builder bb;
    Handle(ShapeBuild_ReShape) m_reshape = new ShapeBuild_ReShape; // shared by the repairs of one body
};

// ---- NURBS --------------------------------------------------------------------------

// XT flags a curve or surface periodic when it closes smoothly. Some producers nevertheless give a
// clamped knot vector (end multiplicity degree + 1) with one repeated end vertex instead of the
// `degree` repeated vertices a periodic form needs. Only a form whose first `degree` vertices
// really are repeated at the end is treated as periodic; anything else is a clamped, closed spline.
bool vertexRepeated(const double* a, const double* b, int dim) {
    for (int k = 0; k < dim; ++k)
        if (std::abs(a[k] - b[k]) > 1e-9 * (1.0 + std::abs(a[k])))
            return false;
    return true;
}

bool curveRepeatsEnds(const double* v, int n, int dim, int degree) {
    for (int i = 0; i < degree; ++i)
        if (!vertexRepeated(v + static_cast<size_t>(i) * dim, v + static_cast<size_t>(n - degree + i) * dim, dim))
            return false;
    return true;
}

bool Builder::knotVectors(const Node& multNode, const Node& knotNode, int degree, int nVertices, bool periodic,
                          NCollection_Array1<double>& knots, NCollection_Array1<int>& mults) {
    if (multNode.valueCount != knotNode.valueCount || knotNode.valueCount == 0)
        return false;
    const double* kv = m.file().values.data() + knotNode.valueOffset;
    const double* mv = m.file().values.data() + multNode.valueOffset;
    size_t distinct = knotNode.valueCount;
    // Some XT writers pad the two arrays with paired unset knots and zero
    // multiplicities. These are unused slots, not knots in the curve.
    while (distinct && mv[distinct - 1] == 0 && std::isnan(kv[distinct - 1]))
        --distinct;
    if (distinct < 2)
        return false;
    std::vector<double> distinctKnots(kv, kv + distinct);
    std::vector<int> distinctMults(distinct);
    size_t total = 0;
    for (size_t i = 0; i < distinct; ++i) {
        if (!std::isfinite(kv[i]) || (i && kv[i] <= kv[i - 1]) ||
            !(mv[i] >= 1 && mv[i] <= degree + 1) || mv[i] != std::trunc(mv[i]))
            return false;
        distinctMults[i] = static_cast<int>(mv[i]);
        total += static_cast<size_t>(distinctMults[i]);
    }
    if (total != static_cast<size_t>(nVertices + degree + 1))
        return false;
    if (!periodic) {
        knots = NCollection_Array1<double>(1, static_cast<int>(distinct));
        mults = NCollection_Array1<int>(1, static_cast<int>(distinct));
        for (size_t i = 0; i < distinct; ++i) {
            knots.SetValue(static_cast<int>(i) + 1, distinctKnots[i]);
            mults.SetValue(static_cast<int>(i) + 1, distinctMults[i]);
        }
        return true;
    }
    // Periodic: expand, keep the valid range [t_degree, t_{N-1-degree}], run-length encode.
    std::vector<double> expanded;
    expanded.reserve(total);
    for (size_t i = 0; i < distinct; ++i)
        for (int j = 0; j < distinctMults[i]; ++j)
            expanded.push_back(distinctKnots[i]);
    const size_t N = expanded.size();
    if (N < static_cast<size_t>(2 * degree + 2))
        return false;
    std::vector<double> k2;
    std::vector<int> m2;
    for (size_t i = static_cast<size_t>(degree); i <= N - 1 - static_cast<size_t>(degree); ++i) {
        if (!k2.empty() && std::abs(expanded[i] - k2.back()) < 1e-12 * std::max(1.0, std::abs(expanded[i])))
            ++m2.back();
        else {
            k2.push_back(expanded[i]);
            m2.push_back(1);
        }
    }
    if (k2.size() < 2 || m2.front() != m2.back())
        return false;
    knots = NCollection_Array1<double>(1, static_cast<int>(k2.size()));
    mults = NCollection_Array1<int>(1, static_cast<int>(k2.size()));
    for (size_t i = 0; i < k2.size(); ++i) {
        knots.SetValue(static_cast<int>(i) + 1, k2[i]);
        mults.SetValue(static_cast<int>(i) + 1, m2[i]);
    }
    return true;
}

Handle(Geom_BSplineCurve) Builder::nurbsCurve(const Node& nurbs) {
    const int degree = static_cast<int>(num(nurbs, "NURBS_CURVE", "degree"));
    const int n = static_cast<int>(num(nurbs, "NURBS_CURVE", "n_vertices"));
    const int dim = static_cast<int>(num(nurbs, "NURBS_CURVE", "vertex_dim"));
    bool periodic = num(nurbs, "NURBS_CURVE", "periodic") == 'T';
    const bool rational = num(nurbs, "NURBS_CURVE", "rational") == 'T';
    const Node* verts = node(ptr(nurbs, "NURBS_CURVE", "bspline_vertices"));
    const Node* mult = node(ptr(nurbs, "NURBS_CURVE", "knot_mult"));
    const Node* knotNode = node(ptr(nurbs, "NURBS_CURVE", "knots"));
    if (!verts || !mult || !knotNode || degree < 1 || n < 2 || (dim != 3 && dim != 4) || (dim == 4) != rational)
        return {};
    const size_t requiredValues = static_cast<size_t>(n) * static_cast<size_t>(dim);
    const double* vertexValues = m.file().values.data() + verts->valueOffset;
    // The vertex array can reserve whole unused poles after the declared payload.
    // Accept zero-filled capacity and keep the declared poles and knot count authoritative.
    if (verts->valueCount < requiredValues || verts->valueCount % dim != 0
        || !std::all_of(vertexValues + requiredValues, vertexValues + verts->valueCount,
                        [](double value) { return value == 0.0; }))
        return {};
    if (periodic)
        periodic = curveRepeatsEnds(m.file().values.data() + verts->valueOffset, n, dim, degree);
    NCollection_Array1<double> knots;
    NCollection_Array1<int> mults;
    if (!knotVectors(*mult, *knotNode, degree, n, periodic, knots, mults))
        return {};
    const int poleCount = periodic ? n - degree : n;
    if (poleCount < 2)
        return {};
    const double* v = m.file().values.data() + verts->valueOffset;
    NCollection_Array1<gp_Pnt> poles(1, poleCount);
    NCollection_Array1<double> weights(1, poleCount);
    for (int i = 0; i < poleCount; ++i) {
        const double w = rational ? v[i * 4 + 3] : 1.0;
        if (!(w > 0))
            return {};
        poles.SetValue(i + 1, gp_Pnt(v[i * dim] / w * scale, v[i * dim + 1] / w * scale, v[i * dim + 2] / w * scale));
        weights.SetValue(i + 1, w);
    }
    try {
        return rational ? new Geom_BSplineCurve(poles, weights, knots, mults, degree, periodic)
                        : new Geom_BSplineCurve(poles, knots, mults, degree, periodic);
    } catch (const Standard_Failure&) {
        return {};
    }
}

Handle(Geom_BSplineSurface) Builder::nurbsSurface(const Node& nurbs) {
    const int ud = static_cast<int>(num(nurbs, "NURBS_SURF", "u_degree"));
    const int vd = static_cast<int>(num(nurbs, "NURBS_SURF", "v_degree"));
    const int nu = static_cast<int>(num(nurbs, "NURBS_SURF", "n_u_vertices"));
    const int nv = static_cast<int>(num(nurbs, "NURBS_SURF", "n_v_vertices"));
    const int dim = static_cast<int>(num(nurbs, "NURBS_SURF", "vertex_dim"));
    bool uPer = num(nurbs, "NURBS_SURF", "u_periodic") == 'T';
    bool vPer = num(nurbs, "NURBS_SURF", "v_periodic") == 'T';
    const bool rational = num(nurbs, "NURBS_SURF", "rational") == 'T';
    const Node* verts = node(ptr(nurbs, "NURBS_SURF", "bspline_vertices"));
    const Node* um = node(ptr(nurbs, "NURBS_SURF", "u_knot_mult"));
    const Node* vm = node(ptr(nurbs, "NURBS_SURF", "v_knot_mult"));
    const Node* uk = node(ptr(nurbs, "NURBS_SURF", "u_knots"));
    const Node* vk = node(ptr(nurbs, "NURBS_SURF", "v_knots"));
    if (!verts || !um || !vm || !uk || !vk || ud < 1 || vd < 1 || nu < 2 || nv < 2 || (dim != 3 && dim != 4) ||
        (dim == 4) != rational)
        return {};
    if (verts->valueCount != static_cast<size_t>(nu) * static_cast<size_t>(nv) * static_cast<size_t>(dim))
        return {};
    const double* v = m.file().values.data() + verts->valueOffset;
    // The control net is stored with the v index running fastest: vertex (i, j) is at i * nv + j.
    auto vertexAt = [&](int i, int j) { return v + (static_cast<size_t>(i) * static_cast<size_t>(nv) + static_cast<size_t>(j)) * static_cast<size_t>(dim); };
    if (uPer) {
        for (int j = 0; j < nv && uPer; ++j)
            for (int i = 0; i < ud && uPer; ++i)
                uPer = vertexRepeated(vertexAt(i, j), vertexAt(nu - ud + i, j), dim);
    }
    if (vPer) {
        for (int i = 0; i < nu && vPer; ++i)
            for (int j = 0; j < vd && vPer; ++j)
                vPer = vertexRepeated(vertexAt(i, j), vertexAt(i, nv - vd + j), dim);
    }
    NCollection_Array1<double> uKnots, vKnots;
    NCollection_Array1<int> uMults, vMults;
    if (!knotVectors(*um, *uk, ud, nu, uPer, uKnots, uMults) || !knotVectors(*vm, *vk, vd, nv, vPer, vKnots, vMults))
        return {};
    const int pu = uPer ? nu - ud : nu;
    const int pv = vPer ? nv - vd : nv;
    if (pu < 2 || pv < 2)
        return {};
    NCollection_Array2<gp_Pnt> poles(1, pu, 1, pv);
    NCollection_Array2<double> weights(1, pu, 1, pv);
    for (int j = 0; j < pv; ++j)
        for (int i = 0; i < pu; ++i) {
            const double* p = vertexAt(i, j);
            const double w = rational ? p[3] : 1.0;
            if (!(w > 0))
                return {};
            poles.SetValue(i + 1, j + 1, gp_Pnt(p[0] / w * scale, p[1] / w * scale, p[2] / w * scale));
            weights.SetValue(i + 1, j + 1, w);
        }
    try {
        return rational ? new Geom_BSplineSurface(poles, weights, uKnots, vKnots, uMults, vMults, ud, vd, uPer, vPer)
                        : new Geom_BSplineSurface(poles, uKnots, vKnots, uMults, vMults, ud, vd, uPer, vPer);
    } catch (const Standard_Failure&) {
        return {};
    }
}

// XT defines the point of an intersection curve between two chart points exactly: it is the local intersection of the two
// surfaces with the plane through the chord midpoint orthogonal to the chord. This finds that point by Newton iteration on
// (u0, v0, u1, v1): S0(u0, v0) - S1(u1, v1) = 0 (three equations) and (S0(u0, v0) - M) . t = 0. `uv0` and `uv1` hold the start
// parameters on each surface. Returns false when the iteration does not converge to a point near the chord.
static bool exactChartPoint(const Handle(Geom_Surface)& s0, const Handle(Geom_Surface)& s1, const gp_Pnt& mid, const gp_Vec& chord,
                            gp_Pnt2d& uv0, gp_Pnt2d& uv1, gp_Pnt& result, double tolerance) {
    gp_Vec t = chord;
    const double length = t.Magnitude();
    if (length < 1e-12)
        return false;
    t /= length;
    double x[4] = {uv0.X(), uv0.Y(), uv1.X(), uv1.Y()};
    double u0a, u0b, v0a, v0b;
    s0->Bounds(u0a, u0b, v0a, v0b);
    double u1a, u1b, v1a, v1b;
    s1->Bounds(u1a, u1b, v1a, v1b);
    for (int iteration = 0; iteration < 30; ++iteration) {
        gp_Pnt p0, p1;
        gp_Vec d0u, d0v, d1u, d1v;
        s0->D1(x[0], x[1], p0, d0u, d0v);
        s1->D1(x[2], x[3], p1, d1u, d1v);
        const gp_Vec gap(p1, p0);
        double f[4] = {gap.X(), gap.Y(), gap.Z(), gp_Vec(mid, p0).Dot(t)};
        if (std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]) < tolerance && std::abs(f[3]) < tolerance) {
            result = gp_Pnt((p0.X() + p1.X()) * 0.5, (p0.Y() + p1.Y()) * 0.5, (p0.Z() + p1.Z()) * 0.5);
            uv0 = gp_Pnt2d(x[0], x[1]);
            uv1 = gp_Pnt2d(x[2], x[3]);
            return result.Distance(mid) < length;
        }
        double a[4][5] = {{d0u.X(), d0v.X(), -d1u.X(), -d1v.X(), -f[0]},
                          {d0u.Y(), d0v.Y(), -d1u.Y(), -d1v.Y(), -f[1]},
                          {d0u.Z(), d0v.Z(), -d1u.Z(), -d1v.Z(), -f[2]},
                          {d0u.Dot(t), d0v.Dot(t), 0, 0, -f[3]}};
        // Gaussian elimination with partial pivoting.
        for (int c = 0; c < 4; ++c) {
            int pivot = c;
            for (int r = c + 1; r < 4; ++r)
                if (std::abs(a[r][c]) > std::abs(a[pivot][c]))
                    pivot = r;
            if (std::abs(a[pivot][c]) < 1e-14)
                return false;
            if (pivot != c)
                for (int k = 0; k < 5; ++k)
                    std::swap(a[c][k], a[pivot][k]);
            for (int r = c + 1; r < 4; ++r) {
                const double factor = a[r][c] / a[c][c];
                for (int k = c; k < 5; ++k)
                    a[r][k] -= factor * a[c][k];
            }
        }
        double dx[4];
        for (int r = 3; r >= 0; --r) {
            double sum = a[r][4];
            for (int k = r + 1; k < 4; ++k)
                sum -= a[r][k] * dx[k];
            dx[r] = sum / a[r][r];
        }
        for (int k = 0; k < 4; ++k) {
            if (!std::isfinite(dx[k]))
                return false;
            x[k] += dx[k];
        }
        // Stay inside the surface domain; a step that leaves it is not a solution near the chart.
        const double slack = 1e-6;
        if (!Precision::IsInfinite(u0a) && (x[0] < u0a - slack || x[0] > u0b + slack)) return false;
        if (!Precision::IsInfinite(v0a) && (x[1] < v0a - slack || x[1] > v0b + slack)) return false;
        if (!Precision::IsInfinite(u1a) && (x[2] < u1a - slack || x[2] > u1b + slack)) return false;
        if (!Precision::IsInfinite(v1a) && (x[3] < v1a - slack || x[3] > v1b + slack)) return false;
    }
    return false;
}

// Makes the interpolated curve through `pts` agree with both surfaces. Each round interpolates the points, takes the predicted
// midpoint of every span that is not yet accepted, and solves for the exact intersection point on the plane through it normal to
// the curve. A span whose prediction is within `target` of the exact point is accepted; otherwise the exact point is inserted and
// the two halves are checked in the next round. Returns the number of points inserted. Surfaces that cannot be evaluated by
// parameter, and spans where the solver fails, are left as they are.
static size_t refineChartAdaptive(std::vector<gp_Pnt>& pts, const Handle(Geom_Surface)& s0, const Handle(Geom_Surface)& s1,
                                  double target, double newtonTolerance, double surfaceTolerance, size_t maxPoints) {
    if (s0.IsNull() || s1.IsNull() || pts.size() < 2)
        return 0;
    const Handle(ShapeAnalysis_Surface) a0 = new ShapeAnalysis_Surface(s0), a1 = new ShapeAnalysis_Surface(s1);
    std::vector<char> accepted(pts.size() - 1, 0);
    size_t inserted = 0;
    for (int round = 0; round < 8 && pts.size() < maxPoints; ++round) {
        // Chord-length parameters make span i the parameter interval [t[i], t[i + 1]] of the interpolated curve.
        std::vector<double> t(pts.size(), 0.0);
        for (size_t i = 1; i < pts.size(); ++i)
            t[i] = t[i - 1] + pts[i].Distance(pts[i - 1]);
        Handle(Geom_BSplineCurve) curve;
        try {
            Handle(NCollection_HArray1<gp_Pnt>) arr = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(pts.size()));
            Handle(NCollection_HArray1<double>) params = new NCollection_HArray1<double>(1, static_cast<int>(pts.size()));
            for (size_t i = 0; i < pts.size(); ++i) {
                arr->SetValue(static_cast<int>(i) + 1, pts[i]);
                params->SetValue(static_cast<int>(i) + 1, t[i]);
            }
            GeomAPI_Interpolate interp(arr, params, false, Precision::Confusion());
            interp.Perform();
            if (!interp.IsDone())
                break;
            curve = interp.Curve();
        } catch (const Standard_Failure&) {
            break;
        }
        // Parameters of every point on both surfaces, tracked point to point.
        std::vector<gp_Pnt2d> uv0(pts.size()), uv1(pts.size());
        try {
            uv0[0] = a0->ValueOfUV(pts[0], surfaceTolerance);
            uv1[0] = a1->ValueOfUV(pts[0], surfaceTolerance);
            for (size_t i = 1; i < pts.size(); ++i) {
                uv0[i] = a0->NextValueOfUV(uv0[i - 1], pts[i], surfaceTolerance, 1e3 * surfaceTolerance);
                uv1[i] = a1->NextValueOfUV(uv1[i - 1], pts[i], surfaceTolerance, 1e3 * surfaceTolerance);
            }
        } catch (const Standard_Failure&) {
            break;
        }
        std::vector<gp_Pnt> next;
        std::vector<char> nextAccepted;
        next.reserve(pts.size() * 2);
        nextAccepted.reserve(pts.size() * 2);
        size_t added = 0;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            next.push_back(pts[i]);
            if (accepted[i] || !(t[i + 1] > t[i])) {
                nextAccepted.push_back(1);
                continue;
            }
            try {
                const double tm = 0.5 * (t[i] + t[i + 1]);
                gp_Pnt predicted;
                gp_Vec tangent;
                curve->D1(tm, predicted, tangent);
                gp_Pnt2d m0 = a0->NextValueOfUV(uv0[i], predicted, surfaceTolerance, 1e3 * surfaceTolerance);
                gp_Pnt2d m1 = a1->NextValueOfUV(uv1[i], predicted, surfaceTolerance, 1e3 * surfaceTolerance);
                gp_Pnt exact;
                if (tangent.Magnitude() > 1e-12 && exactChartPoint(s0, s1, predicted, tangent, m0, m1, exact, newtonTolerance)) {
                    const double miss = exact.Distance(predicted);
                    const double limit = 0.5 * pts[i].Distance(pts[i + 1]);
                    if (miss > target && miss < limit && exact.Distance(pts[i]) > newtonTolerance &&
                        exact.Distance(pts[i + 1]) > newtonTolerance) {
                        next.push_back(exact);
                        nextAccepted.push_back(0);
                        nextAccepted.push_back(0);
                        ++added;
                        continue;
                    }
                }
            } catch (const Standard_Failure&) {
            }
            nextAccepted.push_back(1); // within target, or the solver could not improve it
        }
        next.push_back(pts.back());
        if (!added)
            break;
        // `nextAccepted` was filled per original span: a split span contributes its two halves, so rebuild the flags from it.
        std::vector<char> flags;
        flags.reserve(next.size());
        for (size_t k = 0; k < nextAccepted.size();)
            flags.push_back(nextAccepted[k++]);
        pts.swap(next);
        accepted.swap(flags);
        accepted.resize(pts.size() - 1, 0);
        inserted += added;
    }
    return inserted;
}

Handle(Geom_BSplineCurve) Builder::chartCurve(const Node& inter) {
    ++res.stats.approximatedGeometry; // a chart is a chordal approximation of the intersection curve
    const Node* chart = node(ptr(inter, "INTERSECTION", "chart"));
    if (!chart)
        return {};
    const Slot& hv = sl("CHART", "hvec");
    if (!hv.ok())
        return {};
    const size_t count = chart->valueCount > hv.offset ? (chart->valueCount - hv.offset) / 3 : 0;
    const auto chartPoint = [&](size_t i) {
        return gp_Pnt(m.value(*chart, hv, i * 3) * scale,
                      m.value(*chart, hv, i * 3 + 1) * scale,
                      m.value(*chart, hv, i * 3 + 2) * scale);
    };
    std::vector<gp_Pnt> pts;
    pts.reserve(count);
    const double minGap = 1e-7 * scale;
    for (size_t i = 0; i < count; ++i) {
        const gp_Pnt p = chartPoint(i);
        if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()))
            return {};
        if (pts.empty() || pts.back().Distance(p) > minGap)
            pts.push_back(p);
    }
    if (pts.size() < 2)
        return {};

    // A terminator stores the singular endpoint followed by the branch
    // point at the corresponding end of the chart.
    auto terminatorAt = [&](const char* field, const gp_Pnt& chartEnd, gp_Pnt& endpoint) {
        const Node* limit = node(ptr(inter, "INTERSECTION", field));
        if (!limit || num(*limit, "LIMIT", "type") != 'T')
            return false;
        const Slot& samples = sl("LIMIT", "hvec");
        if (!samples.ok() || samples.offset > limit->valueCount || limit->valueCount - samples.offset != 6)
            return false;
        auto sample = [&](size_t i) {
            return gp_Pnt(m.value(*limit, samples, i) * scale,
                          m.value(*limit, samples, i + 1) * scale,
                          m.value(*limit, samples, i + 2) * scale);
        };
        endpoint = sample(0);
        const gp_Pnt branch = sample(3);
        return std::isfinite(endpoint.X()) && std::isfinite(endpoint.Y()) &&
               std::isfinite(endpoint.Z()) && branch.Distance(chartEnd) <= minGap;
    };
    gp_Pnt start, end;
    const bool hasStart = terminatorAt("start", chartPoint(0), start);
    const bool hasEnd = terminatorAt("end", chartPoint(count - 1), end);
    if (hasStart) {
        if (start.Distance(pts.front()) <= minGap)
            pts.front() = start;
        else
            pts.insert(pts.begin(), start);
    }
    if (hasEnd) {
        if (end.Distance(pts.back()) <= minGap)
            pts.back() = end;
        else
            pts.push_back(end);
    }
    const bool closed = !hasStart && !hasEnd && pts.size() > 3 && pts.front().Distance(pts.back()) <= minGap * 10;
    if (closed)
        pts.pop_back();
    if (!std::getenv("XT_NO_REFINE_CHART")) {
        // The stored chart is a chordal approximation with its own error estimate; points between chart points are defined
        // exactly by the two surfaces. Insert them until the estimated chord error is small.
        const double chordalMm = num(*chart, "CHART", "chordal_error") * scale;
        const double targetLength = (std::getenv("XT_REFINE_TARGET") ? std::atof(std::getenv("XT_REFINE_TARGET")) : 1e-8) * scale; // default 0.00001 mm
        if (std::isfinite(chordalMm) && chordalMm > targetLength) {
            const SurfaceInfo first = surfaceFor(ptr(inter, "INTERSECTION", "surface", 0)); // copies: the map may rehash
            const SurfaceInfo second = surfaceFor(ptr(inter, "INTERSECTION", "surface", 1));
            auto supported = [](const SurfaceInfo& s) {
                return s.ok && s.kind != "BLENDED_EDGE" && s.kind != "BLEND_BOUND";
            };
            if (supported(first) && supported(second)) {
                if (closed)
                    pts.push_back(pts.front());
                const size_t inserted = refineChartAdaptive(pts, first.surface, second.surface, targetLength, 1e-9 * scale, 1e-6 * scale, 4000);
                if (closed)
                    pts.pop_back();
                if (std::getenv("XT_DEBUG_CS"))
                    std::fprintf(stderr, "refine %lld: chordal %g mm, inserted %zu, now %zu points\n", (long long)inter.index, chordalMm, inserted, pts.size());
                if (inserted) {
                    ++res.stats.facesSkippedBySurface["  chart-refined"];
                    res.stats.facesSkippedBySurface["  chart-points-inserted"] += inserted;
                } else {
                    ++res.stats.facesSkippedBySurface["  chart-refine-failed"];
                }
            } else {
                if (std::getenv("XT_DEBUG_CS"))
                    std::fprintf(stderr, "refine unsupported %lld: %s ok=%d / %s ok=%d\n", (long long)inter.index, first.kind.c_str(), (int)first.ok, second.kind.c_str(), (int)second.ok);
                ++res.stats.facesSkippedBySurface["  chart-refine-unsupported"];
            }
        }
    }
    if (!closed && pts.size() > 48) {
        // Charts are dense; a fitted spline with far fewer poles is much cheaper downstream.
        try {
            NCollection_Array1<gp_Pnt> arr(1, static_cast<int>(pts.size()));
            for (size_t i = 0; i < pts.size(); ++i)
                arr.SetValue(static_cast<int>(i) + 1, pts[i]);
            GeomAPI_PointsToBSpline fit(arr, 3, 5, GeomAbs_C2, 2e-7 * scale);
            if (fit.IsDone()) {
                const Handle(Geom_BSplineCurve)& curve = fit.Curve();
                if ((!hasStart || curve->StartPoint().Distance(start) <= minGap) &&
                    (!hasEnd || curve->EndPoint().Distance(end) <= minGap))
                    return curve;
            }
        } catch (const Standard_Failure&) {
        }
    }
    try {
        Handle(NCollection_HArray1<gp_Pnt>) arr = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(pts.size()));
        for (size_t i = 0; i < pts.size(); ++i)
            arr->SetValue(static_cast<int>(i) + 1, pts[i]);
        GeomAPI_Interpolate interp(arr, closed, minGap);
        interp.Perform();
        if (interp.IsDone())
            return interp.Curve();
    } catch (const Standard_Failure&) {
    }
    // Fallback: the chart polyline itself as a degree-1 B-spline.
    try {
        if (closed)
            pts.push_back(pts.front());
        NCollection_Array1<gp_Pnt> poles(1, static_cast<int>(pts.size()));
        NCollection_Array1<double> knots(1, static_cast<int>(pts.size()));
        NCollection_Array1<int> mults(1, static_cast<int>(pts.size()));
        double s = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            if (i)
                s += pts[i].Distance(pts[i - 1]);
            poles.SetValue(static_cast<int>(i) + 1, pts[i]);
            knots.SetValue(static_cast<int>(i) + 1, s);
            mults.SetValue(static_cast<int>(i) + 1, (i == 0 || i + 1 == pts.size()) ? 2 : 1);
        }
        return new Geom_BSplineCurve(poles, knots, mults, 1, false);
    } catch (const Standard_Failure&) {
        return {};
    }
}

Handle(Geom2d_BSplineCurve) Builder::nurbsCurve2d(const Node& nurbs) {
    const int degree = static_cast<int>(num(nurbs, "NURBS_CURVE", "degree"));
    const int n = static_cast<int>(num(nurbs, "NURBS_CURVE", "n_vertices"));
    const int dim = static_cast<int>(num(nurbs, "NURBS_CURVE", "vertex_dim"));
    bool periodic = num(nurbs, "NURBS_CURVE", "periodic") == 'T';
    const bool rational = num(nurbs, "NURBS_CURVE", "rational") == 'T';
    const Node* verts = node(ptr(nurbs, "NURBS_CURVE", "bspline_vertices"));
    const Node* mult = node(ptr(nurbs, "NURBS_CURVE", "knot_mult"));
    const Node* knotNode = node(ptr(nurbs, "NURBS_CURVE", "knots"));
    if (!verts || !mult || !knotNode || degree < 1 || n < 2 || (dim != 2 && dim != 3) || (dim == 3) != rational)
        return {};
    if (verts->valueCount != static_cast<size_t>(n) * static_cast<size_t>(dim))
        return {};
    if (periodic)
        periodic = curveRepeatsEnds(m.file().values.data() + verts->valueOffset, n, dim, degree);
    NCollection_Array1<double> knots;
    NCollection_Array1<int> mults;
    if (!knotVectors(*mult, *knotNode, degree, n, periodic, knots, mults))
        return {};
    const int poleCount = periodic ? n - degree : n;
    if (poleCount < 2)
        return {};
    const double* v = m.file().values.data() + verts->valueOffset;
    NCollection_Array1<gp_Pnt2d> poles(1, poleCount);
    NCollection_Array1<double> weights(1, poleCount);
    for (int i = 0; i < poleCount; ++i) {
        const double w = rational ? v[i * 3 + 2] : 1.0;
        if (!(w > 0))
            return {};
        poles.SetValue(i + 1, gp_Pnt2d(v[i * dim] / w, v[i * dim + 1] / w));
        weights.SetValue(i + 1, w);
    }
    try {
        return rational ? new Geom2d_BSplineCurve(poles, weights, knots, mults, degree, periodic)
                        : new Geom2d_BSplineCurve(poles, knots, mults, degree, periodic);
    } catch (const Standard_Failure&) {
        return {};
    }
}

Handle(Geom2d_BSplineCurve) Builder::spCurve2d(const Node& sp) {
    const Node* b = node(ptr(sp, "SP_CURVE", "b_curve"));
    const Node* nurbs = b ? node(ptr(*b, "B_CURVE", "nurbs")) : nullptr;
    if (!nurbs)
        return {};
    return nurbsCurve2d(*nurbs);
}

bool Builder::isSplineOffset(int64_t index) {
    const Node* offset = node(index);
    if (!offset || nameOf(*offset) != "OFFSET_SURF")
        return false;
    const Node* basis = node(ptr(*offset, "OFFSET_SURF", "surface"));
    return basis && nameOf(*basis) == "B_SURFACE";
}

PCurve Builder::authoredPCurve(const Node& sp, const Node* trim, const SurfaceInfo& si,
                              const TopoDS_Edge& edge) {
    try {
        Handle(Geom2d_BSplineCurve) curve = spCurve2d(sp);
        if (curve.IsNull() || BRep_Tool::Degenerated(edge))
            return {};
        const double lo = curve->FirstParameter(), hi = curve->LastParameter();
        double first = trim ? num(*trim, "TRIMMED_CURVE", "parm_1") : lo;
        double last = trim ? num(*trim, "TRIMMED_CURVE", "parm_2") : hi;
        if (!std::isfinite(first) || !std::isfinite(last))
            return {};
        if (last < first)
            std::swap(first, last);
        if (!curve->IsPeriodic()) {
            const double eps = Precision::PConfusion();
            if (first < lo - eps || last > hi + eps)
                return {};
            first = std::max(lo, first);
            last = std::min(hi, last);
        }
        if (!(first < last))
            return {};

        const double uScale = si.kind == "PLANE" ? scale : 1.0;
        const double vScale = si.kind == "PLANE" || si.kind == "CYLINDER" ? scale : 1.0;
        for (int i = 1; i <= curve->NbPoles(); ++i) {
            const gp_Pnt2d pole = curve->Pole(i);
            curve->SetPole(i, gp_Pnt2d(pole.X() * uScale, pole.Y() * vScale));
        }

        double q0, q1;
        const Handle(Geom_Curve) c3 = BRep_Tool::Curve(edge, q0, q1);
        if (c3.IsNull() || !std::isfinite(q0) || !std::isfinite(q1) || !(q0 < q1))
            return {};
        const double limit = std::clamp(BRep_Tool::Tolerance(edge), 2e-5 * scale, 5e-4 * scale);
        auto onSupport = [&](double t) {
            const gp_Pnt2d uv = curve->Value(t);
            return si.surface->Value(uv.X(), uv.Y());
        };
        const gp_Pnt a = onSupport(first), b = onSupport(last);
        const gp_Pnt start = c3->Value(q0), end = c3->Value(q1);
        bool forward = a.Distance(start) <= limit && b.Distance(end) <= limit;
        bool reversed = b.Distance(start) <= limit && a.Distance(end) <= limit;
        // Test edge traversal independently of fin orientation and parameter spacing.
        // This also distinguishes the two directions when the edge endpoints coincide.
        const int samples = std::clamp(curve->NbPoles() + 4, 8, 32);
        const double parameterTolerance = (q1 - q0) * 1e-6;
        double previous = 0;
        for (int i = 1; i < samples && (forward || reversed); ++i) {
            const gp_Pnt point = onSupport(first + (last - first) * i / samples);
            GeomAPI_ProjectPointOnCurve projection(point, c3, q0, q1);
            if (projection.NbPoints() == 0 || !(projection.LowerDistance() <= limit))
                return {};
            const double parameter = projection.LowerDistanceParameter();
            if (i > 1) {
                forward = forward && parameter + parameterTolerance >= previous;
                reversed = reversed && parameter - parameterTolerance <= previous;
            }
            previous = parameter;
        }
        if (forward == reversed)
            return {};
        if (reversed) {
            const double newFirst = curve->ReversedParameter(last);
            last = curve->ReversedParameter(first);
            first = newFirst;
            curve->Reverse();
        }
        return {curve, first, last};
    } catch (const Standard_Failure&) {
        return {};
    }
}

// Lift the authored 2D spline through its support. A trim's parameters
// belong to that spline, so apply them before interpolating the 3D samples.
Handle(Geom_BSplineCurve) Builder::liftSpCurve(const Node& sp, const Node* trim) {
    const int64_t surfaceIndex = ptr(sp, "SP_CURVE", "surface");
    const SurfaceInfo& si = surfaceFor(surfaceIndex);
    const bool uvKnown = si.kind == "PLANE" || si.kind == "CYLINDER" || si.kind == "B_SURFACE" ||
                         si.kind == "SPHERE" || si.kind == "TORUS" || si.kind == "SPUN_SURF" || isSplineOffset(surfaceIndex);
    if (!si.ok || !uvKnown)
        return {};
    double profileScale = 1.0;
    if (si.kind == "SPUN_SURF") {
        const Node* support = node(surfaceIndex);
        const Node* profile = support ? node(ptr(*support, "SPUN_SURF", "profile")) : nullptr;
        if (!profile)
            return {};
        if (nameOf(*profile) == "LINE")
            profileScale = scale;
    }

    const Handle(Geom2d_BSplineCurve) curve = spCurve2d(sp);
    if (curve.IsNull())
        return {};
    const double lo = curve->FirstParameter(), hi = curve->LastParameter();
    const double first = trim ? num(*trim, "TRIMMED_CURVE", "parm_1") : lo;
    const double last = trim ? num(*trim, "TRIMMED_CURVE", "parm_2") : hi;
    if (!std::isfinite(first) || !std::isfinite(last) || first == last)
        return {};

    try {
        const int samples = std::clamp(curve->NbPoles() + 8, 12, 48);
        const double minGap = 1e-7 * scale;
        std::vector<gp_Pnt> points;
        for (int i = 0; i <= samples; ++i) {
            double t = first + (last - first) * i / samples;
            if (!curve->IsPeriodic())
                t = std::clamp(t, lo, hi);
            gp_Pnt2d uv = curve->Value(t);
            if (si.kind == "PLANE")
                uv = gp_Pnt2d(uv.X() * scale, uv.Y() * scale);
            else if (si.kind == "CYLINDER")
                uv.SetY(uv.Y() * scale);
            else if (si.kind == "SPUN_SURF")
                uv = gp_Pnt2d(uv.Y(), uv.X() * profileScale);
            const gp_Pnt point = si.surface->Value(uv.X(), uv.Y());
            if (points.empty() || points.back().Distance(point) > minGap)
                points.push_back(point);
        }
        if (points.size() < 2)
            return {};
        Handle(NCollection_HArray1<gp_Pnt>) poles = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(points.size()));
        for (size_t i = 0; i < points.size(); ++i)
            poles->SetValue(static_cast<int>(i) + 1, points[i]);
        GeomAPI_Interpolate interpolation(poles, false, minGap);
        interpolation.Perform();
        if (interpolation.IsDone())
            return interpolation.Curve();
    } catch (const Standard_Failure&) {
    }
    return {};
}

// Tolerant edges carry their geometry on the fins.
Handle(Geom_BSplineCurve) Builder::tolerantCurve(const Node& edge) {
    const Node* first = node(ptr(edge, "EDGE", "fin"));
    const Node* fin = first;
    for (size_t guard = 0; fin && guard < 64; ++guard) {
        const Node* trim = node(ptr(*fin, "FIN", "curve"));
        if (trim && nameOf(*trim) == "TRIMMED_CURVE") {
            const Node* sp = node(ptr(*trim, "TRIMMED_CURVE", "basis_curve"));
            if (sp && nameOf(*sp) == "SP_CURVE") {
                const auto lifted = liftSpCurve(*sp, trim);
                if (!lifted.IsNull())
                    return lifted;
            }
        }
        fin = node(ptr(*fin, "FIN", "other"));
        if (fin == first)
            break;
    }
    return {};
}

// ---- curves and surfaces ---------------------------------------------------------------

Handle(Geom_Curve) Builder::transverseBlendCurve(const Node& edge, const gp_Pnt& start, const gp_Pnt& end) {
    const Node* first = node(ptr(edge, "EDGE", "fin"));
    const Node* second = first ? node(ptr(*first, "FIN", "other")) : nullptr;
    if (!second || second == first || ptr(*second, "FIN", "other") != first->index)
        return {};
    const double tol = std::clamp(num(edge, "EDGE", "tolerance") * scale,
                                  kXtTolerance * scale, 2e-6 * scale);
    if (!std::isfinite(tol) || start.Distance(end) <= tol)
        return {};
    std::array<const Node*, 2> charts{};
    double radius = 0;
    for (const Node* fin : {first, second}) {
        const Node* trim = node(ptr(*fin, "FIN", "curve"));
        const Node* sp = trim && nameOf(*trim) == "TRIMMED_CURVE"
            ? node(ptr(*trim, "TRIMMED_CURVE", "basis_curve")) : nullptr;
        const Node* blend = sp && nameOf(*sp) == "SP_CURVE" ? node(ptr(*sp, "SP_CURVE", "surface")) : nullptr;
        if (!blend || nameOf(*blend) != "BLENDED_EDGE" || num(*blend, "BLENDED_EDGE", "blend_type") != 'R'
            || num(*blend, "BLENDED_EDGE", "thumb_weight", 0) != 1
            || num(*blend, "BLENDED_EDGE", "thumb_weight", 1) != 1)
            return {};
        const auto pc = spCurve2d(*sp);
        const double a = num(*trim, "TRIMMED_CURVE", "parm_1"), b = num(*trim, "TRIMMED_CURVE", "parm_2");
        if (pc.IsNull() || pc->Degree() != 1 || pc->NbPoles() != 2
            || !std::isfinite(a) || !std::isfinite(b)
            || std::min(a,b) < pc->FirstParameter() || std::max(a,b) > pc->LastParameter())
            return {};
        const gp_Pnt2d uv0 = pc->Value(a), uv1 = pc->Value(b);
        if (std::abs(uv0.X() - uv1.X()) * scale > tol
            || std::abs(std::min(uv0.Y(),uv1.Y())) > 1e-3
            || std::abs(std::max(uv0.Y(),uv1.Y()) - 1) > 1e-3)
            return {};
        const double r = std::abs(num(*blend, "BLENDED_EDGE", "range", 0)) * scale;
        const double other = std::abs(num(*blend, "BLENDED_EDGE", "range", 1)) * scale;
        if (!std::isfinite(other) || !std::isfinite(r) || !(r > tol)
            || std::abs(r - other) > tol || (radius > 0 && std::abs(r - radius) > tol))
            return {};
        radius = r;
        const Node* spine = node(ptr(*blend, "BLENDED_EDGE", "spine"));
        if (!spine || nameOf(*spine) != "INTERSECTION")
            return {};
        charts[fin == first ? 0 : 1] = node(ptr(*spine, "INTERSECTION", "chart"));
    }
    const Slot& xyz = sl("CHART", "hvec");
    if (!xyz.ok())
        return {};
    gp_Pnt centre;
    bool found = false;
    for (const auto* chart : charts) {
        if (!chart || chart->valueCount < xyz.offset)
            continue;
        const size_t count = (chart->valueCount - xyz.offset) / 3;
        for (size_t i = 0; i < count; ++i) {
            if (cancelled())
                return {};
            const gp_Pnt p(m.value(*chart, xyz, 3*i) * scale, m.value(*chart, xyz, 3*i+1) * scale,
                           m.value(*chart, xyz, 3*i+2) * scale);
            if (!(std::abs(p.Distance(start) - radius) <= tol && std::abs(p.Distance(end) - radius) <= tol))
                continue;
            if (found && centre.Distance(p) > tol)
                return {}; // more than one candidate section
            centre = p;
            found = true;
        }
    }
    if (!found)
        return {};
    const gp_Vec a(centre, start), b(centre, end);
    const gp_Vec bisector = a.Normalized() + b.Normalized();
    if (bisector.Magnitude() < 0.05)
        return {}; // the short arc is ambiguous near a semicircle
    const gp_Pnt midpoint = centre.Translated(bisector.Normalized() * radius);
    GC_MakeArcOfCircle arc(start, midpoint, end);
    if (!arc.IsDone())
        return {};
    return arc.Value();
}

const CurveInfo& Builder::curveFor(int64_t index) {
    const auto found = m_curves.find(index);
    if (found != m_curves.end())
        return found->second;
    CurveInfo info;
    const Node* n = node(index);
    if (n) {
        info.kind = nameOf(*n);
        const std::string& k = info.kind;
        try {
            if (k == "LINE") {
                gp_Dir d;
                if (dir(*n, "LINE", "direction", d))
                    info.curve = new Geom_Line(pnt(*n, "LINE", "pvec"), d);
                info.senseSign = signOf(*n, "LINE", "sense");
            } else if (k == "CIRCLE" || k == "ELLIPSE") {
                gp_Dir normal, xAxis;
                if (dir(*n, k.c_str(), "normal", normal) && dir(*n, k.c_str(), "x_axis", xAxis)) {
                    const gp_Ax2 ax(pnt(*n, k.c_str(), "centre"), normal, xAxis);
                    if (std::getenv("XT_DEBUG_TOL") && k == "CIRCLE" && num(*n, "CIRCLE", "radius") * scale > 800)
                        std::fprintf(stderr, "big circle node %lld radius=%g\n", (long long)index, num(*n, "CIRCLE", "radius") * scale);
                    if (k == "CIRCLE")
                        info.curve = new Geom_Circle(ax, num(*n, "CIRCLE", "radius") * scale);
                    else
                        info.curve = new Geom_Ellipse(ax, num(*n, "ELLIPSE", "major_radius") * scale,
                                                      num(*n, "ELLIPSE", "minor_radius") * scale);
                }
                info.senseSign = signOf(*n, k.c_str(), "sense");
            } else if (k == "B_CURVE") {
                if (const Node* nurbs = node(ptr(*n, "B_CURVE", "nurbs")))
                    info.curve = nurbsCurve(*nurbs);
                info.senseSign = signOf(*n, "B_CURVE", "sense");
            } else if (k == "INTERSECTION") {
                info.curve = chartCurve(*n);
                if (!std::getenv("XT_NO_BLEND_CONTACT_SPINE")) {
                    if (const auto contact = contactCurveFromSpine(*n); !contact.IsNull()) {
                        info.curve = contact;
                        ++res.stats.facesSkippedBySurface["  contact-from-spine"];
                    }
                }
                info.senseSign = signOf(*n, "INTERSECTION", "sense");
            } else if (k == "SP_CURVE") {
                info.curve = liftSpCurve(*n, nullptr);
                info.senseSign = signOf(*n, "SP_CURVE", "sense");
                if (!info.curve.IsNull())
                    ++res.stats.approximatedGeometry;
            } else if (k == "TRIMMED_CURVE") {
                const int64_t basisIndex = ptr(*n, "TRIMMED_CURVE", "basis_curve");
                const Node* basisNode = node(basisIndex);
                if (basisNode && nameOf(*basisNode) == "SP_CURVE") {
                    info.curve = liftSpCurve(*basisNode, n);
                    // The lifted samples already follow the authored trim order.
                    if (!info.curve.IsNull())
                        ++res.stats.approximatedGeometry;
                } else {
                    const CurveInfo& basis = curveFor(basisIndex);
                    if (basis.ok) {
                        info.curve = basis.curve;
                        info.senseSign = basis.senseSign;
                        info.trimmed = true;
                        info.parm1 = num(*n, "TRIMMED_CURVE", "parm_1");
                        info.parm2 = num(*n, "TRIMMED_CURVE", "parm_2");
                    }
                }
            }
        } catch (const Standard_Failure&) {
            info.curve.Nullify();
        }
        info.ok = !info.curve.IsNull();
    }
    return m_curves.emplace(index, std::move(info)).first->second;
}

const SurfaceInfo& Builder::surfaceFor(int64_t index) {
    const auto found = m_surfaces.find(index);
    if (found != m_surfaces.end())
        return found->second;
    SurfaceInfo info;
    const Node* n = node(index);
    if (n) {
        info.kind = nameOf(*n);
        const std::string& k = info.kind;
        info.senseSign = signOf(*n, k.c_str(), "sense");
        try {
            gp_Dir axis, xAxis;
            if (k == "PLANE") {
                if (dir(*n, "PLANE", "normal", axis) && dir(*n, "PLANE", "x_axis", xAxis))
                    info.surface = new Geom_Plane(gp_Ax3(pnt(*n, "PLANE", "pvec"), axis, xAxis));
            } else if (k == "CYLINDER") {
                if (dir(*n, "CYLINDER", "axis", axis) && dir(*n, "CYLINDER", "x_axis", xAxis))
                    info.surface = new Geom_CylindricalSurface(gp_Ax3(pnt(*n, "CYLINDER", "pvec"), axis, xAxis),
                                                               num(*n, "CYLINDER", "radius") * scale);
            } else if (k == "CONE") {
                if (dir(*n, "CONE", "axis", axis) && dir(*n, "CONE", "x_axis", xAxis)) {
                    const double half = std::atan2(num(*n, "CONE", "sin_half_angle"), num(*n, "CONE", "cos_half_angle"));
                    if (std::getenv("XT_DEBUG"))
                        std::fprintf(stderr, "CONE %lld P=(%g %g %g) A=(%g %g %g) X=(%g %g %g) r=%g sin=%g cos=%g sense=%c\n", (long long)index,
                                     num(*n, "CONE", "pvec", 0), num(*n, "CONE", "pvec", 1), num(*n, "CONE", "pvec", 2),
                                     num(*n, "CONE", "axis", 0), num(*n, "CONE", "axis", 1), num(*n, "CONE", "axis", 2),
                                     num(*n, "CONE", "x_axis", 0), num(*n, "CONE", "x_axis", 1), num(*n, "CONE", "x_axis", 2),
                                     num(*n, "CONE", "radius"), num(*n, "CONE", "sin_half_angle"), num(*n, "CONE", "cos_half_angle"),
                                     (int)num(*n, "CONE", "sense"));
                    // Observed in the local samples (and contradicting the sign in the reference formula):
                    // the radius grows by tan(a) per unit of distance along +A from pvec. The OCCT cone
                    // along +A with semi-angle +a has the same points and an outward natural normal.
                    info.surface = new Geom_ConicalSurface(gp_Ax3(pnt(*n, "CONE", "pvec"), axis, xAxis), half,
                                                           num(*n, "CONE", "radius") * scale);
                    info.flip = 1;
                }
            } else if (k == "SPHERE") {
                if (dir(*n, "SPHERE", "axis", axis) && dir(*n, "SPHERE", "x_axis", xAxis))
                    info.surface = new Geom_SphericalSurface(gp_Ax3(pnt(*n, "SPHERE", "centre"), axis, xAxis),
                                                             num(*n, "SPHERE", "radius") * scale);
            } else if (k == "TORUS") {
                const double major = num(*n, "TORUS", "major_radius") * scale;
                const double minor = num(*n, "TORUS", "minor_radius") * scale;
                if (std::getenv("XT_DEBUG_TOL") && major > 1500)
                    std::fprintf(stderr, "big torus node %lld major=%g minor=%g\n", (long long)index, major, minor);
                if (std::isfinite(major) && std::isfinite(minor) && minor > 0 &&
                    dir(*n, "TORUS", "axis", axis) && dir(*n, "TORUS", "x_axis", xAxis)) {
                    const gp_Pnt centre = pnt(*n, "TORUS", "centre");
                    const gp_Ax3 frame(centre, axis, xAxis);
                    if (major > minor) {
                        info.surface = new Geom_ToroidalSurface(frame, major, minor);
                    } else if (major != 0 && major > -minor) {
                        // Apple and lemon tori use only the branch with nonnegative radial
                        // distance. Bound the meridian at the two intersections with the axis.
                        const double limit = std::acos(-major / minor);
                        const gp_Dir radial = frame.XDirection();
                        const gp_Pnt meridianCentre = centre.Translated(gp_Vec(radial) * major);
                        const gp_Ax2 meridianFrame(meridianCentre, frame.YDirection().Reversed(), radial);
                        Handle(Geom_Circle) circle = new Geom_Circle(meridianFrame, minor);
                        Handle(Geom_TrimmedCurve) meridian = new Geom_TrimmedCurve(circle, -limit, limit, true, false);
                        // Revolution uses (rotation, meridian), the same order as XT TORUS.
                        info.surface = new Geom_SurfaceOfRevolution(meridian, gp_Ax1(centre, frame.Direction()));
                    }
                }
            } else if (k == "B_SURFACE") {
                if (const Node* nurbs = node(ptr(*n, "B_SURFACE", "nurbs")))
                    info.surface = nurbsSurface(*nurbs);
            } else if (k == "SWEPT_SURF") {
                // R(u, v) = C(u) + v D; u follows the section curve.
                const CurveInfo section = curveFor(ptr(*n, "SWEPT_SURF", "section")); // copy: the map may rehash
                if (section.ok && dir(*n, "SWEPT_SURF", "sweep", axis)) {
                    Handle(Geom_Curve) basis = section.curve;
                    const auto spline = Handle(Geom_BSplineCurve)::DownCast(basis);
                    if (!spline.IsNull() && spline->IsClosed() && !spline->IsPeriodic()) {
                        try {
                            const auto periodic = Handle(Geom_BSplineCurve)::DownCast(spline->Copy());
                            periodic->SetPeriodic();
                            const double first = spline->FirstParameter(), last = spline->LastParameter();
                            bool equivalent = std::abs(periodic->Period() - (last - first)) <= Precision::PConfusion();
                            for (int i = 0; i <= 64 && equivalent; ++i) {
                                const double t = first + (last - first) * i / 64;
                                equivalent = spline->Value(t).Distance(periodic->Value(t)) <= kXtTolerance * scale;
                            }
                            if (equivalent)
                                basis = periodic;
                        } catch (const Standard_Failure&) {
                        }
                    }
                    info.surface = new Geom_SurfaceOfLinearExtrusion(basis, axis);
                }
            } else if (k == "SPUN_SURF") {
                // XT uses (profile, angle) and OCCT uses (angle, profile). Swapping
                // the derivatives reverses the natural normal and the trim winding.
                const CurveInfo profile = curveFor(ptr(*n, "SPUN_SURF", "profile"));
                if (profile.ok && dir(*n, "SPUN_SURF", "axis", axis)) {
                    info.surface = new Geom_SurfaceOfRevolution(profile.curve, gp_Ax1(pnt(*n, "SPUN_SURF", "base"), axis));
                    info.flip = -1;
                }
            } else if (k == "OFFSET_SURF") {
                const SurfaceInfo under = surfaceFor(ptr(*n, "OFFSET_SURF", "surface")); // copy: the map may rehash
                if (under.ok) {
                    // The offset runs along the underlying natural normal as modified by its sense.
                    const double value = num(*n, "OFFSET_SURF", "offset") * scale * under.senseSign * under.flip;
                    try {
                        info.surface = new Geom_OffsetSurface(under.surface, value);
                    } catch (const Standard_Failure& failure) {
                        // A basis that is only C0 somewhere is refused by the constructor's continuity check.
                        if (std::getenv("XT_DEBUG_CS"))
                            std::fprintf(stderr, "offset surface %lld refused: %s\n", (long long)index, failure.GetMessageString());
                        if (!std::getenv("XT_NO_OFFSET_C0"))
                            info.surface = new Geom_OffsetSurface(under.surface, value, true);
                    }
                    info.flip = under.flip;
                }
            }
        } catch (const Standard_Failure&) {
            info.surface.Nullify();
        }
        info.ok = !info.surface.IsNull();
        if (info.ok)
            ++res.stats.surfacesBuilt[k];
    }
    return m_surfaces.emplace(index, std::move(info)).first->second;
}

// Evaluate contact with a first-level rolling-ball blend. Its contact lies on the
// circular section normal to the spine, at the foot of the dependent ball centre.
bool Builder::blendSupportContact(const Node& blend, const gp_Pnt& centre, double offset, gp_Pnt& contact) {
    auto bsFail = [](int code) {
        if (std::getenv("XT_DEBUG_BS"))
            std::fprintf(stderr, "bsc %d\n", code);
        return false;
    };
    const bool cliffType = num(blend, "BLENDED_EDGE", "blend_type") == 'E' && !std::getenv("XT_NO_CLIFF_BLEND");
    if ((num(blend, "BLENDED_EDGE", "blend_type") != 'R' && !cliffType) || !std::isfinite(offset)) {
        if (std::getenv("XT_DEBUG_BS"))
            std::fprintf(stderr, "bsc1 type=%c finite=%d\n", static_cast<char>(num(blend, "BLENDED_EDGE", "blend_type")), (int)std::isfinite(offset));
        return bsFail(1);
    }
    const double r0 = std::abs(num(blend, "BLENDED_EDGE", "range", 0)) * scale;
    const double r1 = std::abs(num(blend, "BLENDED_EDGE", "range", 1)) * scale;
    const double tolerance = kBlendContactTolerance * scale;
    if (!(r0 > 0 && r1 > 0) || std::abs(r0 - r1) > kXtTolerance * scale)
        return bsFail(2);

    const CurveInfo spine = curveFor(ptr(blend, "BLENDED_EDGE", "spine"));
    const SurfaceInfo a = surfaceFor(ptr(blend, "BLENDED_EDGE", "surface", 0));
    const SurfaceInfo b = surfaceFor(ptr(blend, "BLENDED_EDGE", "surface", 1));
    // Cliff-edge blend: one support is a zero-range blend whose spine is the edge curve the ball rolls on.
    Handle(Geom_Curve) cliffCurve;
    if (cliffType) {
        for (int side = 0; side < 2 && cliffCurve.IsNull(); ++side) {
            const Node* sn = node(ptr(blend, "BLENDED_EDGE", "surface", side));
            if (sn && nameOf(*sn) == "BLENDED_EDGE" && num(*sn, "BLENDED_EDGE", "range", 0) == 0 &&
                num(*sn, "BLENDED_EDGE", "range", 1) == 0)
                cliffCurve = curveFor(ptr(*sn, "BLENDED_EDGE", "spine")).curve;
        }
        if (cliffCurve.IsNull() || (a.ok == b.ok))
            return bsFail(11);
    }
    if (!spine.ok || (!cliffType && (!a.ok || !b.ok)))
        return bsFail(3);
    GeomAPI_ProjectPointOnCurve foot(centre, spine.curve);
    if (foot.NbPoints() == 0)
        return bsFail(4);
    gp_Pnt origin;
    gp_Vec tangent;
    spine.curve->D1(foot.LowerDistanceParameter(), origin, tangent);
    if (tangent.SquareMagnitude() <= Precision::SquareConfusion())
        return bsFail(5);

    gp_Pnt footA, footB;
    auto footOnSide = [&](const SurfaceInfo& side, gp_Pnt& out) {
        if (side.ok) {
            GeomAPI_ProjectPointOnSurf on(origin, side.surface);
            if (on.NbPoints() == 0)
                return false;
            out = on.NearestPoint();
            return true;
        }
        GeomAPI_ProjectPointOnCurve on(origin, cliffCurve);
        if (on.NbPoints() == 0)
            return false;
        out = on.NearestPoint();
        return true;
    };
    if (!footOnSide(a, footA) || !footOnSide(b, footB))
        return bsFail(6);
    const gp_Vec fromA(origin, footA), fromB(origin, footB);
    if (fromA.SquareMagnitude() <= Precision::SquareConfusion() ||
        fromB.SquareMagnitude() <= Precision::SquareConfusion() ||
        std::abs(fromA.Magnitude() - r0) > tolerance || std::abs(fromB.Magnitude() - r1) > tolerance)
        return bsFail(7);

    const gp_Vec x = (spine.senseSign < 0 ? fromB : fromA).Normalized();
    const gp_Vec end = (spine.senseSign < 0 ? fromA : fromB).Normalized();
    const double cosAngle = std::clamp(x.Dot(end), -1.0, 1.0);
    const double angle = std::acos(cosAngle);
    if (angle < 1e-9 || angle > kPi - 1e-3)
        return bsFail(8);
    const gp_Vec y = (end - x * cosAngle).Normalized();
    const gp_Vec toCentre(origin, centre);
    if (toCentre.SquareMagnitude() <= Precision::SquareConfusion())
        return bsFail(9);
    for (const double sign : {1.0, -1.0}) {
        const gp_Vec direction = toCentre.Normalized() * sign;
        const double theta = std::atan2(direction.Dot(y), direction.Dot(x));
        const double angularTolerance = std::min(0.05, tolerance / r0);
        if (theta < -angularTolerance || theta > angle + angularTolerance) {
            if (std::getenv("XT_DEBUG_BS"))
                std::fprintf(stderr, "bsd outside sign=%g theta=%g angle=%g r0=%g off=%g |toC|=%g\n", sign, theta, angle, r0, offset, toCentre.Magnitude());
            continue;
        }
        const double section = std::clamp(theta, 0.0, angle);
        const gp_Vec radial = x * std::cos(section) + y * std::sin(section);
        const gp_Pnt candidate = origin.Translated(radial * r0);
        const gp_Vec across = x * -std::sin(section) + y * std::cos(section);
        const gp_Vec normal = tangent.Crossed(across);
        if (normal.SquareMagnitude() <= Precision::SquareConfusion())
            continue;
        const gp_Vec expected = normal.Normalized() * (offset * signOf(blend, "BLENDED_EDGE", "sense"));
        // The side of the blend the dependent ball sits on does not change where it touches the blend;
        // the data shows both sides (the XT blend sense does not predict it reliably), so the contact is
        // accepted when the centre lies |offset| from the candidate along the section normal either way.
        if ((gp_Vec(candidate, centre) - expected).Magnitude() <= tolerance ||
            (!std::getenv("XT_BLEND_OLD_SIGN") && (gp_Vec(candidate, centre) + expected).Magnitude() <= tolerance)) {
            contact = candidate;
            return true;
        }
        if (std::getenv("XT_DEBUG_BS"))
            std::fprintf(stderr, "bsd resid sign=%g plus=%g minus=%g tol=%g r0=%g off=%g |toC|=%g\n", sign,
                         (gp_Vec(candidate, centre) - expected).Magnitude(),
                         (gp_Vec(candidate, centre) + expected).Magnitude(), tolerance, r0, offset, toCentre.Magnitude());
    }
    return bsFail(10);
}

// A rolling-ball blend (BLENDED_EDGE) is defined implicitly: the spine curve is the locus of the ball
// centre, and each cross-section is the circular arc around the spine point between the contact
// points on the two supporting surfaces. The exact surface is not a standard OCCT surface, so it is
// sampled around the part of the spine that the face uses and fitted with a B-spline surface. The
// fit is an approximation; its deviation from the face edges is what the --deviation measurement reports.
SurfaceInfo Builder::blendSurfaceForFace(const Node& blend, const Node& face) {
    SurfaceInfo info;
    auto failBlend = [&](int code) { m_blendWhy = code; return info; };
    m_blendWhy = 0;
    info.kind = "BLENDED_EDGE";
    info.senseSign = signOf(blend, "BLENDED_EDGE", "sense");
    const CurveInfo spine = curveFor(ptr(blend, "BLENDED_EDGE", "spine"));
    const int64_t aIndex = ptr(blend, "BLENDED_EDGE", "surface", 0);
    const int64_t bIndex = ptr(blend, "BLENDED_EDGE", "surface", 1);
    const SurfaceInfo a = surfaceFor(aIndex);
    const SurfaceInfo b = surfaceFor(bIndex);
    if (std::getenv("XT_BLEND_SUPPORT_STATS")) {
        std::string pair = a.kind < b.kind ? a.kind + "+" + b.kind : b.kind + "+" + a.kind;
        pair += std::string(" type ") + static_cast<char>(num(blend, "BLENDED_EDGE", "blend_type"));
        ++res.stats.facesSkippedBySurface["  blend-supports " + pair];
        std::fprintf(stderr, "BLENDPAIR\t%lld\t%s\n", (long long)face.index, pair.c_str());
    }
    const bool nested = !a.ok || !b.ok;
    const Node* nestedA = !a.ok ? node(aIndex) : nullptr; // a support that is itself a blend
    const Node* nestedB = !b.ok ? node(bIndex) : nullptr;
    if (!spine.ok)
        return failBlend(21);
    if (!a.ok && !b.ok && std::getenv("XT_NO_DOUBLE_NESTED"))
        return failBlend(22);
    if ((nestedA == nullptr) != a.ok || (nestedB == nullptr) != b.ok ||
        (nestedA && nameOf(*nestedA) != "BLENDED_EDGE") || (nestedB && nameOf(*nestedB) != "BLENDED_EDGE"))
        return failBlend(23);
    // A cliff-edge blend ('E') rolls a ball between one surface and an edge curve: its second support is a blend of range
    // [0,0] whose spine is that edge curve. The ball centre is r from the surface and r from the edge.
    Handle(Geom_Curve) cliffCurve;
    int cliffSide = -1; // 0 = support a is the edge, 1 = support b
    if (nested && num(blend, "BLENDED_EDGE", "blend_type") == 'E' && !std::getenv("XT_NO_CLIFF_BLEND")) {
        for (int side = 0; side < 2 && cliffCurve.IsNull(); ++side) {
            const Node* sn = side == 0 ? nestedA : nestedB;
            if (sn && num(*sn, "BLENDED_EDGE", "range", 0) == 0 && num(*sn, "BLENDED_EDGE", "range", 1) == 0) {
                cliffCurve = curveFor(ptr(*sn, "BLENDED_EDGE", "spine")).curve;
                cliffSide = side;
            }
        }
    }
    const bool cliff = !cliffCurve.IsNull();
    if (nested && !cliff && num(blend, "BLENDED_EDGE", "blend_type") != 'R')
        return failBlend(24);
    const double aOffset = num(blend, "BLENDED_EDGE", "range", 0) * scale;
    const double bOffset = num(blend, "BLENDED_EDGE", "range", 1) * scale;
    if (nested && (!std::isfinite(aOffset) || !std::isfinite(bOffset) ||
                   std::abs(std::abs(aOffset) - std::abs(bOffset)) > kXtTolerance * scale))
        return failBlend(2);

    // Sample points of the face's edges tell which stretch of the spine the face needs.
    std::vector<gp_Pnt> pts;
    for (const Node* loop : chain(node(ptr(face, "FACE", "loop")), "LOOP", "next")) {
        const Node* start = node(ptr(*loop, "LOOP", "fin"));
        const Node* fin = start;
        std::unordered_set<int64_t> seen;
        for (; fin && seen.insert(fin->index).second; fin = node(ptr(*fin, "FIN", "forward"))) {
            if (cancelled())
                return failBlend(3);
            if (const Node* en = node(ptr(*fin, "FIN", "edge"))) {
                const TopoDS_Edge e = edgeFor(*en);
                double c0, c1;
                if (!e.IsNull()) {
                    const Handle(Geom_Curve) c = BRep_Tool::Curve(e, c0, c1);
                    if (!c.IsNull())
                        for (int k = 0; k <= 8; ++k)
                            pts.push_back(c->Value(c0 + (c1 - c0) * k / 8));
                }
            }
        }
        if (fin != start)
            return failBlend(4);
    }
    if (pts.empty())
        return failBlend(5);
    if (!nested && !spine.curve.IsNull() && !spine.curve->IsPeriodic())
        extendSpine(ptr(blend, "BLENDED_EDGE", "spine"), pts);
    if (!nested)
        correctSpineAgainstSupports(blend);
    const Handle(Geom_Curve) sc = curveFor(ptr(blend, "BLENDED_EDGE", "spine")).curve; // possibly extended or corrected
    bool closedSpine = false;
    double tmin = 1e300, tmax = -1e300;
    std::vector<double> params;
    for (const gp_Pnt& p : pts) {
        GeomAPI_ProjectPointOnCurve proj(p, sc);
        if (proj.NbPoints() == 0)
            continue;
        const double t = proj.LowerDistanceParameter();
        params.push_back(t);
        tmin = std::min(tmin, t);
        tmax = std::max(tmax, t);
    }
    if (!(tmax > tmin))
        return failBlend(6);
    if (sc->IsPeriodic()) {
        // Wrapped parameters of a short blend that crosses the period origin look nearly a full turn long. The
        // span is the smallest arc covering all points: the period less the largest gap between neighbours.
        const double period = sc->Period();
        const double first = sc->FirstParameter();
        std::vector<double> wrapped;
        wrapped.reserve(params.size());
        for (double t : params) {
            double w = std::fmod(t - first, period);
            if (w < 0)
                w += period;
            wrapped.push_back(w);
        }
        std::sort(wrapped.begin(), wrapped.end());
        double bestGap = wrapped.front() + period - wrapped.back(); // gap across the origin
        size_t gapAfter = wrapped.size() - 1;
        for (size_t i = 0; i + 1 < wrapped.size(); ++i)
            if (wrapped[i + 1] - wrapped[i] > bestGap) {
                bestGap = wrapped[i + 1] - wrapped[i];
                gapAfter = i;
            }
        const double span = period - bestGap;
        if (span > 0.5 * period) {
            // Only fit a full sweep when the boundary samples cover nearly
            // the entire spine. A long open arc stays on the existing path.
            if (nested || span < 0.85 * period
                || num(blend, "BLENDED_EDGE", "blend_type") != 'R')
                return failBlend(7);
            closedSpine = true;
            tmin = first;
            tmax = first + period;
        } else {
            const double startWrapped = wrapped[(gapAfter + 1) % wrapped.size()];
            tmin = first + startWrapped;
            tmax = tmin + span;
        }
        if (!(tmax > tmin))
            return failBlend(6);
        ++res.stats.facesSkippedBySurface["  blend-periodic-arc"];
    }
    // A nested contact can end at the supporting blend's boundary.
    const double margin = nested || closedSpine ? 0.0 : 0.02 * (tmax - tmin);
    tmin -= margin;
    tmax += margin;
    if (!sc->IsPeriodic()) {
        if (!Precision::IsInfinite(sc->FirstParameter())) tmin = std::max(tmin, sc->FirstParameter());
        if (!Precision::IsInfinite(sc->LastParameter())) tmax = std::min(tmax, sc->LastParameter());
    }

    if (std::getenv("XT_TRY_OPEN_BLEND_PIPE") && !sc->IsPeriodic() && !nested
        && num(blend, "BLENDED_EDGE", "blend_type") == 'R'
        && num(blend, "BLENDED_EDGE", "thumb_weight", 0) == 1
        && num(blend, "BLENDED_EDGE", "thumb_weight", 1) == 1
        && std::isfinite(aOffset) && std::isfinite(bOffset)
        && std::abs(aOffset) > kBlendContactTolerance * scale
        && std::abs(std::abs(aOffset) - std::abs(bOffset)) <= kXtTolerance * scale) {
        try {
            const Node* spineNode = node(ptr(blend, "BLENDED_EDGE", "spine"));
            if (spineNode && nameOf(*spineNode) == "INTERSECTION" && tmin < tmax) {
                Handle(Geom_Curve) corrected = rebuildPlaneSplineIntersection(*spineNode, sc, tmin, tmax,
                    sc->Value(tmin), sc->Value(tmax), true);
                if (corrected.IsNull()) {
                    int64_t first = ptr(*spineNode, "INTERSECTION", "surface", 0);
                    int64_t second = ptr(*spineNode, "INTERSECTION", "surface", 1);
                    const auto supportedOffset = [&](int64_t index) {
                        const auto* definition = node(index);
                        if (!definition || nameOf(*definition) != "OFFSET_SURF")
                            return false;
                        const auto* basis = node(ptr(*definition, "OFFSET_SURF", "surface"));
                        return basis && (nameOf(*basis) == "B_SURFACE" || nameOf(*basis) == "SWEPT_SURF");
                    };
                    if (supportedOffset(first))
                        std::swap(first, second);
                    const auto* planeNode = node(first);
                    if (planeNode && nameOf(*planeNode) == "PLANE" && supportedOffset(second)) {
                        const auto plane = surfaceFor(first).surface;
                        auto offset = surfaceFor(second).surface;
                        if (offset.IsNull()) {
                            const auto* definition = node(second);
                            const auto basis = surfaceFor(ptr(*definition, "OFFSET_SURF", "surface"));
                            if (basis.ok) {
                                const double distance = num(*definition, "OFFSET_SURF", "offset") * scale
                                    * basis.senseSign * basis.flip;
                                // The local intersection can use smooth spans of a spline
                                // with a C0 knot. The rail check still qualifies its pipe.
                                offset = new Geom_OffsetSurface(basis.surface, distance, true);
                                trace("open-blend-span-offset", face.index);
                            }
                        }
                        if (!plane.IsNull() && !offset.IsNull()) {
                            GeomAPI_IntSS intersection(plane, offset, kXtTolerance * scale);
                            if (intersection.IsDone()) {
                                if (m_trace)
                                    std::fprintf(stderr, "XT open blend intersections face=%lld branches=%d\n",
                                                 (long long)face.index, intersection.NbLines());
                                const double radius = std::abs(aOffset);
                                const double limit = 5e-4 * scale;
                                for (int i = 1; i <= intersection.NbLines(); ++i) {
                                    const auto curve = intersection.Line(i);
                                    if (curve.IsNull() || curve->IsClosed() || curve->IsPeriodic()
                                        || Precision::IsInfinite(curve->FirstParameter())
                                        || Precision::IsInfinite(curve->LastParameter()))
                                        continue;
                                    const bool fits = std::all_of(pts.begin(), pts.end(), [&](const gp_Pnt& p) {
                                        GeomAPI_ProjectPointOnCurve nearest(p, curve);
                                        double distance = std::min(p.Distance(curve->Value(curve->FirstParameter())),
                                                                   p.Distance(curve->Value(curve->LastParameter())));
                                        if (nearest.NbPoints() > 0)
                                            distance = std::min(distance, nearest.LowerDistance());
                                        return std::abs(distance - radius) <= limit;
                                    });
                                    if (!fits)
                                        continue;
                                    if (!corrected.IsNull()) {
                                        corrected.Nullify();
                                        break; // ambiguous branch
                                    }
                                    corrected = curve;
                                }
                                if (!corrected.IsNull())
                                    trace("open-blend-intersection-spine", face.index);
                            }
                        }
                    }
                }
                if (!corrected.IsNull()) {
                    trace("open-blend-refined-spine", face.index);
                    gp_Pnt origin;
                    gp_Vec tangent;
                    corrected->D1(corrected->FirstParameter(), origin, tangent);
                    const Handle(Geom_Circle) profile = new Geom_Circle(gp_Ax2(origin, gp_Dir(tangent)), std::abs(aOffset));
                    const auto path = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(corrected).Edge()).Wire();
                    const auto section = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(profile).Edge()).Wire();
                    BRepOffsetAPI_MakePipeShell sweep(path);
                    const double precision = 2e-7 * scale;
                    sweep.SetDiscreteMode();
                    sweep.SetTolerance(precision, precision, 0.01);
                    sweep.SetMaxDegree(5);
                    sweep.SetMaxSegments(400);
                    sweep.Add(section, false, false);
                    sweep.Build();
                    if (sweep.IsDone() && sweep.ErrorOnSurface() <= precision) {
                        TopExp_Explorer faces(sweep.Shape(), TopAbs_FACE);
                        if (faces.More()) {
                            const auto pipe = BRep_Tool::Surface(TopoDS::Face(faces.Current()));
                            faces.Next();
                            if (!faces.More() && !pipe.IsNull()) {
                                const double limit = 5e-4 * scale;
                                const bool fits = std::all_of(pts.begin(), pts.end(), [&](const gp_Pnt& point) {
                                    GeomAPI_ProjectPointOnSurf projection(point, pipe);
                                    return projection.NbPoints() && projection.LowerDistance() <= limit;
                                });
                                if (fits) {
                                    info.surface = pipe;
                                    info.ok = true;
                                    ++res.stats.surfacesBuilt["BLENDED_EDGE"];
                                    ++res.stats.approximatedGeometry;
                                    trace("open-blend-pipe", face.index);
                                    return info;
                                }
                                trace("open-blend-pipe-rail-mismatch", face.index);
                            }
                        }
                    }
                }
            }
        } catch (const Standard_Failure&) {
            trace("open-blend-pipe-exception", face.index);
        }
    }

    const bool straight = !Handle(Geom_Line)::DownCast(sc).IsNull();
    int nu = 24;
    if (straight)
        nu = 3;
    else if (Handle(Geom_BSplineCurve) bs = Handle(Geom_BSplineCurve)::DownCast(sc))
        nu = std::clamp(bs->NbPoles() * 2, 24, 160);
    constexpr int nv = 8;
    NCollection_Array2<gp_Pnt> grid(1, nu, 1, nv + 1);
    const Handle(ShapeAnalysis_Surface) trackA = a.ok ? new ShapeAnalysis_Surface(a.surface) : Handle(ShapeAnalysis_Surface)();
    const Handle(ShapeAnalysis_Surface) trackB = b.ok ? new ShapeAnalysis_Surface(b.surface) : Handle(ShapeAnalysis_Surface)();
    gp_Pnt2d trackUvA, trackUvB;
    bool trackHaveA = false, trackHaveB = false;
    double radiusSum = 0;
    for (int i = 0; i < nu; ++i) {
        if (cancelled())
            return failBlend(8);
        const double sampleT = tmin + (tmax - tmin) * i / (closedSpine ? nu : nu - 1);
        gp_Pnt c = sc->Value(sampleT);
        if (cliff && (a.ok || b.ok)) {
            // The stored spine is a coarse chart: move each sample to the exact point r from the surface and r from the edge,
            // in the plane through the sample normal to the chart direction (Newton on three conditions).
            const SurfaceInfo& surf = a.ok ? a : b;
            const double r = std::abs(a.ok ? aOffset : bOffset);
            gp_Pnt tmp;
            gp_Vec d;
            sc->D1(sampleT, tmp, d);
            if (d.Magnitude() > 1e-12) {
                d /= d.Magnitude();
                const gp_Pnt seed = c;
                for (int it = 0; it < 12; ++it) {
                    GeomAPI_ProjectPointOnSurf ps(c, surf.surface);
                    GeomAPI_ProjectPointOnCurve pe(c, cliffCurve);
                    if (ps.NbPoints() == 0 || pe.NbPoints() == 0)
                        break;
                    const gp_Pnt fs = ps.NearestPoint(), fe = pe.NearestPoint();
                    const gp_Vec gs(fs, c), ge(fe, c);
                    const double ds = gs.Magnitude(), de = ge.Magnitude();
                    if (ds < 1e-12 || de < 1e-12)
                        break;
                    const gp_Vec ns = gs / ds, ne = ge / de;
                    const double f[3] = {ds - r, de - r, gp_Vec(seed, c).Dot(d)};
                    if (std::abs(f[0]) < 1e-9 * scale && std::abs(f[1]) < 1e-9 * scale && std::abs(f[2]) < 1e-9 * scale)
                        break;
                    double m[3][4] = {{ns.X(), ns.Y(), ns.Z(), -f[0]}, {ne.X(), ne.Y(), ne.Z(), -f[1]}, {d.X(), d.Y(), d.Z(), -f[2]}};
                    bool singular = false;
                    for (int col = 0; col < 3 && !singular; ++col) {
                        int piv = col;
                        for (int row = col + 1; row < 3; ++row)
                            if (std::abs(m[row][col]) > std::abs(m[piv][col])) piv = row;
                        if (std::abs(m[piv][col]) < 1e-9) { singular = true; break; }
                        for (int k = 0; k < 4; ++k) std::swap(m[col][k], m[piv][k]);
                        for (int row = col + 1; row < 3; ++row) {
                            const double fac = m[row][col] / m[col][col];
                            for (int k = col; k < 4; ++k) m[row][k] -= fac * m[col][k];
                        }
                    }
                    if (singular)
                        break;
                    double dx[3];
                    for (int row = 2; row >= 0; --row) {
                        double sum = m[row][3];
                        for (int k = row + 1; k < 3; ++k) sum -= m[row][k] * dx[k];
                        dx[row] = sum / m[row][row];
                    }
                    c.Translate(gp_Vec(dx[0], dx[1], dx[2]));
                }
                ++res.stats.facesSkippedBySurface["  cliff-spine-corrected"];
            }
        }
        std::optional<ScopedTimer> sampleTimer;
        sampleTimer.emplace(res.stats.seconds["spine-sample-check"]);
        if (std::getenv("XT_SAMPLE_CORRECT") && !cliff && !nested && a.ok && b.ok && std::isfinite(aOffset) && std::isfinite(bOffset) && std::abs(aOffset) > 1e-9 &&
            std::abs(bOffset) > 1e-9 && !std::getenv("XT_NO_SPINE_CORRECT")) {
            // Where the stored spine strays from the ball-centre condition (its chart runs on past the surface it was cut
            // from) the sample is moved to the exact centre: |aOffset| from the first support, |bOffset| from the second.
            GeomAPI_ProjectPointOnSurf pa(c, a.surface), pb(c, b.surface);
            const bool off = pa.NbPoints() == 0 || pb.NbPoints() == 0 ||
                std::abs(pa.LowerDistance() - std::abs(aOffset)) > 2.5e-4 * scale || std::abs(pb.LowerDistance() - std::abs(bOffset)) > 2.5e-4 * scale;
            if (off) {
                gp_Pnt tmp;
                gp_Vec d;
                sc->D1(sampleT, tmp, d);
                if (d.Magnitude() > 1e-12) {
                    d /= d.Magnitude();
                    const gp_Pnt seed = c;
                    for (int it = 0; it < 20; ++it) {
                        GeomAPI_ProjectPointOnSurf qa(c, a.surface), qb(c, b.surface);
                        if (qa.NbPoints() == 0 || qb.NbPoints() == 0)
                            break;
                        const gp_Vec ga(qa.NearestPoint(), c), gb(qb.NearestPoint(), c);
                        const double da = ga.Magnitude(), db = gb.Magnitude();
                        if (da < 1e-12 || db < 1e-12)
                            break;
                        const gp_Vec na = ga / da, nb = gb / db;
                        const double f[3] = {da - std::abs(aOffset), db - std::abs(bOffset), gp_Vec(seed, c).Dot(d)};
                        if (std::abs(f[0]) < 1e-9 * scale && std::abs(f[1]) < 1e-9 * scale && std::abs(f[2]) < 1e-9 * scale)
                            break;
                        double m[3][4] = {{na.X(), na.Y(), na.Z(), -f[0]}, {nb.X(), nb.Y(), nb.Z(), -f[1]}, {d.X(), d.Y(), d.Z(), -f[2]}};
                        bool singular = false;
                        for (int col = 0; col < 3 && !singular; ++col) {
                            int piv = col;
                            for (int row = col + 1; row < 3; ++row)
                                if (std::abs(m[row][col]) > std::abs(m[piv][col])) piv = row;
                            if (std::abs(m[piv][col]) < 1e-9) { singular = true; break; }
                            for (int k = 0; k < 4; ++k) std::swap(m[col][k], m[piv][k]);
                            for (int row = col + 1; row < 3; ++row) {
                                const double fac = m[row][col] / m[col][col];
                                for (int k = col; k < 4; ++k) m[row][k] -= fac * m[col][k];
                            }
                        }
                        if (singular)
                            break;
                        double dx[3];
                        for (int row = 2; row >= 0; --row) {
                            double sum = m[row][3];
                            for (int k = row + 1; k < 3; ++k) sum -= m[row][k] * dx[k];
                            dx[row] = sum / m[row][row];
                        }
                        c.Translate(gp_Vec(dx[0], dx[1], dx[2]));
                    }
                    ++res.stats.facesSkippedBySurface["  spine-sample-corrected"];
                }
            }
        }
        auto projectContact = [&](const SurfaceInfo& support, double offset, gp_Pnt& q) {
            const bool isASupport = &support == &a;
            if (!support.ok && cliff && cliffSide == (isASupport ? 0 : 1)) {
                GeomAPI_ProjectPointOnCurve onEdge(c, cliffCurve);
                if (onEdge.NbPoints() == 0)
                    return false;
                q = onEdge.NearestPoint();
                return std::abs(c.Distance(q) - std::abs(offset)) <= kBlendContactTolerance * scale;
            }
            if (!support.ok)
                return blendSupportContact(isASupport ? *nestedA : *nestedB, c, offset, q);
            {
                // Tracked projection: the foot moves little between neighbouring spine samples.
                const bool isA = &support == &a;
                const Handle(ShapeAnalysis_Surface)& an = isA ? trackA : trackB;
                gp_Pnt2d& uvT = isA ? trackUvA : trackUvB;
                bool& haveT = isA ? trackHaveA : trackHaveB;
                try {
                    const double tolT = 1e-6 * scale;
                    uvT = haveT ? an->NextValueOfUV(uvT, c, tolT, 1e3 * tolT) : an->ValueOfUV(c, tolT);
                    haveT = true;
                    q = support.surface->Value(uvT.X(), uvT.Y());
                } catch (const Standard_Failure&) {
                    return false;
                }
            }
            const bool good = !nested || std::abs(c.Distance(q) - std::abs(offset)) <= kBlendContactTolerance * scale;
            if (!good && std::getenv("XT_DEBUG_BS"))
                std::fprintf(stderr, "blend %lld i=%d: nested distance %g vs offset %g on %s\n", (long long)blend.index, i, c.Distance(q), offset, support.kind.c_str());
            return good;
        };
        gp_Pnt q0, q1;
        if (!projectContact(a, aOffset, q0) || !projectContact(b, bOffset, q1)) {
            if (std::getenv("XT_DEBUG_BS"))
                std::fprintf(stderr, "blend %lld fails at i=%d of %d nested=%d a=%s b=%s\n", (long long)blend.index, i, nu, (int)nested, a.kind.c_str(), b.kind.c_str());
            return failBlend(9);
        }
        if (spine.senseSign < 0)
            std::swap(q0, q1);
        const double r0 = c.Distance(q0), r1 = c.Distance(q1);
        if (r0 < 1e-9 || r1 < 1e-9)
            return failBlend(10);
        const gp_Vec d0(c, q0), d1(c, q1);
        const gp_Vec u0 = d0 / r0, u1 = d1 / r1;
        const double r = 0.5 * (r0 + r1);
        radiusSum += r;
        const double alpha = std::acos(std::clamp(u0.Dot(u1), -1.0, 1.0));
        // alpha ~ 0: the two contacts coincide (the blend closes up here); every point of the arc is the contact itself.
        if (alpha > kPi - 1e-3 || (alpha < 1e-9 && std::getenv("XT_NO_BLEND_CLOSE")))
            return failBlend(11);
        for (int j = 0; j <= nv; ++j) {
            const double v = static_cast<double>(j) / nv;
            const gp_Vec dir = alpha < 1e-9 ? u0 : (std::sin((1 - v) * alpha) * u0 + std::sin(v * alpha) * u1) / std::sin(alpha);
            grid.SetValue(i + 1, j + 1, gp_Pnt(c.XYZ() + r * dir.XYZ()));
        }
    }
    try {
        const double tol = std::getenv("XT_BLEND_FIT_TOL") ? std::atof(std::getenv("XT_BLEND_FIT_TOL")) * scale : std::max(1e-5 * scale, 2e-5 * radiusSum / nu);
        GeomAPI_PointsToBSplineSurface fit;
        fit.Init(grid, Approx_ChordLength, 3, 5, GeomAbs_C2, tol, closedSpine);
        if (!fit.IsDone())
            return failBlend(12);
        info.surface = fit.Surface();
    } catch (const Standard_Failure&) {
        return failBlend(13);
    }
    info.ok = !info.surface.IsNull();
    if (info.ok) {
        ++res.stats.surfacesBuilt["BLENDED_EDGE"];
        if (nested)
            ++res.stats.surfacesBuilt["BLENDED_EDGE (blend support)"];
        ++res.stats.approximatedGeometry;
        if (closedSpine)
            trace("periodic-blend-surface", face.index);
    }
    return info;
}

// ---- topology -------------------------------------------------------------------------

TopoDS_Vertex Builder::vertexFor(int64_t index) {
    const auto found = m_vertices.find(index);
    if (found != m_vertices.end())
        return found->second;
    TopoDS_Vertex v;
    if (const Node* vn = node(index)) {
        if (const Node* point = node(ptr(*vn, "VERTEX", "point"))) {
            double tol = num(*vn, "VERTEX", "tolerance");
            tol = std::isnan(tol) ? kXtTolerance : tol;
            bb.MakeVertex(v, pnt(*point, "POINT", "pvec"), std::max(tol * scale, kXtTolerance * scale));
            if (std::getenv("XT_DEBUG_TOL") && tol * scale > 0.5)
                std::fprintf(stderr, "vertex %lld xt tolerance %g (m)\n", (long long)index, tol);
        }
    }
    m_vertices.emplace(index, v);
    return v;
}

TopoDS_Edge Builder::edgeForImpl(const Node& edge) {
    const auto found = m_edges.find(edge.index);
    if (found != m_edges.end())
        return found->second;
    TopoDS_Edge result;
    auto remember = [&](const TopoDS_Edge& e) {
        m_edges.emplace(edge.index, e);
        return e;
    };
    ++res.stats.edges;

    // End vertices: the forward vertex of a positive fin is the edge end, of a negative fin the start.
    const Node* startV = nullptr;
    const Node* endV = nullptr;
    {
        const Node* first = node(ptr(edge, "EDGE", "fin"));
        const Node* fin = first;
        for (size_t guard = 0; fin && guard < 64; ++guard) {
            const Node* v = node(ptr(*fin, "FIN", "vertex"));
            if (num(*fin, "FIN", "sense") == '+')
                endV = endV ? endV : v;
            else
                startV = startV ? startV : v;
            fin = node(ptr(*fin, "FIN", "other"));
            if (fin == first)
                break;
        }
    }

    const int64_t curveIndex = ptr(edge, "EDGE", "curve");
    CurveInfo tolerant; // a tolerant edge has no 3D curve; one is rebuilt from the SP-curve of a fin
    if (!curveIndex) {
        tolerant.curve = tolerantCurve(edge);
        if (tolerant.curve.IsNull() && startV && endV && std::getenv("XT_TRY_BLEND_SECTION")) {
            const auto startVertex = vertexFor(startV->index), endVertex = vertexFor(endV->index);
            if (!startVertex.IsNull() && !endVertex.IsNull()) {
                tolerant.curve = transverseBlendCurve(edge, BRep_Tool::Pnt(startVertex), BRep_Tool::Pnt(endVertex));
                if (!tolerant.curve.IsNull())
                    trace("rebuild-transverse-blend-edge", edge.index);
            }
        }
        tolerant.ok = !tolerant.curve.IsNull();
        tolerant.kind = "SP_CURVE";
        if (!tolerant.ok) {
            ++res.stats.tolerantEdgesSkipped;
            return remember(result);
        }
        { ++res.stats.tolerantEdgesRebuilt; ++res.stats.approximatedGeometry; }
    }
    const CurveInfo& ci = curveIndex ? curveFor(curveIndex) : tolerant;
    if (!ci.ok) {
        const Node* cn = node(curveIndex);
        ++res.stats.edgesSkippedByCurve[cn ? nameOf(*cn) : std::string("missing")];
        return remember(result);
    }

    // Curve in edge direction and its parameter range.
    Handle(Geom_Curve) curve = ci.curve;
    double first = 0, last = 0;
    const bool reverse = ci.senseSign < 0;
    if (ci.trimmed && !startV && !endV) {
        // Parameters follow the basis curve; with a negative basis sense parm_2 < parm_1.
        if (reverse) {
            const double a = curve->ReversedParameter(ci.parm1), b = curve->ReversedParameter(ci.parm2);
            curve = curve->Reversed();
            first = a;
            last = b;
        } else {
            first = ci.parm1;
            last = ci.parm2;
        }
        if (curve->IsPeriodic() && std::abs(last - first) < 1e-12)
            last = first + curve->Period();
    } else {
        if (reverse)
            curve = curve->Reversed();
        const TopoDS_Vertex sv = startV ? vertexFor(startV->index) : TopoDS_Vertex();
        const TopoDS_Vertex ev = endV ? vertexFor(endV->index) : TopoDS_Vertex();
        const bool haveS = !sv.IsNull(), haveE = !ev.IsNull();
        if (!haveS && !haveE) {
            first = curve->FirstParameter();
            last = curve->LastParameter();
            if (Precision::IsInfinite(first) || Precision::IsInfinite(last)) {
                ++res.stats.edgesSkippedByCurve["unbounded-ring"];
                return remember(result);
            }
        } else if (haveS && haveE) {
            auto project = [&](const gp_Pnt& p, double& t) {
                if (Handle(Geom_Line) line = Handle(Geom_Line)::DownCast(curve)) {
                    const gp_Ax1 ax = line->Position();
                    t = gp_Vec(ax.Direction()).Dot(gp_Vec(ax.Location(), p));
                    return true;
                }
                double best = 1e300;
                t = 0;
                GeomAPI_ProjectPointOnCurve proj(p, curve);
                if (proj.NbPoints() > 0) {
                    t = proj.LowerDistanceParameter();
                    best = proj.LowerDistance();
                }
                // A vertex at (or just beyond) an end of a bounded curve may have no interior extremum.
                if (!curve->IsPeriodic() && !Precision::IsInfinite(curve->FirstParameter()) && !Precision::IsInfinite(curve->LastParameter())) {
                    for (const double u : {curve->FirstParameter(), curve->LastParameter()}) {
                        const double d = p.Distance(curve->Value(u));
                        if (d < best) {
                            best = d;
                            t = u;
                        }
                    }
                }
                return best < 1e299;
            };
            if (!project(BRep_Tool::Pnt(sv), first) || !project(BRep_Tool::Pnt(ev), last)) {
                ++res.stats.edgesSkippedByCurve["projection-failed"];
                return remember(result);
            }
            if (curve->IsPeriodic()) {
                const double period = curve->Period();
                if (last <= first + 1e-9 * period)
                    last += period;
                if (last > first + period * (1 + 1e-9))
                    last -= period;
            } else if (last < first) {
                // The vertices run against the curve direction: try the reversed curve once.
                curve = curve->Reversed();
                if (project(BRep_Tool::Pnt(sv), first) && project(BRep_Tool::Pnt(ev), last) && last > first)
                    ++res.stats.edgeDirectionFlips;
                else {
                    ++res.stats.edgeDirectionConflicts;
                    return remember(result);
                }
            }
        } else {
            ++res.stats.edgesSkippedByCurve["one-vertex"];
            return remember(result);
        }
    }
    // An edge shorter than a few modeller resolutions is degenerate: the mesher cannot discretise it
    // (it crashed on one), so the edge, and with it the faces that use it, are left out.
    if (!curve->IsPeriodic() && last > first && curve->Value(first).Distance(curve->Value(last)) < 10 * kXtTolerance * scale &&
        curve->Value(first).Distance(curve->Value(0.5 * (first + last))) < 10 * kXtTolerance * scale) {
        ++res.stats.edgesSkippedByCurve["micro-edge"];
        // The faces around it still close once its two end vertices are one: the edge is left out of their
        // wires and the vertices are merged when the body is assembled.
        if (startV && endV && startV->index == endV->index)
            m_microEdges.insert(edge.index); // a tiny closed loop on one vertex simply vanishes
        else if (startV && endV) {
            const TopoDS_Vertex vs = vertexFor(startV->index);
            const TopoDS_Vertex ve = vertexFor(endV->index);
            if (!vs.IsNull() && !ve.IsNull() && !vs.IsSame(ve)) {
                const double gap = BRep_Tool::Pnt(vs).Distance(BRep_Tool::Pnt(ve));
                bb.UpdateVertex(vs, std::max(BRep_Tool::Tolerance(vs), gap + kXtTolerance * scale));
                m_reshape->Replace(ve, vs.Oriented(ve.Orientation()));
                m_microEdges.insert(edge.index);
            }
        }
        return remember(result);
    }
    if (Precision::IsInfinite(first) || Precision::IsInfinite(last)) {
        ++res.stats.edgesSkippedByCurve["unbounded"];
        return remember(result);
    }
    {
        // An edge whose curve runs far beyond the distance between its end vertices is wrong (a vertex
        // projected onto the wrong part of a curve, or a chart that does not match): it would produce
        // long spikes in the mesh. Closed edges (one vertex, or a full period) are exempt.
        const bool sameVertex = startV && endV && startV->index == endV->index;
        const bool closedEdge = sameVertex || (!startV && !endV) || (curve->IsPeriodic() && std::abs(last - first - curve->Period()) < 1e-6 * curve->Period());
        if (!closedEdge && !curve->IsPeriodic() && last > first) {
            double length = 0;
            gp_Pnt prev = curve->Value(first);
            const gp_Pnt begin = prev;
            for (int k = 1; k <= 16; ++k) {
                const gp_Pnt cur = curve->Value(first + (last - first) * k / 16);
                length += prev.Distance(cur);
                prev = cur;
            }
            const double chord = begin.Distance(prev);
            bool wild = length > 20 * chord + 1e-3 * scale;
            if (wild && !std::getenv("XT_NO_LOOP_EDGE")) {
                // A curve that is closed up to a small gap is a loop; an edge that runs forwards along it
                // from one vertex to a vertex close by (near the seam) legitimately covers almost the whole
                // loop. Only a curve that is not such a loop makes a long edge between close vertices wrong.
                const double fullFirst = curve->FirstParameter();
                const double fullLast = curve->LastParameter();
                if (!Precision::IsInfinite(fullFirst) && !Precision::IsInfinite(fullLast) &&
                    curve->Value(fullFirst).Distance(curve->Value(fullLast)) < 0.01 * length) {
                    wild = false;
                    ++res.stats.edgesSkippedByCurve["loop-edge-kept"];
                }
            }
            if (wild) {
                if (std::getenv("XT_DEBUG_TOL"))
                    std::fprintf(stderr, "wild kind=%s length=%g chord=%g range=[%g %g] per=%d same=%d full=[%g %g] ends-gap=%g\n", ci.kind.c_str(), length, chord, first, last, (int)curve->IsPeriodic(), (int)sameVertex, curve->FirstParameter(), curve->LastParameter(), curve->Value(curve->FirstParameter()).Distance(curve->Value(curve->LastParameter())));
                ++res.stats.edgesSkippedByCurve["wild-edge"];
                return remember(result);
            }
        }
    }
    {
        // The curve must reach its end vertices. Where it does not, SameParameter would stretch the vertex
        // tolerances to cover the gap, and everything that touches those vertices turns into spikes.
        const TopoDS_Vertex vs = startV ? vertexFor(startV->index) : TopoDS_Vertex();
        const TopoDS_Vertex ve = endV ? vertexFor(endV->index) : TopoDS_Vertex();
        // Charts of intersection curves are fitted to sample points and may stop a little short of the
        // vertex. A gap of a couple of millimetres in a metre-sized part is absorbed as vertex tolerance;
        // a gap that is a large part of the edge itself is a wrong curve.
        double chordLength = 0;
        {
            gp_Pnt prevPoint = curve->Value(first);
            for (int k = 1; k <= 8; ++k) {
                const gp_Pnt cur = curve->Value(first + (last - first) * k / 8);
                chordLength += prevPoint.Distance(cur);
                prevPoint = cur;
            }
        }
        const double reach = std::max(1e-4 * scale, std::min(kMaxVertexGap * scale, 0.25 * chordLength));
        if ((!vs.IsNull() && curve->Value(first).Distance(BRep_Tool::Pnt(vs)) > reach) ||
            (!ve.IsNull() && curve->Value(last).Distance(BRep_Tool::Pnt(ve)) > reach)) {
            if (std::getenv("XT_DEBUG_TOL"))
                std::fprintf(stderr, "vmismatch kind=%s%s d0=%g d1=%g range=[%g %g] per=%d\n", ci.kind.c_str(), ci.trimmed ? "(trimmed)" : "",
                             vs.IsNull() ? -1.0 : curve->Value(first).Distance(BRep_Tool::Pnt(vs)),
                             ve.IsNull() ? -1.0 : curve->Value(last).Distance(BRep_Tool::Pnt(ve)), first, last, (int)curve->IsPeriodic());
            ++res.stats.edgesSkippedByCurve["vertex-mismatch"];
            return remember(result);
        }
    }
    if (!(last > first)) {
        ++res.stats.edgesSkippedByCurve["empty-range"];
        return remember(result);
    }

    const TopoDS_Vertex sv = startV ? vertexFor(startV->index) : TopoDS_Vertex();
    const TopoDS_Vertex ev = endV ? vertexFor(endV->index) : TopoDS_Vertex();
    if (!sv.IsNull() && !ev.IsNull() && !sv.IsSame(ev) &&
        (ci.kind == "INTERSECTION" || ci.kind == "TRIMMED_CURVE")) {
        const Node* inter = node(curveIndex);
        if (inter && nameOf(*inter) == "TRIMMED_CURVE")
            inter = node(ptr(*inter, "TRIMMED_CURVE", "basis_curve"));
        if (inter && nameOf(*inter) == "INTERSECTION") {
            const auto rebuilt = rebuildPlaneSplineIntersection(*inter, curve, first, last,
                BRep_Tool::Pnt(sv), BRep_Tool::Pnt(ev));
            if (!rebuilt.IsNull()) {
                curve = rebuilt;
                first = curve->FirstParameter();
                last = curve->LastParameter();
                trace("rebuild-plane-spline-edge", edge.index);
            } else if (const auto contact = rebuildSplineBlendRail(*inter, curve, first, last,
                           BRep_Tool::Pnt(sv), BRep_Tool::Pnt(ev)); !contact.IsNull()) {
                curve = contact;
                first = curve->FirstParameter();
                last = curve->LastParameter();
                trace("rebuild-spline-blend-rail", edge.index);
            }
        }
    }
    if (ci.kind == "INTERSECTION" && !sv.IsNull() && !ev.IsNull() && !sv.IsSame(ev)) {
        const Node* inter = node(curveIndex);
        const Node* chart = inter ? node(ptr(*inter, "INTERSECTION", "chart")) : nullptr;
        if (chart && num(*chart, "CHART", "chart_count") == 2) {
            const gp_Pnt a = BRep_Tool::Pnt(sv), b = BRep_Tool::Pnt(ev);
            const auto contact = rebuildBlendContact(*inter, a, b);
            if (!contact.IsNull()) {
                curve = contact;
                first = curve->FirstParameter();
                last = curve->LastParameter();
                trace("rebuild-blend-contact-edge", edge.index);
            }
            const double length = a.Distance(b);
            const double chordalError = num(*chart, "CHART", "chordal_error") * scale;
            const double budget = std::min({chordalError, 1e-4 * scale, 0.05 * length});
            const double gap = std::max(a.Distance(curve->Value(first)), b.Distance(curve->Value(last)));
            if (contact.IsNull() && std::isfinite(budget) && gap > kXtTolerance * scale && gap <= budget) {
                // A two-point chart supplies a chord. Keep the topological end points
                // when that chord still agrees with the chart within its error budget.
                const gp_Vec span(a, b);
                bool agrees = true;
                for (int i = 1; i < 8 && agrees; ++i) {
                    const double t = i / 8.0;
                    agrees = a.Translated(span * t).Distance(curve->Value(first + (last - first) * t)) <= budget;
                }
                if (agrees) {
                    curve = new Geom_Line(a, gp_Dir(span));
                    first = 0;
                    last = length;
                    trace("anchor-intersection-chord", edge.index);
                }
            }
        }
    }
    TopoDS_Edge e;
    const double xtTol = num(edge, "EDGE", "tolerance");
    bb.MakeEdge(e, curve, TopLoc_Location(), std::isnan(xtTol) ? kXtTolerance * scale : std::max(xtTol * scale, kXtTolerance * scale));
    if (!sv.IsNull() && !ev.IsNull()) {
        if (startV == endV || startV->index == endV->index) {
            bb.Add(e, sv.Oriented(TopAbs_FORWARD));
            bb.Add(e, sv.Oriented(TopAbs_REVERSED));
            bb.UpdateVertex(sv, first, e, BRep_Tool::Tolerance(sv));
        } else {
            bb.Add(e, sv.Oriented(TopAbs_FORWARD));
            bb.Add(e, ev.Oriented(TopAbs_REVERSED));
            bb.UpdateVertex(sv, first, e, BRep_Tool::Tolerance(sv));
            bb.UpdateVertex(ev, last, e, BRep_Tool::Tolerance(ev));
        }
    }
    if (sv.IsNull() && ev.IsNull()) {
        // Ring edge (no vertices in XT): OCCT needs one vertex that closes the edge.
        TopoDS_Vertex rv;
        bb.MakeVertex(rv, curve->Value(first), kXtTolerance * scale);
        bb.Add(e, rv.Oriented(TopAbs_FORWARD));
        bb.Add(e, rv.Oriented(TopAbs_REVERSED));
        bb.UpdateVertex(rv, first, e, kXtTolerance * scale);
    }
    bb.Range(e, first, last);
    m_edgeKind[e.TShape().get()] = ci.kind + (ci.trimmed ? "(trimmed)" : "") + (ci.senseSign < 0 ? "(-)" : "");
    ++res.stats.edgesBuilt;
    return remember(e);
}

// A positively wound great-circle triangle contained in a hemisphere needs
// neither a pole nor a seam. Centre its longitude around pi, away from U=0.
static Handle(Geom_SphericalSurface) sphericalTriangleChart(
    const Handle(Geom_SphericalSurface)& sphere, const TopoDS_Wire& wire, double tolerance)
{
    if (sphere.IsNull())
        return {};
    const gp_Pnt centre = sphere->Location();
    const double radius = sphere->Radius();
    std::array<gp_Pnt, 3> starts, ends;
    size_t count = 0;
    for (TopoDS_Iterator it(wire); it.More(); it.Next()) {
        if (count == starts.size())
            return {};
        const TopoDS_Edge edge = TopoDS::Edge(it.Value());
        double first, last;
        const auto circle = Handle(Geom_Circle)::DownCast(BRep_Tool::Curve(edge, first, last));
        if (circle.IsNull() || !(last > first && last - first < kPi) ||
            circle->Location().Distance(centre) > tolerance || std::abs(circle->Radius() - radius) > tolerance)
            return {};
        const bool reversed = edge.Orientation() == TopAbs_REVERSED;
        starts[count] = circle->Value(reversed ? last : first);
        ends[count++] = circle->Value(reversed ? first : last);
    }
    if (count != starts.size())
        return {};

    std::array<gp_Vec, 3> directions;
    gp_Vec sum(0, 0, 0);
    for (size_t i = 0; i < count; ++i) {
        if (ends[i].Distance(starts[(i + 1) % count]) > tolerance)
            return {};
        directions[i] = gp_Vec(centre, starts[i]) / radius;
        sum += directions[i];
    }
    if (sum.SquareMagnitude() <= Precision::SquareConfusion())
        return {};
    const gp_Vec x = sum.Normalized();
    const gp_Vec z = x.Crossed(directions.front());
    if (z.SquareMagnitude() <= Precision::SquareConfusion())
        return {};

    double winding = 0;
    for (size_t i = 0; i < count; ++i) {
        if (x.Dot(directions[i]) <= tolerance / radius)
            return {};
        winding += x.Dot(directions[i].Crossed(directions[(i + 1) % count]));
    }
    if (winding <= Precision::Angular())
        return {};
    return new Geom_SphericalSurface(gp_Ax3(centre, gp_Dir(z), gp_Dir(x.Reversed())), radius);
}

static bool circularRevolutionUV(const Geom_SurfaceOfRevolution& revolution, const Geom_Circle& circle,
                                 const gp_Pnt& point, double limit, gp_Pnt2d& uv) {
    const gp_Ax1 axis = revolution.Axis();
    const gp_Vec direction(axis.Direction());
    const gp_Ax2& frame = circle.Position();
    const double a = circle.Radius() * direction.Dot(gp_Vec(frame.XDirection()));
    const double b = circle.Radius() * direction.Dot(gp_Vec(frame.YDirection()));
    const double amplitude = std::hypot(a, b);
    if (amplitude <= Precision::Confusion())
        return false;
    const double relative = gp_Vec(circle.Location(), point).Dot(direction) / amplitude;
    if (!(std::abs(relative) <= 1.0))
        return false;
    const double phase = std::atan2(b, a), delta = std::acos(relative);
    const auto radial = [&](const gp_Pnt& p) {
        const gp_Vec r(axis.Location(), p);
        return r - direction * r.Dot(direction);
    };
    const gp_Vec target = radial(point);
    const double targetRadius = target.Magnitude();
    if (targetRadius <= Precision::Confusion())
        return false;
    const double v0 = phase + delta, v1 = phase - delta;
    const gp_Vec r0 = radial(circle.Value(v0)), r1 = radial(circle.Value(v1));
    const double d0 = std::abs(r0.Magnitude() - targetRadius);
    const double d1 = std::abs(r1.Magnitude() - targetRadius);
    const gp_Vec& chosen = d0 <= d1 ? r0 : r1;
    if (std::min(d0, d1) > limit || chosen.Magnitude() <= Precision::Confusion())
        return false;
    const double u = chosen.AngleWithRef(target, direction);
    const double v = std::fmod(d0 <= d1 ? v0 : v1, circle.Period());
    uv.SetCoord(u < 0 ? u + revolution.UPeriod() : u, v < 0 ? v + circle.Period() : v);
    return true;
}

void Builder::correctSpineAgainstSupports(const Node& blend) {
    if (std::getenv("XT_NO_SPINE_CORRECT") || num(blend, "BLENDED_EDGE", "blend_type") != 'R')
        return;
    const int64_t spineIndex = ptr(blend, "BLENDED_EDGE", "spine");
    if (!m_spineCorrected.insert(blend.index).second)
        return;
    ScopedTimer correctTimer(res.stats.seconds["spine-correct"]);
    const SurfaceInfo a = surfaceFor(ptr(blend, "BLENDED_EDGE", "surface", 0)); // copies: the maps may rehash
    const SurfaceInfo b = surfaceFor(ptr(blend, "BLENDED_EDGE", "surface", 1));
    const double ra = std::abs(num(blend, "BLENDED_EDGE", "range", 0)) * scale, rb = std::abs(num(blend, "BLENDED_EDGE", "range", 1)) * scale;
    if (!a.ok || !b.ok || a.kind == "BLENDED_EDGE" || b.kind == "BLENDED_EDGE" || !(ra > 1e-9) || !(rb > 1e-9))
        return;
    const CurveInfo info = curveFor(spineIndex); // copy
    if (!info.ok || info.curve.IsNull() || info.curve->IsPeriodic() || Precision::IsInfinite(info.curve->FirstParameter()) ||
        Precision::IsInfinite(info.curve->LastParameter()))
        return;
    const Handle(Geom_Curve) curve = info.curve;
    const double t0 = curve->FirstParameter(), t1 = curve->LastParameter();
    const int n = 120;
    const Handle(ShapeAnalysis_Surface) sa = new ShapeAnalysis_Surface(a.surface), sb = new ShapeAnalysis_Surface(b.surface);
    const double uvTol = 1e-6 * scale;
    gp_Pnt2d uvA, uvB;
    bool haveUv = false;
    // Foot of a point on a support, tracked from the previous foot (the cached projector is far cheaper than a fresh one).
    auto foot = [&](const Handle(ShapeAnalysis_Surface)& analysis, const Handle(Geom_Surface)& surface, gp_Pnt2d& uv, const gp_Pnt& p) {
        uv = haveUv ? analysis->NextValueOfUV(uv, p, uvTol, 1e3 * uvTol) : analysis->ValueOfUV(p, uvTol);
        return surface->Value(uv.X(), uv.Y());
    };
    {
        // Most spines are already exact: probe a few samples before doing the full pass.
        bool allGood = true;
        gp_Pnt2d pa, pb;
        bool have = false;
        try {
            for (int i = 0; i <= 12 && allGood; ++i) {
                const gp_Pnt c = curve->Value(t0 + (t1 - t0) * i / 12);
                pa = have ? sa->NextValueOfUV(pa, c, uvTol, 1e3 * uvTol) : sa->ValueOfUV(c, uvTol);
                pb = have ? sb->NextValueOfUV(pb, c, uvTol, 1e3 * uvTol) : sb->ValueOfUV(c, uvTol);
                have = true;
                allGood = std::abs(c.Distance(a.surface->Value(pa.X(), pa.Y())) - ra) <= 2.5e-4 * scale &&
                          std::abs(c.Distance(b.surface->Value(pb.X(), pb.Y())) - rb) <= 2.5e-4 * scale;
            }
        } catch (const Standard_Failure&) {
            allGood = false;
        }
        if (allGood)
            return;
    }
    std::vector<gp_Pnt> points;
    bool any = false;
    gp_Pnt previous;
    try {
        for (int i = 0; i <= n; ++i) {
            const double t = t0 + (t1 - t0) * i / n;
            gp_Pnt c = curve->Value(t);
            const gp_Pnt fa = foot(sa, a.surface, uvA, c), fb = foot(sb, b.surface, uvB, c);
            haveUv = true;
            const bool off = std::abs(c.Distance(fa) - ra) > 2.5e-4 * scale || std::abs(c.Distance(fb) - rb) > 2.5e-4 * scale;
            if (off) {
                gp_Pnt tmp;
                gp_Vec d;
                curve->D1(t, tmp, d);
                if (d.Magnitude() > 1e-12) {
                    d /= d.Magnitude();
                    const gp_Pnt seed = c;
                    for (int it = 0; it < 20; ++it) {
                        const gp_Pnt qa = foot(sa, a.surface, uvA, c), qb = foot(sb, b.surface, uvB, c);
                        const gp_Vec ga(qa, c), gb(qb, c);
                        const double da = ga.Magnitude(), db = gb.Magnitude();
                        if (da < 1e-12 || db < 1e-12)
                            break;
                        const gp_Vec na = ga / da, nb = gb / db;
                        const double f[3] = {da - ra, db - rb, gp_Vec(seed, c).Dot(d)};
                        if (std::abs(f[0]) < 1e-9 * scale && std::abs(f[1]) < 1e-9 * scale && std::abs(f[2]) < 1e-9 * scale)
                            break;
                        double m[3][4] = {{na.X(), na.Y(), na.Z(), -f[0]}, {nb.X(), nb.Y(), nb.Z(), -f[1]}, {d.X(), d.Y(), d.Z(), -f[2]}};
                        bool singular = false;
                        for (int col = 0; col < 3 && !singular; ++col) {
                            int piv = col;
                            for (int row = col + 1; row < 3; ++row)
                                if (std::abs(m[row][col]) > std::abs(m[piv][col])) piv = row;
                            if (std::abs(m[piv][col]) < 1e-9) { singular = true; break; }
                            for (int k = 0; k < 4; ++k) std::swap(m[col][k], m[piv][k]);
                            for (int row = col + 1; row < 3; ++row) {
                                const double fac = m[row][col] / m[col][col];
                                for (int k = col; k < 4; ++k) m[row][k] -= fac * m[col][k];
                            }
                        }
                        if (singular)
                            break;
                        double dx[3];
                        for (int row = 2; row >= 0; --row) {
                            double sum = m[row][3];
                            for (int k = row + 1; k < 3; ++k) sum -= m[row][k] * dx[k];
                            dx[row] = sum / m[row][row];
                        }
                        c.Translate(gp_Vec(dx[0], dx[1], dx[2]));
                        if (std::sqrt(dx[0] * dx[0] + dx[1] * dx[1] + dx[2] * dx[2]) < 1e-8 * scale)
                            break; // converged: the residual cannot drop further than the projection noise
                    }
                    any = true;
                }
            }
            if (i > 0 && c.Distance(previous) < 1e-7 * scale)
                continue; // collapsed onto the previous point: drop the duplicate
            points.push_back(c);
            previous = c;
        }
    } catch (const Standard_Failure&) {
        return;
    }
    if (!any || points.size() < 4)
        return;
    try {
        Handle(NCollection_HArray1<gp_Pnt>) arr = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(points.size()));
        for (size_t i = 0; i < points.size(); ++i)
            arr->SetValue(static_cast<int>(i) + 1, points[i]);
        GeomAPI_Interpolate fit(arr, false, Precision::Confusion());
        fit.Perform();
        if (!fit.IsDone())
            return;
        m_curves[spineIndex].curve = fit.Curve();
        ++res.stats.facesSkippedBySurface["  spine-corrected"];
    } catch (const Standard_Failure&) {
    }
}

bool Builder::extendSpine(int64_t spineIndex, const std::vector<gp_Pnt>& points) {
    if (std::getenv("XT_NO_EXTEND_SPINE") || points.empty())
        return false;
    ScopedTimer extendTimer(res.stats.seconds["spine-extend"]);
    const Node* sn = node(spineIndex);
    if (!sn || nameOf(*sn) != "INTERSECTION")
        return false;
    const SurfaceInfo s0 = surfaceFor(ptr(*sn, "INTERSECTION", "surface", 0)); // copies: the maps may rehash
    const SurfaceInfo s1 = surfaceFor(ptr(*sn, "INTERSECTION", "surface", 1));
    auto supported = [](const SurfaceInfo& s) { return s.ok && s.kind != "BLENDED_EDGE" && s.kind != "BLEND_BOUND"; };
    if (!supported(s0) || !supported(s1)) {
        if (std::getenv("XT_DEBUG_CS")) std::fprintf(stderr, "spine %lld: unsupported surfaces %s ok=%d / %s ok=%d\n", (long long)spineIndex, s0.kind.c_str(), (int)s0.ok, s1.kind.c_str(), (int)s1.ok);
        return false;
    }
    const CurveInfo info = curveFor(spineIndex); // copy
    if (!info.ok || info.curve.IsNull() || info.curve->IsPeriodic()) {
        if (std::getenv("XT_DEBUG_CS")) std::fprintf(stderr, "spine %lld: curve not usable\n", (long long)spineIndex);
        return false;
    }
    Handle(Geom_Curve) curve = info.curve;
    const double c0 = curve->FirstParameter(), c1 = curve->LastParameter();
    if (Precision::IsInfinite(c0) || Precision::IsInfinite(c1))
        return false;
    double length = 0;
    {
        gp_Pnt prev = curve->Value(c0);
        for (int k = 1; k <= 32; ++k) {
            const gp_Pnt cur = curve->Value(c0 + (c1 - c0) * k / 32);
            length += prev.Distance(cur);
            prev = cur;
        }
    }
    // How far beyond each end the points reach along the curve's direction there.
    double need[2] = {0, 0};
    gp_Pnt endPoint[2] = {curve->Value(c0), curve->Value(c1)};
    gp_Vec endTangent[2];
    {
        gp_Pnt tmp;
        gp_Vec d;
        curve->D1(c0, tmp, d);
        endTangent[0] = d.Magnitude() > 1e-12 ? -d / d.Magnitude() : gp_Vec();
        curve->D1(c1, tmp, d);
        endTangent[1] = d.Magnitude() > 1e-12 ? d / d.Magnitude() : gp_Vec();
    }
    for (const gp_Pnt& p : points) {
        // A point beyond an end has no orthogonal projection on the curve (or a far one): the end point is then its nearest.
        GeomAPI_ProjectPointOnCurve projection(p, curve, c0, c1);
        const double interior = projection.NbPoints() > 0 ? projection.LowerDistance() : 1e300;
        for (int e = 0; e < 2; ++e) {
            if (endTangent[e].Magnitude() < 0.5)
                continue;
            const double dEnd = p.Distance(endPoint[e]);
            if (dEnd > interior - 1e-7 * scale)
                continue;
            const double along = gp_Vec(endPoint[e], p).Dot(endTangent[e]);
            need[e] = std::max(need[e], along);
        }
    }
    if (std::getenv("XT_DEBUG_CS")) std::fprintf(stderr, "spine %lld need %g / %g length %g\n", (long long)spineIndex, need[0], need[1], length);
    bool changed = false;
    for (int e = 0; e < 2; ++e) {
        if (need[e] < 1e-3 * scale)
            continue;
        const double total = std::min(need[e] * 1.1 + 1e-3 * scale, 0.5 * length + 5e-3 * scale);
        const int steps = std::clamp(static_cast<int>(std::ceil(total / (0.01 * length + 1e-9))), 4, 60);
        const double h = total / steps;
        const Handle(ShapeAnalysis_Surface) a0 = new ShapeAnalysis_Surface(s0.surface), a1 = new ShapeAnalysis_Surface(s1.surface);
        std::vector<gp_Pnt> ext{endPoint[e]};
        try {
            gp_Pnt2d uv0 = a0->ValueOfUV(endPoint[e], 1e-6 * scale), uv1 = a1->ValueOfUV(endPoint[e], 1e-6 * scale);
            gp_Vec tangent = endTangent[e];
            for (int k = 1; k <= steps; ++k) {
                const gp_Pnt predicted = ext.back().Translated(tangent * h);
                gp_Pnt exact;
                if (!exactChartPoint(s0.surface, s1.surface, predicted, tangent * h, uv0, uv1, exact, 1e-9 * scale))
                    break;
                gp_Vec step(ext.back(), exact);
                if (step.Magnitude() < 0.2 * h)
                    break;
                tangent = step / step.Magnitude();
                ext.push_back(exact);
            }
        } catch (const Standard_Failure&) {
        }
        if (ext.size() < 3)
            continue;
        try {
            Handle(NCollection_HArray1<gp_Pnt>) arr = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(ext.size()));
            for (size_t i = 0; i < ext.size(); ++i)
                arr->SetValue(static_cast<int>(i) + 1, ext[i]);
            GeomAPI_Interpolate fit(arr, false, Precision::Confusion());
            fit.Perform();
            if (!fit.IsDone())
                continue;
            Handle(Geom_BSplineCurve) piece = fit.Curve();
            if (e == 0)
                piece->Reverse();
            Handle(Geom_BSplineCurve) base = Handle(Geom_BSplineCurve)::DownCast(curve);
            if (base.IsNull())
                base = GeomConvert::CurveToBSplineCurve(curve);
            GeomConvert_CompCurveToBSplineCurve joiner(e == 0 ? piece : base);
            if (!joiner.Add(e == 0 ? base : piece, 1e-4 * scale, true))
                continue;
            curve = joiner.BSplineCurve();
            changed = true;
            ++res.stats.facesSkippedBySurface["  spine-extended"];
        } catch (const Standard_Failure&) {
        }
    }
    if (changed) {
        m_curves[spineIndex].curve = curve;
        if (std::getenv("XT_DEBUG_CS"))
            std::fprintf(stderr, "spine %lld extended by %g / %g (length %g)\n", (long long)spineIndex, need[0], need[1], length);
    }
    return changed;
}

Handle(Geom_BSplineCurve) Builder::contactCurveFromSpine(const Node& inter) {
    const int64_t s0 = ptr(inter, "INTERSECTION", "surface", 0), s1 = ptr(inter, "INTERSECTION", "surface", 1);
    const Node* bound = node(s0);
    int64_t supportId = s1;
    if (!bound || nameOf(*bound) != "BLEND_BOUND") {
        bound = node(s1);
        supportId = s0;
    }
    if (!bound || nameOf(*bound) != "BLEND_BOUND")
        return {};
    const Node* blend = node(ptr(*bound, "BLEND_BOUND", "blend"));
    if (!blend || nameOf(*blend) != "BLENDED_EDGE" || num(*blend, "BLENDED_EDGE", "blend_type") != 'R')
        return {};
    const int side = ptr(*blend, "BLENDED_EDGE", "surface", 0) == supportId ? 0 : (ptr(*blend, "BLENDED_EDGE", "surface", 1) == supportId ? 1 : -1);
    if (side < 0)
        return {};
    const double radius = std::abs(num(*blend, "BLENDED_EDGE", "range", side)) * scale;
    if (!std::isfinite(radius) || radius < 1e-6 * scale)
        return {};
    const SurfaceInfo support = surfaceFor(supportId); // copy: the map may rehash
    const bool dbgCs = std::getenv("XT_DEBUG_CS") != nullptr;
    if (!support.ok || support.kind == "BLENDED_EDGE" || support.kind == "BLEND_BOUND") {
        if (dbgCs) std::fprintf(stderr, "cs %lld: support not usable (%s ok=%d)\n", (long long)inter.index, support.kind.c_str(), (int)support.ok);
        return {};
    }
    const CurveInfo& pre = curveFor(ptr(*blend, "BLENDED_EDGE", "spine"));
    if (!pre.ok || pre.curve.IsNull() || pre.curve->IsPeriodic()) {
        if (dbgCs) std::fprintf(stderr, "cs %lld: spine not usable ok=%d periodic=%d\n", (long long)inter.index, (int)pre.ok, pre.curve.IsNull() ? -1 : (int)pre.curve->IsPeriodic());
        return {};
    }
    const Node* chart = node(ptr(inter, "INTERSECTION", "chart"));
    const Slot& hv = sl("CHART", "hvec");
    if (!chart || !hv.ok())
        return {};
    const size_t count = chart->valueCount > hv.offset ? (chart->valueCount - hv.offset) / 3 : 0;
    if (count < 2)
        return {};
    const auto chartPoint = [&](size_t i) {
        return gp_Pnt(m.value(*chart, hv, i * 3) * scale, m.value(*chart, hv, i * 3 + 1) * scale, m.value(*chart, hv, i * 3 + 2) * scale);
    };
    correctSpineAgainstSupports(*blend);
    {
        std::vector<gp_Pnt> chartPoints;
        for (size_t i = 0; i < count; ++i)
            chartPoints.push_back(chartPoint(i));
        extendSpine(ptr(*blend, "BLENDED_EDGE", "spine"), chartPoints);
    }
    const CurveInfo spine = curveFor(ptr(*blend, "BLENDED_EDGE", "spine")); // copy, possibly extended
    // The contact point lies on the circular section normal to the spine, so its nearest point on the spine is the ball centre.
    std::vector<double> t(count);
    const double tolerance = 5e-4 * scale;
    for (size_t i = 0; i < count; ++i) {
        const gp_Pnt p = chartPoint(i);
        GeomAPI_ProjectPointOnCurve projection(p, spine.curve);
        if (projection.NbPoints() == 0 || std::abs(projection.LowerDistance() - radius) > tolerance) {
            if (dbgCs) {
                double lo = 1e300, hi = 0;
                for (int k = 0; k <= 40; ++k) {
                    const gp_Pnt q = spine.curve->Value(spine.curve->FirstParameter() + (spine.curve->LastParameter() - spine.curve->FirstParameter()) * k / 40);
                    GeomAPI_ProjectPointOnSurf ps(q, support.surface);
                    if (ps.NbPoints() > 0) { lo = std::min(lo, ps.LowerDistance()); hi = std::max(hi, ps.LowerDistance()); }
                    if (ps.NbPoints() > 0 && std::getenv("XT_DEBUG_CS2")) std::fprintf(stderr, "  spine k=%d/40 distance to support %g\n", k, ps.LowerDistance());
                }
                if (const Node* sn = node(ptr(*blend, "BLENDED_EDGE", "spine"))) {
                    for (int which = 0; which < 2; ++which) {
                        const SurfaceInfo ss = surfaceFor(ptr(*sn, "INTERSECTION", "surface", which));
                        double worst = 0;
                        for (int k = 0; k <= 40 && ss.ok; ++k) {
                            const gp_Pnt q = spine.curve->Value(spine.curve->FirstParameter() + (spine.curve->LastParameter() - spine.curve->FirstParameter()) * k / 40);
                            GeomAPI_ProjectPointOnSurf ps(q, ss.surface);
                            if (ps.NbPoints() > 0) worst = std::max(worst, ps.LowerDistance());
                        }
                        std::fprintf(stderr, "cs %lld: spine surface %d is %s ok=%d, worst spine deviation %g\n", (long long)inter.index, which, ss.kind.c_str(), (int)ss.ok, worst);
                    }
                }
                {
                    // Independent check: the true ball centre of this contact point is P +- r * (normal of the support at P).
                    GeomAPI_ProjectPointOnSurf onSup(p, support.surface);
                    if (onSup.NbPoints() > 0) {
                        double uu, vv;
                        onSup.LowerDistanceParameters(uu, vv);
                        GeomLProp_SLProps props(support.surface, uu, vv, 1, 1e-9);
                        if (props.IsNormalDefined()) {
                            const gp_Vec nrm(props.Normal().XYZ());
                            for (int sg = -1; sg <= 1; sg += 2) {
                                const gp_Pnt centre = p.Translated(nrm * (sg * radius));
                                GeomAPI_ProjectPointOnCurve toSpine(centre, spine.curve);
                                std::fprintf(stderr, "cs %lld: true centre side %d is %g from the spine curve\n", (long long)inter.index, sg, toSpine.NbPoints() ? toSpine.LowerDistance() : -1.0);
                            }
                        }
                    }
                }
                GeomAPI_ProjectPointOnSurf pp(p, support.surface);
                std::fprintf(stderr, "cs %lld: spine distance to support %g..%g; chart point distance to support %g\n", (long long)inter.index, lo, hi, pp.NbPoints() ? pp.LowerDistance() : -1.0);
            }
            if (dbgCs) std::fprintf(stderr, "cs %lld: chart point %zu distance to spine %g vs radius %g (to ends %g %g, spine length ends param %g..%g, found param %g)\n", (long long)inter.index, i, projection.NbPoints() ? projection.LowerDistance() : -1.0, radius, p.Distance(spine.curve->Value(spine.curve->FirstParameter())), p.Distance(spine.curve->Value(spine.curve->LastParameter())), spine.curve->FirstParameter(), spine.curve->LastParameter(), projection.NbPoints() ? projection.LowerDistanceParameter() : -1.0);
            return {};
        }
        t[i] = projection.LowerDistanceParameter();
    }
    const double tFirst = t.front(), tLast = t.back();
    if (!(std::abs(tLast - tFirst) > 1e-9))
        return {};
    const size_t samples = std::clamp<size_t>(count * 3, 24, 240);
    const Handle(ShapeAnalysis_Surface) analysis = new ShapeAnalysis_Surface(support.surface);
    Handle(NCollection_HArray1<gp_Pnt>) points = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(samples));
    gp_Pnt2d uv;
    try {
        for (size_t j = 0; j < samples; ++j) {
            const gp_Pnt centre = spine.curve->Value(tFirst + (tLast - tFirst) * static_cast<double>(j) / static_cast<double>(samples - 1));
            uv = j == 0 ? analysis->ValueOfUV(chartPoint(0), 1e-6 * scale) : analysis->NextValueOfUV(uv, centre, 1e-6 * scale, 1e3 * 1e-6 * scale);
            gp_Pnt foot = support.surface->Value(uv.X(), uv.Y());
            if (std::abs(foot.Distance(centre) - radius) > tolerance) {
                // The tracked solution can sit on another branch; fall back to the global projection once.
                uv = analysis->ValueOfUV(centre, 1e-6 * scale);
                foot = support.surface->Value(uv.X(), uv.Y());
                if (std::abs(foot.Distance(centre) - radius) > tolerance) {
                    if (dbgCs) std::fprintf(stderr, "cs %lld: foot at sample %zu distance %g vs radius %g\n", (long long)inter.index, j, foot.Distance(centre), radius);
                    return {};
                }
            }
            points->SetValue(static_cast<int>(j) + 1, j == 0 ? chartPoint(0) : (j + 1 == samples ? chartPoint(count - 1) : foot));
        }
        GeomAPI_Interpolate fit(points, false, Precision::Confusion());
        fit.Perform();
        if (!fit.IsDone())
            return {};
        return fit.Curve();
    } catch (const Standard_Failure&) {
        return {};
    }
}

Handle(Geom_BSplineCurve) Builder::rebuildBlendContact(const Node& intersection, const gp_Pnt& start, const gp_Pnt& end) {
    const int64_t s0 = ptr(intersection, "INTERSECTION", "surface", 0);
    const int64_t s1 = ptr(intersection, "INTERSECTION", "surface", 1);
    const Node* bound = node(s0);
    int64_t supportId = s1;
    if (!bound || nameOf(*bound) != "BLEND_BOUND") {
        bound = node(s1);
        supportId = s0;
    }
    const Node* support = node(supportId);
    if (!bound || nameOf(*bound) != "BLEND_BOUND" || !support || nameOf(*support) != "SPUN_SURF")
        return {};
    const Node* blend = node(ptr(*bound, "BLEND_BOUND", "blend"));
    if (!blend || nameOf(*blend) != "BLENDED_EDGE" || num(*blend, "BLENDED_EDGE", "blend_type") != 'R')
        return {};
    const int side = ptr(*blend, "BLENDED_EDGE", "surface", 0) == supportId ? 0 : 1;
    if (ptr(*blend, "BLENDED_EDGE", "surface", side) != supportId)
        return {};
    const Node* spine = node(ptr(*blend, "BLENDED_EDGE", "spine"));
    if (!spine || nameOf(*spine) != "INTERSECTION")
        return {};
    int64_t offsetId = ptr(*spine, "INTERSECTION", "surface", 0);
    int64_t coneId = ptr(*spine, "INTERSECTION", "surface", 1);
    const auto isOffsetOfSupport = [&](int64_t id) {
        const Node* offset = node(id);
        return offset && nameOf(*offset) == "OFFSET_SURF" && ptr(*offset, "OFFSET_SURF", "surface") == supportId;
    };
    if (!isOffsetOfSupport(offsetId))
        std::swap(offsetId, coneId);
    const Node* coneNode = node(coneId);
    const int64_t otherId = ptr(*blend, "BLENDED_EDGE", "surface", 1 - side);
    const Node* otherNode = node(otherId);
    if (!isOffsetOfSupport(offsetId) || !coneNode || nameOf(*coneNode) != "CONE"
        || !otherNode || nameOf(*otherNode) != "CONE")
        return {};

    try {
        const auto revolution = Handle(Geom_SurfaceOfRevolution)::DownCast(surfaceFor(supportId).surface);
        const auto spineCone = Handle(Geom_ConicalSurface)::DownCast(surfaceFor(coneId).surface);
        const auto otherCone = Handle(Geom_ConicalSurface)::DownCast(surfaceFor(otherId).surface);
        const Handle(Geom_Surface) offset = surfaceFor(offsetId).surface;
        if (revolution.IsNull() || spineCone.IsNull() || otherCone.IsNull() || offset.IsNull())
            return {};
        const auto circle = Handle(Geom_Circle)::DownCast(revolution->BasisCurve());
        if (circle.IsNull())
            return {};
        const double radius = std::abs(num(*blend, "BLENDED_EDGE", "range", side)) * scale;
        const double otherRadius = std::abs(num(*blend, "BLENDED_EDGE", "range", 1 - side)) * scale;
        const Node* chart = node(ptr(intersection, "INTERSECTION", "chart"));
        const double budget = std::min({num(*chart, "CHART", "chordal_error") * scale,
                                       3e-3 * scale, 0.05 * start.Distance(end)});
        const double tolerance = 2e-5 * scale;
        if (!std::isfinite(budget) || !(budget > tolerance) || !(radius > tolerance) || !(otherRadius > tolerance))
            return {};
        const auto coneDistance = [](const Geom_ConicalSurface& cone, const gp_Pnt& p) {
            const gp_Vec fromOrigin(cone.Location(), p), axis(cone.Position().Direction());
            const double height = fromOrigin.Dot(axis);
            const double radial = (fromOrigin - axis * height).Magnitude();
            return (radial - cone.RefRadius()) * std::cos(cone.SemiAngle()) - height * std::sin(cone.SemiAngle());
        };
        const auto residual = [&](const gp_Pnt2d& uv) {
            return coneDistance(*spineCone, offset->Value(uv.X(), uv.Y()));
        };
        const auto matches = [&](const gp_Pnt2d& uv, const gp_Pnt& point) {
            const gp_Pnt centre = offset->Value(uv.X(), uv.Y());
            return std::abs(residual(uv)) <= tolerance
                && std::abs(centre.Distance(point) - radius) <= tolerance
                && std::abs(std::abs(coneDistance(*otherCone, centre)) - otherRadius) <= tolerance;
        };
        constexpr int count = 25;
        Handle(NCollection_HArray1<gp_Pnt>) points = new NCollection_HArray1<gp_Pnt>(1, count);
        const gp_Vec span(start, end);
        bool needsCorrection = false;
        for (int i = 1; i <= count; ++i) {
            if (cancelled())
                return {};
            const gp_Pnt seed = start.Translated(span * (static_cast<double>(i - 1) / (count - 1)));
            gp_Pnt2d uv;
            if (!circularRevolutionUV(*revolution, *circle, seed, budget, uv))
                return {};
            const bool endpoint = i == 1 || i == count;
            if (!endpoint) {
                // Solve the spine-cone condition on the offset of the contact surface.
                // The chart chord seeds the branch and bounds displacement.
                constexpr double step = 1e-5;
                for (int iteration = 0; iteration < 12; ++iteration) {
                    const double r = residual(uv);
                    if (std::abs(r) <= 0.1 * tolerance)
                        break;
                    const double du = (residual(gp_Pnt2d(uv.X() + step, uv.Y()))
                                     - residual(gp_Pnt2d(uv.X() - step, uv.Y()))) / (2 * step);
                    const double dv = (residual(gp_Pnt2d(uv.X(), uv.Y() + step))
                                     - residual(gp_Pnt2d(uv.X(), uv.Y() - step))) / (2 * step);
                    const double gradient2 = du * du + dv * dv;
                    if (!(gradient2 > Precision::Confusion()))
                        return {};
                    uv.SetCoord(uv.X() - r * du / gradient2, uv.Y() - r * dv / gradient2);
                }
            }
            const gp_Pnt onSupport = revolution->Value(uv.X(), uv.Y());
            const gp_Pnt point = endpoint ? seed : onSupport;
            if (onSupport.Distance(seed) > (endpoint ? tolerance : budget) || !matches(uv, point))
                return {};
            needsCorrection = needsCorrection || point.Distance(seed) > 5e-4 * scale;
            points->SetValue(i, point);
        }
        if (!needsCorrection)
            return {};
        GeomAPI_Interpolate fit(points, false, Precision::Confusion());
        fit.Perform();
        if (!fit.IsDone())
            return {};
        const auto fitted = fit.Curve();
        const double first = fitted->FirstParameter(), last = fitted->LastParameter();
        for (int i = 0; i <= 2 * count; ++i) {
            const gp_Pnt p = fitted->Value(first + (last - first) * i / (2 * count));
            gp_Pnt2d uv;
            if (!circularRevolutionUV(*revolution, *circle, p, tolerance, uv) || !matches(uv, p))
                return {};
        }
        return fitted;
    } catch (const Standard_Failure&) {
        return {};
    }
}

Handle(Geom_BSplineCurve) Builder::rebuildSplineBlendRail(const Node& intersection, const Handle(Geom_Curve)& seed,
    double first, double last, const gp_Pnt& start, const gp_Pnt& end) {
    int64_t splineId = ptr(intersection, "INTERSECTION", "surface", 0);
    int64_t limitId = ptr(intersection, "INTERSECTION", "surface", 1);
    const Node* limit = node(limitId);
    if (!limit || nameOf(*limit) != "BLEND_BOUND")
        std::swap(splineId, limitId);
    limit = node(limitId);
    const Node* splineNode = node(splineId);
    if (!limit || nameOf(*limit) != "BLEND_BOUND" || !splineNode || nameOf(*splineNode) != "B_SURFACE")
        return {};
    const Node* blend = node(ptr(*limit, "BLEND_BOUND", "blend"));
    const double side = num(*limit, "BLEND_BOUND", "boundary");
    const Node* chart = node(ptr(intersection, "INTERSECTION", "chart"));
    if (!blend || !chart || (side != 0 && side != 1)
        || ptr(*blend, "BLENDED_EDGE", "surface", side == 0 ? 1 : 0) != splineId)
        return {};
    const double budget = std::min(3e-3 * scale, num(*chart, "CHART", "chordal_error") * scale);
    const double tolerance = kBlendContactTolerance * scale;
    if (!(budget > 5e-4 * scale))
        return {};
    try {
        const auto spline = Handle(Geom_BSplineSurface)::DownCast(surfaceFor(splineId).surface);
        if (spline.IsNull() || spline->IsUClosed() || spline->IsVClosed())
            return {};
        const Handle(ShapeAnalysis_Surface) analysis = new ShapeAnalysis_Surface(spline);
        const auto pointAt = [&](const gp_Pnt2d& uv) { return spline->Value(uv.X(), uv.Y()); };
        const auto onSpline = [&](const gp_Pnt& p) { return pointAt(analysis->ValueOfUV(p, tolerance)); };
        if (start.Distance(onSpline(start)) > tolerance || end.Distance(onSpline(end)) > tolerance)
            return {};
        bool needsRefit = false;
        for (int i = 1; i < 8; ++i) {
            const gp_Pnt p = seed->Value(first + (last - first) * i / 8);
            const double gap = p.Distance(onSpline(p));
            if (gap > budget)
                return {};
            needsRefit |= gap > 2.5e-4 * scale;
        }
        if (!needsRefit)
            return {};

        // The chart seeds the branch. Its interpolant can leave the support
        // between chart points, especially on a contact with a curved spline.
        constexpr int count = 49;
        Handle(NCollection_HArray1<gp_Pnt>) samples = new NCollection_HArray1<gp_Pnt>(1, count);
        samples->SetValue(1, start);
        samples->SetValue(count, end);
        gp_Pnt2d uv = analysis->ValueOfUV(start, tolerance);
        for (int i = 2; i < count; ++i) {
            if (cancelled())
                return {};
            const gp_Pnt seedPoint = seed->Value(first + (last - first) * (i - 1) / (count - 1));
            uv = analysis->NextValueOfUV(uv, seedPoint, tolerance, budget);
            const gp_Pnt corrected = pointAt(uv);
            if (corrected.Distance(seedPoint) > budget)
                return {};
            samples->SetValue(i, corrected);
        }
        GeomAPI_Interpolate fit(samples, false, Precision::Confusion());
        fit.Perform();
        if (!fit.IsDone())
            return {};
        const auto curve = fit.Curve();
        const double lo = curve->FirstParameter(), hi = curve->LastParameter();
        uv = analysis->ValueOfUV(start, tolerance);
        for (int i = 1; i < 2 * count - 1; ++i) {
            const gp_Pnt p = curve->Value(lo + (hi - lo) * i / (2 * (count - 1)));
            uv = analysis->NextValueOfUV(uv, p, tolerance, budget);
            if (p.Distance(pointAt(uv)) > tolerance)
                return {};
            GeomAPI_ProjectPointOnCurve branch(p, seed, first, last);
            if (branch.NbPoints() == 0 || branch.LowerDistance() > budget)
                return {};
        }
        return curve;
    } catch (const Standard_Failure&) {
        return {};
    }
}

Handle(Geom_BSplineCurve) Builder::rebuildPlaneSplineIntersection(const Node& intersection,
    const Handle(Geom_Curve)& seedCurve, double first, double last, const gp_Pnt& start, const gp_Pnt& end, bool spine) {
    const auto rejected = [&](const char* reason, int sample, double gap) {
        if (spine && m_trace)
            std::fprintf(stderr, "XT blend spine intersection=%lld rejected=%s sample=%d gap=%.6g\n",
                         (long long)intersection.index, reason, sample, gap);
        return Handle(Geom_BSplineCurve)();
    };
    int64_t planeId = ptr(intersection, "INTERSECTION", "surface", 0);
    int64_t splineId = ptr(intersection, "INTERSECTION", "surface", 1);
    const Node* planeNode = node(planeId);
    if (!planeNode || nameOf(*planeNode) != "PLANE")
        std::swap(planeId, splineId);
    planeNode = node(planeId);
    const Node* splineNode = node(splineId);
    const Node* chart = node(ptr(intersection, "INTERSECTION", "chart"));
    if (!planeNode || nameOf(*planeNode) != "PLANE" || !splineNode
        || (nameOf(*splineNode) != "B_SURFACE" && !isSplineOffset(splineId)) || !chart)
        return {};
    const double budget = std::min({num(*chart, "CHART", "chordal_error") * scale,
                                    3e-3 * scale, std::max(1.5e-3 * scale, 0.05 * start.Distance(end))});
    if (!(budget > (spine ? kBlendContactTolerance : 5e-4) * scale) || !std::isfinite(budget))
        return rejected("chart-budget", 0, budget);

    try {
        const auto plane = Handle(Geom_Plane)::DownCast(surfaceFor(planeId).surface);
        const Handle(Geom_Surface) spline = surfaceFor(splineId).surface;
        if (plane.IsNull() || spline.IsNull())
            return {};
        const gp_Vec normal(plane->Position().Direction());
        const double tolerance = 2e-5 * scale;
        const auto planeGap = [&](const gp_Pnt& p) { return gp_Vec(plane->Location(), p).Dot(normal); };
        Handle(ShapeAnalysis_Surface) analysis = new ShapeAnalysis_Surface(spline);
        const int count = spine ? 97 : 25;
        Handle(NCollection_HArray1<gp_Pnt>) points = new NCollection_HArray1<gp_Pnt>(1, count);
        const gp_Vec startCorrection(seedCurve->Value(first), start);
        const gp_Vec endCorrection(seedCurve->Value(last), end);
        gp_Pnt2d uv;
        bool changed = false;
        for (int i = 1; i <= count; ++i) {
            if (cancelled())
                return {};
            const bool endpoint = i == 1 || i == count;
            const double fraction = static_cast<double>(i - 1) / (count - 1);
            const gp_Pnt chartPoint = seedCurve->Value(first + (last - first) * fraction);
            const gp_Pnt seed = i == 1 ? start : i == count ? end
                : chartPoint.Translated(startCorrection * (1.0 - fraction) + endCorrection * fraction);
            uv = i == 1 ? analysis->ValueOfUV(seed, tolerance)
                        : analysis->NextValueOfUV(uv, seed, tolerance, budget);
            gp_Pnt p = spline->Value(uv.X(), uv.Y());
            if (!endpoint || spine) {
                for (int iteration = 0; iteration < 12; ++iteration) {
                    gp_Vec du, dv;
                    spline->D1(uv.X(), uv.Y(), p, du, dv);
                    const double gap = planeGap(p);
                    if (std::abs(gap) <= 0.1 * tolerance)
                        break;
                    // Project the plane normal into the spline tangent plane.
                    const double uu = du.Dot(du), vv = dv.Dot(dv), uvDot = du.Dot(dv);
                    const double determinant = uu * vv - uvDot * uvDot;
                    if (!(determinant > Precision::Confusion()))
                        return rejected("singular-spline", i, determinant);
                    const double a = du.Dot(normal), b = dv.Dot(normal);
                    const double uStep = (vv * a - uvDot * b) / determinant;
                    const double vStep = (uu * b - uvDot * a) / determinant;
                    const double slope = a * uStep + b * vStep;
                    if (!(slope > 1e-8))
                        return rejected("tangent-plane", i, gap);
                    uv.SetCoord(uv.X() - gap * uStep / slope, uv.Y() - gap * vStep / slope);
                }
                p = spline->Value(uv.X(), uv.Y());
            }
            const bool preserveEndpoint = endpoint && !spine;
            if (!(p.Distance(seed) <= (preserveEndpoint ? tolerance : budget)) || !(p.Distance(chartPoint) <= budget))
                return rejected("chart-distance", i, std::max(p.Distance(seed), p.Distance(chartPoint)));
            const double residual = std::abs(planeGap(preserveEndpoint ? seed : p));
            if (!(residual <= tolerance))
                return rejected("plane-distance", i, residual);
            changed = changed || p.Distance(chartPoint) > 2e-4 * scale;
            points->SetValue(i, preserveEndpoint ? seed : p);
        }
        if (!changed && !spine)
            return {};
        GeomAPI_Interpolate fit(points, false, Precision::Confusion());
        fit.Perform();
        if (!fit.IsDone())
            return {};
        const auto curve = fit.Curve();
        const double a = curve->FirstParameter(), b = curve->LastParameter();
        for (int i = 0; i <= 2 * count; ++i) {
            const gp_Pnt p = curve->Value(a + (b - a) * i / (2 * count));
            uv = i == 0 ? analysis->ValueOfUV(p, tolerance) : analysis->NextValueOfUV(uv, p, tolerance, budget);
            const double residual = std::max(std::abs(planeGap(p)), spline->Value(uv.X(), uv.Y()).Distance(p));
            if (!(residual <= tolerance))
                return rejected("fitted-spine", i, residual);
        }
        return curve;
    } catch (const Standard_Failure&) {
        return {};
    }
}

// A compact cap can encircle a pole in the authored chart even though it is
// entirely regular in a chart with a different axis.
static Handle(Geom_SphericalSurface) sphericalCapChart(
    const Handle(Geom_SphericalSurface)& sphere, const TopoDS_Wire& wire, double tolerance)
{
    if (sphere.IsNull())
        return {};
    const gp_Pnt centre = sphere->Location();
    const double radius = sphere->Radius();
    const gp_Dir axis = sphere->Position().Direction();
    const gp_Dir chartAxis = sphere->Position().XDirection();
    gp_Dir pole = axis;
    TopLoc_Location toPoleFrame;
    gp_Vec first, previous;
    double winding = 0;
    bool started = false;
    for (TopoDS_Iterator it(wire); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Value());
        double lo, hi;
        const auto curve = BRep_Tool::Curve(edge, lo, hi);
        if (curve.IsNull() || !(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi))
            return {};
        if (edge.Orientation() == TopAbs_REVERSED)
            std::swap(lo, hi);
        const gp_Vec start = gp_Vec(centre, curve->Value(lo)) / radius;
        if (!started) {
            if (start.Dot(gp_Vec(axis)) < 0)
                pole.Reverse();
            gp_Trsf changeFrame;
            changeFrame.SetTransformation(gp_Ax3(centre, chartAxis, pole));
            toPoleFrame = TopLoc_Location(changeFrame);
            previous = first = start;
            started = true;
        } else if ((previous - start).Magnitude() * radius > tolerance) {
            return {};
        }
        Bnd_Box box;
        BRepBndLib::AddOptimal(edge.Moved(toPoleFrame), box, false, false);
        // Keep all of the boundary in the cap, including between sampled points.
        if (box.IsVoid() || box.IsOpen() || box.CornerMin().X() <= 0.5 * radius)
            return {};
        for (int i = 1; i <= 16; ++i) {
            const gp_Vec next = gp_Vec(centre, curve->Value(lo + (hi - lo) * (i / 16.0))) / radius;
            winding += gp_Vec(pole).Dot(previous.Crossed(next));
            previous = next;
        }
    }
    if (!started || (previous - first).Magnitude() * radius > tolerance ||
        winding <= Precision::Angular())
        return {};
    return new Geom_SphericalSurface(gp_Ax3(centre, chartAxis, pole.Reversed()), radius);
}

TopoDS_Face Builder::cliffRingFace(const Node& face, const Node& blend) {
    // A cliff-edge blend can bound a complete ring with two closed rails and
    // one curve-less section edge used twice. Qualify that topology up front.
    const double radius = std::abs(num(blend, "BLENDED_EDGE", "range", 0)) * scale;
    const double otherRadius = std::abs(num(blend, "BLENDED_EDGE", "range", 1)) * scale;
    const double budget = kBlendContactTolerance * scale;
    if (!(radius > budget) || !std::isfinite(radius) || !std::isfinite(otherRadius)
        || std::abs(otherRadius - radius) > kXtTolerance * scale
        || num(blend, "BLENDED_EDGE", "thumb_weight", 0) != 1
        || num(blend, "BLENDED_EDGE", "thumb_weight", 1) != 1)
        return {};
    const auto isCliff = [&](int side) {
        const Node* support = node(ptr(blend, "BLENDED_EDGE", "surface", side));
        return support && nameOf(*support) == "BLENDED_EDGE"
            && num(*support, "BLENDED_EDGE", "range", 0) == 0
            && num(*support, "BLENDED_EDGE", "range", 1) == 0;
    };
    if (isCliff(0) == isCliff(1))
        return {};
    const Node* loop = node(ptr(face, "FACE", "loop"));
    if (!loop || ptr(*loop, "LOOP", "next"))
        return {};
    const Node* first = node(ptr(*loop, "LOOP", "fin"));
    if (!first)
        return {};
    const Node* fin = first;
    const Node* seam = nullptr;
    TopoDS_Vertex seamStart, seamEnd;
    struct Rail {
        TopoDS_Edge edge;
        Handle(Geom_Curve) curve;
        Handle(Geom2d_BSplineCurve) trim;
        double first = 0, last = 0;
        double residual = 0;
        int seamEndIndex = -1;
        bool forward = false;
        gp_Pnt2d start, end;
    };
    std::vector<Rail> rails;
    gp_Vec axis;
    for (int i = 0; i < 4; ++i) {
        if (!fin || (i && fin == first))
            return {};
        const Node* edge = node(ptr(*fin, "FIN", "edge"));
        if (!edge)
            return {};
        const bool forward = num(*fin, "FIN", "sense") == '+';
        if (ptr(*edge, "EDGE", "curve")) {
            Rail rail;
            rail.edge = edgeFor(*edge);
            if (rail.edge.IsNull() || !BRep_Tool::IsClosed(rail.edge))
                return {};
            rail.curve = BRep_Tool::Curve(rail.edge, rail.first, rail.last);
            if (rail.curve.IsNull() || !std::isfinite(rail.first) || !std::isfinite(rail.last) || !(rail.last > rail.first))
                return {};
            const auto ellipse = Handle(Geom_Ellipse)::DownCast(rail.curve);
            if (!ellipse.IsNull())
                axis = gp_Vec(ellipse->Axis().Direction());
            rail.forward = forward;
            rails.push_back(std::move(rail));
        } else {
            const Node* trim = node(ptr(*fin, "FIN", "curve"));
            const Node* sp = trim && nameOf(*trim) == "TRIMMED_CURVE"
                ? node(ptr(*trim, "TRIMMED_CURVE", "basis_curve")) : nullptr;
            if ((seam && seam != edge) || !sp || nameOf(*sp) != "SP_CURVE"
                || ptr(*sp, "SP_CURVE", "surface") != blend.index)
                return {};
            seam = edge;
            TopoDS_Vertex& end = forward ? seamEnd : seamStart;
            if (!end.IsNull())
                return {};
            end = vertexFor(ptr(*fin, "FIN", "vertex"));
        }
        fin = node(ptr(*fin, "FIN", "forward"));
    }
    if (fin != first || !seam || seamStart.IsNull() || seamEnd.IsNull()
        || rails.size() != 2 || axis.SquareMagnitude() <= Precision::SquareConfusion())
        return {};
    const double seamTolerance = std::max(kXtTolerance * scale, num(*seam, "EDGE", "tolerance") * scale);
    const gp_Pnt seamPoints[] = { BRep_Tool::Pnt(seamStart), BRep_Tool::Pnt(seamEnd) };
    if (seamPoints[0].Distance(seamPoints[1]) > radius)
        return {};

    const CurveInfo spine = curveFor(ptr(blend, "BLENDED_EDGE", "spine"));
    if (!spine.ok || !spine.curve->IsPeriodic())
        return {};
    const double spineFirst = spine.curve->FirstParameter(), spineLast = spine.curve->LastParameter();
    gp_Pnt centre;
    gp_Vec tangent;
    for (int i = 1; i <= 32; ++i) {
        spine.curve->D1(spineFirst + (spineLast - spineFirst) * i / 32, centre, tangent);
        if (tangent.SquareMagnitude() <= Precision::SquareConfusion()
            || std::abs(tangent.Dot(axis)) > tangent.Magnitude() * 0.5)
            return {};
    }
    spine.curve->D1(spineFirst, centre, tangent);
    const Handle(Geom_Circle) circle = new Geom_Circle(gp_Ax2(centre, gp_Dir(tangent)), radius);
    const TopoDS_Wire path = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(spine.curve).Edge()).Wire();
    const TopoDS_Wire section = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circle).Edge()).Wire();
    BRepOffsetAPI_MakePipeShell pipe(path);
    pipe.SetMode(gp_Dir(axis));
    const double pipeTolerance = budget * 0.01;
    pipe.SetTolerance(pipeTolerance, pipeTolerance, 0.01);
    pipe.SetMaxDegree(5);
    pipe.SetMaxSegments(256);
    pipe.Add(section, false, false);
    pipe.Build();
    if (!pipe.IsDone() || pipe.ErrorOnSurface() > pipeTolerance) {
        trace("cliff-ring-pipe-failed", face.index);
        return {};
    }
    TopExp_Explorer pipeFaces(pipe.Shape(), TopAbs_FACE);
    if (!pipeFaces.More())
        return {};
    const auto surface = Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(TopoDS::Face(pipeFaces.Current())));
    pipeFaces.Next();
    if (pipeFaces.More() || surface.IsNull() || !surface->IsUClosed())
        return {};
    if (!surface->IsVClosed()) {
        const int last = surface->NbVPoles();
        for (int u = 1; u <= surface->NbUPoles(); ++u)
            if (surface->Pole(u, 1).Distance(surface->Pole(u, last)) > pipeTolerance)
                return {};
        for (int u = 1; u <= surface->NbUPoles(); ++u)
            surface->SetPole(u, last, surface->Pole(u, 1), surface->Weight(u, 1));
    }
    if (!surface->IsUPeriodic())
        surface->SetUPeriodic();
    if (!surface->IsVPeriodic())
        surface->SetVPeriodic();
    const double up = surface->UPeriod(), vp = surface->VPeriod();

    auto project = [&](const gp_Pnt& point, double tolerance, gp_Pnt2d& uv) {
        GeomAPI_ProjectPointOnSurf projection(point, surface);
        if (!projection.NbPoints() || projection.LowerDistance() > tolerance)
            return false;
        double u, v;
        projection.LowerDistanceParameters(u, v);
        uv.SetCoord(u, v);
        return true;
    };
    auto unwrap = [&](gp_Pnt2d& uv, const gp_Pnt2d& near) {
        uv.SetCoord(near.X() + std::remainder(uv.X() - near.X(), up),
                    near.Y() + std::remainder(uv.Y() - near.Y(), vp));
    };
    gp_Pnt2d ends[2];
    if (!project(seamPoints[0], seamTolerance, ends[0])
        || !project(seamPoints[1], seamTolerance, ends[1]))
        return {};
    unwrap(ends[1], ends[0]);
    trace("cliff-ring-carrier", face.index);
    constexpr int samples = 129;
    for (Rail& rail : rails) {
        const gp_Pnt start = rail.curve->Value(rail.first);
        const int endIndex = start.Distance(seamPoints[0]) < start.Distance(seamPoints[1]) ? 0 : 1;
        rail.seamEndIndex = endIndex;
        const gp_Pnt2d end = ends[endIndex];
        if (start.Distance(seamPoints[endIndex]) > budget)
            return {};
        Handle(NCollection_HArray1<gp_Pnt2d>) uv = new NCollection_HArray1<gp_Pnt2d>(1, samples);
        Handle(NCollection_HArray1<double>) t = new NCollection_HArray1<double>(1, samples);
        uv->SetValue(1, end);
        t->SetValue(1, rail.first);
        for (int i = 2; i <= samples; ++i) {
            if (cancelled())
                return {};
            const double parameter = rail.first + (rail.last - rail.first) * (i - 1) / (samples - 1);
            gp_Pnt2d point;
            if (!project(rail.curve->Value(parameter), budget, point)) {
                trace("cliff-ring-rail-projection-failed", face.index);
                return {};
            }
            unwrap(point, uv->Value(i - 1));
            uv->SetValue(i, point);
            t->SetValue(i, parameter);
        }
        const double winding = uv->Value(samples).Y() - end.Y();
        if (std::abs(std::abs(winding) - vp) > vp * 0.01)
            return {};
        uv->SetValue(samples, gp_Pnt2d(end.X(), end.Y() + std::copysign(vp, winding)));
        if (winding < 0)
            for (int i = 1; i <= samples; ++i)
                uv->ChangeValue(i).SetY(uv->Value(i).Y() + vp);
        Geom2dAPI_Interpolate fit(uv, t, false, Precision::PConfusion());
        fit.Perform();
        if (!fit.IsDone())
            return {};
        rail.trim = fit.Curve();
        rail.start = uv->Value(rail.forward ? 1 : samples);
        rail.end = uv->Value(rail.forward ? samples : 1);
        for (int i = 0; i <= 2 * (samples - 1); ++i) {
            const double parameter = rail.first + (rail.last - rail.first) * i / (2 * (samples - 1));
            const gp_Pnt2d p = rail.trim->Value(parameter);
            rail.residual = std::max(rail.residual, surface->Value(p.X(), p.Y()).Distance(rail.curve->Value(parameter)));
            if (rail.residual > budget) {
                trace("cliff-ring-rail-fit-failed", face.index);
                return {};
            }
        }
    }
    trace("cliff-ring-rails-qualified", face.index);
    const TopoDS_Vertex vertices[] = {seamStart, seamEnd};
    if (rails[0].seamEndIndex == rails[1].seamEndIndex)
        return {};
    for (const Rail& rail : rails) {
        if (!TopExp::FirstVertex(rail.edge).IsSame(vertices[rail.seamEndIndex])) {
            trace("cliff-ring-rail-vertex-mismatch", face.index);
            return {};
        }
    }
    const auto upper = [&](const gp_Pnt2d& uv, int endpoint) {
        return uv.Y() > ends[endpoint].Y() + vp * 0.5;
    };
    const bool firstJoinUpper = upper(rails[0].end, rails[0].seamEndIndex);
    if (firstJoinUpper != upper(rails[1].start, rails[1].seamEndIndex)
        || firstJoinUpper == upper(rails[1].end, rails[1].seamEndIndex))
        return {};

    // Both uses of the seam share one 3D curve. Their pcurves differ by
    // one longitudinal period, including when the section is oblique in UV.
    const Handle(Geom2d_Curve) bottom = GCE2d_MakeSegment(ends[0], ends[1]).Value();
    const Handle(Geom2d_Curve) top = Handle(Geom2d_Curve)::DownCast(bottom->Translated(gp_Vec2d(0, vp)));
    const double seamFirst = bottom->FirstParameter(), seamLast = bottom->LastParameter();
    Handle(NCollection_HArray1<gp_Pnt>) sectionPoints = new NCollection_HArray1<gp_Pnt>(1, 9);
    Handle(NCollection_HArray1<double>) sectionParams = new NCollection_HArray1<double>(1, 9);
    for (int i = 1; i <= 9; ++i) {
        const double t = seamFirst + (seamLast - seamFirst) * (i - 1) / 8;
        const gp_Pnt2d uv = bottom->Value(t);
        sectionPoints->SetValue(i, surface->Value(uv.X(), uv.Y()));
        sectionParams->SetValue(i, t);
    }
    GeomAPI_Interpolate sectionFit(sectionPoints, sectionParams, false, Precision::Confusion());
    sectionFit.Perform();
    if (!sectionFit.IsDone())
        return {};
    // Preserve the source FIN orientation on the returned face. OCCT wants
    // its forward face wire wound positively in the carrier's chart.
    double twiceArea = 0;
    auto areaTerm = [&](const gp_Pnt2d& a, const gp_Pnt2d& b) {
        twiceArea += a.X() * b.Y() - b.X() * a.Y();
    };
    for (const Rail& rail : rails) {
        gp_Pnt2d previous = rail.start;
        for (int i = 1; i <= samples - 1; ++i) {
            const double fraction = i / static_cast<double>(samples - 1);
            const double t = rail.first + (rail.last - rail.first) * (rail.forward ? fraction : 1 - fraction);
            const gp_Pnt2d next = rail.trim->Value(t);
            areaTerm(previous, next);
            previous = next;
        }
    }
    areaTerm(rails[0].end, rails[1].start);
    areaTerm(rails[1].end, rails[0].start);
    if (std::abs(twiceArea) <= Precision::PConfusion())
        return {};
    const bool reverse = twiceArea < 0;
    const bool firstSeamForward = rails[0].seamEndIndex == 0;
    const bool forwardOnUpper = firstJoinUpper == (firstSeamForward != reverse);

    auto build = [&](const TopoDS_Edge& firstRail, const TopoDS_Edge& secondRail, const TopoDS_Edge& sectionEdge) {
        TopoDS_Face candidate;
        bb.MakeFace(candidate, surface, TopLoc_Location(), kXtTolerance * scale);
        const TopoDS_Edge railEdges[] = {firstRail, secondRail};
        for (int i = 0; i < 2; ++i) {
            bb.UpdateEdge(railEdges[i], rails[i].trim, candidate,
                          std::max(BRep_Tool::Tolerance(railEdges[i]), rails[i].residual + kXtTolerance * scale));
            bb.Range(railEdges[i], candidate, rails[i].first, rails[i].last);
        }
        bb.UpdateEdge(sectionEdge, forwardOnUpper ? top : bottom, forwardOnUpper ? bottom : top, candidate, seamTolerance);
        bb.Range(sectionEdge, candidate, seamFirst, seamLast);
        TopoDS_Wire wire;
        bb.MakeWire(wire);
        const TopoDS_Shape ordered[] = {
            firstRail.Oriented(rails[0].forward ? TopAbs_FORWARD : TopAbs_REVERSED),
            sectionEdge.Oriented(firstSeamForward ? TopAbs_FORWARD : TopAbs_REVERSED),
            secondRail.Oriented(rails[1].forward ? TopAbs_FORWARD : TopAbs_REVERSED),
            sectionEdge.Oriented(firstSeamForward ? TopAbs_REVERSED : TopAbs_FORWARD),
        };
        for (int i = 0; i < 4; ++i)
            bb.Add(wire, reverse ? ordered[3 - i].Reversed() : ordered[i]);
        wire.Closed(true);
        bb.Add(candidate, wire);
        return candidate;
    };
    auto makeEdge = [&](const Handle(Geom_Curve)& curve, double lo, double hi, const TopoDS_Vertex& a,
                        const TopoDS_Vertex& b, double tolerance) {
        TopoDS_Edge edge;
        bb.MakeEdge(edge, curve, TopLoc_Location(), tolerance);
        bb.Add(edge, a.Oriented(TopAbs_FORWARD));
        bb.Add(edge, b.Oriented(TopAbs_REVERSED));
        bb.Range(edge, lo, hi);
        return edge;
    };
    // Validate isolated copies before adding any representation to a shared
    // edge. A failed candidate must leave adjacent faces untouched.
    TopoDS_Vertex stagedVertices[2];
    for (int i = 0; i < 2; ++i)
        bb.MakeVertex(stagedVertices[i], seamPoints[i], std::max(BRep_Tool::Tolerance(vertices[i]), seamTolerance));
    TopoDS_Edge stagedRails[2];
    for (int i = 0; i < 2; ++i) {
        const auto& rail = rails[i];
        const auto& vertex = stagedVertices[rail.seamEndIndex];
        stagedRails[i] = makeEdge(rail.curve, rail.first, rail.last, vertex, vertex, BRep_Tool::Tolerance(rail.edge));
    }
    const auto stagedSeam = makeEdge(sectionFit.Curve(), seamFirst, seamLast, stagedVertices[0], stagedVertices[1], seamTolerance);
    TopoDS_Face candidate = build(stagedRails[0], stagedRails[1], stagedSeam);
    BRepLib::SameParameter(candidate, kXtTolerance * scale, false);
    if (!BRepCheck_Analyzer(candidate).IsValid()) {
        trace("cliff-ring-invalid-wire", face.index);
        return {};
    }
    const auto sectionEdge = makeEdge(sectionFit.Curve(), seamFirst, seamLast, seamStart, seamEnd, seamTolerance);
    struct SplitTrim {
        TopoDS_Edge edge;
        Handle(Geom2d_BSplineCurve) curve;
        double first, last, tolerance;
    };
    std::vector<SplitTrim> splitTrims;
    // A neighbour can split a ring at its own periodic cut. The shared
    // replacement edges must inherit this face's chart, not reproject
    // independently and choose a different periodic branch.
    for (const Rail& rail : rails) {
        const TopoDS_Shape replacement = m_reshape->Apply(rail.edge);
        if (replacement.IsSame(rail.edge))
            continue;
        if (replacement.IsNull())
            return {};
        for (TopExp_Explorer ex(replacement, TopAbs_EDGE); ex.More(); ex.Next()) {
            const auto piece = TopoDS::Edge(ex.Current());
            double first, last;
            const auto curve = BRep_Tool::Curve(piece, first, last);
            if (curve.IsNull() || !std::isfinite(first) || !std::isfinite(last) || !(last > first))
                return {};
            const double period = rail.last - rail.first;
            bool matched = false;
            for (double shift : {0.0, period, -period}) {
                if (first + shift < rail.first - Precision::PConfusion()
                    || last + shift > rail.last + Precision::PConfusion())
                    continue;
                double residual = 0;
                for (int i = 0; i <= 32; ++i) {
                    const double t = first + (last - first) * i / 32;
                    const auto uv = rail.trim->Value(t + shift);
                    residual = std::max(residual, curve->Value(t).Distance(surface->Value(uv.X(), uv.Y())));
                }
                if (residual > budget)
                    continue;
                auto trim = rail.trim;
                if (shift != 0) {
                    trim = Handle(Geom2d_BSplineCurve)::DownCast(trim->Copy());
                    NCollection_Array1<double> knots(1, trim->NbKnots());
                    for (int i = 1; i <= knots.Upper(); ++i)
                        knots.SetValue(i, trim->Knot(i) - shift);
                    trim->SetKnots(knots);
                }
                splitTrims.push_back({piece, trim, first, last, residual + kXtTolerance * scale});
                matched = true;
                break;
            }
            if (!matched) {
                trace("cliff-ring-split-trim-mismatch", face.index);
                return {};
            }
        }
    }
    bb.UpdateVertex(seamStart, seamFirst, sectionEdge, BRep_Tool::Tolerance(seamStart));
    bb.UpdateVertex(seamEnd, seamLast, sectionEdge, BRep_Tool::Tolerance(seamEnd));
    candidate = build(rails[0].edge, rails[1].edge, sectionEdge);
    for (const SplitTrim& trim : splitTrims) {
        bb.UpdateEdge(trim.edge, trim.curve, candidate, std::max(BRep_Tool::Tolerance(trim.edge), trim.tolerance));
        bb.Range(trim.edge, candidate, trim.first, trim.last);
        bb.SameParameter(trim.edge, false);
    }
    if (!splitTrims.empty())
        trace("cliff-ring-split-trims", face.index);
    for (const Rail& rail : rails)
        bb.SameParameter(rail.edge, false);
    bb.SameParameter(sectionEdge, false);
    if (reverse)
        candidate.Reverse();
    ++res.stats.facesBuilt;
    ++res.stats.edgesBuilt;
    ++res.stats.tolerantEdgesRebuilt;
    ++res.stats.approximatedGeometry;
    ++res.stats.surfacesBuilt["BLENDED_EDGE"];
    trace("cliff-ring-face", face.index);
    return candidate;
}

TopoDS_Face Builder::faceForImpl(const Node& face, TopAbs_Orientation use) {
    ++res.stats.faces;
    const int64_t surfIndex = ptr(face, "FACE", "surface");
    SurfaceInfo blended; // blends are built per face (they depend on the stretch of spine the face uses)
    const Node* surfNode = node(surfIndex);
    const bool isBlend = surfNode && nameOf(*surfNode) == "BLENDED_EDGE";
    if (isBlend) {
        trace("blend", face.index);
        if (num(*surfNode, "BLENDED_EDGE", "blend_type") == 'E' && std::getenv("XT_TRY_CLIFF_RING")) {
            try {
                const TopoDS_Face cliff = cliffRingFace(face, *surfNode);
                if (!cliff.IsNull())
                    return cliff;
            } catch (const Standard_Failure&) {
                trace("cliff-ring-exception", face.index);
            }
        }
        blended = blendSurfaceForFace(*surfNode, face);
    }
    const SurfaceInfo& si = isBlend ? blended : surfaceFor(surfIndex);
    if (!si.ok) {
        const Node* sn = node(surfIndex);
        trace("unsupported-surface", face.index);
        ++res.stats.facesSkippedBySurface[sn ? nameOf(*sn) : std::string("none")];
        if (isBlend)
            ++res.stats.facesSkippedBySurface["blend-why-" + std::to_string(m_blendWhy)];
        return {};
    }
    // Face normal = natural XT normal when face and surface senses agree; OCCT's natural
    // normal is `flip` times XT's.
    const int xtNormal = signOf(face, "FACE", "sense") * si.senseSign;
    bool faceReversed = (xtNormal * si.flip) < 0;

    TopoDS_Face f;
    Handle(Geom_Surface) faceSurface = si.surface;
    bb.MakeFace(f, faceSurface, TopLoc_Location(), kXtTolerance * scale);

    struct EdgeUse {
        TopoDS_Edge edge;
        const Node* fin;
        PCurve authored;
    };
    const bool authoredChart = si.kind == "PLANE" || si.kind == "CYLINDER";
    const bool authoredSplineOffset = si.kind == "OFFSET_SURF" && isSplineOffset(surfIndex);
    std::unordered_map<const TopoDS_TShape*, int> edgeUse;
    std::vector<EdgeUse> toFix;
    size_t wires = 0;
    TopoDS_Wire lastWire;
    for (const Node* loop : chain(node(ptr(face, "FACE", "loop")), "LOOP", "next")) {
        const Node* start = node(ptr(*loop, "LOOP", "fin"));
        if (!start)
            continue;
        std::vector<const Node*> ring;
        std::unordered_set<int64_t> seen;
        const Node* fin = start;
        for (; fin && seen.insert(fin->index).second; fin = node(ptr(*fin, "FIN", "forward"))) {
            if (cancelled())
                return {};
            ring.push_back(fin);
        }
        if (fin != start) {
            ++res.stats.facesSkippedBySurface["open-or-cyclic-fin-ring"];
            return {};
        }
        // Wires are traversed with the face on the left of the XT normal. The OCCT face is built
        // along its own natural normal; when the two differ the loop direction flips.
        const bool flipWire = (xtNormal * si.flip) < 0;
        TopoDS_Wire wire;
        bb.MakeWire(wire);
        bool wireOk = true;
        std::vector<EdgeUse> items;
        for (const Node* fn : ring) {
            const Node* en = node(ptr(*fn, "FIN", "edge"));
            if (!en) {
                // A fin without an edge belongs to an isolated-vertex loop (or a dummy fin); it bounds nothing.
                trace("fin-without-edge", fn->index);
                continue;
            }
            TopoDS_Edge e = edgeFor(*en);
            if (e.IsNull() && m_microEdges.count(en->index))
                continue; // merged away: its end vertices are one vertex
            if (e.IsNull()) {
                trace("unsupported-edge", en->index);
                wireOk = false;
                break;
            }
            TopAbs_Orientation o = num(*fn, "FIN", "sense") == '+' ? TopAbs_FORWARD : TopAbs_REVERSED;
            if (flipWire)
                o = (o == TopAbs_FORWARD) ? TopAbs_REVERSED : TopAbs_FORWARD;
            e.Orientation(o);
            items.push_back({e, fn, {}});
        }
        if (!wireOk) {
            ++res.stats.facesSkippedBySurface["edge-unsupported"];
            return {};
        }
        if (items.empty())
            continue;
        if (flipWire)
            std::reverse(items.begin(), items.end());
        // Seam detection: the same edge used twice in this face.
        for (auto& it : items) {
            bb.Add(wire, it.edge);
            if (++edgeUse[it.edge.TShape().get()] == 1)
                toFix.push_back(std::move(it));
        }
        wire.Closed(authoredChart ? BRep_Tool::IsClosed(wire) : true);
        if (!wire.Closed()) {
            ++res.stats.openTrimWires;
            trace("open-trim-wire", face.index);
        }
        bb.Add(f, wire);
        lastWire = wire;
        ++wires;
    }
    // A clamped but closed spline chart cannot represent a ring crossing the chart cut
    // as a continuous pcurve. A full ring can be paired with a multi-edge trim.
    if (si.kind == "B_SURFACE" && wires == 2) {
        bool hasRing = false;
        for (TopoDS_Iterator wire(f); wire.More() && !hasRing; wire.Next()) {
            TopoDS_Iterator edge(wire.Value());
            if (edge.More() && BRep_Tool::IsClosed(edge.Value())) {
                edge.Next();
                hasRing = !edge.More();
            }
        }
        if (hasRing) {
            try {
                const auto spline = Handle(Geom_BSplineSurface)::DownCast(faceSurface);
                if (!spline.IsNull() && spline->IsUClosed() && !spline->IsUPeriodic() && !spline->IsVClosed()) {
                    const auto periodic = Handle(Geom_BSplineSurface)::DownCast(spline->Copy());
                    periodic->SetUPeriodic();
                    // Move the seam away from the authored chart boundary before
                    // projecting trims. A seam at that boundary can cause ShapeFix
                    // to introduce an edge that departs from the original wall.
                    const int first = periodic->FirstUKnotIndex();
                    const int last = periodic->LastUKnotIndex();
                    if (last - first > 1)
                        periodic->SetUOrigin(first + (last - first) / 2);
                    double u0, u1, v0, v1;
                    spline->Bounds(u0, u1, v0, v1);
                    bool equivalent = std::abs(periodic->UPeriod() - (u1 - u0)) <= Precision::PConfusion();
                    for (int u = 0; u <= 8 && equivalent; ++u)
                        for (int v = 0; v <= 8 && equivalent; ++v) {
                            const double x = u0 + (u1 - u0) * u / 8, y = v0 + (v1 - v0) * v / 8;
                            equivalent = spline->Value(x, y).Distance(periodic->Value(x, y)) <= kXtTolerance * scale;
                        }
                    if (equivalent) {
                        faceSurface = periodic;
                        bb.UpdateFace(f, faceSurface, TopLoc_Location(), kXtTolerance * scale);
                        trace("periodic-spline-ring-chart", face.index);
                    }
                }
            } catch (const Standard_Failure&) {
            }
        }
    }
    if (si.kind == "B_SURFACE" && wires == 1 && toFix.size() >= 3) {
        const auto original = Handle(Geom_BSplineSurface)::DownCast(faceSurface);
        const char* dualChartFaces = std::getenv("XT_TRY_DUAL_CHART_FACES");
        const bool trialDualChart = dualChartFaces
            && (std::string(",") + dualChartFaces + ",").find("," + std::to_string(face.index) + ",") != std::string::npos;
        if (!original.IsNull() && original->IsUClosed() && (!original->IsVClosed() || trialDualChart)
            && std::none_of(edgeUse.begin(), edgeUse.end(), [](const auto& item) { return item.second > 1; })) {
            try {
                double minUV[2], maxUV[2];
                original->Bounds(minUV[0], maxUV[0], minUV[1], maxUV[1]);
                const bool closed[] = {original->IsUClosed(), original->IsVClosed()};
                auto spline = original;
                bool qualified = true;
                if ((closed[0] && !original->IsUPeriodic()) || (closed[1] && !original->IsVPeriodic())) {
                    spline = Handle(Geom_BSplineSurface)::DownCast(original->Copy());
                    if (closed[0] && !spline->IsUPeriodic())
                        spline->SetUPeriodic();
                    if (closed[1] && !spline->IsVPeriodic())
                        spline->SetVPeriodic();
                    for (int i = 0; i <= 8 && qualified; ++i)
                        for (int j = 0; j <= 8 && qualified; ++j) {
                            const double u = minUV[0] + (maxUV[0] - minUV[0]) * i / 8;
                            const double v = minUV[1] + (maxUV[1] - minUV[1]) * j / 8;
                            qualified = original->Value(u, v).Distance(spline->Value(u, v)) <= kXtTolerance * scale;
                        }
                }
                const double periods[] = {closed[0] ? spline->UPeriod() : 0, closed[1] ? spline->VPeriod() : 0};
                for (int axis = 0; axis < 2; ++axis)
                    if (closed[axis] && std::abs(periods[axis] - (maxUV[axis] - minUV[axis])) > Precision::PConfusion())
                        qualified = false;
                std::array<std::vector<double>, 2> stations;
                const double tolerance = 5e-4 * scale;
                for (const auto& item : toFix) {
                    if (!qualified)
                        break;
                    double first, last;
                    const auto curve = BRep_Tool::Curve(item.edge, first, last);
                    if (curve.IsNull() || !std::isfinite(first) || !std::isfinite(last) || first >= last) {
                        qualified = false;
                        break;
                    }
                    double previous[2]{};
                    for (int i = 0; i <= 16 && qualified; ++i) {
                        if (cancelled())
                            return {};
                        const gp_Pnt point = curve->Value(first + (last - first) * i / 16);
                        GeomAPI_ProjectPointOnSurf projection(point, spline);
                        if (!projection.NbPoints() || projection.LowerDistance() > tolerance) {
                            qualified = false;
                            break;
                        }
                        double uv[2];
                        projection.LowerDistanceParameters(uv[0], uv[1]);
                        for (int axis = 0; axis < 2; ++axis) {
                            if (!closed[axis])
                                continue;
                            const double period = periods[axis];
                            if (i && std::abs(std::remainder(uv[axis] - previous[axis], period)) > period / 8) {
                                qualified = false;
                                break;
                            }
                            previous[axis] = uv[axis];
                            stations[axis].push_back(uv[axis]);
                        }
                    }
                }
                bool crossesCut = false;
                for (int axis = 0; axis < 2 && qualified; ++axis) {
                    if (!closed[axis])
                        continue;
                    auto& samples = stations[axis];
                    std::sort(samples.begin(), samples.end());
                    const double period = periods[axis];
                    const double span = samples.back() - samples.front();
                    crossesCut |= span > 0.8 * period;
                    double gap = period - span;
                    double start = samples.front();
                    for (size_t i = 1; i < samples.size(); ++i) {
                        const double interval = samples[i] - samples[i - 1];
                        if (interval > gap) {
                            gap = interval;
                            start = samples[i];
                        }
                    }
                    if (gap <= 0.25 * period) {
                        qualified = false;
                        break;
                    }
                    const double margin = gap * 0.25;
                    minUV[axis] = start - margin;
                    maxUV[axis] = start + period - gap + margin;
                }
                if (qualified && crossesCut) {
                    // Leave the cuts outside the outer loop in both periodic directions.
                    faceSurface = new Geom_RectangularTrimmedSurface(spline, minUV[0], maxUV[0], minUV[1], maxUV[1]);
                    bb.UpdateFace(f, faceSurface, TopLoc_Location(), kXtTolerance * scale);
                    trace("compact-periodic-spline-chart", face.index);
                }
            } catch (const Standard_Failure&) {
            }
        }
    }
    bool seamFreeSphericalTrim = false;
    bool shiftedRevolution = false;
    if (si.kind == "SPUN_SURF" && wires > 1) {
        // Move the chart cuts away from a compact patch without changing its surface.
        try {
            const auto revolution = Handle(Geom_SurfaceOfRevolution)::DownCast(faceSurface);
            const gp_Ax1 axis = revolution->Axis();
            const auto circle = Handle(Geom_Circle)::DownCast(revolution->BasisCurve());
            std::vector<double> angles, profileParameters;
            const auto addParameters = [&](const gp_Pnt& p) {
                gp_Pnt2d uv;
                if (!circle.IsNull() && circularRevolutionUV(*revolution, *circle, p, 5e-4 * scale, uv)) {
                    angles.push_back(uv.X());
                    profileParameters.push_back(uv.Y());
                    return;
                }
                GeomAPI_ProjectPointOnSurf projection(p, revolution);
                if (projection.NbPoints() > 0 && projection.LowerDistance() <= 5e-4 * scale) {
                    double u, v;
                    projection.LowerDistanceParameters(u, v);
                    angles.push_back(u);
                    profileParameters.push_back(v);
                }
            };
            for (const auto& useEdge : toFix) {
                if (cancelled())
                    return {};
                const TopoDS_Vertex start = TopExp::FirstVertex(useEdge.edge);
                const TopoDS_Vertex end = TopExp::LastVertex(useEdge.edge);
                if (!start.IsNull())
                    addParameters(BRep_Tool::Pnt(start));
                if (!end.IsNull() && !end.IsSame(start))
                    addParameters(BRep_Tool::Pnt(end));
                double first, last;
                const auto curve = BRep_Tool::Curve(useEdge.edge, first, last);
                if (!curve.IsNull() && std::isfinite(first) && std::isfinite(last) && first < last)
                    for (int i = 0; i <= 4; ++i)
                        addParameters(curve->Value(first + (last - first) * i / 4));
            }
            if (angles.size() > 1) {
                const auto cutShift = [](std::vector<double>& params, double period) {
                    std::sort(params.begin(), params.end());
                    double gap = 0, midpoint = 0;
                    for (size_t i = 1; i < params.size(); ++i) {
                        const double between = params[i] - params[i - 1];
                        if (between > gap) {
                            gap = between;
                            midpoint = (params[i] + params[i - 1]) * 0.5;
                        }
                    }
                    return gap > 0.75 * period && params.back() - params.front() > 0.75 * period ? midpoint : 0.0;
                };
                const double uShift = cutShift(angles, revolution->UPeriod());
                Handle(Geom_Curve) basis = revolution->BasisCurve();
                const double vShift = circle.IsNull() ? 0.0 : cutShift(profileParameters, circle->Period());
                if (uShift != 0.0 || vShift != 0.0) {
                    gp_Trsf rotation;
                    if (vShift != 0.0) {
                        rotation.SetRotation(circle->Axis(), vShift);
                        basis = Handle(Geom_Curve)::DownCast(basis->Transformed(rotation));
                    }
                    if (uShift != 0.0) {
                        rotation.SetRotation(axis, uShift);
                        basis = Handle(Geom_Curve)::DownCast(basis->Transformed(rotation));
                    }
                    faceSurface = new Geom_SurfaceOfRevolution(basis, axis);
                    bb.UpdateFace(f, faceSurface, TopLoc_Location(), kXtTolerance * scale);
                    const bool compactU = uShift != 0.0 || angles.back() - angles.front() < 0.25 * revolution->UPeriod();
                    const bool compactV = vShift != 0.0 || !revolution->IsVClosed()
                        || (!circle.IsNull() && profileParameters.back() - profileParameters.front() < 0.25 * circle->Period());
                    shiftedRevolution = compactU && compactV;
                    trace(vShift != 0.0 ? "shifted-revolution-profile-chart" : "shifted-revolution-chart", face.index);
                }
            }
        } catch (const Standard_Failure&) {
        }
    }
    if (si.kind == "SPHERE" && wires > 0) {
        const auto sphere = Handle(Geom_SphericalSurface)::DownCast(faceSurface);
        Handle(Geom_SphericalSurface) chart;
        if (wires == 1)
            chart = sphericalTriangleChart(sphere, lastWire, kXtTolerance * scale);
        const bool triangle = !chart.IsNull();
        if (!triangle) {
            try {
                for (TopoDS_Iterator outer(f); outer.More() && chart.IsNull(); outer.Next()) {
                    const double joinTolerance = (wires == 1 ? 2e-6 : 1e-4) * scale;
                    const auto candidate = sphericalCapChart(sphere, TopoDS::Wire(outer.Value()), joinTolerance);
                    if (candidate.IsNull())
                        continue;
                    // Qualify the rest of the trims in the same regular chart. On this cap
                    // the new pole and the U seam are both outside the boundary.
                    gp_Trsf toCapFrame;
                    const gp_Ax3& axes = candidate->Position();
                    toCapFrame.SetTransformation(gp_Ax3(axes.Location(), axes.Direction(), axes.XDirection().Reversed()));
                    bool compact = true;
                    for (TopoDS_Iterator wire(f); wire.More() && compact; wire.Next()) {
                        Bnd_Box box;
                        BRepBndLib::AddOptimal(wire.Value().Moved(TopLoc_Location(toCapFrame)), box, false, false);
                        compact = !box.IsVoid() && !box.IsOpen() && box.CornerMin().X() > 0.5 * candidate->Radius();
                    }
                    if (compact)
                        chart = candidate;
                }
            } catch (const Standard_Failure&) {
                // A failed bound leaves the face on its ordinary repair path.
            }
        }
        if (!chart.IsNull()) {
            trace(triangle ? "spherical-triangle-chart" : "spherical-cap-chart", face.index);
            faceSurface = chart;
            bb.UpdateFace(f, faceSurface, TopLoc_Location(), kXtTolerance * scale);
            seamFreeSphericalTrim = true;
        }
    }
    if (wires == 0 && ptr(face, "FACE", "loop") != 0) {
        // The face has loops but none with edges (isolated-vertex loops): it is not a full-surface face.
        ++res.stats.facesSkippedBySurface["vertex-loops-only"];
        return {};
    }
    auto retainBoundary = [&] {
        if (!opt.retainDisplayCandidates || !wires ||
            (!seamFreeSphericalTrim && (faceSurface->IsUClosed() || faceSurface->IsVClosed())))
            return;
        TopoDS_Face candidate = f;
        if (faceReversed != (use == TopAbs_REVERSED))
            candidate.Reverse();
        m_displayCandidates.push_back(std::move(candidate));
        trace("display-boundary-candidate", face.index);
    };
    if (wires == 0) {
        // No loops: a full natural-bounds face (sphere, torus...). Infinite surfaces cannot be bounded.
        double u0, u1, v0, v1;
        si.surface->Bounds(u0, u1, v0, v1);
        if (Precision::IsInfinite(u0) || Precision::IsInfinite(u1) || Precision::IsInfinite(v0) || Precision::IsInfinite(v1)) {
            ++res.stats.facesSkippedBySurface["unbounded-no-loops"];
            return {};
        }
        BRepBuilderAPI_MakeFace bounded(si.surface, u0, u1, v0, v1, kXtTolerance * scale);
        f = bounded.Face();
    } else {
        ScopedTimer pcurveTimer(res.stats.seconds["pcurves:" + si.kind]);
        bool hasAuthored = false;
        if (authoredChart || authoredSplineOffset) {
            for (auto& it : toFix) {
                if (cancelled())
                    return {};
                // A seam needs a pair of pcurves with a common range.
                if (edgeUse.at(it.edge.TShape().get()) > 1)
                    continue;
                const Node* source = node(ptr(*it.fin, "FIN", "curve"));
                if (!source && authoredSplineOffset) {
                    const Node* xtEdge = node(ptr(*it.fin, "FIN", "edge"));
                    if (xtEdge)
                        source = node(ptr(*xtEdge, "EDGE", "curve"));
                }
                const Node* trim = source && nameOf(*source) == "TRIMMED_CURVE" ? source : nullptr;
                const Node* sp = trim ? node(ptr(*trim, "TRIMMED_CURVE", "basis_curve")) : source;
                if (!sp || nameOf(*sp) != "SP_CURVE")
                    continue;
                if (ptr(*sp, "SP_CURVE", "surface") == surfIndex)
                    it.authored = authoredPCurve(*sp, trim, si, it.edge);
                if (it.authored.curve.IsNull())
                    ++res.stats.rejectedAuthoredPCurves;
                else
                    hasAuthored = true;
            }
        }
        if (hasAuthored) {
            // OCCT stores pcurves on the shared edge, keyed by surface and location.
            // Give this face its own support handle so its trim cannot replace a neighbour's.
            faceSurface = Handle(Geom_Surface)::DownCast(faceSurface->Copy());
            bb.UpdateFace(f, faceSurface, TopLoc_Location(), kXtTolerance * scale);
        }
        ShapeFix_Edge fixer;
        // One surface analyser per face: building it (a grid search structure for B-splines) is the expensive part.
        const Handle(ShapeAnalysis_Surface) analyser = new ShapeAnalysis_Surface(faceSurface);
        const auto extrusion = Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(faceSurface);
        Handle(Geom_Curve) flatSection;
        Handle(Geom_Curve) section;
        gp_Vec sweep;
        gp_Pnt sectionOrigin;
        if (!extrusion.IsNull() && extrusion->IsUPeriodic()) {
            try {
                section = extrusion->BasisCurve();
                sweep = gp_Vec(extrusion->Direction());
                sectionOrigin = section->Value(section->FirstParameter());
                const Handle(Geom_Plane) projectionPlane = new Geom_Plane(sectionOrigin, extrusion->Direction());
                flatSection = GeomProjLib::ProjectOnPlane(section, projectionPlane, extrusion->Direction(), true);
            } catch (const Standard_Failure&) {
            }
        }
        for (const auto& it : toFix) {
            if (cancelled())
                return {};
            const auto& edge = it.edge;
            const auto* key = edge.TShape().get();
            const bool seam = edgeUse.at(key) > 1;
            if (!it.authored.curve.IsNull()) {
                bb.UpdateEdge(edge, Handle(Geom2d_Curve)(it.authored.curve), f, BRep_Tool::Tolerance(edge));
                bb.Range(edge, f, it.authored.first, it.authored.last);
                bb.SameRange(edge, false);
                bb.SameParameter(edge, false);
                ++res.stats.authoredPCurves;
                trace("authored-pcurve", face.index);
                continue;
            }
            try {
                const auto pc0 = std::chrono::steady_clock::now();
                trace("project-pcurve", face.index);
                ++res.stats.pcurveProjectionAttempts;
                bool attached = false;
                if ((shiftedRevolution || authoredSplineOffset || !flatSection.IsNull())
                    && !seam && !BRep_Tool::Degenerated(edge)) {
                    try {
                        double first, last;
                        const auto curve = BRep_Tool::Curve(edge, first, last);
                        if (!curve.IsNull() && std::isfinite(first) && std::isfinite(last) && first < last) {
                            constexpr int count = 49;
                            const double limit = 4.9e-4 * scale;
                            const auto revolution = Handle(Geom_SurfaceOfRevolution)::DownCast(faceSurface);
                            Handle(Geom_Circle) circle;
                            if (!revolution.IsNull())
                                circle = Handle(Geom_Circle)::DownCast(revolution->BasisCurve());
                            Handle(NCollection_HArray1<gp_Pnt2d>) points = new NCollection_HArray1<gp_Pnt2d>(1, count);
                            Handle(NCollection_HArray1<double>) params = new NCollection_HArray1<double>(1, count);
                            const auto onSupport = [&](const gp_Pnt2d& uv, const gp_Pnt& p) {
                                return std::isfinite(uv.X()) && std::isfinite(uv.Y())
                                    && faceSurface->Value(uv.X(), uv.Y()).Distance(p) <= limit;
                            };
                            gp_Pnt2d uv;
                            bool agrees = true;
                            for (int i = 1; i <= count && agrees; ++i) {
                                const double t = first + (last - first) * (i - 1) / (count - 1);
                                const gp_Pnt p = curve->Value(t);
                                gp_Pnt2d exactUV;
                                if (!flatSection.IsNull()) {
                                    const gp_Pnt flattened = p.Translated(-sweep * gp_Vec(sectionOrigin, p).Dot(sweep));
                                    GeomAPI_ProjectPointOnCurve projection(flattened, flatSection,
                                                                           section->FirstParameter(), section->LastParameter());
                                    if (projection.NbPoints() == 0) {
                                        agrees = false;
                                        break;
                                    }
                                    double u = projection.LowerDistanceParameter();
                                    if (i > 1)
                                        u = uv.X() + std::remainder(u - uv.X(), section->Period());
                                    uv.SetCoord(u, gp_Vec(section->Value(u), p).Dot(sweep));
                                } else if (!circle.IsNull() && circularRevolutionUV(*revolution, *circle, p, limit, exactUV))
                                    uv = exactUV;
                                else
                                    uv = i == 1 ? analyser->ValueOfUV(p, pcurvePrecision)
                                                : analyser->NextValueOfUV(uv, p, pcurvePrecision, limit);
                                agrees = onSupport(uv, p);
                                points->SetValue(i, uv);
                                params->SetValue(i, t);
                            }
                            if (agrees) {
                                Geom2dAPI_Interpolate interpolate(points, params, false, Precision::PConfusion());
                                interpolate.Perform();
                                if (interpolate.IsDone()) {
                                    const Handle(Geom2d_Curve) pc = interpolate.Curve();
                                    for (int i = 1; i < count && agrees; ++i) {
                                        const double t = (params->Value(i) + params->Value(i + 1)) * 0.5;
                                        agrees = onSupport(pc->Value(t), curve->Value(t));
                                    }
                                    if (agrees) {
                                        bb.UpdateEdge(edge, pc, f, BRep_Tool::Tolerance(edge));
                                        bb.Range(edge, f, first, last);
                                        bb.SameParameter(edge, false);
                                        attached = true;
                                        trace(shiftedRevolution ? "continuous-revolution-pcurve" :
                                            authoredSplineOffset ? "continuous-spline-offset-pcurve" : "continuous-extrusion-pcurve",
                                            face.index);
                                    }
                                }
                            }
                        }
                    } catch (const Standard_Failure&) {
                    }
                }
                if (!attached)
                    fixer.FixAddPCurve(edge, f, seam, analyser, pcurvePrecision);
                if (shiftedRevolution && !attached && m_trace)
                    std::fprintf(stderr, "XT revolution projection fallback face=%lld edge=%lld\n",
                                 static_cast<long long>(face.index),
                                 static_cast<long long>(ptr(*it.fin, "FIN", "edge")));
                res.stats.seconds["pc-edge:" + m_edgeKind[key]] += std::chrono::duration<double>(std::chrono::steady_clock::now() - pc0).count();
                if (std::getenv("XT_DEBUG_SLOW")) {
                    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - pc0).count();
                    if (s > 0.2)
                        std::fprintf(stderr, "slow pcurve %.2fs face %lld surf %s edge %s\n", s, (long long)face.index, si.kind.c_str(), m_edgeKind[key].c_str());
                }
            } catch (const Standard_Failure&) {
            }
        }
        // XT can omit the artificial seam needed by OCCT on a periodic face.
        if (!seamFreeSphericalTrim && !shiftedRevolution
            && (faceSurface->IsUClosed() || faceSurface->IsVClosed())) {
            ScopedTimer seamTimer(res.stats.seconds["seam-repair"]);
            try {
                ShapeFix_Face sff(f);
                sff.SetContext(m_reshape);
                sff.SetPrecision(1e-6 * scale);
                if (si.kind == "SPHERE" && wires == 1) {
                    // The seam operation can add the degenerate pole edge for a
                    // belting wire without running the full face-healing pipeline.
                    trace("repair-spherical-seam", face.index);
                    if (sff.FixMissingSeam()) {
                        const TopoDS_Shape repaired = sff.Result();
                        TopExp_Explorer faces(repaired, TopAbs_FACE);
                        if (!faces.More()) {
                            ++res.stats.facesSkippedBySurface["spherical-seam-empty"];
                            return {};
                        }
                        const TopoDS_Face repairedFace = TopoDS::Face(faces.Current());
                        faces.Next();
                        if (faces.More()) {
                            ++res.stats.facesSkippedBySurface["spherical-seam-split"];
                            return {};
                        }
                        f = repairedFace;
                        ++res.stats.seamRepairedFaces;
                    }
                } else {
                    sff.FixMissingSeamMode() = 1;
                    trace("repair-seam", face.index);
                    sff.Perform();
                    if (m_trace && si.kind == "BLENDED_EDGE" && faceSurface->IsUPeriodic()) {
                        size_t resultFaces = 0, resultWires = 0, selectedWires = 0;
                        for (TopExp_Explorer ex(sff.Result(), TopAbs_FACE); ex.More(); ex.Next())
                            ++resultFaces;
                        for (TopExp_Explorer ex(sff.Result(), TopAbs_WIRE); ex.More(); ex.Next())
                            ++resultWires;
                        for (TopExp_Explorer ex(sff.Face(), TopAbs_WIRE); ex.More(); ex.Next())
                            ++selectedWires;
                        std::fprintf(stderr, "XT periodic seam face=%lld source-wires=%zu result-faces=%zu result-wires=%zu selected-wires=%zu\n",
                                     static_cast<long long>(face.index), wires, resultFaces, resultWires, selectedWires);
                    }
                    f = sff.Face();
                    ++res.stats.seamRepairedFaces;
                }
            } catch (const Standard_Failure&) {
            }
        }
    }
    if (wires && !seamFreeSphericalTrim && !faceSurface->IsUClosed() && !faceSurface->IsVClosed()) {
        // The wires were oriented on the assumption that the surface's natural normal has the sense given by
        // `si.flip`. For surfaces whose parametrisation is not known in advance (fitted blends, B-spline
        // nets) the outer wire can come out clockwise in (u, v), which the mesher cannot triangulate. The
        // parameter-space direction of the largest wire tells the true relation: flip wires and face together.
        double outerArea = 0;
        for (TopExp_Explorer ew(f, TopAbs_WIRE); ew.More(); ew.Next()) {
            const double a = wireParameterArea(f, TopoDS::Wire(ew.Current()));
            if (const char* dbg = std::getenv("XT_DEBUG_ORIENT"); dbg && std::atoll(dbg) == face.index)
                std::fprintf(stderr, "orient face %lld: wire uv-area %g (surface %s)\n", static_cast<long long>(face.index), a, si.kind.c_str());
            if (std::abs(a) > std::abs(outerArea))
                outerArea = a;
        }
        if (outerArea < 0) {
            TopoDS_Face g;
            bb.MakeFace(g, faceSurface, TopLoc_Location(), kXtTolerance * scale);
            for (TopExp_Explorer ew(f, TopAbs_WIRE); ew.More(); ew.Next())
                bb.Add(g, ew.Current().Reversed());
            f = g;
            faceReversed = !faceReversed;
            ++res.stats.orientationFlips[si.kind];
        }
    }
    if (opt.deviationSamples > 0 && wires) {
        double worst = 0;
        for (const auto& it : toFix) {
            double c0, c1;
            const Handle(Geom_Curve) c = BRep_Tool::Curve(it.edge, c0, c1);
            if (c.IsNull())
                continue;
            double edgeWorst = 0;
            int worstSample = 0;
            for (int s = 0; s <= opt.deviationSamples; ++s) {
                const gp_Pnt p = c->Value(c0 + (c1 - c0) * s / opt.deviationSamples);
                GeomAPI_ProjectPointOnSurf proj(p, faceSurface);
                if (proj.NbPoints() > 0) {
                    worst = std::max(worst, proj.LowerDistance());
                    if (proj.LowerDistance() > edgeWorst) {
                        edgeWorst = proj.LowerDistance();
                        worstSample = s;
                    }
                }
                if (std::getenv("XT_DEBUG") && proj.NbPoints() > 0 && proj.LowerDistance() > 1e-6) {
                    std::fprintf(stderr, "  edgekind %s\n", m_edgeKind[it.edge.TShape().get()].c_str());
                    std::fprintf(stderr, "  dev %s face %lld edge-pt (%g %g %g) dist %g\n", si.kind.c_str(), (long long)face.index,
                                 p.X(), p.Y(), p.Z(), proj.LowerDistance());
                }
            }
            if (std::getenv("XT_DEV_BRUTE") && edgeWorst > 0.05) {
                // Is the large distance real, or a failed point projection? Brute-force nearest surface sample.
                double u0b, u1b, v0b, v1b;
                faceSurface->Bounds(u0b, u1b, v0b, v1b);
                double best = 1e300;
                const gp_Pnt p = c->Value(c0 + (c1 - c0) * worstSample / opt.deviationSamples);
                for (int iu = 0; iu <= 300; ++iu)
                    for (int iv = 0; iv <= 100; ++iv)
                        best = std::min(best, p.Distance(faceSurface->Value(u0b + (u1b - u0b) * iu / 300.0, v0b + (v1b - v0b) * iv / 100.0)));
                std::fprintf(stderr, "BRUTE face %lld edge %s projected %.6g brute-grid %.6g\n", (long long)face.index, m_edgeKind[it.edge.TShape().get()].c_str(), edgeWorst, best);
            }
            if (std::getenv("XT_DEV_TABLE"))
                std::fprintf(stderr, "DEVROW\t%s\t%s\t%lld\t%.6g\t%d\t%d\n", m_edgeKind[it.edge.TShape().get()].c_str(), si.kind.c_str(),
                             static_cast<long long>(face.index), edgeWorst, worstSample, opt.deviationSamples);
        }
        double& slot = res.stats.maxSurfaceDeviation[si.kind];
        slot = std::max(slot, worst);
    }
    if (wires) {
        // A face whose pcurves do not follow its 3D edges would make SameParameter inflate the tolerances
        // of everything it touches and would mesh into long spikes. Compare the surface point of each
        // pcurve with the 3D curve before anything else sees the face; such a face is left out.
        const double limit = 5e-4 * scale;
        for (TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopoDS_Edge e = TopoDS::Edge(ex.Current());
            if (BRep_Tool::Degenerated(e))
                continue;
            double p0, p1, q0, q1;
            const Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(e, f, p0, p1);
            const Handle(Geom_Curve) c3 = BRep_Tool::Curve(e, q0, q1);
            if (pc.IsNull() || c3.IsNull() || !(p1 > p0) || !(q1 > q0))
                continue;
            for (int k = 0; k <= 4; ++k) {
                const double s = static_cast<double>(k) / 4;
                const gp_Pnt2d uv = pc->Value(p0 + (p1 - p0) * s);
                const gp_Pnt onSurface = faceSurface->Value(uv.X(), uv.Y());
                double d;
                if (k == 0 || k == 4) {
                    d = onSurface.Distance(c3->Value(k == 0 ? q0 : q1));
                } else {
                    GeomAPI_ProjectPointOnCurve proj(onSurface, c3, q0, q1);
                    d = proj.NbPoints() > 0 ? proj.LowerDistance() : 0.0;
                }
                if (d > limit) {
                    trace("pcurve-mismatch", face.index);
                    if (m_trace) {
                        const auto source = std::find_if(toFix.begin(), toFix.end(), [&](const auto& useEdge) {
                            return useEdge.edge.IsSame(e);
                        });
                        const auto edgeIndex = source == toFix.end() ? 0 : ptr(*source->fin, "FIN", "edge");
                        std::fprintf(stderr, "XT trim residual face=%lld edge=%lld sample=%d gap=%.6g\n",
                                     static_cast<long long>(face.index), static_cast<long long>(edgeIndex), k, d);
                    }
                    ++res.stats.facesSkippedBySurface["pcurve-mismatch"];
                    retainBoundary();
                    return {};
                }
            }
        }
    }
    if (wires) {
        // The mesher dereferences the pcurve of every edge of every face. A face whose edges did not
        // all get one (projection failed) would crash it, so such a face is left out and reported.
        for (TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopoDS_Edge e = TopoDS::Edge(ex.Current());
            double c0, c1;
            if (BRep_Tool::Degenerated(e))
                continue;
            if (BRep_Tool::CurveOnSurface(e, f, c0, c1).IsNull()) {
                trace("missing-pcurve", face.index);
                ++res.stats.facesSkippedBySurface["pcurve-failed"];
                retainBoundary();
                return {};
            }
        }
    }
    if (wires) {
        // A self-intersecting boundary sends the mesher's Delaunay step into very long loops (one face
        // made it spin for minutes), so such a face is left out and reported rather than hanging the import.
        trace("validate-face", face.index);
        ScopedTimer validateTimer(res.stats.seconds["validate-face"]);
        const BRepCheck_Analyzer analyzer(f);
        if (!analyzer.IsValid()) {
            for (TopExp_Explorer ew(f, TopAbs_WIRE); ew.More(); ew.Next()) {
                const Handle(BRepCheck_Result) wr = analyzer.Result(ew.Current());
                if (wr.IsNull())
                    continue;
                for (const BRepCheck_Status st : wr->Status())
                    if (st == BRepCheck_SelfIntersectingWire && !std::getenv("XT_KEEP_SI")
                        && (std::getenv("XT_FILTER_ALL_SI") || si.kind != "PLANE")) {
                        // OCCT's own STEP post-processing runs ShapeFix with self-intersection repair on every
                        // face; try the same on this one before giving it up.
                        if (!std::getenv("XT_NO_SI_REPAIR")) {
                            try {
                                ShapeFix_Face repair(f);
                                repair.SetPrecision(5e-3);
                                repair.FixWireMode() = 1;
                                repair.FixWireTool()->FixSelfIntersectionMode() = 1;
                                repair.FixWireTool()->FixIntersectingEdgesMode() = 1;
                                repair.FixWireTool()->FixNonAdjacentIntersectingEdgesMode() = 1;
                                repair.Perform();
                                const TopoDS_Face candidate = repair.Face();
                                const BRepCheck_Analyzer after(candidate);
                                bool stillSelfIntersecting = false;
                                for (TopExp_Explorer w2(candidate, TopAbs_WIRE); w2.More(); w2.Next()) {
                                    const Handle(BRepCheck_Result) r2 = after.Result(w2.Current());
                                    if (r2.IsNull())
                                        continue;
                                    for (const BRepCheck_Status s2 : r2->Status())
                                        if (s2 == BRepCheck_SelfIntersectingWire)
                                            stillSelfIntersecting = true;
                                }
                                if (!stillSelfIntersecting && !candidate.IsNull()) {
                                    f = candidate;
                                    ++res.stats.facesSkippedBySurface["  self-intersection repaired " + si.kind];
                                    goto selfIntersectionResolved;
                                }
                            } catch (const Standard_Failure&) {
                            }
                        }
                        trace("self-intersecting-wire", face.index);
                        if (std::getenv("XT_DEBUG_SI") && si.kind == "PLANE") {
                            std::fprintf(stderr, "SI plane face %lld:", (long long)face.index);
                            for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                                const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                                double p0, p1;
                                const Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(e, f, p0, p1);
                                std::fprintf(stderr, " [%s o=%d", m_edgeKind[e.TShape().get()].c_str(), (int)e.Orientation());
                                if (!pc.IsNull()) {
                                    const gp_Pnt2d a = pc->Value(p0), b = pc->Value(p1);
                                    std::fprintf(stderr, " (%.4g,%.4g)->(%.4g,%.4g)", a.X(), a.Y(), b.X(), b.Y());
                                }
                                std::fprintf(stderr, "]");
                            }
                            std::fprintf(stderr, "\n");
                        }
                        ++res.stats.facesSkippedBySurface["self-intersecting-wire"];
                        ++res.stats.facesSkippedBySurface["  self-intersecting " + si.kind + (faceSurface->IsUClosed() || faceSurface->IsVClosed() ? " closed" : "")];
                        retainBoundary();
                        return {};
                    }
            }
        }
    }
selfIntersectionResolved:
    if (faceReversed)
        f.Reverse();
    ++res.stats.facesBuilt;
    return f;
}

// OCCT reports construction problems by exception; one bad edge or face must not end the import.
TopoDS_Edge Builder::edgeFor(const Node& edge) {
    try {
        return edgeForImpl(edge);
    } catch (const Standard_Failure&) {
    } catch (const std::exception&) {
    }
    m_edges[edge.index] = TopoDS_Edge();
    ++res.stats.edgesSkippedByCurve["exception"];
    return {};
}

// BRepFill_Filling has crashed (access violation) on a damaged boundary; no C++ objects with destructors in
// here, so structured exception handling can contain it.
// Largest distance between an edge's 3D curve and its pcurve mapped onto the face's surface, over all edge uses
// of the face. A large value means the face's own trim is wrong, whatever tolerance a shared edge has reached.
static double faceTrimResidual(const TopoDS_Face& face) {
    double worst = 0;
    try {
        BRepAdaptor_Surface surface(face, false);
        for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
            const TopoDS_Edge e = TopoDS::Edge(ee.Current());
            if (BRep_Tool::Degenerated(e))
                continue;
            double a, b;
            const Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(e, face, a, b);
            if (pc.IsNull())
                return 1e300; // no trim at all
            BRepAdaptor_Curve curve(e);
            const double c0 = curve.FirstParameter(), c1 = curve.LastParameter();
            // A range that is empty, inverted or not finite cannot be checked, so the trim is not validated.
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c0) || !std::isfinite(c1) || !(b > a) ||
                !(c1 > c0))
                return 1e300;
            for (int k = 0; k <= 8; ++k) {
                const double f = k / 8.0;
                const gp_Pnt2d uv = pc->Value(a + (b - a) * f);
                const gp_Pnt onSurface = surface.Value(uv.X(), uv.Y());
                const gp_Pnt onCurve = curve.Value(c0 + (c1 - c0) * f);
                const double d = onSurface.Distance(onCurve);
                if (!std::isfinite(d))
                    return 1e300;
                worst = std::max(worst, d);
            }
        }
    } catch (const Standard_Failure&) {
        return 1e300;
    }
    return worst;
}

static void fillTrace(const char* stage) {
    static const bool on = std::getenv("XT_TRACE_IMPORT") != nullptr;
    if (on) {
        std::fprintf(stderr, "XT fill %s\n", stage);
        std::fflush(stderr);
    }
}

static bool buildFillingGuarded(BRepFill_Filling* filling) {
    fillTrace("filling-build");
#ifdef _MSC_VER
    __try {
        filling->Build();
        return filling->IsDone();
    } __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
        return false;
    }
#else
    filling->Build();
    return filling->IsDone();
#endif
}

struct TrimSample {
    const TopoDS_Face* face = nullptr;
    double u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    bool useClassifier = true;
    Bnd_Box box;
    int inside = 0;
    bool nonFinite = false;
};

static void sampleSurfaceInTrim(TrimSample* s) {
    BRepAdaptor_Surface surface(*s->face);
    std::unique_ptr<BRepTopAdaptor_FClass2d> classifier;
    if (s->useClassifier)
        classifier = std::make_unique<BRepTopAdaptor_FClass2d>(*s->face, 1e-7);
    constexpr int kGrid = 24;
    for (int i = 0; i <= kGrid; ++i)
        for (int j = 0; j <= kGrid; ++j) {
            const double u = s->u0 + (s->u1 - s->u0) * i / kGrid, v = s->v0 + (s->v1 - s->v0) * j / kGrid;
            if (classifier && classifier->Perform(gp_Pnt2d(u, v)) == TopAbs_OUT)
                continue;
            const gp_Pnt p = surface.Value(u, v);
            if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z())) {
                s->nonFinite = true;
                return;
            }
            s->box.Add(p);
            ++s->inside;
        }
}

// No C++ objects with destructors in here, so structured exception handling can contain a crash.
static bool sampleGuarded(TrimSample* s) {
#ifdef _MSC_VER
    __try {
        sampleSurfaceInTrim(s);
        return true;
    } __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
        return false;
    }
#else
    sampleSurfaceInTrim(s);
    return true;
#endif
}

// Check against the input boundary. The filler can generate different edges, so its own
// boundary is not an independent limit on where the patch may extend.
static bool fillStaysInsideBoundary(const TopoDS_Face& face, const Bnd_Box& boundary) {
    fillTrace("stay-inside-check");
    try {
        if (boundary.IsVoid() || boundary.IsOpen())
            return false;
        double b[6];
        boundary.Get(b[0], b[1], b[2], b[3], b[4], b[5]);
        const double esz = std::max({b[3] - b[0], b[4] - b[1], b[5] - b[2]});
        // The mesher itself can spin for hours on a runaway surface (a traced run stopped here for good), so the
        // surface is sampled on a grid over the face's parameter box instead; no meshing is involved.
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        if (!(u1 > u0) || !(v1 > v0) || !std::isfinite(u0) || !std::isfinite(u1) || !std::isfinite(v0) ||
            !std::isfinite(v1))
            return false;
        // Only samples inside the trim count: the corners of the parameter box that the face does not use can
        // lie anywhere and must not reject a good patch. The 2D classifier has crashed (access violation) on a
        // damaged fill, so it runs under structured exception handling; if it fails the whole box is sampled.
        TrimSample sample;
        sample.face = &face;
        sample.u0 = u0;
        sample.u1 = u1;
        sample.v0 = v0;
        sample.v1 = v1;
        fillTrace("stay-inside-sampling");
        if (!sampleGuarded(&sample)) {
            sample = TrimSample();
            sample.face = &face;
            sample.u0 = u0;
            sample.u1 = u1;
            sample.v0 = v0;
            sample.v1 = v1;
            sample.useClassifier = false;
            if (!sampleGuarded(&sample))
                return false;
        }
        if (sample.nonFinite || sample.inside == 0)
            return false;
        Bnd_Box& tb = sample.box;
        double a[6];
        tb.Get(a[0], a[1], a[2], a[3], a[4], a[5]);
        // Each axis is compared with the source boundary on its own, with a margin of half the boundary's extent
        // on that axis, so a sideways excursion on a long thin face is not hidden by its long dimension.
        for (int k = 0; k < 3; ++k) {
            const double extent = b[k + 3] - b[k];
            const double margin = 0.5 * extent + 0.05 * esz + 0.5;
            if (a[k] < b[k] - margin || a[k + 3] > b[k + 3] + margin)
                return false;
        }
        return true;
    } catch (const Standard_Failure&) {
        return false;
    }
}

// When no smooth fill stays inside its boundary, a boundary that is nearly planar (within a few percent of its
// size) is closed with a plane instead, which cannot run away.
static TopoDS_Face planarFillOfWire(const TopoDS_Wire& wire) {
    fillTrace("planar-fill");
    try {
        Bnd_Box box;
        BRepBndLib::AddOptimal(wire, box, false, false);
        if (box.IsVoid())
            return {};
        double a[6];
        box.Get(a[0], a[1], a[2], a[3], a[4], a[5]);
        const double size = std::max({a[3] - a[0], a[4] - a[1], a[5] - a[2]});
        BRepLib_FindSurface finder(wire, 0.02 * size, true);
        if (!finder.Found())
            return {};
        BRepBuilderAPI_MakeFace maker(finder.Surface(), wire, true);
        if (!maker.IsDone())
            return {};
        TopoDS_Face face = maker.Face();
        ShapeFix_Face fix(face);
        fix.SetPrecision(std::max(5e-3, 0.02 * size));
        fix.Perform();
        face = fix.Face();
        if (face.IsNull() || !fillStaysInsideBoundary(face, box))
            return {};
        return face;
    } catch (const Standard_Failure&) {
        return {};
    }
}

// Cuts the given hole wires out of a filled face. The result is accepted only when it is valid, stays inside
// its boundary and still has every hole; otherwise `filled` is left as it was (capped) and false is returned.
static bool cutHolesFromFill(TopoDS_Face& filled, const std::vector<TopoDS_Wire>& holes,
                             const Bnd_Box& boundary) {
    if (holes.empty())
        return true;
    try {
        TopLoc_Location location;
        const Handle(Geom_Surface) fs = BRep_Tool::Surface(filled, location);
        if (fs.IsNull() || !location.IsIdentity())
            return false;
        fillTrace("holes-makeface");
        BRepBuilderAPI_MakeFace trimmed(fs, BRep_Tool::Tolerance(filled));
        trimmed.Add(BRepTools::OuterWire(filled));
        for (const TopoDS_Wire& hole : holes)
            trimmed.Add(hole);
        fillTrace("holes-shapefix");
        ShapeFix_Face fix(trimmed.Face());
        fix.SetPrecision(5e-3);
        fix.FixAddNaturalBoundMode() = 0;
        fix.Perform();
        TopoDS_Face candidate = fix.Face();
        fillTrace("holes-check");
        if (candidate.IsNull() || !BRepCheck_Analyzer(candidate).IsValid())
            return false;
        fillTrace("holes-inside");
        if (!fillStaysInsideBoundary(candidate, boundary))
            return false;
        size_t wires = 0;
        for (TopExp_Explorer ew(candidate, TopAbs_WIRE); ew.More(); ew.Next())
            ++wires;
        if (wires != holes.size() + 1)
            return false; // a hole was lost in the repair
        if (candidate.Orientation() != filled.Orientation())
            candidate.Orientation(filled.Orientation());
        filled = candidate; // same surface as the filled face, so the same orientation applies
        return true;
    } catch (const Standard_Failure&) {
        return false;
    }
}

// A face whose surface disagrees with its own edges is replaced by a surface filled through the outer
// boundary of the same edges, oriented like the face it replaces. Inner loops of the original are cut out again.
static TopoDS_Face fillFromBoundaryOf(const TopoDS_Face& original, size_t* holesLost = nullptr) {
    const TopoDS_Wire outer = BRepTools::OuterWire(original);
    if (outer.IsNull())
        return {};
    Bnd_Box boundary;
    BRepBndLib::AddOptimal(outer, boundary, false, false);
    // First attempt on the shared edges themselves; an edge that has lost the point of a vertex makes the filler
    // throw, and then clean copies built from the edges' 3D curves are used instead.
    TopoDS_Face filled;
    for (int attempt = 0; attempt < 2 && filled.IsNull(); ++attempt) {
        try {
            BRepFill_Filling filling(3, 12, 2, false, 1e-5, 5e-3, 0.05, 0.5, 8, 9);
            size_t count = 0;
            for (TopExp_Explorer ee(outer, TopAbs_EDGE); ee.More(); ee.Next()) {
                TopoDS_Edge e = TopoDS::Edge(ee.Current());
                if (BRep_Tool::Degenerated(e))
                    continue;
                if (attempt == 1) {
                    double a, b;
                    const Handle(Geom_Curve) c = BRep_Tool::Curve(e, a, b);
                    if (c.IsNull() || !(b > a))
                        return {};
                    e = BRepBuilderAPI_MakeEdge(c, a, b).Edge();
                }
                filling.Add(e, GeomAbs_C0, true);
                ++count;
            }
            if (count == 0 || count > 60)
                return {};
            if (buildFillingGuarded(&filling))
                filled = filling.Face();
        } catch (const Standard_Failure&) {
            if (attempt == 1)
                throw;
        }
    }
    if (filled.IsNull() || !fillStaysInsideBoundary(filled, boundary))
        filled = planarFillOfWire(outer);
    if (filled.IsNull())
        return {};
    try {
        BRepAdaptor_Surface fs(filled);
        gp_Pnt p;
        gp_Vec du, dv;
        fs.D1(0.5 * (fs.FirstUParameter() + fs.LastUParameter()), 0.5 * (fs.FirstVParameter() + fs.LastVParameter()), p, du, dv);
        gp_Vec nf = du.Crossed(dv);
        if (filled.Orientation() == TopAbs_REVERSED)
            nf.Reverse();
        TopLoc_Location location;
        const Handle(Geom_Surface) os = BRep_Tool::Surface(original, location);
        if (!os.IsNull()) {
            ShapeAnalysis_Surface analyser(os);
            const gp_Pnt2d uv = analyser.ValueOfUV(p.Transformed(location.Transformation().Inverted()), 1e-3);
            gp_Pnt q;
            gp_Vec ou, ov;
            os->D1(uv.X(), uv.Y(), q, ou, ov);
            gp_Vec no = ou.Crossed(ov);
            if (original.Orientation() == TopAbs_REVERSED)
                no.Reverse();
            if (nf.Magnitude() > 1e-12 && no.Magnitude() > 1e-12 && nf.Dot(no) < 0)
                filled.Reverse();
        }
    } catch (const Standard_Failure&) {
    }
    std::vector<TopoDS_Wire> holes;
    for (TopExp_Explorer ew(original, TopAbs_WIRE); ew.More(); ew.Next()) {
        const TopoDS_Wire w = TopoDS::Wire(ew.Current());
        if (!w.IsSame(outer))
            holes.push_back(w);
    }
    if (!holes.empty() && !cutHolesFromFill(filled, holes, boundary) && holesLost)
        *holesLost += holes.size();
    return filled;
}

// Last resort for a face whose own surface cannot be built (or whose exact trim is unusable): a smooth
// surface is filled through the boundary edges, so the part has no hole. The face is an approximation of
// the real one - same boundary, interior shape inferred - and is reported as such.
TopoDS_Face Builder::fillFaceFromBoundary(const Node& face) {
    auto why = [&](const char* w) {
        ++res.stats.facesSkippedBySurface[std::string("  fill-") + w];
        return TopoDS_Face();
    };
    struct Ring {
        std::vector<TopoDS_Edge> edges;
        std::vector<gp_Pnt> points;
        gp_Vec newell;
    };
    std::vector<Ring> rings;
    for (const Node* loop : chain(node(ptr(face, "FACE", "loop")), "LOOP", "next")) {
        const Node* start = node(ptr(*loop, "LOOP", "fin"));
        if (!start)
            continue;
        Ring ring;
        std::unordered_set<int64_t> seen;
        const Node* fin = start;
        for (; fin && seen.insert(fin->index).second; fin = node(ptr(*fin, "FIN", "forward"))) {
            const Node* en = node(ptr(*fin, "FIN", "edge"));
            if (!en || m_microEdges.count(en->index))
                continue;
            TopoDS_Edge e = edgeFor(*en);
            if (e.IsNull()) {
                // The edge's own curve could not be built: a straight segment between its end vertices stands
                // in for it, so the filled surface is still bounded.
                const Node* sV = nullptr;
                const Node* eV = nullptr;
                const Node* first = node(ptr(*en, "EDGE", "fin"));
                const Node* f2 = first;
                for (size_t guard = 0; f2 && guard < 64; ++guard) {
                    const Node* v = node(ptr(*f2, "FIN", "vertex"));
                    if (num(*f2, "FIN", "sense") == '+')
                        eV = eV ? eV : v;
                    else
                        sV = sV ? sV : v;
                    f2 = node(ptr(*f2, "FIN", "other"));
                    if (f2 == first)
                        break;
                }
                if (!sV || !eV || sV->index == eV->index)
                    return why("nobridge"); // nothing to bridge
                const TopoDS_Vertex vs = vertexFor(sV->index), ve = vertexFor(eV->index);
                if (vs.IsNull() || ve.IsNull() || BRep_Tool::Pnt(vs).Distance(BRep_Tool::Pnt(ve)) < 1e-6)
                    return why("nobridge2");
                e = BRepBuilderAPI_MakeEdge(vs, ve).Edge();
                if (e.IsNull())
                    return why("badbridge");
            }
            BRepAdaptor_Curve curve(e);
            const bool forward = num(*fin, "FIN", "sense") == '+';
            const double a = curve.FirstParameter(), b = curve.LastParameter();
            for (int k = 0; k < 6; ++k) {
                const double t = forward ? a + (b - a) * k / 6.0 : b - (b - a) * k / 6.0;
                ring.points.push_back(curve.Value(t));
            }
            ring.edges.push_back(e);
        }
        if (fin != start || ring.edges.empty())
            continue;
        double nx = 0, ny = 0, nz = 0;
        for (size_t i = 0; i < ring.points.size(); ++i) {
            const gp_Pnt& p = ring.points[i];
            const gp_Pnt& q = ring.points[(i + 1) % ring.points.size()];
            nx += (p.Y() - q.Y()) * (p.Z() + q.Z());
            ny += (p.Z() - q.Z()) * (p.X() + q.X());
            nz += (p.X() - q.X()) * (p.Y() + q.Y());
        }
        ring.newell = gp_Vec(nx, ny, nz);
        rings.push_back(std::move(ring));
    }
    if (rings.empty())
        return why("noedge");
    // The outer boundary is the loop with the largest enclosed area; any other loop is a hole that the
    // filled surface does not cut out.
    size_t outer = 0;
    for (size_t i = 1; i < rings.size(); ++i)
        if (rings[i].newell.Magnitude() > rings[outer].newell.Magnitude())
            outer = i;
    const Ring& ring = rings[outer];
    if (ring.edges.size() > 60)
        return why("noedge");
    Bnd_Box boundary;
    for (const TopoDS_Edge& e : ring.edges)
        BRepBndLib::AddOptimal(e, boundary, false, false);

    // Several fill settings are tried; the first surface that stays inside its boundary wins.
    static const int variants[][3] = {{3, 12, 2}, {2, 8, 1}};
    TopoDS_Face filled;
    bool anyBuilt = false;
    int variantNumber = 0;
    for (const auto& v : variants) {
        ++variantNumber;
        try {
            BRepFill_Filling filling(v[0], v[1], v[2], false, 1e-5, 5e-3, 0.05, 0.5, 8, 9);
            for (const TopoDS_Edge& e : ring.edges)
                filling.Add(e, GeomAbs_C0, true);
            bool built;
            {
                const auto b0 = std::chrono::steady_clock::now();
                ScopedTimer t(res.stats.seconds[variantNumber == 1 ? "fill-build-variant1" : "fill-build-variant2"]);
                built = buildFillingGuarded(&filling);
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count();
                if (s > 1.0 && std::getenv("XT_DEBUG_SLOW"))
                    std::fprintf(stderr, "slow fill face %lld variant %d edges %zu %.1fs built=%d\n",
                                 static_cast<long long>(face.index), variantNumber, ring.edges.size(), s, built ? 1 : 0);
            }
            if (!built) {
                ++res.stats.facesSkippedBySurface[variantNumber == 1 ? "  fill-variant1-notbuilt" : "  fill-variant2-notbuilt"];
                continue;
            }
            const TopoDS_Face candidate = filling.Face();
            if (candidate.IsNull())
                continue;
            anyBuilt = true;
            ScopedTimer t(res.stats.seconds["fill-inside-check"]);
            if (fillStaysInsideBoundary(candidate, boundary)) {
                ++res.stats.facesSkippedBySurface[variantNumber == 1 ? "  fill-variant1-accepted" : "  fill-variant2-accepted"];
                filled = candidate;
                m_fillKind = variantNumber == 1 ? "fill-v1" : "fill-v2";
                break;
            }
        } catch (const Standard_Failure&) {
        }
    }
    if (filled.IsNull()) {
        ScopedTimer planarTimer(res.stats.seconds["fill-planar-stage"]);
        BRepBuilderAPI_MakeWire ringWire;
        for (const TopoDS_Edge& e : ring.edges)
            ringWire.Add(e);
        if (ringWire.IsDone())
            filled = planarFillOfWire(ringWire.Wire());
        if (!filled.IsNull()) {
            ++res.stats.facesSkippedBySurface["  fill-planar"];
            m_fillKind = "fill-planar";
        }
    }
    if (filled.IsNull())
        return why(anyBuilt ? "runaway" : "noring");

    // Orient the face along the XT normal, which the loop direction defines (the face is on its left).
    try {
        BRepAdaptor_Surface surface(filled);
        const double u = 0.5 * (surface.FirstUParameter() + surface.LastUParameter());
        const double v = 0.5 * (surface.FirstVParameter() + surface.LastVParameter());
        gp_Pnt p;
        gp_Vec du, dv;
        surface.D1(u, v, p, du, dv);
        gp_Vec normal = du.Crossed(dv);
        if (filled.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        if (normal.Magnitude() > 1e-12 && ring.newell.Magnitude() > 1e-12 && normal.Dot(ring.newell) < 0)
            filled.Reverse();
    } catch (const Standard_Failure&) {
    }
    // Inner loops are cut out of the filled surface, so a hole in the source face stays a hole. If the
    // trimmed face is not valid the capped face is kept: a filled patch is better than a missing one.
    if (rings.size() > 1 && !std::getenv("XT_NO_FILL_HOLES")) {
        try {
            std::vector<TopoDS_Wire> holeWires;
            size_t unclosed = 0;
            for (size_t i = 0; i < rings.size(); ++i) {
                if (i == outer || rings[i].edges.empty())
                    continue;
                BRepBuilderAPI_MakeWire wire;
                for (const TopoDS_Edge& e : rings[i].edges)
                    wire.Add(e);
                if (!wire.IsDone() || !wire.Wire().Closed()) {
                    ++unclosed;
                    continue;
                }
                holeWires.push_back(wire.Wire());
            }
            // Every hole of the source face has to survive; a subset would silently cap the rest. A face with
            // an unclosed inner loop is reported as incomplete even when the other loops are cut.
            if (unclosed)
                ++res.stats.facesSkippedBySurface["  fill-holes-unclosed"];
            bool allCut = unclosed == 0;
            if (!holeWires.empty()) {
                if (cutHolesFromFill(filled, holeWires, boundary)) {
                    ++res.stats.facesSkippedBySurface["  fill-holes-cut"];
                } else {
                    ++res.stats.facesSkippedBySurface["  fill-holes-invalid"];
                    allCut = false;
                }
            }
            if (!allCut) {
                ++res.stats.facesSkippedBySurface["  fill-capped-incomplete"];
                m_fillKind += "+holes-capped";
            }
        } catch (const Standard_Failure&) {
            ++res.stats.facesSkippedBySurface["  fill-holes-exception"];
        }
    }
    ++res.stats.facesFilledFromBoundary;
    ++res.stats.approximatedGeometry;
    ++res.stats.facesBuilt;
    return filled;
}

TopoDS_Face Builder::faceFor(const Node& face, TopAbs_Orientation use) {
    if (cancelled())
        return {};
    trace("face", face.index);
    if (opt.progress && (res.stats.faces & 63) == 0)
        opt.progress(res.stats.faces, m_totalFaces);
    TopoDS_Face result;
    const size_t candidatesBefore = m_displayCandidates.size();
    std::map<std::string, size_t> countersBefore;
    if (m_outcomes)
        countersBefore = res.stats.facesSkippedBySurface;
    m_fillKind.clear();
    bool fromFill = false;
    try {
        result = faceForImpl(face, use);
    } catch (const Standard_Failure&) {
        ++res.stats.facesSkippedBySurface["exception"];
    } catch (const std::exception&) {
        ++res.stats.facesSkippedBySurface["exception"];
    }
    if (result.IsNull() && !cancelled() && !std::getenv("XT_NO_FILL")) {
        try {
            ScopedTimer fillTimer(res.stats.seconds["fill-face"]);
            result = fillFaceFromBoundary(face);
            if (!result.IsNull()) {
                fromFill = true;
                trace("face-filled-from-boundary", face.index);
                // The rejected face was queued for display; the filled face replaces it, so it is not drawn twice.
                if (m_displayCandidates.size() > candidatesBefore)
                    m_displayCandidates.resize(candidatesBefore);
            }
        } catch (const Standard_Failure&) {
        } catch (const std::exception&) {
        }
    }
    if (result.IsNull())
        trace("omit-face", face.index);
    if (m_outcomes) {
        FaceOutcome outcome;
        const Node* surf = node(ptr(face, "FACE", "surface"));
        outcome.surface = surf ? nameOf(*surf) : std::string("none");
        outcome.origin = result.IsNull() ? "omitted" : fromFill ? (m_fillKind.empty() ? std::string("fill") : m_fillKind) : "native";
        for (const auto& [key, count] : res.stats.facesSkippedBySurface) {
            const auto before = countersBefore.find(key);
            if (before != countersBefore.end() && before->second == count)
                continue;
            const size_t first = key.find_first_not_of(' ');
            if (!outcome.reason.empty())
                outcome.reason += ',';
            outcome.reason += first == std::string::npos ? key : key.substr(first);
        }
        (*m_outcomes)[face.index] = std::move(outcome);
    }
    return result;
}

// Follows a chain of nodes (next pointers) and stops at a repeated node, so a damaged file with a cycle
// cannot keep the importer in the same chain forever.
std::vector<const Node*> Builder::chain(const Node* head, const char* nodeName, const char* nextField) {
    std::vector<const Node*> out;
    std::unordered_set<int64_t> seen;
    const Slot& next = sl(nodeName, nextField);
    for (const Node* n = head; n; n = node(m.pointer(*n, next))) {
        if (!seen.insert(n->index).second) {
            diag("cyclic-chain", std::string(nodeName) + " chain repeats node " + std::to_string(n->index) + "; the rest of the chain is ignored", n->index);
            break;
        }
        out.push_back(n);
        if ((out.size() & 1023) == 0 && cancelled())
            break;
    }
    return out;
}

static size_t countFacesWithoutPCurves(const TopoDS_Shape& shape) {
    size_t bad = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        bool missing = false;
        for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More() && !missing; ee.Next()) {
            const TopoDS_Edge e = TopoDS::Edge(ee.Current());
            double a, b;
            if (!BRep_Tool::Degenerated(e) && BRep_Tool::CurveOnSurface(e, face, a, b).IsNull())
                missing = true;
        }
        bad += missing ? 1 : 0;
    }
    return bad;
}

void Builder::buildBody(const Node& body) {
    m_displayCandidates.clear();
    BuiltBody out;
    out.index = body.index;
    out.name = displayName(body, "Body");
    out.bodyType = static_cast<int>(num(body, "BODY", "body_type"));
    ++res.stats.bodies;
    m_outcomes = &out.faceOutcomes;

    TopoDS_Compound compound;
    bb.MakeCompound(compound);
    size_t parts = 0;
    std::vector<std::pair<TopoDS_Face, int64_t>> tracedFaces;

    auto addFaceChain = [&](TopoDS_Shell& shell, const Node* head, const char* nextField, TopAbs_Orientation use,
                            size_t& built, size_t& total) {
        size_t guard = 0;
        for (const Node* f : chain(head, "FACE", nextField)) {
            if ((++guard & 255) == 0 && cancelled())
                return;
            ++total;
            TopoDS_Face face = faceFor(*f, use);
            if (face.IsNull())
                continue;
            bb.Add(shell, use == TopAbs_REVERSED ? face.Reversed() : face);
            tracedFaces.emplace_back(face, f->index);
            ++built;
        }
    };

    bool anySolid = false;
    for (const Node* region : chain(node(ptr(body, "BODY", "region")), "REGION", "next")) {
        if (num(*region, "REGION", "type") != 'S')
            continue;
        anySolid = true;
        TopoDS_Solid solid;
        bb.MakeSolid(solid);
        size_t shellCount = 0;
        for (const Node* sh : chain(node(ptr(*region, "REGION", "shell")), "SHELL", "next")) {
            TopoDS_Shell shell;
            bb.MakeShell(shell);
            addFaceChain(shell, node(ptr(*sh, "SHELL", "face")), "next", TopAbs_FORWARD, out.facesBuilt, out.facesTotal);
            addFaceChain(shell, node(ptr(*sh, "SHELL", "front_face")), "next_front", TopAbs_REVERSED, out.facesBuilt,
                         out.facesTotal);
            TopExp_Explorer ex(shell, TopAbs_FACE);
            if (ex.More()) {
                bb.Add(solid, shell);
                ++shellCount;
            }
        }
        if (shellCount) {
            bb.Add(compound, solid);
            ++parts;
        }
    }
    if (anySolid)
        ++res.stats.solidBodies;

    if (!anySolid) {
        // Sheet, wire or general body: collect every face of every shell once, plus wireframe edges.
        bool anyFace = false;
        std::unordered_set<int64_t> seen;
        for (const Node* region : chain(node(ptr(body, "BODY", "region")), "REGION", "next")) {
            for (const Node* sh : chain(node(ptr(*region, "REGION", "shell")), "SHELL", "next")) {
                TopoDS_Shell shell;
                bb.MakeShell(shell);
                size_t built = 0, total = 0;
                auto take = [&](const Node* head, const char* nextField, TopAbs_Orientation use) {
                    for (const Node* f : chain(head, "FACE", nextField)) {
                        if (!seen.insert(f->index).second)
                            continue;
                        ++total;
                        TopoDS_Face face = faceFor(*f, use);
                        if (face.IsNull())
                            continue;
                        bb.Add(shell, face);
                        tracedFaces.emplace_back(face, f->index);
                        ++built;
                    }
                };
                take(node(ptr(*sh, "SHELL", "face")), "next", TopAbs_FORWARD);
                take(node(ptr(*sh, "SHELL", "front_face")), "next_front", TopAbs_FORWARD);
                out.facesBuilt += built;
                out.facesTotal += total;
                if (built) {
                    bb.Add(compound, shell);
                    ++parts;
                    anyFace = true;
                }
                for (const Node* e : chain(node(ptr(*sh, "SHELL", "edge")), "EDGE", "next")) {
                    TopoDS_Edge edge = edgeFor(*e);
                    if (!edge.IsNull()) {
                        bb.Add(compound, edge);
                        ++parts;
                    }
                }
            }
        }
        if (anyFace)
            ++res.stats.sheetBodies;
        else
            ++res.stats.wireBodies;
    }

    if (cancelled())
        return;
    out.displayCandidates = std::move(m_displayCandidates);
    if (parts == 0 && out.displayCandidates.empty()) {
        diag("body-empty", "no geometry could be built for body " + std::to_string(body.index), body.index);
        return;
    }
    std::unordered_map<const TopoDS_TShape*, int64_t> nodeOfFace; // built face -> XT FACE node (after the shared repairs)
    try {
        // Repaired faces carry replacement edges; make every face that shares them see the same ones.
        ScopedTimer finishTimer(res.stats.seconds["finish-body"]);
        const bool pcDebug = std::getenv("XT_DEBUG_PC") != nullptr;
        const size_t pc0 = pcDebug ? countFacesWithoutPCurves(compound) : 0;
        compound = TopoDS::Compound(m_reshape->Apply(compound));
        for (const auto& [face, source] : tracedFaces) {
            const TopoDS_Shape reshaped = m_reshape->Apply(face);
            if (!reshaped.IsNull() && reshaped.ShapeType() == TopAbs_FACE) {
                nodeOfFace.emplace(reshaped.TShape().get(), source);
                const auto outcome = out.faceOutcomes.find(source);
                if (outcome != out.faceOutcomes.end())
                    outcome->second.shape = reshaped.TShape().get();
                if (m_trace)
                    out.tracedFaceNodes.emplace(reshaped.TShape().get(), source);
            }
        }
        m_reshape->Clear();
        // Later passes change a face's fate; the outcome table follows them.
        auto setOutcomeOrigin = [&](const TopoDS_Face& face, const char* suffix, const char* replace = nullptr) {
            const auto n = nodeOfFace.find(face.TShape().get());
            if (n == nodeOfFace.end())
                return;
            const auto it = out.faceOutcomes.find(n->second);
            if (it == out.faceOutcomes.end())
                return;
            if (replace)
                it->second.origin = replace;
            else
                it->second.origin += suffix;
        };
        const size_t pc1 = pcDebug ? countFacesWithoutPCurves(compound) : 0;
        // Applying the shared repairs replaces an edge by the copy a repaired neighbour carries, and that copy
        // only has the pcurve of the face that was repaired. Give every other face its pcurves back.
        {
            ShapeFix_Edge fixer;
            for (TopExp_Explorer ex(compound, TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face face = TopoDS::Face(ex.Current());
                std::vector<TopoDS_Edge> lacking;
                std::unordered_map<const TopoDS_TShape*, int> uses;
                for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                    const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                    ++uses[e.TShape().get()];
                    double a, b;
                    if (!BRep_Tool::Degenerated(e) && BRep_Tool::CurveOnSurface(e, face, a, b).IsNull())
                        lacking.push_back(e);
                }
                if (lacking.empty())
                    continue;
                TopLoc_Location location;
                const Handle(Geom_Surface) surface = BRep_Tool::Surface(face, location);
                if (surface.IsNull() || !location.IsIdentity())
                    continue;
                const Handle(ShapeAnalysis_Surface) analyser = new ShapeAnalysis_Surface(surface);
                for (const TopoDS_Edge& e : lacking) {
                    try {
                        fixer.FixAddPCurve(e, face, uses[e.TShape().get()] > 1, analyser, pcurvePrecision);
                    } catch (const Standard_Failure&) {
                    }
                }
            }
        }
        trace("same-parameter", body.index);
        if (!std::getenv("XT_KEEP_STALE_PCURVES")) {
            // A face that was rejected after it attached its pcurve to a shared edge leaves that pcurve behind.
            // SameParameter and UpdateTolerances evaluate every pcurve of an edge, so the stale one inflates the
            // edge tolerance (measured: 3-8 mm on edges whose live faces agree to microns) and puts the
            // neighbouring faces at risk. Pcurves on surfaces that none of the edge's faces uses are removed.
            TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
            TopExp::MapShapesAndAncestors(compound, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
            for (int i = 1; i <= edgeFaces.Extent(); ++i) {
                const TopoDS_Edge& edge = TopoDS::Edge(edgeFaces.FindKey(i));
                std::vector<std::pair<Handle(Geom_Surface), TopLoc_Location>> usedSupports;
                for (const TopoDS_Shape& f : edgeFaces.FindFromIndex(i)) {
                    TopLoc_Location faceLocation;
                    if (const Handle(Geom_Surface) s = BRep_Tool::Surface(TopoDS::Face(f), faceLocation); !s.IsNull())
                        usedSupports.emplace_back(s, faceLocation.Predivided(edge.Location()));
                }
                const Handle(BRep_TEdge) tedge = Handle(BRep_TEdge)::DownCast(edge.TShape());
                if (tedge.IsNull())
                    continue;
                bool removedHere = false;
                auto& curves = tedge->ChangeCurves();
                for (NCollection_List<Handle(BRep_CurveRepresentation)>::Iterator it(curves); it.More();) {
                    const Handle(BRep_CurveRepresentation)& rep = it.Value();
                    if (rep->IsCurveOnSurface() &&
                        std::none_of(usedSupports.begin(), usedSupports.end(), [&](const auto& support) {
                            return rep->IsCurveOnSurface(support.first, support.second);
                        })) {
                        curves.Remove(it);
                        ++res.stats.facesSkippedBySurface["  stale-pcurves-removed"];
                        removedHere = true;
                    } else {
                        it.Next();
                    }
                }
                if (removedHere) {
                    tedge->Modified(true);
                    ++res.stats.facesSkippedBySurface["  stale-pcurve-edges"];
                }
            }
        }
        // Edges that the builder flagged SameParameter by hand are skipped by the non-forced call, so their real
        // pcurve/3D deviation is never measured and BRepCheck later reports InvalidSameParameterFlag.
        // Forced by default: in the standalone tool it cut invalid faces from 1009 to 70 (XS650) and 2726 to 334
        // (CBR) with no loss of meshed faces. XT_NO_FORCE_SAMEPARAM restores the old behaviour for comparison.
        // OCCT's STEP-style operator: for an edge already flagged SameParameter it measures the deviation and raises the tolerance;
        // for the others it keeps the better of the original and a recomputed pcurve. XT_SAMEPARAM_MODE=enforce re-derives every
        // edge, XT_SAMEPARAM_MODE=legacy restores the earlier forced BRepLib::SameParameter (and XT_NO_FORCE_SAMEPARAM the
        // unforced one). Measured against the legacy call: XS650-class engine 63 to 47 invalid faces, same build time.
        if (const char* mode = std::getenv("XT_SAMEPARAM_MODE"); mode && std::string(mode) == "legacy") {
            BRepLib::SameParameter(compound, 1e-4 * scale, std::getenv("XT_NO_FORCE_SAMEPARAM") == nullptr);
        } else {
            ShapeFix::SameParameter(compound, mode && std::string(mode) == "enforce", 1e-4 * scale);
        }
        if (pcDebug)
            std::fprintf(stderr, "pcdebug body %lld: faces without pcurves before-reshape %zu after-reshape %zu after-sameparam %zu\n",
                         (long long)body.index, pc0, pc1, countFacesWithoutPCurves(compound));
        BRepLib::UpdateTolerances(compound, false);
        // A face whose pcurves do not follow its 3D edges makes SameParameter/UpdateTolerances inflate the
        // edge tolerances to the size of the error (hundreds of metres here); such a face meshes into long
        // spikes. Faces with an edge tolerance far above anything a real tolerant edge has are removed.
        {
            const double limit = (kMaxVertexGap + 5e-4) * scale;
            std::vector<TopoDS_Face> broken;
            std::vector<std::pair<TopoDS_Edge, double>> rescuedEdges;
            std::vector<std::pair<TopoDS_Face, std::vector<TopoDS_Edge>>> rescuedFaces;
            for (TopExp_Explorer ex(compound, TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face face = TopoDS::Face(ex.Current());
                bool inflated = false;
                for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More() && !inflated; ee.Next())
                    inflated = BRep_Tool::Tolerance(TopoDS::Edge(ee.Current())) > limit;
                if (!inflated)
                    continue;
                // The inflated tolerance can come from a neighbour that shares the edge. Only a face whose own
                // 3D curves and pcurves disagree is removed; the others keep their geometry.
                const double residual = faceTrimResidual(face);
                if (residual > limit) {
                    broken.push_back(face);
                    continue;
                }
                ++res.stats.facesSkippedBySurface["  trim-validated-kept"];
                setOutcomeOrigin(face, "+rescued");
                std::vector<TopoDS_Edge> faceEdges;
                for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                    const TopoDS_Edge e = TopoDS::Edge(ee.Current());
                    if (BRep_Tool::Tolerance(e) > limit) {
                        rescuedEdges.emplace_back(e, std::max(kXtTolerance * scale, 1.2 * residual));
                        faceEdges.push_back(e);
                    }
                }
                rescuedFaces.emplace_back(face, std::move(faceEdges));
            }
            if (!broken.empty()) {
                std::vector<TopoDS_Shape> shells;
                for (TopExp_Explorer ex(compound, TopAbs_SHELL); ex.More(); ex.Next())
                    shells.push_back(ex.Current());
                std::vector<std::vector<TopoDS_Face>> shellOf(shells.size());
                for (size_t si2 = 0; si2 < shells.size(); ++si2) {
                    shells[si2].Free(Standard_True);
                    for (const TopoDS_Face& face : broken) {
                        bool inShell = false;
                        for (TopoDS_Iterator it(shells[si2]); it.More() && !inShell; it.Next())
                            inShell = it.Value().IsSame(face);
                        if (!inShell)
                            continue;
                        bb.Remove(shells[si2], face);
                        shellOf[si2].push_back(face);
                    }
                }
                for (const TopoDS_Face& face : broken)
                    for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next())
                        bb.UpdateEdge(TopoDS::Edge(ee.Current()), kXtTolerance * scale);
                for (const TopoDS_Face& face : broken)
                    for (TopExp_Explorer ev(face, TopAbs_VERTEX); ev.More(); ev.Next())
                        bb.UpdateVertex(TopoDS::Vertex(ev.Current()), kXtTolerance * scale);
                BRepLib::UpdateTolerances(compound, false);
                res.stats.facesSkippedBySurface["pcurve-mismatch"] += broken.size();
                size_t replaced = 0;
                if (!std::getenv("XT_NO_FILL")) {
                    // The exact surface of these faces disagrees with their edges; a surface filled through the
                    // same edges keeps the part closed.
                    for (size_t si2 = 0; si2 < shells.size(); ++si2) {
                        for (const TopoDS_Face& face : shellOf[si2]) {
                            try {
                                ScopedTimer fillTimer(res.stats.seconds["fill-from-boundary-of"]);
                                size_t holesLost = 0;
                                const TopoDS_Face filled = fillFromBoundaryOf(face, &holesLost);
                                res.stats.facesSkippedBySurface["  fill-broken-holes-capped"] += holesLost;
                                bool runaway = false;
                                if (!filled.IsNull() && !std::getenv("XT_NO_FILL_RUNAWAY_GUARD")) {
                                    // A fill far larger than the face it replaces has run away through a bad boundary.
                                    GProp_GProps before, after;
                                    BRepGProp::SurfaceProperties(face, before);
                                    BRepGProp::SurfaceProperties(filled, after);
                                    if (std::getenv("XT_DEBUG_CS"))
                                        std::fprintf(stderr, "broken fill: before %g after %g\n", before.Mass(), after.Mass());
                                    runaway = std::abs(after.Mass()) > 4 * std::abs(before.Mass()) + 50 * scale * scale * 1e-6;
                                    if (runaway)
                                        ++res.stats.facesSkippedBySurface["  fill-broken-runaway"];
                                }
                                if (!filled.IsNull() && !runaway) {
                                    bb.Add(shells[si2], filled);
                                    setOutcomeOrigin(face, "", "broken->fill");
                                    if (const auto n = nodeOfFace.find(face.TShape().get()); n != nodeOfFace.end())
                                        if (const auto o = out.faceOutcomes.find(n->second); o != out.faceOutcomes.end())
                                            o->second.shape = filled.TShape().get();
                                    const auto source = out.tracedFaceNodes.find(face.TShape().get());
                                    if (source != out.tracedFaceNodes.end())
                                        out.tracedFaceNodes.emplace(filled.TShape().get(), source->second);
                                    ++replaced;
                                } else {
                                    ++res.stats.facesSkippedBySurface["  fill-broken-failed"];
                                    setOutcomeOrigin(face, "", "broken->lost");
                                    if (const auto n = nodeOfFace.find(face.TShape().get()); n != nodeOfFace.end())
                                        if (const auto o = out.faceOutcomes.find(n->second); o != out.faceOutcomes.end())
                                            o->second.shape = nullptr;
                                }
                            } catch (const Standard_Failure& ex) {
                                if (std::getenv("XT_DEBUG_FILL"))
                                    std::fprintf(stderr, "fill exception: %s\n", ex.GetMessageString() ? ex.GetMessageString() : "(no message)");
                                ++res.stats.facesSkippedBySurface["  fill-broken-exception"];
                            }
                        }
                    }
                }
                res.stats.facesFilledFromBoundary += replaced;
                res.stats.approximatedGeometry += replaced;
                const size_t lost = broken.size() - replaced;
                out.facesBuilt = out.facesBuilt >= lost ? out.facesBuilt - lost : 0;
                res.stats.facesBuilt = res.stats.facesBuilt >= lost ? res.stats.facesBuilt - lost : 0;
            }
            if (!rescuedEdges.empty()) {
                for (const auto& [e, tol] : rescuedEdges)
                    bb.UpdateEdge(e, tol);
                for (const auto& [e, tol] : rescuedEdges)
                    for (TopExp_Explorer ev(e, TopAbs_VERTEX); ev.More(); ev.Next())
                        bb.UpdateVertex(TopoDS::Vertex(ev.Current()), kXtTolerance * scale);
                BRepLib::UpdateTolerances(compound, false);
                // This runs after the broken faces are gone: UpdateTolerances raises an edge to the tolerance of its
                // vertices, and those stay inflated while a face that is about to be removed still touches them
                // (measured: vertex and edge tolerance were equal on every edge that came back high). Edges that are
                // still above the limit after the reset are counted.
                if (std::getenv("XT_DEBUG_TRIM")) {
                    // Trial on a fresh edge (as OCCT's STEP transfer does before it touches the shared edge): the
                    // same 3D curve and this face's pcurve, no vertices of the shared edge. If the trial edge reaches
                    // a small tolerance the representations agree and the inflation comes from the vertices.
                    for (const auto& [face, edges] : rescuedFaces)
                        for (const TopoDS_Edge& e : edges) {
                            try {
                                double a, b, c0, c1;
                                const Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(e, face, a, b);
                                TopLoc_Location l3, ls;
                                const Handle(Geom_Curve) c = BRep_Tool::Curve(e, l3, c0, c1);
                                const Handle(Geom_Surface) s = BRep_Tool::Surface(face, ls);
                                if (pc.IsNull() || c.IsNull() || s.IsNull() || !l3.IsIdentity())
                                    continue;
                                TopoDS_Edge trial = BRepBuilderAPI_MakeEdge(c, c0, c1).Edge();
                                bb.UpdateEdge(trial, pc, s, ls, 1e-7);
                                bb.Range(trial, s, ls, a, b);
                                BRepLib::SameParameter(trial, 1e-4 * scale);
                                const TopoDS_Vertex vf = TopExp::FirstVertex(e), vl = TopExp::LastVertex(e);
                                const gp_Pnt p0 = c->Value(c0), p1 = c->Value(c1);
                                const double gapA = std::max(BRep_Tool::Pnt(vf).Distance(p0), BRep_Tool::Pnt(vl).Distance(p1));
                                const double gapB = std::max(BRep_Tool::Pnt(vf).Distance(p1), BRep_Tool::Pnt(vl).Distance(p0));
                                {
                                    // OCCT's own check of every representation of the shared edge.
                                    ShapeAnalysis_Edge analysis;
                                    double allDeviation = 0, faceDeviation = 0;
                                    analysis.CheckSameParameter(e, allDeviation);
                                    analysis.CheckSameParameter(e, face, faceDeviation);
                                    {
                                        TopTools_IndexedDataMapOfShapeListOfShape vertexEdges;
                                        TopExp::MapShapesAndAncestors(compound, TopAbs_VERTEX, TopAbs_EDGE, vertexEdges);
                                        const TopoDS_Vertex vf = TopExp::FirstVertex(e);
                                        if (vertexEdges.Contains(vf)) {
                                            std::fprintf(stderr, "edges at first vertex (tol %.4g):", BRep_Tool::Tolerance(vf));
                                            for (const TopoDS_Shape& other : vertexEdges.FindFromKey(vf)) {
                                                double od = 0;
                                                ShapeAnalysis_Edge oa;
                                                oa.CheckSameParameter(TopoDS::Edge(other), od);
                                                std::fprintf(stderr, " [tol %.4g dev %.4g%s]", BRep_Tool::Tolerance(TopoDS::Edge(other)), od,
                                                             other.IsSame(e) ? " THIS" : "");
                                            }
                                            std::fprintf(stderr, "\n");
                                        }
                                    }
                                    {
                                        // Exercise the entire isolated wire, including both seam branches.
                                        BRepBuilderAPI_Copy copy(face, true, false);
                                        const TopoDS_Shape& isolated = copy.Shape();
                                        const TopoDS_Edge copiedEdge = TopoDS::Edge(copy.ModifiedShape(e));
                                        const double floor = kXtTolerance * scale;
                                        ShapeFix_ShapeTolerance tolerances;
                                        tolerances.SetTolerance(isolated, floor, TopAbs_EDGE);
                                        tolerances.SetTolerance(isolated, floor, TopAbs_VERTEX);
                                        BRepLib::SameParameter(isolated, 1e-4 * scale, true);
                                        BRepLib::UpdateTolerances(isolated, false);
                                        const TopoDS_Vertex first = TopExp::FirstVertex(copiedEdge);
                                        const TopoDS_Vertex last = TopExp::LastVertex(copiedEdge);
                                        if (!first.IsNull() && !last.IsNull())
                                            std::fprintf(stderr, "isolated face: edge %.4g -> %.4g, vertices %.4g/%.4g, valid %d\n",
                                                         floor, BRep_Tool::Tolerance(copiedEdge), BRep_Tool::Tolerance(first),
                                                         BRep_Tool::Tolerance(last), BRepCheck_Analyzer(isolated).IsValid() ? 1 : 0);
                                    }
                                    std::fprintf(stderr, "shared edge deviation all=%.4g onThisFace=%.4g closed=%d sameParam=%d sameRange=%d vertexTol=%.4g/%.4g\n",
                                                 allDeviation, faceDeviation, BRep_Tool::IsClosed(e, face) ? 1 : 0,
                                                 BRep_Tool::SameParameter(e) ? 1 : 0, BRep_Tool::SameRange(e) ? 1 : 0,
                                                 BRep_Tool::Tolerance(TopExp::FirstVertex(e)), BRep_Tool::Tolerance(TopExp::LastVertex(e)));
                                }
                                std::fprintf(stderr,
                                             "trial edge tol %.4g | shared edge tol %.4g | vertex gap %.4g | 3d range [%.5g,%.5g] pcurve range [%.5g,%.5g] | same-parameter %d\n",
                                             BRep_Tool::Tolerance(trial), BRep_Tool::Tolerance(e), std::min(gapA, gapB), c0, c1, a, b,
                                             BRep_Tool::SameParameter(trial) ? 1 : 0);
                            } catch (const Standard_Failure&) {
                                std::fprintf(stderr, "trial edge exception\n");
                            }
                        }
                }
                for (const auto& [face, edges] : rescuedFaces) {
                    bool stillHigh = false;
                    for (const TopoDS_Edge& e : edges)
                        stillHigh = stillHigh || BRep_Tool::Tolerance(e) > limit;
                    if (!stillHigh)
                        continue;
                    ++res.stats.facesSkippedBySurface["  trim-kept-tolerance-still-high"];
                    setOutcomeOrigin(face, "-tol-high");
                }
            }
        }
    } catch (const Standard_Failure&) {
    }
    out.shape = compound;
    m_outcomes = nullptr;
    assignColours(out, body);
    res.bodies.push_back(std::move(out));
}

// ---- spike: XT topology assembled bottom-up with the OCCT 8 BRepGraph editor ---------------------------
//
// XT -> OCCT equivalence exercised here (see research/parasolid/PARASOLID_XT_OCCT_EQUIVALENCE.md):
//   VERTEX -> VertexOps::Add(point, tolerance)            EDGE -> EdgeOps::Add(v0, v1, curve, first, last, tol)
//   FIN    -> CoEdge (edge + parity orientation + 2D curve on the face's surface)
//   LOOP   -> Wire (ordered CoEdges, connected)           FACE -> Face (surface, outer wire, inner wires)
//   SHELL  -> Shell (face + parity orientation)           REGION 'S' -> Solid
// Edge curves and ranges come from the existing edge builder; the 2D curves are projections.
void Builder::buildBodyViaGraph(const Node& body) {
    BuiltBody out;
    out.index = body.index;
    out.name = displayName(body, "Body");
    out.bodyType = static_cast<int>(num(body, "BODY", "body_type"));
    ++res.stats.bodies;

    BRepGraph graph;
    struct GEdge {
        BRepGraph_EdgeId id;
        BRepGraph_VertexId v0, v1;
        Handle(Geom_Curve) curve;
        double first = 0, last = 0;
    };
    std::unordered_map<const TopoDS_TShape*, BRepGraph_VertexId> graphVertices;
    std::unordered_map<int64_t, GEdge> graphEdges;
    auto fail = [&](const char* why) {
        ++res.stats.facesSkippedBySurface[std::string("graph-") + why];
        return false;
    };
    auto vertexId = [&](const TopoDS_Vertex& v) {
        const auto found = graphVertices.find(v.TShape().get());
        if (found != graphVertices.end())
            return found->second;
        const BRepGraph_VertexId id = graph.Editor().Vertices().Add(BRep_Tool::Pnt(v), BRep_Tool::Tolerance(v));
        graphVertices.emplace(v.TShape().get(), id);
        return id;
    };
    auto edgeRecord = [&](const Node& en) -> const GEdge* {
        const auto found = graphEdges.find(en.index);
        if (found != graphEdges.end())
            return &found->second;
        const TopoDS_Edge e0 = edgeFor(en);
        if (e0.IsNull())
            return nullptr;
        TopLoc_Location loc;
        double a, b;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(e0, loc, a, b);
        if (curve.IsNull())
            return nullptr;
        if (!loc.IsIdentity())
            curve = Handle(Geom_Curve)::DownCast(curve->Transformed(loc.Transformation()));
        TopoDS_Vertex v0, v1;
        TopExp::Vertices(e0, v0, v1);
        if (v0.IsNull() || v1.IsNull())
            return nullptr;
        GEdge rec;
        rec.curve = curve;
        rec.first = a;
        rec.last = b;
        rec.v0 = vertexId(v0);
        rec.v1 = vertexId(v1);
        rec.id = graph.Editor().Edges().Add(rec.v0, rec.v1, curve, a, b, BRep_Tool::Tolerance(e0));
        if (!rec.id.IsValid())
            return nullptr;
        return &graphEdges.emplace(en.index, std::move(rec)).first->second;
    };

    // One XT face -> one graph face. Returns false (with a counted reason) when it cannot be built.
    auto addFace = [&](BRepGraph_ShellId shell, const Node& face, TopAbs_Orientation use) {
        ++res.stats.faces;
        const int64_t surfIndex = ptr(face, "FACE", "surface");
        SurfaceInfo blended;
        const Node* surfNode = node(surfIndex);
        const bool isBlend = surfNode && nameOf(*surfNode) == "BLENDED_EDGE";
        if (isBlend)
            blended = blendSurfaceForFace(*surfNode, face);
        const SurfaceInfo& si = isBlend ? blended : surfaceFor(surfIndex);
        if (!si.ok)
            return fail(isBlend ? "blend-surface" : "surface");
        const int xtNormal = signOf(face, "FACE", "sense") * si.senseSign;
        const bool flipWire = (xtNormal * si.flip) < 0; // loops are walked against OCCT's natural normal
        bool faceReversed = flipWire;

        struct Use {
            const GEdge* edge;
            TopAbs_Orientation ori;
            int64_t xtEdge;
            BRepGraph_CoEdgeId coedge;
            Handle(Geom2d_Curve) p2d;
        };
        std::vector<std::vector<Use>> loops;
        for (const Node* loop : chain(node(ptr(face, "FACE", "loop")), "LOOP", "next")) {
            const Node* start = node(ptr(*loop, "LOOP", "fin"));
            if (!start)
                continue;
            std::vector<const Node*> ring;
            std::unordered_set<int64_t> seen;
            const Node* fin = start;
            for (; fin && seen.insert(fin->index).second; fin = node(ptr(*fin, "FIN", "forward")))
                ring.push_back(fin);
            if (fin != start)
                return fail("fin-ring-open");
            std::vector<Use> uses;
            for (const Node* fn : ring) {
                const Node* en = node(ptr(*fn, "FIN", "edge"));
                if (!en)
                    continue; // isolated-vertex loop
                if (m_microEdges.count(en->index))
                    return fail("micro-edge");
                const GEdge* rec = edgeRecord(*en);
                if (!rec)
                    return fail("edge");
                TopAbs_Orientation o = num(*fn, "FIN", "sense") == '+' ? TopAbs_FORWARD : TopAbs_REVERSED;
                if (flipWire)
                    o = o == TopAbs_FORWARD ? TopAbs_REVERSED : TopAbs_FORWARD;
                uses.push_back({rec, o, en->index, {}, {}});
            }
            if (uses.empty())
                continue;
            if (flipWire)
                std::reverse(uses.begin(), uses.end());
            loops.push_back(std::move(uses));
        }
        if (loops.empty())
            return fail("no-loops");
        const bool periodicU = si.surface->IsUPeriodic();
        const bool periodicV = si.surface->IsVPeriodic();
        const bool periodic = periodicU || periodicV;
        const double periodU = periodicU ? si.surface->UPeriod() : 0.0;
        const double periodV = periodicV ? si.surface->VPeriod() : 0.0;
        if (si.surface->IsUClosed() || si.surface->IsVClosed()) {
            // OCCT wants a periodic face to be a topological disk in (u, v): an explicit seam edge, used twice by
            // the same face. XT does not store one; record whether this face has any.
            std::unordered_map<int64_t, int> uses;
            bool seam = false;
            for (const auto& l : loops)
                for (const Use& u : l)
                    seam = seam || ++uses[u.xtEdge] > 1;
            ++res.stats.facesSkippedBySurface[std::string(seam ? "graph-closed-surface-face-with-seam " : "graph-closed-surface-face-no-seam ") + si.kind];
            ++res.stats.facesSkippedBySurface["graph-closed-surface-loops-" + std::to_string(loops.size())];
        }

        // 2D curves: the projection of each edge curve onto the face surface (no face is needed for that).
        auto startUV = [](const Use& u) { return u.p2d->Value(u.ori == TopAbs_REVERSED ? u.edge->last : u.edge->first); };
        auto endUV = [](const Use& u) { return u.p2d->Value(u.ori == TopAbs_REVERSED ? u.edge->first : u.edge->last); };
        auto shift = [](Use& u, double du, double dv) {
            if (du != 0 || dv != 0) {
                u.p2d = Handle(Geom2d_Curve)::DownCast(u.p2d->Copy());
                u.p2d->Translate(gp_Vec2d(du, dv));
            }
        };
        // A periodic surface projects each edge into the first period; along a loop the 2D curves must be
        // continuous, so each one is shifted by whole periods to start where the previous one ended (the first
        // one to `target` when given).
        auto unwrap = [&](std::vector<Use>& uses, const gp_Pnt2d* target) {
            if (!periodic)
                return;
            for (size_t i = 0; i < uses.size(); ++i) {
                const gp_Pnt2d want = i ? endUV(uses[i - 1]) : (target ? *target : startUV(uses[i]));
                const gp_Pnt2d have = startUV(uses[i]);
                shift(uses[i], periodicU ? std::round((want.X() - have.X()) / periodU) * periodU : 0.0,
                      periodicV ? std::round((want.Y() - have.Y()) / periodV) * periodV : 0.0);
            }
        };
        std::unordered_map<int64_t, int> seamUse;
        for (auto& uses : loops) {
            for (Use& u : uses) {
                try {
                    double reached = 1e-3 * scale;
                    u.p2d = GeomProjLib::Curve2d(u.edge->curve, u.edge->first, u.edge->last, si.surface, reached);
                } catch (const Standard_Failure&) {
                }
                if (u.p2d.IsNull())
                    return fail("pcurve-projection");
                // Qualification: the 2D curve must lie on the 3D edge at both ends.
                for (const double t : {u.edge->first, u.edge->last}) {
                    const gp_Pnt2d q = u.p2d->Value(t);
                    if (si.surface->Value(q.X(), q.Y()).Distance(u.edge->curve->Value(t)) > 1e-2 * scale)
                        return fail("pcurve-mismatch");
                }
                if (++seamUse[u.xtEdge] == 2 && periodic) {
                    // An edge used twice by one face is a seam: its second curve is the first one offset by a period.
                    const bool inU = periodicU;
                    const gp_Pnt2d q = u.p2d->Value(0.5 * (u.edge->first + u.edge->last));
                    const double c = inU ? q.X() : q.Y();
                    const double period = inU ? periodU : periodV;
                    const double d = c < 0.5 * period ? period : -period;
                    shift(u, inU ? d : 0.0, inU ? 0.0 : d);
                    ++res.stats.facesSkippedBySurface["graph-seam-second-pcurve"];
                }
            }
            unwrap(uses, nullptr);
        }

        // A loop that does not close in (u, v) winds around the surface (the end of a full cylinder or cone).
        auto winding = [&](const std::vector<Use>& uses) {
            const gp_Pnt2d a = startUV(uses.front()), b = endUV(uses.back());
            return gp_Vec2d(b.X() - a.X(), b.Y() - a.Y());
        };
        size_t windingLoops = 0;
        for (const auto& uses : loops)
            if (winding(uses).Magnitude() > 1e-6)
                ++windingLoops;
        res.stats.facesSkippedBySurface["graph-wrapping-loop " + si.kind] += windingLoops;

        // Seam: two loops winding in opposite directions around U are joined by an iso-parametric seam edge,
        // used twice (once at each side of the period), into the single wire OCCT expects.
        struct SeamUse {
            BRepGraph_CoEdgeId coedge;
            Handle(Geom2d_Curve) p2d;
            double first = 0, last = 0;
        };
        std::vector<SeamUse> seams;
        std::vector<Use> merged;
        if (windingLoops == 2 && loops.size() == 2 && periodicU) {
            std::vector<Use>& A = loops[0];
            std::vector<Use>& B = loops[1];
            const gp_Vec2d wA = winding(A), wB = winding(B);
            const bool uWinding = std::abs(std::abs(wA.X()) - periodU) < 1e-6 * periodU && std::abs(std::abs(wB.X()) - periodU) < 1e-6 * periodU &&
                                  std::abs(wA.Y()) < 1e-6 && std::abs(wB.Y()) < 1e-6 && wA.X() * wB.X() < 0;
            if (!uWinding) {
                ++res.stats.facesSkippedBySurface["graph-seam-skipped-not-u-opposed"];
            } else {
                const double u0 = startUV(A.front()).X();
                const double sA = wA.X() > 0 ? 1.0 : -1.0;
                size_t rotation = B.size();
                for (size_t i = 0; i < B.size() && rotation == B.size(); ++i) {
                    const double d = (startUV(B[i]).X() - u0) / periodU;
                    if (std::abs(d - std::round(d)) * periodU < 1e-5)
                        rotation = i;
                }
                if (rotation == B.size()) {
                    ++res.stats.facesSkippedBySurface["graph-seam-skipped-no-aligned-vertex"];
                } else {
                    std::rotate(B.begin(), B.begin() + rotation, B.end());
                    const gp_Pnt2d target(u0 + sA * periodU, startUV(B.front()).Y());
                    unwrap(B, &target);
                    const double vA = startUV(A.front()).Y(), vB = startUV(B.front()).Y();
                    const BRepGraph_VertexId nodeA = A.front().ori == TopAbs_FORWARD ? A.front().edge->v0 : A.front().edge->v1;
                    const BRepGraph_VertexId nodeB = B.front().ori == TopAbs_FORWARD ? B.front().edge->v0 : B.front().edge->v1;
                    const Handle(Geom_Curve) iso = si.surface->UIso(u0);
                    const gp_Pnt pointA = si.surface->Value(u0, vA), pointB = si.surface->Value(u0, vB);
                    const gp_Pnt startA = A.front().edge->curve->Value(A.front().ori == TopAbs_REVERSED ? A.front().edge->last : A.front().edge->first);
                    const gp_Pnt startB = B.front().edge->curve->Value(B.front().ori == TopAbs_REVERSED ? B.front().edge->last : B.front().edge->first);
                    if (iso.IsNull() || std::abs(vA - vB) < 1e-9 || pointA.Distance(startA) > 1e-2 * scale || pointB.Distance(startB) > 1e-2 * scale) {
                        ++res.stats.facesSkippedBySurface["graph-seam-skipped-geometry"];
                    } else {
                        const bool aLow = vA < vB;
                        const BRepGraph_EdgeId seamEdge = graph.Editor().Edges().Add(aLow ? nodeA : nodeB, aLow ? nodeB : nodeA, iso, std::min(vA, vB), std::max(vA, vB), kXtTolerance * scale);
                        const BRepGraph_CoEdgeId c1 = graph.Editor().CoEdges().Add(seamEdge, aLow ? TopAbs_FORWARD : TopAbs_REVERSED);
                        const BRepGraph_CoEdgeId c2 = graph.Editor().CoEdges().Add(seamEdge, aLow ? TopAbs_REVERSED : TopAbs_FORWARD);
                        if (!seamEdge.IsValid() || !c1.IsValid() || !c2.IsValid())
                            return fail("seam-edge");
                        auto line = [&](double u) { return Handle(Geom2d_Curve)(new Geom2d_Line(gp_Pnt2d(u, 0), gp_Dir2d(0, 1))); };
                        seams.push_back({c1, line(u0 + sA * periodU), std::min(vA, vB), std::max(vA, vB)});
                        seams.push_back({c2, line(u0), std::min(vA, vB), std::max(vA, vB)});
                        // Wire order: A, seam at the far side, B (shifted to start there), seam at the near side.
                        merged = A;
                        merged.push_back({nullptr, aLow ? TopAbs_FORWARD : TopAbs_REVERSED, -1, c1, {}});
                        merged.insert(merged.end(), B.begin(), B.end());
                        merged.push_back({nullptr, aLow ? TopAbs_REVERSED : TopAbs_FORWARD, -2, c2, {}});
                        ++res.stats.facesSkippedBySurface["graph-seam-inserted " + si.kind];
                    }
                }
            }
        } else if (windingLoops > 0) {
            ++res.stats.facesSkippedBySurface["graph-seam-skipped-loop-count " + std::to_string(windingLoops) + "/" + std::to_string(loops.size())];
        }

        // Outer loop first: the loop of largest absolute parametric area (seam faces have one merged loop).
        auto loopArea = [&](const std::vector<Use>& uses) {
            double area = 0;
            std::vector<gp_Pnt2d> polygon;
            for (const Use& u : uses) {
                if (!u.edge)
                    continue;
                const bool rev = u.ori == TopAbs_REVERSED;
                for (int k = 0; k < 8; ++k) {
                    const double f = k / 8.0;
                    polygon.push_back(u.p2d->Value(rev ? u.edge->last - (u.edge->last - u.edge->first) * f : u.edge->first + (u.edge->last - u.edge->first) * f));
                }
            }
            for (size_t i = 0; i < polygon.size(); ++i) {
                const gp_Pnt2d& p = polygon[i];
                const gp_Pnt2d& q = polygon[(i + 1) % polygon.size()];
                area += 0.5 * (p.X() * q.Y() - q.X() * p.Y());
            }
            return area;
        };
        std::vector<std::vector<Use>> wiresToBuild;
        if (!merged.empty()) {
            wiresToBuild.push_back(merged);
        } else {
            wiresToBuild = loops;
            size_t outerLoop = 0;
            std::vector<double> areas;
            for (const auto& l : wiresToBuild)
                areas.push_back(loopArea(l));
            for (size_t i = 1; i < areas.size(); ++i)
                if (std::abs(areas[i]) > std::abs(areas[outerLoop]))
                    outerLoop = i;
            if (!si.surface->IsUClosed() && !si.surface->IsVClosed()) {
                if (outerLoop != 0)
                    ++res.stats.facesSkippedBySurface["graph-outer-loop-not-first"];
                if (areas[outerLoop] < 0)
                    ++res.stats.orientationFlips["graph-" + si.kind];
            }
            std::swap(wiresToBuild[0], wiresToBuild[outerLoop]);
        }

        // CoEdges -> wires. The graph checks that the ordered coedges form a connected chain.
        std::vector<BRepGraph_WireId> wires;
        for (auto& uses : wiresToBuild) {
            NCollection_LinearVector<BRepGraph_CoEdgeId> coedges;
            for (Use& u : uses) {
                if (u.edge) {
                    u.coedge = graph.Editor().CoEdges().Add(u.edge->id, u.ori);
                    if (!u.coedge.IsValid())
                        return fail("coedge");
                }
                coedges.Append(u.coedge);
            }
            const auto status = graph.Editor().Wires().CheckCoEdgeOrder(coedges.ToArray1());
            using S = BRepGraph::EditorView::WireOps::CoEdgeOrderStatus;
            if (status == S::Disconnected)
                return fail("wire-disconnected");
            if (status == S::Reordered)
                ++res.stats.facesSkippedBySurface["graph-wire-reordered"];
            const BRepGraph_WireId wire = graph.Editor().Wires().Add(coedges.ToArray1());
            if (!wire.IsValid())
                return fail("wire");
            wires.push_back(wire);
        }
        NCollection_LinearVector<BRepGraph_WireId> inner;
        for (size_t i = 1; i < wires.size(); ++i)
            inner.Append(wires[i]);
        const BRepGraph_FaceId gface = graph.Editor().Faces().Add(si.surface, wires.front(), inner.ToArray1(), kXtTolerance * scale);
        if (!gface.IsValid())
            return fail("face");
        for (auto& uses : wiresToBuild) {
            for (Use& u : uses) {
                graph.Editor().CoEdges().SetFaceId(u.coedge, gface);
                if (u.edge)
                    graph.Editor().CoEdges().SetPCurve(u.coedge, u.p2d, u.edge->first, u.edge->last);
            }
        }
        for (const SeamUse& su : seams) {
            graph.Editor().CoEdges().SetFaceId(su.coedge, gface);
            graph.Editor().CoEdges().SetPCurve(su.coedge, su.p2d, su.first, su.last);
        }

        const bool reversed = faceReversed != (use == TopAbs_REVERSED);
        if (!graph.Editor().Shells().Append(shell, gface, reversed ? TopAbs_REVERSED : TopAbs_FORWARD).IsValid())
            return fail("shell-append");
        ++res.stats.facesBuilt;
        return true;
    };

    TopoDS_Compound compound;
    bb.MakeCompound(compound);
    size_t parts = 0;
    auto addShellFaces = [&](BRepGraph_ShellId shell, const Node* sh, bool solidBody, std::unordered_set<int64_t>* seen) {
        size_t built = 0;
        auto take = [&](const Node* head, const char* nextField, TopAbs_Orientation use) {
            for (const Node* f : chain(head, "FACE", nextField)) {
                if (seen && !seen->insert(f->index).second)
                    continue;
                ++out.facesTotal;
                try {
                    if (addFace(shell, *f, use)) {
                        ++out.facesBuilt;
                        ++built;
                    }
                } catch (const Standard_Failure&) {
                    ++res.stats.facesSkippedBySurface["graph-exception"];
                }
            }
        };
        take(node(ptr(*sh, "SHELL", "face")), "next", TopAbs_FORWARD);
        take(node(ptr(*sh, "SHELL", "front_face")), "next_front", solidBody ? TopAbs_REVERSED : TopAbs_FORWARD);
        return built;
    };
    std::vector<BRepGraph_NodeId> roots;
    bool anySolid = false;
    std::unordered_set<int64_t> seenFaces;
    for (const Node* region : chain(node(ptr(body, "BODY", "region")), "REGION", "next"))
        anySolid = anySolid || num(*region, "REGION", "type") == 'S';
    for (const Node* region : chain(node(ptr(body, "BODY", "region")), "REGION", "next")) {
        const bool solidRegion = num(*region, "REGION", "type") == 'S';
        if (anySolid && !solidRegion)
            continue; // the void region of a solid body repeats the faces of its solid regions
        BRepGraph_SolidId solid;
        bool solidHasShell = false;
        if (solidRegion)
            solid = graph.Editor().Solids().Add();
        for (const Node* sh : chain(node(ptr(*region, "REGION", "shell")), "SHELL", "next")) {
            const BRepGraph_ShellId shell = graph.Editor().Shells().Add();
            if (addShellFaces(shell, sh, solidRegion, solidRegion ? nullptr : &seenFaces) == 0)
                continue;
            if (solidRegion) {
                (void)graph.Editor().Solids().Append(solid, shell);
                solidHasShell = true;
            } else {
                roots.push_back(shell);
            }
        }
        if (solidRegion && solidHasShell)
            roots.push_back(solid);
    }
    try {
        const BRepGraph_Validate::Result check = BRepGraph_Validate::Perform(graph);
        res.stats.facesSkippedBySurface["graph-validate-errors"] += check.NbIssues(BRepGraph_Validate::Severity::Error);
        for (const BRepGraph_NodeId& root : roots) {
            const TopoDS_Shape shape = graph.Shapes().Reconstruct(root);
            if (!shape.IsNull()) {
                bb.Add(compound, shape);
                ++parts;
            }
        }
    } catch (const Standard_Failure&) {
        ++res.stats.facesSkippedBySurface["graph-reconstruct-exception"];
    }
    if (anySolid)
        ++res.stats.solidBodies;
    else if (parts)
        ++res.stats.sheetBodies;
    if (parts == 0)
        return;
    if (!std::getenv("XT_GRAPH_RAW")) {
        // Same finishing as the direct builder, so the two are comparable.
        try {
            BRepLib::SameParameter(compound, 1e-4 * scale, false);
            BRepLib::UpdateTolerances(compound, false);
        } catch (const Standard_Failure&) {
        }
    }
    out.shape = compound;
    res.bodies.push_back(std::move(out));
}

void Builder::run() {
    const char* maxBodies = std::getenv("XT_MAX_BODIES"); // testing aid: stop after this many bodies
    const size_t bodyLimit = maxBodies ? static_cast<size_t>(std::atoll(maxBodies)) : static_cast<size_t>(-1);
    for (const Node* body : m.nodesNamed("BODY")) {
        if (res.stats.bodies >= bodyLimit)
            break;
        if (const char* only = std::getenv("XT_ONLY_BODY"))
            if (body->index != std::atoll(only))
                continue;
        if (cancelled()) {
            res.cancelled = true;
            return;
        }
        const auto b0 = std::chrono::steady_clock::now();
        trace("begin-body", body->index);
        m_outcomes = nullptr;
        if (std::getenv("XT_GRAPH"))
            buildBodyViaGraph(*body);
        else
            buildBody(*body);
        trace("end-body", body->index);
        if (std::getenv("XT_DEBUG_SLOW")) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count();
            if (s > 2.0)
                std::fprintf(stderr, "slow body %lld %.1fs faces so far %zu\n", (long long)body->index, s, res.stats.faces);
        }
    }
    if (cancelled()) {
        res.cancelled = true;
        return;
    }
    placeInstances();
}

// Transform node -> gp_Trsf: x' = (R x + t) * s, lengths scaled to the output unit. Returns false
// for reflections and general affine maps, which a gp_Trsf cannot express.
bool Builder::transformOf(const Node* tr, gp_Trsf& out) {
    out = gp_Trsf();
    if (!tr)
        return true;
    const int flag = static_cast<int>(num(*tr, "TRANSFORM", "flag"));
    if (flag & (8 | 16))
        return false;
    double r[9];
    for (int i = 0; i < 9; ++i)
        r[i] = num(*tr, "TRANSFORM", "rotation_matrix", static_cast<size_t>(i));
    const double s = std::isnan(num(*tr, "TRANSFORM", "scale")) ? 1.0 : num(*tr, "TRANSFORM", "scale");
    if (std::getenv("XT_DEBUG_TOL") && (std::abs(s - 1.0) > 1e-9 || (flag & ~1) != 0))
        std::fprintf(stderr, "transform %lld flag=%d scale=%g\n", (long long)tr->index, flag, s);
    const double tx = num(*tr, "TRANSFORM", "translation_vector", 0) * s * scale;
    const double ty = num(*tr, "TRANSFORM", "translation_vector", 1) * s * scale;
    const double tz = num(*tr, "TRANSFORM", "translation_vector", 2) * s * scale;
    for (double v : r)
        if (std::isnan(v))
            return false;
    try {
        out.SetValues(r[0] * s, r[1] * s, r[2] * s, tx, r[3] * s, r[4] * s, r[5] * s, ty, r[6] * s, r[7] * s, r[8] * s, tz);
    } catch (const Standard_Failure&) {
        return false;
    }
    return true;
}

// Flattens ASSEMBLY/INSTANCE trees into placed bodies. Parts that no instance refers to are roots. A part
// reached again while it is still being expanded (a cycle) is refused, instance chains stop at a repeated
// node, and the number of placed occurrences is budgeted, so a damaged file cannot expand without end.
std::string Builder::attributeText(int64_t owner, const char* identifier) {
    if (!m_attributesIndexed) {
        m_attributesIndexed = true;
        const Slot ownerOf = m.slot("ATTRIBUTE", "owner");
        for (const Node* a : m.nodesNamed("ATTRIBUTE")) {
            const double o = m.value(*a, ownerOf);
            if (!std::isnan(o) && o > 0)
                m_attributesByOwner[static_cast<int64_t>(o)].push_back(a);
        }
    }
    const auto it = m_attributesByOwner.find(owner);
    if (it == m_attributesByOwner.end())
        return {};
    const Slot defOf = m.slot("ATTRIBUTE", "definition"), fieldsOf = m.slot("ATTRIBUTE", "fields"),
               identOf = m.slot("ATTRIB_DEF", "identifier"), idString = m.slot("ATT_DEF_ID", "string");
    auto text = [&](const Node* n, const Slot& s) {
        std::string out;
        if (n)
            for (size_t i = 0; i < n->valueCount; ++i) {
                const double v = m.value(*n, s, i);
                if (!std::isnan(v) && v >= 32 && v < 127)
                    out += static_cast<char>(v);
            }
        return out;
    };
    for (const Node* a : it->second) {
        const Node* def = node(static_cast<int64_t>(m.value(*a, defOf)));
        const Node* id = def ? node(static_cast<int64_t>(m.value(*def, identOf))) : nullptr;
        if (text(id, idString) != identifier || a->valueCount <= fieldsOf.offset)
            continue;
        const Node* field = node(static_cast<int64_t>(m.value(*a, fieldsOf, 0)));
        if (!field)
            continue;
        std::string out;
        const bool unicode = m.typeName(field->type) == "UNICODE_VALUES";
        for (size_t i = 0; i < field->valueCount; ++i) {
            const double v = m.file().values[field->valueOffset + i];
            if (std::isnan(v) || v <= 0)
                continue;
            const auto c = static_cast<uint32_t>(v);
            if (c < 0x80 || !unicode) {
                out += static_cast<char>(c & 0xFF);
            } else if (c < 0x800) {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                out += static_cast<char>(0xE0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }
    return {};
}

std::vector<double> Builder::attributeReals(int64_t owner, const char* identifier) {
    attributeText(owner, ""); // builds the owner index
    const auto it = m_attributesByOwner.find(owner);
    if (it == m_attributesByOwner.end())
        return {};
    const Slot defOf = m.slot("ATTRIBUTE", "definition"), fieldsOf = m.slot("ATTRIBUTE", "fields"),
               identOf = m.slot("ATTRIB_DEF", "identifier"), idString = m.slot("ATT_DEF_ID", "string");
    for (const Node* a : it->second) {
        const Node* def = node(static_cast<int64_t>(m.value(*a, defOf)));
        const Node* id = def ? node(static_cast<int64_t>(m.value(*def, identOf))) : nullptr;
        std::string idText;
        if (id)
            for (size_t i = 0; i < id->valueCount; ++i) {
                const double v = m.value(*id, idString, i);
                if (!std::isnan(v) && v >= 32 && v < 127)
                    idText += static_cast<char>(v);
            }
        if (idText != identifier || a->valueCount <= fieldsOf.offset)
            continue;
        const Node* field = node(static_cast<int64_t>(m.value(*a, fieldsOf, 0)));
        if (!field || m.typeName(field->type) != "REAL_VALUES")
            continue;
        std::vector<double> out;
        for (size_t i = 0; i < field->valueCount; ++i)
            out.push_back(m.file().values[field->valueOffset + i]);
        return out;
    }
    return {};
}

// Colours the file stores as attributes: SDL/TYSA_COLOUR on faces, SDL/TYSA_COLOUR_2 on bodies. A face colour
// wins over the body colour. Values are three reals in 0-1.
void Builder::assignColours(BuiltBody& out, const Node& body) {
    auto rgb = [](const std::vector<double>& v, Rgb& c) {
        if (v.size() < 3 || std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]))
            return false;
        c = {std::clamp(v[0], 0.0, 1.0), std::clamp(v[1], 0.0, 1.0), std::clamp(v[2], 0.0, 1.0)};
        return true;
    };
    out.hasBodyColour = rgb(attributeReals(body.index, "SDL/TYSA_COLOUR_2"), out.bodyColour) ||
                        rgb(attributeReals(body.index, "SDL/TYSA_COLOUR"), out.bodyColour);
    // Opacity: SDL/TYSA_TRANSLUCENCY is a fraction (0 = opaque), TYSA_ALPHA an integer 0-255 (255 = opaque).
    auto opacityOf = [&](int64_t owner) {
        double opacity = 1.0;
        const std::vector<double> t = attributeReals(owner, "SDL/TYSA_TRANSLUCENCY");
        if (!t.empty() && std::isfinite(t[0]) && t[0] > 0)
            opacity = std::clamp(1.0 - t[0], 0.0, 1.0);
        return opacity;
    };
    out.bodyColour.a = opacityOf(body.index);
    for (const auto& kv : out.faceOutcomes) {
        Rgb c = out.bodyColour;
        const bool hasColour = rgb(attributeReals(kv.first, "SDL/TYSA_COLOUR"), c);
        double opacity = opacityOf(kv.first);
        if (opacity >= 1.0)
            opacity = out.bodyColour.a; // no face translucency: inherit the body's
        c.a = opacity;
        if (hasColour || opacity < 1.0)
            out.faceColours[kv.first] = c;
    }
}

std::string Builder::explicitName(const Node& n) {
    for (const char* id : {"SDL/TYSA_UNAME", "SDL/TYSA_NAME"}) {
        std::string name = attributeText(n.index, id);
        while (!name.empty() && (name.back() == ' ' || name.back() == static_cast<char>(0)))
            name.pop_back();
        if (!name.empty())
            return name;
    }
    return {};
}

std::string Builder::displayName(const Node& n, const char* fallback) {
    std::string name = explicitName(n);
    return name.empty() ? std::string(fallback) + " " + std::to_string(n.index) : name;
}

void Builder::placeInstances() {
    std::unordered_map<int64_t, size_t> bodyIndex;
    for (size_t i = 0; i < res.bodies.size(); ++i)
        bodyIndex[res.bodies[i].index] = i;

    std::unordered_set<int64_t> referenced;
    for (const Node* inst : m.nodesNamed("INSTANCE"))
        if (const int64_t p = ptr(*inst, "INSTANCE", "part"))
            referenced.insert(p);

    constexpr size_t kOccurrenceBudget = 2'000'000;
    size_t unplaced = 0, missing = 0, expansions = 0;
    std::unordered_set<int64_t> activePath;
    std::vector<InstanceStep> assemblyPath; // assemblies from the top down to the part being placed
    std::function<void(const Node&, const TopLoc_Location&, int)> place = [&](const Node& part, const TopLoc_Location& loc, int depth) {
        if (cancelled()) {
            res.cancelled = true;
            return;
        }
        if (depth > 64) {
            ++missing;
            diag("assembly-depth", "assembly nesting deeper than 64 levels; the rest is ignored", part.index);
            return;
        }
        if (++expansions > kOccurrenceBudget) {
            if (expansions == kOccurrenceBudget + 1)
                diag("assembly-budget", "more than 2,000,000 occurrences; the rest of the assembly is ignored", part.index);
            ++missing;
            return;
        }
        const std::string& kind = nameOf(part);
        if (kind == "BODY") {
            const auto it = bodyIndex.find(part.index);
            if (it == bodyIndex.end()) {
                ++missing; // nothing could be built for this body
                return;
            }
            BuiltInstance inst;
            inst.body = it->second;
            inst.name = res.bodies[it->second].name;
            inst.location = loc;
            inst.path = assemblyPath;
            if (std::getenv("XT_DEBUG_INSTANCES")) {
                const gp_XYZ t = loc.Transformation().TranslationPart();
                std::fprintf(stderr, "instance body %lld faces %zu at (%.3f, %.3f, %.3f) depth %d\n",
                             static_cast<long long>(part.index), res.bodies[it->second].facesBuilt, t.X(), t.Y(), t.Z(), depth);
            }
            res.instances.push_back(std::move(inst));
        } else if (kind == "ASSEMBLY") {
            if (!activePath.insert(part.index).second) {
                ++missing;
                diag("assembly-cycle", "assembly " + std::to_string(part.index) + " contains itself; the repeated occurrence is left out", part.index);
                return;
            }
            for (const Node* inst : chain(node(ptr(part, "ASSEMBLY", "sub_instance")), "INSTANCE", "next_in_part")) {
                const Node* child = node(ptr(*inst, "INSTANCE", "part"));
                if (!child) {
                    ++missing;
                    diag("instance-part-missing", "instance " + std::to_string(inst->index) + " refers to a part that is not in the file", inst->index);
                    continue;
                }
                gp_Trsf trsf;
                if (!transformOf(node(ptr(*inst, "INSTANCE", "transform")), trsf)) {
                    ++unplaced;
                    diag("instance-transform-unsupported", "instance " + std::to_string(inst->index) + " has a reflection or general affine transform, which is not supported; the occurrence is left out", inst->index);
                    continue; // left out rather than shown in the wrong place
                }
                const bool childIsAssembly = nameOf(*child) == "ASSEMBLY";
                if (childIsAssembly)
                    assemblyPath.push_back({inst->index, displayName(*child, "Assembly")});
                place(*child, loc * TopLoc_Location(trsf), depth + 1);
                if (childIsAssembly)
                    assemblyPath.pop_back();
                if (res.cancelled)
                    break;
            }
            activePath.erase(part.index);
        } else {
            ++missing;
        }
    };

    for (const Node* assembly : m.nodesNamed("ASSEMBLY")) {
        if (res.cancelled)
            break;
        if (!referenced.count(assembly->index)) {
            // An unnamed top-level assembly adds no level: the file node above it already names the model.
            assemblyPath.clear();
            if (const std::string topName = explicitName(*assembly); !topName.empty())
                assemblyPath.push_back({-assembly->index, topName});
            place(*assembly, TopLoc_Location(), 0);
            assemblyPath.clear();
        }
    }
    for (const Node* body : m.nodesNamed("BODY")) {
        if (res.cancelled)
            break;
        if (!referenced.count(body->index))
            place(*body, TopLoc_Location(), 0);
    }
    res.stats.instancesWithUnsupportedTransform = unplaced;
    res.stats.missingOccurrences = missing;
}

} // namespace

BuildResult buildShapes(const Model& model, const BuildOptions& options) {
    BuildResult result;
    Builder(model, options, result).run();
    return result;
}

} // namespace Xt
