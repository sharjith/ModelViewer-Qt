// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "XtSceneImporter.h"

#include "BRepToAssimpConverter.h"
#include "FaceFallbackTriangulator.h"
#include "Logger.h"
#include "XtImport.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Quantity_Color.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <assimp/scene.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Xt {

namespace {

class MeshCancellation : public Message_ProgressIndicator
{
public:
    explicit MeshCancellation(const SceneImportOptions& options) : m_options(options) {}

    Standard_Boolean UserBreak() override { return m_options.cancelled && m_options.cancelled(); }
    void Show(const Message_ProgressScope&, const Standard_Boolean) override {}

private:
    const SceneImportOptions& m_options;
};

// Meshes one shape. C++ exceptions are caught here; an access violation is caught by the caller.
bool meshBody(const TopoDS_Shape& shape, double deflection, double angle, const SceneImportOptions& options, bool relative)
{
    try {
        IMeshTools_Parameters params;
        params.Deflection = deflection;
        params.Angle = angle;
        params.Relative = relative;
        params.InParallel = false; // bodies are meshed in parallel instead
        params.AllowQualityDecrease = true;
        Handle(Message_ProgressIndicator) progress = new MeshCancellation(options);
        BRepMesh_IncrementalMesh mesher(shape, params, progress->Start());
        return true;
    } catch (...) {
        return false;
    }
}

// No C++ objects with destructors in this function, so structured exception handling is allowed.
bool meshBodyGuarded(const TopoDS_Shape* shape, double deflection, double angle, const SceneImportOptions& options,
                     bool relative = true)
{
#ifdef _MSC_VER
    __try {
        return meshBody(*shape, deflection, angle, options, relative);
    } __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
        return false;
    }
#else
    return meshBody(*shape, deflection, angle, options, relative);
#endif
}

bool hasUsableTriangulation(const TopoDS_Face& face, const Handle(Poly_Triangulation)& triangles,
                            const TopLoc_Location& location)
{
    if (triangles.IsNull() || triangles->NbTriangles() == 0)
        return false;

    // An incorrectly wrapped trim can produce triangles far beyond its reconstructed edges.
    Bnd_Box triangleBox, edgeBox;
    for (int i = 1; i <= triangles->NbNodes(); ++i)
        triangleBox.Add(triangles->Node(i).Transformed(location.Transformation()));
    if (triangleBox.IsVoid() || triangleBox.IsOpen())
        return false;
    for (TopExp_Explorer edge(face, TopAbs_EDGE); edge.More(); edge.Next())
        BRepBndLib::AddOptimal(edge.Current(), edgeBox, false, false);
    if (edgeBox.IsVoid())
        return true;
    if (edgeBox.IsOpen())
        return false;

    double x0, y0, z0, x1, y1, z1;
    edgeBox.Get(x0, y0, z0, x1, y1, z1);
    edgeBox.Enlarge(0.5 * std::max({x1 - x0, y1 - y0, z1 - z0}) + 0.01);
    triangleBox.Get(x0, y0, z0, x1, y1, z1);
    return !edgeBox.IsOut(gp_Pnt(x0, y0, z0)) && !edgeBox.IsOut(gp_Pnt(x1, y1, z1));
}

TopoDS_Shape buildDisplayFallback(const BuiltBody& body, const SceneImportOptions& options, size_t& recovered)
{
    if (body.displayCandidates.empty())
        return {};
    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(body.shape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    TopoDS_Compound display;
    BRep_Builder builder;
    builder.MakeCompound(display);
    for (const TopoDS_Face& candidate : body.displayCandidates) {
        if (options.cancelled && options.cancelled())
            return {};
        const auto boundary = FaceFallbackTriangulator::captureBoundary(candidate, edgeToFaces);
        // XT viewer coordinates are in millimetres. Reject a boundary that the
        // fallback cannot project to its support within 0.05 mm.
        const auto triangles = FaceFallbackTriangulator::triangulate(candidate, boundary, nullptr, 0.05);
        if (!hasUsableTriangulation(candidate, triangles, TopLoc_Location()))
            continue;
        TopoDS_Face meshOnly;
        builder.MakeFace(meshOnly, triangles);
        meshOnly.Orientation(candidate.Orientation());
        builder.Add(display, meshOnly);
        ++recovered;
    }
    return recovered ? display : TopoDS_Shape();
}

struct FaceMeshRecovery {
    size_t recovered = 0;
    size_t dropped = 0;
};

FaceMeshRecovery recoverUntriangulatedFaces(BuiltBody& built, double deflection, double angle,
                                           const SceneImportOptions& options, bool trace)
{
    TopoDS_Shape& body = built.shape;
    const auto bodyIndex = static_cast<long long>(built.index);
    const bool traceBodyMesh = trace && qEnvironmentVariableIsSet("XT_ONLY_BODY");
    const int traceSharedFace = trace ? qEnvironmentVariableIntValue("XT_TRACE_SHARED_FACE") : 0;
    std::vector<TopoDS_Face> usable;
    std::vector<std::pair<TopoDS_Face, size_t>> failed;
    size_t faceNumber = 0;
    for (TopExp_Explorer ex(body, TopAbs_FACE); ex.More(); ex.Next()) {
        if (options.cancelled && options.cancelled())
            return {};
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        ++faceNumber;
        TopLoc_Location location;
        const auto& triangles = BRep_Tool::Triangulation(face, location);
        if (traceSharedFace > 0) {
            const auto source = built.tracedFaceNodes.find(face.TShape().get());
            if (source != built.tracedFaceNodes.end() && source->second == traceSharedFace) {
                TopTools_IndexedDataMapOfShapeListOfShape adjacent;
                TopExp::MapShapesAndUniqueAncestors(body, TopAbs_EDGE, TopAbs_FACE, adjacent);
                TopTools_IndexedMapOfShape faceEdges;
                TopExp::MapShapes(face, TopAbs_EDGE, faceEdges);
                for (int i = 1; i <= faceEdges.Extent(); ++i) {
                    const auto& edge = TopoDS::Edge(faceEdges(i));
                    std::fprintf(stderr, "XT shared face=%d edge=%d seam=%d adjacent",
                                 traceSharedFace, i, BRep_Tool::IsClosed(edge, face));
                    for (const auto& neighbour : adjacent.FindFromKey(edge)) {
                        if (neighbour.IsSame(face))
                            continue;
                        const auto other = built.tracedFaceNodes.find(neighbour.TShape().get());
                        std::fprintf(stderr, " %lld", (long long)(other == built.tracedFaceNodes.end() ? 0 : other->second));
                    }
                    std::fprintf(stderr, "\n");
                    double first, last;
                    const auto trim = BRep_Tool::CurveOnSurface(edge, face, first, last);
                    if (!trim.IsNull()) {
                        const auto a = trim->Value(first), b = trim->Value(last);
                        double c0, c1;
                        BRep_Tool::Curve(edge, c0, c1);
                        std::fprintf(stderr, "XT shared trim face=%d edge=%d t=[%.9g,%.9g] c=[%.9g,%.9g]"
                                     " uv=(%.9g,%.9g)->(%.9g,%.9g) tol=%.6g\n",
                                     traceSharedFace, i, first, last, c0, c1,
                                     a.X(), a.Y(), b.X(), b.Y(), BRep_Tool::Tolerance(edge));
                    }
                }
            }
        }
        if (traceBodyMesh) {
            const auto source = built.tracedFaceNodes.find(face.TShape().get());
            const auto faceNode = static_cast<long long>(source == built.tracedFaceNodes.end() ? 0 : source->second);
            size_t wires = 0;
            for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next())
                ++wires;
            std::fprintf(stderr, "XT body mesh body=%lld face=%zu node=%lld wires=%zu triangles=%d\n",
                         bodyIndex, faceNumber, faceNode, wires, triangles.IsNull() ? 0 : triangles->NbTriangles());
            Bnd_Box meshBounds;
            double largestTriangleArea = 0;
            gp_Pnt largestTriangleCentre;
            if (!triangles.IsNull()) {
                for (int i = 1; i <= triangles->NbTriangles(); ++i) {
                    int a, b, c;
                    triangles->Triangle(i).Get(a, b, c);
                    for (int n : {a, b, c})
                        meshBounds.Add(triangles->Node(n).Transformed(location.Transformation()));
                    const gp_Pnt p = triangles->Node(a);
                    const double area = 0.5 * gp_Vec(p, triangles->Node(b)).CrossMagnitude(gp_Vec(p, triangles->Node(c)));
                    if (area > largestTriangleArea) {
                        largestTriangleArea = area;
                        largestTriangleCentre = gp_Pnt((p.XYZ() + triangles->Node(b).XYZ() + triangles->Node(c).XYZ()) / 3);
                    }
                }
            }
            if (!meshBounds.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                meshBounds.Get(x0, y0, z0, x1, y1, z1);
                std::fprintf(stderr, "XT body mesh bounds node=%lld min=(%.6g,%.6g,%.6g) max=(%.6g,%.6g,%.6g) max-triangle-area=%.6g\n",
                             faceNode, x0, y0, z0, x1, y1, z1, largestTriangleArea);
                if (faceNode == traceSharedFace && largestTriangleArea > 0) {
                    try {
                        const auto support = BRep_Tool::Surface(face);
                        if (!support.IsNull()) {
                            const gp_Pnt centre = largestTriangleCentre.Transformed(location.Transformation());
                            GeomAPI_ProjectPointOnSurf projection(centre, support);
                            if (projection.NbPoints())
                                std::fprintf(stderr, "XT body mesh largest triangle node=%lld centre=(%.6g,%.6g,%.6g) support-gap=%.6g\n",
                                             faceNode, centre.X(), centre.Y(), centre.Z(), projection.LowerDistance());
                        }
                    } catch (const Standard_Failure&) {
                    }
                }
            }
        }
        if (hasUsableTriangulation(face, triangles, location))
            usable.push_back(face);
        else
            failed.emplace_back(face, faceNumber);
    }
    if (failed.empty())
        return {};

    // Capture boundaries before the per-face OCCT retry can change shared-edge discretizations.
    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    for (const TopoDS_Face& face : usable)
        TopExp::MapShapesAndAncestors(face, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    BRep_Builder bb;
    FaceMeshRecovery result;
    std::vector<TopoDS_Face> bad;
    const TopTools_IndexedDataMapOfShapeListOfShape noNeighbourPolygons;
    for (const auto& [face, ordinal] : failed) {
        if (options.cancelled && options.cancelled())
            return {};
        try {
            const auto source = built.tracedFaceNodes.find(face.TShape().get());
            const auto faceNode = static_cast<long long>(source == built.tracedFaceNodes.end() ? 0 : source->second);
            const auto boundary = FaceFallbackTriangulator::captureBoundary(face, edgeToFaces);
            std::string failure;
            auto triangles = FaceFallbackTriangulator::triangulate(face, boundary, &failure);
            if (triangles.IsNull() && failure.rfind("boundary constraints cross each other", 0) == 0) {
                // A neighbour's tessellation can introduce a crossing when projected to this face.
                // Retry from the actual edge curves, with the same surface-distance limit as display recovery.
                const auto curveBoundary = FaceFallbackTriangulator::captureBoundary(face, noNeighbourPolygons);
                std::string curveFailure;
                triangles = FaceFallbackTriangulator::triangulate(face, curveBoundary, &curveFailure, 0.05);
                if (trace)
                    std::fprintf(stderr, "XT mesh edge-curve retry body=%lld face=%zu node=%lld: %s\n",
                                 bodyIndex, ordinal, faceNode,
                                 triangles.IsNull() ? curveFailure.c_str() : "triangulated");
                if (triangles.IsNull()) {
                    // Projection can choose the wrong branch on a spline. Use the reconciled
                    // per-face UV curves directly and check the same 3D surface-distance limit.
                    const auto trimBoundary = FaceFallbackTriangulator::captureSurfaceTrims(face);
                    std::string trimFailure;
                    const BRepAdaptor_Surface surface(face, false);
                    if (surface.IsUPeriodic() || surface.IsVPeriodic()) {
                        triangles = FaceFallbackTriangulator::triangulate(face, trimBoundary, &trimFailure, 0.05, false);
                        if (trace)
                            std::fprintf(stderr, "XT mesh stored-chart retry body=%lld face=%zu node=%lld: %s\n",
                                         bodyIndex, ordinal, faceNode, triangles.IsNull() ? trimFailure.c_str() : "triangulated");
                    }
                    if (triangles.IsNull())
                        triangles = FaceFallbackTriangulator::triangulate(face, trimBoundary, &trimFailure, 0.05);
                    if (trace)
                        std::fprintf(stderr, "XT mesh surface-trim retry body=%lld face=%zu node=%lld: %s\n",
                                     bodyIndex, ordinal, faceNode,
                                     triangles.IsNull() ? trimFailure.c_str() : "triangulated");
                }
            }
            // The fallback returns points in body coordinates. OCCT stores them in face coordinates.
            if (hasUsableTriangulation(face, triangles, TopLoc_Location())) {
                if (!face.Location().IsIdentity()) {
                    const gp_Trsf toLocal = face.Location().Transformation().Inverted();
                    for (int i = 1; i <= triangles->NbNodes(); ++i)
                        triangles->SetNode(i, triangles->Node(i).Transformed(toLocal));
                }
                bb.UpdateFace(face, triangles);
                ++result.recovered;
                continue;
            }
            if (trace) {
                const BRepAdaptor_Surface surface(face, false);
                std::fprintf(stderr, "XT mesh boundary retry body=%lld face=%zu node=%lld surface=%d loops=%zu: %s\n",
                             bodyIndex, ordinal, faceNode, static_cast<int>(surface.GetType()),
                             boundary.loops.size(),
                             failure.empty() ? "unusable triangulation" : failure.c_str());
            }
        } catch (const Standard_Failure&) {
        } catch (const std::exception&) {
        }
        bad.push_back(face);
    }
    std::vector<TopoDS_Face> omitted;
    for (const TopoDS_Face& face : bad) {
        if (options.cancelled && options.cancelled())
            return {};
        TopLoc_Location location;
        const auto& original = BRep_Tool::Triangulation(face, location);
        if ((original.IsNull() || original->NbTriangles() == 0) &&
            meshBodyGuarded(&face, deflection, angle, options)) {
            const auto& triangles = BRep_Tool::Triangulation(face, location);
            if (hasUsableTriangulation(face, triangles, location))
                continue;
        }
        omitted.push_back(face);
    }

    // Remove omissions before the shared converter can retry them without the XT meshing guard.
    if (omitted.empty())
        return result;
    std::vector<TopoDS_Shape> shells;
    for (TopExp_Explorer ex(body, TopAbs_SHELL); ex.More(); ex.Next())
        shells.push_back(ex.Current());
    for (TopoDS_Shape& shell : shells) {
        shell.Free(Standard_True);
        for (const TopoDS_Face& face : omitted)
            bb.Remove(shell, face);
    }
    result.dropped = omitted.size();
    return result;
}


// ---- assembly tree --------------------------------------------------------------------------
// The converter returns one flat child of the root per placed part, with the placement already applied to the
// geometry. This moves each child under group nodes named after the XT assemblies that contain it (identity
// transforms, so nothing moves) and removes the instance tag from the node names.
constexpr char kInstanceTag = '';

struct TreeGroup {
    std::string name;
    std::vector<std::pair<TreeGroup*, aiNode*>> children; // exactly one of the pair is set
    std::map<int64_t, std::unique_ptr<TreeGroup>> byKey;
};

aiNode* materialize(const TreeGroup& g, aiNode* parent) {
    auto* node = new aiNode();
    node->mName = aiString(g.name);
    node->mParent = parent;
    node->mNumChildren = static_cast<unsigned>(g.children.size());
    if (!g.children.empty()) {
        node->mChildren = new aiNode*[g.children.size()];
        for (size_t i = 0; i < g.children.size(); ++i) {
            const auto& [group, leaf] = g.children[i];
            if (group) {
                node->mChildren[i] = materialize(*group, node);
            } else {
                leaf->mParent = node;
                node->mChildren[i] = leaf;
            }
        }
    }
    return node;
}

void regroupByAssembly(aiScene* scene, const std::vector<Xt::BuiltInstance>& instances) {
    if (!scene || !scene->mRootNode)
        return;
    // The converter also names each mesh after its tuple, so the tag is in the mesh names too.
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const std::string meshName = scene->mMeshes[i]->mName.C_Str();
        if (const size_t tag = meshName.rfind(kInstanceTag); tag != std::string::npos)
            scene->mMeshes[i]->mName = aiString(meshName.substr(0, tag));
    }
    aiNode* root = scene->mRootNode;
    TreeGroup top;
    top.name = root->mName.C_Str();
    bool anyAssembly = false;
    std::vector<aiNode*> leaves(root->mChildren, root->mChildren + root->mNumChildren);
    for (aiNode* leaf : leaves) {
        std::string name = leaf->mName.C_Str();
        const size_t tag = name.rfind(kInstanceTag);
        size_t instanceIndex = instances.size();
        if (tag != std::string::npos) {
            instanceIndex = static_cast<size_t>(std::strtoull(name.c_str() + tag + 1, nullptr, 10));
            leaf->mName = aiString(name.substr(0, tag));
        }
        TreeGroup* group = &top;
        if (instanceIndex < instances.size()) {
            for (const Xt::InstanceStep& step : instances[instanceIndex].path) {
                auto& slot = group->byKey[step.key];
                if (!slot) {
                    slot = std::make_unique<TreeGroup>();
                    slot->name = step.name;
                    group->children.emplace_back(slot.get(), nullptr);
                    anyAssembly = true;
                }
                group = slot.get();
            }
        }
        group->children.emplace_back(nullptr, leaf);
    }
    if (!anyAssembly)
        return; // nothing to regroup; the flat tree stays as it was
    // Detach the old children (the nodes themselves live on under their new parents), then rebuild the root.
    delete[] root->mChildren;
    root->mChildren = nullptr;
    root->mNumChildren = 0;
    root->mNumChildren = static_cast<unsigned>(top.children.size());
    root->mChildren = new aiNode*[top.children.size()];
    for (size_t i = 0; i < top.children.size(); ++i) {
        const auto& [group, leaf] = top.children[i];
        if (group) {
            root->mChildren[i] = materialize(*group, root);
        } else {
            leaf->mParent = root;
            root->mChildren[i] = leaf;
        }
    }
}

} // namespace

QString findBaseLayoutFile(QStringList* tried)
{
    QStringList candidates;
    const QString fromEnv = qEnvironmentVariable("MODELVIEWER_XT_LAYOUTS");
    if (!fromEnv.isEmpty())
        candidates << fromEnv;
    const QString fromSettings = QSettings().value(QStringLiteral("xtLayoutsFile")).toString();
    if (!fromSettings.isEmpty())
        candidates << fromSettings;
    const QString name = QStringLiteral("xt-layouts-v13.txt");
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath(name) << QDir(appDir).filePath(QStringLiteral("data/") + name);
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation))
        candidates << QDir(dir).filePath(name);
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation))
        candidates << QDir(dir).filePath(name);
    candidates << QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("Sharjith N/ModelViewer/") + name);
    for (const QString& path : candidates) {
        if (tried)
            tried->append(QDir::toNativeSeparators(path));
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}

// Directory with the Parasolid schema text files (sch_<number>.sch_txt): they give the record layout of every schema
// version, including files that carry no schema of their own. The files are Siemens data and are not shipped.
QString findSchemaDirectory(QStringList* tried)
{
    QStringList candidates;
    const QString fromEnv = qEnvironmentVariable("MODELVIEWER_XT_SCHEMA_DIR");
    if (!fromEnv.isEmpty())
        candidates << fromEnv;
    const QString fromSettings = QSettings().value(QStringLiteral("xtSchemaDir")).toString();
    if (!fromSettings.isEmpty())
        candidates << fromSettings;
    const QString name = QStringLiteral("schema");
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath(name) << QDir(appDir).filePath(QStringLiteral("data/") + name);
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation))
        candidates << QDir(dir).filePath(name);
    candidates << QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("Sharjith N/ModelViewer/") + name);
    for (const QString& path : candidates) {
        if (tried)
            tried->append(QDir::toNativeSeparators(path));
        if (!QDir(path).entryList({QStringLiteral("sch_*.sch_txt")}, QDir::Files).isEmpty())
            return path; // QDir name filters are case-insensitive on Windows
    }
    return {};
}

SceneImportResult importScene(const QString& path, const SceneImportOptions& options)
{
    auto report = [&](const QString& message, int done, int total) {
        if (options.progress)
            options.progress(message, done, total);
    };
    auto cancelledNow = [&] { return options.cancelled && options.cancelled(); };

    QStringList tried, triedSchema;
    const QString layoutPath = findBaseLayoutFile(&tried);
    const QString schemaDir = findSchemaDirectory(&triedSchema);
    Xt::LayoutTable layouts;
    if (!layoutPath.isEmpty()) {
        QFile f(layoutPath);
        std::string error;
        if (!f.open(QIODevice::ReadOnly) || !layouts.parse(f.readAll().toStdString(), &error))
            throw std::runtime_error(QStringLiteral("Cannot use the XT layout file %1: %2").arg(layoutPath, QString::fromStdString(error)).toStdString());
    } else if (!schemaDir.isEmpty()) {
        std::string error;
        const Xt::LayoutTable* base = Xt::schemaFromDirectory(schemaDir.toStdString(), 13006, &error);
        if (!base)
            throw std::runtime_error(QStringLiteral("Cannot use the XT schema 13006 in %1: %2").arg(schemaDir, QString::fromStdString(error)).toStdString());
        layouts = *base;
    } else {
        throw std::runtime_error(QStringLiteral("Parasolid X_T import needs the Parasolid schema files (a folder with sch_13006.sch_txt and the "
                                                "other sch_<number>.sch_txt files) or the base layout file xt-layouts-v13.txt, and found neither. "
                                                "Put the schema folder next to the application as \"schema\" or set MODELVIEWER_XT_SCHEMA_DIR. Looked in:\n%1\n%2")
                                     .arg(triedSchema.join(QLatin1Char('\n')), tried.join(QLatin1Char('\n'))).toStdString());
    }

    report(QObject::tr("Reading Parasolid file..."), 0, 1000);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(QStringLiteral("Cannot open %1").arg(path).toStdString());
    const QByteArray bytes = file.readAll();
    file.close();
    if (cancelledNow())
        throw std::runtime_error("Model loading cancelled by user.");

    Xt::BuildOptions build;
    build.cancelled = cancelledNow;
    build.retainDisplayCandidates = true;
    build.progress = [&](size_t done, size_t total) {
        report(QObject::tr("Building Parasolid shapes..."), 150 + int(550.0 * double(done) / double(total ? total : 1)), 1000);
    };
    Xt::Options readOptions;
    readOptions.cancelled = cancelledNow;
    if (!schemaDir.isEmpty()) {
        const std::string dir = schemaDir.toStdString();
        readOptions.externalSchema = [dir, &layouts](int number) { return Xt::schemaFromDirectory(dir, number, nullptr, &layouts); };
    }
    report(QObject::tr("Decoding Parasolid data..."), 100, 1000);
    Xt::ImportReport r = Xt::importText(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())), layouts, build, readOptions);
    if (cancelledNow())
        throw std::runtime_error("Model loading cancelled by user.");
    if (!r.error.empty())
        throw std::runtime_error(QStringLiteral("Parasolid import failed: %1").arg(QString::fromStdString(r.error)).toStdString());

    SceneImportResult result;
    const Xt::BuildStats& s = r.build.stats;
    Logger::instance().info(
        QStringLiteral("XT trim construction: %1 authored pcurves attached, %2 authored candidates rejected, "
                       "%3 projection attempts, %4 open plane/cylinder wires.")
            .arg(s.authoredPCurves).arg(s.rejectedAuthoredPCurves)
            .arg(s.pcurveProjectionAttempts).arg(s.openTrimWires),
        QStringLiteral("Parasolid"));
    if (s.facesBuilt < s.faces) {
        QStringList kinds;
        for (const auto& kv : s.facesSkippedBySurface)
            kinds << QStringLiteral("%1 %2").arg(QString::fromStdString(kv.first)).arg(kv.second);
        result.warnings << QStringLiteral("Parasolid: %1 of %2 faces could not be built as B-Rep (%3).")
                               .arg(s.faces - s.facesBuilt).arg(s.faces).arg(kinds.join(QStringLiteral(", ")));
    }
    if (s.instancesWithUnsupportedTransform)
        result.warnings << QStringLiteral("Parasolid: %1 assembly occurrences with a mirror or general affine transform are not supported and are left out.")
                               .arg(s.instancesWithUnsupportedTransform);
    // Test aids read by the builder. Left set in the environment they silently cut the model down to a few bodies,
    // so the import says so instead of leaving only "occurrences left out".
    for (const char* name : {"XT_ONLY_BODY", "XT_MAX_BODIES"}) {
        if (qEnvironmentVariableIsSet(name))
            result.warnings << QStringLiteral("Parasolid: the environment variable %1 is set to \"%2\", so only part of the file was built. "
                                              "Remove it and import again to load the whole model.")
                                   .arg(QLatin1String(name), QString::fromLocal8Bit(qgetenv(name)));
    }
    if (s.missingOccurrences)
        result.warnings << QStringLiteral("Parasolid: %1 assembly occurrences refer to parts that are missing or could not be built and are left out.").arg(s.missingOccurrences);
    if (s.facesFilledFromBoundary)
        result.warnings << QStringLiteral("Parasolid: %1 faces could not be reconstructed exactly; a smooth surface was filled through their boundary edges instead.")
                               .arg(s.facesFilledFromBoundary);
    if (s.approximatedGeometry)
        result.warnings << QStringLiteral("Parasolid: %1 blend surfaces, intersection curves or tolerant edges were rebuilt as spline approximations.")
                               .arg(s.approximatedGeometry);
    for (size_t i = 0; i < r.build.diagnostics.size() && i < 5; ++i)
        result.warnings << QStringLiteral("Parasolid: %1").arg(QString::fromStdString(r.build.diagnostics[i].message));
    result.warnings << QStringLiteral("Parasolid: lengths are read as metres (the XT default) and shown in millimetres; the file does not state its unit.");
    if (s.tolerantEdgesSkipped || s.edgeDirectionConflicts)
        result.warnings << QStringLiteral("Parasolid: %1 tolerant edges and %2 edges with conflicting direction were skipped.")
                               .arg(s.tolerantEdgesSkipped).arg(s.edgeDirectionConflicts);

    report(QObject::tr("Converting Parasolid shapes..."), 700, 1000);
    // Mesh each body on its own, in parallel, behind a crash guard: the OCCT mesher has crashed on
    // malformed faces, and one bad body must not take the whole application down. A body that cannot
    // be meshed is left out and reported.
    const size_t bodyCount = r.build.bodies.size();
    const bool trace = qEnvironmentVariableIsSet("XT_TRACE_IMPORT");
    std::vector<char> bodyMeshed(bodyCount, 1);
    std::vector<TopoDS_Shape> displayFallbacks(bodyCount);
    std::atomic<size_t> droppedFaces{0};
    std::atomic<size_t> recoveredFaces{0};
    std::atomic<size_t> displayFaces{0};
    {
        const double deflection = BRepToAssimpConverter::resolveDeflectionFraction();
        const double angle = BRepToAssimpConverter::resolveAngularDeflection();
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
        const unsigned threadCount = std::max(1u, std::min(std::thread::hardware_concurrency(), 16u));
        auto worker = [&] {
            for (;;) {
                const size_t i = next.fetch_add(1);
                if (i >= bodyCount || cancelledNow())
                    return;
                // Nothing may escape a worker thread (an exception there ends the process), so every step is caught.
                try {
                    const auto bodyIndex = static_cast<long long>(r.build.bodies[i].index);
                    if (trace) {
                        std::fprintf(stderr, "XT mesh begin body=%lld\n", bodyIndex);
                        std::fflush(stderr);
                    }
                    auto& body = r.build.bodies[i];
                    const bool isolatedTrace = trace && qEnvironmentVariableIsSet("XT_ONLY_BODY");
                    const double absolute = isolatedTrace ? qgetenv("XT_TRACE_ABSOLUTE_DEFLECTION").toDouble() : 0;
                    const bool hasFaces = TopExp_Explorer(body.shape, TopAbs_FACE).More();
                    if (hasFaces && !meshBodyGuarded(&body.shape, absolute > 0 ? absolute : deflection,
                                                     angle, options, !(absolute > 0)))
                        bodyMeshed[i] = 0;
                    else if (!cancelledNow()) {
                        if (trace) {
                            std::fprintf(stderr, "XT mesh check begin body=%lld\n", bodyIndex);
                            std::fflush(stderr);
                        }
                        const auto mesh = recoverUntriangulatedFaces(body, deflection, angle, options, trace);
                        const auto dump = isolatedTrace ? qgetenv("XT_TRACE_BODY_BREP") : QByteArray();
                        if (!dump.isEmpty())
                            BRepTools::Write(body.shape, dump.constData());
                        droppedFaces += mesh.dropped;
                        recoveredFaces += mesh.recovered;
                        size_t recovered = 0;
                        displayFallbacks[i] = buildDisplayFallback(body, options, recovered);
                        displayFaces += recovered;
                        if (!hasFaces && displayFallbacks[i].IsNull() && !body.displayCandidates.empty())
                            bodyMeshed[i] = 0;
                    }
                    if (trace) {
                        std::fprintf(stderr, "XT mesh end body=%lld\n", bodyIndex);
                        std::fflush(stderr);
                    }
                } catch (...) {
                    bodyMeshed[i] = 0;
                }
                const size_t d = ++done;
                if ((d & 7) == 0) {
                    try {
                        report(QObject::tr("Meshing Parasolid shapes..."), 700 + int(280.0 * double(d) / double(bodyCount)), 1000);
                    } catch (...) {
                    }
                }
            }
        };
        std::vector<std::thread> pool;
        try {
            for (unsigned t = 1; t < threadCount; ++t)
                pool.emplace_back(worker);
        } catch (...) {
            // Could not start every thread; the threads that did start and this one do the work.
        }
        worker();
        for (std::thread& t : pool)
            t.join();
    }
    if (cancelledNow())
        throw std::runtime_error("Model loading cancelled by user.");
    size_t failedBodies = 0;
    size_t displayCandidates = 0;
    for (char ok : bodyMeshed)
        failedBodies += ok ? 0 : 1;
    for (const auto& body : r.build.bodies)
        displayCandidates += body.displayCandidates.size();
    if (droppedFaces)
        result.warnings << QStringLiteral("Parasolid: %1 faces could not be meshed and are left out.").arg(droppedFaces.load());
    if (recoveredFaces)
        result.warnings << QStringLiteral("Parasolid: %1 faces were meshed from their boundary curves.").arg(recoveredFaces.load());
    if (displayCandidates)
        result.warnings << QStringLiteral("Parasolid: recovered %1 of %2 display mesh candidates rejected during B-Rep construction.")
                               .arg(displayFaces.load()).arg(displayCandidates);
    if (failedBodies)
        result.warnings << QStringLiteral("Parasolid: %1 of %2 bodies could not be meshed and are left out.").arg(failedBodies).arg(bodyCount);

    // Colours stored in the file (face colour over body colour). A body whose faces share one colour stays one
    // shape; a body with several colours is split into one face compound per colour so each gets its material.
    struct ColourPart {
        Quantity_Color colour;
        TopoDS_Shape shape;
        float opacity = 1.0f;
    };
    std::vector<std::vector<ColourPart>> colourParts(r.build.bodies.size());
    std::vector<char> colourPartsReady(r.build.bodies.size(), 0);
    const Quantity_Color defaultColour(0.7, 0.7, 0.7, Quantity_TOC_RGB);
    auto partsOf = [&](size_t bodyIdx) -> const std::vector<ColourPart>& {
        if (colourPartsReady[bodyIdx])
            return colourParts[bodyIdx];
        colourPartsReady[bodyIdx] = 1;
        const auto& body = r.build.bodies[bodyIdx];
        std::unordered_map<const TopoDS_TShape*, int64_t> nodeOfFace;
        for (const auto& kv : body.faceOutcomes)
            if (kv.second.shape)
                nodeOfFace.emplace(kv.second.shape, kv.first);
        std::map<Xt::Rgb, std::vector<TopoDS_Face>> groups;
        for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next()) {
            Xt::Rgb colour = body.bodyColour;
            bool coloured = body.hasBodyColour;
            if (const auto n = nodeOfFace.find(ex.Current().TShape().get()); n != nodeOfFace.end())
                if (const auto f = body.faceColours.find(n->second); f != body.faceColours.end()) {
                    colour = f->second;
                    coloured = true;
                }
            if (!coloured) {
                const double alpha = colour.a;
                colour = Xt::Rgb{0.7, 0.7, 0.7, alpha};
            }
            groups[colour].push_back(TopoDS::Face(ex.Current()));
        }
        auto toColour = [](const Xt::Rgb& c) { return Quantity_Color(c.r, c.g, c.b, Quantity_TOC_RGB); };
        if (groups.size() <= 1) {
            colourParts[bodyIdx].push_back({groups.empty() ? defaultColour : toColour(groups.begin()->first), body.shape,
                                            groups.empty() ? 1.0f : static_cast<float>(groups.begin()->first.a)});
        } else {
            for (const auto& [colour, faces] : groups) {
                TopoDS_Compound compound;
                BRep_Builder builder;
                builder.MakeCompound(compound);
                for (const TopoDS_Face& f : faces)
                    builder.Add(compound, f);
                colourParts[bodyIdx].push_back({toColour(colour), compound, static_cast<float>(colour.a)});
            }
        }
        return colourParts[bodyIdx];
    };

    std::vector<ShapeWithNameAndTrsf> tuples;
    std::vector<float> opacities; // parallel to tuples
    for (size_t instanceIndex = 0; instanceIndex < r.build.instances.size(); ++instanceIndex) {
        const Xt::BuiltInstance& inst = r.build.instances[instanceIndex];
        if (!bodyMeshed[inst.body])
            continue;
        // The converter keeps no link from a node to its tuple, so the instance index rides in the name and is
        // removed again when the nodes are regrouped below.
        const std::string taggedName = inst.name + kInstanceTag + std::to_string(instanceIndex);
        const auto& body = r.build.bodies[inst.body];
        const auto& display = displayFallbacks[inst.body];
        const auto& parts = partsOf(inst.body);
        const Quantity_Color color = parts.empty() ? defaultColour : parts.front().colour;
        if (display.IsNull() || TopExp_Explorer(body.shape, TopAbs_FACE).More())
            for (const ColourPart& part : parts) {
                tuples.emplace_back(part.shape, taggedName, inst.location, part.colour);
                opacities.push_back(part.opacity);
            }
        if (!display.IsNull()) {
            tuples.emplace_back(display.Moved(inst.location, false), taggedName, TopLoc_Location(), color);
            opacities.push_back(1.0f);
        }
    }
    if (tuples.empty())
        throw std::runtime_error("Parasolid import failed: no body could be meshed.");
    // Solids have no open boundary: hairline gaps between the tessellations of neighbouring faces are closed on the mesh.
    BRepToAssimpConverter::setFillMeshGaps(qEnvironmentVariableIsEmpty("MODELVIEWER_XT_NO_GAP_FILL"));
    result.scene.reset(BRepToAssimpConverter::convert(tuples, /*copyTriangulation=*/true, &opacities));
    BRepToAssimpConverter::setFillMeshGaps(false);
    if (!result.scene || result.scene->mNumMeshes == 0)
        throw std::runtime_error("Parasolid import failed: the shapes could not be converted to meshes.");
    regroupByAssembly(result.scene.get(), r.build.instances);
    report(QObject::tr("Done"), 1000, 1000);
    return result;
}

} // namespace Xt
