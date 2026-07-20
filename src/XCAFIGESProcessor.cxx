#include "BinDrivers.hxx"
#include "BRepToAssimpConverter.h"
#include "IGESCAFControl_Reader.hxx"
#include "IGESControl_Controller.hxx"
#include "MainWindow.h"
#include "OcctDeprecatedAliases.h"
#include "TDF_Tool.hxx"
#include "TDocStd_Document.hxx"
#include "XCAFApp_Application.hxx"
#include "XCAFIGESProcessor.hxx"
#include "XCAFReadProgressIndicator.hxx"
#include <QFileInfo>
#include <QString>


#include <assimp/scene.h>

aiScene* XCAFIGESProcessor::processFile(const std::string& path)
{
	XCAFDocProcessor::initializeDocumentProcessing();
	return processIGESFile(path);
}

aiScene* XCAFIGESProcessor::processIGESFile(const std::string& path)
{
    // Create XCAF Application and document
    Handle(TDocStd_Document) doc;
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    BinDrivers::DefineFormat(app);
    app->NewDocument("BinOcaf", doc);

    // Initialize IGES controller inside session
    IGESControl_Controller::Init();

    MainWindow::showIndeterminateProgressBar();

    Handle(XCAFReadProgressIndicator) progress = new XCAFReadProgressIndicator();
    Message_ProgressRange rootRange = progress->Start();

    Message_ProgressScope transferScope(rootRange, "IGES Transfer", -1);

    // Read IGES file into XCAF document
    IGESCAFControl_Reader cafReader;
    cafReader.SetLayerMode(false); // Layer assignments — not consumed by the viewer
    if (cafReader.ReadFile(path.c_str()) != IFSelect_RetDone)
    {
        MainWindow::resetProgressBar();
        if (MainWindow::isFileLoadCancelRequested())
        {
            return nullptr;
        }
        qWarning("Failed to read IGES file: %s", path.c_str());
        return nullptr;
    }
    MainWindow::resetProgressBar();

    MainWindow::showStatusMessage(tr("Transfering shapes..."));

    cafReader.Transfer(doc, transferScope.Next());

    // Access shape tool and color tool
    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    // Collect free shapes (top-level shapes)
    TDF_LabelSequence labels;
    shapeTool->GetFreeShapes(labels);

    // Precompute the total number of meshes for progress bar updates
	int totalMeshes = countMeshes(shapeTool, labels.Value(1));

    // Get the document name to set the root node name using file information
    QFileInfo fi(QString::fromStdString(path));

    // Create the root Assimp scene
    aiScene* scene = new aiScene();
    scene->mRootNode = new aiNode();
    std::string docName = fi.baseName().toStdString();
    std::string rootNodeName = docName + "_IGES";
    scene->mRootNode->mName = aiString(rootNodeName.c_str());

    int meshIndex = 0; // Tracks the mesh indices for the scene
    // Initialize progress tracking
    int processedMeshes = 0;

    // Pre-allocate the root node's children array so traverseXCAFAssembly can
    // use direct index assignment instead of writing into a null pointer.
    scene->mRootNode->mChildren    = labels.Length() > 0 ? new aiNode*[labels.Length()] : nullptr;
    scene->mRootNode->mNumChildren = 0;

    // Traverse the assembly structure and build the aiScene
    MainWindow::showStatusMessage(tr("Traversing assembly and building scene..."));

    for (Standard_Integer i = 1; i <= labels.Length(); ++i)
    {
        traverseXCAFAssembly(shapeTool, colorTool, labels.Value(i), TopLoc_Location(), scene->mRootNode, scene, meshIndex, processedMeshes, totalMeshes);
    }

    // Close the XCAF document and clean up
#ifdef __DEBUG__
    QFileInfo fi(QString::fromStdString(path));
    QString savePath = "/home/sharjith/" + fi.baseName() + ".cbf";
    PCDM_StoreStatus sstatus = app->SaveAs(doc, TCollection_ExtendedString(savePath.toStdString().c_str()));
    if (sstatus != PCDM_SS_OK)
    {
        app->Close(doc);
        qDebug() << "Error saving the document";
    }
#endif

    app->Close(doc);

    return scene;
}
