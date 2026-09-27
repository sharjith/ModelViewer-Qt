#pragma once

// Boundary-surface extraction for simulation results - see docs/simulation_results_design.md (section 5).
// GUI-free: QtCore + standard library only, safe on a worker thread.

#include "ResultDataset.h"

#include <QString>

#include <atomic>
#include <cstdint>
#include <vector>

// The displayable outer skin of a ResultDataset, with the back-references the design requires for later
// phases (probe -> node id, interior sections, attach-to-CAD): every boundary vertex knows its dataset
// node, every triangle its source cell and local face.
struct ResultBoundarySurface
{
	std::vector<float> positions;              // xyz per boundary vertex (undeformed)
	std::vector<std::uint32_t> vertexNode;     // dataset node index per boundary vertex
	std::vector<std::uint32_t> triangles;      // 3 vertex indices per triangle, outward-facing for volume cells
	std::vector<std::uint32_t> triangleCell;   // dataset cell index per triangle
	std::vector<std::uint8_t> triangleFace;    // local face within the cell; kNoFace for surface (shell) cells

	// Cells that produced no geometry: Unsupported types, zero-length lines. (Volume cells fully enclosed by
	// neighbours are NOT counted - they legitimately have no boundary face.)
	std::size_t skippedCells = 0;

	// The tubes drawn around line cells (see below): the vertices that belong to them, the unit direction of each from its node's axis, and the model's diagonal
	// the radius is a fraction of. applyLineRadius() re-places the vertices when the radius changes.
	std::vector<std::uint32_t> tubeVertices;
	std::vector<float> tubeDirections; // 3 per entry of tubeVertices
	double tubeDiagonal = 0.0;

	static constexpr std::uint8_t kNoFace = 0xFF;

	std::size_t vertexCount() const { return positions.size() / 3; }
	std::size_t triangleCount() const { return triangles.size() / 3; }
};

// Volume cells: a face referenced by exactly one cell is a boundary face. Polyhedron cells use their explicit face lists (ResultDataset::faceNodes
// ...), faces of any size matched with the faces of the other cells by their node sets and triangulated (ear clipping) when they have more than 4 nodes. Surface cells (triangles,
// quads - e.g. thin-walled structural models) are always emitted as they are. Line cells (beams, pipes, trusses) are drawn as thin six-sided tubes
// of a radius of one percent of the model's diagonal, open at the ends: each end's ring belongs to that end's node (node fields interpolate along the tube),
// and the tube's triangles belong to the line cell (cell fields are flat along it). Quads are split into two
// triangles. Volume-face winding is normalised to point away from the owning cell's centroid.
//
// Memory: faces are hashed in partitions so peak temporary memory stays bounded on very large meshes.
//
// The radius of the tubes around line cells, as a fraction of the model's diagonal, that extractBoundarySurface() starts with.
constexpr double kDefaultLineRadius = 0.01;

// Sets the radius of the line tubes of `surface` to `fraction` of the model's diagonal, moving their vertices (positions only: the triangles stay). Nothing to
// do when the surface has no tubes.
void applyLineRadius(const ResultDataset& dataset, ResultBoundarySurface& surface, double fraction);

// Returns false (with `error` set, if given) on cancellation or an invalid dataset. Node data only in
// this phase: vertices are shared, so cell-data display (which needs unshared vertices) is a later phase.
// `facesPerPartition` is the target number of faces hashed per pass (default ~2M); tests lower it to force
// the multi-partition path on small meshes.
bool extractBoundarySurface(const ResultDataset& dataset, ResultBoundarySurface& out,
                            const std::atomic<bool>* cancel = nullptr, QString* error = nullptr,
                            std::size_t facesPerPartition = 2000000);
