// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Reads a STEP file and prints every shell (closed or open) it holds with its bounding box and face count, as JSON. Used to
// find out which bodies of an Inventor part its own STEP export leaves out: the shells of the export are matched by bounding
// box with the bodies the part reader finds (inventor_compound_probe --part-bodies prints their boxes). Lengths are the STEP
// file's own (millimetres).
//
//   step_shell_boxes <file.stp>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>

#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "usage: step_shell_boxes <file.stp>\n");
        return 2;
    }
    STEPControl_Reader reader;
    if (reader.ReadFile(argv[1]) != IFSelect_RetDone) {
        std::fprintf(stderr, "could not read the STEP file\n");
        return 1;
    }
    reader.TransferRoots();
    QJsonArray shells;
    for (int root = 1; root <= reader.NbShapes(); ++root) {
        const TopoDS_Shape shape = reader.Shape(root);
        for (TopExp_Explorer explorer(shape, TopAbs_SHELL); explorer.More(); explorer.Next()) {
            const TopoDS_Shape& shell = explorer.Current();
            Bnd_Box box;
            BRepBndLib::AddOptimal(shell, box, false, false);
            if (box.IsVoid())
                continue;
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(shell, TopAbs_FACE, faces);
            shells.append(QJsonObject{
                {QStringLiteral("root"), root},
                {QStringLiteral("min"), QJsonArray{x0, y0, z0}},
                {QStringLiteral("max"), QJsonArray{x1, y1, z1}},
                {QStringLiteral("faces"), faces.Extent()},
            });
        }
    }
    const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("shells"), shells}}).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
    return 0;
}
