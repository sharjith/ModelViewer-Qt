#pragma once

#include "PmiGeometry.h"

#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Filled text for recorded PMI. The file stores each letter as its outline cut into many short polylines (type 8) and no filled
// shape, so the pieces are chained back into closed loops (pieces meet at identical points), loops are nested by even-odd
// containment (a loop inside a letter's outline is a hole, as in "O" or "A"), and each outline with its holes is triangulated by
// bridging the holes into the outline and clipping ears. Anything that does not form clean closed loops (open strokes, points where
// three pieces meet) is left out and keeps its plain outline.
namespace RecordedPmi
{
namespace Glyph
{
struct P2 { double u = 0, v = 0; };

inline double cross(const P2& o, const P2& a, const P2& b) { return (a.u - o.u) * (b.v - o.v) - (a.v - o.v) * (b.u - o.u); }

inline double signedArea(const std::vector<P2>& poly)
{
	double a = 0;
	for (size_t i = 0; i < poly.size(); ++i)
	{
		const P2& p = poly[i];
		const P2& q = poly[(i + 1) % poly.size()];
		a += p.u * q.v - q.u * p.v;
	}
	return 0.5 * a;
}

inline bool inside(const std::vector<P2>& poly, const P2& p)
{
	bool in = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
		if ((poly[i].v > p.v) != (poly[j].v > p.v)
			&& p.u < (poly[j].u - poly[i].u) * (p.v - poly[i].v) / (poly[j].v - poly[i].v) + poly[i].u)
			in = !in;
	return in;
}

inline bool same(const P2& a, const P2& b, double tol) { return std::abs(a.u - b.u) <= tol && std::abs(a.v - b.v) <= tol; }

// True when every vertex of `inner` lies inside `outer`. A loop that only partly overlaps another (the slash through the ring of
// "Ø") is a separate outline, not a hole.
inline bool allInside(const std::vector<P2>& outer, const std::vector<P2>& inner)
{
	for (const P2& p : inner)
		if (!inside(outer, p))
			return false;
	return true;
}

// Rounds the corners the file's coarse curve tessellation leaves (a circle is stored with about a dozen segments): vertices that
// turn by less than about 50 degrees count as lying on a curve and the loop is cut at the quarter points twice (Chaikin);
// sharper vertices stay where they are.
inline std::vector<P2> smoothLoop(const std::vector<P2>& loop)
{
	const size_t n = loop.size();
	std::vector<P2> pts = loop;
	std::vector<char> corner(n, 0);
	for (size_t i = 0; i < n; ++i)
	{
		const P2& a = loop[(i + n - 1) % n];
		const P2& b = loop[i];
		const P2& c = loop[(i + 1) % n];
		const double ux = b.u - a.u, uy = b.v - a.v, vx = c.u - b.u, vy = c.v - b.v;
		const double lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
		corner[i] = lu <= 0 || lv <= 0 || (ux * vx + uy * vy) / (lu * lv) < 0.64; // cos 50 degrees
	}
	for (int pass = 0; pass < 2; ++pass)
	{
		std::vector<P2> next;
		std::vector<char> nextCorner;
		auto addPoint = [&](const P2& p, char isCorner) {
			if (!next.empty() && same(next.back(), p, 1e-12))
				return;
			next.push_back(p);
			nextCorner.push_back(isCorner);
		};
		const size_t m = pts.size();
		for (size_t i = 0; i < m; ++i)
		{
			const P2& a = pts[i];
			const P2& b = pts[(i + 1) % m];
			const bool ca = corner[i], cb = corner[(i + 1) % m];
			addPoint(ca ? a : P2{ 0.75 * a.u + 0.25 * b.u, 0.75 * a.v + 0.25 * b.v }, ca);
			addPoint(cb ? b : P2{ 0.25 * a.u + 0.75 * b.u, 0.25 * a.v + 0.75 * b.v }, cb);
		}
		if (next.size() > 1 && same(next.front(), next.back(), 1e-12))
		{
			next.pop_back();
			nextCorner.pop_back();
		}
		pts = std::move(next);
		corner = std::move(nextCorner);
	}
	return pts;
}

// True when the open segments ab and cd cross at a point interior to both.
inline bool properCross(const P2& a, const P2& b, const P2& c, const P2& d)
{
	const double d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
	return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

// Ear clipping of a simple or weakly simple polygon (counter-clockwise, vertices may repeat where a hole was bridged in).
inline void clipEars(std::vector<P2> poly, double tol, std::vector<std::array<P2, 3>>& out)
{
	const double areaTol = tol * tol;
	size_t guard = poly.size() * poly.size() + 16;
	while (poly.size() > 3 && guard--)
	{
		const size_t n = poly.size();
		bool clipped = false;
		for (size_t i = 0; i < n && !clipped; ++i)
		{
			const P2& a = poly[(i + n - 1) % n];
			const P2& b = poly[i];
			const P2& c = poly[(i + 1) % n];
			const double turn = cross(a, b, c);
			if (std::abs(turn) <= areaTol) // collinear or repeated point: drop it without a triangle
			{
				poly.erase(poly.begin() + std::ptrdiff_t(i));
				clipped = true;
				break;
			}
			if (turn < 0)
				continue;
			bool empty = true;
			for (size_t k = 0; k < n && empty; ++k)
			{
				const P2& p = poly[k];
				if (same(p, a, tol) || same(p, b, tol) || same(p, c, tol))
					continue;
				if (cross(a, b, p) >= -areaTol && cross(b, c, p) >= -areaTol && cross(c, a, p) >= -areaTol)
					empty = false;
			}
			if (!empty)
				continue;
			out.push_back({ a, b, c });
			poly.erase(poly.begin() + std::ptrdiff_t(i));
			clipped = true;
		}
		if (!clipped) // no ear: the outline is not clean enough, keep what was clipped
			return;
	}
	if (poly.size() == 3 && std::abs(cross(poly[0], poly[1], poly[2])) > areaTol)
		out.push_back({ poly[0], poly[1], poly[2] });
}

// Triangulates `outer` (any orientation) minus `holes`. Holes join the outline through bridges that cross nothing and run through
// the filled area; a hole that cannot be bridged is dropped, which fills it (the outline is still drawn on top).
inline void triangulate(std::vector<P2> outer, std::vector<std::vector<P2>> holes, double tol, std::vector<std::array<P2, 3>>& out)
{
	if (signedArea(outer) < 0)
		std::reverse(outer.begin(), outer.end());
	for (auto& h : holes)
		if (signedArea(h) > 0)
			std::reverse(h.begin(), h.end());

	std::vector<std::array<P2, 2>> edges; // every outline, hole and bridge edge, for the crossing tests
	auto addLoop = [&edges](const std::vector<P2>& l) {
		for (size_t i = 0; i < l.size(); ++i)
			edges.push_back({ l[i], l[(i + 1) % l.size()] });
	};
	addLoop(outer);
	for (const auto& h : holes)
		addLoop(h);

	// The hole with the rightmost vertex goes first, as in the usual hole elimination.
	std::sort(holes.begin(), holes.end(), [](const auto& a, const auto& b) {
		auto top = [](const std::vector<P2>& l) { double m = -1e300; for (const P2& p : l) m = std::max(m, p.u); return m; };
		return top(a) > top(b);
	});
	std::vector<P2> poly = outer;
	for (const auto& hole : holes)
	{
		size_t m = 0;
		for (size_t i = 1; i < hole.size(); ++i)
			if (hole[i].u > hole[m].u)
				m = i;
		std::vector<size_t> order(poly.size());
		for (size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		const P2 M = hole[m];
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			return (poly[a].u - M.u) * (poly[a].u - M.u) + (poly[a].v - M.v) * (poly[a].v - M.v)
				< (poly[b].u - M.u) * (poly[b].u - M.u) + (poly[b].v - M.v) * (poly[b].v - M.v);
		});
		// A corner an earlier bridge already uses appears twice in the polygon; joining there could pick the wrong copy, so such
		// corners are tried only when nothing else is visible.
		for (int pass = 0; pass < 2; ++pass)
		for (size_t pi : order)
		{
			const P2 V = poly[pi];
			if (pass == 0)
			{
				int copies = 0;
				for (const P2& q : poly)
					copies += same(q, V, tol) ? 1 : 0;
				if (copies > 1)
					continue;
			}
			bool clear = true;
			for (const auto& e : edges)
				if (!same(e[0], V, tol) && !same(e[1], V, tol) && !same(e[0], M, tol) && !same(e[1], M, tol)
					&& properCross(M, V, e[0], e[1]))
				{
					clear = false;
					break;
				}
			if (!clear)
				continue;
			const P2 mid{ 0.5 * (M.u + V.u), 0.5 * (M.v + V.v) };
			if (!inside(outer, mid))
				continue;
			bool inAHole = false;
			for (const auto& h : holes)
				if (inside(h, mid))
				{
					inAHole = true;
					break;
				}
			if (inAHole)
				continue;
			std::vector<P2> merged(poly.begin(), poly.begin() + std::ptrdiff_t(pi) + 1);
			for (size_t k = 0; k <= hole.size(); ++k)
				merged.push_back(hole[(m + k) % hole.size()]);
			merged.push_back(V);
			merged.insert(merged.end(), poly.begin() + std::ptrdiff_t(pi) + 1, poly.end());
			poly = std::move(merged);
			edges.push_back({ V, M });
			pass = 2;
			break;
		}
	}
	clipEars(std::move(poly), tol, out);
}

struct Box
{
	double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
	double width() const { return u1 - u0; }
	double height() const { return v1 - v0; }
};

inline Box boxOf(const std::vector<P2>& loop)
{
	Box b;
	for (const P2& p : loop)
	{
		b.u0 = std::min(b.u0, p.u);
		b.u1 = std::max(b.u1, p.u);
		b.v0 = std::min(b.v0, p.v);
		b.v1 = std::max(b.v1, p.v);
	}
	return b;
}

// A circle drawn as one line: the circularity and concentricity symbols of a feature control frame come from a symbol font whose circles
// are single lines, not outlines, so the loop must stay an outline (filling it gives a solid disc). A letter's closed outline is not a
// circle: its bounding box is not square, or its area is not that of the inscribed ellipse, or it is a dot far smaller than the text.
inline bool isLineCircle(const std::vector<P2>& loop, double area, double biggest)
{
	const Box b = boxOf(loop);
	const double w = b.width(), h = b.height();
	if (w <= 0 || h <= 0)
		return false;
	const double aspect = w / h;
	if (aspect < 0.9 || aspect > 1.1 || std::min(w, h) < 0.35 * biggest)
		return false;
	const double fillRatio = area / (w * h);
	return fillRatio > 0.7 && fillRatio < 0.85;
}

// Fills closed loops by even-odd containment: a loop wholly inside an odd number of others is a hole of the smallest loop around
// it, and loops that merely overlap are separate shapes. Triangles are appended to `out`. A loop that is a circle drawn as one line,
// alone or as a ring of two concentric ones, is not filled (see isLineCircle).
inline void fillLoops(const std::vector<std::vector<P2>>& loops, double tol, std::vector<std::array<P2, 3>>& out)
{
	const size_t n = loops.size();
	std::vector<double> area(n);
	double biggest = 0;
	for (size_t i = 0; i < n; ++i)
	{
		area[i] = std::abs(Glyph::signedArea(loops[i]));
		const Box b = boxOf(loops[i]);
		biggest = std::max(biggest, std::max(b.width(), b.height()));
	}
	std::vector<int> depth(n, 0), parent(n, -1);
	for (size_t i = 0; i < n; ++i)
		for (size_t j = 0; j < n; ++j)
			if (i != j && area[j] > area[i] && Glyph::allInside(loops[j], loops[i]))
			{
				++depth[i];
				if (parent[i] < 0 || area[j] < area[size_t(parent[i])])
					parent[i] = int(j);
			}

	for (size_t i = 0; i < n; ++i)
	{
		if (depth[i] % 2 != 0)
			continue;
		std::vector<std::vector<P2>> holes;
		std::vector<size_t> holeIndex;
		for (size_t j = 0; j < n; ++j)
			if (depth[j] % 2 != 0 && parent[j] == int(i))
			{
				holes.push_back(loops[j]);
				holeIndex.push_back(j);
			}
		bool lineCircle = false;
		if (holeIndex.empty())
			lineCircle = isLineCircle(loops[i], area[i], biggest);
		else if (holeIndex.size() == 1 && isLineCircle(loops[i], area[i], biggest) && isLineCircle(loops[holeIndex[0]], area[holeIndex[0]], 0.0))
		{
			const Box o = boxOf(loops[i]), h = boxOf(loops[holeIndex[0]]);
			lineCircle = std::abs((o.u0 + o.u1) - (h.u0 + h.u1)) < 0.1 * o.width() && std::abs((o.v0 + o.v1) - (h.v0 + h.v1)) < 0.1 * o.height();
		}
		if (lineCircle)
			continue; // stays an outline
		triangulate(loops[i], std::move(holes), tol, out);
	}
}
}

// The filled text of a drawing, in the part's frame: `triangles` (x y z, three vertices each) and `outlines` (x y z, two vertices per
// segment) that replace the stored text polylines: smoothed loops, and the stored pieces of anything that is not a clean loop.
struct GlyphFill
{
	std::vector<float> triangles;
	std::vector<float> outlines;
	std::vector<size_t> absorbed; // plain (non-text) polylines that turned out to be part of a letter and are drawn through `outlines`
};

// Fills the text strokes of `g`. `types` gives each polyline's type (see polylineTypes) and `textType` marks text. Empty without a
// frame.
inline GlyphFill glyphFill(const PmiEntityGeometry& g, const std::vector<std::int16_t>& types, std::int16_t textType)
{
	using Glyph::P2;
	GlyphFill fill;
	std::vector<float>& result = fill.triangles;
	if (!g.hasFrame || g.indices.size() < 2)
		return fill;
	const QVector3D origin(g.frame[0], g.frame[1], g.frame[2]);
	QVector3D xAxis = QVector3D(g.frame[3], g.frame[4], g.frame[5]) - origin;
	QVector3D yAxis = QVector3D(g.frame[6], g.frame[7], g.frame[8]) - origin;
	xAxis.normalize();
	yAxis.normalize();
	const QVector3D normal = QVector3D::crossProduct(xAxis, yAxis).normalized();
	const size_t vertexCount = g.vertices.size() / 3;

	// Text points in the plane, nodes snapped to a grid so pieces that meet share a node.
	std::vector<P2> nodes;
	std::unordered_map<std::int64_t, std::vector<int>> grid;
	double low = 1e300, high = -1e300, lift = 0;
	size_t liftCount = 0;
	std::vector<P2> raw;
	std::vector<std::vector<size_t>> rawPieces;
	for (size_t k = 0; k + 1 < g.indices.size() && k < types.size(); ++k)
	{
		if (types[k] != textType)
			continue;
		const std::int32_t begin = g.indices[k], end = g.indices[k + 1];
		if (begin < 0 || end - begin < 2 || size_t(end) > vertexCount)
			continue;
		std::vector<size_t> ids;
		for (std::int32_t i = begin; i < end; ++i)
		{
			const QVector3D p(g.vertices[size_t(i) * 3], g.vertices[size_t(i) * 3 + 1], g.vertices[size_t(i) * 3 + 2]);
			const QVector3D d = p - origin;
			const P2 q{ double(QVector3D::dotProduct(d, xAxis)), double(QVector3D::dotProduct(d, yAxis)) };
			lift += double(QVector3D::dotProduct(d, normal));
			++liftCount;
			low = std::min({ low, q.u, q.v });
			high = std::max({ high, q.u, q.v });
			ids.push_back(raw.size());
			raw.push_back(q);
		}
		rawPieces.push_back(std::move(ids));
	}
	if (raw.empty())
		return fill;
	const double tol = std::max(1e-6, 1e-5 * (high - low));
	auto nodeOf = [&](const P2& p, bool create = true) {
		const std::int64_t cu = std::llround(p.u / tol), cv = std::llround(p.v / tol);
		for (std::int64_t du = -1; du <= 1; ++du)
			for (std::int64_t dv = -1; dv <= 1; ++dv)
			{
				const auto it = grid.find((cu + du) * 2000003LL + (cv + dv));
				if (it == grid.end())
					continue;
				for (int id : it->second)
					if (Glyph::same(nodes[size_t(id)], p, 2 * tol))
						return id;
			}
		if (!create)
			return -1;
		nodes.push_back(p);
		grid[cu * 2000003LL + cv].push_back(int(nodes.size()) - 1);
		return int(nodes.size()) - 1;
	};
	std::vector<std::vector<int>> adjacency;
	for (const auto& ids : rawPieces)
	{
		int previous = -1;
		for (size_t id : ids)
		{
			const int node = nodeOf(raw[id]);
			if (adjacency.size() < nodes.size())
				adjacency.resize(nodes.size());
			if (previous >= 0 && previous != node)
			{
				auto link = [&adjacency](int a, int b) {
					if (std::find(adjacency[size_t(a)].begin(), adjacency[size_t(a)].end(), b) == adjacency[size_t(a)].end())
						adjacency[size_t(a)].push_back(b);
				};
				link(previous, node);
				link(node, previous);
			}
			previous = node;
		}
	}

	// The file sometimes stores a piece of a letter as a plain line (the arc that closes the ring of "Ø", the dot of a decimal
	// point): a non-text polyline whose two ends are both loose ends of the text strokes belongs to the letter.
	{
		std::vector<char> loose(nodes.size(), 0);
		for (size_t i = 0; i < nodes.size(); ++i)
			loose[i] = adjacency[i].size() == 1;
		auto vertexAt = [&](std::int32_t i) {
			const QVector3D d = QVector3D(g.vertices[size_t(i) * 3], g.vertices[size_t(i) * 3 + 1], g.vertices[size_t(i) * 3 + 2]) - origin;
			return P2{ double(QVector3D::dotProduct(d, xAxis)), double(QVector3D::dotProduct(d, yAxis)) };
		};
		for (size_t k = 0; k + 1 < g.indices.size() && k < types.size(); ++k)
		{
			const std::int32_t begin = g.indices[k], end = g.indices[k + 1];
			if (types[k] == textType || begin < 0 || end - begin < 2 || size_t(end) > vertexCount)
				continue;
			const int first = nodeOf(vertexAt(begin), false), last = nodeOf(vertexAt(end - 1), false);
			if (first < 0 || last < 0 || first == last || !loose[size_t(first)] || !loose[size_t(last)])
				continue;
			int previous = -1;
			for (std::int32_t i = begin; i < end; ++i)
			{
				const int node = nodeOf(vertexAt(i));
				if (adjacency.size() < nodes.size())
					adjacency.resize(nodes.size());
				if (previous >= 0 && previous != node)
				{
					auto link = [&adjacency](int a, int b) {
						if (std::find(adjacency[size_t(a)].begin(), adjacency[size_t(a)].end(), b) == adjacency[size_t(a)].end())
							adjacency[size_t(a)].push_back(b);
					};
					link(previous, node);
					link(node, previous);
				}
				previous = node;
			}
			fill.absorbed.push_back(k);
		}
	}

	// Components in which every node has exactly two neighbours are closed loops.
	std::vector<std::vector<P2>> loops;
	std::vector<char> seen(nodes.size(), 0), inLoop(nodes.size(), 0);
	adjacency.resize(nodes.size());
	for (size_t start = 0; start < nodes.size(); ++start)
	{
		if (seen[start])
			continue;
		std::vector<int> component{ int(start) };
		seen[start] = 1;
		bool clean = true;
		int ends = -1; // a chain with exactly two loose ends is an outline whose closing edge the file leaves implied
		int loose = 0;
		for (size_t c = 0; c < component.size(); ++c)
		{
			const size_t degree = adjacency[size_t(component[c])].size();
			if (degree == 1)
			{
				++loose;
				if (ends < 0)
					ends = component[c];
			}
			else if (degree != 2)
				clean = false;
			for (int next : adjacency[size_t(component[c])])
				if (!seen[size_t(next)])
				{
					seen[size_t(next)] = 1;
					component.push_back(next);
				}
		}
		if (!clean || (loose != 0 && loose != 2) || component.size() < 3)
			continue;
		std::vector<P2> loop;
		int previous = -1, current = loose ? ends : int(start);
		for (size_t step = 0; step < component.size(); ++step)
		{
			loop.push_back(nodes[size_t(current)]);
			const auto& nb = adjacency[size_t(current)];
			const int next = nb.size() == 1 || nb[0] != previous ? nb[0] : nb[1];
			previous = current;
			current = next;
		}
		for (int node : component)
			inLoop[size_t(node)] = 1;
		loops.push_back(Glyph::smoothLoop(loop));
	}

	std::vector<std::array<P2, 3>> triangles;
	Glyph::fillLoops(loops, tol, triangles);

	const double w = liftCount ? lift / double(liftCount) : 0.0;
	auto lifted = [&](const P2& p) { return origin + xAxis * float(p.u) + yAxis * float(p.v) + normal * float(w); };
	result.reserve(triangles.size() * 9);
	for (const auto& t : triangles)
		for (const P2& p : t)
		{
			const QVector3D q = lifted(p);
			result.insert(result.end(), { q.x(), q.y(), q.z() });
		}
	auto segment = [&](const P2& a, const P2& b) {
		const QVector3D p = lifted(a), q = lifted(b);
		fill.outlines.insert(fill.outlines.end(), { p.x(), p.y(), p.z(), q.x(), q.y(), q.z() });
	};
	for (const auto& loop : loops)
		for (size_t i = 0; i < loop.size(); ++i)
			segment(loop[i], loop[(i + 1) % loop.size()]);
	for (size_t a = 0; a < nodes.size(); ++a)
		if (!inLoop[a])
			for (int b : adjacency[a])
				if (size_t(b) > a)
					segment(nodes[a], nodes[size_t(b)]);
	return fill;
}

inline std::vector<float> glyphFillTriangles(const PmiEntityGeometry& g, const std::vector<std::int16_t>& types, std::int16_t textType)
{
	return glyphFill(g, types, textType).triangles;
}
}
