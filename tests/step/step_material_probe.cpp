// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Evidence tool, not a test: prints the materials that OpenCASCADE's STEPCAFControl_Reader puts into the XCAF document of a STEP file
// (name, description, density as OCCT returns it, the density's name and value type), and how many shapes reference each one.
// Purpose: to see what OCCT does with the file's density unit (the samples use both g/cm3 and kg/m3) before the importer relies on it.
// No part names are printed; shapes are identified by their document label entry only.
//
//   step_material_probe <file.step|stp> [--shapes N] [--set name=value ...]
//     N = how many referencing shape labels to list (default 5); --set sets an OpenCASCADE static parameter (Interface_Static) after the
//     reader is created and before the file is read, e.g. --set xstep.cascade.unit=M; the result of each --set is printed.
//     --tree prints every label under the shape tool's base label with its XCAF flags, its name attribute and whether it references a
//     material (part names ARE printed in this mode; use it for small files and keep the output under out/).
#include <BinDrivers.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_Label.hxx>
#include <TDF_Tool.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_TreeNode.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
std::string text(const occ::handle<TCollection_HAsciiString>& value)
{
    return value.IsNull() ? std::string("(none)") : std::string(value->ToCString());
}

std::string entry(const TDF_Label& label)
{
    TCollection_AsciiString id;
    TDF_Tool::Entry(label, id);
    return std::string(id.ToCString());
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: step_material_probe <file.step|stp> [--shapes N]\n");
        return 2;
    }
    int listShapes = 5;
    std::vector<std::string> settings;
    bool printTree = false;
    for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], "--tree") == 0)
            printTree = true;
    for (int i = 2; i + 1 < argc; ++i)
    {
        if (std::strcmp(argv[i], "--shapes") == 0)
            listShapes = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--set") == 0)
            settings.push_back(argv[i + 1]);
    }

    occ::handle<XCAFApp_Application> app = XCAFApp_Application::GetApplication();
    BinDrivers::DefineFormat(app);
    occ::handle<TDocStd_Document> doc;
    app->NewDocument("BinOcaf", doc);

    STEPCAFControl_Reader reader;
    reader.SetMatMode(true);
    for (const std::string& setting : settings)
    {
        const size_t eq = setting.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string name = setting.substr(0, eq), value = setting.substr(eq + 1);
        const bool ok = Interface_Static::SetCVal(name.c_str(), value.c_str());
        std::printf("set %s = %s : %s (now '%s')\n", name.c_str(), value.c_str(), ok ? "accepted" : "REFUSED",
                    Interface_Static::CVal(name.c_str()) ? Interface_Static::CVal(name.c_str()) : "(unknown parameter)");
    }
    if (reader.ReadFile(argv[1]) != IFSelect_RetDone)
    {
        std::fprintf(stderr, "cannot read file\n");
        return 1;
    }
    if (!reader.Transfer(doc))
    {
        std::fprintf(stderr, "transfer failed\n");
        return 1;
    }

    double documentUnitMetres = 0.0;
    if (XCAFDoc_DocumentTool::GetLengthUnit(doc, documentUnitMetres))
        std::printf("document length unit: %.17g metre\n", documentUnitMetres);
    else
        std::printf("document length unit: not available\n");

    occ::handle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    occ::handle<XCAFDoc_MaterialTool> materials = XCAFDoc_DocumentTool::MaterialTool(doc->Main());

    NCollection_Sequence<TDF_Label> materialLabels;
    materials->GetMaterialLabels(materialLabels);
    std::printf("materials in document: %d\n", int(materialLabels.Length()));
    std::map<std::string, int> uses;   // material label entry -> number of shape labels that reference it
    for (int i = 1; i <= materialLabels.Length(); ++i)
    {
        occ::handle<TCollection_HAsciiString> name, description, densName, densValType;
        double density = 0.0;
        if (!XCAFDoc_MaterialTool::GetMaterial(materialLabels.Value(i), name, description, density, densName, densValType))
            continue;
        std::printf("  [%s] name='%s' description='%s' density=%.17g densName='%s' densValType='%s'\n",
                    entry(materialLabels.Value(i)).c_str(), text(name).c_str(), text(description).c_str(), density,
                    text(densName).c_str(), text(densValType).c_str());
    }

    if (printTree)
    {
        std::printf("label tree (entry | flags | material ref | name):\n");
        for (TDF_ChildIterator it(shapes->BaseLabel(), true); it.More(); it.Next())
        {
            const TDF_Label label = it.Value();
            if (!shapes->IsShape(label))
                continue;
            occ::handle<TDataStd_TreeNode> node;
            const bool hasMaterial = label.FindAttribute(XCAFDoc::MaterialRefGUID(), node) && !node.IsNull() && node->HasFather();
            occ::handle<TDataStd_Name> name;
            std::string nameText;
            if (label.FindAttribute(TDataStd_Name::GetID(), name))
            {
                TCollection_AsciiString ascii(name->Get());
                nameText = ascii.ToCString();
            }
            std::printf("  %-16s | free=%d top=%d assembly=%d simple=%d compound=%d component=%d reference=%d subshape=%d | %s | %s\n",
                        entry(label).c_str(), int(shapes->IsFree(label)), int(shapes->IsTopLevel(label)), int(shapes->IsAssembly(label)),
                        int(shapes->IsSimpleShape(label)), int(shapes->IsCompound(label)), int(shapes->IsComponent(label)),
                        int(shapes->IsReference(label)), int(shapes->IsSubShape(label)),
                        hasMaterial ? entry(node->Father()->Label()).c_str() : "-", nameText.c_str());
        }
    }

    int referencing = 0, listed = 0;
    for (TDF_ChildIterator it(shapes->BaseLabel(), true); it.More(); it.Next())
    {
        occ::handle<TDataStd_TreeNode> node;
        if (!it.Value().FindAttribute(XCAFDoc::MaterialRefGUID(), node) || node.IsNull() || !node->HasFather())
            continue;
        ++referencing;
        const std::string materialEntry = entry(node->Father()->Label());
        ++uses[materialEntry];
        if (listed < listShapes)
        {
            std::printf("  shape label %s -> material %s, XCAFDoc_MaterialTool::GetDensityForShape = %.17g\n",
                        entry(it.Value()).c_str(), materialEntry.c_str(), XCAFDoc_MaterialTool::GetDensityForShape(it.Value()));
            ++listed;
        }
    }
    std::printf("shape labels referencing a material: %d\n", referencing);
    for (const auto& use : uses)
        std::printf("  material %s used by %d shape label(s)\n", use.first.c_str(), use.second);
    return 0;
}
