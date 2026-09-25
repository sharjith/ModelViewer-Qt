#pragma once

// Simulation result data model - see docs/simulation_results_design.md (section 4).
//
// GUI-free and GL-free on purpose: depends only on QtCore + the standard library, so the readers and
// boundary extraction built on it are unit-testable without a window or a GL context. This is the
// SOURCE OF TRUTH for a loaded result; the mesh shown in the scene is derived from it, never the other
// way round. A future Simulation workbench is built on this class, not on scene meshes.

#include <QString>
#include <QStringList>

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

class ResultDataset;
// User-facing warnings for a freshly read dataset's cell types: quadratic cells shown through their corner
// nodes, and cells of an unsupported type that will not be displayed. Empty when there is nothing to say.
QStringList resultCellTypeWarnings(const ResultDataset& dataset);

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
	// Optional names of the components when the file provides them (CalculiX: SXX, SYY, SZZ, SXY, SYZ, SZX).
	// Empty, or exactly `components` entries.
	std::vector<QString> componentNames;
	// Units (see ResultUnits.h, design section 7). quantityKind is a ResultUnits kind id ("pressure", "length", ...);
	// fileUnit is what the stored numbers are written in, displayUnit what they are shown in (empty = the file
	// unit). All empty means "not specified". unitConfirmed is false while fileUnit is only a guess.
	QString quantityKind;
	QString fileUnit;
	QString displayUnit;
	bool unitConfirmed = false;
	// For a field computed from another (von Mises from STRESS): that field's index; -1 otherwise. Derived fields
	// always share their source's units.
	int derivedFromField = -1;
	std::vector<std::vector<float>> stepData;
	// Optional min/max over data that is NOT in stepData (a stored snapshot keeps only the boundary vertices but
	// remembers the range of the whole model, in file units): [step][selector][lo, hi] flattened, NaN = unknown. A
	// selector is a component (0..components-1) or, for a 3-component field, the magnitude (index 3); a scalar has
	// one. Empty = none: the range is whatever stepData holds. Only ever WIDENS the range buildDisplayScalar reports.
	std::vector<float> storedRange;

	std::size_t tupleCount(std::size_t step = 0) const
	{
		return (step < stepData.size() && components > 0) ? stepData[step].size() / static_cast<std::size_t>(components) : 0;
	}
};

// Range selectors of a field (see ResultField::storedRange).
inline int resultRangeSelectorCount(int components) { return components == 1 ? 1 : (components == 3 ? 4 : components); }
// The selector a request for `component` (-1 = the scalar itself, or the magnitude of a 3-component field) uses;
// -1 when there is none (a tensor without an explicit component).
inline int resultRangeSelector(int components, int component)
{
	if (components == 1)
		return 0;
	if (component >= 0)
		return component < components ? component : -1;
	return components == 3 ? 3 : -1;
}

struct ResultStep
{
	double time = 0.0;
	QString label;    // e.g. "Mode 3"; empty for a plain time step
	QString timeUnit; // unit of `time` when the file says what it is (a modal step's time is a frequency: "Hz")
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
