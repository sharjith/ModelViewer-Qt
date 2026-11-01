#pragma once

#include "PmiGeometry.h"
#include "RecordedGlyphFill.h"

#include <QMatrix4x4>
#include <QUuid>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

// How a recorded PMI item is drawn: its stored polylines, placed in 3D through the owning mesh's render transform, so the item
// stays in the annotation plane its author defined and rotates with the model instead of facing the camera (see
// research/jt/JT_PMI_STUDY.md, "Annotation planes and how PMI is displayed").
struct RecordedDrawing
{
	QUuid meshUuid;                                  // the mesh whose render transform places the drawing (its part's frame)
	std::shared_ptr<const PmiEntityGeometry> geometry; // shared with the importer's per-part data
	// Triangles that fill the text outlines (see glyphFillTriangles), built on first use. Not stored in the MVF.
	mutable bool fillBuilt = false;
	mutable std::vector<float> fill;
	mutable std::vector<size_t> fillAbsorbed; // polylines drawn through fillOutlines instead of as stored
	mutable std::vector<float> fillOutlines; // smoothed outlines that replace the stored text polylines when the fill is drawn
};

namespace RecordedPmi
{
constexpr std::int16_t TextPolylineType = 8; // JT 10.6 Table 59: "text line", the glyph strokes in the controls

// The type of each polyline of the drawing. `types` holds pairs (up to polyline index, type); polylines past the last pair, or
// all of them when the pairs are missing or malformed, count as general lines (type 0).
inline std::vector<std::int16_t> polylineTypes(const PmiEntityGeometry& g)
{
	const size_t polylines = g.indices.size() > 1 ? g.indices.size() - 1 : 0;
	std::vector<std::int16_t> result(polylines, 0);
	size_t from = 0;
	for (size_t pair = 0; pair + 1 < g.types.size(); pair += 2)
	{
		const std::int16_t upTo = g.types[pair];
		if (upTo < 0 || size_t(upTo) < from)
			break;
		const size_t to = std::min(size_t(upTo), polylines);
		for (size_t k = from; k < to; ++k)
			result[k] = g.types[pair + 1];
		from = to;
	}
	return result;
}

// Mirrors p about the plane through `anchor` perpendicular to the unit vector `xAxis`.
inline QVector3D flipAboutAnchor(const QVector3D& p, const QVector3D& anchor, const QVector3D& xAxis)
{
	return p - 2.0f * QVector3D::dotProduct(p - anchor, xAxis) * xAxis;
}

// True when the viewer at `eye` sees the annotation plane from its back side. `toWorld` carries the part's frame to the world.
inline bool seenFromBehind(const PmiEntityGeometry& g, const QMatrix4x4& toWorld, const QVector3D& eye)
{
	if (!g.hasFrame)
		return false;
	const QVector3D origin(g.frame[0], g.frame[1], g.frame[2]);
	const QVector3D x = toWorld.mapVector(QVector3D(g.frame[3], g.frame[4], g.frame[5]) - origin);
	const QVector3D y = toWorld.mapVector(QVector3D(g.frame[6], g.frame[7], g.frame[8]) - origin);
	const QVector3D normal = QVector3D::crossProduct(x, y);
	return QVector3D::dotProduct(normal, eye - toWorld.map(origin)) < 0.0f;
}

// The coordinate, along the frame's X axis (unit vector `xAxis` from the frame origin `origin`), of the middle of the extent of
// all text strokes; NaN when there are none. Text mirrors about this when its plane is seen from behind: each text, and a symbol's
// text group, stays where it is and reads left to right, like the Hole/A text in its circle (the file's own text origin is
// the group's left end and would move the text out of its symbol).
inline float textPivot(const PmiEntityGeometry& g, const std::vector<std::int16_t>& types, const QVector3D& origin, const QVector3D& xAxis)
{
	float low = std::numeric_limits<float>::infinity(), high = -std::numeric_limits<float>::infinity();
	const size_t vertexCount = g.vertices.size() / 3;
	for (size_t k = 0; k + 1 < g.indices.size() && k < types.size(); ++k)
	{
		if (types[k] != TextPolylineType)
			continue;
		const std::int32_t begin = g.indices[k], end = g.indices[k + 1];
		if (begin < 0 || end <= begin || size_t(end) > vertexCount)
			continue;
		for (std::int32_t i = begin; i < end; ++i)
		{
			const QVector3D p(g.vertices[size_t(i) * 3], g.vertices[size_t(i) * 3 + 1], g.vertices[size_t(i) * 3 + 2]);
			const float u = QVector3D::dotProduct(p - origin, xAxis);
			low = std::min(low, u);
			high = std::max(high, u);
		}
	}
	return low <= high ? 0.5f * (low + high) : std::numeric_limits<float>::quiet_NaN();
}

// Appends the drawing as line segments (x y z r g b per vertex) and, when `triangles` is given, fills the small closed
// triangles that are arrowheads (four points, last equal to first, not text) and the letters of the text; without it they stay
// outlines. When the plane is
// seen from behind the text strokes are mirrored about their own centre (see textPivot). Invalid polylines are skipped, never
// read past the data.
inline void appendStrokes(const RecordedDrawing& drawing, const QMatrix4x4& toWorld, const QVector3D& eye, const QVector3D& color,
	std::vector<float>& lines, std::vector<float>* triangles)
{
	if (!drawing.geometry)
		return;
	const PmiEntityGeometry& g = *drawing.geometry;
	if (g.indices.size() < 2 || g.vertices.size() < 3)
		return;
	const size_t vertexCount = g.vertices.size() / 3;
	const std::vector<std::int16_t> types = polylineTypes(g);

	QVector3D origin, xAxis(1, 0, 0);
	if (g.hasFrame)
	{
		origin = QVector3D(g.frame[0], g.frame[1], g.frame[2]);
		xAxis = QVector3D(g.frame[3], g.frame[4], g.frame[5]) - origin;
		xAxis.normalize();
	}
	QVector3D anchor;
	bool mirror = g.hasFrame && seenFromBehind(g, toWorld, eye);
	if (mirror)
	{
		const float pivot = textPivot(g, types, origin, xAxis);
		mirror = std::isfinite(pivot);
		anchor = origin + xAxis * pivot;
	}

	auto push = [&color](std::vector<float>& out, const QVector3D& p) {
		out.insert(out.end(), { p.x(), p.y(), p.z(), color.x(), color.y(), color.z() });
	};
	if (triangles)
	{
		if (!drawing.fillBuilt)
		{
			GlyphFill built = glyphFill(g, types, TextPolylineType);
			drawing.fill = std::move(built.triangles);
			drawing.fillOutlines = std::move(built.outlines);
			drawing.fillAbsorbed = std::move(built.absorbed);
			drawing.fillBuilt = true;
		}
		for (size_t i = 0; i + 2 < drawing.fill.size(); i += 9)
		{
			QVector3D t[3];
			for (int c = 0; c < 3; ++c)
			{
				QVector3D p(drawing.fill[i + size_t(c) * 3], drawing.fill[i + size_t(c) * 3 + 1], drawing.fill[i + size_t(c) * 3 + 2]);
				if (mirror)
					p = flipAboutAnchor(p, anchor, xAxis);
				t[c] = toWorld.map(p);
			}
			for (int c : { 0, 1, 2, 0, 2, 1 })
				push(*triangles, t[c]);
		}
	}
	for (size_t k = 0; k + 1 < g.indices.size(); ++k)
	{
		const std::int32_t begin = g.indices[k], end = g.indices[k + 1];
		if (begin < 0 || end <= begin || size_t(end) > vertexCount)
			continue;
		const bool text = types[k] == TextPolylineType;
		if (triangles && !drawing.fillOutlines.empty()
			&& (text || std::find(drawing.fillAbsorbed.begin(), drawing.fillAbsorbed.end(), k) != drawing.fillAbsorbed.end()))
			continue; // drawn below from the smoothed outlines
		std::vector<QVector3D> world;
		world.reserve(size_t(end - begin));
		for (std::int32_t i = begin; i < end; ++i)
		{
			QVector3D p(g.vertices[size_t(i) * 3], g.vertices[size_t(i) * 3 + 1], g.vertices[size_t(i) * 3 + 2]);
			if (mirror && text)
				p = flipAboutAnchor(p, anchor, xAxis);
			world.push_back(toWorld.map(p));
		}
		const bool arrow = !text && world.size() == 4 && (world.front() - world.back()).lengthSquared() < 1e-12f;
		if (arrow && triangles)
		{
			push(*triangles, world[0]); push(*triangles, world[1]); push(*triangles, world[2]);
			push(*triangles, world[0]); push(*triangles, world[2]); push(*triangles, world[1]);
		}
		for (size_t i = 0; i + 1 < world.size(); ++i)
		{
			push(lines, world[i]);
			push(lines, world[i + 1]);
		}
	}
	if (triangles)
	{
		for (size_t i = 0; i + 5 < drawing.fillOutlines.size(); i += 6)
			for (int c = 0; c < 2; ++c)
			{
				QVector3D p(drawing.fillOutlines[i + size_t(c) * 3], drawing.fillOutlines[i + size_t(c) * 3 + 1],
					drawing.fillOutlines[i + size_t(c) * 3 + 2]);
				if (mirror)
					p = flipAboutAnchor(p, anchor, xAxis);
				push(lines, toWorld.map(p));
			}
	}
}
}
