#pragma once

// GUI-free, GL-free mesh builders that turn a Plot3DDataset's content into plain vertex/index arrays a UI-layer
// caller (Plot3DPanel) turns into an actual SceneMesh - kept separate from Plot3DData.h itself so the data model
// stays free of any notion of "mesh", the same separation SimulationCharts.h/SimulationVolume.h keep from
// ResultDataset. "Wireframe" (see docs/plot3d_blueprint.md section 2) is deliberately NOT a separate builder here -
// it is the existing per-mesh wireframe DISPLAY MODE applied to the very same mesh Surface produces.

#include "Plot3DData.h"

#include <QString>

#include <vector>

struct Plot3DMeshData
{
	std::vector<float> positions;      // x,y,z per vertex
	std::vector<float> normals;        // x,y,z per vertex, parallel to positions (see buildPlot3DLineMesh()/
	                                    // buildPlot3DScatterMesh()'s doc comments: unused placeholder for those two)
	std::vector<double> values;        // one scalar per vertex, parallel to positions - for colour-by-value
	std::vector<unsigned int> indices; // triangle list for Surface/Bar; deliberately EMPTY for Line/Scatter, whose
	                                    // vertices are drawn in order via glDrawArrays with a GL_LINE_STRIP/
	                                    // GL_POINTS primitive mode instead - see buildPlot3DLineMesh()'s doc comment

	std::size_t vertexCount() const { return values.size(); }
	bool empty() const { return vertexCount() == 0; }
};

// A sparse CSV occupancy set expanded into the dense, axis-aligned grid consumed by the volume renderer.  Empty
// cells remain NaN so they are transparent; explicitly supplied zero-valued voxels are retained as valid samples.
// Coordinates are integer cell indices and each cell has a unit extent.
struct Plot3DVoxelGrid
{
	std::vector<float> values;
	int dimX = 0, dimY = 0, dimZ = 0;
	float origin[3] = { 0.0f, 0.0f, 0.0f };

	std::size_t voxelCount() const { return static_cast<std::size_t>(dimX) * dimY * dimZ; }
	bool empty() const { return dimX <= 0 || dimY <= 0 || dimZ <= 0 || values.size() != voxelCount(); }
};

// Builds a triangulated mesh from Surface data. Complete regular X/Y grids retain deterministic two-triangle cells;
// any other non-collinear X/Y sample set is triangulated through CGAL Delaunay. Grid rows/columns need not be in any
// particular order in the input - they are re-sorted internally by their distinct x/y coordinate. Duplicate X/Y
// positions and collinear inputs are rejected rather than guessed at.
bool buildPlot3DSurfaceMesh(const Plot3DSurfaceData& data, Plot3DMeshData& out, QString* error = nullptr);

// Builds surface-following iso-lines of Z from a complete regular Surface grid. Each level is emitted as
// independent GL_LINES vertices so the caller can use the same fixed-pixel-width path as Line plots.
bool buildPlot3DContourMesh(const Plot3DSurfaceData& data, Plot3DMeshData& out, int levelCount = 10, QString* error = nullptr);

// Builds a flat, ORDERED vertex list (no triangles) for Line data - the caller draws it as a GL_LINE_STRIP, the
// same native-primitive path glTF line-set import already uses (SceneMesh::draw()'s GL_POINTS/GL_LINE_STRIP
// branch), which renders at a fixed PIXEL line width via glLineWidth() rather than real 3D geometry. That is
// deliberate: unlike an earlier version of this builder (a solid tube mesh), a native line stays a constant size
// on screen regardless of camera zoom, matching matplotlib's own line/scatter markers, and needs no new rendering
// code. Row order in the input IS significant here (unlike Surface/Scatter) since it becomes the polyline's path.
bool buildPlot3DLineMesh(const Plot3DLineData& data, Plot3DMeshData& out, QString* error = nullptr);

// Builds a flat, unordered vertex list (no triangles) for Scatter data - the caller draws it as GL_POINTS, for the
// same constant-screen-size reason buildPlot3DLineMesh() above documents.
bool buildPlot3DScatterMesh(const Plot3DScatterData& data, Plot3DMeshData& out, QString* error = nullptr);

// Builds one independent GL_LINES segment per Scatter sample from its Z position to `baseZ`. Values are duplicated
// at both endpoints so each stem receives one uniform colour from the normal Plot3D overlay path.
bool buildPlot3DStemMesh(const Plot3DScatterData& data, double baseZ, Plot3DMeshData& out, QString* error = nullptr);
bool buildPlot3DErrorBarMesh(const Plot3DScatterData& data, Plot3DMeshData& out, QString* error = nullptr);

// Builds one closed, flat-shaded cuboid per bar. Vertices are intentionally duplicated per face so every face has
// the correct hard normal; the bar's scalar value is repeated for all 24 vertices so colour-by-value stays uniform.
// Positive and negative heights are both supported, extending from `base` in the appropriate Z direction.
bool buildPlot3DBarMesh(const Plot3DBarData& data, Plot3DMeshData& out, QString* error = nullptr);

// Builds the regular occupancy grid for a Voxel plot.  The imported indices may start at any non-negative value;
// their minimum becomes the grid origin. Duplicate cells and dimensions above 256 are rejected rather than
// allocating an accidental, impractically large sparse volume.
bool buildPlot3DVoxelGrid(const Plot3DVoxelData& data, Plot3DVoxelGrid& out, QString* error = nullptr);

// Builds a flat, unordered vertex list (no triangles, drawn as GL_POINTS) of Quiver's own arrow BASE positions -
// the "site" a Plot3D UI-layer caller anchors a GlyphSet's arrows to (see docs/plot3d_blueprint.md section 5:
// Quiver reuses SimulationGlyphController directly). Deliberately does not build the arrows themselves: GlyphSet
// and the glyph controller live in the Simulation module, which this Core file must not depend on (see this
// header's own top comment) - only Plot3DPanel (UI layer, which already depends on Simulation/Viewport headers)
// builds the actual GlyphSet, anchored to the SceneMesh this function's output becomes.
bool buildPlot3DQuiverSiteMesh(const Plot3DQuiverData& data, Plot3DMeshData& out, QString* error = nullptr);
