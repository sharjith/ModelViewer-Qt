#pragma once

// GUI-free data model and tabular import for general-purpose 3-D plots. Plot data deliberately has no dependency
// on Simulation/ResultDataset: CSV files and pasted text become ordinary scene content in a ModelViewer document.

#include <QChar>
#include <QString>
#include <QStringList>

#include <array>
#include <cstddef>
#include <limits>
#include <variant>
#include <vector>

enum class Plot3DPrimitive
{
	Surface,
	Contour,
	Line,
	Scatter,
	Bar,
	Voxel,
	Quiver
};

enum class Plot3DAxisScale
{
	Linear,
	Log10,
	SymLog
};

struct Plot3DAxisConfig
{
	QString label;
	Plot3DAxisScale scale = Plot3DAxisScale::Linear;
	bool automaticRange = true;
	double minimum = 0.0;
	double maximum = 1.0;
	int targetTicks = 6;
	double symlogLinearThreshold = 1.0;
};

struct Plot3DPoint
{
	double x = 0.0, y = 0.0, z = 0.0;
};

struct Plot3DSample
{
	Plot3DPoint position;
	// Optional colour/scalar channel. NaN means that the primitive should use z or its uniform material.
	double value = std::numeric_limits<double>::quiet_NaN();
};

struct Plot3DSurfaceData { std::vector<Plot3DSample> samples; };
struct Plot3DLineData { std::vector<Plot3DSample> samples; };
struct Plot3DScatterData { std::vector<Plot3DSample> samples; };

struct Plot3DBar
{
	double x = 0.0, y = 0.0;
	double base = 0.0, height = 0.0;
	double width = 0.8, depth = 0.8;
	double value = std::numeric_limits<double>::quiet_NaN();
};
struct Plot3DBarData { std::vector<Plot3DBar> bars; };

struct Plot3DVoxel
{
	int x = 0, y = 0, z = 0;
	double occupancy = 1.0;
};
struct Plot3DVoxelData { std::vector<Plot3DVoxel> voxels; };

struct Plot3DQuiver
{
	Plot3DPoint position;
	Plot3DPoint vector;
	double value = std::numeric_limits<double>::quiet_NaN();
};
struct Plot3DQuiverData { std::vector<Plot3DQuiver> arrows; };

using Plot3DContent = std::variant<Plot3DSurfaceData, Plot3DLineData, Plot3DScatterData,
	Plot3DBarData, Plot3DVoxelData, Plot3DQuiverData>;

struct Plot3DDataset
{
	QString name;
	Plot3DPrimitive primitive = Plot3DPrimitive::Surface;
	std::array<Plot3DAxisConfig, 3> axes{ Plot3DAxisConfig{ QStringLiteral("X") },
	                                      Plot3DAxisConfig{ QStringLiteral("Y") },
	                                      Plot3DAxisConfig{ QStringLiteral("Z") } };
	Plot3DContent content = Plot3DSurfaceData();

	std::size_t itemCount() const;
	bool empty() const { return itemCount() == 0; }
};

struct Plot3DCsvOptions
{
	QChar delimiter = QLatin1Char(',');
	bool firstRowIsHeader = true;
};

// Parsed text is retained as strings so a future import panel can preview it and let the user choose column roles
// before numeric conversion. Rows always have exactly headers.size() cells.
struct Plot3DCsvTable
{
	QStringList headers;
	std::vector<QStringList> rows;

	bool empty() const { return rows.empty(); }
	int columnCount() const { return headers.size(); }
};

// Column indices used to turn a table into one primitive. x/y/z are positions for Surface/Line/Scatter/Quiver,
// bar x/y/height for Bar (Y may be -1 for a one-dimensional histogram), and integer grid indices for Voxel.
// Optional columns use -1.
struct Plot3DColumnMapping
{
	int x = 0, y = 1, z = 2;
	int value = -1;
	int u = 3, v = 4, w = 5; // Quiver vector
	int base = -1;            // Bar base (default 0)
	int width = -1, depth = -1; // Bar footprint (defaults below)
};

struct Plot3DBuildOptions
{
	double defaultBarWidth = 0.8;
	double defaultBarDepth = 0.8;
};

// RFC-4180-style quoted cells and doubled quote escapes are supported, including newlines inside a quoted cell.
// Blank physical rows are ignored. Error text includes the one-based row where possible.
bool parsePlot3DCsv(const QString& text, const Plot3DCsvOptions& options, Plot3DCsvTable& out, QString* error = nullptr);

// Converts only the columns needed by `primitive`; every required value must be finite. Surface/Line/Scatter use z
// as their scalar value when mapping.value is absent. Voxel coordinates must be non-negative integers.
bool buildPlot3DDataset(const Plot3DCsvTable& table, Plot3DPrimitive primitive, const Plot3DColumnMapping& mapping,
	Plot3DDataset& out, QString* error = nullptr, const Plot3DBuildOptions& options = Plot3DBuildOptions());

// Bounds of the plotted mesh extent (complete bars and unit voxels included). Quiver arrows are camera-stable
// overlays, so their raw vector magnitudes do not expand the coordinate box; its sites define the plot extent.
// False when empty.
bool plot3DDataBounds(const Plot3DDataset& dataset, double minimum[3], double maximum[3]);

QString plot3DPrimitiveName(Plot3DPrimitive primitive);
