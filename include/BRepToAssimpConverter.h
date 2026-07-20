#pragma once

#include <Quantity_Color.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Face.hxx>
#include "OcctDeprecatedAliases.h"
#include <STEPCAFControl_Reader.hxx>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TopoDS.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <map>
#include <unordered_map>
#include <vector>

// Hash by TShape pointer only (orientation-independent).
// Two TopoDS_Shape objects that share the same underlying TShape but differ only in
// orientation (e.g. FORWARD vs REVERSED as stored in the STEP transfer map vs the
// face as extracted from the BRep) will map to the same bucket.
struct ShapeHasher
{
	std::size_t operator()(const TopoDS_Shape& shape) const
	{
		return std::hash<const void*>{}(shape.IsNull() ? nullptr : shape.TShape().get());
	}
};

// Equality by IsPartner() — same TShape pointer, any orientation.
struct ShapeEqual
{
	bool operator()(const TopoDS_Shape& lhs, const TopoDS_Shape& rhs) const
	{
		return lhs.IsPartner(rhs);
	}
};

struct QuantityColorComparator
{
	constexpr static double EPSILON = 1e-6;
	bool operator()(const Quantity_Color& lhs, const Quantity_Color& rhs) const
	{
		if (std::abs(lhs.Red() - rhs.Red()) > EPSILON)
			return lhs.Red() < rhs.Red();
		if (std::abs(lhs.Green() - rhs.Green()) > EPSILON)
			return lhs.Green() < rhs.Green();
		return lhs.Blue() < rhs.Blue();
	}
};

using ShapeWithNameAndTrsf = std::tuple<TopoDS_Shape, std::string, TopLoc_Location, Quantity_Color>;

class BRepToAssimpConverter
{
public:
	// Shape → colour map populated directly from STEP model entities.
	// Key: TopoDS_Shape (face or solid, orientation-independent via ShapeHasher/ShapeEqual)
	// Value: Quantity_Color extracted from the STEP STYLED_ITEM → COLOUR_RGB chain.
	using StepColorMap = std::unordered_map<TopoDS_Shape, Quantity_Color, ShapeHasher, ShapeEqual>;

	// Flat list of line-segment endpoints {x0,y0,z0, x1,y1,z1, ...} in model space.
	// Populated by convertFaceGroupToMesh() for OCC-sourced meshes (STEP/IGES/BREP).
	using OccEdgeSegments = std::vector<float>;

	// Boundary table: bounds[i] = first vec3-index (into segments) of topological edge i.
	// bounds.back() == segments.size()/3 (sentinel).  Length == numTopoEdges + 1.
	using OccEdgeBoundaries = std::vector<int>;

	// Combined payload: flat segment array + per-topological-edge boundary table.
	struct OccEdgeData {
		OccEdgeSegments  segments;  // flat {x0,y0,z0, x1,y1,z1, ...}
		OccEdgeBoundaries bounds;   // bounds[i] = first vec3-index of topo-edge i
	};

	// Returns precomputed B-Rep edge data for the given aiMesh*, or nullptr if
	// this mesh was not produced by BRepToAssimpConverter (e.g. OBJ/glTF).
	static const OccEdgeData* getPrecomputedEdges(const aiMesh* mesh);

	// Clears the edge segment cache.  Called from clearColorCache() before each load.
	static void clearEdgeCache();

	static aiScene* convert(const std::vector<ShapeWithNameAndTrsf>& shapeTuples);
	static aiScene* convert(const TopoDS_Shape& shape, const Quantity_Color& color, int& index, const std::string& name = "");
	static aiScene* convert(
		const TopoDS_Shape& shape,
		const Handle(XCAFDoc_ColorTool)& colorTool,
		const Handle(XCAFDoc_ShapeTool)& shapeTool,
		const TDF_Label& defLabel,
		const TDF_Label& instanceLabel,
		int& meshIndex,
		const std::string& name);

	static aiNode* cloneNodeDeep(const aiNode* src);

	// Clears all per-document colour caches (s_stepColorMap).
	// Called by XCAFDocProcessor::initializeDocumentProcessing() before each new file load.
	static void clearColorCache();

	// Stores the STEP-entity colour map built by XCAFSTEPProcessor::buildStepColorMap().
	// Must be called after reader.Transfer() and before any convert() call.
	static void setStepColorMap(const StepColorMap& map);

	// Returns the deflection fraction (0.0–1.0) to use for STEP tessellation.
	// Reads the "linearDeflectionSpinBox" QSettings key written by SettingsDialog.
	// Used by both the parallel pre-tessellation pass (XCAFSTEPProcessor) and the
	// per-face fallback in convertFaceGroupToMesh().
	static Standard_Real resolveDeflectionFraction();

	// Returns the angular deflection (radians) to use for STEP tessellation.
	// Reads the "angularDeflectionSpinBox" QSettings key written by SettingsDialog.
	static Standard_Real resolveAngularDeflection();


private:

	// Per-document STEP colour map: populated from raw STEP StyledItem entities,
	// bypassing the broken XCAFDoc_ColorTool::SetColor(TopoDS_Shape) → FindShape() path.
	static StepColorMap s_stepColorMap;

	// Per-document B-Rep edge data keyed by the aiMesh* they belong to.
	// Cleared by clearEdgeCache() (called via clearColorCache()) before each load.
	static std::unordered_map<const aiMesh*, OccEdgeData> s_occEdges;

	// Tessellates all unique non-degenerate edges in faceGroup and returns them
	// as a flat segment list plus per-topological-edge boundary table.
	static OccEdgeData extractEdgesFromFaceGroup(
		const TopTools_IndexedMapOfShape& faceGroup, Standard_Real deflection);

	static bool isShapeMeshable(const TopoDS_Shape& shape);

	static aiMesh* convertFaceGroupToMesh(const TopTools_IndexedMapOfShape& faceGroup, int meshIndex, bool enableStatistics = false);

	static std::vector<aiMesh*> convertFaceGroupToMeshesWithCache(
		const TopTools_IndexedMapOfShape& faceGroup,
		const TopoDS_Shape& colorContextShape,
		int& meshIndex,
		const Handle(XCAFDoc_ColorTool)& colorTool,
		const Handle(XCAFDoc_ShapeTool)& shapeTool,
		const TDF_Label& defLabel,
		const TDF_Label& instanceLabel,
		std::map<Quantity_Color, unsigned int, QuantityColorComparator>& materialMap,
		std::vector<aiMaterial*>& materials);

	static Standard_Real computeDeflectionFromBBox(const TopTools_IndexedMapOfShape& faceGroup, Standard_Real percent = 0.01);
	static TopoDS_Face healAndTriangulateFace(const TopoDS_Face& inputFace,
		double deflection = 0.01,
		double angularDeflection = 0.5,
		double fixTolerance = 1e-6);

	static TopoDS_Face rebuildFace(const TopoDS_Face& face);

	// Lightweight triangle validation (no quality metrics unless needed)
	static inline bool isTriangleValid(const aiVector3D& v0, const aiVector3D& v1,
		const aiVector3D& v2, float threshold)
	{
		// Fast degenerate check - just cross product magnitude
		const aiVector3D edge1 = v1 - v0;
		const aiVector3D edge2 = v2 - v0;

		// Compute cross product components directly (avoid temporary aiVector3D)
		const float nx = edge1.y * edge2.z - edge1.z * edge2.y;
		const float ny = edge1.z * edge2.x - edge1.x * edge2.z;
		const float nz = edge1.x * edge2.y - edge1.y * edge2.x;

		// Check squared length directly (avoid sqrt)
		const float normalLengthSq = nx * nx + ny * ny + nz * nz;

		return normalLengthSq >= threshold;
	}

	// Fast bounding box calculation (minimal overhead)
	static aiVector3D calculateMeshScale(const TopTools_IndexedMapOfShape& faceGroup)
	{
		float minVal = FLT_MAX, maxVal = -FLT_MAX;

		// Sample only every 4th face for large face groups (statistical approximation)
		const int sampleStep = (faceGroup.Extent() > 100) ? 4 : 1;

		for (int f = 1; f <= faceGroup.Extent(); f += sampleStep)
		{
			const TopoDS_Face& face = TopoDS::Face(faceGroup(f));
			if (face.IsNull()) continue;

			Bnd_Box bbox;
			BRepBndLib::Add(face, bbox);
			if (bbox.IsVoid()) continue;

			Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
			bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);

			// Track only min/max for overall scale (faster than full bbox)
			minVal = std::min(minVal, static_cast<float>(std::min({ xmin, ymin, zmin })));
			maxVal = std::max(maxVal, static_cast<float>(std::max({ xmax, ymax, zmax })));
		}

		const float scale = (maxVal > minVal) ? (maxVal - minVal) : 1.0f;
		return aiVector3D(scale, scale, scale);
	}

	// Pre-computed threshold strategies (computed once, reused)
	struct ThresholdConfig
	{
		float strictThreshold;      // 1e-12f
		float practicalThreshold;   // 1e-8f
		float looseThreshold;       // 1e-6f
		float meshScale;
		bool useAdaptive;

		ThresholdConfig(float scale) : meshScale(scale)
		{
			// Pre-compute all thresholds once
			float scaleSq = scale * scale;
			strictThreshold = 1e-12f;
			practicalThreshold = std::max(1e-8f, scaleSq * 1e-12f);
			looseThreshold = std::max(1e-6f, scaleSq * 1e-10f);

			// Simple heuristic to choose strategy
			useAdaptive = (scale > 100.0f || scale < 0.01f);
		}

		inline float getThreshold() const
		{
			if (!useAdaptive) return practicalThreshold; // Most common case

			if (meshScale > 1000.0f) return looseThreshold;
			if (meshScale < 0.1f) return strictThreshold;
			return practicalThreshold;
		}
	};
};
