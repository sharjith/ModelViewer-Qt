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
		const QByteArray& buf;
		qint64 pos = 0;
		std::size_t lineNo = 0;

		explicit LineReader(const QByteArray& b) : buf(b) {}

		bool next(Ln& line)
		{
			if (pos >= buf.size())
				return false;
			const char* start = buf.constData() + pos;
			const char* nl = static_cast<const char*>(std::memchr(start, '\n', static_cast<std::size_t>(buf.size() - pos)));
			const qint64 length = nl ? (nl - start) : (buf.size() - pos);
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

	// One nodal result block, before it is filed under its step.
	struct ResultBlock
	{
		double time = 0.0;
		int mode = 0; // from a preceding "1PMODE" record, 0 if none
		QString name;
		std::vector<QString> componentNames;
		std::vector<float> values; // nodeCount * componentNames.size(), NaN where the node was not listed
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

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return fail(QStringLiteral("Cannot open '%1': %2").arg(path, file.errorString()));
	const QByteArray all = file.readAll();
	file.close();
	if (all.isEmpty())
		return fail(QStringLiteral("The file is empty."));

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("CalculiX");

	LineReader reader(all);
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
			const QList<QByteArray> head = tokens(line);
			if (head.size() < 3)
				return fail(QStringLiteral("Malformed result header") + where());
			const int format = headerFormat(line);
			if (format == 2)
				return fail(QStringLiteral("Binary .frd files are not supported; write ASCII output (the default) instead."));
			const int idWidth = format == 0 ? 5 : 10;

			ResultBlock block;
			bool ok = false;
			block.time = head[2].toDouble(&ok);
			if (!ok)
				return fail(QStringLiteral("Bad result time") + where());
			block.mode = pendingMode;
			pendingMode = 0;

			// " -4  NAME  ncomponents  irtype"
			if (!reader.next(line) || !hasKey(line, "-4"))
				return fail(QStringLiteral("A result block has no -4 (name) record") + where());
			const QList<QByteArray> nameRecord = tokens(line);
			if (nameRecord.size() < 2)
				return fail(QStringLiteral("Malformed result name") + where());
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
				return fail(QStringLiteral("Result block '%1' has no stored components").arg(block.name) + where());
			const int comps = static_cast<int>(block.componentNames.size());
			block.values.assign(dataset->nodeCount() * static_cast<std::size_t>(comps), nan);

			// `line` is the first data line (or "-3" for an empty block).
			do
			{
				if (hasKey(line, "-3"))
					break;
				if (!hasKey(line, "-1"))
					return fail(QStringLiteral("Unexpected record in result block '%1'").arg(block.name) + where());
				const std::int64_t nodeId = fixedInt(line, 3, idWidth, ok);
				if (!ok)
					return fail(QStringLiteral("Bad node id in a result block") + where());
				const auto it = nodeIndex.find(nodeId);
				const int firstValue = 3 + idWidth;
				int have = 0;
				Ln current = line;
				while (true)
				{
					for (int k = 0; current.n >= firstValue + 12 * (k + 1) && have < comps; ++k)
					{
						const double v = fixedDouble(current, firstValue + 12 * k, 12, ok);
						if (!ok)
							return fail(QStringLiteral("Bad value in result block '%1'").arg(block.name) + where());
						if (it != nodeIndex.end())
							block.values[static_cast<std::size_t>(it->second) * static_cast<std::size_t>(comps) + static_cast<std::size_t>(have)] = static_cast<float>(v);
						++have;
					}
					if (have >= comps)
						break;
					// More components than fit one line: continued on " -2" records.
					if (!reader.next(current) || !hasKey(current, "-2"))
						return fail(QStringLiteral("Result block '%1' has a truncated value list").arg(block.name) + where());
				}
			} while (reader.next(line));

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

	if (dataset->cellCount() == 0)
		outcome.warnings << QStringLiteral("The file has no element block, so there is no surface to display.");
	outcome.warnings << resultCellTypeWarnings(*dataset);

	const QString problem = dataset->validate();
	if (!problem.isEmpty())
		return fail(QStringLiteral("Invalid dataset: %1").arg(problem));

	outcome.dataset = std::move(dataset);
	return outcome;
}
