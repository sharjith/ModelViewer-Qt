// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Mesh-level repair of small gaps. Tessellating a B-Rep face by face leaves hairline cracks and small triangular holes where
// neighbouring faces were discretised differently or one face failed to mesh. The gaps are found on the mesh itself: vertices
// are welded by position, edges used by a single triangle are chained into boundary loops, and each small loop is closed by
// ear clipping with triangles that reuse the existing vertices (so the normals and colours stay those of the neighbours).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MeshGapFill {

struct Options {
    double weldFraction = 2e-5;    // position weld, as a fraction of the mesh bounding-box diagonal
    double maxLoopFraction = 0.35; // a loop whose bounding box is larger than this fraction of the diagonal is a real opening
    size_t maxLoopEdges = 400;     // longer boundary loops are left alone
};

struct Result {
    size_t boundaryEdges = 0;   // before filling
    size_t loopsFound = 0;
    size_t loopsFilled = 0;
    size_t trianglesAdded = 0;
};

// xyz: interleaved coordinates of the vertices; triangles: flat vertex indices (3 per triangle). New triangles are appended to
// `triangles`. Returns statistics.
inline Result fill(const float* xyz, size_t vertexCount, std::vector<unsigned>& triangles, const Options& options = {}) {
    Result result;
    if (vertexCount == 0 || triangles.size() < 3)
        return result;
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (size_t i = 0; i < vertexCount; ++i)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min<double>(lo[k], xyz[3 * i + k]);
            hi[k] = std::max<double>(hi[k], xyz[3 * i + k]);
        }
    const double diagonal = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
    if (!(diagonal > 0))
        return result;
    const double weld = std::max(options.weldFraction * diagonal, 1e-9);

    // Weld by position: grid cells of the weld size, neighbours checked.
    std::vector<unsigned> rep(vertexCount);
    {
        struct Key { int64_t x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
        struct Hash { size_t operator()(const Key& k) const { return static_cast<size_t>(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL); } };
        std::unordered_map<Key, std::vector<unsigned>, Hash> cells;
        cells.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const double x = xyz[3 * i], y = xyz[3 * i + 1], z = xyz[3 * i + 2];
            const Key c{static_cast<int64_t>(std::floor(x / weld)), static_cast<int64_t>(std::floor(y / weld)), static_cast<int64_t>(std::floor(z / weld))};
            unsigned found = static_cast<unsigned>(i);
            bool have = false;
            for (int dx = -1; dx <= 1 && !have; ++dx)
                for (int dy = -1; dy <= 1 && !have; ++dy)
                    for (int dz = -1; dz <= 1 && !have; ++dz) {
                        const auto it = cells.find(Key{c.x + dx, c.y + dy, c.z + dz});
                        if (it == cells.end())
                            continue;
                        for (const unsigned j : it->second) {
                            const double ex = xyz[3 * j] - x, ey = xyz[3 * j + 1] - y, ez = xyz[3 * j + 2] - z;
                            if (ex * ex + ey * ey + ez * ez <= weld * weld) {
                                found = j;
                                have = true;
                                break;
                            }
                        }
                    }
            rep[i] = found;
            if (!have)
                cells[c].push_back(static_cast<unsigned>(i));
        }
    }

    // Edge usage over welded vertices.
    std::map<std::pair<unsigned, unsigned>, int> use;
    const size_t triangleCount = triangles.size() / 3;
    for (size_t t = 0; t < triangleCount; ++t) {
        const unsigned v[3] = {rep[triangles[3 * t]], rep[triangles[3 * t + 1]], rep[triangles[3 * t + 2]]};
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
            continue;
        for (int k = 0; k < 3; ++k)
            ++use[{std::min(v[k], v[(k + 1) % 3]), std::max(v[k], v[(k + 1) % 3])}];
    }
    // Boundary half-edges: a -> b as used by the one triangle that has the edge.
    std::multimap<unsigned, unsigned> outgoing;
    for (size_t t = 0; t < triangleCount; ++t) {
        const unsigned v[3] = {rep[triangles[3 * t]], rep[triangles[3 * t + 1]], rep[triangles[3 * t + 2]]};
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
            continue;
        for (int k = 0; k < 3; ++k) {
            const unsigned a = v[k], b = v[(k + 1) % 3];
            if (use[{std::min(a, b), std::max(a, b)}] == 1) {
                outgoing.emplace(a, b);
                ++result.boundaryEdges;
            }
        }
    }
    if (outgoing.empty())
        return result;

    auto position = [&](unsigned v, int k) { return static_cast<double>(xyz[3 * v + k]); };
    // Chain half-edges into loops.
    while (!outgoing.empty()) {
        const unsigned start = outgoing.begin()->first;
        std::vector<unsigned> loop{start};
        unsigned current = start;
        bool closed = false;
        while (loop.size() <= options.maxLoopEdges + 1) {
            const auto it = outgoing.find(current);
            if (it == outgoing.end())
                break;
            const unsigned next = it->second;
            outgoing.erase(it);
            if (next == start) {
                closed = true;
                break;
            }
            loop.push_back(next);
            current = next;
        }
        if (!closed) {
            // Drop the half-edges of an open chain or an over-long loop; they are not touched again.
            if (loop.size() > options.maxLoopEdges) {
                for (const unsigned v : loop)
                    outgoing.erase(v);
            }
            continue;
        }
        ++result.loopsFound;
        const size_t n = loop.size();
        if (n < 3 || n > options.maxLoopEdges)
            continue;
        double blo[3] = {1e300, 1e300, 1e300}, bhi[3] = {-1e300, -1e300, -1e300};
        for (const unsigned v : loop)
            for (int k = 0; k < 3; ++k) {
                blo[k] = std::min(blo[k], position(v, k));
                bhi[k] = std::max(bhi[k], position(v, k));
            }
        const double size = std::sqrt((bhi[0] - blo[0]) * (bhi[0] - blo[0]) + (bhi[1] - blo[1]) * (bhi[1] - blo[1]) + (bhi[2] - blo[2]) * (bhi[2] - blo[2]));
        if (std::getenv("XT_GAPFILL_DEBUG"))
            std::fprintf(stderr, "loop edges %zu size %.4g (%.4g of diagonal)\n", n, size, size / diagonal);
        if (size > options.maxLoopFraction * diagonal)
            continue; // a real opening
        // The new triangles must run against the boundary: the loop is closed in reverse.
        std::vector<unsigned> poly(loop.rbegin(), loop.rend());
        // Newell normal of the polygon.
        double nx = 0, ny = 0, nz = 0;
        for (size_t i = 0; i < n; ++i) {
            const unsigned a = poly[i], b = poly[(i + 1) % n];
            nx += (position(a, 1) - position(b, 1)) * (position(a, 2) + position(b, 2));
            ny += (position(a, 2) - position(b, 2)) * (position(a, 0) + position(b, 0));
            nz += (position(a, 0) - position(b, 0)) * (position(a, 1) + position(b, 1));
        }
        const double normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (!(normalLength > 0))
            continue;
        nx /= normalLength; ny /= normalLength; nz /= normalLength;
        auto cross = [&](unsigned a, unsigned b, unsigned c) {
            const double ux = position(b, 0) - position(a, 0), uy = position(b, 1) - position(a, 1), uz = position(b, 2) - position(a, 2);
            const double vx = position(c, 0) - position(a, 0), vy = position(c, 1) - position(a, 1), vz = position(c, 2) - position(a, 2);
            return (uy * vz - uz * vy) * nx + (uz * vx - ux * vz) * ny + (ux * vy - uy * vx) * nz;
        };
        // Ear clipping; when no ear is found (a skew loop) the rest is fanned.
        std::vector<unsigned> remaining = poly;
        std::vector<unsigned> added;
        while (remaining.size() > 3) {
            const size_t m = remaining.size();
            bool clipped = false;
            for (size_t i = 0; i < m && !clipped; ++i) {
                const unsigned a = remaining[(i + m - 1) % m], b = remaining[i], c = remaining[(i + 1) % m];
                if (cross(a, b, c) <= 0)
                    continue;
                bool inside = false;
                for (size_t j = 0; j < m && !inside; ++j) {
                    const unsigned p = remaining[j];
                    if (p == a || p == b || p == c)
                        continue;
                    inside = cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0;
                }
                if (inside)
                    continue;
                added.insert(added.end(), {a, b, c});
                remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
                clipped = true;
            }
            if (!clipped)
                break;
        }
        for (size_t i = 1; i + 1 < remaining.size(); ++i)
            added.insert(added.end(), {remaining[0], remaining[i], remaining[i + 1]});
        triangles.insert(triangles.end(), added.begin(), added.end());
        result.trianglesAdded += added.size() / 3;
        ++result.loopsFilled;
    }
    return result;
}

} // namespace MeshGapFill
