#pragma once

// Cutting the VOLUME of a simulation result: a plane section coloured by a field ("data-aware section"), and iso-surfaces of a field. One engine
// serves both - it cuts every volume cell where a per-node signed function changes sign: for a plane the distance to the plane, for an
// iso-surface the field minus the level. GUI-free (QtCore + standard library), unit-tested.
//
// How a cell is cut: the cell's faces are walked; on each face the run(s) of nodes on the negative side are isolated by one segment between the two
// edges they leave through. The segments of a cell chain into closed loops (each edge is shared by two faces), every loop is a polygon of edge
// crossings, split into triangles as a fan. A crossing sits on a mesh edge, at the linear interpolation of the function along it, computed from the
// edge's nodes in a fixed order - so neighbouring cells produce the very same vertex and the cut is crack-free. A node exactly on the cut counts as
// positive. The rule for an ambiguous face (two opposite corners on each side) is the same for both cells that share it, so the surface stays closed.
//
// Tetrahedra, pyramids, wedges, hexahedra (their quadratic forms through the corner nodes) and polyhedra (through their explicit faces) are cut;
// surface and line cells are not.

#include "ResultDataset.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

struct SliceMesh
{
	std::vector<float> positions;            // xyz per vertex (the dataset's own coordinates)
	std::vector<float> values;               // one per vertex: the node values interpolated onto the cut (NaN when none were given)
	std::vector<std::uint32_t> triangles;    // 3 vertex indices per triangle, wound so the normal points to the positive side of the function
	std::vector<std::uint32_t> triangleCell; // the dataset cell each triangle was cut from
	// Where every vertex sits: on the mesh edge between dataset nodes edgeNodes[2 v] and edgeNodes[2 v + 1], a fraction edgeT[v] of the way. With
	// this the cut can be recoloured for another field (or another time step) without cutting again: see interpolateSliceValues().
	std::vector<std::uint32_t> edgeNodes;
	std::vector<float> edgeT;

	std::size_t vertexCount() const { return positions.size() / 3; }
	std::size_t triangleCount() const { return triangles.size() / 3; }
};

// Cuts every volume cell of `dataset` where `distance` (one value per dataset node) changes sign. `nodeValues` (one per node, may be null) are
// interpolated to the cut vertices. Vertices are shared between triangles (welded). False when `distance` does not have one value per node,
// or on cancellation (`cancel`, polled every few thousand cells).
bool cutVolume(const ResultDataset& dataset, const std::vector<float>& distance, const std::vector<float>* nodeValues, SliceMesh& out,
               const std::atomic<bool>* cancel = nullptr);

// Sets `values` of every vertex from a per-node field (one value per dataset node): the value of the edge's nodes interpolated at the vertex.
void interpolateSliceValues(SliceMesh& mesh, const std::vector<float>& nodeValues);

// Keeps the part of a triangle set on the side of the plane the `normal` points to (distance >= 0), cutting the triangles that straddle it.
// `attributes` holds `stride` floats per vertex (a value, or a colour ...) that are interpolated at the new vertices. Works on any indexed
// triangle set; the result is unwelded (each triangle has its own vertices) and `triangleCell`, when not empty, is carried along.
void clipTrianglesToHalfSpace(std::vector<float>& positions, std::vector<float>& attributes, int stride, std::vector<std::uint32_t>& triangles,
                              std::vector<std::uint32_t>& triangleCell, const double point[3], const double normal[3]);

// The signed distance of every node from the plane through `point` with the (unit or not) `normal`: positive on the side the normal points to.
std::vector<float> planeDistances(const ResultDataset& dataset, const double point[3], const double normal[3]);

// Gives every triangle its own three vertices (positions and values copied), for a colouring that is constant per triangle (a cell field).
void unweldSlice(SliceMesh& mesh);
