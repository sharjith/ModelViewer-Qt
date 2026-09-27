#include "XCAFDocProcessor.hxx"
#include "BRepToAssimpConverter.h"
#include "MainWindow.h"
#include <algorithm>
#include <map>
#include <TDF_Tool.hxx>

namespace
{
std::string trimmedName(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();

    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string labelAttributeName(const TDF_Label& label)
{
    if (label.IsNull())
        return std::string();

    Handle(TDataStd_Name) nameAttr;
    if (!label.FindAttribute(TDataStd_Name::GetID(), nameAttr))
        return std::string();

    return trimmedName(TCollection_AsciiString(nameAttr->Get()).ToCString());
}

std::string labelEntryName(const TDF_Label& label, const std::string& prefix)
{
    if (label.IsNull())
        return prefix;

    TCollection_AsciiString entry;
    TDF_Tool::Entry(label, entry);
    const std::string entryText = trimmedName(entry.ToCString());
    if (entryText.empty())
        return prefix;

    return prefix + "_" + entryText;
}

std::string resolvedLabelName(const TDF_Label& primary,
                              const TDF_Label& secondary,
                              const std::string& fallbackPrefix)
{
    const std::string primaryName = labelAttributeName(primary);
    if (!primaryName.empty() && primaryName != "Unnamed")
        return primaryName;

    const std::string secondaryName = labelAttributeName(secondary);
    if (!secondaryName.empty() && secondaryName != "Unnamed")
        return secondaryName;

    if (!primary.IsNull())
        return labelEntryName(primary, fallbackPrefix);

    if (!secondary.IsNull())
        return labelEntryName(secondary, fallbackPrefix);

    return fallbackPrefix;
}
}

void XCAFDocProcessor::initializeDocumentProcessing()
{
    // Clear all per-document caches so stale data from a previous file never leaks in.
    BRepToAssimpConverter::clearColorCache();
    myRefCache.Clear();
}

// Traverse the STEP assembly structure and extract shapes and names
void XCAFDocProcessor::traverseXCAFAssembly(
	const Handle(XCAFDoc_ShapeTool)& shapeTool,
	const Handle(XCAFDoc_ColorTool)& colorTool,
	const TDF_Label& label,
	const TopLoc_Location& parentLoc,
	std::vector<TopoDS_Shape>& outShapes,
	std::vector<Quantity_Color>& outColors,
	std::vector<std::string>& outNames)
{
	// 1) Assembly?  Recurse its components (cycle-safe)
	if (shapeTool->IsAssembly(label))
	{
		TDF_LabelSequence comps;
		shapeTool->GetComponents(label, comps);
		for (Standard_Integer i = 1; i <= comps.Length(); ++i)
		{
			traverseXCAFAssembly(shapeTool, colorTool, comps.Value(i), parentLoc, outShapes, outColors, outNames);
		}
		return;
	}

	// 2) Compute this instance's transform
	TopLoc_Location loc = parentLoc * shapeTool->GetLocation(label);

	// 3) If it's a reference, resolve to its definition label (cache-first).
	TDF_Label defLabel = label;
	if (shapeTool->IsReference(label))
	{
		if (!myRefCache.Find(label, defLabel))
		{
			TDF_Label tmp;
			if (shapeTool->GetReferredShape(label, tmp))
			{
				defLabel = tmp;
				myRefCache.Bind(label, defLabel);
			}
		}
	}

	// 4) If that definition is *also* an assembly, dive in (so we don't name a sub-assembly as if it were a leaf)
	if (shapeTool->IsAssembly(defLabel))
	{
		// an instance of an assemblyrecurse into its real children
		TDF_LabelSequence comps;
		shapeTool->GetComponents(defLabel, comps);
		for (Standard_Integer i = 1; i <= comps.Length(); ++i)
		{
			traverseXCAFAssembly(shapeTool, colorTool, comps.Value(i), loc, outShapes, outColors, outNames);
		}
		return;
	}

	// 5) Now defLabel must be a true leaf part definition-grab its shape
	TopoDS_Shape shape = shapeTool->GetShape(defLabel);
	if (shape.IsNull()) return;

	shape.Move(loc);
	outShapes.push_back(shape);

	// 6) Extract the name from the *definition* label (defLabel), falling back to the instance label only if needed
	std::string name = resolvedLabelName(defLabel, label, "Part");

	// 7) Add instance number to the name (e.g., Wheel.1, Wheel.2)
	// --- Extract Instance Number from the label ---
	static std::map<std::string, int> nameCountMap;
	int instanceNum = 1; // Default value if no instance number is found
	// Attempt to extract instance number based on shapeTool's label information
	// Check if the name is already counted (for handling multiple instances)
	if (nameCountMap.find(name) != nameCountMap.end())
	{
		instanceNum = ++nameCountMap[name];
	}
	else
	{
		nameCountMap[name] = 1;
	}

	std::string instanceName = name + "." + std::to_string(instanceNum);
	outNames.push_back(instanceName);

	// 8) (Fixed) Get the color of the shape
	Quantity_Color color;
	bool hasColor = false;
	if (!colorTool.IsNull())
	{
		hasColor = GetShapeColorFromShape(colorTool, shape, color);
	}

	if (!hasColor)
	{
		color = Quantity_NOC_GRAY95; // fallback to default if no color found
	}

	outColors.push_back(color);
}


// Traverse the STEP assembly structure and convert it to an Assimp scene
void XCAFDocProcessor::traverseXCAFAssembly(
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    const TDF_Label& label,
    const TopLoc_Location& parentLoc,
    aiNode* parentNode,
    aiScene* scene,
    int& meshIndex,
    int& processedMeshes,
    int totalMeshes)
{ 
    // 1) Extract the name from the TDF_Label
    std::string nodeName = resolvedLabelName(label, TDF_Label(), "Assembly");

    // 2) Assembly? Recurse its components (cycle-safe)
    if (shapeTool->IsAssembly(label))
    {
        TDF_LabelSequence comps;
        shapeTool->GetComponents(label, comps);

        // Create a node for this assembly.
        // Pre-allocate its children array to the exact component count so every
        // recursive call below can use direct index assignment instead of realloc().
        aiNode* assemblyNode = new aiNode();
        assemblyNode->mName        = aiString(nodeName);
        assemblyNode->mNumChildren = 0;
        assemblyNode->mChildren    = comps.Length() > 0 ? new aiNode*[comps.Length()] : nullptr;

        // parentNode's mChildren array was pre-allocated by its creator; direct assignment.
        parentNode->mChildren[parentNode->mNumChildren++] = assemblyNode;

        for (Standard_Integer i = 1; i <= comps.Length(); ++i)
        {
            traverseXCAFAssembly(shapeTool, colorTool, comps.Value(i), parentLoc, assemblyNode, scene, meshIndex, processedMeshes, totalMeshes);
        }
        return;
    }

    // 3) Compute this instance's transform
    TopLoc_Location loc = parentLoc * shapeTool->GetLocation(label);

    // 4) If it's a reference, resolve to its definition label.
    // Serve from the cache populated by countMeshes() — GetReferredShape() is only
    // called here if this instance was somehow not visited during the counting pass.
    TDF_Label defLabel = label;
    if (shapeTool->IsReference(label))
    {
        if (!myRefCache.Find(label, defLabel))
        {
            TDF_Label tmp;
            if (shapeTool->GetReferredShape(label, tmp))
            {
                defLabel = tmp;
                myRefCache.Bind(label, defLabel);
            }
        }
    }

    // 5) If it's an assembly, recurse into its components
    if (shapeTool->IsAssembly(defLabel))
    {
        TDF_LabelSequence comps;
        shapeTool->GetComponents(defLabel, comps);

        // Extract the name for the sub-assembly
        std::string subAssemblyName =
            resolvedLabelName(defLabel, label, "SubAssembly");
        // Create a node for the sub-assembly.
        // Pre-allocate its children array to the exact component count.
        aiNode* subAssemblyNode = new aiNode();
        subAssemblyNode->mName        = aiString(subAssemblyName);
        subAssemblyNode->mNumChildren = 0;
        subAssemblyNode->mChildren    = comps.Length() > 0 ? new aiNode*[comps.Length()] : nullptr;

        // Direct assignment — parentNode's array was pre-allocated by its creator.
        parentNode->mChildren[parentNode->mNumChildren++] = subAssemblyNode;

        for (Standard_Integer i = 1; i <= comps.Length(); ++i)
        {
            traverseXCAFAssembly(shapeTool, colorTool, comps.Value(i), loc, subAssemblyNode, scene, meshIndex, processedMeshes, totalMeshes);
        }
        return;
    }

    // 6) Now defLabel must be a true leaf part definition - grab its shape
    TopoDS_Shape shape = shapeTool->GetShape(defLabel);
    if (shape.IsNull()) return;

    //shape.Move(loc);

    // 7) Extract the name for the leaf node
    std::string leafNodeName = resolvedLabelName(defLabel, label, "Part");

    // 8) Convert the shape into a sub-scene with enhanced color extraction
    aiScene* subScene = BRepToAssimpConverter::convert(shape, colorTool, shapeTool, defLabel, label, meshIndex, nodeName);

    if (subScene == nullptr)
        return;
    // Update the progress bar
    processedMeshes++;

    if (processedMeshes % 5 == 0 || processedMeshes == totalMeshes)
    {
        double progress = static_cast<double>(processedMeshes) / totalMeshes;
        MainWindow::setProgressValue(progress * 100);
    }

    // 9) Merge sub-scene into the main scene
    if (subScene)
    {
        unsigned int meshBase = scene->mNumMeshes;        
        unsigned int materialBase = scene->mNumMaterials;

        // Append meshes
		int newSize = sizeof(aiMesh*) * (scene->mNumMeshes + subScene->mNumMeshes);
        auto* temp = realloc(scene->mMeshes, newSize);
        if (temp)
        {
            scene->mMeshes = static_cast<aiMesh**>(temp);
        }
        else
        {            
            throw std::bad_alloc();
        }        
        for (unsigned int m = 0; m < subScene->mNumMeshes; ++m)
        {
            aiMesh* mesh = subScene->mMeshes[m];
            scene->mMeshes[meshBase + m] = mesh;
            // IMPORTANT: Update material index, don't overwrite it
            mesh->mMaterialIndex += materialBase;
        }
        scene->mNumMeshes += subScene->mNumMeshes;

        // Append materials
        newSize = sizeof(aiMaterial*) * (scene->mNumMaterials + subScene->mNumMaterials);
        auto* tempMat = realloc(scene->mMaterials, newSize);
        if (tempMat)
        {
            scene->mMaterials = static_cast<aiMaterial**>(tempMat);
        }
        else
        {            
            throw std::bad_alloc();
        }        
        for (unsigned int i = 0; i < subScene->mNumMaterials; ++i)
        {
            scene->mMaterials[materialBase + i] = subScene->mMaterials[i];
        }
        scene->mNumMaterials += subScene->mNumMaterials;

        // Attach sub-scene's root node to the parent node
        aiNode* nodeCopy = BRepToAssimpConverter::cloneNodeDeep(subScene->mRootNode);

        // Convert OpenCASCADE location to Assimp transformation matrix
        aiMatrix4x4 transformMatrix = convertLocationToMatrix(loc);

        // Apply the transformation to the node
        nodeCopy->mTransformation = transformMatrix * nodeCopy->mTransformation;

        // Adjust mesh indices in the copied node
        for (unsigned int j = 0; j < nodeCopy->mNumMeshes; ++j)
        {
            nodeCopy->mMeshes[j] += meshBase;
        }

        // Use the name from TDF_Label for the node
        nodeCopy->mName = aiString(leafNodeName);

        // Direct assignment — parentNode's array was pre-allocated by its creator.
        parentNode->mChildren[parentNode->mNumChildren++] = nodeCopy;

        // Clean up sub-scene
        subScene->mMeshes = nullptr;
        subScene->mMaterials = nullptr;
        subScene->mRootNode = nullptr;
        subScene->mNumMeshes = 0;
        subScene->mNumMaterials = 0;
        delete subScene;
    }
}

// --- Helper to get color from shape ---
bool XCAFDocProcessor::GetShapeColorFromShape(
	const Handle(XCAFDoc_ColorTool)& colorTool,
	const TopoDS_Shape& shape,
	Quantity_Color& outColor)
{
	static const XCAFDoc_ColorType colorTypes[] = {
		XCAFDoc_ColorSurf, XCAFDoc_ColorCurv, XCAFDoc_ColorGen
	};

	for (XCAFDoc_ColorType type : colorTypes)
	{
		if (colorTool->GetColor(shape, type, outColor))
			return true;
		if (colorTool->GetInstanceColor(shape, type, outColor))
			return true;
	}
	return false;
}

int XCAFDocProcessor::countMeshes(const Handle(XCAFDoc_ShapeTool)& shapeTool, const TDF_Label& label)
{
    int meshCount = 0;

    // Assembly? Recurse its components
    if (shapeTool->IsAssembly(label))
    {
        TDF_LabelSequence comps;
        shapeTool->GetComponents(label, comps);

        for (Standard_Integer i = 1; i <= comps.Length(); ++i)
        {
            meshCount += countMeshes(shapeTool, comps.Value(i));
        }
        return meshCount;
    }

    // If it's a reference, resolve to its definition label.
    // Cache the result so traverseXCAFAssembly() can reuse it without a second
    // GetReferredShape() call (E2 optimisation — one lookup per instance label).
    TDF_Label defLabel = label;
    if (shapeTool->IsReference(label))
    {
        if (!myRefCache.Find(label, defLabel))
        {
            TDF_Label tmp;
            if (shapeTool->GetReferredShape(label, tmp))
            {
                defLabel = tmp;
                myRefCache.Bind(label, defLabel);
            }
        }
    }

    // If it's an assembly, recurse into its components
    if (shapeTool->IsAssembly(defLabel))
    {
        TDF_LabelSequence comps;
        shapeTool->GetComponents(defLabel, comps);

        for (Standard_Integer i = 1; i <= comps.Length(); ++i)
        {
            meshCount += countMeshes(shapeTool, comps.Value(i));
        }
        return meshCount;
    }

    // Leaf node
    TopoDS_Shape shape = shapeTool->GetShape(defLabel);
    if (!shape.IsNull())
    {
        meshCount += 1; // Count this shape as one mesh
    }

    return meshCount;
}


aiMatrix4x4 XCAFDocProcessor::convertLocationToMatrix(const TopLoc_Location& loc)
{
    if (loc.IsIdentity())
    {
        return aiMatrix4x4(); // Identity matrix
    }

    gp_Trsf transformation = loc.Transformation();

    aiMatrix4x4 matrix;
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            // False positive from Assimp's own aiMatrix4x4::operator[] (a
            // type-punning trick over its named a1..d4 members, defined in
            // matrix4x4.inl and inlined here) that clang's analyzer
            // doesn't trust, not our indexing - i in [0,2] and j in [0,3]
            // are both in-bounds for a 4x4 matrix, and the analyzer flags
            // the identical pattern directly inside Assimp's own header
            // too.
            matrix[i][j] = transformation.Value(i + 1, j + 1); // NOLINT(clang-analyzer-security.ArrayBound)
        }
    }
    matrix[3][0] = matrix[3][1] = matrix[3][2] = 0.0f;
    matrix[3][3] = 1.0f;

    return matrix;
}
