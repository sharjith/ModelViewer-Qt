#include "VtkLegacyReader.h"

#include "VtkDataTypes.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QSysInfo>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>

using namespace vtkio;

namespace
{
	// ---- Type names --------------------------------------------------------------------------------

	VtkType parseLegacyType(const QByteArray& name)
	{
		const QByteArray t = name.toLower();
		if (t == "unsigned_char")  return VtkType::UInt8;
		if (t == "char")           return VtkType::Int8;
		if (t == "unsigned_short") return VtkType::UInt16;
		if (t == "short")          return VtkType::Int16;
		if (t == "unsigned_int")   return VtkType::UInt32;
		if (t == "int")            return VtkType::Int32;
		// 'long' is platform-sized in the writer; VTK's 64-bit writers use 8 bytes, assumed here.
		if (t == "unsigned_long" || t == "vtktypeuint64") return VtkType::UInt64;
		if (t == "long" || t == "vtktypeint64" || t == "vtkidtype") return VtkType::Int64;
		if (t == "float")          return VtkType::Float32;
		if (t == "double")         return VtkType::Float64;
		return VtkType::Invalid; // includes "bit" and "string"
	}

	QString decodeName(const QByteArray& raw)
	{
		// The legacy writer percent-encodes spaces and other awkward characters in array names.
		return QString::fromUtf8(QByteArray::fromPercentEncoding(raw));
	}

	// ---- Byte cursor -------------------------------------------------------------------------------

	struct Cursor
	{
		const QByteArray& buf;
		qint64 pos = 0;
		bool binary = false;
		bool swap = false; // binary data is big-endian; swap when the host is little-endian

		explicit Cursor(const QByteArray& b) : buf(b) {}

		bool atEnd() const { return pos >= buf.size(); }

		// One raw line (without its terminator or a trailing '\r').
		QByteArray readRawLine()
		{
			const qint64 start = pos;
			while (pos < buf.size() && buf[pos] != '\n')
				++pos;
			QByteArray line = buf.mid(start, pos - start);
			if (pos < buf.size())
				++pos; // the '\n'
			if (!line.isEmpty() && line.endsWith('\r'))
				line.chop(1);
			return line;
		}

		// Tokens of the next non-blank line; empty at end of file. Consumes that line including its '\n',
		// so binary data following a keyword line starts exactly at `pos`.
		QList<QByteArray> readLineTokens()
		{
			while (!atEnd())
			{
				const QByteArray simplified = readRawLine().simplified();
				if (!simplified.isEmpty())
					return simplified.split(' ');
			}
			return {};
		}

		bool nextStartsWith(const char* keyword)
		{
			qint64 p = pos;
			if (!binary)
				while (p < buf.size() && std::isspace(static_cast<unsigned char>(buf[p])))
					++p;
			return buf.mid(p, static_cast<int>(std::char_traits<char>::length(keyword))).toUpper() == keyword;
		}

		// `count` values of `type` from the current position: whitespace-separated numbers in ASCII files,
		// packed big-endian bytes in BINARY files.
		template <class Dst>
		bool readValues(qint64 count, VtkType type, std::vector<Dst>& out, QString& err)
		{
			out.clear();
			if (count < 0)
			{
				err = QStringLiteral("negative element count");
				return false;
			}
			if (type == VtkType::Invalid)
			{
				err = QStringLiteral("unsupported data type");
				return false;
			}
			if (binary)
			{
				const qint64 bytes = count * static_cast<qint64>(vtkTypeSize(type));
				if (buf.size() - pos < bytes)
				{
					err = QStringLiteral("binary data is truncated");
					return false;
				}
				if (!convertBinaryBytes(buf.constData() + pos, static_cast<std::size_t>(bytes), type, swap, out, err))
					return false;
				pos += bytes;
				return true;
			}

			if (count > (buf.size() - pos)) // every ASCII value needs at least one character
			{
				err = QStringLiteral("more values declared than the file contains");
				return false;
			}
			out.reserve(static_cast<std::size_t>(count));
			const bool isFloat = vtkTypeIsFloat(type);
			const bool isUnsigned = vtkTypeIsUnsigned(type);
			const char* p = buf.constData() + pos;
			const char* end = buf.constData() + buf.size();
			for (qint64 i = 0; i < count; ++i)
			{
				while (p < end && std::isspace(static_cast<unsigned char>(*p)))
					++p;
				if (p >= end)
				{
					err = QStringLiteral("unexpected end of ASCII data");
					return false;
				}
				char* next = nullptr;
				if (isFloat)
					out.push_back(static_cast<Dst>(std::strtod(p, &next)));
				else if (isUnsigned)
					out.push_back(static_cast<Dst>(std::strtoull(p, &next, 10)));
				else
					out.push_back(static_cast<Dst>(std::strtoll(p, &next, 10)));
				if (next == p)
				{
					err = QStringLiteral("could not parse an ASCII number");
					return false;
				}
				p = next;
			}
			pos = p - buf.constData();
			return true;
		}
	};

	bool cancelled(const std::atomic<bool>* cancel)
	{
		return cancel && cancel->load(std::memory_order_acquire);
	}

	// A cell list as read from CELLS / VERTICES / LINES / POLYGONS / TRIANGLE_STRIPS.
	struct CellList
	{
		std::vector<std::uint32_t> offsets{ 0 }; // cellCount + 1 entries
		std::vector<std::uint32_t> connectivity;
		std::size_t count() const { return offsets.size() - 1; }
	};

	// `header` = [KEYWORD, n, size]. Classic layout: `size` ints of [count, id...] per cell. VTK 5.x layout:
	// OFFSETS (n values) then CONNECTIVITY (size values), each introduced by its own line.
	bool readCellList(Cursor& cur, const QList<QByteArray>& header, CellList& out, QString& err)
	{
		bool ok1 = false, ok2 = false;
		const qint64 n = header.size() > 1 ? header[1].toLongLong(&ok1) : 0;
		const qint64 size = header.size() > 2 ? header[2].toLongLong(&ok2) : 0;
		if (!ok1 || !ok2 || n < 0 || size < 0)
		{
			err = QStringLiteral("malformed %1 header").arg(QString::fromLatin1(header.value(0)));
			return false;
		}
		out = CellList();

		if (cur.nextStartsWith("OFFSETS"))
		{
			const QList<QByteArray> offHdr = cur.readLineTokens();
			std::vector<std::int64_t> offsets;
			if (offHdr.size() < 2 || !cur.readValues(n, parseLegacyType(offHdr[1]), offsets, err))
			{
				if (err.isEmpty())
					err = QStringLiteral("malformed OFFSETS header");
				return false;
			}
			const QList<QByteArray> connHdr = cur.readLineTokens();
			std::vector<std::int64_t> conn;
			if (connHdr.size() < 2 || connHdr[0].toUpper() != "CONNECTIVITY"
				|| !cur.readValues(size, parseLegacyType(connHdr[1]), conn, err))
			{
				if (err.isEmpty())
					err = QStringLiteral("malformed CONNECTIVITY header");
				return false;
			}
			out.offsets.clear();
			for (std::int64_t o : offsets)
				out.offsets.push_back(static_cast<std::uint32_t>(o));
			if (out.offsets.empty())
				out.offsets.push_back(0);
			for (std::int64_t c : conn)
				out.connectivity.push_back(static_cast<std::uint32_t>(c));
			return true;
		}

		std::vector<std::int64_t> flat;
		if (!cur.readValues(size, VtkType::Int32, flat, err))
			return false;
		out.connectivity.reserve(static_cast<std::size_t>(size));
		std::size_t i = 0;
		for (qint64 c = 0; c < n; ++c)
		{
			if (i >= flat.size())
			{
				err = QStringLiteral("cell list ends early");
				return false;
			}
			const std::int64_t count = flat[i++];
			if (count < 0 || i + static_cast<std::size_t>(count) > flat.size())
			{
				err = QStringLiteral("cell %1 overruns the cell list").arg(c);
				return false;
			}
			for (std::int64_t k = 0; k < count; ++k)
				out.connectivity.push_back(static_cast<std::uint32_t>(flat[i++]));
			out.offsets.push_back(static_cast<std::uint32_t>(out.connectivity.size()));
		}
		return true;
	}

	// Implicit cells of a structured grid: hexahedra for a 3D block, quads for a plane, lines for a row.
	void buildStructuredCells(const int dims[3], ResultDataset& ds)
	{
		int axes[3];
		int used = 0;
		for (int a = 0; a < 3; ++a)
			if (dims[a] > 1)
				axes[used++] = a;
		auto node = [&](int i, int j, int k) -> std::uint32_t
		{
			return static_cast<std::uint32_t>(i + dims[0] * (j + static_cast<std::int64_t>(dims[1]) * k));
		};
		auto addCell = [&](ResultCellType type, std::initializer_list<std::uint32_t> nodes)
		{
			ds.cellTypes.push_back(type);
			for (std::uint32_t n : nodes)
				ds.cellConnectivity.push_back(n);
			ds.cellOffsets.push_back(static_cast<std::uint32_t>(ds.cellConnectivity.size()));
		};
		ds.cellOffsets.assign(1, 0);
		if (used == 3)
		{
			for (int k = 0; k + 1 < dims[2]; ++k)
				for (int j = 0; j + 1 < dims[1]; ++j)
					for (int i = 0; i + 1 < dims[0]; ++i)
						addCell(ResultCellType::Hexahedron,
							{ node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k),
							  node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1) });
		}
		else if (used == 2)
		{
			const int a = axes[0], b = axes[1];
			auto at = [&](int u, int v) -> std::uint32_t
			{
				int idx[3] = { 0, 0, 0 };
				idx[a] = u;
				idx[b] = v;
				return node(idx[0], idx[1], idx[2]);
			};
			for (int v = 0; v + 1 < dims[b]; ++v)
				for (int u = 0; u + 1 < dims[a]; ++u)
					addCell(ResultCellType::Quad, { at(u, v), at(u + 1, v), at(u + 1, v + 1), at(u, v + 1) });
		}
		else if (used == 1)
		{
			const int a = axes[0];
			for (int u = 0; u + 1 < dims[a]; ++u)
			{
				int i0[3] = { 0, 0, 0 }, i1[3] = { 0, 0, 0 };
				i0[a] = u;
				i1[a] = u + 1;
				addCell(ResultCellType::Line, { node(i0[0], i0[1], i0[2]), node(i1[0], i1[1], i1[2]) });
			}
		}
	}
}

ResultReadOutcome readVtkLegacy(const QString& path, const std::atomic<bool>* cancel)
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

	Cursor cur(all);
	cur.swap = QSysInfo::ByteOrder == QSysInfo::LittleEndian;

	// ---- Header: version line, title line, ASCII|BINARY ---------------------------------------------
	const QByteArray versionLine = cur.readRawLine();
	if (!versionLine.trimmed().toLower().startsWith("# vtk datafile"))
		return fail(QStringLiteral("Not a legacy VTK file (missing '# vtk DataFile Version' line)."));
	const QByteArray titleLine = cur.readRawLine();
	{
		const QByteArray titleAsFormat = titleLine.trimmed().toUpper();
		if (titleAsFormat == "ASCII" || titleAsFormat == "BINARY") // a file with no title line
			cur.binary = titleAsFormat == "BINARY";
		else
		{
			const QList<QByteArray> fmt = cur.readLineTokens();
			if (fmt.isEmpty() || (fmt[0].toUpper() != "ASCII" && fmt[0].toUpper() != "BINARY"))
				return fail(QStringLiteral("Missing ASCII/BINARY format line."));
			cur.binary = fmt[0].toUpper() == "BINARY";
		}
	}

	// ---- DATASET -----------------------------------------------------------------------------------
	QList<QByteArray> tokens = cur.readLineTokens();
	if (tokens.isEmpty() || tokens[0].toUpper() != "DATASET")
		return fail(QStringLiteral("The file has no DATASET section (an attribute-only file that must be paired "
		                           "with a geometry file is not supported)."));
	if (tokens.size() < 2)
		return fail(QStringLiteral("Malformed DATASET line."));
	const QByteArray datasetType = tokens[1].toUpper();
	const bool isUnstructured = datasetType == "UNSTRUCTURED_GRID";
	const bool isPolyData = datasetType == "POLYDATA";
	const bool isStructuredGrid = datasetType == "STRUCTURED_GRID";
	const bool isStructuredPoints = datasetType == "STRUCTURED_POINTS";
	const bool isRectilinear = datasetType == "RECTILINEAR_GRID";
	if (!isUnstructured && !isPolyData && !isStructuredGrid && !isStructuredPoints && !isRectilinear)
		return fail(QStringLiteral("Unsupported dataset type '%1'.").arg(QString::fromLatin1(datasetType)));

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;

	int dims[3] = { 0, 0, 0 };
	bool haveDims = false;
	double origin[3] = { 0, 0, 0 };
	double spacing[3] = { 1, 1, 1 };
	std::vector<float> rectCoords[3];
	bool havePoints = false;

	CellList unstructuredCells;
	std::vector<std::int32_t> unstructuredTypes;
	bool haveCells = false, haveTypes = false;
	CellList polyLists[4]; // vertices, lines, polygons, triangle strips (VTK's cell order for polydata)

	enum class Assoc { None, Node, Cell } assoc = Assoc::None;
	qint64 assocCount = 0;
	double timeValue = 0.0;
	QString err;

	auto addField = [&](const QString& name, int comps, std::vector<float>&& values)
	{
		ResultField f;
		f.name = name;
		f.association = assoc == Assoc::Node ? ResultFieldAssociation::Node : ResultFieldAssociation::Cell;
		f.components = comps;
		f.stepData.push_back(std::move(values));
		dataset->fields.push_back(std::move(f));
	};

	// FIELD name nArrays, then `name numComp numTuples type` + data per array.
	auto readFieldArrays = [&](const QList<QByteArray>& hdr) -> bool
	{
		bool ok = false;
		const qint64 nArrays = hdr.size() > 2 ? hdr[2].toLongLong(&ok) : 0;
		if (!ok || nArrays < 0)
		{
			err = QStringLiteral("malformed FIELD header");
			return false;
		}
		for (qint64 a = 0; a < nArrays; ++a)
		{
			const QList<QByteArray> ah = cur.readLineTokens();
			bool okC = false, okT = false;
			const qint64 comps = ah.size() > 2 ? ah[1].toLongLong(&okC) : 0;
			const qint64 tuples = ah.size() > 3 ? ah[2].toLongLong(&okT) : 0;
			if (ah.size() < 4 || !okC || !okT || comps <= 0 || tuples < 0)
			{
				err = QStringLiteral("malformed FIELD array header");
				return false;
			}
			std::vector<float> values;
			if (!cur.readValues(comps * tuples, parseLegacyType(ah[3]), values, err))
				return false;
			const QString name = decodeName(ah[0]);
			if (assoc == Assoc::None)
			{
				if (name == QLatin1String("TIME") || name == QLatin1String("TimeValue"))
				{
					if (!values.empty())
						timeValue = values.front();
				}
				continue; // dataset-level field data: metadata, ignored
			}
			if (tuples != assocCount)
			{
				outcome.warnings << QStringLiteral("Ignored field-data array '%1' (%2 tuples, expected %3).")
					.arg(name).arg(tuples).arg(assocCount);
				continue;
			}
			addField(name, static_cast<int>(comps), std::move(values));
		}
		return true;
	};

	// ---- Section loop --------------------------------------------------------------------------------
	while (true)
	{
		if (cancelled(cancel))
			return fail(QStringLiteral("cancelled"));
		tokens = cur.readLineTokens();
		if (tokens.isEmpty())
			break;
		const QByteArray kw = tokens[0].toUpper();
		bool ok = false;

		if (kw == "POINTS")
		{
			const qint64 n = tokens.size() > 1 ? tokens[1].toLongLong(&ok) : 0;
			if (!ok || tokens.size() < 3)
				return fail(QStringLiteral("Malformed POINTS line."));
			if (!cur.readValues(n * 3, parseLegacyType(tokens[2]), dataset->nodePositions, err))
				return fail(QStringLiteral("Reading points: %1").arg(err));
			havePoints = true;
		}
		else if (kw == "CELLS" && isUnstructured)
		{
			if (!readCellList(cur, tokens, unstructuredCells, err))
				return fail(QStringLiteral("Reading cells: %1").arg(err));
			haveCells = true;
		}
		else if (kw == "CELL_TYPES" && isUnstructured)
		{
			const qint64 n = tokens.size() > 1 ? tokens[1].toLongLong(&ok) : 0;
			if (!ok)
				return fail(QStringLiteral("Malformed CELL_TYPES line."));
			if (!cur.readValues(n, VtkType::Int32, unstructuredTypes, err))
				return fail(QStringLiteral("Reading cell types: %1").arg(err));
			haveTypes = true;
		}
		else if (isPolyData && (kw == "VERTICES" || kw == "LINES" || kw == "POLYGONS" || kw == "TRIANGLE_STRIPS"))
		{
			const int slot = kw == "VERTICES" ? 0 : kw == "LINES" ? 1 : kw == "POLYGONS" ? 2 : 3;
			if (!readCellList(cur, tokens, polyLists[slot], err))
				return fail(QStringLiteral("Reading %1: %2").arg(QString::fromLatin1(kw), err));
		}
		else if (kw == "DIMENSIONS")
		{
			if (tokens.size() < 4)
				return fail(QStringLiteral("Malformed DIMENSIONS line."));
			for (int a = 0; a < 3; ++a)
				dims[a] = tokens[1 + a].toInt();
			haveDims = dims[0] > 0 && dims[1] > 0 && dims[2] > 0;
			if (!haveDims)
				return fail(QStringLiteral("Invalid DIMENSIONS."));
		}
		else if (kw == "ORIGIN" || kw == "SPACING" || kw == "ASPECT_RATIO")
		{
			if (tokens.size() < 4)
				return fail(QStringLiteral("Malformed %1 line.").arg(QString::fromLatin1(kw)));
			double* target = kw == "ORIGIN" ? origin : spacing;
			for (int a = 0; a < 3; ++a)
				target[a] = tokens[1 + a].toDouble();
		}
		else if (kw == "X_COORDINATES" || kw == "Y_COORDINATES" || kw == "Z_COORDINATES")
		{
			const int axis = kw[0] == 'X' ? 0 : kw[0] == 'Y' ? 1 : 2;
			const qint64 n = tokens.size() > 1 ? tokens[1].toLongLong(&ok) : 0;
			if (!ok || tokens.size() < 3)
				return fail(QStringLiteral("Malformed %1 line.").arg(QString::fromLatin1(kw)));
			if (!cur.readValues(n, parseLegacyType(tokens[2]), rectCoords[axis], err))
				return fail(QStringLiteral("Reading %1: %2").arg(QString::fromLatin1(kw), err));
		}
		else if (kw == "POINT_DATA" || kw == "CELL_DATA")
		{
			assocCount = tokens.size() > 1 ? tokens[1].toLongLong(&ok) : 0;
			if (!ok || assocCount < 0)
				return fail(QStringLiteral("Malformed %1 line.").arg(QString::fromLatin1(kw)));
			assoc = kw == "POINT_DATA" ? Assoc::Node : Assoc::Cell;
		}
		else if (kw == "FIELD")
		{
			if (!readFieldArrays(tokens))
				return fail(QStringLiteral("Reading FIELD data: %1").arg(err));
		}
		else if (kw == "METADATA")
		{
			// Informational key/value block, terminated by a blank line.
			while (!cur.atEnd() && !cur.readRawLine().trimmed().isEmpty())
			{
			}
		}
		else if (assoc != Assoc::None
		         && (kw == "SCALARS" || kw == "VECTORS" || kw == "NORMALS" || kw == "TENSORS"
		             || kw == "TEXTURE_COORDINATES" || kw == "COLOR_SCALARS" || kw == "LOOKUP_TABLE"))
		{
			if (tokens.size() < 3)
				return fail(QStringLiteral("Malformed %1 line.").arg(QString::fromLatin1(kw)));
			const QString name = decodeName(tokens[1]);
			std::vector<float> values;
			if (kw == "SCALARS")
			{
				int comps = 1;
				if (tokens.size() > 3)
					comps = std::max(1, tokens[3].toInt());
				if (cur.nextStartsWith("LOOKUP_TABLE")) // the table line is mandatory in the spec; be lenient
					cur.readLineTokens();
				if (!cur.readValues(assocCount * comps, parseLegacyType(tokens[2]), values, err))
					return fail(QStringLiteral("Reading scalars '%1': %2").arg(name, err));
				addField(name, comps, std::move(values));
			}
			else if (kw == "VECTORS" || kw == "NORMALS" || kw == "TENSORS")
			{
				const int comps = kw == "TENSORS" ? 9 : 3;
				if (!cur.readValues(assocCount * comps, parseLegacyType(tokens[2]), values, err))
					return fail(QStringLiteral("Reading %1 '%2': %3").arg(QString::fromLatin1(kw), name, err));
				addField(name, comps, std::move(values));
			}
			else if (kw == "TEXTURE_COORDINATES")
			{
				const int comps = std::max(1, tokens[2].toInt());
				if (tokens.size() < 4 || !cur.readValues(assocCount * comps, parseLegacyType(tokens[3]), values, err))
					return fail(QStringLiteral("Reading texture coordinates '%1': %2").arg(name, err));
				addField(name, comps, std::move(values));
			}
			else if (kw == "COLOR_SCALARS")
			{
				const int comps = std::max(1, tokens[2].toInt());
				if (cur.binary)
				{
					std::vector<float> bytes;
					if (!cur.readValues(assocCount * comps, VtkType::UInt8, bytes, err))
						return fail(QStringLiteral("Reading colors '%1': %2").arg(name, err));
					for (float& b : bytes)
						b /= 255.0f;
					values = std::move(bytes);
				}
				else if (!cur.readValues(assocCount * comps, VtkType::Float32, values, err))
					return fail(QStringLiteral("Reading colors '%1': %2").arg(name, err));
				addField(name, comps, std::move(values));
			}
			else // standalone LOOKUP_TABLE name size: RGBA entries, not needed for display
			{
				const qint64 size = tokens[2].toLongLong(&ok);
				std::vector<float> ignored;
				if (!ok || !cur.readValues(size * 4, cur.binary ? VtkType::UInt8 : VtkType::Float32, ignored, err))
					return fail(QStringLiteral("Reading lookup table '%1': %2").arg(name, err));
			}
		}
		else
		{
			outcome.warnings << QStringLiteral("Skipped unrecognised section '%1'.").arg(QString::fromLatin1(tokens[0]));
		}
	}

	// ---- Assemble geometry ---------------------------------------------------------------------------
	if (isStructuredPoints || isRectilinear)
	{
		if (!haveDims)
			return fail(QStringLiteral("The structured dataset has no DIMENSIONS."));
		std::vector<double> axisValues[3];
		for (int a = 0; a < 3; ++a)
		{
			if (isRectilinear)
			{
				if (rectCoords[a].size() != static_cast<std::size_t>(dims[a]))
					return fail(QStringLiteral("Rectilinear coordinate count does not match DIMENSIONS."));
				axisValues[a].assign(rectCoords[a].begin(), rectCoords[a].end());
			}
			else
				for (int i = 0; i < dims[a]; ++i)
					axisValues[a].push_back(origin[a] + spacing[a] * i);
		}
		dataset->nodePositions.clear();
		dataset->nodePositions.reserve(static_cast<std::size_t>(dims[0]) * dims[1] * dims[2] * 3);
		for (int k = 0; k < dims[2]; ++k)
			for (int j = 0; j < dims[1]; ++j)
				for (int i = 0; i < dims[0]; ++i)
				{
					dataset->nodePositions.push_back(static_cast<float>(axisValues[0][i]));
					dataset->nodePositions.push_back(static_cast<float>(axisValues[1][j]));
					dataset->nodePositions.push_back(static_cast<float>(axisValues[2][k]));
				}
		buildStructuredCells(dims, *dataset);
	}
	else
	{
		if (!havePoints)
			return fail(QStringLiteral("The file has no POINTS section."));
		if (isStructuredGrid)
		{
			if (!haveDims)
				return fail(QStringLiteral("The structured grid has no DIMENSIONS."));
			if (dataset->nodeCount() != static_cast<std::size_t>(dims[0]) * dims[1] * dims[2])
				return fail(QStringLiteral("POINTS count does not match DIMENSIONS."));
			buildStructuredCells(dims, *dataset);
		}
		else if (isUnstructured)
		{
			if (!haveCells || !haveTypes)
				return fail(QStringLiteral("The unstructured grid is missing CELLS or CELL_TYPES."));
			if (unstructuredTypes.size() != unstructuredCells.count())
				return fail(QStringLiteral("CELLS declares %1 cells but CELL_TYPES has %2.")
					.arg(unstructuredCells.count()).arg(unstructuredTypes.size()));
			dataset->cellOffsets = std::move(unstructuredCells.offsets);
			dataset->cellConnectivity = std::move(unstructuredCells.connectivity);
			dataset->cellTypes.reserve(unstructuredTypes.size());
			for (std::int32_t t : unstructuredTypes)
				dataset->cellTypes.push_back(resultCellTypeFromVtk(t));
		}
		else // polydata: cells in VTK order - vertices, lines, polygons, strips
		{
			dataset->cellOffsets.assign(1, 0);
			for (int slot = 0; slot < 4; ++slot)
			{
				const CellList& list = polyLists[slot];
				for (std::size_t c = 0; c < list.count(); ++c)
				{
					const std::uint32_t begin = list.offsets[c], end = list.offsets[c + 1];
					const std::uint32_t n = end - begin;
					ResultCellType type = ResultCellType::Unsupported; // vertices, polylines, strips, big polygons
					if (slot == 1 && n == 2)
						type = ResultCellType::Line;
					else if (slot == 2 && n == 3)
						type = ResultCellType::Triangle;
					else if (slot == 2 && n == 4)
						type = ResultCellType::Quad;
					dataset->cellTypes.push_back(type);
					dataset->cellConnectivity.insert(dataset->cellConnectivity.end(),
						list.connectivity.begin() + begin, list.connectivity.begin() + end);
					dataset->cellOffsets.push_back(static_cast<std::uint32_t>(dataset->cellConnectivity.size()));
				}
			}
		}
	}

	// Drop attribute arrays whose tuple count does not match (kept out of validate()'s hard failure).
	{
		const std::size_t nodes = dataset->nodeCount(), cells = dataset->cellCount();
		std::vector<ResultField> kept;
		for (ResultField& f : dataset->fields)
		{
			const std::size_t expected = f.association == ResultFieldAssociation::Node ? nodes : cells;
			if (f.tupleCount(0) == expected)
				kept.push_back(std::move(f));
			else
				outcome.warnings << QStringLiteral("Ignored %1 field '%2': %3 tuples, expected %4.")
					.arg(f.association == ResultFieldAssociation::Node ? QStringLiteral("point") : QStringLiteral("cell"),
					     f.name).arg(f.tupleCount(0)).arg(expected);
		}
		dataset->fields = std::move(kept);
	}

	ResultStep step;
	step.time = timeValue;
	dataset->steps.push_back(step);

	outcome.warnings << resultCellTypeWarnings(*dataset);

	const QString problem = dataset->validate();
	if (!problem.isEmpty())
		return fail(QStringLiteral("Invalid dataset: %1").arg(problem));

	outcome.dataset = std::move(dataset);
	return outcome;
}
