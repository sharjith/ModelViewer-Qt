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
};

struct Plot3DAxisLabel
{
	QString text;
	QVector3D position;
};

struct Plot3DReferencePlane
{
	std::array<QVector3D, 4> corners;
};

struct Plot3DAxisLayout
{
	std::array<double, 3> minimum{};
	std::array<double, 3> maximum{};
	std::array<std::vector<Plot3DAxisTick>, 3> ticks;
	std::vector<Plot3DLineSegment> axisLines;
	std::vector<Plot3DLineSegment> tickLines;
	std::vector<Plot3DAxisLabel> labels;
	std::vector<Plot3DReferencePlane> referencePlanes;
};

double plot3DTransformAxisValue(double value, const Plot3DAxisConfig& config, bool* valid = nullptr);
double plot3DInverseAxisValue(double value, const Plot3DAxisConfig& config);
std::vector<Plot3DAxisTick> plot3DGenerateAxisTicks(double minimum, double maximum,
	const Plot3DAxisConfig& config);

class Plot3DAxisController
{
public:
	void setReferencePlanesVisible(bool xy, bool xz, bool yz);
	bool buildLayout(const std::array<Plot3DAxisConfig, 3>& axes, const double dataMinimum[3],
		const double dataMaximum[3], Plot3DAxisLayout& layout, QString* error = nullptr) const;

private:
	std::array<bool, 3> _referencePlanes{ true, false, false }; // XY, XZ, YZ
};
