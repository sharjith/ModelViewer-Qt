#pragma once

#include "Plot3DData.h"

#include <QUuid>

#include <array>
#include <utility>
#include <vector>

// Document-owned state for one generated Plot3D mesh.  The imported SceneMesh remains
// ordinary scene content; this record retains only the non-geometry controls required
// to revisit its axes and colour presentation after the import dialog has closed.
// The definition a generated plot (Formula surface, Parametric surface / curve, Formula vector field, Implicit surface, Formula
// streamlines, Formula pathlines) was built from, so it can be edited and rebuilt in place like a CSV plot. sourceMode is the
// creation dialog's source (1 formula surface, 2 parametric surface, 3 parametric curve, 4 vector field, 5 implicit surface,
// 6 streamlines, 7 pathlines, 8 CSV time series); the expression and range fields are used the way that source's dialog uses them.
struct Plot3DGeneratedSpec
{
	bool valid = false;
	int sourceMode = 0;
	int presetIndex = -1;                              // which entry of that source's preset combo it was started from (-1 = unknown)
	QString title;
	QString expression;                                // z = ... (formula surface, implicit surface)
	QString xExpression, yExpression, zExpression;     // x/y/z(u,v), x/y/z(t) or u/v/w
	double xMinimum = 0.0, xMaximum = 1.0, yMinimum = 0.0, yMaximum = 1.0, zMinimum = 0.0, zMaximum = 1.0;
	int xSamples = 2, ySamples = 2, zSamples = 2;
	std::vector<std::pair<QString, double>> parameters; // in the order the dialog lists them
};

// A text note placed at a point in the plot's own data coordinates (not on a mesh surface, so it works for points, lines and
// quivers too). Drawn in the axes box through the same projected-text path as the tick labels.
struct Plot3DTextLabel
{
	QString text;
	double x = 0.0, y = 0.0, z = 0.0;
};

struct Plot3DSession
{
	QUuid meshUuid;
	// Set for a plot built from a formula / definition (not a CSV table): what Edit Plot reopens and rebuilds.
	Plot3DGeneratedSpec generated;
	// Stem plots own a companion GL_POINTS mesh for their constant-pixel endpoint markers. It shares the node and
	// visibility lifecycle of meshUuid but needs its own per-vertex colour data when the colour controls change.
	QUuid markerMeshUuid;
	QString name;
	// Screen-space heading above the shared Plot3D coordinate box.
	QString title;
	Plot3DPrimitive primitive = Plot3DPrimitive::Surface;
	std::array<Plot3DAxisConfig, 3> axes;
	std::array<double, 3> dataMinimum{};
	std::array<double, 3> dataMaximum{};
	std::vector<float> values;
	std::vector<bool> valid;
	std::vector<float> markerValues;
	std::vector<bool> markerValid;
	float dataMinimumValue = 0.0f;
	float dataMaximumValue = 1.0f;
	// Whether the colour range follows the data range. Stored, not derived from colourMinimum == dataMinimumValue: a manual
	// range that happens to equal the data range would otherwise read as automatic again and the box could never be cleared.
	bool automaticColourRange = true;
	float colourMinimum = 0.0f;
	float colourMaximum = 1.0f;
	int colormap = 0;
	int bands = 0;
	float lineWidth = 1.5f;
	float markerSize = 3.0f;
	float arrowScale = 1.0f;
	float barWidthScale = 1.0f;
	float barDepthScale = 1.0f;
	bool isStem = false;
	bool isErrorBars = false;
	// Filled scatter ribbons use authored vertex colours because the analysis-overlay shader path does not retain
	// its colour ramp when the underlying material is alpha blended. This flag lets the persistent colour controls
	// update those vertex colours while preserving the transparent material.
	bool isFilledScatter = false;
	double scatterBaseZ = 0.0;
	// CSV-backed plots retain their source and role mapping for the in-place Edit Plot workflow.
	bool editableCsv = false;
	QString csvSource;
	Plot3DCsvOptions csvOptions;
	Plot3DColumnMapping columnMapping;
	// Bar source geometry is retained so footprint styling can rebuild the mesh without reopening the import dialog.
	Plot3DBarData barSource;
	Plot3DSurfaceData contourSource;
	int contourLevels = 10;
	// Contour iso-lines flattened onto the plane at the surface's minimum Z (the axes box floor) instead of at their elevation.
	bool contourProjected = false;
	// A Surface-type plot (Surface, parametric, implicit) can carry a companion mesh of Z iso-lines: 0 = none, 1 = lying on
	// the surface, 2 = flattened onto the base plane. The lines are rebuilt from the plot's own mesh, so only these
	// settings are saved; the per-vertex colour values below are re-derived on load.
	// Hover section curves for this plot (a transient viewing aid: not saved with the file).
	bool sectionProbe = false;
	// A pathline plot's trails are being played back over time (transient, not saved with the file).
	bool pathlineAnimation = false;
	int contourOverlayMode = 0;
	int contourOverlayLevels = 10;
	QUuid contourOverlayMeshUuid;
	std::vector<float> overlayValues;
	std::vector<bool> overlayValid;
	std::vector<Plot3DTextLabel> textLabels;
	bool visible = true;
	bool axesVisible = true;
	std::array<bool, 3> referencePlanes{ true, false, false }; // XY, XZ, YZ
	float referencePlaneOpacity = 0.08f;
};
