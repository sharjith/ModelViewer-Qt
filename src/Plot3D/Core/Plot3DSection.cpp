#include "Plot3DSection.h"

#include <cstdint>
#include <unordered_map>

namespace
{
	// The segment where the plane cuts triangle `t` (corner indices tri[3]); false when it does not.
	bool cutTriangle(const std::vector<float>& positions, const unsigned int tri[3], int axis, float value, float points[2][3])
	{
		bool positive[3];
		for (int c = 0; c < 3; ++c)
			positive[c] = positions[static_cast<std::size_t>(tri[c]) * 3 + axis] >= value;
		if (positive[0] == positive[1] && positive[1] == positive[2])
			return false;
		int found = 0;
		for (int e = 0; e < 3 && found < 2; ++e)
		{
			const unsigned int a = tri[e], b = tri[(e + 1) % 3];
			if (positive[e] == positive[(e + 1) % 3])
				continue;
			const float da = positions[static_cast<std::size_t>(a) * 3 + axis] - value;
			const float db = positions[static_cast<std::size_t>(b) * 3 + axis] - value;
			const float t = da / (da - db); // da and db are on opposite sides, so da - db != 0
			for (int k = 0; k < 3; ++k)
			{
				const float pa = positions[static_cast<std::size_t>(a) * 3 + k];
				const float pb = positions[static_cast<std::size_t>(b) * 3 + k];
				points[found][k] = pa + t * (pb - pa);
			}
			++found;
		}
		return found == 2;
	}

	bool triangleCorners(const std::vector<unsigned int>& indices, std::size_t vertexCount, std::size_t triangle, unsigned int tri[3])
	{
		const std::size_t first = triangle * 3;
		if (first + 2 >= indices.size())
			return false;
		for (int c = 0; c < 3; ++c)
		{
			tri[c] = indices[first + static_cast<std::size_t>(c)];
			if (tri[c] >= vertexCount)
				return false;
		}
		return true;
	}

	void appendSegment(std::vector<float>& out, const float points[2][3])
	{
		out.insert(out.end(), { points[0][0], points[0][1], points[0][2], points[1][0], points[1][1], points[1][2] });
	}
}

std::size_t plot3DSectionSegments(const std::vector<float>& positions, const std::vector<unsigned int>& indices,
	int axis, float value, std::vector<float>& out)
{
	std::size_t segments = 0;
	if (axis < 0 || axis > 2)
		return segments;
	const std::size_t vertexCount = positions.size() / 3;
	for (std::size_t t = 0; t < indices.size() / 3; ++t)
	{
		unsigned int tri[3];
		float points[2][3];
		if (triangleCorners(indices, vertexCount, t, tri) && cutTriangle(positions, tri, axis, value, points))
		{
			appendSegment(out, points);
			++segments;
		}
	}
	return segments;
}

std::vector<int> plot3DTriangleNeighbours(const std::vector<unsigned int>& indices)
{
	const std::size_t triangles = indices.size() / 3;
	std::vector<int> neighbours(triangles * 3, -1);
	std::unordered_map<std::uint64_t, std::size_t> open; // undirected edge -> (triangle * 3 + edge) that has it so far
	open.reserve(triangles * 2);
	for (std::size_t t = 0; t < triangles; ++t)
	{
		for (std::size_t e = 0; e < 3; ++e)
		{
			const std::uint64_t a = indices[t * 3 + e], b = indices[t * 3 + (e + 1) % 3];
			const std::uint64_t key = (a < b ? a : b) << 32 | (a < b ? b : a);
			const auto [it, inserted] = open.emplace(key, t * 3 + e);
			if (inserted)
				continue;
			const std::size_t other = it->second;
			if (neighbours[other] == -1 && neighbours[t * 3 + e] == -1)
			{
				neighbours[other] = static_cast<int>(t);
				neighbours[t * 3 + e] = static_cast<int>(other / 3);
			}
		}
	}
	return neighbours;
}

std::size_t plot3DSectionCurveThrough(const std::vector<float>& positions, const std::vector<unsigned int>& indices,
	const std::vector<int>& neighbours, int startTriangle, int axis, float value, std::vector<float>& out)
{
	const std::size_t triangles = indices.size() / 3;
	if (axis < 0 || axis > 2 || startTriangle < 0 || static_cast<std::size_t>(startTriangle) >= triangles || neighbours.size() != triangles * 3)
		return 0;
	const std::size_t vertexCount = positions.size() / 3;

	std::vector<char> visited(triangles, 0);
	std::vector<std::size_t> pending{ static_cast<std::size_t>(startTriangle) };
	visited[static_cast<std::size_t>(startTriangle)] = 1;
	std::size_t segments = 0;
	while (!pending.empty())
	{
		const std::size_t t = pending.back();
		pending.pop_back();
		unsigned int tri[3];
		float points[2][3];
		if (!triangleCorners(indices, vertexCount, t, tri) || !cutTriangle(positions, tri, axis, value, points))
			continue;
		appendSegment(out, points);
		++segments;
		// The cut leaves this triangle through the edges whose ends lie on opposite sides of the plane; each leads to the
		// next triangle of the same curve.
		for (int e = 0; e < 3; ++e)
		{
			const bool a = positions[static_cast<std::size_t>(tri[e]) * 3 + axis] >= value;
			const bool b = positions[static_cast<std::size_t>(tri[(e + 1) % 3]) * 3 + axis] >= value;
			if (a == b)
				continue;
			const int next = neighbours[t * 3 + static_cast<std::size_t>(e)];
			if (next >= 0 && !visited[static_cast<std::size_t>(next)])
			{
				visited[static_cast<std::size_t>(next)] = 1;
				pending.push_back(static_cast<std::size_t>(next));
			}
		}
	}
	return segments;
}
