#include "BRepToAssimpConverter.h"
#include "MainWindow.h"
#include "XCAFSTEPProcessor.hxx"
#include <assimp/scene.h>
#include <BinDrivers.hxx>
#include <BRep_Builder.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <DE_ShapeFixParameters.hxx>
#include <IMeshTools_Parameters.hxx>
#include <QFileInfo>
#include <QString>
#include <STEPCAFControl_Reader.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>
#include "OcctDeprecatedAliases.h"
#include <TDF_Tool.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>
#include <Standard_Failure.hxx>
#include <XCAFReadProgressIndicator.hxx>

// STEP presentation / colour entity headers
#include <StepVisual_StyledItem.hxx>
#include <StepVisual_PresentationStyleAssignment.hxx>
#include <StepVisual_PresentationStyleSelect.hxx>
#include <StepVisual_SurfaceStyleUsage.hxx>
#include <StepVisual_SurfaceSideStyle.hxx>
#include <StepVisual_SurfaceStyleElementSelect.hxx>
#include <StepVisual_SurfaceStyleFillArea.hxx>
#include <StepVisual_FillAreaStyle.hxx>
#include <StepVisual_FillStyleSelect.hxx>
#include <StepVisual_FillAreaStyleColour.hxx>
#include <StepVisual_ColourRgb.hxx>
#include <StepVisual_StyledItemTarget.hxx>
#include <StepRepr_Representation.hxx>
#include <StepData_StepModel.hxx>
#include <TransferBRep.hxx>
#include <XSControl_WorkSession.hxx>

// ---------------------------------------------------------------------------
// buildStepColorMap
//
// Walks every entity in the already-transferred STEP model, finds
// StepVisual_StyledItem entries, navigates the 7-level AP214/AP242 colour
// chain (STYLED_ITEM → PSA → SSU → SSS → SSFA → FAS → FASC → COLOUR_RGB),
// resolves each item's target geometry to a TopoDS_Shape via the transfer
// process, and stores the shape→colour mapping for consumption by
// BRepToAssimpConverter::convertFaceGroupToMeshesWithCache().
//
// Three STYLED_ITEM target types are handled in priority order:
//   2 — ADVANCED_FACE (per-face colour; highest priority)
//   1 — MANIFOLD_SOLID_BREP (solid-level colour)
//   0 — ADVANCED_BREP_SHAPE_REPRESENTATION (assembly-level; lowest priority)
//
// Priority ensures per-face colours are never overwritten by solid- or
// assembly-level entries that land in the same shape bucket.
//
// This completely bypasses XCAFDoc_ColorTool, which relies on FindShape()
// internally and silently fails for face sub-shapes in OCCT 7.x.
// ---------------------------------------------------------------------------
static void buildStepColorMap(const STEPCAFControl_Reader& cafReader)
{
	// Access the underlying STEPControl_Reader to reach the model and work session
	const STEPControl_Reader& reader = cafReader.Reader();

	Handle(StepData_StepModel)      model = reader.StepModel();
	Handle(XSControl_WorkSession)   ws    = reader.WS();
	if (model.IsNull() || ws.IsNull()) return;

	Handle(Transfer_TransientProcess) TP = ws->MapReader();
	if (TP.IsNull()) return;

	BRepToAssimpConverter::StepColorMap colorMap;
	// Track the priority at which each shape's colour was set so higher-priority
	// entries (FACE) never get overwritten by lower-priority ones (SOLID/ABSR).
	std::unordered_map<TopoDS_Shape, int, ShapeHasher, ShapeEqual> priorityMap;

	colorMap.reserve(static_cast<size_t>(model->NbEntities() / 5));
	priorityMap.reserve(colorMap.bucket_count());

	auto tryInsert = [&](const TopoDS_Shape& shape, const Quantity_Color& color, int priority)
	{
		if (shape.IsNull()) return;
		auto pit = priorityMap.find(shape);
		if (pit == priorityMap.end() || priority >= pit->second)
		{
			colorMap[shape]    = color;
			priorityMap[shape] = priority;
		}
	};

	for (Standard_Integer i = 1; i <= model->NbEntities(); ++i)
	{
		// Only process StyledItem entities
		Handle(StepVisual_StyledItem) si =
			Handle(StepVisual_StyledItem)::DownCast(model->Value(i));
		if (si.IsNull() || si->NbStyles() == 0) continue;

		// --- Navigate the colour chain ---

		// Level 1: PresentationStyleAssignment
		Handle(StepVisual_PresentationStyleAssignment) psa = si->StylesValue(1);
		if (psa.IsNull() || psa->NbStyles() == 0) continue;

		// Level 2: PresentationStyleSelect → SurfaceStyleUsage
		Handle(StepVisual_SurfaceStyleUsage) ssu =
			psa->StylesValue(1).SurfaceStyleUsage();
		if (ssu.IsNull()) continue;

		// Level 3: SurfaceSideStyle
		Handle(StepVisual_SurfaceSideStyle) sss = ssu->Style();
		if (sss.IsNull() || sss->NbStyles() == 0) continue;

		// Level 4: SurfaceStyleElementSelect → SurfaceStyleFillArea
		Handle(StepVisual_SurfaceStyleFillArea) ssfa =
			sss->StylesValue(1).SurfaceStyleFillArea();
		if (ssfa.IsNull()) continue;

		// Level 5: FillAreaStyle
		Handle(StepVisual_FillAreaStyle) fas = ssfa->FillArea();
		if (fas.IsNull() || fas->NbFillStyles() == 0) continue;

		// Level 6: FillStyleSelect → FillAreaStyleColour
		Handle(StepVisual_FillAreaStyleColour) fasc =
			fas->FillStylesValue(1).FillAreaStyleColour();
		if (fasc.IsNull()) continue;

		// Level 7: ColourRgb (downcast from base Colour)
		Handle(StepVisual_ColourRgb) rgb =
			Handle(StepVisual_ColourRgb)::DownCast(fasc->FillColour());
		if (rgb.IsNull()) continue;

		const Quantity_Color color(rgb->Red(), rgb->Green(), rgb->Blue(),
		                           Quantity_TOC_RGB);

		// --- Resolve the STYLED_ITEM target → TopoDS_Shape(s) ---
		//
		// si->Item() covers MANIFOLD_SOLID_BREP (solid) and ADVANCED_FACE (face)
		// targets (both inherit StepRepr_RepresentationItem).
		//
		// For ADVANCED_BREP_SHAPE_REPRESENTATION (ABSR) targets, si->Item()
		// returns null because ABSR inherits StepRepr_Representation, not
		// RepresentationItem.  In that case we use si->ItemAP242() which returns
		// a StepVisual_StyledItemTarget union; case 3 is Representation (= ABSR).
		// We then iterate the ABSR's item list to extract the solid entities.

		Handle(StepRepr_RepresentationItem) item = si->Item();
		if (!item.IsNull())
		{
			const TopoDS_Shape shape = TransferBRep::ShapeResult(TP, item);
			if (!shape.IsNull())
			{
				// ADVANCED_FACE → priority 2, anything else (SOLID) → priority 1
				const int prio = (shape.ShapeType() == TopAbs_FACE) ? 2 : 1;
				tryInsert(shape, color, prio);
			}
		}
		else
		{
			// ABSR (or other Representation-derived) target — case 3 in the union
			StepVisual_StyledItemTarget target = si->ItemAP242();
			if (target.CaseNumber() != 3) continue; // not a Representation, skip

			Handle(StepRepr_Representation) repr = target.Representation();
			if (repr.IsNull()) continue;

			// Iterate the ABSR's items to find solid entities and map them at
			// priority 0 (lowest — a per-face or per-solid entry overrides this).
			for (Standard_Integer j = 1; j <= repr->NbItems(); ++j)
			{
				Handle(StepRepr_RepresentationItem) reprItem = repr->ItemsValue(j);
				if (reprItem.IsNull()) continue;

				const TopoDS_Shape shape = TransferBRep::ShapeResult(TP, reprItem);
				if (!shape.IsNull())
					tryInsert(shape, color, /*priority=*/0);
			}
		}
	}

	BRepToAssimpConverter::setStepColorMap(colorMap);
}

aiScene* XCAFSTEPProcessor::processFile(const std::string& path)
{
	XCAFDocProcessor::initializeDocumentProcessing();
	return processSTEPFile(path);
}

aiScene* XCAFSTEPProcessor::processSTEPFile(const std::string& path)
{
	// Create XCAF Application and document
	Handle(TDocStd_Document) doc;
	Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
	BinDrivers::DefineFormat(app);
	app->NewDocument("BinOcaf", doc);

	try
	{
		readSTEPFile(path.c_str(), doc);
	}
	catch (const std::exception& e)
	{
		if (MainWindow::isFileLoadCancelRequested())
		{
			return nullptr;
		}

		qWarning("Failed to read STEP file: %s", e.what());
		return nullptr;
	}

	// Get the shape tool from the document
	Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
	Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

	// Get the shapes from the STEP file
	TDF_LabelSequence labels;
	shapeTool->GetFreeShapes(labels);

	if (labels.IsEmpty())
	{
		qWarning("No shapes found in STEP file");
		return nullptr;
	}

	// Pre-tessellate all free shapes in parallel using a single BRepMesh_IncrementalMesh call.
	// Collecting them into one compound lets OCCT distribute face meshing across all available
	// CPU cores (InParallel = true), which is significantly faster than the per-face sequential
	// meshing that convertFaceGroupToMesh() would otherwise do.
	// Deflection = 0.05 with Relative = true means 5 % of each face's bounding-box diagonal,
	// matching the adaptive quality used by convertFaceGroupToMesh().
	{
		MainWindow::showStatusMessage(tr("Pre-tessellating geometry (parallel)..."));

		TopoDS_Compound compound;
		BRep_Builder builder;
		builder.MakeCompound(compound);

		for (Standard_Integer i = 1; i <= labels.Length(); ++i)
		{
			TopoDS_Shape shape;
			if (shapeTool->GetShape(labels.Value(i), shape) && !shape.IsNull())
				builder.Add(compound, shape);
		}

		IMeshTools_Parameters meshParams;
		meshParams.Deflection             = BRepToAssimpConverter::resolveDeflectionFraction(); // user-configurable, default 5 %
		meshParams.Angle                  = 0.3;    // angular deflection in radians
		meshParams.Relative               = true;   // deflection is relative to each face's bbox
		meshParams.InParallel             = true;   // use all available CPU cores
		meshParams.AllowQualityDecrease   = true;   // avoid stalling on difficult faces

		BRepMesh_IncrementalMesh(compound, meshParams);
	}

#ifdef __DEBUG__
	auto startTraverse = std::chrono::high_resolution_clock::now();
#endif

	// Get the document name to set the root node name using file information
	QFileInfo fi(QString::fromStdString(path));

	// Create the main Assimp scene
	aiScene* scene = new aiScene();
	scene->mRootNode = new aiNode();
	std::string docName = fi.baseName().toStdString();
	std::string rootNodeName = docName + "_STEP";
	scene->mRootNode->mName = aiString(rootNodeName.c_str());

	int meshIndex = 0; // Tracks the mesh indices for the scene

	// Count total leaf parts across ALL free shape labels (the original code only counted
	// labels.Value(1), which gave a wrong total when a file has multiple top-level shapes).
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

#ifdef __DEBUG__
	auto endTraverse = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> durationTraverse = endTraverse - startTraverse;
	std::cout << "TraverseXCAFAssembly and scene creation took " << durationTraverse.count() << " seconds\n";
#endif

	// Finalize the scene
	if (scene->mNumMeshes == 0)
	{
		qWarning("No meshes were generated during scene creation");
		delete scene;
		return nullptr;
	}

#ifdef __DEBUG__
	std::cout << "End of loading STEP file" << std::endl;
	std::cout << "--------------------------------------------------" << std::endl;

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

// Read s STEP file
void XCAFSTEPProcessor::readSTEPFile(const std::string & filename, Handle(TDocStd_Document) & doc)
{
	STEPCAFControl_Reader reader;

	// Disable XCAF sub-systems that are irrelevant for pure 3-D visualization.
	// Each disabled mode skips a separate traversal/annotation pass during Transfer(),
	// which can meaningfully reduce load time on large assemblies.
	reader.SetGDTMode(false);          // Geometric Dimensioning & Tolerancing annotations
	reader.SetSHUOMode(false);         // Super-imposed Higher-Usage Occurrence relationships
	reader.SetPropsMode(false);        // Validation properties (mass, surface area, etc.)
	reader.SetViewMode(false);         // Saved camera views embedded in the STEP file
	reader.SetLayerMode(false);        // Layer assignments — not consumed by the viewer
	reader.SetMetaMode(false);         // Metadata attributes — not consumed by the viewer
	reader.SetProductMetaMode(false);  // Product metadata — not consumed by the viewer

	// Phase D: Tune the OCCT 7.9.x shape-fix pipeline via DE_ShapeFixParameters.
	// By default OCCT runs FixShellOrientationMode = FixOrNot, meaning it will attempt
	// to reconcile shell normal orientations on every imported solid.  For well-authored
	// STEP files from professional CAD tools this is unnecessary overhead; we disable it
	// explicitly.  Other expensive topology-repair passes that have no visual benefit
	// for a viewer (self-intersection detection, face splitting) are also suppressed.
	// All other modes are left at their OCCT defaults so legitimate healing still runs.
	{
		using FM = DE_ShapeFixParameters::FixMode;
		DE_ShapeFixParameters fixParams;
		fixParams.FixShellOrientationMode  = FM::NotFix; // skip expensive shell normal reconciliation
		fixParams.FixSplitFaceMode         = FM::NotFix; // skip topology splitting (viewer does not need it)
		fixParams.FixSelfIntersectionMode  = FM::NotFix; // skip costly self-intersection detection
		fixParams.FixIntersectingWiresMode = FM::NotFix; // O(N²) wire intersection test — not needed for visualization
		fixParams.FixSmallAreaWireMode     = FM::NotFix; // degenerate tiny-wire removal — BRepMesh tolerates these
		fixParams.FixLoopWiresMode         = FM::NotFix; // loop wire detection — not needed for visualization
		fixParams.FixNotchedEdgesMode      = FM::NotFix; // rare edge case, not worth the cost for a viewer
		reader.SetShapeFixParameters(fixParams);
	}

#ifdef __DEBUG__
	auto startCount = std::chrono::high_resolution_clock::now();
#endif
	MainWindow::showIndeterminateProgressBar();

	if (!reader.ReadFile(filename.c_str()))
	{
		MainWindow::resetProgressBar();
		if (MainWindow::isFileLoadCancelRequested())
		{
			throw std::runtime_error("Model loading cancelled by user.");
		}
		throw std::runtime_error("Cannot read STEP file");
	}
	MainWindow::resetProgressBar();

#ifdef __DEBUG__
	auto endCount = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> durationCount = endCount - startCount;
	std::cout << "ReadFile took " << durationCount.count() << " seconds\n";


	auto startTraverse = std::chrono::high_resolution_clock::now();
#endif

	MainWindow::showStatusMessage(tr("Transfering shapes..."));

	Handle(XCAFReadProgressIndicator) progress = new XCAFReadProgressIndicator();
	Message_ProgressRange rootRange = progress->Start();
	Message_ProgressScope transferScope(rootRange, "STEP Transfer", -1);

	try
	{
		if (!reader.Transfer(doc, transferScope.Next()))
		{
			if (MainWindow::isFileLoadCancelRequested())
				throw std::runtime_error("Model loading cancelled by user.");
			throw std::runtime_error("Cannot transfer STEP data to XCAF document");
		}
	}
	catch (const Standard_Failure& e)
	{
		throw std::runtime_error(std::string("STEP transfer error: ") + e.GetMessageString());
	}

	// Build the shape→colour map directly from STEP entities while the reader
	// (and its transfer process) is still in scope.  Must happen after Transfer()
	// so all geometry shapes are registered in the TransientProcess.
	buildStepColorMap(reader);

#ifdef __DEBUG__
	auto endTraverse = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> durationTraverse = endTraverse - startTraverse;
	std::cout << "\nTransfer took " << durationTraverse.count() << " seconds\n";
#endif
}

