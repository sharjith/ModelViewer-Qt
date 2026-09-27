#include "BRepToAssimpConverter.h"
#include "MainWindow.h"
#include "XCAFVRMLProcessor.hxx"
#include <assimp/scene.h>
#include <BinDrivers.hxx>
#include <QFileInfo>
#include <QString>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>
#include "OcctDeprecatedAliases.h"
#include <TDocStd_Document.hxx>
#include <VrmlAPI_CafReader.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Message_ProgressRange.hxx>
#include <Standard_Failure.hxx>
#include <XCAFReadProgressIndicator.hxx>

aiScene* XCAFVRMLProcessor::processFile(const std::string& path)
{
	XCAFDocProcessor::initializeDocumentProcessing();
	return processVRMLFile(path);
}

aiScene* XCAFVRMLProcessor::processVRMLFile(const std::string& path)
{
	// Create XCAF Application and document
	Handle(TDocStd_Document) doc;
	Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
	BinDrivers::DefineFormat(app);
	app->NewDocument("BinOcaf", doc);

	try
	{
		readVRMLFile(path, doc);
	}
	catch (const std::exception& e)
	{
		app->Close(doc);
		if (MainWindow::isFileLoadCancelRequested())
		{
			return nullptr;
		}

		qWarning("Failed to read VRML file: %s", e.what());
		return nullptr;
	}

	// Get the shape tool from the document
	Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
	Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

	// Get the shapes read from the VRML file
	TDF_LabelSequence labels;
	shapeTool->GetFreeShapes(labels);

	if (labels.IsEmpty())
	{
		qWarning("No shapes found in VRML file");
		app->Close(doc);
		return nullptr;
	}

	// Get the document name to set the root node name using file information
	QFileInfo fi(QString::fromStdString(path));

	// Create the main Assimp scene
	aiScene* scene = new aiScene();
	scene->mRootNode = new aiNode();
	std::string docName = fi.baseName().toStdString();
	std::string rootNodeName = docName + "_VRML";
	scene->mRootNode->mName = aiString(rootNodeName.c_str());

	int meshIndex = 0; // Tracks the mesh indices for the scene

	// Count total leaf parts across ALL free shape labels
	int totalMeshes = 0;
	for (Standard_Integer i = 1; i <= labels.Length(); ++i)
		totalMeshes += countMeshes(shapeTool, labels.Value(i));
	int processedMeshes = 0;

	// Pre-allocate the root node's children array to the exact number of free shape
	// labels so traverseXCAFAssembly can use direct index assignment instead of
	// calling realloc() for every top-level child it attaches.
	scene->mRootNode->mChildren    = labels.Length() > 0 ? new aiNode*[labels.Length()] : nullptr;
	scene->mRootNode->mNumChildren = 0;

	MainWindow::showStatusMessage(tr("Traversing assembly and building scene..."));

	for (Standard_Integer i = 1; i <= labels.Length(); ++i)
	{
		traverseXCAFAssembly(shapeTool, colorTool, labels.Value(i), TopLoc_Location(), scene->mRootNode, scene, meshIndex, processedMeshes, totalMeshes);
	}

	// Finalize the scene
	if (scene->mNumMeshes == 0)
	{
		qWarning("No meshes were generated during scene creation");
		delete scene;
		app->Close(doc);
		return nullptr;
	}

	app->Close(doc);

	return scene;
}

// Read a VRML (.wrl) file directly into an XCAF document. VrmlAPI_CafReader (an
// RWMesh_CafReader specialisation - the same reader family OCCT uses for OBJ/glTF/STL,
// not the STEPCAFControl_Reader/IGESCAFControl_Reader family STEP/IGES use) already
// populates ShapeTool/ColorTool with named, coloured shapes in one Perform() call, so
// there's no separate ReadFile()+Transfer() step and no STEP-style bespoke colour-chain
// walk needed - VRML's per-node Material (old-school diffuse/specular/shininess, not
// AP242 styled items) is applied by the reader itself via the standard ColorTool path
// traverseXCAFAssembly()/GetShapeColorFromShape() already consume for every other format.
void XCAFVRMLProcessor::readVRMLFile(const std::string& filename, Handle(TDocStd_Document)& doc)
{
	VrmlAPI_CafReader reader;
	reader.SetDocument(doc);

	MainWindow::showIndeterminateProgressBar();

	Handle(XCAFReadProgressIndicator) progress = new XCAFReadProgressIndicator();
	Message_ProgressRange rootRange = progress->Start();

	bool ok = false;
	try
	{
		ok = reader.Perform(TCollection_AsciiString(filename.c_str()), rootRange);
	}
	catch (const Standard_Failure& e)
	{
		MainWindow::resetProgressBar();
		throw std::runtime_error(std::string("VRML read error: ") + e.GetMessageString());
	}

	MainWindow::resetProgressBar();

	if (!ok)
	{
		if (MainWindow::isFileLoadCancelRequested())
			throw std::runtime_error("Model loading cancelled by user.");
		throw std::runtime_error("Cannot read VRML file");
	}
}
