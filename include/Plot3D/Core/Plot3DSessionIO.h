#pragma once

// GUI-free (QtCore only) serialisation of a Plot3DSession for the .mvf session file, so a saved document comes
// back with its 3D Plot tab, axes box and colour/appearance controls instead of only the bare plot meshes.
//
// The JSON object holds every scalar control; each large array (colour values, CSV source, bar/contour source
// data, Quiver arrows, Voxel grid) is returned as a compressed blob and referenced from the JSON by name. The
// caller stores the blobs in the file's binary GEOM chunk the same way Simulation snapshots do, and hands them
// back, in the same order, when reading. Keeping this free of the viewport and of any Simulation type is what lets
// plot3d_tests round-trip it without a GL context.

#include "Plot3DSession.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <vector>

// What the Quiver and Voxel renderers hold OUTSIDE the plot's mesh (SimulationGlyphController /
// SimulationVolumeController). Plain arrays rather than GlyphSet/VolumeGrid so this header needs no Simulation
// include; ModelViewer converts at the boundary. A plot of any other primitive leaves both flags false.
struct Plot3DRendererPayload
{
	// Quiver: the arrows exactly as drawn (already scaled), one 3-float vector and one magnitude per arrow.
	bool hasGlyphs = false;
	std::vector<float> glyphVectors;
	std::vector<float> glyphValues;
	float glyphReferenceLength = 0.0f;
	float glyphFieldMinimum = 0.0f;
	float glyphFieldMaximum = 1.0f;

	// Voxel: the dense occupancy grid.
	bool hasVolume = false;
	std::vector<float> volumeValues;
	int volumeDimensions[3] = { 0, 0, 0 };
	float volumeOrigin[3] = { 0.0f, 0.0f, 0.0f };
	float volumeVoxelSize[3] = { 1.0f, 1.0f, 1.0f };
	float volumeFieldMinimum = 0.0f;
	float volumeFieldMaximum = 1.0f;
	QString volumeLabel;
};

// Appends this plot's blobs to `blobs` and returns its JSON. The blob order is the read order.
QJsonObject plot3DSessionToJson(const Plot3DSession& session, const Plot3DRendererPayload& payload,
	std::vector<QByteArray>& blobs);

// `blobs` is the list plot3DSessionToJson() appended for THIS plot, in order. False (with `error`) when a referenced
// blob is missing or corrupt; the plot should then be skipped rather than half-restored.
bool plot3DSessionFromJson(const QJsonObject& json, const std::vector<QByteArray>& blobs, Plot3DSession& session,
	Plot3DRendererPayload& payload, QString* error = nullptr);
