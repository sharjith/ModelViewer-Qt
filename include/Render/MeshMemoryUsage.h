#pragma once

#include <QtGlobal>

// CPU-side bytes held by meshes, by kind (capacity, not size: that is what the heap is charged). Filled by
// SceneMesh::addMemoryUsage() for the "[MemoryAudit]" log line, to see where a large model's memory goes.
struct MeshMemoryUsage
{
	qint64 meshes = 0;
	qint64 vertices = 0;
	qint64 triangles = 0;

	qint64 vertexStructs = 0;   // std::vector<Vertex>, 128 bytes per vertex
	qint64 points = 0;
	qint64 normals = 0;
	qint64 colors = 0;
	qint64 texCoords = 0;
	qint64 tangents = 0;        // tangents + bitangents
	qint64 joints = 0;          // joint indices + weights
	qint64 indices = 0;
	qint64 worldPoints = 0;     // MeshInstanceState's transformed-points cache
	qint64 lodIndices = 0;
	qint64 edges = 0;           // feature-edge indices and OCC edge/face data
	qint64 caches = 0;          // lazy adjacency/loop/face-lookup caches
	qint64 morph = 0;

	qint64 total() const
	{
		return vertexStructs + points + normals + colors + texCoords + tangents + joints + indices + worldPoints
			+ lodIndices + edges + caches + morph;
	}
};
