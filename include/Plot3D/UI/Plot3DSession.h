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
	QString name;
	Plot3DPrimitive primitive = Plot3DPrimitive::Surface;
	std::array<Plot3DAxisConfig, 3> axes;
	std::array<double, 3> dataMinimum{};
	std::array<double, 3> dataMaximum{};
	std::vector<float> values;
	std::vector<bool> valid;
	float dataMinimumValue = 0.0f;
	float dataMaximumValue = 1.0f;
	float colourMinimum = 0.0f;
	float colourMaximum = 1.0f;
	int colormap = 0;
	int bands = 0;
	bool visible = true;
	bool axesVisible = true;
};
