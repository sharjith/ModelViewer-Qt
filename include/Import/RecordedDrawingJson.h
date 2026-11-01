#pragma once

#include "RecordedDrawing.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <cstring>

// MVF form of a recorded drawing: the arrays as base64 of little-endian int32, int16 and float32 (every supported host is
// little-endian), the rest as plain JSON. Reading validates sizes and polyline bounds and returns null for anything malformed.
namespace RecordedPmi
{
template <class T>
inline QString packArray(const std::vector<T>& values)
{
	return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(values.data()), qsizetype(values.size() * sizeof(T))).toBase64());
}

template <class T>
inline bool unpackArray(const QString& text, std::vector<T>& out, size_t maxCount)
{
	const QByteArray raw = QByteArray::fromBase64(text.toLatin1());
	if (raw.size() % qsizetype(sizeof(T)) != 0 || size_t(raw.size()) / sizeof(T) > maxCount)
		return false;
	out.resize(size_t(raw.size()) / sizeof(T));
	if (!out.empty())
		std::memcpy(out.data(), raw.constData(), size_t(raw.size()));
	return true;
}

inline QJsonObject drawingToJson(const RecordedDrawing& d)
{
	QJsonObject o;
	if (!d.geometry)
		return o;
	const PmiEntityGeometry& g = *d.geometry;
	o.insert(QStringLiteral("meshUuid"), d.meshUuid.toString(QUuid::WithoutBraces));
	if (g.hasFrame)
	{
		QJsonArray frame;
		for (float v : g.frame)
			frame.append(double(v));
		o.insert(QStringLiteral("frame"), frame);
	}
	o.insert(QStringLiteral("indices"), packArray(g.indices));
	o.insert(QStringLiteral("types"), packArray(g.types));
	o.insert(QStringLiteral("vertices"), packArray(g.vertices));
	return o;
}

inline std::shared_ptr<const RecordedDrawing> drawingFromJson(const QJsonObject& o)
{
	constexpr size_t MaxVertexValues = size_t(3) << 24;
	if (!o.contains(QStringLiteral("vertices")))
		return nullptr;
	auto g = std::make_shared<PmiEntityGeometry>();
	if (!unpackArray(o[QStringLiteral("indices")].toString(), g->indices, size_t(1) << 24)
		|| !unpackArray(o[QStringLiteral("types")].toString(), g->types, size_t(1) << 24)
		|| !unpackArray(o[QStringLiteral("vertices")].toString(), g->vertices, MaxVertexValues)
		|| g->vertices.size() % 3 != 0)
		return nullptr;
	const QJsonArray frame = o[QStringLiteral("frame")].toArray();
	if (frame.size() == 9)
	{
		g->hasFrame = true;
		for (int i = 0; i < 9; ++i)
			g->frame[i] = float(frame[i].toDouble());
	}
	const size_t vertexCount = g->vertices.size() / 3;
	for (size_t k = 0; k < g->indices.size(); ++k)
		if (g->indices[k] < 0 || size_t(g->indices[k]) > vertexCount || (k && g->indices[k] < g->indices[k - 1]))
			return nullptr;
	auto d = std::make_shared<RecordedDrawing>();
	d->meshUuid = QUuid(o[QStringLiteral("meshUuid")].toString());
	d->geometry = g;
	return d;
}
}
