#pragma once

// Snapshot of a displayed simulation result for MVF persistence - see docs/simulation_mvf_persistence_design.md.
// GUI-free (QtCore only), unit-tested in result_tests.
//
// A snapshot is the result as it is DISPLAYED: per boundary-surface vertex, not per solver node. Encoding turns a
// dataset + its boundary surface + the view state into a JSON description plus a list of binary blobs; the MVF
// writer stores the blobs in the GEOM chunk and the JSON in mvfSession, the reader hands both back to
// decodeResultSnapshot(), which rebuilds an ordinary ResultDataset (nodes = surface vertices, one triangle cell per
// surface triangle) that the rest of the application works with unchanged.
//
// Blobs hold little-endian float32 (or int64 for node ids). Large ones are compressed losslessly: the bytes are
// shuffled into planes (all first bytes, then all second bytes, ...) which makes smooth field data far more
// compressible, then deflated with zlib (qCompress). A blob that would not shrink is stored raw.

#include "ResultBoundary.h"
#include "ResultDataset.h"
#include "SimulationOverlays.h"
#include "SimulationResultDisplay.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <vector>

struct SnapshotOptions
{
	enum class Content
	{
		ShownAndDisplacement, // the field on display (its source tensor for a derived one) and the displacement field
		AllFields             // every source field (derived ones are recomputed on load, never stored)
	};
	Content content = Content::ShownAndDisplacement;
	int shownField = -1;  // index into dataset.fields; used by ShownAndDisplacement
	int maxSteps = 100;   // more steps than this are subsampled evenly (first and last always kept)
	bool compress = true;
	// Also store the volume (nodes, cells, the chosen fields at every node and cell, and how the surface maps onto them), so a restored result can be cut
	// and traced again: sections, iso-surfaces and streamlines stay live. Without it only the surface is stored and those show frozen (see SnapshotOverlays).
	bool includeVolume = false;
	// Fields to store besides the shown one and the displacement (ShownAndDisplacement only): the ones the arrows, streamlines and iso-surfaces on display
	// use, which a restored result needs to show them again. Indices into dataset.fields; out-of-range ones are ignored.
	std::vector<int> extraFields;
};

struct SnapshotSize
{
	std::uint64_t rawBytes = 0;    // the blobs uncompressed (exact)
	std::uint64_t storedBytes = 0; // after compression (an estimate from a sample, unless it came from an encode)
	bool estimated = true;
};

// Size a snapshot with these options would have, without encoding it all: rawBytes is exact, storedBytes is
// estimated by compressing a sample of the largest blobs.
SnapshotSize estimateSnapshotSize(const ResultDataset& dataset, const ResultBoundarySurface& surface, const SnapshotOptions& options);

struct ResultSnapshot
{
	QJsonObject json;                // references blobs by index ("blobs" array, parallel to `blobs`)
	std::vector<QByteArray> blobs;
	SnapshotSize size;               // exact after encoding
	QStringList notes;               // things the user should hear about (e.g. "20 of 300 steps stored")
};

// The step indices kept when a result with `stepCount` steps is limited to `maxSteps`: all of them when it fits,
// otherwise evenly spaced including the first and the last.
std::vector<int> snapshotStepIndices(int stepCount, int maxSteps);

// Encodes what is displayed. `overlays` (may be null) are the cut faces, iso-surfaces and streamlines on display: they are stored as they are, so a result
// restored without its volume still shows them. False (with `error`) when the dataset is unusable.
bool encodeResultSnapshot(const ResultDataset& dataset, const ResultBoundarySurface& surface, const SimulationViewState& state,
                          const SnapshotOptions& options, ResultSnapshot& out, QString* error = nullptr, const SnapshotOverlays* overlays = nullptr);

struct DecodedSnapshot
{
	std::shared_ptr<ResultDataset> dataset; // nodes = surface vertices, cells = the surface triangles
	std::vector<float> restPositions;       // the undeformed vertex positions
	SimulationViewState state;              // the saved view (field indices re-resolved by name; -1 when gone)
	QString sourcePath;                     // where the full result came from (informational)
	QStringList warnings;
	// The frozen cut faces, iso-surfaces and streamlines that were on display when it was saved (empty when there were none).
	SnapshotOverlays overlays;
	// Set when the volume was stored too: `dataset` is then the full result (its own nodes and cells) and these say how the mesh's surface maps onto it,
	// the way a ResultBoundarySurface does (one node per vertex, one cell per triangle, the face marker per triangle).
	bool hasVolume = false;
	std::vector<std::uint32_t> vertexNode, triangleCell;
	std::vector<std::uint8_t> triangleFace;
};

// Rebuilds the result. `vertexCount` and `triangles` describe the mesh as it is now: a different vertex or
// triangle count than at save time means the mesh was edited and the per-vertex data no longer matches, which is
// an error rather than a silent misalignment.
bool decodeResultSnapshot(const QJsonObject& json, const std::vector<QByteArray>& blobs, std::size_t vertexCount,
                          const std::vector<std::uint32_t>& triangles, DecodedSnapshot& out, QString* error = nullptr);

// The lossless blob codec, exposed for tests. `elementSize` is 4 (float32) or 8 (int64).
QByteArray shuffleBytes(const QByteArray& raw, int elementSize);
QByteArray unshuffleBytes(const QByteArray& shuffled, int elementSize);
