#pragma once

// Simulation result data model - see docs/simulation_results_design.md (section 4).
//
// GUI-free and GL-free on purpose: depends only on QtCore + the standard library, so the readers and
// boundary extraction built on it are unit-testable without a window or a GL context. This is the
// SOURCE OF TRUTH for a loaded result; the mesh shown in the scene is derived from it, never the other
// way round. A future Simulation workbench is built on this class, not on scene meshes.

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

enum class ResultCellType : std::uint8_t
{
	Unsupported = 0, // kept as a placeholder so cell indices, offsets and cell-field tuples stay aligned
	Line,
	Triangle,
	Quad,
	Tetra,
	Hexahedron,
	Wedge,
	Pyramid,
	// Quadratic (second-order) cells, VTK node order: corner nodes first, then mid-edge nodes. Phase 0 shows
	// them through their corner nodes only (see resultCellCornerType()); mid-edge nodes are kept in the
	// dataset but ignored for display, so curved edges are not represented yet.
	Triangle6,
	Quad8,
	Tetra10,
	Hexahedron20,
	Wedge15,
	Pyramid13
};

// Number of nodes a cell of this type must reference; 0 for Unsupported (any count accepted).
int resultCellNodeCount(ResultCellType type);
bool resultCellIsVolume(ResultCellType type);  // Tetra, Hexahedron, Wedge, Pyramid and their quadratic forms
bool resultCellIsSurface(ResultCellType type); // Triangle, Quad and their quadratic forms
bool resultCellIsQuadratic(ResultCellType type);
// The linear cell whose corners are the first N nodes of `type` (identity for linear/unsupported types).
ResultCellType resultCellCornerType(ResultCellType type);

// Maps a VTK cell-type id (vtkCellType.h) to ours; anything not yet supported is Unsupported.
ResultCellType resultCellTypeFromVtk(int vtkCellTypeId);

enum class ResultFieldAssociation
{
	Node,
	Cell
};

// One named field. `stepData[s]` holds tupleCount * components floats for step s, or is EMPTY when that
// step is not loaded (the dataset will load steps lazily; Phase 0 files have exactly one step).
struct ResultField
{
	QString name;
	ResultFieldAssociation association = ResultFieldAssociation::Node;
	int components = 1; // 1 scalar, 3 vector, 6/9 tensor
	// Filled by the unit handling (design section 7); empty until the user/file confirms them.
	QString quantityKind;
	QString fileUnit;
	std::vector<std::vector<float>> stepData;

	std::size_t tupleCount(std::size_t step = 0) const
	{
		return (step < stepData.size() && components > 0) ? stepData[step].size() / static_cast<std::size_t>(components) : 0;
	}
};

struct ResultStep
{
	double time = 0.0;
	QString label;
};

class ResultDataset
{
public:
	QString sourcePath;
	QString solverName; // empty when unknown
	QString lengthUnit; // empty until confirmed - never guess silently (design section 7)

	// Nodes: xyz per node in the file's own length unit. nodeIds is optional (empty => id == index).
	std::vector<float> nodePositions;
	std::vector<std::int64_t> nodeIds;

	// Cells in VTK layout: cell i uses cellConnectivity[cellOffsets[i] .. cellOffsets[i+1]).
	// cellOffsets has cellCount()+1 entries. cellIds is optional (empty => id == index).
	std::vector<ResultCellType> cellTypes;
	std::vector<std::uint32_t> cellOffsets;
	std::vector<std::uint32_t> cellConnectivity;
	std::vector<std::int64_t> cellIds;

	std::vector<ResultStep> steps;
	std::vector<ResultField> fields;

	std::size_t nodeCount() const { return nodePositions.size() / 3; }
	std::size_t cellCount() const { return cellTypes.size(); }
	std::size_t stepCount() const { return steps.size(); }

	std::int64_t nodeId(std::size_t index) const
	{
		return index < nodeIds.size() ? nodeIds[index] : static_cast<std::int64_t>(index);
	}
	std::int64_t cellId(std::size_t index) const
	{
		return index < cellIds.size() ? cellIds[index] : static_cast<std::int64_t>(index);
	}

	const ResultField* findField(const QString& name, ResultFieldAssociation association) const;

	// Structural consistency check. Returns an empty string when the dataset is valid, otherwise a
	// description of the FIRST problem found. Readers call this before handing a dataset out, so every
	// consumer can rely on: all offsets/indices in range, cell node counts matching their type, and
	// every loaded field step sized tupleCount * components.
	QString validate() const;
};
