#pragma once

#include <vector>
#include <QColor>
#include <QPixmap>
#include <QString>

// Which color scheme to use when mapping a scalar analysis value to a
// display color - shared by every Surface Analysis mode (curvature/
// thickness/deviation). Kept as its own small, dependency-free header (no
// GL/CGAL dependency) so it's trivially reusable from any analyzer or the
// dialog's own legend widget.
enum class AnalysisColormap
{
	// Blue -> Cyan -> Green -> Yellow -> Red - for MAGNITUDE-only values
	// with no meaningful "zero crossing" (deviation magnitude, wall
	// thickness).
	Sequential,
	// Blue -> White -> Red, centered on the midpoint of [rangeMin, rangeMax] -
	// for SIGNED values where zero is meaningful (draft angle, curvature sign).
	Diverging,
	// Two flat colours split at the midpoint of [rangeMin, rangeMax]: red below, green at or above. A pass/fail
	// map for "is anything thinner than X" questions - the caller sets the range to [0, 2X] so the split lands
	// on X. Its legend is thresholdLegend(), not legendGradient().
	Threshold
};

class AnalysisColorRamp
{
public:
	// One RGBA quadruplet per input sample, ready for
	// RenderableMesh::setAnalysisOverlayColors(). validPerSample.size() must
	// equal scalarPerSample.size() (a shorter/empty validPerSample is
	// treated as "every sample valid", for callers that have no invalid
	// samples to represent). A false entry renders as a fixed, unmistakable
	// "invalid/no data" color (see invalidSampleColor()) - never silently
	// folded into the real data's min/max range, which would visually imply
	// it's a real, if extreme, measured value.
	static std::vector<float> mapToRGBA(
		const std::vector<float>& scalarPerSample,
		const std::vector<bool>& validPerSample,
		float rangeMin, float rangeMax,
		AnalysisColormap colormap);

	// A small horizontal gradient strip for the analysis dialog's legend,
	// with min/max value labels (plus an optional unit suffix, e.g. " mm")
	// drawn beneath each end.
	static QPixmap legendGradient(
		int width, int height,
		float rangeMin, float rangeMax,
		AnalysisColormap colormap,
		const QString& unitSuffix = QString(),
		// True when values above rangeMax are clamped to the top colour (a robust range that deliberately
		// excludes outliers): the max label then reads ">= max" instead of implying max is the largest value.
		bool openEndedMax = false);

	// Legend for AnalysisColormap::Threshold: two labelled blocks (below / at-or-above the threshold).
	static QPixmap thresholdLegend(int width, int height, const QString& belowText, const QString& aboveText);

	// The color a single normalized value (0 = rangeMin, 1 = rangeMax) maps
	// to - exposed publicly since legendGradient() and mapToRGBA() both need
	// it, and a future per-sample probe/tooltip (see the plan's future-
	// roadmap "value probing" item) will too.
	static QColor colorForNormalized(float t, AnalysisColormap colormap);

	// Fixed, unmistakable color for an invalid/no-data sample - never
	// produced by colorForNormalized() for any real t (both colormaps are
	// full-saturation or pure-white-crossing, so this mid-gray is never a
	// coincidental real result), so it can't be confused with genuine data.
	static QColor invalidSampleColor();
};
