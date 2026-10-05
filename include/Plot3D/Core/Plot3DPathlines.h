#pragma once

// GUI-free pathline tracing shared by the formula source (Plot3DFormula) and the CSV time-series source: particles released at
// the start time are carried through an UNSTEADY vector field with classical RK4 and drawn as trails coloured by time.

#include "Plot3DData.h"
#include "Plot3DMeshBuilder.h"

#include <QString>

#include <functional>

struct Plot3DVec3
{
	double x = 0.0, y = 0.0, z = 0.0;
};

// The field at a position and time. Returns false (and sets *error) when it cannot be evaluated.
using Plot3DUnsteadyField = std::function<bool(const Plot3DVec3& position, double time, Plot3DVec3& velocity, QString* error)>;

struct Plot3DPathlineDomain
{
	double xMinimum = 0.0, xMaximum = 1.0, yMinimum = 0.0, yMaximum = 1.0;
	bool boundZ = false;                 // also stop a trail that leaves the z range
	double zMinimum = 0.0, zMaximum = 0.0;
	double seedZ = 0.0;                  // seeds sit in the z = seedZ plane
};

// Releases `seedCount` seeds along Y at the middle of the X range and advances them through `steps` equal time steps from
// tMinimum to tMaximum. A trail ends where its particle leaves the domain, becomes non-finite, or the field cannot be
// evaluated (the latter is an error). Output is GL_LINES-style vertex pairs whose value is the time at that vertex.
bool tracePlot3DPathlines(const Plot3DUnsteadyField& field, const Plot3DPathlineDomain& domain, int seedCount,
	double tMinimum, double tMaximum, int steps, Plot3DMeshData& out, QString* error = nullptr);

// Which table columns hold what. Indices into the table's headers; z may be -1 (a planar field, z = 0).
struct Plot3DTimeSeriesColumns
{
	int time = -1, x = -1, y = -1, z = -1, u = -1, v = -1, w = -1;
};

// A vector field sampled on a COMPLETE regular grid in (t, x, y[, z]), one table row per grid node, interpolated linearly in
// space and time (clamped outside the grid). Rows may come in any order. Rejected with a message when a column is not numeric,
// a node is missing or repeated, or the grid is larger than about five million nodes.
class Plot3DTimeSeriesField
{
public:
	bool load(const Plot3DCsvTable& table, const Plot3DTimeSeriesColumns& columns, QString* error = nullptr);
	bool sample(const Plot3DVec3& position, double time, Plot3DVec3& velocity) const;

	double timeMinimum() const { return _t.front(); }
	double timeMaximum() const { return _t.back(); }
	Plot3DPathlineDomain domain() const;
	std::size_t nodeCount() const { return _u.size(); }
	int timeSteps() const { return static_cast<int>(_t.size()); }

private:
	std::vector<double> _t, _x, _y, _z;
	std::vector<double> _u, _v, _w; // index ((it * nz + iz) * ny + iy) * nx + ix
};

// Loads the table as a Plot3DTimeSeriesField and traces its pathlines (seeds along Y at the middle of X, in the middle z plane).
bool buildPlot3DTimeSeriesPathlines(const Plot3DCsvTable& table, const Plot3DTimeSeriesColumns& columns, int seedCount, int steps,
	Plot3DMeshData& out, QString* error = nullptr);
