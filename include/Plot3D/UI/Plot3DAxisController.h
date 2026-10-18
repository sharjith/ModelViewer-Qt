#pragma once

#include "Plot3DData.h"

#include <QVector3D>

#include <array>
#include <vector>

struct Plot3DAxisTick
{
	double value = 0.0;
	double transformedValue = 0.0;
	QString label;
};

struct Plot3DLineSegment
{
	QVector3D first;
	QVector3D second;
	QVector3D color{ 0.7f, 0.7f, 0.7f };
};

struct Plot3DAxisLabel
{
	QString text;
	QVector3D position;
	QVector3D color{ 0.95f, 0.97f, 0.99f };
};

struct Plot3DAxisTitle
{
	QString text;
	QVector3D first;
	QVector3D second;
	QVector3D color{ 0.95f, 0.97f, 0.99f };
};

struct Plot3DReferencePlane
{
	std::array<QVector3D, 4> corners;
	QVector3D color{ 0.35f, 0.45f, 0.55f };
};

struct Plot3DAxisLayout
{
	std::array<double, 3> minimum{};
	std::array<double, 3> maximum{};
	std::array<std::vector<Plot3DAxisTick>, 3> ticks;
	std::vector<Plot3DLineSegment> axisLines;
	std::vector<Plot3DLineSegment> tickLines;
	// Low-contrast guides on the three planes meeting at the minimum corner.
	// Kept separate so the coloured box edges remain visually dominant.
	std::vector<Plot3DLineSegment> gridLines;
	std::vector<Plot3DAxisLabel> labels;
	std::vector<Plot3DAxisTitle> axisTitles;
	std::vector<Plot3DReferencePlane> referencePlanes;
	float referencePlaneOpacity = 0.08f;
	QString title;
};

double plot3DTransformAxisValue(double value, const Plot3DAxisConfig& config, bool* valid = nullptr);
double plot3DInverseAxisValue(double value, const Plot3DAxisConfig& config);
std::vector<Plot3DAxisTick> plot3DGenerateAxisTicks(double minimum, double maximum,
	const Plot3DAxisConfig& config);

// Whether `axes` can place data spanning [minimum, maximum] on each axis: a Log 10 axis needs a positive minimum, a SymLog one a positive linear
// threshold. On false, `badAxis` (when given) receives the first axis (0..2) that cannot. The axes of the box are shared by every plot in it, so this is
// asked of each plot's data before a scale is applied.
bool plot3DAxesFitBounds(const std::array<Plot3DAxisConfig, 3>& axes, const std::array<double, 3>& minimum, const std::array<double, 3>& maximum,
	int* badAxis = nullptr);

// Re-aims vectors drawn in the space of the axes `from` for the space of `to`: each vector is carried by the local stretch of the scale at its site
// (the Jacobian of the axis-wise transform is diagonal: to's slope over from's slope, per axis), so an arrow keeps pointing along the same data
// direction. Each vector keeps its OWN length (it carries the magnitude; only the direction changes). `sitePositions` holds 3 floats per vector, the site
// in the `from` space; a vector with no usable stretch is left as it is.
void plot3DStretchVectors(std::vector<float>& vectors, const std::vector<float>& sitePositions, const std::array<Plot3DAxisConfig, 3>& from,
	const std::array<Plot3DAxisConfig, 3>& to);

// A voxel grid drawn on non-linear axes: the cells of a grid (unit cells in data space, `origin` + index) are resampled onto a grid that is REGULAR in the
// scaled space, because the volume renderer needs a regular grid. Each output cell takes the value of the data cell under its centre (nearest, so an empty
// cell stays empty); a scaled axis gets up to twice the cells so the stretched part keeps its detail (at most 256). A Linear axis is copied as it is.
// False when the grid's extent cannot be placed on the axes (a Log 10 axis over a grid reaching zero).
struct Plot3DResampledVoxels
{
	std::vector<float> values; // x fastest, then y, then z (the layout of Plot3DVoxelGrid / VolumeGrid)
	int dim[3] = { 0, 0, 0 };
	double minimum[3] = { 0.0, 0.0, 0.0 }; // the scaled-space corner of the grid
	double size[3] = { 1.0, 1.0, 1.0 };    // the scaled-space size of one output cell
};
bool plot3DResampleVoxels(const std::vector<float>& values, const int dim[3], const double origin[3], const std::array<Plot3DAxisConfig, 3>& axes,
	Plot3DResampledVoxels& out);

// True when two configs draw an axis the same way (same scale, and the same linear threshold for SymLog).
bool plot3DSameAxisScale(const Plot3DAxisConfig& a, const Plot3DAxisConfig& b);
// d(transformed)/d(value) at `value`: how much the scale stretches that axis there (1 for Linear).
double plot3DAxisScaleSlope(double value, const Plot3DAxisConfig& config);

class Plot3DAxisController
{
public:
	void setReferencePlanesVisible(bool xy, bool xz, bool yz);
	void setReferencePlaneOpacity(float opacity);
	bool buildLayout(const std::array<Plot3DAxisConfig, 3>& axes, const double dataMinimum[3],
		const double dataMaximum[3], Plot3DAxisLayout& layout, QString* error = nullptr,
		const QString& title = {}) const;

private:
	std::array<bool, 3> _referencePlanes{ true, false, false }; // XY, XZ, YZ
	float _referencePlaneOpacity = 0.08f;
};
