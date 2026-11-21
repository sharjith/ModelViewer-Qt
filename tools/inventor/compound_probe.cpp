// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "Inventor/Assembly.h"
#include "Inventor/CompoundFile.h"
#include "Inventor/PartSegment.h"
#include "Inventor/PartSolid.h"

#include <Bnd_Box.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <vector>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Compound.hxx>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QJsonObject>
#include <QMap>

#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <exception>

namespace {

int printJson(const QJsonObject& object)
{
    const QByteArray json = QJsonDocument(object).toJson();
    const auto size = static_cast<std::size_t>(json.size());
    return std::fwrite(json.constData(), 1, size, stdout) == size ? 0 : 1;
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject measure(const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shape, bounds, false, false);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    GProp_GProps volume;
    BRepGProp::VolumeProperties(shape, volume);
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    return {
        {QStringLiteral("dimensions_mm"), QJsonArray{xmax - xmin, ymax - ymin, zmax - zmin}},
        {QStringLiteral("bbox_min"), QJsonArray{xmin, ymin, zmin}},
        {QStringLiteral("bbox_max"), QJsonArray{xmax, ymax, zmax}},
        {QStringLiteral("volume_mm3"), volume.Mass()},
        {QStringLiteral("faces"), faces.Extent()},
        {QStringLiteral("edges"), edges.Extent()},
    };
}


// The occurrences of an assembly and, recursively, of its sub-assemblies, with the placement of each relative to its parent
// and composed in the root frame (millimetres). A component that cannot be found or an assembly that cannot be read is listed
// with the reason, and the walk goes on.
QJsonArray matrixRows(const gp_Trsf& transform)
{
    QJsonArray rows;
    for (int row = 1; row <= 3; ++row)
        rows.append(QJsonArray{transform.Value(row, 1), transform.Value(row, 2), transform.Value(row, 3), transform.Value(row, 4)});
    return rows;
}

void walkAssembly(const QString& file, const gp_Trsf& parentWorld, const QString& parentPath, int depth, QJsonArray& out)
{
    std::vector<Inventor::AssemblyOccurrence> occurrences;
    QString savedPath;
    try {
        occurrences = Inventor::readAssemblyOccurrences(file, {}, &savedPath);
    } catch (const std::exception& error) {
        out.append(QJsonObject{{QStringLiteral("path"), parentPath}, {QStringLiteral("depth"), depth - 1},
                               {QStringLiteral("assembly_error"), QString::fromUtf8(error.what())}});
        return;
    }
    for (const auto& occurrence : occurrences) {
        const gp_Trsf world = parentWorld * occurrence.placement;
        const QString path = parentPath.isEmpty() ? occurrence.name : parentPath + QStringLiteral(" / ") + occurrence.name;
        const auto resolved = Inventor::resolveAssemblyComponent(file, savedPath, occurrence.reference);
        const bool isAssembly = occurrence.reference.endsWith(QStringLiteral(".iam"), Qt::CaseInsensitive);
        QJsonObject row{
            {QStringLiteral("path"), path},
            {QStringLiteral("instance"), occurrence.name},
            {QStringLiteral("reference"), QFileInfo(occurrence.reference).fileName()},
            {QStringLiteral("saved_reference"), occurrence.reference},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("is_assembly"), isAssembly},
            {QStringLiteral("visible"), occurrence.visible},
            {QStringLiteral("suppressed"), occurrence.suppressed},
            {QStringLiteral("local"), matrixRows(occurrence.placement)},
            {QStringLiteral("world"), matrixRows(world)},
        };
        if (resolved.path.isEmpty())
            row.insert(QStringLiteral("unresolved"), resolved.problem);
        out.append(row);
        if (isAssembly && !resolved.path.isEmpty())
            walkAssembly(resolved.path, world, path, depth + 1, out);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Inventory streams using ModelViewer's compound-file reader"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("sample"), QStringLiteral("Path to the original sample"));
    const QCommandLineOption hashStreams(QStringLiteral("hash-streams"), QStringLiteral("Also hash stream payloads"));
    const QCommandLineOption partBrep(QStringLiteral("part-brep"), QStringLiteral("Inspect the saved part B-rep segment"));
    const QCommandLineOption partSolid(QStringLiteral("part-solid"), QStringLiteral("Measure the reconstructed OCCT solids"));
    const QCommandLineOption partBodies(QStringLiteral("part-bodies"),
        QStringLiteral("Report every body (solid, incomplete surface or error) without stopping at the first failure"));
    const QCommandLineOption partAppearance(QStringLiteral("part-appearance"), QStringLiteral("Report saved colors mapped to the reconstructed part"));
    const QCommandLineOption partGraphics(QStringLiteral("part-graphics"),
        QStringLiteral("Report the saved colours of every graphics body (multi-body parts)"));
    parser.addOption(partGraphics);
    const QCommandLineOption assembly(QStringLiteral("assembly"),
        QStringLiteral("List the occurrences of an assembly (.iam) and its sub-assemblies with their composed placements"));
    parser.addOption(assembly);
    const QCommandLineOption partNormals(QStringLiteral("part-normals"),
        QStringLiteral("Tessellate body <n> (1-based) and check the vertex normals the converter would produce"),
        QStringLiteral("n"));
    parser.addOption(partNormals);
    parser.addOption(hashStreams);
    parser.addOption(partBrep);
    parser.addOption(partSolid);
    parser.addOption(partBodies);
    parser.addOption(partAppearance);
    parser.process(app);
    const auto arguments = parser.positionalArguments();
    if (arguments.size() != 1 || parser.isSet(hashStreams) + parser.isSet(partBrep) +
        parser.isSet(partSolid) + parser.isSet(partBodies) + parser.isSet(partAppearance) > 1)
        parser.showHelp(1);

    try {
        if (parser.isSet(assembly)) {
            QJsonArray rows;
            walkAssembly(QFileInfo(arguments.front()).absoluteFilePath(), gp_Trsf(), QString(), 1, rows);
            return printJson({{QStringLiteral("occurrences"), rows}});
        }
        if (parser.isSet(partGraphics)) {
            Inventor::CompoundFile file(arguments.front());
            const auto table = Inventor::readPartColorTable(file);
            if (!table)
                return printJson({{QStringLiteral("color_table"), QStringLiteral("not readable")}});
            const auto hex = [](const Inventor::DiffuseColor& c) {
                return QStringLiteral("%1%2%3").arg(qRound(c[0] * 255), 2, 16, QLatin1Char('0'))
                    .arg(qRound(c[1] * 255), 2, 16, QLatin1Char('0')).arg(qRound(c[2] * 255), 2, 16, QLatin1Char('0'));
            };
            QMap<QString, int> counts;
            for (const auto& [key, color] : table->faces)
                ++counts[hex(color)];
            QJsonObject histogram;
            for (auto it = counts.cbegin(); it != counts.cend(); ++it)
                histogram.insert(it.key(), it.value());
            return printJson({
                {QStringLiteral("default_rgb"), table->defaultColor ? hex(*table->defaultColor) : QStringLiteral("none")},
                {QStringLiteral("faces_with_colour"), static_cast<qint64>(table->faces.size())},
                {QStringLiteral("conflicting_keys"), static_cast<qint64>(table->conflicts)},
                {QStringLiteral("colours"), histogram},
            });
        }
        if (parser.isSet(partAppearance)) {
            Inventor::PartAppearance appearance;
            Inventor::readPartSolids(arguments.front(), {}, &appearance);
            const auto rgb = [](const Quantity_Color& color) {
                return QJsonArray{color.Red(), color.Green(), color.Blue()};
            };
            QJsonObject report;
            if (appearance.body)
                report.insert(QStringLiteral("body_rgb"), rgb(*appearance.body));
            QJsonArray faces;
            for (const auto& [face, color] : appearance.faces) {
                auto row = measure(face);
                row.remove(QStringLiteral("volume_mm3"));
                row.remove(QStringLiteral("faces"));
                row.remove(QStringLiteral("edges"));
                row.insert(QStringLiteral("rgb"), rgb(color));
                faces.append(row);
            }
            report.insert(QStringLiteral("face_overrides"), faces);
            return printJson(report);
        }
        if (parser.isSet(partNormals)) {
            const int wanted = parser.value(partNormals).toInt();
            const auto outcomes = Inventor::readPartBodies(arguments.front(), {}, nullptr, false);
            if (wanted < 1 || static_cast<std::size_t>(wanted) > outcomes.size())
                return printJson({{QStringLiteral("error"), QStringLiteral("body index out of range")}});
            const auto& outcome = outcomes[static_cast<std::size_t>(wanted - 1)];
            const TopoDS_Shape shape = !outcome.solid.IsNull() ? TopoDS_Shape(outcome.solid) : outcome.incompleteShape;
            if (shape.IsNull())
                return printJson({{QStringLiteral("error"), QStringLiteral("body has no shape")}});

            Bnd_Box box;
            BRepBndLib::AddOptimal(shape, box, false, false);
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            const double diagonal = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
            BRepMesh_IncrementalMesh mesher(shape, 0.0005 * diagonal, false, 0.5, true);

            // The converter's own rules (BRepToAssimpConverter): scale = coordinate range, threshold from ThresholdConfig.
            const float scale = static_cast<float>(std::max({x1, y1, z1}) - std::min({x0, y0, z0}));
            const float scaleSquared = scale * scale;
            const float practical = std::max(1e-8f, scaleSquared * 1e-12f);
            const float loose = std::max(1e-6f, scaleSquared * 1e-10f);
            const bool adaptive = scale > 100.0f || scale < 0.01f;
            const float threshold = !adaptive ? practical : (scale > 1000.0f ? loose : (scale < 0.1f ? 1e-12f : practical));

            TopTools_IndexedMapOfShape faceGroup;
            for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
                faceGroup.Add(explorer.Current());

            std::size_t faces = 0, unmeshed = 0, triangles = 0, degenerate = 0, sliver = 0, vertices = 0, fallback = 0;
            std::size_t agree = 0, disagree = 0, noAnalytic = 0, facesAgree = 0, facesDisagree = 0, facesMixed = 0;
            std::size_t reversedFaces = 0;
            for (int f = 1; f <= faceGroup.Extent(); ++f) {
                const TopoDS_Face face = TopoDS::Face(faceGroup(f));
                TopLoc_Location location;
                const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
                ++faces;
                if (triangulation.IsNull() || triangulation->NbTriangles() == 0) {
                    ++unmeshed;
                    continue;
                }
                const bool reversed = face.Orientation() == TopAbs_REVERSED;
                reversedFaces += reversed;
                const int nodes = triangulation->NbNodes();
                std::vector<gp_Pnt> points(static_cast<std::size_t>(nodes));
                for (int i = 1; i <= nodes; ++i)
                    points[static_cast<std::size_t>(i - 1)] = triangulation->Node(i).Transformed(location.Transformation());
                std::vector<gp_Vec> accumulated(static_cast<std::size_t>(nodes), gp_Vec(0, 0, 0));
                std::vector<int> counts(static_cast<std::size_t>(nodes), 0);
                std::vector<gp_Vec> sliverAccumulated(static_cast<std::size_t>(nodes), gp_Vec(0, 0, 0));
                std::vector<int> sliverCounts(static_cast<std::size_t>(nodes), 0);
                BRepGProp_Face analytic(face);
                std::size_t faceAgree = 0, faceDisagree = 0;
                for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
                    int a, b, c;
                    triangulation->Triangle(i).Get(a, b, c);
                    --a; --b; --c;
                    if (reversed)
                        std::swap(b, c);
                    const gp_Pnt& p0 = points[static_cast<std::size_t>(a)];
                    const gp_Pnt& p1 = points[static_cast<std::size_t>(b)];
                    const gp_Pnt& p2 = points[static_cast<std::size_t>(c)];
                    ++triangles;
                    const gp_Vec cross = gp_Vec(p0, p1).Crossed(gp_Vec(p0, p2));
                    const double lengthSquared = cross.SquareMagnitude();
                    if (p0.IsEqual(p1, 0) || p1.IsEqual(p2, 0) || p0.IsEqual(p2, 0)) {
                        ++degenerate;
                        continue;
                    }
                    if (static_cast<float>(lengthSquared) >= threshold) {
                        const gp_Vec normal = cross / std::sqrt(lengthSquared);
                        for (const int node : {a, b, c}) {
                            accumulated[static_cast<std::size_t>(node)] += normal;
                            ++counts[static_cast<std::size_t>(node)];
                        }
                    } else {
                        ++sliver;
                        if (lengthSquared > 0) {
                            const gp_Vec normal = cross / std::sqrt(lengthSquared);
                            for (const int node : {a, b, c}) {
                                sliverAccumulated[static_cast<std::size_t>(node)] += normal;
                                ++sliverCounts[static_cast<std::size_t>(node)];
                            }
                        }
                    }
                    if (triangulation->HasUVNodes() && lengthSquared > 0) {
                        const gp_Pnt2d u0 = triangulation->UVNode(a + 1), u1 = triangulation->UVNode(b + 1),
                                       u2 = triangulation->UVNode(c + 1);
                        gp_Pnt surfacePoint;
                        gp_Vec outward;
                        analytic.Normal((u0.X() + u1.X() + u2.X()) / 3, (u0.Y() + u1.Y() + u2.Y()) / 3, surfacePoint, outward);
                        if (outward.SquareMagnitude() > 1e-20) {
                            if (cross.Dot(outward) > 0) {
                                ++agree;
                                ++faceAgree;
                            } else {
                                ++disagree;
                                ++faceDisagree;
                            }
                        } else {
                            ++noAnalytic;
                        }
                    } else {
                        ++noAnalytic;
                    }
                }
                for (int i = 0; i < nodes; ++i) {
                    ++vertices;
                    if (counts[static_cast<std::size_t>(i)] == 0) {
                        // The converter now uses the tiny triangles' normal for such a vertex; only a vertex with no usable
                        // triangle at all still gets the constant normal (0, 0, 1).
                        if (sliverCounts[static_cast<std::size_t>(i)] == 0 ||
                            (sliverAccumulated[static_cast<std::size_t>(i)] / sliverCounts[static_cast<std::size_t>(i)]).Magnitude() <= 1e-6)
                            ++fallback;
                    } else {
                        const gp_Vec average = accumulated[static_cast<std::size_t>(i)] / counts[static_cast<std::size_t>(i)];
                        if (average.Magnitude() <= 1e-6)
                            ++fallback; // cancelled out: the same fallback
                    }
                }
                if (faceAgree + faceDisagree != 0)
                    (faceDisagree == 0 ? facesAgree : (faceAgree == 0 ? facesDisagree : facesMixed))++;
            }
            return printJson({
                {QStringLiteral("body"), wanted},
                {QStringLiteral("kind"), !outcome.solid.IsNull() ? QStringLiteral("solid") : QStringLiteral("shell")},
                {QStringLiteral("threshold"), static_cast<double>(threshold)},
                {QStringLiteral("scale"), static_cast<double>(scale)},
                {QStringLiteral("faces"), static_cast<qint64>(faces)},
                {QStringLiteral("faces_reversed"), static_cast<qint64>(reversedFaces)},
                {QStringLiteral("faces_unmeshed"), static_cast<qint64>(unmeshed)},
                {QStringLiteral("triangles"), static_cast<qint64>(triangles)},
                {QStringLiteral("triangles_degenerate"), static_cast<qint64>(degenerate)},
                {QStringLiteral("triangles_sliver_left_out_of_normals"), static_cast<qint64>(sliver)},
                {QStringLiteral("vertices"), static_cast<qint64>(vertices)},
                {QStringLiteral("vertices_with_fallback_normal"), static_cast<qint64>(fallback)},
                {QStringLiteral("triangle_normal_agrees_with_occ_outward"), static_cast<qint64>(agree)},
                {QStringLiteral("triangle_normal_opposes_occ_outward"), static_cast<qint64>(disagree)},
                {QStringLiteral("triangles_without_analytic_normal"), static_cast<qint64>(noAnalytic)},
                {QStringLiteral("faces_all_agree"), static_cast<qint64>(facesAgree)},
                {QStringLiteral("faces_all_oppose"), static_cast<qint64>(facesDisagree)},
                {QStringLiteral("faces_mixed"), static_cast<qint64>(facesMixed)},
            });
        }
        if (parser.isSet(partBodies)) {
            QJsonArray bodies;
            int index = 0;
            Inventor::PartAppearance appearance;
            const auto outcomes = Inventor::readPartBodies(arguments.front(), {}, &appearance, false);
            // The saved colours that reached a face of the built shapes (matched by face identity).
            std::unordered_set<const void*> coloured;
            for (const auto& [face, color] : appearance.faces)
                coloured.insert(face.TShape().get());
            const auto colouredFaces = [&coloured](const TopoDS_Shape& shape) {
                NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faceMap;
                TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
                int count = 0;
                for (int i = 1; i <= faceMap.Extent(); ++i)
                    count += coloured.count(faceMap(i).TShape().get()) != 0;
                return count;
            };
            for (const auto& outcome : outcomes) {
                QJsonObject row{{QStringLiteral("index"), ++index}, {QStringLiteral("record"), outcome.bodyRecord},
                                {QStringLiteral("name"), outcome.bodyName}, {QStringLiteral("feature"), outcome.featureName}};
                if (!outcome.solid.IsNull()) {
                    row.insert(QStringLiteral("kind"), QStringLiteral("solid"));
                    row.insert(QStringLiteral("measure"), measure(outcome.solid));
                    row.insert(QStringLiteral("coloured_faces"), colouredFaces(outcome.solid));
                } else if (!outcome.incompleteShape.IsNull()) {
                    QJsonObject shape = measure(outcome.incompleteShape);
                    // An open surface has no volume. The divergence-theorem sum over its oriented faces is still reported as
                    // a signed number: negative means the faces point inward (the shell is inside out for shading).
                    shape.insert(QStringLiteral("signed_volume_mm3"), shape.take(QStringLiteral("volume_mm3")));
                    row.insert(QStringLiteral("kind"), outcome.isSurface ? QStringLiteral("surface") : QStringLiteral("incomplete"));
                    row.insert(QStringLiteral("measure"), shape);
                    row.insert(QStringLiteral("coloured_faces"), colouredFaces(outcome.incompleteShape));
                    row.insert(QStringLiteral("message"), outcome.error);
                } else {
                    row.insert(QStringLiteral("kind"), QStringLiteral("failed"));
                    row.insert(QStringLiteral("message"), outcome.error);
                }
                bodies.append(row);
            }
            QJsonObject summary{{QStringLiteral("faces_with_colour"), static_cast<qint64>(appearance.faces.size())}};
            if (appearance.body)
                summary.insert(QStringLiteral("default_rgb"),
                               QJsonArray{appearance.body->Red(), appearance.body->Green(), appearance.body->Blue()});
            return printJson({{QStringLiteral("appearance"), summary}, {QStringLiteral("bodies"), bodies}});
        }
        if (parser.isSet(partSolid)) {
            const auto solids = Inventor::readPartSolids(arguments.front());
            if (solids.size() == 1)
                return printJson(measure(solids.front()));

            BRep_Builder builder;
            TopoDS_Compound part;
            builder.MakeCompound(part);
            QJsonArray bodies;
            for (const TopoDS_Solid& solid : solids) {
                builder.Add(part, solid);
                bodies.append(measure(solid));
            }
            QJsonObject result = measure(part);
            result.insert(QStringLiteral("bodies"), bodies);
            return printJson(result);
        }
        Inventor::CompoundFile file(arguments.front());
        constexpr qsizetype maxStreamBytes = 512 * 1024 * 1024;
        if (parser.isSet(partBrep)) {
            const QByteArray bytes = Inventor::readPartBRepSegment(file, maxStreamBytes);
            return printJson({
                {QStringLiteral("size_bytes"), static_cast<qint64>(bytes.size())},
                {QStringLiteral("sha256"), sha256(bytes)},
                {QStringLiteral("asm_header_offset"), static_cast<qint64>(bytes.indexOf("ASM BinaryFile4"))},
            });
        }
        QJsonArray streams;
        for (std::size_t index = 0; index < file.streams().size(); ++index) {
            const auto& stream = file.streams()[index];
            QJsonObject row{
                {QStringLiteral("path"), QJsonArray::fromStringList(stream.path)},
                {QStringLiteral("size_bytes"), static_cast<qint64>(stream.size)},
            };
            if (parser.isSet(hashStreams)) {
                const QByteArray bytes = file.readStream(index, maxStreamBytes);
                row.insert(QStringLiteral("sha256"), sha256(bytes));
            }
            streams.append(row);
        }
        return printJson({{QStringLiteral("streams"), streams}});
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
