#include "CalculixFrdReader.h"

#include "ResultDerivedFields.h"

#include <QByteArray>
#include <QFile>
#include <QHash>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>

namespace
{
	// ---- Fixed-width line access -------------------------------------------------------------------------------

	struct Ln
	{
		const char* d = nullptr;
		int n = 0;
	};

	// Skips leading spaces, then checks the record key.
	bool hasKey(const Ln& l, const char* key)
	{
		int k = 0;
		while (k < l.n && l.d[k] == ' ')
			++k;
		const int len = static_cast<int>(std::strlen(key));
		return k + len <= l.n && std::strncmp(l.d + k, key, static_cast<std::size_t>(len)) == 0;
	}

	bool isBlank(const Ln& l)
	{
		for (int i = 0; i < l.n; ++i)
			if (l.d[i] != ' ' && l.d[i] != '\t')
				return false;
		return true;
	}

	// One column field, trimmed. `ok` is false if the field lies outside the line or is not a number.
	std::int64_t fixedInt(const Ln& l, int pos, int width, bool& ok)
	{
		ok = false;
		if (pos >= l.n)
			return 0;
		const QByteArray field = QByteArray(l.d + pos, std::min(width, l.n - pos)).trimmed();
		if (field.isEmpty())
			return 0;
		return field.toLongLong(&ok);
	}

	double fixedDouble(const Ln& l, int pos, int width, bool& ok)
	{
		ok = false;
		if (pos >= l.n)
			return 0.0;
		const QByteArray field = QByteArray(l.d + pos, std::min(width, l.n - pos)).trimmed();
		if (field.isEmpty())
			return 0.0;
		return field.toDouble(&ok);
	}

	QList<QByteArray> tokens(const Ln& l)
	{
		const QByteArray simplified = QByteArray(l.d, l.n).simplified();
		if (simplified.isEmpty())
			return {};
		return simplified.split(' ');
	}

	struct LineReader
	{
		const char* data;
		qint64 size;
		qint64 pos = 0;
		qint64 lineStart = 0; // where the line last read begins
		std::size_t lineNo = 0;

		LineReader(const char* d, qint64 n) : data(d), size(n) {}

		bool next(Ln& line)
		{
			if (pos >= size)
				return false;
			lineStart = pos;
			const char* start = data + pos;
			const char* nl = static_cast<const char*>(std::memchr(start, '\n', static_cast<std::size_t>(size - pos)));
			const qint64 length = nl ? (nl - start) : (size - pos);
			pos += length + (nl ? 1 : 0);
			int n = static_cast<int>(length);
			if (n > 0 && start[n - 1] == '\r')
				--n;
			line.d = start;
			line.n = n;
			++lineNo;
			return true;
		}
	};

	// ---- Element types -----------------------------------------------------------------------------------------

	struct FrdType
	{
		int nodes;
		ResultCellType type;
	};

	bool frdElementType(int code, FrdType& out)
	{
		switch (code)
		{
		case 1:  out = { 8, ResultCellType::Hexahedron };    return true; // HE8
		case 2:  out = { 6, ResultCellType::Wedge };         return true; // PE6
		case 3:  out = { 4, ResultCellType::Tetra };         return true; // TE4
		case 4:  out = { 20, ResultCellType::Hexahedron20 }; return true; // HE20
		case 5:  out = { 15, ResultCellType::Wedge15 };      return true; // PE15
		case 6:  out = { 10, ResultCellType::Tetra10 };      return true; // TE10
		case 7:  out = { 3, ResultCellType::Triangle };      return true; // TR3
		case 8:  out = { 6, ResultCellType::Triangle6 };     return true; // TR6
		case 9:  out = { 4, ResultCellType::Quad };          return true; // QU4
		case 10: out = { 8, ResultCellType::Quad8 };         return true; // QU8
		case 11: out = { 2, ResultCellType::Line };          return true; // BE2
		case 12: out = { 3, ResultCellType::Unsupported };   return true; // BE3
		default: break;
		}
		return false;
	}

	// The record-format flag is the last number on a block's header line: 0 short ASCII, 1 long ASCII, 2 binary.
	int headerFormat(const Ln& header)
	{
		const QList<QByteArray> t = tokens(header);
		bool ok = false;
		const int f = t.isEmpty() ? 1 : t.last().toInt(&ok);
		return ok ? f : 1;
	}

	bool cancelled(const std::atomic<bool>* cancel)
	{
		return cancel && cancel->load(std::memory_order_acquire);
	}

	// The file's bytes: memory-mapped when the system allows (nothing is copied, the pages come and go with the system's cache), else read in one piece.
	struct FrdSource
	{
		QFile file;
		QByteArray fallback;
		const char* data = nullptr;
		qint64 size = 0;

		bool open(const QString& path, QString& error)
		{
			file.setFileName(path);
			if (!file.open(QIODevice::ReadOnly))
			{
				error = file.errorString();
				return false;
			}
			size = file.size();
			if (size <= 0)
				return true;
			if (const uchar* mapped = file.map(0, size))
			{
				data = reinterpret_cast<const char*>(mapped);
				return true;
			}
			fallback = file.readAll();
			data = fallback.constData();
			size = fallback.size();
			return true;
		}
	};

	// One nodal result block, before it is filed under its step.
	struct ResultBlock
	{
		double time = 0.0;
		int mode = 0; // from a preceding "1PMODE" record, 0 if none
		QString name;
		std::vector<QString> componentNames;
		std::vector<float> values; // nodeCount * componentNames.size(), NaN where the node was not listed
		qint64 offset = -1;        // where its "100C" record starts in the file
		int idWidth = 10;          // width of the node id in its data records
	};

	// The header records of a result block: `line` is its "100C" record on entry; on return it is the first data line (or "-3" for an empty block). False with `error`.
	bool readBlockHeader(LineReader& reader, Ln& line, ResultBlock& block, QString& error)
	{
		const QList<QByteArray> head = tokens(line);
		if (head.size() < 3)
		{
			error = QStringLiteral("Malformed result header");
			return false;
		}
		const int format = headerFormat(line);
		if (format == 2)
		{
			error = QStringLiteral("Binary .frd files are not supported; write ASCII output (the default) instead.");
			return false;
		}
		block.idWidth = format == 0 ? 5 : 10;
		bool ok = false;
		block.time = head[2].toDouble(&ok);
		if (!ok)
		{
			error = QStringLiteral("Bad result time");
			return false;
		}
		// " -4  NAME  ncomponents  irtype"
		if (!reader.next(line) || !hasKey(line, "-4"))
		{
			error = QStringLiteral("A result block has no -4 (name) record");
			return false;
		}
		const QList<QByteArray> nameRecord = tokens(line);
		if (nameRecord.size() < 2)
		{
			error = QStringLiteral("Malformed result name");
			return false;
		}
		block.name = QString::fromLatin1(nameRecord[1]);

		// " -5  COMP  menu ictype icind1 icind2 [iexist NAME]": a fifth number marks a component the solver did
		// not store (a calculated one such as "ALL"); only the others have values in the data lines.
		bool eof = false;
		while (true)
		{
			if (!reader.next(line))
			{
				eof = true;
				break;
			}
			if (!hasKey(line, "-5"))
				break;
			const QList<QByteArray> c = tokens(line);
			if (c.size() >= 2 && c.size() < 7)
				block.componentNames.push_back(QString::fromLatin1(c[1]));
		}
		if (eof || block.componentNames.empty())
		{
			error = QStringLiteral("Result block '%1' has no stored components").arg(block.name);
			return false;
		}
		return true;
	}

	// The data records of a result block, from `line` (the first data line, see readBlockHeader) to its "-3": the values of the block's components per node.
	// `lookup(id, index)` turns a node id into the node's index (false: not a node of the mesh, the record is skipped). False with `error`.
	template <class Lookup>
	bool readBlockData(LineReader& reader, Ln& line, ResultBlock& block, std::size_t nodeCount, const Lookup& lookup, QString& error)
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const int comps = static_cast<int>(block.componentNames.size());
		block.values.assign(nodeCount * static_cast<std::size_t>(comps), nan);
		bool ok = false;
		do
		{
			if (hasKey(line, "-3"))
				break;
			if (!hasKey(line, "-1"))
			{
				error = QStringLiteral("Unexpected record in result block '%1'").arg(block.name);
				return false;
			}
			const std::int64_t nodeId = fixedInt(line, 3, block.idWidth, ok);
			if (!ok)
			{
				error = QStringLiteral("Bad node id in a result block");
				return false;
			}
			std::uint32_t index = 0;
			const bool known = lookup(nodeId, index);
			const int firstValue = 3 + block.idWidth;
			int have = 0;
			Ln current = line;
			while (true)
			{
				for (int k = 0; current.n >= firstValue + 12 * (k + 1) && have < comps; ++k)
				{
					const double v = fixedDouble(current, firstValue + 12 * k, 12, ok);
					if (!ok)
					{
						error = QStringLiteral("Bad value in result block '%1'").arg(block.name);
						return false;
					}
					if (known)
						block.values[static_cast<std::size_t>(index) * static_cast<std::size_t>(comps) + static_cast<std::size_t>(have)] = static_cast<float>(v);
					++have;
				}
				if (have >= comps)
					break;
				// More components than fit one line: continued on " -2" records.
				if (!reader.next(current) || !hasKey(current, "-2"))
				{
					error = QStringLiteral("Result block '%1' has a truncated value list").arg(block.name);
					return false;
				}
			}
		} while (reader.next(line));
		return true;
	}

	// The same walk without reading a number: to the block's "-3".
	void skipBlockData(LineReader& reader, Ln& line)
	{
		do
		{
			if (hasKey(line, "-3"))
				break;
		} while (reader.next(line));
	}

	// Where the state of a lazily read file is kept (see LazySteps): the file's bytes and how to find a block's nodes.
	struct LazyFrd
	{
		std::shared_ptr<FrdSource> source;
		std::size_t nodeCount = 0;
		bool denseIds = false;                                    // node ids are 1..n in order: no lookup table needed
		std::unordered_map<std::int64_t, std::uint32_t> nodeIndex; // else this
		struct Ref
		{
			std::size_t field;
			qint64 offset;
		};
		std::vector<std::vector<Ref>> blocksOfStep;
	};
}

ResultReadOutcome readCalculixFrd(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;
	auto fail = [&outcome](const QString& message) -> ResultReadOutcome
	{
		outcome.error = message;
		outcome.dataset.reset();
		return std::move(outcome);
	};

	auto source = std::make_shared<FrdSource>();
	{
		QString openError;
		if (!source->open(path, openError))
			return fail(QStringLiteral("Cannot open '%1': %2").arg(path, openError));
	}
	if (source->size <= 0)
		return fail(QStringLiteral("The file is empty."));
	// A file as large as this holds more steps than it should keep in memory: its result blocks are then only indexed (where each starts), and read one step at a time
	// when asked for (see LazySteps). The text is several times the size of the numbers in it, so the file's size is a safe stand-in for the data's.
	const bool indexOnly = static_cast<std::size_t>(source->size) > resultLazyThresholdBytes();

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("CalculiX");

	LineReader reader(source->data, source->size);
	Ln line;
	std::unordered_map<std::int64_t, std::uint32_t> nodeIndex;
	std::vector<ResultBlock> blocks;
	int pendingMode = 0;
	bool sawNodes = false;
	const float nan = std::numeric_limits<float>::quiet_NaN();

	auto where = [&reader]() { return QStringLiteral(" (line %1)").arg(reader.lineNo); };

	while (reader.next(line))
	{
		if ((reader.lineNo & 0xFFF) == 0 && cancelled(cancel))
			return fail(QStringLiteral("cancelled"));
		if (isBlank(line))
			continue;
		if (hasKey(line, "9999"))
			break;

		// ---- Nodes: " -1" + node id + 3 x E12.5 ----------------------------------------------------------------
		if (hasKey(line, "2C"))
		{
			const int format = headerFormat(line);
			if (format == 2)
				return fail(QStringLiteral("Binary .frd files are not supported; write ASCII output (the default) instead."));
			const int idWidth = format == 0 ? 5 : 10;
			sawNodes = true;
			while (reader.next(line))
			{
				if (hasKey(line, "-3"))
					break;
				if (!hasKey(line, "-1"))
					return fail(QStringLiteral("Unexpected record in the node block") + where());
				bool ok = false;
				const std::int64_t id = fixedInt(line, 3, idWidth, ok);
				if (!ok)
					return fail(QStringLiteral("Bad node id") + where());
				float xyz[3];
				for (int k = 0; k < 3; ++k)
				{
					const double v = fixedDouble(line, 3 + idWidth + 12 * k, 12, ok);
					if (!ok)
						return fail(QStringLiteral("Bad node coordinate") + where());
					xyz[k] = static_cast<float>(v);
				}
				nodeIndex[id] = static_cast<std::uint32_t>(dataset->nodeCount());
				dataset->nodeIds.push_back(id);
				dataset->nodePositions.insert(dataset->nodePositions.end(), xyz, xyz + 3);
			}
		}
		// ---- Elements: " -1" id type group material, then " -2" lines of node ids -------------------------------
		else if (hasKey(line, "3C"))
		{
			const int format = headerFormat(line);
			if (format == 2)
				return fail(QStringLiteral("Binary .frd files are not supported; write ASCII output (the default) instead."));
			const int idWidth = format == 0 ? 5 : 10;
			const int idsPerLine = format == 0 ? 15 : 10;
			dataset->cellOffsets.assign(1, 0);
			while (reader.next(line))
			{
				if (hasKey(line, "-3"))
					break;
				if (!hasKey(line, "-1"))
					return fail(QStringLiteral("Unexpected record in the element block") + where());
				bool ok = false;
				const std::int64_t elementId = fixedInt(line, 3, idWidth, ok);
				const int typeCode = static_cast<int>(fixedInt(line, 3 + idWidth, 5, ok));
				if (!ok)
					return fail(QStringLiteral("Bad element header") + where());
				FrdType type;
				if (!frdElementType(typeCode, type))
					return fail(QStringLiteral("Unsupported element type %1").arg(typeCode) + where());

				std::vector<std::uint32_t> connectivity;
				while (static_cast<int>(connectivity.size()) < type.nodes)
				{
					if (!reader.next(line) || !hasKey(line, "-2"))
						return fail(QStringLiteral("Element %1 is missing its node list").arg(elementId) + where());
					for (int k = 0; k < idsPerLine && static_cast<int>(connectivity.size()) < type.nodes; ++k)
					{
						const int pos = 3 + k * idWidth;
						if (pos >= line.n)
							break;
						const std::int64_t nodeId = fixedInt(line, pos, idWidth, ok);
						if (!ok)
							break;
						const auto it = nodeIndex.find(nodeId);
						if (it == nodeIndex.end())
							return fail(QStringLiteral("Element %1 uses unknown node %2").arg(elementId).arg(nodeId) + where());
						connectivity.push_back(it->second);
					}
				}
				dataset->cellTypes.push_back(type.type);
				dataset->cellIds.push_back(elementId);
				dataset->cellConnectivity.insert(dataset->cellConnectivity.end(), connectivity.begin(), connectivity.end());
				dataset->cellOffsets.push_back(static_cast<std::uint32_t>(dataset->cellConnectivity.size()));
			}
		}
		// ---- The mode number of a modal result (precedes its "100C" block) --------------------------------------
		else if (hasKey(line, "1PMODE"))
		{
			const QList<QByteArray> t = tokens(line);
			pendingMode = t.size() > 1 ? t[1].toInt() : 0;
		}
		// ---- Nodal result block ---------------------------------------------------------------------------------
		else if (hasKey(line, "100C"))
		{
			ResultBlock block;
			block.offset = reader.lineStart;
			QString error;
			if (!readBlockHeader(reader, line, block, error))
				return fail(error + where());
			block.mode = pendingMode;
			pendingMode = 0;
			if (indexOnly)
				skipBlockData(reader, line); // the values are read when the step is asked for
			else if (!readBlockData(reader, line, block, dataset->nodeCount(),
			                        [&nodeIndex](std::int64_t id, std::uint32_t& index) {
				                        const auto it = nodeIndex.find(id);
				                        if (it == nodeIndex.end())
					                        return false;
				                        index = it->second;
				                        return true;
			                        },
			                        error))
				return fail(error + where());
			blocks.push_back(std::move(block));
		}
		// Everything else (title, user records, other "1P" records, ...) carries nothing we show.
	}

	if (!sawNodes || dataset->nodeCount() == 0)
		return fail(QStringLiteral("The file has no node block."));

	// ---- Steps: one per distinct result time, in order of first appearance -----------------------------------
	std::vector<double> stepTimes;
	std::vector<int> stepModes;
	auto stepOf = [&](const ResultBlock& b) -> std::size_t
	{
		for (std::size_t i = 0; i < stepTimes.size(); ++i)
			if (stepTimes[i] == b.time)
				return i;
		stepTimes.push_back(b.time);
		stepModes.push_back(b.mode);
		return stepTimes.size() - 1;
	};

	// Lazy only when there is more than one step to save memory on: with one, the blocks are read now.
	bool denseIds = true;
	for (std::size_t i = 0; i < dataset->nodeIds.size() && denseIds; ++i)
		denseIds = dataset->nodeIds[i] == static_cast<std::int64_t>(i + 1);
	for (const ResultBlock& b : blocks)
		stepOf(b);
	const bool lazy = indexOnly && stepTimes.size() > 1;
	if (indexOnly && !lazy)
		for (ResultBlock& b : blocks)
		{
			LineReader again(source->data, source->size);
			again.pos = b.offset;
			Ln first;
			QString error;
			ResultBlock reread;
			if (!again.next(first) || !readBlockHeader(again, first, reread, error)
			    || !readBlockData(again, first, reread, dataset->nodeCount(),
			                      [&nodeIndex](std::int64_t id, std::uint32_t& index) {
				                      const auto it = nodeIndex.find(id);
				                      if (it == nodeIndex.end())
					                      return false;
				                      index = it->second;
				                      return true;
			                      },
			                      error))
				return fail(error);
			b.values = std::move(reread.values);
		}
	auto lazyFrd = std::make_shared<LazyFrd>();
	if (lazy)
	{
		lazyFrd->source = source;
		lazyFrd->nodeCount = dataset->nodeCount();
		lazyFrd->denseIds = denseIds;
		if (!denseIds)
			lazyFrd->nodeIndex = std::move(nodeIndex);
		lazyFrd->blocksOfStep.resize(stepTimes.size());
	}

	QHash<QString, int> fieldIndex;
	for (ResultBlock& b : blocks)
	{
		const std::size_t step = stepOf(b);
		const int comps = static_cast<int>(b.componentNames.size());
		int index = fieldIndex.value(b.name, -1);
		if (index < 0)
		{
			ResultField field;
			field.name = b.name;
			field.association = ResultFieldAssociation::Node;
			field.components = comps;
			field.componentNames = b.componentNames;
			field.lazyData = lazy;
			dataset->fields.push_back(std::move(field));
			index = static_cast<int>(dataset->fields.size()) - 1;
			fieldIndex.insert(b.name, index);
		}
		ResultField& field = dataset->fields[static_cast<std::size_t>(index)];
		if (field.components != comps)
		{
			outcome.warnings << QStringLiteral("Ignored a '%1' result block with a different component count.").arg(b.name);
			continue;
		}
		if (lazy)
		{
			lazyFrd->blocksOfStep[step].push_back({ static_cast<std::size_t>(index), b.offset });
			continue;
		}
		if (field.stepData.size() <= step)
			field.stepData.resize(step + 1);
		field.stepData[step] = std::move(b.values);
	}

	if (stepTimes.empty()) // geometry only
	{
		stepTimes.push_back(0.0);
		stepModes.push_back(0);
	}
	for (std::size_t i = 0; i < stepTimes.size(); ++i)
	{
		ResultStep step;
		step.time = stepTimes[i];
		if (stepModes[i] > 0)
		{
			step.label = QStringLiteral("Mode %1").arg(stepModes[i]);
			step.timeUnit = QStringLiteral("Hz"); // a modal result's "time" is the frequency
		}
		dataset->steps.push_back(step);
	}
	for (ResultField& field : dataset->fields)
		field.stepData.resize(dataset->steps.size());

	addDerivedStressFields(*dataset);

	if (lazy)
	{
		// Loader: a step is its result blocks, read from where they were found; then the stress fields derived from them are computed.
		auto lazySteps = std::make_shared<LazySteps>();
		lazySteps->load = [lazyFrd](std::size_t step, ResultDataset& ds) {
			if (step >= lazyFrd->blocksOfStep.size())
				return false;
			for (const LazyFrd::Ref& ref : lazyFrd->blocksOfStep[step])
			{
				LineReader again(lazyFrd->source->data, lazyFrd->source->size);
				again.pos = ref.offset;
				Ln first;
				ResultBlock block;
				QString error;
				if (!again.next(first) || !readBlockHeader(again, first, block, error) || ref.field >= ds.fields.size()
				    || static_cast<int>(block.componentNames.size()) != ds.fields[ref.field].components)
					continue;
				const bool read = lazyFrd->denseIds
					? readBlockData(again, first, block, lazyFrd->nodeCount,
					                [n = lazyFrd->nodeCount](std::int64_t id, std::uint32_t& index) {
						                if (id < 1 || static_cast<std::size_t>(id) > n)
							                return false;
						                index = static_cast<std::uint32_t>(id - 1);
						                return true;
					                },
					                error)
					: readBlockData(again, first, block, lazyFrd->nodeCount,
					                [lazyFrd](std::int64_t id, std::uint32_t& index) {
						                const auto it = lazyFrd->nodeIndex.find(id);
						                if (it == lazyFrd->nodeIndex.end())
							                return false;
						                index = it->second;
						                return true;
					                },
					                error);
				if (read && step < ds.fields[ref.field].stepData.size())
					ds.fields[ref.field].stepData[step] = std::move(block.values);
			}
			computeDerivedStressStep(ds, step);
			return true;
		};
		dataset->lazy = std::move(lazySteps);
	}

	if (dataset->cellCount() == 0)
		outcome.warnings << QStringLiteral("The file has no element block, so there is no surface to display.");
	outcome.warnings << resultCellTypeWarnings(*dataset);

	const QString problem = dataset->validate();
	if (!problem.isEmpty())
		return fail(QStringLiteral("Invalid dataset: %1").arg(problem));

	outcome.dataset = std::move(dataset);
	return outcome;
}
