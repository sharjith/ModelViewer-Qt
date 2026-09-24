#include "XCAFBREPProcessor.hxx"
#include "MainWindow.h"
#include "BRepToAssimpConverter.h"
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BinDrivers.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TCollection_ExtendedString.hxx>
#include <assimp/scene.h>

aiScene* XCAFBREPProcessor::processFile(const std::string& path)
{
	XCAFDocProcessor::initializeDocumentProcessing();
	return processBREPFile(path);
}

aiScene* XCAFBREPProcessor::processBREPFile(const std::string& path)
{
	// Create XCAF Application and document
	Handle(TDocStd_Document) doc;
	Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
	BinDrivers::DefineFormat(app);
	app->NewDocument("BinOcaf", doc);

	MainWindow::showIndeterminateProgressBar();

	try
	{
		// Read BREP file directly into a shape
		TopoDS_Shape shape;
		BRep_Builder builder;

		if (!BRepTools::Read(shape, path.c_str(), builder))
		{
			qCritical("Failed to read BREP file: %s", path.c_str());
			MainWindow::resetProgressBar();
			app->Close(doc);
			return nullptr;
		}

		// Check if shape is valid
		if (shape.IsNull())
		{
			qCritical("Read BREP file but shape is null: %s", path.c_str());
			MainWindow::resetProgressBar();
			app->Close(doc);
			return nullptr;
		}

		MainWindow::resetProgressBar();
		MainWindow::showStatusMessage(tr("Transferring shapes..."));

		// Access shape tool and color tool - ensure they're properly initialized
		Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
		Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

		// Verify tools are valid
		if (shapeTool.IsNull() || colorTool.IsNull())
		{
			qCritical("Failed to initialize XCAF tools for BREP file: %s", path.c_str());
			MainWindow::resetProgressBar();
			app->Close(doc);
			return nullptr;
		}

		// Add the shape to XCAF document
		TDF_Label rootLabel = shapeTool->AddShape(shape, Standard_False);

		// Verify the root label is valid
		if (rootLabel.IsNull())
		{
			qCritical("Failed to add shape to XCAF document: %s", path.c_str());
			MainWindow::resetProgressBar();
			app->Close(doc);
			return nullptr;
		}

		// Single model mode - treat entire shape as one unit
		MainWindow::showStatusMessage(tr("Loading as single model..."));

		// Set name and color for the single shape
		try
		{
			Handle(TDataStd_Name) nameAttr = TDataStd_Name::Set(rootLabel,
				TCollection_ExtendedString("BREPModel"));

			if (!colorTool->IsSet(rootLabel, XCAFDoc_ColorGen))
			{
				Quantity_Color defaultColor(0.7, 0.7, 0.7, Quantity_TOC_RGB);
				colorTool->SetColor(rootLabel, defaultColor, XCAFDoc_ColorGen);
			}
		}
		catch (const Standard_Failure& e)
		{
			qWarning("Warning: Could not set name/color for shape: %s", e.GetMessageString());
		}

		// Create single shape tuple
		std::vector<ShapeWithNameAndTrsf> shapeTuples;
		shapeTuples.emplace_back(shape, "BREPModel", TopLoc_Location(),
			Quantity_Color(0.7, 0.7, 0.7, Quantity_TOC_RGB));

		// Tessellate the whole shape in one parallel pass first (as the STEP and IGES readers do)
		MainWindow::showStatusMessage(tr("Pre-tessellating geometry (parallel)..."));
		BRepToAssimpConverter::preTessellate(shape);

		// Convert to Assimp scene
		MainWindow::showStatusMessage(tr("Converting shape to mesh..."));
		aiScene* scene = BRepToAssimpConverter::convert(shapeTuples);

#ifdef __DEBUG__
		QFileInfo fi(QString::fromStdString(path));
		QString savePath = "/home/sharjith/" + fi.baseName() + ".cbf";
		PCDM_StoreStatus sstatus = app->SaveAs(doc, TCollection_ExtendedString(savePath.toStdString().c_str()));
		if (sstatus != PCDM_SS_OK)
		{
			qDebug() << "Error saving the document";
		}
#endif
		app->Close(doc);
		return scene;
	}
	catch (const Standard_Failure& e)
	{
		qCritical("OpenCASCADE exception while reading BREP file: %s", e.GetMessageString());
		MainWindow::resetProgressBar();
		app->Close(doc);
		return nullptr;
	}
	catch (...)
	{
		qCritical("Unknown exception while reading BREP file: %s", path.c_str());
		MainWindow::resetProgressBar();
		app->Close(doc);
		return nullptr;
	}
}
