#include "Plot3DAxisController.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace
{
	double niceStep(double range, int targetTicks)
	{
		if (!(range > 0.0)) return 1.0;
		const double raw = range / std::max(1, targetTicks);
		const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
		const double normalized = raw / magnitude;
		const double factor = normalized < 1.5 ? 1.0 : (normalized < 3.5 ? 2.0 : (normalized < 7.5 ? 5.0 : 10.0));
		return factor * magnitude;
	}

	QString formatTick(double value)
	{
		if (std::abs(value) < 1.0e-12) value = 0.0;
		const double magnitude = std::abs(value);
		if (magnitude != 0.0 && (magnitude >= 1.0e5 || magnitude < 1.0e-3))
			return QString::number(value, 'g', 5);
		QString text = QString::number(value, 'f',
			std::clamp(static_cast<int>(std::ceil(-std::log10(std::max(magnitude, 1.0e-12)))) + 1, 0, 6));
		if (text.contains(QLatin1Char('.')))
		{
			text.remove(QRegularExpression(QStringLiteral("0+$")));
			if (text.endsWith(QLatin1Char('.'))) text.chop(1);
		}
		return text;
	}

	QVector3D point(double x, double y, double z) { return QVector3D(float(x), float(y), float(z)); }

	// Additive primary colours make each edge's orientation apparent: an X/Y
	// edge is yellow, X/Z is magenta, and Y/Z is cyan.  The pale far corner
	// is the combination of all three directions.
	const QVector3D xAxisColor(0.96f, 0.18f, 0.20f);
	const QVector3D yAxisColor(0.18f, 0.84f, 0.30f);
	const QVector3D zAxisColor(0.20f, 0.45f, 1.00f);
	const QVector3D xyAxisColor(0.95f, 0.86f, 0.18f);
	const QVector3D xzAxisColor(0.94f, 0.32f, 0.92f);
	const QVector3D yzAxisColor(0.18f, 0.88f, 0.94f);
	const QVector3D xyzAxisColor(0.92f, 0.94f, 0.98f);
}

double plot3DTransformAxisValue(double value, const Plot3DAxisConfig& config, bool* valid)
{
	bool ok = std::isfinite(value);
	double result = value;
	if (ok && config.scale == Plot3DAxisScale::Log10)
	{
		ok = value > 0.0;
		if (ok) result = std::log10(value);
	}
	else if (ok && config.scale == Plot3DAxisScale::SymLog)
	{
		ok = config.symlogLinearThreshold > 0.0 && std::isfinite(config.symlogLinearThreshold);
		if (ok) result = std::copysign(std::log10(1.0 + std::abs(value) / config.symlogLinearThreshold), value);
	}
	if (valid) *valid = ok;
	return ok ? result : 0.0;
}

double plot3DInverseAxisValue(double value, const Plot3DAxisConfig& config)
{
	if (config.scale == Plot3DAxisScale::Log10) return std::pow(10.0, value);
	if (config.scale == Plot3DAxisScale::SymLog)
		return std::copysign(config.symlogLinearThreshold * (std::pow(10.0, std::abs(value)) - 1.0), value);
	return value;
}

bool plot3DAxesFitBounds(const std::array<Plot3DAxisConfig, 3>& axes, const std::array<double, 3>& minimum, const std::array<double, 3>& maximum, int* badAxis)
{
	for (int axis = 0; axis < 3; ++axis)
	{
		const Plot3DAxisConfig& config = axes[static_cast<std::size_t>(axis)];
		const double lo = minimum[static_cast<std::size_t>(axis)], hi = maximum[static_cast<std::size_t>(axis)];
		bool ok = std::isfinite(lo) && std::isfinite(hi);
		if (ok && config.scale == Plot3DAxisScale::Log10)
			ok = lo > 0.0;
		else if (ok && config.scale == Plot3DAxisScale::SymLog)
			ok = config.symlogLinearThreshold > 0.0 && std::isfinite(config.symlogLinearThreshold);
		if (!ok)
		{
			if (badAxis)
				*badAxis = axis;
			return false;
		}
	}
	return true;
}

void plot3DStretchVectors(std::vector<float>& vectors, const std::vector<float>& sitePositions, const std::array<Plot3DAxisConfig, 3>& from,
	const std::array<Plot3DAxisConfig, 3>& to)
{
	const std::size_t count = std::min(vectors.size(), sitePositions.size()) / 3;
	for (std::size_t i = 0; i < count; ++i)
	{
		double stretch[3] = { 1.0, 1.0, 1.0 };
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			const double data = plot3DInverseAxisValue(static_cast<double>(sitePositions[i * 3 + axis]), from[axis]);
			const double ratio = plot3DAxisScaleSlope(data, to[axis]) / plot3DAxisScaleSlope(data, from[axis]);
			if (std::isfinite(ratio) && ratio > 0.0)
				stretch[axis] = ratio;
		}
		const double original[3] = { vectors[i * 3], vectors[i * 3 + 1], vectors[i * 3 + 2] };
		const double moved[3] = { original[0] * stretch[0], original[1] * stretch[1], original[2] * stretch[2] };
		const double length = std::sqrt(original[0] * original[0] + original[1] * original[1] + original[2] * original[2]);
		const double movedLength = std::sqrt(moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]);
		if (!(movedLength > 1.0e-30) || !std::isfinite(movedLength) || !(length > 0.0))
			continue;
		const double scale = length / movedLength;
		for (std::size_t axis = 0; axis < 3; ++axis)
			vectors[i * 3 + axis] = static_cast<float>(moved[axis] * scale);
	}
}

bool plot3DResampleVoxels(const std::vector<float>& values, const int dim[3], const double origin[3], const std::array<Plot3DAxisConfig, 3>& axes,
	Plot3DResampledVoxels& out)
{
	out = Plot3DResampledVoxels();
	if (dim[0] <= 0 || dim[1] <= 0 || dim[2] <= 0 || values.size() != static_cast<std::size_t>(dim[0]) * static_cast<std::size_t>(dim[1]) * static_cast<std::size_t>(dim[2]))
		return false;
	std::vector<int> source[3]; // per axis: the data cell under each output cell's centre (-1 = none)
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		bool lowOk = false, highOk = false;
		const double low = plot3DTransformAxisValue(origin[axis], axes[axis], &lowOk);
		const double high = plot3DTransformAxisValue(origin[axis] + dim[axis], axes[axis], &highOk);
		if (!lowOk || !highOk || !(high > low))
			return false;
		const bool scaled = axes[axis].scale != Plot3DAxisScale::Linear;
		const int count = scaled ? std::clamp(dim[axis] * 2, dim[axis], std::max(dim[axis], 256)) : dim[axis];
		out.dim[axis] = count;
		out.minimum[axis] = low;
		out.size[axis] = (high - low) / count;
		source[axis].assign(static_cast<std::size_t>(count), -1);
		for (int cell = 0; cell < count; ++cell)
		{
			const double centre = low + (cell + 0.5) * out.size[axis];
			const double data = plot3DInverseAxisValue(centre, axes[axis]);
			const double index = std::floor(data - origin[axis]);
			if (std::isfinite(index) && index >= 0.0 && index < dim[axis])
				source[axis][static_cast<std::size_t>(cell)] = static_cast<int>(index);
		}
	}
	out.values.assign(static_cast<std::size_t>(out.dim[0]) * static_cast<std::size_t>(out.dim[1]) * static_cast<std::size_t>(out.dim[2]), std::numeric_limits<float>::quiet_NaN());
	std::size_t at = 0;
	for (int k = 0; k < out.dim[2]; ++k)
		for (int j = 0; j < out.dim[1]; ++j)
			for (int i = 0; i < out.dim[0]; ++i, ++at)
			{
				const int si = source[0][static_cast<std::size_t>(i)], sj = source[1][static_cast<std::size_t>(j)], sk = source[2][static_cast<std::size_t>(k)];
				if (si >= 0 && sj >= 0 && sk >= 0)
					out.values[at] = values[(static_cast<std::size_t>(sk) * static_cast<std::size_t>(dim[1]) + static_cast<std::size_t>(sj)) * static_cast<std::size_t>(dim[0]) + static_cast<std::size_t>(si)];
			}
	return true;
}

bool plot3DSameAxisScale(const Plot3DAxisConfig& a, const Plot3DAxisConfig& b)
{
	return a.scale == b.scale && (a.scale != Plot3DAxisScale::SymLog || a.symlogLinearThreshold == b.symlogLinearThreshold);
}

double plot3DAxisScaleSlope(double value, const Plot3DAxisConfig& config)
{
	if (config.scale == Plot3DAxisScale::Linear)
		return 1.0;
	const double h = std::max(std::abs(value) * 1.0e-4, 1.0e-9);
	bool lowOk = false, highOk = false;
	const double low = plot3DTransformAxisValue(value - h, config, &lowOk);
	const double high = plot3DTransformAxisValue(value + h, config, &highOk);
	if (!lowOk || !highOk || !(high > low))
		return 1.0;
	return (high - low) / (2.0 * h);
}

std::vector<Plot3DAxisTick> plot3DGenerateAxisTicks(double minimum, double maximum, const Plot3DAxisConfig& config)
{
	std::vector<Plot3DAxisTick> ticks;
	bool minOk = false, maxOk = false;
	const double transformedMin = plot3DTransformAxisValue(minimum, config, &minOk);
	const double transformedMax = plot3DTransformAxisValue(maximum, config, &maxOk);
	if (!minOk || !maxOk || !(transformedMax > transformedMin)) return ticks;
	const double step = config.scale == Plot3DAxisScale::Log10 ? 1.0
		: niceStep(transformedMax - transformedMin, config.targetTicks);
	const double first = std::ceil(transformedMin / step - 1.0e-10) * step;
	for (double transformed = first; transformed <= transformedMax + step * 1.0e-8 && ticks.size() < 1000; transformed += step)
	{
		const double value = plot3DInverseAxisValue(transformed, config);
		ticks.push_back({ value, transformed, formatTick(value) });
	}
	return ticks;
}

void Plot3DAxisController::setReferencePlanesVisible(bool xy, bool xz, bool yz)
{
	_referencePlanes = { xy, xz, yz };
}

void Plot3DAxisController::setReferencePlaneOpacity(float opacity)
{
	_referencePlaneOpacity = std::clamp(opacity, 0.0f, 0.35f);
}

bool Plot3DAxisController::buildLayout(const std::array<Plot3DAxisConfig, 3>& axes, const double dataMinimum[3],
	const double dataMaximum[3], Plot3DAxisLayout& layout, QString* error, const QString& title) const
{
	layout = Plot3DAxisLayout();
	if (error) error->clear();
	// The largest extent among the axes that have one: a flat axis is padded relative to it, so a plane of small data (a 0.1 m wide plot) does
	// not get a box a unit tall.
	double largestExtent = 0.0;
	for (int axis = 0; axis < 3; ++axis)
	{
		const double lo = axes[axis].automaticRange ? dataMinimum[axis] : axes[axis].minimum;
		const double hi = axes[axis].automaticRange ? dataMaximum[axis] : axes[axis].maximum;
		if (std::isfinite(lo) && std::isfinite(hi) && hi > lo && axes[axis].scale == Plot3DAxisScale::Linear)
			largestExtent = std::max(largestExtent, hi - lo);
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		double lo = axes[axis].automaticRange ? dataMinimum[axis] : axes[axis].minimum;
		double hi = axes[axis].automaticRange ? dataMaximum[axis] : axes[axis].maximum;
		// Flat datasets are common (a surface at constant Z or a line along one axis). Give an automatic range visible
		// depth while preserving strict validation for a user-entered manual range.
		if (axes[axis].automaticRange && lo == hi && std::isfinite(lo))
		{
			if (axes[axis].scale == Plot3DAxisScale::Log10 && lo > 0.0)
			{
				lo /= 10.0;
				hi *= 10.0;
			}
			else
			{
				// A flat axis gets depth in proportion to the others (10 % of the largest extent); only when every axis is flat is a
				// fixed amount used.
				const double padding = largestExtent > 0.0 ? largestExtent * 0.1 : std::max(1.0, std::abs(lo) * 0.05);
				lo -= padding;
				hi += padding;
			}
		}
		bool loOk = false, hiOk = false;
		layout.minimum[axis] = plot3DTransformAxisValue(lo, axes[axis], &loOk);
		layout.maximum[axis] = plot3DTransformAxisValue(hi, axes[axis], &hiOk);
		if (!loOk || !hiOk || !(layout.maximum[axis] > layout.minimum[axis]))
		{
			if (error) *error = QStringLiteral("Axis %1 has an invalid or empty range for its selected scale.").arg(axis + 1);
			return false;
		}
		layout.ticks[axis] = plot3DGenerateAxisTicks(lo, hi, axes[axis]);
	}

	const double x0=layout.minimum[0], x1=layout.maximum[0], y0=layout.minimum[1], y1=layout.maximum[1], z0=layout.minimum[2], z1=layout.maximum[2];
	// Complete bounding box.  The three edges at the minimum corner retain
	// the conventional X=red, Y=green and Z=blue colouring; parallel edges
	// blend the colours of the non-minimum coordinates they share.
	layout.axisLines = {
		{ point(x0,y0,z0), point(x1,y0,z0), xAxisColor },
		{ point(x0,y1,z0), point(x1,y1,z0), xyAxisColor },
		{ point(x0,y0,z1), point(x1,y0,z1), xzAxisColor },
		{ point(x0,y1,z1), point(x1,y1,z1), xyzAxisColor },
		{ point(x0,y0,z0), point(x0,y1,z0), yAxisColor },
		{ point(x1,y0,z0), point(x1,y1,z0), xyAxisColor },
		{ point(x0,y0,z1), point(x0,y1,z1), yzAxisColor },
		{ point(x1,y0,z1), point(x1,y1,z1), xyzAxisColor },
		{ point(x0,y0,z0), point(x0,y0,z1), zAxisColor },
		{ point(x1,y0,z0), point(x1,y0,z1), xzAxisColor },
		{ point(x0,y1,z0), point(x0,y1,z1), yzAxisColor },
		{ point(x1,y1,z0), point(x1,y1,z1), xyzAxisColor },
	};
	const double tickSize = 0.015 * std::max({x1-x0, y1-y0, z1-z0});
	for (const auto& tick : layout.ticks[0]) layout.tickLines.push_back({point(tick.transformedValue,y0,z0),point(tick.transformedValue,y0-tickSize,z0), xAxisColor});
	for (const auto& tick : layout.ticks[1]) layout.tickLines.push_back({point(x0,tick.transformedValue,z0),point(x0-tickSize,tick.transformedValue,z0), yAxisColor});
	for (const auto& tick : layout.ticks[2]) layout.tickLines.push_back({point(x0,y0,tick.transformedValue),point(x0-tickSize,y0,tick.transformedValue), zAxisColor});
	// A restrained three-plane grid gives depth and scale without competing
	// with the data.  Every guide comes from an existing major tick, so its
	// spacing stays consistent with linear, log and symmetric-log axes.
	const QVector3D gridColor(0.49f, 0.53f, 0.57f);
	// XY floor: lines parallel to Y for X ticks, and parallel to X for Y ticks.
	for (const auto& tick : layout.ticks[0]) layout.gridLines.push_back({ point(tick.transformedValue, y0, z0), point(tick.transformedValue, y1, z0), gridColor });
	for (const auto& tick : layout.ticks[1]) layout.gridLines.push_back({ point(x0, tick.transformedValue, z0), point(x1, tick.transformedValue, z0), gridColor });
	// XZ back wall and YZ side wall complete the depth cues.
	for (const auto& tick : layout.ticks[0]) layout.gridLines.push_back({ point(tick.transformedValue, y0, z0), point(tick.transformedValue, y0, z1), gridColor });
	for (const auto& tick : layout.ticks[2]) layout.gridLines.push_back({ point(x0, y0, tick.transformedValue), point(x1, y0, tick.transformedValue), gridColor });
	for (const auto& tick : layout.ticks[1]) layout.gridLines.push_back({ point(x0, tick.transformedValue, z0), point(x0, tick.transformedValue, z1), gridColor });
	for (const auto& tick : layout.ticks[2]) layout.gridLines.push_back({ point(x0, y0, tick.transformedValue), point(x0, y1, tick.transformedValue), gridColor });
	for (int axis=0; axis<3; ++axis)
		for (const auto& tick : layout.ticks[axis])
		{
			QVector3D p = point(x0,y0,z0);
			if(axis==0) p.setX(float(tick.transformedValue)); else if(axis==1) p.setY(float(tick.transformedValue)); else p.setZ(float(tick.transformedValue));
			layout.labels.push_back({tick.label, p});
		}
	// Axis titles follow their projected axis line in the renderer. This gives
	// descriptive labels the familiar chart treatment instead of placing a
	// horizontal word at an endpoint.
	layout.axisTitles = {
		{ axes[0].label, point(x0,y0,z0), point(x1,y0,z0), xAxisColor },
		{ axes[1].label, point(x0,y0,z0), point(x0,y1,z0), yAxisColor },
		{ axes[2].label, point(x0,y0,z0), point(x0,y0,z1), zAxisColor },
	};
	layout.title = title.trimmed();
	layout.referencePlaneOpacity = _referencePlaneOpacity;
	if (_referencePlanes[0]) layout.referencePlanes.push_back({{point(x0,y0,z0),point(x1,y0,z0),point(x1,y1,z0),point(x0,y1,z0)}, xyAxisColor});
	if (_referencePlanes[1]) layout.referencePlanes.push_back({{point(x0,y0,z0),point(x1,y0,z0),point(x1,y0,z1),point(x0,y0,z1)}, xzAxisColor});
	if (_referencePlanes[2]) layout.referencePlanes.push_back({{point(x0,y0,z0),point(x0,y1,z0),point(x0,y1,z1),point(x0,y0,z1)}, yzAxisColor});
	return true;
}
