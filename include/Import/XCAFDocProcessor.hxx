#pragma once

#include "IXCAFDocProcessor.hxx"
#include <assimp/scene.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <Quantity_Color.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include "OcctDeprecatedAliases.h"
#include <XCAFDoc_Location.hxx>
#include <XCAFApp_Application.hxx>
#include <QObject>

class XCAFDocProcessor : public QObject, public IXCAFDocProcessor
{
    Q_OBJECT

public:
    XCAFDocProcessor() = default;
    ~XCAFDocProcessor() override { releaseConvertedParts(); }

protected:
    using ShapeWithNameAndTrsf = std::tuple<TopoDS_Shape, std::string, TopLoc_Location, Quantity_Color>;

    void initializeDocumentProcessing();

    // Common XCAF functionality shared by all CAD processors
    void traverseXCAFAssembly(
        const Handle(XCAFDoc_ShapeTool)& shapeTool,
        const Handle(XCAFDoc_ColorTool)& colorTool,
        const TDF_Label& label,
        const TopLoc_Location& parentLoc,
        std::vector<TopoDS_Shape>& outShapes,
        std::vector<Quantity_Color>& outColors,
        std::vector<std::string>& outNames);

    void traverseXCAFAssembly(
        const Handle(XCAFDoc_ShapeTool)& shapeTool,
        const Handle(XCAFDoc_ColorTool)& colorTool,
        const TDF_Label& label,
        const TopLoc_Location& parentLoc,
        aiNode* parentNode,
        aiScene* scene,
        int& meshIndex,
        int& processedMeshes,
        int totalMeshes);

    bool GetShapeColorFromShape(
        const Handle(XCAFDoc_ColorTool)& colorTool,
        const TopoDS_Shape& shape,
        Quantity_Color& outColor);

    int countMeshes(const Handle(XCAFDoc_ShapeTool)& shapeTool, const TDF_Label& label);

    aiMatrix4x4 convertLocationToMatrix(const TopLoc_Location& loc);

    // Drops the cache of converted part definitions (see traverseXCAFAssembly()); call when the scene is built.
    void releaseConvertedParts();

private:
    // Per-document cache mapping each instance label to its resolved definition label.
    // Populated by countMeshes() on the first (counting) tree walk; consumed by
    // traverseXCAFAssembly() on the second (conversion) walk so GetReferredShape()
    // is called at most once per unique instance label across both passes.
    // Cleared at the start of every file load in initializeDocumentProcessing().
    TDF_LabelDataMap myRefCache;
    // Converted parts by definition label entry. Many instances share one definition (a screw used 36 times is converted
    // once and copied); the geometry, edges and colours of a part do not depend on the instance, only its location, which
    // the node carries. A null entry records a definition that cannot be meshed.
    std::unordered_map<std::string, aiScene*> myConvertedParts;
    unsigned myConvertedHits = 0, myConvertedMisses = 0;
};