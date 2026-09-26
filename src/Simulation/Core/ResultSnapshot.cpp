#include "ResultSnapshot.h"

#include "ResultDerivedFields.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	constexpr int kFormatVersion = 1;     // node fields only
	constexpr int kFormatVersionCells = 2; // also cell fields (per-triangle arrays, "association" keys, cellIds)
	constexpr int kFormatVersionVolume = 3; // also the volume (nodes, cells, full-length fields, the surface mapping): see SnapshotOptions::includeVolume
	constexpr int kMinCompressBytes = 512; // smaller blobs are not worth a deflate header

	// ---- Blob codec -----------------------------------------------------------------------------------------------

	// Returns the bytes to store and fills `desc` (element count, element size, encoding). Compresses when asked to
	// and when that actually shrinks the blob.
	QByteArray encodeBlob(const QByteArray& raw, int elementSize, bool compress, QJsonObject& desc)
	{
		desc.insert(QStringLiteral("n"), static_cast<int>(raw.size() / elementSize));
		desc.insert(QStringLiteral("elem"), elementSize);
		if (compress && raw.size() >= kMinCompressBytes)
		{
			const QByteArray packed = qCompress(shuffleBytes(raw, elementSize), 6);
			if (packed.size() < raw.size())
			{
				desc.insert(QStringLiteral("enc"), QStringLiteral("shuffle-zlib"));
				return packed;
			}
		}
		desc.insert(QStringLiteral("enc"), QStringLiteral("raw"));
		return raw;
	}

	bool decodeBlob(const QJsonObject& desc, const QByteArray& stored, QByteArray& raw, QString* error)
	{
		const int elementSize = desc.value(QStringLiteral("elem")).toInt();
		const qint64 count = desc.value(QStringLiteral("n")).toInt(-1);
		if ((elementSize != 4 && elementSize != 8) || count < 0)
		{
			if (error) *error = QStringLiteral("a stored result array is malformed");
			return false;
		}
		const QString encoding = desc.value(QStringLiteral("enc")).toString();
		if (encoding == QLatin1String("raw"))
			raw = stored;
		else if (encoding == QLatin1String("shuffle-zlib"))
		{
			const QByteArray shuffled = qUncompress(stored);
			if (shuffled.isEmpty() && count > 0)
			{
				if (error) *error = QStringLiteral("a stored result array is corrupt (cannot be decompressed)");
				return false;
			}
			raw = unshuffleBytes(shuffled, elementSize);
		}
		else
		{
			if (error) *error = QStringLiteral("a stored result array uses an unknown encoding '%1'").arg(encoding);
			return false;
		}
		if (raw.size() != count * elementSize)
		{
			if (error) *error = QStringLiteral("a stored result array has the wrong size");
			return false;
		}
		return true;
	}

	QByteArray floatsToBytes(const std::vector<float>& values)
	{
		return QByteArray(reinterpret_cast<const char*>(values.data()), static_cast<qsizetype>(values.size() * sizeof(float)));
	}

	std::vector<float> bytesToFloats(const QByteArray& bytes)
	{
		std::vector<float> values(static_cast<std::size_t>(bytes.size()) / sizeof(float));
		if (!values.empty())
			std::memcpy(values.data(), bytes.constData(), values.size() * sizeof(float));
		return values;
	}

	// ---- What gets stored -----------------------------------------------------------------------------------------

	// Source (non-derived) node fields that carry data, chosen by the options.
	std::vector<int> selectSourceFields(const ResultDataset& dataset, const SnapshotOptions& options)
	{
		std::vector<int> chosen;
		auto usable = [&](int index) {
			if (index < 0 || static_cast<std::size_t>(index) >= dataset.fields.size())
				return false;
			const ResultField& f = dataset.fields[static_cast<std::size_t>(index)];
			return f.derivedFromField < 0 && f.components > 0; // node and cell fields
		};
		auto add = [&](int index) {
			if (usable(index) && std::find(chosen.begin(), chosen.end(), index) == chosen.end())
				chosen.push_back(index);
		};
		if (options.content == SnapshotOptions::Content::AllFields)
		{
			for (std::size_t i = 0; i < dataset.fields.size(); ++i)
				add(static_cast<int>(i));
			return chosen;
		}
		auto sourceOf = [&](int index) {
			if (index >= 0 && static_cast<std::size_t>(index) < dataset.fields.size() && dataset.fields[static_cast<std::size_t>(index)].derivedFromField >= 0)
				return dataset.fields[static_cast<std::size_t>(index)].derivedFromField; // a derived field is rebuilt from its source
			return index;
		};
		add(sourceOf(options.shownField));
		add(findDisplacementField(dataset));
		for (int extra : options.extraFields)
			add(sourceOf(extra));
		std::sort(chosen.begin(), chosen.end());
		return chosen;
	}

	bool isCellField(const ResultField& field) { return field.association == ResultFieldAssociation::Cell; }

	// Whether there is a volume to store: volume cells (or polyhedra), not just a shell or surface mesh.
	bool hasVolumeCells(const ResultDataset& dataset)
	{
		for (ResultCellType type : dataset.cellTypes)
			if (resultCellIsVolume(type) || type == ResultCellType::Polyhedron)
				return true;
		return false;
	}

	// The description of a field (everything but its values), shared by the surface fields and the volume fields.
	QJsonObject fieldMeta(const ResultField& f)
	{
		QJsonObject o;
		o.insert(QStringLiteral("name"), f.name);
		o.insert(QStringLiteral("association"), isCellField(f) ? QStringLiteral("cell") : QStringLiteral("node"));
		o.insert(QStringLiteral("components"), f.components);
		QJsonArray names;
		for (const QString& n : f.componentNames)
			names.append(n);
		o.insert(QStringLiteral("componentNames"), names);
		o.insert(QStringLiteral("kind"), f.quantityKind);
		o.insert(QStringLiteral("fileUnit"), f.fileUnit);
		o.insert(QStringLiteral("displayUnit"), f.displayUnit);
		o.insert(QStringLiteral("unitConfirmed"), f.unitConfirmed);
		return o;
	}

	void applyFieldMeta(const QJsonObject& o, ResultField& f)
	{
		f.name = o.value(QStringLiteral("name")).toString();
		f.association = o.value(QStringLiteral("association")).toString() == QLatin1String("cell") ? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
		f.components = o.value(QStringLiteral("components")).toInt(1);
		for (const QJsonValue& n : o.value(QStringLiteral("componentNames")).toArray())
			f.componentNames.push_back(n.toString());
		f.quantityKind = o.value(QStringLiteral("kind")).toString();
		f.fileUnit = o.value(QStringLiteral("fileUnit")).toString();
		f.displayUnit = o.value(QStringLiteral("displayUnit")).toString();
		f.unitConfirmed = o.value(QStringLiteral("unitConfirmed")).toBool();
	}

	QByteArray u32Bytes(const std::vector<std::uint32_t>& values)
	{
		return QByteArray(reinterpret_cast<const char*>(values.data()), static_cast<qsizetype>(values.size() * sizeof(std::uint32_t)));
	}

	QByteArray i64Bytes(const std::vector<std::int64_t>& values)
	{
		return QByteArray(reinterpret_cast<const char*>(values.data()), static_cast<qsizetype>(values.size() * sizeof(std::int64_t)));
	}

	// Size in bytes of the volume part of a snapshot (uncompressed): the geometry and the given fields at every node / cell, for the kept steps.
	std::uint64_t volumeRawBytes(const ResultDataset& dataset, const ResultBoundarySurface& surface, const std::vector<int>& fields, const std::vector<int>& steps)
	{
		std::uint64_t bytes = dataset.nodePositions.size() * 4 + dataset.nodeIds.size() * 8 + dataset.cellTypes.size() * 4 + dataset.cellOffsets.size() * 4
			+ dataset.cellConnectivity.size() * 4 + dataset.cellIds.size() * 8 + dataset.faceNodes.size() * 4 + dataset.faceOffsets.size() * 4
			+ dataset.cellFaces.size() * 4 + dataset.cellFaceOffsets.size() * 4 + surface.vertexNode.size() * 4 + surface.triangleCell.size() * 4
			+ surface.triangleFace.size() * 4;
		for (int f : fields)
			for (int s : steps)
			{
				const ResultField& field = dataset.fields[static_cast<std::size_t>(f)];
				if (static_cast<std::size_t>(s) < field.stepData.size())
					bytes += field.stepData[static_cast<std::size_t>(s)].size() * 4;
			}
		return bytes;
	}

	// Values of one field at one step for every surface vertex - or, for a cell field, every surface triangle -
	// (components interleaved); empty when the step has none.
	std::vector<float> gatherSurfaceValues(const ResultDataset& dataset, const ResultBoundarySurface& surface, const ResultField& field, std::size_t step)
	{
		std::vector<float> out;
		if (step >= field.stepData.size() || field.stepData[step].empty())
			return out;
		const std::vector<float>& data = field.stepData[step];
		const std::size_t comps = static_cast<std::size_t>(field.components);
		if (isCellField(field))
		{
			if (data.size() != dataset.cellCount() * comps || surface.triangleCell.size() != surface.triangleCount())
				return out;
			out.assign(surface.triangleCount() * comps, std::numeric_limits<float>::quiet_NaN());
			for (std::size_t t = 0; t < surface.triangleCount(); ++t)
			{
				const std::size_t cell = surface.triangleCell[t];
				if (cell >= dataset.cellCount())
					continue;
				for (std::size_t c = 0; c < comps; ++c)
					out[t * comps + c] = data[cell * comps + c];
			}
			return out;
		}
		if (data.size() != dataset.nodeCount() * comps)
			return out;
		out.assign(surface.vertexCount() * comps, std::numeric_limits<float>::quiet_NaN());
		for (std::size_t v = 0; v < surface.vertexNode.size() && v < surface.vertexCount(); ++v)
		{
			const std::size_t node = surface.vertexNode[v];
			if (node >= dataset.nodeCount())
				continue;
			for (std::size_t c = 0; c < comps; ++c)
				out[v * comps + c] = data[node * comps + c];
		}
		return out;
	}

	// Min/max of the values one range selector sees at a step, over ALL solver nodes (file units). NaN when none.
	void selectorRange(const ResultField& field, std::size_t step, int selector, float& lo, float& hi)
	{
		lo = std::numeric_limits<float>::quiet_NaN();
		hi = lo;
		if (step >= field.stepData.size() || field.stepData[step].empty())
			return;
		const std::vector<float>& data = field.stepData[step];
		const std::size_t comps = static_cast<std::size_t>(field.components);
		bool any = false;
		float a = 0.0f, b = 0.0f;
		for (std::size_t n = 0; n + comps <= data.size(); n += comps)
		{
			float v;
			if (comps == 1)
				v = data[n];
			else if (selector < static_cast<int>(comps))
				v = data[n + static_cast<std::size_t>(selector)];
			else
				v = std::sqrt(data[n] * data[n] + data[n + 1] * data[n + 1] + data[n + 2] * data[n + 2]); // magnitude (3 comps)
			if (!std::isfinite(v))
				continue;
			if (!any)
			{
				a = b = v;
				any = true;
			}
			else
			{
				a = std::min(a, v);
				b = std::max(b, v);
			}
		}
		if (any)
		{
			lo = a;
			hi = b;
		}
	}

	QJsonObject viewToJson(const ResultDataset& dataset, const SimulationViewState& s, int keptStep)
	{
		QJsonObject o;
		if (s.fieldIndex >= 0 && static_cast<std::size_t>(s.fieldIndex) < dataset.fields.size())
		{
			o.insert(QStringLiteral("field"), dataset.fields[static_cast<std::size_t>(s.fieldIndex)].name);
			o.insert(QStringLiteral("fieldAssociation"), isCellField(dataset.fields[static_cast<std::size_t>(s.fieldIndex)]) ? QStringLiteral("cell") : QStringLiteral("node"));
		}
		o.insert(QStringLiteral("component"), s.component);
		o.insert(QStringLiteral("customRange"), s.customRange);
		o.insert(QStringLiteral("rangeMin"), s.rangeMin);
		o.insert(QStringLiteral("rangeMax"), s.rangeMax);
		o.insert(QStringLiteral("colormap"), s.colormap);
		o.insert(QStringLiteral("bands"), s.bands);
		o.insert(QStringLiteral("step"), keptStep);
		o.insert(QStringLiteral("allStepsRange"), s.allStepsRange);
		o.insert(QStringLiteral("deform"), s.deform);
		o.insert(QStringLiteral("deformScale"), s.deformScale);
		o.insert(QStringLiteral("markExtrema"), s.markExtrema);
		o.insert(QStringLiteral("sectionFill"), s.sectionFill);
		o.insert(QStringLiteral("iso"), s.iso);
		o.insert(QStringLiteral("isoLevels"), s.isoLevels);
		if (s.isoField >= 0 && static_cast<std::size_t>(s.isoField) < dataset.fields.size())
		{
			o.insert(QStringLiteral("isoFieldName"), dataset.fields[static_cast<std::size_t>(s.isoField)].name);
			o.insert(QStringLiteral("isoFieldAssociation"), isCellField(dataset.fields[static_cast<std::size_t>(s.isoField)]) ? QStringLiteral("cell") : QStringLiteral("node"));
		}
		o.insert(QStringLiteral("streamlines"), s.streamlines);
		o.insert(QStringLiteral("streamSeeds"), s.streamSeeds);
		o.insert(QStringLiteral("streamOnPlane"), s.streamOnPlane);
		if (s.streamField >= 0 && static_cast<std::size_t>(s.streamField) < dataset.fields.size())
			o.insert(QStringLiteral("streamFieldName"), dataset.fields[static_cast<std::size_t>(s.streamField)].name);
		o.insert(QStringLiteral("glyphs"), s.glyphs);
		o.insert(QStringLiteral("glyphScale"), s.glyphScale);
		o.insert(QStringLiteral("glyphCount"), s.glyphCount);
		o.insert(QStringLiteral("glyphScaleByMagnitude"), s.glyphScaleByMagnitude);
		if (s.glyphField >= 0 && static_cast<std::size_t>(s.glyphField) < dataset.fields.size())
		{
			o.insert(QStringLiteral("glyphFieldName"), dataset.fields[static_cast<std::size_t>(s.glyphField)].name);
			o.insert(QStringLiteral("glyphFieldAssociation"), isCellField(dataset.fields[static_cast<std::size_t>(s.glyphField)]) ? QStringLiteral("cell") : QStringLiteral("node"));
		}
		return o;
	}

	QJsonValue floatToJson(float v)
	{
		return std::isfinite(v) ? QJsonValue(static_cast<double>(v)) : QJsonValue(QJsonValue::Null);
	}

	float jsonToFloat(const QJsonValue& v)
	{
		return v.isDouble() ? static_cast<float>(v.toDouble()) : std::numeric_limits<float>::quiet_NaN();
	}

	QString unavailable(const QString& what)
	{
		return QStringLiteral("The stored result cannot be restored: %1.").arg(what);
	}
}

QByteArray shuffleBytes(const QByteArray& raw, int elementSize)
{
	if (elementSize <= 1 || raw.size() % elementSize != 0)
		return raw;
	const qsizetype count = raw.size() / elementSize;
	QByteArray out(raw.size(), Qt::Uninitialized);
	const char* src = raw.constData();
	char* dst = out.data();
	for (qsizetype i = 0; i < count; ++i)
		for (int b = 0; b < elementSize; ++b)
			dst[static_cast<qsizetype>(b) * count + i] = src[i * elementSize + b];
	return out;
}

QByteArray unshuffleBytes(const QByteArray& shuffled, int elementSize)
{
	if (elementSize <= 1 || shuffled.size() % elementSize != 0)
		return shuffled;
	const qsizetype count = shuffled.size() / elementSize;
	QByteArray out(shuffled.size(), Qt::Uninitialized);
	const char* src = shuffled.constData();
	char* dst = out.data();
	for (qsizetype i = 0; i < count; ++i)
		for (int b = 0; b < elementSize; ++b)
			dst[i * elementSize + b] = src[static_cast<qsizetype>(b) * count + i];
	return out;
}

std::vector<int> snapshotStepIndices(int stepCount, int maxSteps)
{
	std::vector<int> kept;
	if (stepCount <= 0)
		return kept;
	if (maxSteps < 2)
		maxSteps = 2;
	if (stepCount <= maxSteps)
	{
		for (int i = 0; i < stepCount; ++i)
			kept.push_back(i);
		return kept;
	}
	for (int k = 0; k < maxSteps; ++k)
		kept.push_back(static_cast<int>(std::llround(static_cast<double>(k) * (stepCount - 1) / (maxSteps - 1))));
	kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
	return kept;
}

SnapshotSize estimateSnapshotSize(const ResultDataset& dataset, const ResultBoundarySurface& surface, const SnapshotOptions& options)
{
	SnapshotSize size;
	const std::vector<int> fields = selectSourceFields(dataset, options);
	const std::vector<int> steps = snapshotStepIndices(static_cast<int>(dataset.stepCount()), options.maxSteps);
	const std::uint64_t vertices = surface.vertexCount();
	size.rawBytes = vertices * 3 * 4 + vertices * 8; // rest positions + node ids
	std::vector<std::pair<int, int>> blobs;           // (field, step) that carry data
	bool anyCell = false;
	for (int f : fields)
		for (int s : steps)
		{
			const ResultField& field = dataset.fields[static_cast<std::size_t>(f)];
			if (static_cast<std::size_t>(s) < field.stepData.size() && !field.stepData[static_cast<std::size_t>(s)].empty())
			{
				const std::uint64_t tuples = isCellField(field) ? surface.triangleCount() : vertices;
				anyCell = anyCell || isCellField(field);
				size.rawBytes += tuples * static_cast<std::uint64_t>(field.components) * 4;
				blobs.emplace_back(f, s);
			}
		}
	if (anyCell)
		size.rawBytes += static_cast<std::uint64_t>(surface.triangleCount()) * 8; // cell ids
	// The volume adds its own geometry and the fields at every node and cell: compress like the surface data does, by its share.
	const std::uint64_t surfaceRaw = size.rawBytes;
	std::uint64_t volumeRaw = 0;
	if (options.includeVolume && hasVolumeCells(dataset))
		volumeRaw = volumeRawBytes(dataset, surface, fields, steps);
	size.rawBytes += volumeRaw;
	size.storedBytes = size.rawBytes;
	if (!options.compress || blobs.empty())
		return size;

	// Compress up to three evenly spaced blobs (capped in length) and apply their ratio to everything.
	std::uint64_t sampleRaw = 0, sampleStored = 0;
	const std::size_t picks = std::min<std::size_t>(3, blobs.size());
	for (std::size_t k = 0; k < picks; ++k)
	{
		const auto& pick = blobs[picks == 1 ? 0 : k * (blobs.size() - 1) / (picks - 1)];
		std::vector<float> values = gatherSurfaceValues(dataset, surface, dataset.fields[static_cast<std::size_t>(pick.first)],
		                                                static_cast<std::size_t>(pick.second));
		if (values.size() > 262144)
			values.resize(262144);
		QJsonObject scratch;
		const QByteArray raw = floatsToBytes(values);
		sampleRaw += static_cast<std::uint64_t>(raw.size());
		sampleStored += static_cast<std::uint64_t>(encodeBlob(raw, 4, true, scratch).size());
	}
	if (sampleRaw > 0)
	{
		const double ratio = static_cast<double>(sampleStored) / static_cast<double>(sampleRaw);
		// Connectivity and node ids compress better than field data and the sample only saw fields: a plain estimate, the volume at the same ratio.
		size.storedBytes = static_cast<std::uint64_t>(static_cast<double>(surfaceRaw) * ratio) + static_cast<std::uint64_t>(static_cast<double>(volumeRaw) * ratio);
	}
	return size;
}

bool encodeResultSnapshot(const ResultDataset& dataset, const ResultBoundarySurface& surface, const SimulationViewState& state,
                          const SnapshotOptions& options, ResultSnapshot& out, QString* error, const SnapshotOverlays* overlays)
{
	out = ResultSnapshot();
	if (surface.vertexCount() == 0 || surface.vertexNode.size() != surface.vertexCount())
	{
		if (error) *error = QStringLiteral("The result has no displayable surface to store.");
		return false;
	}

	QJsonArray blobDescs;
	auto addBlob = [&](const QByteArray& raw, int elementSize) {
		QJsonObject desc;
		const QByteArray stored = encodeBlob(raw, elementSize, options.compress, desc);
		const int index = static_cast<int>(out.blobs.size());
		desc.insert(QStringLiteral("i"), index);
		blobDescs.append(desc);
		out.blobs.push_back(stored);
		out.size.rawBytes += static_cast<std::uint64_t>(raw.size());
		out.size.storedBytes += static_cast<std::uint64_t>(stored.size());
		return index;
	};

	const std::vector<int> chosen = selectSourceFields(dataset, options);
	bool hasCellField = false;
	for (int fi : chosen)
		hasCellField = hasCellField || isCellField(dataset.fields[static_cast<std::size_t>(fi)]);

	QJsonObject root;
	root.insert(QStringLiteral("version"), hasCellField ? kFormatVersionCells : kFormatVersion);
	root.insert(QStringLiteral("vertexCount"), static_cast<int>(surface.vertexCount()));
	root.insert(QStringLiteral("triangleCount"), static_cast<int>(surface.triangleCount()));
	root.insert(QStringLiteral("solver"), dataset.solverName);
	root.insert(QStringLiteral("lengthUnit"), dataset.lengthUnit);
	root.insert(QStringLiteral("sourcePath"), dataset.sourcePath);

	// Rest positions and the file's own node ids, per surface vertex.
	root.insert(QStringLiteral("restPositions"), addBlob(floatsToBytes(surface.positions), 4));
	std::vector<std::int64_t> ids(surface.vertexCount());
	for (std::size_t v = 0; v < ids.size(); ++v)
		ids[v] = dataset.nodeId(surface.vertexNode[v]);
	root.insert(QStringLiteral("nodeIds"), addBlob(QByteArray(reinterpret_cast<const char*>(ids.data()), static_cast<qsizetype>(ids.size() * sizeof(std::int64_t))), 8));

	// Ids of the cells the surface triangles belong to (only needed when a cell field is stored).
	if (hasCellField)
	{
		std::vector<std::int64_t> cellIds(surface.triangleCount());
		for (std::size_t t = 0; t < cellIds.size(); ++t)
			cellIds[t] = t < surface.triangleCell.size() ? dataset.cellId(surface.triangleCell[t]) : static_cast<std::int64_t>(t);
		root.insert(QStringLiteral("cellIds"), addBlob(QByteArray(reinterpret_cast<const char*>(cellIds.data()), static_cast<qsizetype>(cellIds.size() * sizeof(std::int64_t))), 8));
	}

	// Steps (possibly subsampled).
	const std::vector<int> kept = snapshotStepIndices(static_cast<int>(dataset.stepCount()), options.maxSteps);
	QJsonArray stepsJson;
	for (int src : kept)
	{
		const ResultStep& s = dataset.steps[static_cast<std::size_t>(src)];
		QJsonObject o;
		o.insert(QStringLiteral("time"), s.time);
		o.insert(QStringLiteral("label"), s.label);
		o.insert(QStringLiteral("timeUnit"), s.timeUnit);
		o.insert(QStringLiteral("src"), src);
		stepsJson.append(o);
	}
	root.insert(QStringLiteral("steps"), stepsJson);
	if (static_cast<int>(kept.size()) < static_cast<int>(dataset.stepCount()))
		out.notes << QStringLiteral("%1 of %2 time steps are stored (evenly spaced, first and last included).")
			.arg(kept.size()).arg(dataset.stepCount());

	// Source fields with their per-step blobs.
	QJsonArray fieldsJson;
	std::vector<bool> stored(dataset.fields.size(), false);
	for (int fi : chosen)
	{
		const ResultField& f = dataset.fields[static_cast<std::size_t>(fi)];
		stored[static_cast<std::size_t>(fi)] = true;
		QJsonObject o = fieldMeta(f);
		QJsonArray stepBlobs;
		for (int src : kept)
		{
			const std::vector<float> values = gatherSurfaceValues(dataset, surface, f, static_cast<std::size_t>(src));
			stepBlobs.append(values.empty() ? -1 : addBlob(floatsToBytes(values), 4));
		}
		o.insert(QStringLiteral("stepData"), stepBlobs);
		fieldsJson.append(o);
	}
	root.insert(QStringLiteral("fields"), fieldsJson);

	// Ranges over ALL solver nodes for every stored field and every derived field built from one, so the legend and
	// the all-steps range read the same as with the full result even though interior nodes are not stored.
	QJsonArray rangesJson;
	for (std::size_t fi = 0; fi < dataset.fields.size(); ++fi)
	{
		const ResultField& f = dataset.fields[fi];
		const bool derivedOfStored = f.derivedFromField >= 0 && static_cast<std::size_t>(f.derivedFromField) < stored.size()
			&& stored[static_cast<std::size_t>(f.derivedFromField)];
		if (!stored[fi] && !derivedOfStored)
			continue;
		const int selectors = resultRangeSelectorCount(f.components);
		QJsonArray data;
		for (int src : kept)
			for (int sel = 0; sel < selectors; ++sel)
			{
				float lo, hi;
				selectorRange(f, static_cast<std::size_t>(src), sel, lo, hi);
				data.append(floatToJson(lo));
				data.append(floatToJson(hi));
			}
		QJsonObject o;
		o.insert(QStringLiteral("name"), f.name);
		o.insert(QStringLiteral("association"), isCellField(f) ? QStringLiteral("cell") : QStringLiteral("node"));
		o.insert(QStringLiteral("data"), data);
		rangesJson.append(o);
	}
	root.insert(QStringLiteral("ranges"), rangesJson);

	// The volume (opt-in): the whole dataset, so a restored result can be cut and traced again. The surface snapshot above is still stored - it is what
	// the mesh in the file corresponds to - and the mapping says which node / cell each surface vertex / triangle is.
	if (options.includeVolume && hasVolumeCells(dataset) && dataset.nodeCount() > 0 && surface.triangleCell.size() == surface.triangleCount()
	    && surface.triangleFace.size() == surface.triangleCount())
	{
		QJsonObject volume;
		volume.insert(QStringLiteral("nodePositions"), addBlob(floatsToBytes(dataset.nodePositions), 4));
		if (!dataset.nodeIds.empty())
			volume.insert(QStringLiteral("nodeIds"), addBlob(i64Bytes(dataset.nodeIds), 8));
		std::vector<std::uint32_t> types(dataset.cellTypes.size());
		for (std::size_t i = 0; i < types.size(); ++i)
			types[i] = static_cast<std::uint32_t>(dataset.cellTypes[i]);
		volume.insert(QStringLiteral("cellTypes"), addBlob(u32Bytes(types), 4));
		volume.insert(QStringLiteral("cellOffsets"), addBlob(u32Bytes(dataset.cellOffsets), 4));
		volume.insert(QStringLiteral("cellConnectivity"), addBlob(u32Bytes(dataset.cellConnectivity), 4));
		if (!dataset.cellIds.empty())
			volume.insert(QStringLiteral("cellIds"), addBlob(i64Bytes(dataset.cellIds), 8));
		if (!dataset.faceOffsets.empty())
		{
			volume.insert(QStringLiteral("faceNodes"), addBlob(u32Bytes(dataset.faceNodes), 4));
			volume.insert(QStringLiteral("faceOffsets"), addBlob(u32Bytes(dataset.faceOffsets), 4));
			volume.insert(QStringLiteral("cellFaces"), addBlob(u32Bytes(dataset.cellFaces), 4));
			volume.insert(QStringLiteral("cellFaceOffsets"), addBlob(u32Bytes(dataset.cellFaceOffsets), 4));
		}
		volume.insert(QStringLiteral("vertexNode"), addBlob(u32Bytes(surface.vertexNode), 4));
		volume.insert(QStringLiteral("triangleCell"), addBlob(u32Bytes(surface.triangleCell), 4));
		const std::vector<std::uint32_t> faceMarkers(surface.triangleFace.begin(), surface.triangleFace.end()); // stored as 32-bit like the other index arrays
		volume.insert(QStringLiteral("triangleFace"), addBlob(u32Bytes(faceMarkers), 4));
		QJsonArray volumeFields;
		for (int fi : chosen)
		{
			const ResultField& f = dataset.fields[static_cast<std::size_t>(fi)];
			QJsonObject o = fieldMeta(f);
			QJsonArray stepBlobs;
			for (int src : kept)
			{
				const std::size_t step = static_cast<std::size_t>(src);
				stepBlobs.append(step < f.stepData.size() && !f.stepData[step].empty() ? addBlob(floatsToBytes(f.stepData[step]), 4) : -1);
			}
			o.insert(QStringLiteral("stepData"), stepBlobs);
			volumeFields.append(o);
		}
		volume.insert(QStringLiteral("fields"), volumeFields);
		root.insert(QStringLiteral("volume"), volume);
		root.insert(QStringLiteral("version"), kFormatVersionVolume);
	}

	// The cut faces, iso-surfaces and streamlines on display, as they are (used when the volume is not stored).
	if (overlays && !overlays->empty())
	{
		QJsonObject o;
		QJsonArray slices;
		for (const SliceDisplay& d : overlays->slices)
		{
			if (d.triangles.empty())
				continue;
			QJsonObject e;
			e.insert(QStringLiteral("lit"), d.lit);
			e.insert(QStringLiteral("positions"), addBlob(floatsToBytes(d.positions), 4));
			e.insert(QStringLiteral("colors"), addBlob(floatsToBytes(d.colors), 4));
			e.insert(QStringLiteral("triangles"), addBlob(u32Bytes(d.triangles), 4));
			slices.append(e);
		}
		o.insert(QStringLiteral("slices"), slices);
		if (overlays->streamlines.segmentCount() > 0)
		{
			QJsonObject e;
			e.insert(QStringLiteral("positions"), addBlob(floatsToBytes(overlays->streamlines.positions), 4));
			e.insert(QStringLiteral("colors"), addBlob(floatsToBytes(overlays->streamlines.colors), 4));
			e.insert(QStringLiteral("segments"), addBlob(u32Bytes(overlays->streamlines.segments), 4));
			o.insert(QStringLiteral("streamlines"), e);
		}
		QJsonArray cuts;
		for (const OverlayClipCut& c : overlays->cuts)
		{
			QJsonObject e;
			e.insert(QStringLiteral("axis"), c.axis);
			e.insert(QStringLiteral("position"), c.position);
			e.insert(QStringLiteral("keepPositive"), c.keepPositive);
			cuts.append(e);
		}
		o.insert(QStringLiteral("cuts"), cuts);
		root.insert(QStringLiteral("overlays"), o);
	}

	// View state; the saved step is mapped to the nearest kept one.
	int keptStep = 0; // stays 0 for a result without steps (a mesh with no fields), where kept is empty
	for (std::size_t k = 1; k < kept.size(); ++k)
		if (std::abs(kept[k] - state.step) < std::abs(kept[static_cast<std::size_t>(keptStep)] - state.step))
			keptStep = static_cast<int>(k);
	root.insert(QStringLiteral("view"), viewToJson(dataset, state, keptStep));

	root.insert(QStringLiteral("blobs"), blobDescs);
	out.json = root;
	out.size.estimated = false;
	return true;
}

bool decodeResultSnapshot(const QJsonObject& json, const std::vector<QByteArray>& blobs, std::size_t vertexCount,
                          const std::vector<std::uint32_t>& triangles, DecodedSnapshot& out, QString* error)
{
	out = DecodedSnapshot();
	auto fail = [&](const QString& why) {
		if (error) *error = unavailable(why);
		return false;
	};
	const int version = json.value(QStringLiteral("version")).toInt();
	if (version != kFormatVersion && version != kFormatVersionCells && version != kFormatVersionVolume)
		return fail(QStringLiteral("it was written by a newer version of ModelViewer"));
	if (static_cast<std::size_t>(json.value(QStringLiteral("vertexCount")).toInt(-1)) != vertexCount
		|| static_cast<std::size_t>(json.value(QStringLiteral("triangleCount")).toInt(-1)) * 3 != triangles.size())
		return fail(QStringLiteral("the mesh was changed after the result was saved (different vertex or triangle count)"));

	const QJsonArray blobDescs = json.value(QStringLiteral("blobs")).toArray();
	auto blobBytes = [&](int index, QByteArray& raw) {
		if (index < 0 || static_cast<std::size_t>(index) >= blobs.size() || index >= blobDescs.size())
			return false;
		QString why;
		if (!decodeBlob(blobDescs[index].toObject(), blobs[static_cast<std::size_t>(index)], raw, &why))
		{
			if (error) *error = unavailable(why);
			return false;
		}
		return true;
	};
	auto blobFloats = [&](int index, std::size_t expected, std::vector<float>& values) {
		QByteArray raw;
		if (!blobBytes(index, raw))
			return false;
		values = bytesToFloats(raw);
		return values.size() == expected;
	};

	auto dataset = std::make_shared<ResultDataset>();
	dataset->solverName = json.value(QStringLiteral("solver")).toString();
	dataset->lengthUnit = json.value(QStringLiteral("lengthUnit")).toString();
	dataset->sourcePath = json.value(QStringLiteral("sourcePath")).toString();
	out.sourcePath = dataset->sourcePath;

	if (!blobFloats(json.value(QStringLiteral("restPositions")).toInt(-1), vertexCount * 3, out.restPositions))
		return error && !error->isEmpty() ? false : fail(QStringLiteral("the stored vertex positions are missing"));
	dataset->nodePositions = out.restPositions;
	{
		QByteArray raw;
		if (!blobBytes(json.value(QStringLiteral("nodeIds")).toInt(-1), raw) || static_cast<std::size_t>(raw.size()) != vertexCount * sizeof(std::int64_t))
			return error && !error->isEmpty() ? false : fail(QStringLiteral("the stored node ids are missing"));
		dataset->nodeIds.resize(vertexCount);
		std::memcpy(dataset->nodeIds.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
	}

	if (json.contains(QStringLiteral("cellIds")))
	{
		QByteArray raw;
		if (!blobBytes(json.value(QStringLiteral("cellIds")).toInt(-1), raw) || static_cast<std::size_t>(raw.size()) != (triangles.size() / 3) * sizeof(std::int64_t))
			return error && !error->isEmpty() ? false : fail(QStringLiteral("the stored cell ids are missing"));
		dataset->cellIds.resize(triangles.size() / 3);
		std::memcpy(dataset->cellIds.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
	}

	// One triangle cell per surface triangle: the boundary surface of the snapshot is the identity.
	const std::size_t triangleCount = triangles.size() / 3;
	dataset->cellTypes.assign(triangleCount, ResultCellType::Triangle);
	dataset->cellOffsets.resize(triangleCount + 1);
	for (std::size_t t = 0; t <= triangleCount; ++t)
		dataset->cellOffsets[t] = static_cast<std::uint32_t>(t * 3);
	dataset->cellConnectivity = triangles;

	for (const QJsonValue& sv : json.value(QStringLiteral("steps")).toArray())
	{
		const QJsonObject o = sv.toObject();
		ResultStep step;
		step.time = o.value(QStringLiteral("time")).toDouble();
		step.label = o.value(QStringLiteral("label")).toString();
		step.timeUnit = o.value(QStringLiteral("timeUnit")).toString();
		dataset->steps.push_back(step);
	}

	for (const QJsonValue& fv : json.value(QStringLiteral("fields")).toArray())
	{
		const QJsonObject o = fv.toObject();
		ResultField f;
		applyFieldMeta(o, f);
		if (f.components <= 0)
			return fail(QStringLiteral("a field has an invalid component count"));
		const QJsonArray stepBlobs = o.value(QStringLiteral("stepData")).toArray();
		if (static_cast<std::size_t>(stepBlobs.size()) != dataset->steps.size())
			return fail(QStringLiteral("a field does not match the stored steps"));
		for (const QJsonValue& bv : stepBlobs)
		{
			std::vector<float> values;
			const int index = bv.toInt(-1);
			const std::size_t tuples = f.association == ResultFieldAssociation::Cell ? triangles.size() / 3 : vertexCount;
			if (index >= 0 && !blobFloats(index, tuples * static_cast<std::size_t>(f.components), values))
				return error && !error->isEmpty() ? false : fail(QStringLiteral("a stored field array does not match the mesh"));
			f.stepData.push_back(std::move(values));
		}
		dataset->fields.push_back(std::move(f));
	}

	// Derived stress fields are pointwise functions of the stored tensor: rebuilt, never stored.
	auto rebuildDerivedFields = [](ResultDataset& target) {
		const std::size_t sourceFieldCount = target.fields.size();
		addDerivedStressFields(target);
		for (std::size_t i = sourceFieldCount; i < target.fields.size(); ++i)
		{
			ResultField& derived = target.fields[i];
			if (derived.derivedFromField < 0 || static_cast<std::size_t>(derived.derivedFromField) >= sourceFieldCount)
				continue;
			const ResultField& source = target.fields[static_cast<std::size_t>(derived.derivedFromField)];
			derived.quantityKind = source.quantityKind; // derived fields always share their source's units
			derived.fileUnit = source.fileUnit;
			derived.displayUnit = source.displayUnit;
			derived.unitConfirmed = source.unitConfirmed;
		}
	};
	rebuildDerivedFields(*dataset);

	// Full-model ranges (see encode) so the legend matches the live result.
	for (const QJsonValue& rv : json.value(QStringLiteral("ranges")).toArray())
	{
		const QJsonObject o = rv.toObject();
		const QString name = o.value(QStringLiteral("name")).toString();
		const ResultFieldAssociation association = o.value(QStringLiteral("association")).toString() == QLatin1String("cell")
			? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
		for (ResultField& f : dataset->fields)
		{
			if (f.name != name || f.association != association)
				continue;
			const QJsonArray data = o.value(QStringLiteral("data")).toArray();
			if (static_cast<std::size_t>(data.size()) == dataset->steps.size() * static_cast<std::size_t>(resultRangeSelectorCount(f.components)) * 2)
			{
				f.storedRange.resize(static_cast<std::size_t>(data.size()));
				for (int k = 0; k < data.size(); ++k)
					f.storedRange[static_cast<std::size_t>(k)] = jsonToFloat(data[k]);
			}
			break;
		}
	}

	const QString invalid = dataset->validate();
	if (!invalid.isEmpty())
		return fail(invalid);

	// The volume, when it was stored: the full result replaces the surface one (same steps and fields, at every node and cell); the mapping says how the
	// mesh's surface sits on it. Anything wrong with it and the surface result is used, with a warning.
	if (json.contains(QStringLiteral("volume")))
	{
		const QJsonObject vol = json.value(QStringLiteral("volume")).toObject();
		auto u32 = [&](const char* key, std::vector<std::uint32_t>& values, bool required) {
			if (!vol.contains(QLatin1String(key)))
				return !required;
			QByteArray raw;
			if (!blobBytes(vol.value(QLatin1String(key)).toInt(-1), raw) || raw.size() % 4 != 0)
				return false;
			values.resize(static_cast<std::size_t>(raw.size()) / 4);
			if (!values.empty())
				std::memcpy(values.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
			return true;
		};
		auto i64 = [&](const char* key, std::vector<std::int64_t>& values) {
			if (!vol.contains(QLatin1String(key)))
				return true;
			QByteArray raw;
			if (!blobBytes(vol.value(QLatin1String(key)).toInt(-1), raw) || raw.size() % 8 != 0)
				return false;
			values.resize(static_cast<std::size_t>(raw.size()) / 8);
			if (!values.empty())
				std::memcpy(values.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
			return true;
		};
		auto full = std::make_shared<ResultDataset>();
		full->solverName = dataset->solverName;
		full->lengthUnit = dataset->lengthUnit;
		full->sourcePath = dataset->sourcePath;
		full->steps = dataset->steps;
		std::vector<std::uint32_t> types, vertexNode, triangleCell, triangleFace;
		bool ok = u32("cellTypes", types, true) && u32("cellOffsets", full->cellOffsets, true) && u32("cellConnectivity", full->cellConnectivity, true)
			&& u32("faceNodes", full->faceNodes, false) && u32("faceOffsets", full->faceOffsets, false) && u32("cellFaces", full->cellFaces, false)
			&& u32("cellFaceOffsets", full->cellFaceOffsets, false) && u32("vertexNode", vertexNode, true) && u32("triangleCell", triangleCell, true)
			&& u32("triangleFace", triangleFace, true) && i64("nodeIds", full->nodeIds) && i64("cellIds", full->cellIds);
		if (ok)
		{
			QByteArray raw;
			ok = blobBytes(vol.value(QStringLiteral("nodePositions")).toInt(-1), raw);
			if (ok)
				full->nodePositions = bytesToFloats(raw);
		}
		if (ok)
		{
			full->cellTypes.resize(types.size());
			for (std::size_t i = 0; i < types.size(); ++i)
				full->cellTypes[i] = static_cast<ResultCellType>(types[i]);
			for (const QJsonValue& fv : vol.value(QStringLiteral("fields")).toArray())
			{
				const QJsonObject o = fv.toObject();
				ResultField f;
				applyFieldMeta(o, f);
				const QJsonArray stepBlobs = o.value(QStringLiteral("stepData")).toArray();
				if (f.components <= 0 || static_cast<std::size_t>(stepBlobs.size()) != full->steps.size())
				{
					ok = false;
					break;
				}
				for (const QJsonValue& bv : stepBlobs)
				{
					std::vector<float> values;
					QByteArray raw;
					if (bv.toInt(-1) >= 0)
					{
						if (!blobBytes(bv.toInt(-1), raw))
						{
							ok = false;
							break;
						}
						values = bytesToFloats(raw);
					}
					f.stepData.push_back(std::move(values));
				}
				if (!ok)
					break;
				full->fields.push_back(std::move(f));
			}
		}
		if (ok)
		{
			rebuildDerivedFields(*full);
			ok = full->validate().isEmpty() && vertexNode.size() == vertexCount && triangleCell.size() == triangles.size() / 3
				&& triangleFace.size() == triangles.size() / 3;
		}
		for (std::size_t i = 0; ok && i < vertexNode.size(); ++i)
			ok = vertexNode[i] < full->nodeCount();
		for (std::size_t i = 0; ok && i < triangleCell.size(); ++i)
			ok = triangleCell[i] < full->cellCount();
		if (ok)
		{
			dataset = std::move(full);
			out.hasVolume = true;
			out.vertexNode = std::move(vertexNode);
			out.triangleCell = std::move(triangleCell);
			out.triangleFace.assign(triangleFace.begin(), triangleFace.end());
		}
		else
		{
			if (error)
				error->clear();
			out.warnings << QStringLiteral("The stored volume could not be read, so sections, iso-surfaces and streamlines cannot be recomputed; the visible surface is shown.");
		}
	}

	// View state; the field is found again by name because the field list may differ from the original.
	const QJsonObject view = json.value(QStringLiteral("view")).toObject();
	SimulationViewState state;
	const QString fieldName = view.value(QStringLiteral("field")).toString();
	const ResultFieldAssociation fieldAssociation = view.value(QStringLiteral("fieldAssociation")).toString() == QLatin1String("cell")
		? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
	state.fieldIndex = -1;
	for (std::size_t i = 0; i < dataset->fields.size(); ++i)
		if (dataset->fields[i].name == fieldName && dataset->fields[i].association == fieldAssociation)
			state.fieldIndex = static_cast<int>(i);
	if (state.fieldIndex < 0 && !fieldName.isEmpty())
		out.warnings << QStringLiteral("The field '%1' shown when the result was saved is not stored.").arg(fieldName);
	state.component = view.value(QStringLiteral("component")).toInt(-1);
	state.customRange = view.value(QStringLiteral("customRange")).toBool();
	state.rangeMin = view.value(QStringLiteral("rangeMin")).toDouble();
	state.rangeMax = view.value(QStringLiteral("rangeMax")).toDouble(1.0);
	state.colormap = view.value(QStringLiteral("colormap")).toInt();
	state.bands = view.value(QStringLiteral("bands")).toInt();
	state.step = std::clamp(view.value(QStringLiteral("step")).toInt(), 0, std::max(0, static_cast<int>(dataset->steps.size()) - 1));
	state.allStepsRange = view.value(QStringLiteral("allStepsRange")).toBool(true);
	state.deform = view.value(QStringLiteral("deform")).toBool();
	state.deformScale = view.value(QStringLiteral("deformScale")).toDouble(1.0);
	state.markExtrema = view.value(QStringLiteral("markExtrema")).toBool();
	state.sectionFill = view.value(QStringLiteral("sectionFill")).toBool();
	state.iso = view.value(QStringLiteral("iso")).toBool();
	state.isoLevels = view.value(QStringLiteral("isoLevels")).toInt(3);
	{
		const QString isoName = view.value(QStringLiteral("isoFieldName")).toString();
		const ResultFieldAssociation isoAssociation = view.value(QStringLiteral("isoFieldAssociation")).toString() == QLatin1String("cell")
			? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
		for (std::size_t i = 0; i < dataset->fields.size() && !isoName.isEmpty(); ++i)
			if (dataset->fields[i].name == isoName && dataset->fields[i].association == isoAssociation)
				state.isoField = static_cast<int>(i);
	}
	state.streamlines = view.value(QStringLiteral("streamlines")).toBool();
	state.streamSeeds = view.value(QStringLiteral("streamSeeds")).toInt(50);
	state.streamOnPlane = view.value(QStringLiteral("streamOnPlane")).toBool();
	{
		const QString streamName = view.value(QStringLiteral("streamFieldName")).toString();
		for (std::size_t i = 0; i < dataset->fields.size() && !streamName.isEmpty(); ++i)
			if (dataset->fields[i].name == streamName && dataset->fields[i].association == ResultFieldAssociation::Node)
				state.streamField = static_cast<int>(i);
	}
	state.glyphs = view.value(QStringLiteral("glyphs")).toBool();
	state.glyphScale = view.value(QStringLiteral("glyphScale")).toDouble(1.0);
	state.glyphCount = view.value(QStringLiteral("glyphCount")).toInt(800);
	state.glyphScaleByMagnitude = view.value(QStringLiteral("glyphScaleByMagnitude")).toBool(true);
	{
		const QString glyphName = view.value(QStringLiteral("glyphFieldName")).toString();
		const ResultFieldAssociation glyphAssociation = view.value(QStringLiteral("glyphFieldAssociation")).toString() == QLatin1String("cell")
			? ResultFieldAssociation::Cell : ResultFieldAssociation::Node;
		for (std::size_t i = 0; i < dataset->fields.size() && !glyphName.isEmpty(); ++i)
			if (dataset->fields[i].name == glyphName && dataset->fields[i].association == glyphAssociation)
				state.glyphField = static_cast<int>(i);
	}

	// The frozen cut faces, iso-surfaces and streamlines (only entries that fit their own arrays are kept).
	if (json.contains(QStringLiteral("overlays")))
	{
		const QJsonObject o = json.value(QStringLiteral("overlays")).toObject();
		auto floats = [&](const QJsonObject& e, const char* key, std::vector<float>& values) {
			QByteArray raw;
			if (!blobBytes(e.value(QLatin1String(key)).toInt(-1), raw))
				return false;
			values = bytesToFloats(raw);
			return true;
		};
		auto indices = [&](const QJsonObject& e, const char* key, std::vector<std::uint32_t>& values) {
			QByteArray raw;
			if (!blobBytes(e.value(QLatin1String(key)).toInt(-1), raw) || raw.size() % 4 != 0)
				return false;
			values.resize(static_cast<std::size_t>(raw.size()) / 4);
			if (!values.empty())
				std::memcpy(values.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
			return true;
		};
		auto inRange = [](const std::vector<std::uint32_t>& ids, std::size_t vertices) {
			return std::all_of(ids.begin(), ids.end(), [&](std::uint32_t i) { return i < vertices; });
		};
		for (const QJsonValue& sv : o.value(QStringLiteral("slices")).toArray())
		{
			const QJsonObject e = sv.toObject();
			SliceDisplay d;
			d.lit = e.value(QStringLiteral("lit")).toBool();
			if (floats(e, "positions", d.positions) && floats(e, "colors", d.colors) && indices(e, "triangles", d.triangles) && d.positions.size() % 3 == 0
			    && d.colors.size() == d.positions.size() && d.triangles.size() % 3 == 0 && inRange(d.triangles, d.positions.size() / 3))
				out.overlays.slices.push_back(std::move(d));
		}
		const QJsonObject lines = o.value(QStringLiteral("streamlines")).toObject();
		if (!lines.isEmpty())
		{
			StreamlineDisplay d;
			if (floats(lines, "positions", d.positions) && floats(lines, "colors", d.colors) && indices(lines, "segments", d.segments) && d.positions.size() % 3 == 0
			    && d.colors.size() == d.positions.size() && d.segments.size() % 2 == 0 && inRange(d.segments, d.positions.size() / 3))
				out.overlays.streamlines = std::move(d);
		}
		for (const QJsonValue& cv : o.value(QStringLiteral("cuts")).toArray())
		{
			const QJsonObject e = cv.toObject();
			OverlayClipCut c;
			c.axis = e.value(QStringLiteral("axis")).toInt(-1);
			c.position = e.value(QStringLiteral("position")).toDouble();
			c.keepPositive = e.value(QStringLiteral("keepPositive")).toBool();
			if (c.axis >= 0 && c.axis <= 2)
				out.overlays.cuts.push_back(c);
		}
		if (error)
			error->clear();
	}

	out.dataset = std::move(dataset);
	out.state = state;
	return true;
}
