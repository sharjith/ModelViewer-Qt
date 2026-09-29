#pragma once

#include "Plot3DData.h"

#include <QUuid>

#include <array>
#include <vector>

// Document-owned state for one generated Plot3D mesh.  The imported SceneMesh remains
// ordinary scene content; this record retains only the non-geometry controls required
// to revisit its axes and colour presentation after the import dialog has closed.
struct Plot3DSession
{
	QUuid meshUuid;
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
	// Filled scatter ribbons use authored vertex colours because the analysis-overlay shader path does not retain
	// its colour ramp when the underlying material is alpha blended. This flag lets the persistent colour controls
	// update those vertex colours while preserving the transparent material.
	bool isFilledScatter = false;
	// Bar source geometry is retained so footprint styling can rebuild the mesh without reopening the import dialog.
	Plot3DBarData barSource;
	Plot3DSurfaceData contourSource;
	int contourLevels = 10;
	bool visible = true;
	bool axesVisible = true;
	std::array<bool, 3> referencePlanes{ true, false, false }; // XY, XZ, YZ
	float referencePlaneOpacity = 0.08f;
};
