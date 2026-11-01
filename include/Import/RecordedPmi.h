#pragma once

#include "NodeProperties.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <cmath>
#include <limits>
#include <vector>

// Recorded PMI: dimensions an authoring system stored in the file (as opposed to measured ones, which the viewer computes from
// picked points). This header holds the pure parts - reading a node's flattened PMI rows into linear dimensions and placing a
// point on a triangle mesh - so they can be tested without the viewer. RecordedPmi.cpp turns them into scene measurements.
namespace RecordedPmi
{
// A linear dimension as stored: value and the two end points of its leader, in the part's coordinate unit and frame.
struct LinearDimension
{
	int entity = -1;           // index of the PMI entity on its node
	double value = 0.0;        // in the file's coordinate unit
	QString unit;              // the stored display-unit name, e.g. "millimeters"
	int precision = 2;
	QVector3D from, to;        // Leader[0].terminator, Leader[1].terminator
	QVector3D textOrigin;
	bool hasTextOrigin = false;
};

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

// Reads the "PMI <n> <type>" summary rows and "PMI <n>.<key>" property rows that NodeProperties produces for a part's JTPMI
// metadata. Only entities named "Dimension" whose `type` property is 1 (linear, per the JT-IF PMI guide) and that carry a value
// and both leader terminators are returned; everything else is left for later steps.
inline std::vector<LinearDimension> linearDimensions(const NodePropertySet& set)
{
	struct Entity { QString typeName; QMap<QString, QString> properties; };
	QMap<int, Entity> entities;
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
		const QChar separator = row.key[position];
		const QString rest = row.key.mid(position + 1);
		if (separator == QLatin1Char('.'))
			entities[number].properties.insert(rest, row.value);
		else if (separator == QLatin1Char(' '))
			entities[number].typeName = rest;
	}
	std::vector<LinearDimension> result;
	for (auto it = entities.cbegin(); it != entities.cend(); ++it)
	{
		const Entity& e = it.value();
		if (e.typeName != QLatin1String("Dimension") || e.properties.value(QStringLiteral("type")) != QLatin1String("1"))
			continue;
		LinearDimension d;
		d.entity = it.key();
		bool valueOk = false;
		d.value = e.properties.value(QStringLiteral("value")).toDouble(&valueOk);
		if (!valueOk || !std::isfinite(d.value)
			|| !parseVector(e.properties.value(QStringLiteral("Leader[0].terminator")), d.from)
			|| !parseVector(e.properties.value(QStringLiteral("Leader[1].terminator")), d.to))
			continue;
		d.unit = e.properties.value(QStringLiteral("unit"));
		d.precision = qBound(0, e.properties.value(QStringLiteral("precision"), QStringLiteral("2")).toInt(), 8);
		d.hasTextOrigin = parseVector(e.properties.value(QStringLiteral("textOrigin")), d.textOrigin);
		result.push_back(d);
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
