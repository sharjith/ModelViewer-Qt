#include "Plot3DSessionIO.h"

#include <QJsonArray>
#include <QObject>

#include <cstring>

namespace
{
	// Little-endian raw arrays, deflated: colour values and source tables are highly repetitive, and a plot's
	// share of the file should stay small next to its own mesh.
	template <typename T>
	QByteArray packArray(const std::vector<T>& values)
	{
		return qCompress(QByteArray(reinterpret_cast<const char*>(values.data()),
			static_cast<int>(values.size() * sizeof(T))), 6);
	}

	template <typename T>
	bool unpackArray(const QByteArray& blob, std::vector<T>& out)
	{
		const QByteArray raw = qUncompress(blob);
		// qUncompress() returns an empty array for corrupt input; a legitimately empty array was never stored.
		if (raw.isEmpty() || raw.size() % static_cast<int>(sizeof(T)) != 0)
			return false;
		out.resize(static_cast<std::size_t>(raw.size()) / sizeof(T));
		std::memcpy(out.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
		return true;
	}

	std::vector<unsigned char> toBytes(const std::vector<bool>& flags)
	{
		return std::vector<unsigned char>(flags.begin(), flags.end());
	}

	QJsonArray toJson(const double* values, int count)
	{
		QJsonArray array;
		for (int i = 0; i < count; ++i)
			array.append(values[i]);
		return array;
	}

	// Registers blobs under a name and remembers where each landed, so the JSON stays readable and a reader can
	// ignore a blob it does not know about.
	struct BlobWriter
	{
		std::vector<QByteArray>& blobs;
		QJsonObject index;

		void add(const QString& name, QByteArray blob)
		{
			index.insert(name, static_cast<int>(blobs.size()));
			blobs.push_back(std::move(blob));
		}
		template <typename T>
		void addArray(const QString& name, const std::vector<T>& values)
		{
			if (!values.empty())
				add(name, packArray(values));
		}
	};

	struct BlobReader
	{
		const QJsonObject index;
		const std::vector<QByteArray>& blobs;
		QString failure;

		// False only when the blob is named but unusable. An absent name leaves `out` empty - that is how an
		// empty array was stored.
		template <typename T>
		bool readArray(const QString& name, std::vector<T>& out)
		{
			out.clear();
			if (!index.contains(name))
				return true;
			const int position = index.value(name).toInt(-1);
			if (position < 0 || position >= static_cast<int>(blobs.size()) || !unpackArray(blobs[static_cast<std::size_t>(position)], out))
			{
				failure = QObject::tr("The stored 3D Plot data '%1' is missing or damaged.").arg(name);
				return false;
			}
			return true;
		}
	};

	QJsonObject axisToJson(const Plot3DAxisConfig& axis)
	{
		QJsonObject json;
		json.insert(QStringLiteral("label"), axis.label);
		json.insert(QStringLiteral("scale"), static_cast<int>(axis.scale));
		json.insert(QStringLiteral("automaticRange"), axis.automaticRange);
		json.insert(QStringLiteral("minimum"), axis.minimum);
		json.insert(QStringLiteral("maximum"), axis.maximum);
		json.insert(QStringLiteral("targetTicks"), axis.targetTicks);
		json.insert(QStringLiteral("symlogLinearThreshold"), axis.symlogLinearThreshold);
		return json;
	}

	Plot3DAxisConfig axisFromJson(const QJsonObject& json)
	{
		Plot3DAxisConfig axis;
		axis.label = json.value(QStringLiteral("label")).toString();
		const int scale = json.value(QStringLiteral("scale")).toInt(0);
		axis.scale = scale == static_cast<int>(Plot3DAxisScale::Log10) ? Plot3DAxisScale::Log10
			: scale == static_cast<int>(Plot3DAxisScale::SymLog) ? Plot3DAxisScale::SymLog : Plot3DAxisScale::Linear;
		axis.automaticRange = json.value(QStringLiteral("automaticRange")).toBool(true);
		axis.minimum = json.value(QStringLiteral("minimum")).toDouble(0.0);
		axis.maximum = json.value(QStringLiteral("maximum")).toDouble(1.0);
		axis.targetTicks = json.value(QStringLiteral("targetTicks")).toInt(6);
		axis.symlogLinearThreshold = json.value(QStringLiteral("symlogLinearThreshold")).toDouble(1.0);
		return axis;
	}

	QJsonObject mappingToJson(const Plot3DColumnMapping& m)
	{
		QJsonObject json;
		json.insert(QStringLiteral("x"), m.x); json.insert(QStringLiteral("y"), m.y); json.insert(QStringLiteral("z"), m.z);
		json.insert(QStringLiteral("value"), m.value);
		json.insert(QStringLiteral("u"), m.u); json.insert(QStringLiteral("v"), m.v); json.insert(QStringLiteral("w"), m.w);
		json.insert(QStringLiteral("error"), m.error);
		json.insert(QStringLiteral("base"), m.base);
		json.insert(QStringLiteral("width"), m.width); json.insert(QStringLiteral("depth"), m.depth);
		return json;
	}

	Plot3DColumnMapping mappingFromJson(const QJsonObject& json)
	{
		Plot3DColumnMapping m; // member defaults apply to any key an older/newer file does not have
		m.x = json.value(QStringLiteral("x")).toInt(m.x); m.y = json.value(QStringLiteral("y")).toInt(m.y);
		m.z = json.value(QStringLiteral("z")).toInt(m.z);
		m.value = json.value(QStringLiteral("value")).toInt(m.value);
		m.u = json.value(QStringLiteral("u")).toInt(m.u); m.v = json.value(QStringLiteral("v")).toInt(m.v);
		m.w = json.value(QStringLiteral("w")).toInt(m.w);
		m.error = json.value(QStringLiteral("error")).toInt(m.error);
		m.base = json.value(QStringLiteral("base")).toInt(m.base);
		m.width = json.value(QStringLiteral("width")).toInt(m.width); m.depth = json.value(QStringLiteral("depth")).toInt(m.depth);
		return m;
	}

	constexpr int kBarStride = 7;     // x, y, base, height, width, depth, value
	constexpr int kSampleStride = 4;  // x, y, z, value
}

QJsonObject plot3DSessionToJson(const Plot3DSession& session, const Plot3DRendererPayload& payload,
	std::vector<QByteArray>& blobs)
{
	QJsonObject json;
	BlobWriter writer{ blobs, {} };

	json.insert(QStringLiteral("meshUuid"), session.meshUuid.toString(QUuid::WithoutBraces));
	json.insert(QStringLiteral("markerMeshUuid"), session.markerMeshUuid.isNull()
		? QString() : session.markerMeshUuid.toString(QUuid::WithoutBraces));
	json.insert(QStringLiteral("name"), session.name);
	json.insert(QStringLiteral("title"), session.title);
	json.insert(QStringLiteral("primitive"), static_cast<int>(session.primitive));

	QJsonArray axes;
	for (const Plot3DAxisConfig& axis : session.axes)
		axes.append(axisToJson(axis));
	json.insert(QStringLiteral("axes"), axes);
	json.insert(QStringLiteral("dataMinimum"), toJson(session.dataMinimum.data(), 3));
	json.insert(QStringLiteral("dataMaximum"), toJson(session.dataMaximum.data(), 3));

	json.insert(QStringLiteral("dataMinimumValue"), static_cast<double>(session.dataMinimumValue));
	json.insert(QStringLiteral("dataMaximumValue"), static_cast<double>(session.dataMaximumValue));
	json.insert(QStringLiteral("colourMinimum"), static_cast<double>(session.colourMinimum));
	json.insert(QStringLiteral("colourMaximum"), static_cast<double>(session.colourMaximum));
	json.insert(QStringLiteral("colormap"), session.colormap);
	json.insert(QStringLiteral("bands"), session.bands);

	json.insert(QStringLiteral("lineWidth"), static_cast<double>(session.lineWidth));
	json.insert(QStringLiteral("markerSize"), static_cast<double>(session.markerSize));
	json.insert(QStringLiteral("arrowScale"), static_cast<double>(session.arrowScale));
	json.insert(QStringLiteral("barWidthScale"), static_cast<double>(session.barWidthScale));
	json.insert(QStringLiteral("barDepthScale"), static_cast<double>(session.barDepthScale));

	json.insert(QStringLiteral("isStem"), session.isStem);
	json.insert(QStringLiteral("isErrorBars"), session.isErrorBars);
	json.insert(QStringLiteral("isFilledScatter"), session.isFilledScatter);
	json.insert(QStringLiteral("scatterBaseZ"), session.scatterBaseZ);
	json.insert(QStringLiteral("contourLevels"), session.contourLevels);

	json.insert(QStringLiteral("axesVisible"), session.axesVisible);
	QJsonArray planes;
	for (bool plane : session.referencePlanes)
		planes.append(plane);
	json.insert(QStringLiteral("referencePlanes"), planes);
	json.insert(QStringLiteral("referencePlaneOpacity"), static_cast<double>(session.referencePlaneOpacity));

	json.insert(QStringLiteral("editableCsv"), session.editableCsv);
	json.insert(QStringLiteral("csvDelimiter"), QString(session.csvOptions.delimiter));
	json.insert(QStringLiteral("csvHeader"), session.csvOptions.firstRowIsHeader);
	json.insert(QStringLiteral("columnMapping"), mappingToJson(session.columnMapping));
	if (session.editableCsv && !session.csvSource.isEmpty())
		writer.add(QStringLiteral("csvSource"), qCompress(session.csvSource.toUtf8(), 6));

	writer.addArray(QStringLiteral("values"), session.values);
	writer.addArray(QStringLiteral("valid"), toBytes(session.valid));
	writer.addArray(QStringLiteral("markerValues"), session.markerValues);
	writer.addArray(QStringLiteral("markerValid"), toBytes(session.markerValid));

	if (!session.barSource.bars.empty())
	{
		std::vector<double> flat;
		flat.reserve(session.barSource.bars.size() * kBarStride);
		for (const Plot3DBar& bar : session.barSource.bars)
			flat.insert(flat.end(), { bar.x, bar.y, bar.base, bar.height, bar.width, bar.depth, bar.value });
		writer.addArray(QStringLiteral("barSource"), flat);
	}
	if (!session.contourSource.samples.empty())
	{
		std::vector<double> flat;
		flat.reserve(session.contourSource.samples.size() * kSampleStride);
		for (const Plot3DSample& sample : session.contourSource.samples)
			flat.insert(flat.end(), { sample.position.x, sample.position.y, sample.position.z, sample.value });
		writer.addArray(QStringLiteral("contourSource"), flat);
	}

	if (payload.hasGlyphs)
	{
		QJsonObject glyphs;
		glyphs.insert(QStringLiteral("referenceLength"), static_cast<double>(payload.glyphReferenceLength));
		glyphs.insert(QStringLiteral("fieldMinimum"), static_cast<double>(payload.glyphFieldMinimum));
		glyphs.insert(QStringLiteral("fieldMaximum"), static_cast<double>(payload.glyphFieldMaximum));
		json.insert(QStringLiteral("glyphs"), glyphs);
		writer.addArray(QStringLiteral("glyphVectors"), payload.glyphVectors);
		writer.addArray(QStringLiteral("glyphValues"), payload.glyphValues);
	}
	if (payload.hasVolume)
	{
		QJsonObject volume;
		QJsonArray dimensions, origin, voxelSize;
		for (int axis = 0; axis < 3; ++axis)
		{
			dimensions.append(payload.volumeDimensions[axis]);
			origin.append(static_cast<double>(payload.volumeOrigin[axis]));
			voxelSize.append(static_cast<double>(payload.volumeVoxelSize[axis]));
		}
		volume.insert(QStringLiteral("dimensions"), dimensions);
		volume.insert(QStringLiteral("origin"), origin);
		volume.insert(QStringLiteral("voxelSize"), voxelSize);
		volume.insert(QStringLiteral("fieldMinimum"), static_cast<double>(payload.volumeFieldMinimum));
		volume.insert(QStringLiteral("fieldMaximum"), static_cast<double>(payload.volumeFieldMaximum));
		volume.insert(QStringLiteral("label"), payload.volumeLabel);
		json.insert(QStringLiteral("volume"), volume);
		writer.addArray(QStringLiteral("volumeValues"), payload.volumeValues);
	}

	json.insert(QStringLiteral("blobs"), writer.index);
	return json;
}

bool plot3DSessionFromJson(const QJsonObject& json, const std::vector<QByteArray>& blobs, Plot3DSession& session,
	Plot3DRendererPayload& payload, QString* error)
{
	auto fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	session = Plot3DSession();
	payload = Plot3DRendererPayload();

	session.meshUuid = QUuid(json.value(QStringLiteral("meshUuid")).toString());
	if (session.meshUuid.isNull())
		return fail(QObject::tr("A stored 3D Plot has no mesh identity."));
	session.markerMeshUuid = QUuid(json.value(QStringLiteral("markerMeshUuid")).toString());
	session.name = json.value(QStringLiteral("name")).toString();
	session.title = json.value(QStringLiteral("title")).toString();
	const int primitive = json.value(QStringLiteral("primitive")).toInt(-1);
	if (primitive < static_cast<int>(Plot3DPrimitive::Surface) || primitive > static_cast<int>(Plot3DPrimitive::Quiver))
		return fail(QObject::tr("A stored 3D Plot has an unknown plot type."));
	session.primitive = static_cast<Plot3DPrimitive>(primitive);

	const QJsonArray axes = json.value(QStringLiteral("axes")).toArray();
	for (int axis = 0; axis < 3 && axis < axes.size(); ++axis)
		session.axes[static_cast<std::size_t>(axis)] = axisFromJson(axes.at(axis).toObject());
	const QJsonArray minimum = json.value(QStringLiteral("dataMinimum")).toArray();
	const QJsonArray maximum = json.value(QStringLiteral("dataMaximum")).toArray();
	if (minimum.size() != 3 || maximum.size() != 3)
		return fail(QObject::tr("A stored 3D Plot has no data extent."));
	for (int axis = 0; axis < 3; ++axis)
	{
		session.dataMinimum[static_cast<std::size_t>(axis)] = minimum.at(axis).toDouble();
		session.dataMaximum[static_cast<std::size_t>(axis)] = maximum.at(axis).toDouble();
	}

	session.dataMinimumValue = static_cast<float>(json.value(QStringLiteral("dataMinimumValue")).toDouble(0.0));
	session.dataMaximumValue = static_cast<float>(json.value(QStringLiteral("dataMaximumValue")).toDouble(1.0));
	session.colourMinimum = static_cast<float>(json.value(QStringLiteral("colourMinimum")).toDouble(session.dataMinimumValue));
	session.colourMaximum = static_cast<float>(json.value(QStringLiteral("colourMaximum")).toDouble(session.dataMaximumValue));
	session.colormap = json.value(QStringLiteral("colormap")).toInt(0);
	session.bands = json.value(QStringLiteral("bands")).toInt(0);

	session.lineWidth = static_cast<float>(json.value(QStringLiteral("lineWidth")).toDouble(session.lineWidth));
	session.markerSize = static_cast<float>(json.value(QStringLiteral("markerSize")).toDouble(session.markerSize));
	session.arrowScale = static_cast<float>(json.value(QStringLiteral("arrowScale")).toDouble(session.arrowScale));
	session.barWidthScale = static_cast<float>(json.value(QStringLiteral("barWidthScale")).toDouble(session.barWidthScale));
	session.barDepthScale = static_cast<float>(json.value(QStringLiteral("barDepthScale")).toDouble(session.barDepthScale));

	session.isStem = json.value(QStringLiteral("isStem")).toBool(false);
	session.isErrorBars = json.value(QStringLiteral("isErrorBars")).toBool(false);
	session.isFilledScatter = json.value(QStringLiteral("isFilledScatter")).toBool(false);
	session.scatterBaseZ = json.value(QStringLiteral("scatterBaseZ")).toDouble(0.0);
	session.contourLevels = json.value(QStringLiteral("contourLevels")).toInt(session.contourLevels);

	session.axesVisible = json.value(QStringLiteral("axesVisible")).toBool(true);
	const QJsonArray planes = json.value(QStringLiteral("referencePlanes")).toArray();
	for (int plane = 0; plane < 3 && plane < planes.size(); ++plane)
		session.referencePlanes[static_cast<std::size_t>(plane)] = planes.at(plane).toBool();
	session.referencePlaneOpacity = static_cast<float>(
		json.value(QStringLiteral("referencePlaneOpacity")).toDouble(session.referencePlaneOpacity));

	session.editableCsv = json.value(QStringLiteral("editableCsv")).toBool(false);
	const QString delimiter = json.value(QStringLiteral("csvDelimiter")).toString();
	if (!delimiter.isEmpty())
		session.csvOptions.delimiter = delimiter.front();
	session.csvOptions.firstRowIsHeader = json.value(QStringLiteral("csvHeader")).toBool(true);
	session.columnMapping = mappingFromJson(json.value(QStringLiteral("columnMapping")).toObject());

	BlobReader reader{ json.value(QStringLiteral("blobs")).toObject(), blobs, {} };
	auto readFailed = [&]() { return fail(reader.failure); };

	if (reader.index.contains(QStringLiteral("csvSource")))
	{
		const int position = reader.index.value(QStringLiteral("csvSource")).toInt(-1);
		if (position < 0 || position >= static_cast<int>(blobs.size()))
			return fail(QObject::tr("The stored 3D Plot data '%1' is missing or damaged.").arg(QStringLiteral("csvSource")));
		const QByteArray text = qUncompress(blobs[static_cast<std::size_t>(position)]);
		session.csvSource = QString::fromUtf8(text);
		if (session.csvSource.isEmpty())
			session.editableCsv = false; // nothing to edit from; the plot itself is unaffected
	}

	std::vector<unsigned char> valid, markerValid;
	if (!reader.readArray(QStringLiteral("values"), session.values)
		|| !reader.readArray(QStringLiteral("valid"), valid)
		|| !reader.readArray(QStringLiteral("markerValues"), session.markerValues)
		|| !reader.readArray(QStringLiteral("markerValid"), markerValid))
		return readFailed();
	session.valid.assign(valid.begin(), valid.end());
	session.markerValid.assign(markerValid.begin(), markerValid.end());
	if (session.valid.size() != session.values.size() || session.markerValid.size() != session.markerValues.size())
		return fail(QObject::tr("The stored 3D Plot colour data is inconsistent."));

	std::vector<double> flat;
	if (!reader.readArray(QStringLiteral("barSource"), flat))
		return readFailed();
	if (flat.size() % kBarStride != 0)
		return fail(QObject::tr("The stored 3D Plot bar data is inconsistent."));
	for (std::size_t i = 0; i < flat.size(); i += kBarStride)
	{
		Plot3DBar bar;
		bar.x = flat[i]; bar.y = flat[i + 1]; bar.base = flat[i + 2]; bar.height = flat[i + 3];
		bar.width = flat[i + 4]; bar.depth = flat[i + 5]; bar.value = flat[i + 6];
		session.barSource.bars.push_back(bar);
	}
	if (!reader.readArray(QStringLiteral("contourSource"), flat))
		return readFailed();
	if (flat.size() % kSampleStride != 0)
		return fail(QObject::tr("The stored 3D Plot contour data is inconsistent."));
	for (std::size_t i = 0; i < flat.size(); i += kSampleStride)
	{
		Plot3DSample sample;
		sample.position = { flat[i], flat[i + 1], flat[i + 2] };
		sample.value = flat[i + 3];
		session.contourSource.samples.push_back(sample);
	}

	if (json.contains(QStringLiteral("glyphs")))
	{
		const QJsonObject glyphs = json.value(QStringLiteral("glyphs")).toObject();
		payload.hasGlyphs = true;
		payload.glyphReferenceLength = static_cast<float>(glyphs.value(QStringLiteral("referenceLength")).toDouble(0.0));
		payload.glyphFieldMinimum = static_cast<float>(glyphs.value(QStringLiteral("fieldMinimum")).toDouble(0.0));
		payload.glyphFieldMaximum = static_cast<float>(glyphs.value(QStringLiteral("fieldMaximum")).toDouble(1.0));
		if (!reader.readArray(QStringLiteral("glyphVectors"), payload.glyphVectors)
			|| !reader.readArray(QStringLiteral("glyphValues"), payload.glyphValues))
			return readFailed();
		if (payload.glyphVectors.size() != payload.glyphValues.size() * 3 || payload.glyphValues.empty())
			return fail(QObject::tr("The stored 3D Plot arrow data is inconsistent."));
	}
	if (json.contains(QStringLiteral("volume")))
	{
		const QJsonObject volume = json.value(QStringLiteral("volume")).toObject();
		const QJsonArray dimensions = volume.value(QStringLiteral("dimensions")).toArray();
		const QJsonArray origin = volume.value(QStringLiteral("origin")).toArray();
		const QJsonArray voxelSize = volume.value(QStringLiteral("voxelSize")).toArray();
		if (dimensions.size() != 3 || origin.size() != 3 || voxelSize.size() != 3)
			return fail(QObject::tr("The stored 3D Plot volume is inconsistent."));
		payload.hasVolume = true;
		for (int axis = 0; axis < 3; ++axis)
		{
			payload.volumeDimensions[axis] = dimensions.at(axis).toInt(0);
			payload.volumeOrigin[axis] = static_cast<float>(origin.at(axis).toDouble(0.0));
			payload.volumeVoxelSize[axis] = static_cast<float>(voxelSize.at(axis).toDouble(1.0));
		}
		payload.volumeFieldMinimum = static_cast<float>(volume.value(QStringLiteral("fieldMinimum")).toDouble(0.0));
		payload.volumeFieldMaximum = static_cast<float>(volume.value(QStringLiteral("fieldMaximum")).toDouble(1.0));
		payload.volumeLabel = volume.value(QStringLiteral("label")).toString();
		if (!reader.readArray(QStringLiteral("volumeValues"), payload.volumeValues))
			return readFailed();
		const long long expected = 1LL * payload.volumeDimensions[0] * payload.volumeDimensions[1] * payload.volumeDimensions[2];
		if (expected <= 0 || static_cast<long long>(payload.volumeValues.size()) != expected)
			return fail(QObject::tr("The stored 3D Plot volume is inconsistent."));
	}
	return true;
}
