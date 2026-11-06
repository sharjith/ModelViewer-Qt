#pragma once

#include "RecordedGlyphFill.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QVector3D>
#include <vector>

// Text as filled triangles that lie in a plane of the scene, so a measured dimension's value stays in the plane of its dimension
// line and turns with the model, as in CAD viewers, instead of always facing the camera.
namespace InPlaneText
{
struct Outline
{
	std::vector<float> uv; // u v per vertex, three vertices per triangle; unit = cap height, baseline at v = 0
	float left = 0, right = 0;
};

// A tab means "move to the next tab stop", which a glyph outline cannot express: spaces up to the next multiple of `tabWidth`
// characters take its place, so indented notes keep their indentation.
inline QString expandTabs(const QString& text, int tabWidth = 4)
{
	QString out;
	for (const QChar ch : text)
	{
		if (ch == QChar('\t'))
			out.append(QString(tabWidth - out.size() % tabWidth, QChar(' ')));
		else
			out.append(ch);
	}
	return out;
}

// The triangulated outline of `text` in the application's default font, bold, cached per string.
inline const Outline& outline(const QString& text)
{
	static QHash<QString, Outline> cache;
	const auto found = cache.constFind(text);
	if (found != cache.constEnd())
		return *found;

	Outline result;
	QFont font;
	font.setPixelSize(100);
	font.setBold(true); // thick strokes keep the yellow visible inside the dark outline at small sizes
	const qreal cap = std::max<qreal>(QFontMetricsF(font).capHeight(), 1.0);
	QPainterPath path;
	const QString expanded = expandTabs(text);
	path.addText(0, 0, font, expanded);

	std::vector<std::vector<RecordedPmi::Glyph::P2>> loops;
	for (const QPolygonF& polygon : path.toSubpathPolygons())
	{
		std::vector<RecordedPmi::Glyph::P2> loop;
		for (const QPointF& p : polygon)
			loop.push_back({ p.x() / cap, -p.y() / cap });
		if (loop.size() > 1 && RecordedPmi::Glyph::same(loop.front(), loop.back(), 1e-9))
			loop.pop_back();
		if (loop.size() >= 3)
			loops.push_back(std::move(loop));
	}
	std::vector<std::array<RecordedPmi::Glyph::P2, 3>> triangles;
	RecordedPmi::Glyph::fillLoops(loops, 1e-6, triangles);
	for (const auto& t : triangles)
		for (const RecordedPmi::Glyph::P2& p : t)
			result.uv.insert(result.uv.end(), { float(p.u), float(p.v) });
	const QRectF box = path.boundingRect();
	// Leading spaces (an indent) have no glyphs, so a line that starts with whitespace is measured from where its pen starts,
	// or left alignment would drop the indent.
	result.left = expanded.startsWith(QChar(' ')) ? 0.0f : float(box.left() / cap);
	result.right = float(box.right() / cap);
	return cache.insert(text, std::move(result)).value();
}

// Appends the triangles (x y z r g b per vertex) of `text`, centred on `anchor` along `xDir` with its baseline on `anchor`, `yDir`
// up, one cap height being `capHeight` long. `xDir` and `yDir` are unit vectors. `align` 0 centres the text on the anchor, a
// positive value starts it there and a negative value ends it there.
inline void appendText(const QString& text, const QVector3D& anchor, const QVector3D& xDir, const QVector3D& yDir, float capHeight,
	const QVector3D& color, std::vector<float>& triangles, float align = 0.0f)
{
	const Outline& o = outline(text);
	const float centre = align > 0.0f ? o.left : align < 0.0f ? o.right : 0.5f * (o.left + o.right);
	for (size_t i = 0; i + 1 < o.uv.size(); i += 2)
	{
		const QVector3D p = anchor + xDir * ((o.uv[i] - centre) * capHeight) + yDir * (o.uv[i + 1] * capHeight);
		triangles.insert(triangles.end(), { p.x(), p.y(), p.z(), color.x(), color.y(), color.z() });
	}
}

// The same text with a dark contrast outline behind it, so it reads on light and dark backgrounds alike: the text is first drawn
// in `haloColor` shifted a little in eight directions within its plane, then in `color` on top.
inline void appendHaloText(const QString& text, const QVector3D& anchor, const QVector3D& xDir, const QVector3D& yDir, float capHeight,
	const QVector3D& color, const QVector3D& haloColor, std::vector<float>& triangles, float align = 0.0f)
{
	const float reach = capHeight * 0.05f;
	for (int k = 0; k < 8; ++k)
	{
		const float angle = 0.78539816f * float(k);
		const QVector3D shift = xDir * (std::cos(angle) * reach) + yDir * (std::sin(angle) * reach);
		appendText(text, anchor + shift, xDir, yDir, capHeight, haloColor, triangles, align);
	}
	appendText(text, anchor, xDir, yDir, capHeight, color, triangles, align);
}
}
