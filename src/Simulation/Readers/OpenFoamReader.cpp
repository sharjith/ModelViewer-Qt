#include "OpenFoamReader.h"

#include "ResultUnits.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>

namespace
{
	// ---- A tiny cursor over an OpenFOAM text file (the data is a QByteArray, so it is null-terminated) -------------

	struct Cursor
	{
		const char* p = nullptr;
		const char* end = nullptr;
		bool atEnd() const { return p >= end; }
	};

	// Whitespace and both comment styles.
	void skipSpace(Cursor& c)
	{
		for (;;)
		{
			while (c.p < c.end && static_cast<unsigned char>(*c.p) <= ' ')
				++c.p;
			if (c.p + 1 < c.end && c.p[0] == '/' && c.p[1] == '/')
			{
				while (c.p < c.end && *c.p != '\n')
					++c.p;
				continue;
			}
			if (c.p + 1 < c.end && c.p[0] == '/' && c.p[1] == '*')
			{
				c.p += 2;
				while (c.p + 1 < c.end && !(c.p[0] == '*' && c.p[1] == '/'))
					++c.p;
				c.p = std::min(c.end, c.p + 2);
				continue;
			}
			break;
		}
	}

	bool isDelimiter(char ch)
	{
		return static_cast<unsigned char>(ch) <= ' ' || ch == '(' || ch == ')' || ch == ';' || ch == '{' || ch == '}' || ch == '[' || ch == ']';
	}

	bool peekChar(Cursor& c, char ch)
	{
		skipSpace(c);
		return !c.atEnd() && *c.p == ch;
	}

	bool expectChar(Cursor& c, char ch)
	{
		if (!peekChar(c, ch))
			return false;
		++c.p;
		return true;
	}

	// A word up to the next delimiter; a quoted string ("LSB;label=32") is one word, quotes included.
	QByteArray readWord(Cursor& c)
	{
		skipSpace(c);
		const char* start = c.p;
		if (c.p < c.end && *c.p == '"')
		{
			++c.p;
			while (c.p < c.end && *c.p != '"')
				++c.p;
			if (c.p < c.end)
				++c.p;
			return QByteArray(start, static_cast<qsizetype>(c.p - start));
		}
		while (c.p < c.end && !isDelimiter(*c.p))
			++c.p;
		return QByteArray(start, static_cast<qsizetype>(c.p - start));
	}

	bool readInt(Cursor& c, long& value)
	{
		skipSpace(c);
		if (c.atEnd())
			return false;
		char* stop = nullptr;
		value = std::strtol(c.p, &stop, 10);
		if (stop == c.p || stop > c.end)
			return false;
		c.p = stop;
		return true;
	}

	bool readDouble(Cursor& c, double& value)
	{
		skipSpace(c);
		if (c.atEnd())
			return false;
		char* stop = nullptr;
		value = std::strtod(c.p, &stop);
		if (stop == c.p || stop > c.end)
			return false;
		c.p = stop;
		return true;
	}

	// A count can never exceed the bytes left (every entry takes at least one), which stops a corrupt count from
	// reserving gigabytes.
	bool plausibleCount(const Cursor& c, long n) { return n >= 0 && static_cast<std::size_t>(n) <= static_cast<std::size_t>(c.end - c.p); }

	// ---- FoamFile header -------------------------------------------------------------------------------------------

	struct FoamHeader
	{
		QByteArray format;
		QByteArray className;
		bool ok = false;
	};

	FoamHeader readHeader(Cursor& c)
	{
		FoamHeader header;
		if (readWord(c) != "FoamFile" || !expectChar(c, '{'))
			return header;
		while (!c.atEnd())
		{
			skipSpace(c);
			if (!c.atEnd() && *c.p == '}')
			{
				++c.p;
				header.ok = true;
				return header;
			}
			const QByteArray key = readWord(c);
			if (key.isEmpty())
				return header;
			QByteArray value;
			skipSpace(c);
			while (!c.atEnd() && *c.p != ';' && *c.p != '}')
			{
				const QByteArray word = readWord(c);
				if (word.isEmpty())
					++c.p; // a stray delimiter: never stall
				else
					value += word;
				skipSpace(c);
			}
			expectChar(c, ';');
			if (key == "format")
				header.format = value;
			else if (key == "class")
				header.className = value;
		}
		return header;
	}

	// Reads a whole file, its header and leaves the cursor after the header. Empty `error` on success.
	bool openFoamFile(const QString& path, QByteArray& data, Cursor& cursor, FoamHeader& header, QString& error)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			error = QStringLiteral("Cannot open '%1'.").arg(QDir::toNativeSeparators(path));
			return false;
		}
		data = file.readAll();
		cursor.p = data.constData();
		cursor.end = data.constData() + data.size();
		header = readHeader(cursor);
		if (!header.ok)
		{
			error = QStringLiteral("'%1' does not start with an OpenFOAM FoamFile header.").arg(QDir::toNativeSeparators(path));
			return false;
		}
		if (header.format.toLower() != "ascii")
		{
			error = QStringLiteral("'%1' is not in ASCII format (%2). Binary OpenFOAM files are not supported: set "
			                       "writeFormat to ascii in system/controlDict and write the case again, or export with foamToVTK.")
			            .arg(QDir::toNativeSeparators(path), QString::fromLatin1(header.format));
			return false;
		}
		return true;
	}

	// ---- Mesh lists ------------------------------------------------------------------------------------------------

	bool readLabelList(Cursor& c, std::vector<std::uint32_t>& out)
	{
		long n = 0;
		if (!readInt(c, n) || !plausibleCount(c, n) || !expectChar(c, '('))
			return false;
		out.resize(static_cast<std::size_t>(n));
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			long v = 0;
			if (!readInt(c, v) || v < 0)
				return false;
			out[i] = static_cast<std::uint32_t>(v);
		}
		return expectChar(c, ')');
	}

	bool readPoints(Cursor& c, std::vector<float>& out)
	{
		long n = 0;
		if (!readInt(c, n) || !plausibleCount(c, n) || !expectChar(c, '('))
			return false;
		out.resize(static_cast<std::size_t>(n) * 3);
		for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i)
		{
			double x, y, z;
			if (!expectChar(c, '(') || !readDouble(c, x) || !readDouble(c, y) || !readDouble(c, z) || !expectChar(c, ')'))
				return false;
			out[i * 3] = static_cast<float>(x);
			out[i * 3 + 1] = static_cast<float>(y);
			out[i * 3 + 2] = static_cast<float>(z);
		}
		return expectChar(c, ')');
	}

	// faces: N ( 4(0 1 2 3) 3(4 5 6) ... ) -> flat node list + per-face start offsets (size N + 1)
	bool readFaces(Cursor& c, std::vector<std::uint32_t>& nodes, std::vector<std::uint32_t>& offsets)
	{
		long n = 0;
		if (!readInt(c, n) || !plausibleCount(c, n) || !expectChar(c, '('))
			return false;
		offsets.assign(1, 0);
		offsets.reserve(static_cast<std::size_t>(n) + 1);
		nodes.reserve(static_cast<std::size_t>(n) * 4);
		for (long i = 0; i < n; ++i)
		{
			long k = 0;
			if (!readInt(c, k) || k < 3 || !plausibleCount(c, k) || !expectChar(c, '('))
				return false;
			for (long j = 0; j < k; ++j)
			{
				long v = 0;
				if (!readInt(c, v) || v < 0)
					return false;
				nodes.push_back(static_cast<std::uint32_t>(v));
			}
			if (!expectChar(c, ')'))
				return false;
			offsets.push_back(static_cast<std::uint32_t>(nodes.size()));
		}
		return expectChar(c, ')');
	}

	// ---- Fields ----------------------------------------------------------------------------------------------------

	int componentsForClass(const QByteArray& className)
	{
		if (className == "volScalarField" || className == "volSphericalTensorField")
			return 1;
		if (className == "volVectorField")
			return 3;
		if (className == "volSymmTensorField")
			return 6;
		if (className == "volTensorField")
			return 9;
		return 0;
	}

	struct ParsedField
	{
		int components = 0;
		std::vector<int> dimensions; // 7 entries when present
		std::vector<float> values;   // cellCount * components
	};

	// Reads `count` tuples of `components` numbers: a bare number for a scalar, "(a b c)" otherwise.
	bool readTuple(Cursor& c, int components, float* out)
	{
		double v = 0.0;
		if (components == 1)
		{
			if (!readDouble(c, v))
				return false;
			out[0] = static_cast<float>(v);
			return true;
		}
		if (!expectChar(c, '('))
			return false;
		for (int k = 0; k < components; ++k)
		{
			if (!readDouble(c, v))
				return false;
			out[k] = static_cast<float>(v);
		}
		return expectChar(c, ')');
	}

	// Skips one top-level entry we do not need: up to its ';' or over a { } / ( ) block.
	void skipEntry(Cursor& c)
	{
		for (;;)
		{
			skipSpace(c);
			if (c.atEnd())
				return;
			const char ch = *c.p;
			if (ch == ';')
			{
				++c.p;
				return;
			}
			if (ch == '{' || ch == '(')
			{
				const char open = ch, close = ch == '{' ? '}' : ')';
				int depth = 0;
				while (!c.atEnd())
				{
					if (*c.p == open)
						++depth;
					else if (*c.p == close && --depth == 0)
					{
						++c.p;
						break;
					}
					++c.p;
				}
				return;
			}
			if (readWord(c).isEmpty())
				++c.p; // a stray delimiter (')', ']', '}'): never stall
		}
	}

	// dimensions [0 1 -1 0 0 0 0];
	bool readDimensions(Cursor& c, std::vector<int>& dims)
	{
		if (!expectChar(c, '['))
			return false;
		dims.clear();
		long v = 0;
		while (readInt(c, v))
			dims.push_back(static_cast<int>(v));
		return expectChar(c, ']');
	}

	// Parses one field file whose header has been read; returns false with `reason` when it cannot be used.
	bool readFieldBody(Cursor& c, int components, std::size_t cellCount, ParsedField& out, QString& reason)
	{
		out.components = components;
		while (!c.atEnd())
		{
			const QByteArray word = readWord(c);
			if (word.isEmpty())
			{
				if (c.atEnd())
					break;
				++c.p; // a stray delimiter
				continue;
			}
			if (word == "dimensions")
			{
				readDimensions(c, out.dimensions);
				expectChar(c, ';');
				continue;
			}
			if (word != "internalField")
			{
				skipEntry(c);
				continue;
			}
			const QByteArray kind = readWord(c);
			out.values.assign(cellCount * static_cast<std::size_t>(components), 0.0f);
			if (kind == "uniform")
			{
				std::vector<float> tuple(static_cast<std::size_t>(components));
				if (!readTuple(c, components, tuple.data()))
				{
					reason = QStringLiteral("its uniform value cannot be read");
					return false;
				}
				for (std::size_t cell = 0; cell < cellCount; ++cell)
					std::copy(tuple.begin(), tuple.end(), out.values.begin() + static_cast<std::ptrdiff_t>(cell * static_cast<std::size_t>(components)));
				return true;
			}
			if (kind != "nonuniform")
			{
				reason = QStringLiteral("its internalField is neither uniform nor nonuniform");
				return false;
			}
			skipSpace(c);
			if (!c.atEnd() && !((*c.p >= '0' && *c.p <= '9') || *c.p == '-'))
				readWord(c); // the List<type> word, absent in the short form "nonuniform 0()"
			long n = 0;
			if (!readInt(c, n) || !plausibleCount(c, n))
			{
				reason = QStringLiteral("its value list is malformed");
				return false;
			}
			if (static_cast<std::size_t>(n) != cellCount)
			{
				reason = QStringLiteral("it has %1 values but the mesh has %2 cells").arg(n).arg(cellCount);
				return false;
			}
			if (!expectChar(c, '('))
			{
				reason = QStringLiteral("its value list is malformed");
				return false;
			}
			for (std::size_t cell = 0; cell < cellCount; ++cell)
			{
				if (!readTuple(c, components, out.values.data() + cell * static_cast<std::size_t>(components)))
				{
					reason = QStringLiteral("its value list is malformed (at cell %1)").arg(cell);
					return false;
				}
			}
			return true;
		}
		reason = QStringLiteral("it has no internalField");
		return false;
	}

	// OpenFOAM's symmetric tensor is XX XY XZ YY YZ ZZ; every tensor field in this app is XX YY ZZ XY YZ ZX.
	void reorderSymmTensor(std::vector<float>& values)
	{
		for (std::size_t i = 0; i + 5 < values.size(); i += 6)
		{
			const float xx = values[i], xy = values[i + 1], xz = values[i + 2], yy = values[i + 3], yz = values[i + 4], zz = values[i + 5];
			values[i] = xx;
			values[i + 1] = yy;
			values[i + 2] = zz;
			values[i + 3] = xy;
			values[i + 4] = yz;
			values[i + 5] = xz;
		}
	}

	// dimensions [kg m s K mol A cd] -> a quantity kind and its SI unit, only where it is unambiguous.
	bool kindFromDimensions(const std::vector<int>& d, QString& kind)
	{
		if (d.size() < 4)
			return false;
		auto is = [&](int m, int l, int t, int k) { return d[0] == m && d[1] == l && d[2] == t && d[3] == k; };
		for (std::size_t i = 4; i < d.size(); ++i)
			if (d[i] != 0)
				return false;
		if (is(1, -1, -2, 0))  kind = QStringLiteral("pressure");     // Pa (not the kinematic pressure m^2/s^2)
		else if (is(0, 1, -1, 0)) kind = QStringLiteral("velocity");  // m/s
		else if (is(0, 0, 0, 1))  kind = QStringLiteral("temperature"); // K
		else if (is(0, 1, 0, 0))  kind = QStringLiteral("length");    // m
		else if (is(1, -3, 0, 0)) kind = QStringLiteral("density");   // kg/m^3
		else return false;
		return true;
	}

	bool isTimeDirectoryName(const QString& name)
	{
		static const QRegularExpression pattern(QStringLiteral("^[+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?$"));
		return pattern.match(name).hasMatch();
	}

	QStringList fieldFileNames(const QDir& timeDir)
	{
		QStringList names;
		for (const QFileInfo& info : timeDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name))
			if (info.suffix().isEmpty()) // fields have no extension (p, U, T, ...)
				names << info.fileName();
		return names;
	}
}

ResultReadOutcome readOpenFoamCase(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;
	auto cancelled = [cancel]() { return cancel && cancel->load(std::memory_order_acquire); };
	auto fail = [&outcome](const QString& message) {
		outcome.error = message;
		return std::move(outcome);
	};

	const QDir caseDir = QFileInfo(path).absoluteDir();
	const QDir meshDir(caseDir.filePath(QStringLiteral("constant/polyMesh")));
	if (!meshDir.exists())
	{
		if (QDir(caseDir.filePath(QStringLiteral("processor0"))).exists())
			return fail(QStringLiteral("This is a decomposed OpenFOAM case (processor0 ...). Reconstruct it first with reconstructPar."));
		return fail(QStringLiteral("'%1' is not next to an OpenFOAM case: there is no constant/polyMesh directory.")
		                .arg(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())));
	}

	// ---- Mesh ------------------------------------------------------------------------------------------------------
	std::vector<float> points;
	std::vector<std::uint32_t> faceNodes, faceOffsets, owner, neighbour;
	for (const char* name : { "points", "faces", "owner", "neighbour" })
	{
		if (cancelled())
			return fail(QStringLiteral("cancelled"));
		const QString file = meshDir.filePath(QString::fromLatin1(name));
		if (!QFileInfo::exists(file))
		{
			if (QFileInfo::exists(file + QStringLiteral(".gz")))
				return fail(QStringLiteral("The mesh files are compressed (%1.gz). Compressed OpenFOAM files are not supported: set "
				                           "writeCompression off in system/controlDict and write the mesh again.").arg(QString::fromLatin1(name)));
			return fail(QStringLiteral("The mesh file '%1' is missing from constant/polyMesh.").arg(QString::fromLatin1(name)));
		}
		QByteArray data;
		Cursor cursor;
		FoamHeader header;
		QString error;
		if (!openFoamFile(file, data, cursor, header, error))
			return fail(error);
		bool ok = false;
		const QByteArray meshName(name);
		if (meshName == "points")
			ok = readPoints(cursor, points);
		else if (meshName == "faces")
			ok = readFaces(cursor, faceNodes, faceOffsets);
		else if (meshName == "owner")
			ok = readLabelList(cursor, owner);
		else
			ok = readLabelList(cursor, neighbour);
		if (!ok)
			return fail(QStringLiteral("The mesh file '%1' is malformed.").arg(QString::fromLatin1(name)));
	}

	const std::size_t nodeCount = points.size() / 3;
	const std::size_t faceCount = faceOffsets.empty() ? 0 : faceOffsets.size() - 1;
	if (owner.size() != faceCount)
		return fail(QStringLiteral("The mesh is inconsistent: %1 faces but %2 owner entries.").arg(faceCount).arg(owner.size()));
	if (neighbour.size() > faceCount)
		return fail(QStringLiteral("The mesh is inconsistent: more neighbour entries (%1) than faces (%2).").arg(neighbour.size()).arg(faceCount));
	if (faceCount == 0 || nodeCount == 0)
		return fail(QStringLiteral("The mesh is empty."));
	for (std::uint32_t node : faceNodes)
		if (node >= nodeCount)
			return fail(QStringLiteral("The mesh is inconsistent: a face references node %1 but there are only %2 points.").arg(node).arg(nodeCount));

	std::size_t cellCount = 0;
	for (std::uint32_t c : owner)
		cellCount = std::max<std::size_t>(cellCount, static_cast<std::size_t>(c) + 1);
	for (std::uint32_t c : neighbour)
		cellCount = std::max<std::size_t>(cellCount, static_cast<std::size_t>(c) + 1);

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("OpenFOAM");
	dataset->lengthUnit = QStringLiteral("m"); // OpenFOAM is SI; blockMesh's convertToMeters is already applied to the points
	dataset->nodePositions = std::move(points);
	dataset->cellTypes.assign(cellCount, ResultCellType::Polyhedron);
	dataset->cellOffsets.assign(cellCount + 1, 0);

	// The boundary is the faces after the internal ones (those that have a neighbour); each belongs to its owner.
	const std::size_t internalFaces = neighbour.size();
	for (std::size_t f = internalFaces; f < faceCount; ++f)
	{
		const std::uint32_t begin = faceOffsets[f], end = faceOffsets[f + 1];
		for (std::uint32_t i = begin + 1; i + 1 < end; ++i) // fan from the first node
		{
			dataset->boundaryTriangles.push_back(faceNodes[begin]);
			dataset->boundaryTriangles.push_back(faceNodes[i]);
			dataset->boundaryTriangles.push_back(faceNodes[i + 1]);
			dataset->boundaryTriangleCells.push_back(owner[f]);
		}
	}
	if (dataset->boundaryTriangles.empty())
		return fail(QStringLiteral("The mesh has no boundary faces."));

	// ---- Time directories and their fields -------------------------------------------------------------------------
	struct TimeDir
	{
		double time;
		QString name;
	};
	std::vector<TimeDir> times;
	for (const QString& name : caseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		if (isTimeDirectoryName(name))
			times.push_back({ name.toDouble(), name });
	std::sort(times.begin(), times.end(), [](const TimeDir& a, const TimeDir& b) { return a.time < b.time; });

	struct StepFields
	{
		double time = 0.0;
		std::map<QString, ParsedField> fields;
	};
	std::vector<StepFields> steps;
	QStringList skipped;
	for (const TimeDir& timeDir : times)
	{
		StepFields step;
		step.time = timeDir.time;
		const QDir dir(caseDir.filePath(timeDir.name));
		for (const QString& fileName : fieldFileNames(dir))
		{
			if (cancelled())
				return fail(QStringLiteral("cancelled"));
			QByteArray data;
			Cursor cursor;
			FoamHeader header;
			// Cheap class check first, so unrelated files (phi, uniform data, ...) are not even read fully as fields.
			{
				QFile file(dir.filePath(fileName));
				if (!file.open(QIODevice::ReadOnly))
					continue;
				const QByteArray head = file.read(2048);
				if (!head.contains("FoamFile") || !head.contains("class") || !head.contains("vol"))
					continue;
			}
			QFile probe(dir.filePath(fileName));
			if (!probe.open(QIODevice::ReadOnly))
				continue;
			data = probe.readAll();
			cursor.p = data.constData();
			cursor.end = data.constData() + data.size();
			header = readHeader(cursor);
			const int components = header.ok ? componentsForClass(header.className) : 0;
			if (components == 0)
				continue; // not a cell field (surface fields, dictionaries, ...)
			if (header.format.toLower() != "ascii")
			{
				skipped << QStringLiteral("%1 (%2): not ASCII").arg(fileName, timeDir.name);
				continue;
			}
			ParsedField parsed;
			QString reason;
			if (!readFieldBody(cursor, components, cellCount, parsed, reason))
			{
				skipped << QStringLiteral("%1 (%2): %3").arg(fileName, timeDir.name, reason);
				continue;
			}
			if (components == 6)
				reorderSymmTensor(parsed.values);
			step.fields.emplace(fileName, std::move(parsed));
		}
		if (!step.fields.empty())
			steps.push_back(std::move(step));
	}

	// ---- Assemble the steps and fields -----------------------------------------------------------------------------
	for (const StepFields& s : steps)
	{
		ResultStep step;
		step.time = s.time;
		dataset->steps.push_back(step);
	}
	std::map<QString, std::size_t> fieldIndex;
	for (std::size_t s = 0; s < steps.size(); ++s)
	{
		for (auto& entry : steps[s].fields)
		{
			auto found = fieldIndex.find(entry.first);
			if (found == fieldIndex.end())
			{
				ResultField field;
				field.name = entry.first;
				field.association = ResultFieldAssociation::Cell;
				field.components = entry.second.components;
				if (field.components == 6)
					field.componentNames = { QStringLiteral("XX"), QStringLiteral("YY"), QStringLiteral("ZZ"),
					                         QStringLiteral("XY"), QStringLiteral("YZ"), QStringLiteral("ZX") };
				else if (field.components == 9)
					field.componentNames = { QStringLiteral("XX"), QStringLiteral("XY"), QStringLiteral("XZ"),
					                         QStringLiteral("YX"), QStringLiteral("YY"), QStringLiteral("YZ"),
					                         QStringLiteral("ZX"), QStringLiteral("ZY"), QStringLiteral("ZZ") };
				QString kind;
				if (kindFromDimensions(entry.second.dimensions, kind))
				{
					const QStringList symbols = unitSymbols(kind);
					if (!symbols.isEmpty())
					{
						field.quantityKind = kind;
						field.fileUnit = symbols.first(); // the SI unit (first in each kind's list)
						field.displayUnit = field.fileUnit;
						field.unitConfirmed = true; // stated by the file's own dimensions
					}
				}
				field.stepData.assign(steps.size(), std::vector<float>());
				found = fieldIndex.emplace(entry.first, dataset->fields.size()).first;
				dataset->fields.push_back(std::move(field));
			}
			ResultField& field = dataset->fields[found->second];
			if (field.components != entry.second.components)
			{
				skipped << QStringLiteral("%1 (time %2): its type changed between time steps").arg(entry.first).arg(steps[s].time);
				continue;
			}
			field.stepData[s] = std::move(entry.second.values);
		}
	}

	const QString invalid = dataset->validate();
	if (!invalid.isEmpty())
		return fail(QStringLiteral("The OpenFOAM case is inconsistent: %1").arg(invalid));

	if (steps.empty())
		outcome.warnings << QStringLiteral("No result fields were found: only numeric time directories with ASCII volScalarField / "
		                                    "volVectorField / volSymmTensorField / volTensorField files are read.");
	if (!skipped.isEmpty())
	{
		const int shown = std::min<int>(3, static_cast<int>(skipped.size()));
		QString message = QStringLiteral("%1 field file(s) were skipped: %2").arg(skipped.size()).arg(skipped.mid(0, shown).join(QStringLiteral("; ")));
		if (skipped.size() > shown)
			message += QStringLiteral("; ...");
		outcome.warnings << message;
	}
	outcome.warnings << QStringLiteral("Only the boundary of the mesh is displayed; each boundary face shows the value of its cell.");
	outcome.dataset = std::move(dataset);
	return outcome;
}
