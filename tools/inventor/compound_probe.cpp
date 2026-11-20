// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "InventorCompoundFile.h"
#include "InventorPartSegment.h"
#include "InventorPartSolid.h"

#include <Bnd_Box.hxx>
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
#include <QJsonObject>

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
        {QStringLiteral("volume_mm3"), volume.Mass()},
        {QStringLiteral("faces"), faces.Extent()},
        {QStringLiteral("edges"), edges.Extent()},
    };
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
        if (parser.isSet(partBodies)) {
            QJsonArray bodies;
            int index = 0;
            for (const auto& outcome : Inventor::readPartBodies(arguments.front(), {}, nullptr, false)) {
                QJsonObject row{{QStringLiteral("index"), ++index}};
                if (!outcome.solid.IsNull()) {
                    row.insert(QStringLiteral("kind"), QStringLiteral("solid"));
                    row.insert(QStringLiteral("measure"), measure(outcome.solid));
                } else if (!outcome.incompleteShape.IsNull()) {
                    QJsonObject shape = measure(outcome.incompleteShape);
                    shape.remove(QStringLiteral("volume_mm3")); // An open surface has no volume.
                    row.insert(QStringLiteral("kind"), outcome.isSurface ? QStringLiteral("surface") : QStringLiteral("incomplete"));
                    row.insert(QStringLiteral("measure"), shape);
                    row.insert(QStringLiteral("message"), outcome.error);
                } else {
                    row.insert(QStringLiteral("kind"), QStringLiteral("failed"));
                    row.insert(QStringLiteral("message"), outcome.error);
                }
                bodies.append(row);
            }
            return printJson({{QStringLiteral("bodies"), bodies}});
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
