#pragma once

// Turns generated plot data (Plot3DGenerated, see Plot3DGenerate.h) into what the viewer shows: a temporary preview, a committed
// plot (scene mesh + node + document session), or an in-place rebuild of an existing plot. The Add 3D Plot dialog collects the
// inputs and calls these; none of it reads a widget. This is the one place that creates plot meshes, so a new primitive or source
// is added here and in the generator, not in every dialog code path.

#include "MeshVertex.h"
#include "Plot3DGenerate.h"
#include "Plot3DSession.h"

#include <QString>
#include <QUuid>

#include <array>
#include <vector>

class ModelViewer;

// A plot mesh's vertices in the form the scene mesh and the session need: the vertex array, the per-vertex colour values (NaN
// becomes invalid), the value range and the position bounds.
struct Plot3DMeshUpload
{
	std::vector<Vertex> vertices;
	std::vector<float> values;
	std::vector<bool> valid;
	float valueMinimum = 0.0f, valueMaximum = 1.0f; // widened to a non-degenerate range
	bool anyValid = false;
	std::array<double, 3> boundsMinimum{}, boundsMaximum{};
};
// dataNormals = false gives every vertex an up (+Z) normal (point and line meshes carry placeholders).
Plot3DMeshUpload plot3DPrepareUpload(const Plot3DMeshData& data, bool dataNormals = true);

// A CSV-backed plot's table, stored with the plot so Edit Plot can reopen it.
struct Plot3DCsvBinding
{
	QString text;
	Plot3DCsvOptions options;
	Plot3DColumnMapping mapping;
};

struct Plot3DCommitOptions
{
	QString baseName;                  // the unique scene mesh / tree name
	QString title;                     // the plot's axes-box heading (formula title); empty = the name
	Plot3DMeshOptions mesh;            // scatter variant, base plane, contour levels (the dataset -> mesh settings)
	const Plot3DCsvBinding* csv = nullptr; // set for a CSV-backed plot (table sources, including the time series)
	Plot3DGeneratedSpec generated;     // set (valid) for a plot from a generated source
	int contourOverlayMode = 0;        // a Surface can be committed with contour lines (0 none, 1 on the surface, 2 on the base plane)
};

// Shows the temporary preview of the plot (no scene node, session or undo record). `title` heads its axes box.
bool plot3DShowPreview(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DMeshOptions& options, const QString& title,
	QString* error);

// Adds the plot to the document and returns its mesh UUID (null on failure, with *error). *status gets the "Built ..." line.
QUuid plot3DCommit(ModelViewer* viewer, const Plot3DGenerated& generated, const Plot3DCommitOptions& options, QString* status,
	QString* error);

// Replaces an existing plot's geometry in place with freshly generated data, keeping its mesh, scene node, visibility, axes and
// presentation settings. The generated primitive must match the plot's. `csv` replaces a CSV-backed plot's stored table.
bool plot3DRebuild(ModelViewer* viewer, const QUuid& meshUuid, const Plot3DGenerated& generated, const Plot3DCsvBinding* csv,
	QString* error);
