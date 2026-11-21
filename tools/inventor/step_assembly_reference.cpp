// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Reads a STEP assembly exported by Autodesk Inventor and prints the product tree as JSON: for every occurrence its path of
// instance names, the referred product, its depth, the transform relative to its parent and the composed transform in the
// root frame. It is the reference for the placement of nested occurrences when qualifying the Inventor assembly reader
// (the .iam of the same assembly must give the same composed transforms). Lengths are the STEP file's own (millimetres).
//
//   step_assembly_reference <file.stp>      JSON on stdout
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <STEPCAFControl_Reader.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <cstdio>

namespace {

QString nameOf(const TDF_Label& label)
{
    Handle(TDataStd_Name) name;
    if (!label.FindAttribute(TDataStd_Name::GetID(), name))
        return QString();
    const TCollection_ExtendedString& text = name->Get();
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(text.ToExtString()), static_cast<qsizetype>(text.Length()));
}

QJsonArray matrixOf(const gp_Trsf& transform)
{
    QJsonArray rows;
    for (int row = 1; row <= 3; ++row)
        rows.append(QJsonArray{transform.Value(row, 1), transform.Value(row, 2), transform.Value(row, 3), transform.Value(row, 4)});
    return rows;
}

void walk(const Handle(XCAFDoc_ShapeTool)& tool, const TDF_Label& assembly, const gp_Trsf& parentWorld, const QString& parentPath,
          int depth, QJsonArray& out)
{
    TDF_LabelSequence components;
    if (!XCAFDoc_ShapeTool::GetComponents(assembly, components))
        return;
    for (int i = 1; i <= components.Length(); ++i) {
        const TDF_Label component = components.Value(i);
        TDF_Label referred;
        XCAFDoc_ShapeTool::GetReferredShape(component, referred);
        const gp_Trsf local = XCAFDoc_ShapeTool::GetLocation(component).Transformation();
        const gp_Trsf world = parentWorld * local;
        const QString instance = nameOf(component);
        const QString path = parentPath.isEmpty() ? instance : parentPath + QStringLiteral(" / ") + instance;
        const bool isAssembly = !referred.IsNull() && XCAFDoc_ShapeTool::IsAssembly(referred);
        QJsonObject row{
            {QStringLiteral("path"), path},
            {QStringLiteral("instance"), instance},
            {QStringLiteral("product"), referred.IsNull() ? QString() : nameOf(referred)},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("is_assembly"), isAssembly},
            {QStringLiteral("local"), matrixOf(local)},
            {QStringLiteral("world"), matrixOf(world)},
        };
        if (local.ScaleFactor() != 1.0)
            row.insert(QStringLiteral("local_scale"), local.ScaleFactor());
        out.append(row);
        if (isAssembly)
            walk(tool, referred, world, path, depth + 1, out);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "usage: step_assembly_reference <file.stp>\n");
        return 2;
    }
    Handle(TDocStd_Document) document;
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", document);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    if (reader.ReadFile(argv[1]) != IFSelect_RetDone || !reader.Transfer(document)) {
        std::fprintf(stderr, "could not read the STEP file\n");
        return 1;
    }
    const Handle(XCAFDoc_ShapeTool) tool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence roots;
    tool->GetFreeShapes(roots);
    QJsonArray occurrences;
    QJsonArray rootNames;
    for (int i = 1; i <= roots.Length(); ++i) {
        rootNames.append(nameOf(roots.Value(i)));
        walk(tool, roots.Value(i), gp_Trsf(), QString(), 1, occurrences);
    }
    const QJsonObject result{{QStringLiteral("roots"), rootNames}, {QStringLiteral("occurrences"), occurrences}};
    const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
    std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
    return 0;
}
