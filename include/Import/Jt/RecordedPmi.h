#pragma once

#include "NodeProperties.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// Recorded PMI: items an authoring system stored in the file (as opposed to measured ones, which the viewer computes from
// picked points). This header holds the pure parts - reading a node's flattened PMI rows into dimensions and annotation-like
// items, and placing a point on a triangle mesh - so they can be tested without the viewer. RecordedPmiBuilder.cpp turns them
// into scene measurements and annotations.
//
// Rows come from NodeProperties (see JtSceneBuilder pmiMetadata()): "PMI <n> <type name>" summary rows, "PMI <n>.<key>" property
// rows, plus two synthetic rows per entity, "PMI <n>.(entity type)" (the decimal entity type code of the JT 10.6 reference,
// Table 56) and, when the entity stores a 2D reference frame, "PMI <n>.(frame origin)".
namespace RecordedPmi
{
// Entity type codes of the JT 10.6 reference, Table 56.
namespace TypeCode
{
constexpr int SurfaceFinish = 0x0041;
constexpr int Dimension = 0x0082;
constexpr int DatumTarget = 0x0088;
constexpr int Note = 0x0100;
constexpr int BalloonNote = 0x0235;
constexpr int Region = 0x0246;
}

inline bool parseVector(const QString& text, QVector3D& out)
{
	const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (parts.size() != 3)
		return false;
	bool ok[3] = { false, false, false };
	const double x = parts[0].toDouble(&ok[0]), y = parts[1].toDouble(&ok[1]), z = parts[2].toDouble(&ok[2]);
	if (!ok[0] || !ok[1] || !ok[2] || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
		return false;
	out = QVector3D(float(x), float(y), float(z));
	return true;
}

// A colour as the JT reference stores it: "0x00bbggrr" (blue, green, red). Colours too dark to read on the viewer's dark
// backgrounds are refused, so the item keeps the viewer's own colour instead of vanishing.
inline bool parseColour(const QString& text, QVector3D& out)
{
	bool ok = false;
	const quint32 value = text.trimmed().toUInt(&ok, 16);
	if (!ok)
		return false;
	const QVector3D rgb(float(value & 0xffu) / 255.0f, float((value >> 8) & 0xffu) / 255.0f, float((value >> 16) & 0xffu) / 255.0f);
	if (std::max({ rgb.x(), rgb.y(), rgb.z() }) < 0.25f)
		return false;
	out = rgb;
	return true;
}

// One PMI entity regrouped from the flattened rows.
struct Entity
{
	int number = -1;
	int entityType = -1; // -1 when the rows carry no type code
	QString typeName;
	QMap<QString, QString> properties;
};

inline std::vector<Entity> entities(const NodePropertySet& set)
{
	QMap<int, Entity> byNumber;
	for (const NodePropertyRow& row : set.rows)
	{
		if (!row.key.startsWith(QLatin1String("PMI ")))
			continue;
		int position = 4, number = 0;
		bool any = false;
		while (position < row.key.size() && row.key[position].isDigit())
		{
			number = number * 10 + row.key[position].digitValue();
			any = true;
			++position;
			if (number > 10000000)
				break;
		}
		if (!any || position >= row.key.size())
			continue;
		Entity& e = byNumber[number];
		e.number = number;
		const QChar separator = row.key[position];
		const QString rest = row.key.mid(position + 1);
		if (separator == QLatin1Char('.'))
		{
			if (rest == QLatin1String("(entity type)"))
			{
				bool ok = false;
				const int code = row.value.toInt(&ok);
				if (ok)
					e.entityType = code;
			}
			else
				e.properties.insert(rest, row.value);
		}
		else if (separator == QLatin1Char(' '))
			e.typeName = rest;
	}
	std::vector<Entity> result;
	for (auto it = byNumber.cbegin(); it != byNumber.cend(); ++it)
		result.push_back(it.value());
	return result;
}

enum class DimensionKind { Linear, Diameter, Radius, Angle };

// A dimension as stored. Values and points are in the part's coordinate unit and frame, except an angle's value, which is in
// radians (observed on one exporter; the angle format property says how the source displays it).
struct Dimension
{
	DimensionKind kind = DimensionKind::Linear;
	int entity = -1;
	double value = 0.0;
	QString unit;              // the stored display-unit name, e.g. "millimeters"
	int precision = 2;
	QVector3D from, to;        // linear: the two leader terminators. Diameter/radius: a rim point and the opposite rim point
	QVector3D vertex;          // angle: the vertex (the 2D frame origin, where the two extension lines meet)
	QVector3D textOrigin;
	bool hasTextOrigin = false;
	QVector3D color;           // PMIGeometryColor, when present and readable
	bool hasColor = false;
};

// Dimension entities whose rows carry what their kind needs:
//  linear   - type 1 with a value and both leader terminators;
//  diameter - type 3, style 0 or 3, a value, the rim terminator and the frame origin (the circle's centre);
//  radius   - type 3, style 1, same needs;
//  angle    - type 2 with a value and the frame origin (the vertex).
// Entities of other kinds, and incomplete ones, are left out.
inline std::vector<Dimension> dimensions(const NodePropertySet& set)
{
	std::vector<Dimension> result;
	for (const Entity& e : entities(set))
	{
		const bool isDimension = e.entityType == TypeCode::Dimension || (e.entityType < 0 && e.typeName == QLatin1String("Dimension"));
		if (!isDimension)
			continue;
		const QString type = e.properties.value(QStringLiteral("type"));
		const QString style = e.properties.value(QStringLiteral("style"));
		Dimension d;
		d.entity = e.number;
		bool valueOk = false;
		d.value = e.properties.value(QStringLiteral("value")).toDouble(&valueOk);
		if (!valueOk || !std::isfinite(d.value))
			continue;
		d.unit = e.properties.value(QStringLiteral("unit"));
		d.precision = qBound(0, e.properties.value(QStringLiteral("precision"), QStringLiteral("2")).toInt(), 8);
		d.hasTextOrigin = parseVector(e.properties.value(QStringLiteral("textOrigin")), d.textOrigin);
		d.hasColor = parseColour(e.properties.value(QStringLiteral("PMIGeometryColor")), d.color);
		QVector3D frameOrigin;
		const bool hasFrame = parseVector(e.properties.value(QStringLiteral("(frame origin)")), frameOrigin);
		QVector3D terminator;
		const bool hasTerminator = parseVector(e.properties.value(QStringLiteral("Leader[0].terminator")), terminator);
		if (type == QLatin1String("1"))
		{
			d.kind = DimensionKind::Linear;
			if (!hasTerminator || !parseVector(e.properties.value(QStringLiteral("Leader[1].terminator")), d.to))
				continue;
			d.from = terminator;
		}
		else if (type == QLatin1String("3"))
		{
			if (style == QLatin1String("0") || style == QLatin1String("3"))
				d.kind = DimensionKind::Diameter;
			else if (style == QLatin1String("1"))
				d.kind = DimensionKind::Radius;
			else
				continue;
			if (!hasTerminator || !hasFrame)
				continue;
			d.from = terminator;
			d.to = 2.0f * frameOrigin - terminator; // the opposite point of the same circle
		}
		else if (type == QLatin1String("2"))
		{
			if (!hasFrame)
				continue;
			d.kind = DimensionKind::Angle;
			d.vertex = frameOrigin;
		}
		else
			continue;
		result.push_back(d);
	}
	return result;
}

// Annotation-like entities: notes, balloon notes, datum targets, regions (datum points) and surface finish symbols.
struct Note
{
	int entity = -1;
	int entityType = -1;
	QString text;              // empty for symbols that carry none
	QVector3D anchor;          // where the symbol points at: its leader terminator, else its frame origin, else its text origin
	bool hasAnchor = false;
	bool hasLeader = false;
	QVector3D textOrigin;
	bool hasTextOrigin = false;
	QVector3D color;           // PMIGeometryColor, when present and readable
	bool hasColor = false;
};

// Text lines of an entity: `Text[i].Item[j].string` properties, items of one text joined by a space, texts by a newline.
inline QString noteText(const QMap<QString, QString>& properties)
{
	QMap<int, QMap<int, QString>> lines;
	for (auto it = properties.cbegin(); it != properties.cend(); ++it)
	{
		const QString& key = it.key();
		if (!key.startsWith(QLatin1String("Text[")) || !key.endsWith(QLatin1String("].string")))
			continue;
		// Text[i].Item[j].string
		const int closeFirst = key.indexOf(QLatin1Char(']'));
		const int openSecond = key.indexOf(QLatin1String(".Item["), closeFirst);
		const int closeSecond = key.indexOf(QLatin1Char(']'), openSecond + 6);
		if (closeFirst < 0 || openSecond < 0 || closeSecond < 0)
			continue;
		bool a = false, b = false;
		const int i = key.mid(5, closeFirst - 5).toInt(&a);
		const int j = key.mid(openSecond + 6, closeSecond - openSecond - 6).toInt(&b);
		if (a && b)
			lines[i].insert(j, it.value());
	}
	QStringList result;
	for (auto line = lines.cbegin(); line != lines.cend(); ++line)
		result.append(QStringList(line.value().values()).join(QLatin1Char(' ')));
	return result.join(QLatin1Char('\n'));
}

inline std::vector<Note> notes(const NodePropertySet& set)
{
	std::vector<Note> result;
	for (const Entity& e : entities(set))
	{
		Note n;
		n.entity = e.number;
		n.entityType = e.entityType;
		switch (e.entityType)
		{
		case TypeCode::Note: n.text = noteText(e.properties); break;
		case TypeCode::BalloonNote:
			n.text = noteText(e.properties);
			if (n.text.isEmpty())
				n.text = e.properties.value(QStringLiteral("balloonId"));
			break;
		case TypeCode::DatumTarget: n.text = e.properties.value(QStringLiteral("label")) + e.properties.value(QStringLiteral("index")); break;
		case TypeCode::Region: n.text = e.properties.value(QStringLiteral("Description")); break;
		case TypeCode::SurfaceFinish: break; // a symbol; the builder names it
		default: continue;
		}
		QVector3D point;
		if (parseVector(e.properties.value(QStringLiteral("Leader[0].terminator")), point))
		{
			n.anchor = point;
			n.hasAnchor = n.hasLeader = true;
		}
		else if (parseVector(e.properties.value(QStringLiteral("(frame origin)")), point))
		{
			n.anchor = point;
			n.hasAnchor = true;
		}
		n.hasTextOrigin = parseVector(e.properties.value(QStringLiteral("textOrigin")), n.textOrigin);
		n.hasColor = parseColour(e.properties.value(QStringLiteral("PMIGeometryColor")), n.color);
		if (!n.hasAnchor && n.hasTextOrigin)
		{
			n.anchor = n.textOrigin;
			n.hasAnchor = true;
		}
		if (n.hasAnchor)
			result.push_back(n);
	}
	return result;
}

struct TriangleHit
{
	int triangle = -1;
	QVector3D barycentric;
	double distance = std::numeric_limits<double>::infinity();
};

// Closest point of a triangle to p (Ericson, Real-Time Collision Detection 5.1.5), computed in double precision.
inline void closestPointOnTriangle(const double* a, const double* b, const double* c, const double* p, double* out)
{
	auto sub = [](const double* u, const double* v, double* r) { for (int i = 0; i < 3; ++i) r[i] = u[i] - v[i]; };
	auto dot = [](const double* u, const double* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
	double ab[3], ac[3], ap[3];
	sub(b, a, ab); sub(c, a, ac); sub(p, a, ap);
	const double d1 = dot(ab, ap), d2 = dot(ac, ap);
	auto set = [&](const double* v) { for (int i = 0; i < 3; ++i) out[i] = v[i]; };
	if (d1 <= 0 && d2 <= 0) { set(a); return; }
	double bp[3]; sub(p, b, bp);
	const double d3 = dot(ab, bp), d4 = dot(ac, bp);
	if (d3 >= 0 && d4 <= d3) { set(b); return; }
	const double vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) { const double t = d1 / (d1 - d3); for (int i = 0; i < 3; ++i) out[i] = a[i] + t * ab[i]; return; }
	double cp[3]; sub(p, c, cp);
	const double d5 = dot(ab, cp), d6 = dot(ac, cp);
	if (d6 >= 0 && d5 <= d6) { set(c); return; }
	const double vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) { const double t = d2 / (d2 - d6); for (int i = 0; i < 3; ++i) out[i] = a[i] + t * ac[i]; return; }
	const double va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
	{
		const double t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		for (int i = 0; i < 3; ++i) out[i] = b[i] + t * (c[i] - b[i]);
		return;
	}
	const double denom = 1.0 / (va + vb + vc), v = vb * denom, w = vc * denom;
	for (int i = 0; i < 3; ++i) out[i] = a[i] + ab[i] * v + ac[i] * w;
}

// The triangle of an indexed mesh nearest to p, with the barycentric weights of the nearest point on it (weights sum to 1).
inline TriangleHit closestTriangle(const std::vector<float>& points, const std::vector<unsigned>& indices, const QVector3D& p)
{
	TriangleHit best;
	const double target[3] = { p.x(), p.y(), p.z() };
	const size_t triangles = indices.size() / 3;
	for (size_t t = 0; t < triangles; ++t)
	{
		const size_t i0 = indices[3 * t], i1 = indices[3 * t + 1], i2 = indices[3 * t + 2];
		if (3 * i0 + 2 >= points.size() || 3 * i1 + 2 >= points.size() || 3 * i2 + 2 >= points.size())
			continue;
		const double a[3] = { points[3 * i0], points[3 * i0 + 1], points[3 * i0 + 2] };
		const double b[3] = { points[3 * i1], points[3 * i1 + 1], points[3 * i1 + 2] };
		const double c[3] = { points[3 * i2], points[3 * i2 + 1], points[3 * i2 + 2] };
		double q[3];
		closestPointOnTriangle(a, b, c, target, q);
		const double dx = q[0] - target[0], dy = q[1] - target[1], dz = q[2] - target[2];
		const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (distance >= best.distance)
			continue;
		// Barycentric weights of q in (a, b, c).
		double v0[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v1[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		double v2[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] };
		const double d00 = v0[0] * v0[0] + v0[1] * v0[1] + v0[2] * v0[2], d01 = v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2];
		const double d11 = v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
		const double d20 = v2[0] * v0[0] + v2[1] * v0[1] + v2[2] * v0[2], d21 = v2[0] * v1[0] + v2[1] * v1[1] + v2[2] * v1[2];
		const double denom = d00 * d11 - d01 * d01;
		if (std::abs(denom) < 1e-30)
			continue; // degenerate triangle
		const double v = (d11 * d20 - d01 * d21) / denom, w = (d00 * d21 - d01 * d20) / denom;
		best.triangle = int(t);
		best.distance = distance;
		best.barycentric = QVector3D(float(1.0 - v - w), float(v), float(w));
	}
	return best;
}
}
