#pragma once

// GUI-free generation of plots from their definitions. One function turns a Plot3DGeneratedSpec (the same record Edit Plot
// stores in a session) into plot data, so Preview, Build, Rebuild and the status line of the Add 3D Plot dialog all go through the
// same code instead of each repeating "read the widgets, call the right builder". Everything here is QtCore + std only.

#include "Plot3DData.h"
#include "Plot3DFormula.h"
#include "Plot3DMeshBuilder.h"
#include "Plot3DPathlines.h"
#include "Plot3DSession.h"

#include <QString>
#include <QVector>

// The creation dialog's Data source. The numeric values are PERSISTED (Plot3DGeneratedSpec::sourceMode, saved in .mvf files) and
// must not change.
enum class Plot3DSourceKind
{
	Csv = 0,
	FormulaSurface = 1,
	ParametricSurface = 2,
	ParametricCurve = 3,
	FormulaVectorField = 4,
	ImplicitSurface = 5,
	FormulaStreamlines = 6,
	FormulaPathlines = 7,
	CsvTimeSeries = 8,
	ImageSurface = 9 // a picture on a flat plane (a textured quad)
};

inline int plot3DSourceInt(Plot3DSourceKind kind) { return static_cast<int>(kind); }
// False for a value that is not a source (a newer file).
bool plot3DSourceFromInt(int value, Plot3DSourceKind& kind);

// A source whose data comes from an expression / definition (not a table the user supplies).
bool plot3DSourceIsGenerated(Plot3DSourceKind kind);
// Sources that draw pathlines (the time-coloured trails): the formula and the CSV time-series ones.
bool plot3DSourceIsPathline(Plot3DSourceKind kind);
// The plot primitive a source produces (a formula surface may also be a Contour: see generatePlot3D()).
Plot3DPrimitive plot3DSourcePrimitive(Plot3DSourceKind kind);

// OpenGL primitive modes by value, so Core code can name them without a GL header.
namespace Plot3DGl
{
	constexpr unsigned int kPoints = 0x0000, kLines = 0x0001, kLineStrip = 0x0003, kTriangles = 0x0004;
}

struct Plot3DGenerated
{
	Plot3DPrimitive primitive = Plot3DPrimitive::Surface;
	// A dataset source (formula surface / contour, parametric curve, vector field) yields its data here; the mesh is then built
	// from it by plot3DMeshForDataset(). A mesh source (parametric / implicit surface, streamlines, pathlines, time series)
	// yields the finished mesh in `mesh` with its primitive mode.
	bool hasDataset = false;
	Plot3DDataset dataset;
	Plot3DMeshData mesh;
	unsigned int primitiveMode = Plot3DGl::kTriangles;
	QString imagePath; // an image plane: the picture its mesh (which carries UVs) is textured with
};

// Builds the plot a generated spec describes (sources 1..9). A formula surface takes `formulaPrimitive` (Surface or Contour); the
// others have one fixed primitive. The CSV time series needs the table and its column choices; the other sources ignore them.
// False (with a message) when the definition cannot be evaluated or is out of range.
bool generatePlot3D(const Plot3DGeneratedSpec& spec, Plot3DPrimitive formulaPrimitive, const Plot3DCsvTable* table,
	const Plot3DTimeSeriesColumns* columns, Plot3DGenerated& out, QString* error = nullptr);

// How a dataset is turned into a mesh: the variants a primitive can have, and the settings the presentation controls own.
struct Plot3DMeshOptions
{
	int contourLevels = 10;
	bool contourProjected = false;
	bool stems = false, errorBars = false, filled = false; // Scatter variants (mutually exclusive)
	double baseZ = 0.0;                                     // stem / filled-scatter base plane
	double barWidthScale = 1.0, barDepthScale = 1.0;
};

// The mesh of a Surface / Contour / Line / Scatter / Bar dataset, with its primitive mode. Quiver and Voxel are renderer plots (no
// mesh of their own): they return false.
bool plot3DMeshForDataset(const Plot3DDataset& dataset, const Plot3DMeshOptions& options, Plot3DMeshData& out,
	unsigned int& primitiveMode, QString* error = nullptr);

// The extent of the plot as it will be drawn: the dataset's bounds, widened to the base plane of stems / filled ribbons and to
// the scaled bars. False when the dataset is empty.
bool plot3DDatasetBounds(const Plot3DDataset& dataset, const Plot3DMeshOptions& options, double minimum[3], double maximum[3]);

// The extent of a finished mesh's vertices (a mesh source's data bounds). False when it is empty.
bool plot3DMeshBounds(const Plot3DMeshData& mesh, double minimum[3], double maximum[3]);

// One preset of a source as a ready-to-use spec (name is what the preset combo shows).
struct Plot3DPresetEntry
{
	QString name;
	Plot3DGeneratedSpec spec;
};

// The presets of a generated source (Formula surface, Parametric surface / curve, vector field and streamlines (they share their
// presets), Implicit surface, pathlines), converted from their per-source preset structs to specs. Empty for the CSV sources.
QVector<Plot3DPresetEntry> plot3DPresetEntries(Plot3DSourceKind kind);
