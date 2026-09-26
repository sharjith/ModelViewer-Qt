#include "VtkXmlReader.h"

#include "VtkDataTypes.h"

#include <QByteArray>
#include <QFile>
#include <QSysInfo>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

using namespace vtkio;

namespace
{
	// ---- VTK scalar types (shared helpers live in VtkDataTypes.h) -----------------------------------

	VtkType parseVtkType(const QString& s)
	{
		if (s == QLatin1String("Int8"))    return VtkType::Int8;
		if (s == QLatin1String("UInt8"))   return VtkType::UInt8;
		if (s == QLatin1String("Int16"))   return VtkType::Int16;
		if (s == QLatin1String("UInt16"))  return VtkType::UInt16;
		if (s == QLatin1String("Int32"))   return VtkType::Int32;
		if (s == QLatin1String("UInt32"))  return VtkType::UInt32;
		if (s == QLatin1String("Int64"))   return VtkType::Int64;
		if (s == QLatin1String("UInt64"))  return VtkType::UInt64;
		if (s == QLatin1String("Float32")) return VtkType::Float32;
		if (s == QLatin1String("Float64")) return VtkType::Float64;
		return VtkType::Invalid;
	}

	template <class Dst>
	bool convertBinary(const QByteArray& raw, VtkType type, bool swap, std::vector<Dst>& out, QString& err)
	{
		return convertBinaryBytes(raw.constData(), static_cast<std::size_t>(raw.size()), type, swap, out, err);
	}

	// Whitespace-separated numbers -> vector<Dst>. `text` is a QByteArray, so it is NUL-terminated and
	// strtod/strtoll can safely run to the end of the buffer.
	template <class Dst>
	bool parseAscii(const QByteArray& text, VtkType type, std::vector<Dst>& out, QString& err)
	{
		const bool isFloat = type == VtkType::Float32 || type == VtkType::Float64;
		const bool isUnsigned = type == VtkType::UInt8 || type == VtkType::UInt16 || type == VtkType::UInt32 || type == VtkType::UInt64;
		const char* p = text.constData();
		const char* end = p + text.size();
		out.clear();
		while (true)
		{
			while (p < end && std::isspace(static_cast<unsigned char>(*p)))
				++p;
			if (p >= end)
				break;
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
		return true;
	}

	// ---- Binary blobs (inline base64, appended raw, appended base64) -------------------------------

	struct FileContext
	{
		bool swap = false;            // file byte order differs from this machine's
		int headerSize = 4;           // 4 (UInt32) or 8 (UInt64)
		bool compressed = false;      // vtkZLibDataCompressor
		const QByteArray* appended = nullptr;
		bool appendedBase64 = false;
	};

	qint64 base64Chars(qint64 bytes) { return ((bytes + 2) / 3) * 4; }

	QByteArray decodeBase64(const char* src, qint64 chars)
	{
		return QByteArray::fromBase64(QByteArray::fromRawData(src, static_cast<int>(chars)));
	}

	quint64 readHeaderUInt(const char* p, int headerSize, bool swap)
	{
		if (headerSize == 8)
		{
			quint64 v;
			std::memcpy(&v, p, 8);
			if (swap)
				reverseBytes(v);
			return v;
		}
		quint32 v;
		std::memcpy(&v, p, 4);
		if (swap)
			reverseBytes(v);
		return v;
	}

	// One VTK data blob starting at `src` (`avail` bytes/characters remain in the buffer) -> raw payload.
	// Layout, per the VTK XML format: an uncompressed array is [size][data]; a compressed array is
	// [nBlocks][blockSize][lastBlockSize][compressedSize x nBlocks][compressed blocks...]. In base64
	// mode the header and the data are each encoded as their own base64 unit.
	bool decodeBlob(const char* src, qint64 avail, bool base64, const FileContext& ctx, QByteArray& out, QString& err)
	{
		const int hs = ctx.headerSize;
		if (!ctx.compressed)
		{
			QByteArray header;
			qint64 dataStart = 0;
			if (base64)
			{
				const qint64 hdrChars = base64Chars(hs);
				if (avail < hdrChars) { err = QStringLiteral("truncated array header"); return false; }
				header = decodeBase64(src, hdrChars);
				dataStart = hdrChars;
			}
			else
			{
				if (avail < hs) { err = QStringLiteral("truncated array header"); return false; }
				header = QByteArray(src, hs);
				dataStart = hs;
			}
			if (header.size() < hs) { err = QStringLiteral("bad array header"); return false; }
			const quint64 n = readHeaderUInt(header.constData(), hs, ctx.swap);
			if (base64)
			{
				const qint64 dataChars = base64Chars(static_cast<qint64>(n));
				if (avail < dataStart + dataChars) { err = QStringLiteral("truncated array data"); return false; }
				out = decodeBase64(src + dataStart, dataChars);
			}
			else
			{
				if (static_cast<quint64>(avail - dataStart) < n) { err = QStringLiteral("truncated array data"); return false; }
				out = QByteArray(src + dataStart, static_cast<int>(n));
			}
			if (static_cast<quint64>(out.size()) != n) { err = QStringLiteral("decoded array size mismatch"); return false; }
			return true;
		}

		// Compressed: read the 3 fixed header fields first to learn the block count.
		const qint64 fixedBytes = 3 * static_cast<qint64>(hs);
		QByteArray fixed;
		if (base64)
		{
			// 3*4 = 12 or 3*8 = 24 bytes: a whole number of base64 groups, so this prefix decodes cleanly.
			if (avail < base64Chars(fixedBytes)) { err = QStringLiteral("truncated compressed header"); return false; }
			fixed = decodeBase64(src, base64Chars(fixedBytes));
		}
		else
		{
			if (avail < fixedBytes) { err = QStringLiteral("truncated compressed header"); return false; }
			fixed = QByteArray(src, static_cast<int>(fixedBytes));
		}
		if (fixed.size() < fixedBytes) { err = QStringLiteral("bad compressed header"); return false; }

		const quint64 nBlocks = readHeaderUInt(fixed.constData(), hs, ctx.swap);
		const quint64 blockSize = readHeaderUInt(fixed.constData() + hs, hs, ctx.swap);
		const quint64 lastBlock = readHeaderUInt(fixed.constData() + 2 * hs, hs, ctx.swap);
		if (nBlocks > 100000000ULL) { err = QStringLiteral("implausible compressed block count"); return false; }

		const qint64 headerBytes = (3 + static_cast<qint64>(nBlocks)) * hs;
		QByteArray header;
		qint64 dataStart = 0;
		if (base64)
		{
			const qint64 hdrChars = base64Chars(headerBytes);
			if (avail < hdrChars) { err = QStringLiteral("truncated compressed header"); return false; }
			header = decodeBase64(src, hdrChars);
			dataStart = hdrChars;
		}
		else
		{
			if (avail < headerBytes) { err = QStringLiteral("truncated compressed header"); return false; }
			header = QByteArray(src, static_cast<int>(headerBytes));
			dataStart = headerBytes;
		}
		if (header.size() < headerBytes) { err = QStringLiteral("bad compressed header"); return false; }

		std::vector<quint64> compSizes(static_cast<std::size_t>(nBlocks));
		quint64 totalCompressed = 0;
		for (quint64 i = 0; i < nBlocks; ++i)
		{
			compSizes[i] = readHeaderUInt(header.constData() + (3 + i) * hs, hs, ctx.swap);
			totalCompressed += compSizes[i];
		}

		QByteArray compressed;
		if (base64)
		{
			const qint64 chars = base64Chars(static_cast<qint64>(totalCompressed));
			if (avail < dataStart + chars) { err = QStringLiteral("truncated compressed data"); return false; }
			compressed = decodeBase64(src + dataStart, chars);
		}
		else
		{
			if (static_cast<quint64>(avail - dataStart) < totalCompressed) { err = QStringLiteral("truncated compressed data"); return false; }
			compressed = QByteArray(src + dataStart, static_cast<int>(totalCompressed));
		}
		if (static_cast<quint64>(compressed.size()) < totalCompressed) { err = QStringLiteral("decoded compressed data too short"); return false; }

		out.clear();
		if (nBlocks > 0)
			out.reserve(static_cast<int>((nBlocks - 1) * blockSize + (lastBlock ? lastBlock : blockSize)));
		quint64 cursor = 0;
		for (quint64 i = 0; i < nBlocks; ++i)
		{
			const quint64 expected = (i == nBlocks - 1 && lastBlock != 0) ? lastBlock : blockSize;
			// qUncompress() wants a 4-byte big-endian expected-size prefix in front of the zlib stream.
			QByteArray block;
			block.reserve(static_cast<int>(compSizes[i] + 4));
			block.append(static_cast<char>((expected >> 24) & 0xFF));
			block.append(static_cast<char>((expected >> 16) & 0xFF));
			block.append(static_cast<char>((expected >> 8) & 0xFF));
			block.append(static_cast<char>(expected & 0xFF));
			block.append(compressed.constData() + cursor, static_cast<int>(compSizes[i]));
			cursor += compSizes[i];
			const QByteArray inflated = qUncompress(block);
			if (static_cast<quint64>(inflated.size()) != expected)
			{
				err = QStringLiteral("zlib block %1 inflated to the wrong size").arg(i);
				return false;
			}
			out.append(inflated);
		}
		return true;
	}

	// ---- One <DataArray> ----------------------------------------------------------------------------

	struct ArrayDesc
	{
		QString name;
		VtkType type = VtkType::Invalid;
		int components = 1;
		QString format;      // ascii | binary | appended
		qint64 offset = 0;   // appended only
		QByteArray payload;  // ascii text or inline base64 text
	};

	template <class T>
	bool loadArray(const ArrayDesc& d, const FileContext& ctx, std::vector<T>& out, QString& err)
	{
		if (d.type == VtkType::Invalid)
		{
			err = QStringLiteral("array '%1' has an unsupported data type").arg(d.name);
			return false;
		}
		if (d.format == QLatin1String("ascii"))
			return parseAscii(d.payload, d.type, out, err);

		QByteArray raw;
		if (d.format == QLatin1String("binary"))
		{
			QByteArray text;
			text.reserve(d.payload.size());
			for (char c : d.payload)
				if (!std::isspace(static_cast<unsigned char>(c)))
					text.append(c);
			if (!decodeBlob(text.constData(), text.size(), true, ctx, raw, err))
				return false;
		}
		else if (d.format == QLatin1String("appended"))
		{
			if (!ctx.appended)
			{
				err = QStringLiteral("array '%1' is appended but the file has no AppendedData section").arg(d.name);
				return false;
			}
			if (d.offset < 0 || d.offset >= ctx.appended->size())
			{
				err = QStringLiteral("array '%1' has an appended offset outside the data").arg(d.name);
				return false;
			}
			if (!decodeBlob(ctx.appended->constData() + d.offset, ctx.appended->size() - d.offset,
			                ctx.appendedBase64, ctx, raw, err))
				return false;
		}
		else
		{
			err = QStringLiteral("array '%1' has an unknown format '%2'").arg(d.name, d.format);
			return false;
		}
		return convertBinary(raw, d.type, ctx.swap, out, err);
	}

	bool cancelled(const std::atomic<bool>* cancel)
	{
		return cancel && cancel->load(std::memory_order_acquire);
	}
}

ResultReadOutcome readVtkXmlUnstructuredGrid(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		outcome.error = QStringLiteral("Cannot open '%1': %2").arg(path, file.errorString());
		return outcome;
	}
	// The whole file is read into memory: the XML part is parsed with QXmlStreamReader and the
	// appended binary section (which is not valid XML when raw) is addressed by offset.
	const QByteArray all = file.readAll();
	file.close();
	if (all.isEmpty())
	{
		outcome.error = QStringLiteral("The file is empty.");
		return outcome;
	}

	FileContext ctx;
	QByteArray appended;
	QByteArray xmlBytes;
	const int appIdx = all.indexOf("<AppendedData");
	if (appIdx >= 0)
	{
		const int tagEnd = all.indexOf('>', appIdx);
		if (tagEnd < 0)
		{
			outcome.error = QStringLiteral("Malformed AppendedData tag.");
			return outcome;
		}
		const QByteArray tag = all.mid(appIdx, tagEnd - appIdx + 1);
		ctx.appendedBase64 = tag.contains("base64");
		int dataStart = tagEnd + 1;
		while (dataStart < all.size() && all[dataStart] != '_')
			++dataStart;
		if (dataStart >= all.size())
		{
			outcome.error = QStringLiteral("AppendedData section has no '_' marker.");
			return outcome;
		}
		++dataStart; // skip '_'
		// Trailing "</AppendedData></VTKFile>" is not stripped: every array carries its own length,
		// so the extra bytes are never read.
		appended = all.mid(dataStart);
		ctx.appended = &appended;
		xmlBytes = all.left(appIdx) + QByteArray("</VTKFile>");
	}
	else
	{
		xmlBytes = all;
	}

	auto fail = [&outcome](const QString& message) -> ResultReadOutcome
	{
		outcome.error = message;
		outcome.dataset.reset();
		return std::move(outcome);
	};

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;

	QXmlStreamReader xml(xmlBytes);

	enum class Section { None, Points, Cells, PointData, CellData, FieldData };
	Section section = Section::None;

	bool sawFile = false;
	int pieces = 0;
	std::size_t declaredNodes = 0;
	std::size_t declaredCells = 0;
	bool havePoints = false;
	std::vector<std::uint32_t> connectivity, offsets;
	std::vector<std::int64_t> polyFaces, polyFaceOffsets; // <Cells> "faces" / "faceoffsets": the faces of polyhedra (cell type 42)
	std::vector<std::uint8_t> types;
	bool haveConnectivity = false, haveOffsets = false, haveTypes = false;
	double timeValue = 0.0;

	while (!xml.atEnd())
	{
		xml.readNext();
		if (xml.isStartElement())
		{
			const QString name = xml.name().toString();
			const QXmlStreamAttributes attrs = xml.attributes();

			if (name == QLatin1String("VTKFile"))
			{
				sawFile = true;
				const QString type = attrs.value(QLatin1String("type")).toString();
				if (type != QLatin1String("UnstructuredGrid"))
					return fail(QStringLiteral("Unsupported VTK file type '%1' (only UnstructuredGrid .vtu is supported).").arg(type));
				const QString order = attrs.value(QLatin1String("byte_order")).toString();
				const bool fileBigEndian = order == QLatin1String("BigEndian");
				ctx.swap = fileBigEndian != (QSysInfo::ByteOrder == QSysInfo::BigEndian);
				const QString header = attrs.value(QLatin1String("header_type")).toString();
				ctx.headerSize = header == QLatin1String("UInt64") ? 8 : 4;
				const QString compressor = attrs.value(QLatin1String("compressor")).toString();
				if (!compressor.isEmpty())
				{
					if (compressor != QLatin1String("vtkZLibDataCompressor"))
						return fail(QStringLiteral("Unsupported compressor '%1' (only zlib is supported).").arg(compressor));
					ctx.compressed = true;
				}
			}
			else if (name == QLatin1String("Piece"))
			{
				if (++pieces > 1)
					return fail(QStringLiteral("Files with more than one <Piece> are not supported."));
				declaredNodes = attrs.value(QLatin1String("NumberOfPoints")).toString().toULongLong();
				declaredCells = attrs.value(QLatin1String("NumberOfCells")).toString().toULongLong();
			}
			else if (name == QLatin1String("Points"))     section = Section::Points;
			else if (name == QLatin1String("Cells"))      section = Section::Cells;
			else if (name == QLatin1String("PointData"))  section = Section::PointData;
			else if (name == QLatin1String("CellData"))   section = Section::CellData;
			else if (name == QLatin1String("FieldData"))  section = Section::FieldData;
			else if (name == QLatin1String("DataArray"))
			{
				ArrayDesc d;
				d.name = attrs.value(QLatin1String("Name")).toString();
				d.type = parseVtkType(attrs.value(QLatin1String("type")).toString());
				const QString comps = attrs.value(QLatin1String("NumberOfComponents")).toString();
				d.components = comps.isEmpty() ? 1 : comps.toInt();
				d.format = attrs.value(QLatin1String("format")).toString();
				if (d.format.isEmpty())
					d.format = QStringLiteral("ascii");
				d.offset = attrs.value(QLatin1String("offset")).toString().toLongLong();
				// readElementText() consumes through this element's end tag (empty for appended arrays).
				// VTK writers nest <InformationKey> children (e.g. L2_NORM_RANGE) inside a DataArray, which
				// the default behaviour rejects - skip them, keeping only the array's own text.
				d.payload = xml.readElementText(QXmlStreamReader::SkipChildElements).toLatin1();

				if (section == Section::None)
					continue;

				QString err;
				if (section == Section::Points)
				{
					if (d.components != 3)
						return fail(QStringLiteral("Points must have 3 components, found %1.").arg(d.components));
					if (!loadArray<float>(d, ctx, dataset->nodePositions, err))
						return fail(QStringLiteral("Reading points: %1").arg(err));
					havePoints = true;
				}
				else if (section == Section::Cells)
				{
					if (d.name == QLatin1String("connectivity"))
					{
						if (!loadArray<std::uint32_t>(d, ctx, connectivity, err))
							return fail(QStringLiteral("Reading connectivity: %1").arg(err));
						haveConnectivity = true;
					}
					else if (d.name == QLatin1String("offsets"))
					{
						if (!loadArray<std::uint32_t>(d, ctx, offsets, err))
							return fail(QStringLiteral("Reading offsets: %1").arg(err));
						haveOffsets = true;
					}
					else if (d.name == QLatin1String("types"))
					{
						if (!loadArray<std::uint8_t>(d, ctx, types, err))
							return fail(QStringLiteral("Reading cell types: %1").arg(err));
						haveTypes = true;
					}
					else if (d.name == QLatin1String("faces"))
					{
						if (!loadArray<std::int64_t>(d, ctx, polyFaces, err))
							return fail(QStringLiteral("Reading polyhedron faces: %1").arg(err));
					}
					else if (d.name == QLatin1String("faceoffsets"))
					{
						if (!loadArray<std::int64_t>(d, ctx, polyFaceOffsets, err))
							return fail(QStringLiteral("Reading polyhedron face offsets: %1").arg(err));
					}
				}
				else if (section == Section::PointData || section == Section::CellData)
				{
					if (d.name.isEmpty() || d.components <= 0)
					{
						outcome.warnings << QStringLiteral("Skipped an unnamed or malformed data array.");
					}
					else if (d.type == VtkType::Invalid)
					{
						outcome.warnings << QStringLiteral("Skipped field '%1' (unsupported data type).").arg(d.name);
					}
					else
					{
						ResultField field;
						field.name = d.name;
						field.association = section == Section::PointData ? ResultFieldAssociation::Node : ResultFieldAssociation::Cell;
						field.components = d.components;
						field.stepData.emplace_back();
						if (!loadArray<float>(d, ctx, field.stepData.back(), err))
							return fail(QStringLiteral("Reading field '%1': %2").arg(d.name, err));
						dataset->fields.push_back(std::move(field));
					}
				}
				else if (section == Section::FieldData)
				{
					// A single-value TimeValue/TIME array gives this file's time.
					if (d.name == QLatin1String("TimeValue") || d.name == QLatin1String("TIME"))
					{
						std::vector<double> t;
						if (loadArray<double>(d, ctx, t, err) && !t.empty())
							timeValue = t.front();
					}
				}

				if (cancelled(cancel))
					return fail(QStringLiteral("cancelled"));
			}
		}
		else if (xml.isEndElement())
		{
			const QString name = xml.name().toString();
			if (name == QLatin1String("Points") || name == QLatin1String("Cells") || name == QLatin1String("PointData")
				|| name == QLatin1String("CellData") || name == QLatin1String("FieldData"))
				section = Section::None;
		}
	}

	if (xml.hasError())
		return fail(QStringLiteral("XML error: %1 (line %2)").arg(xml.errorString()).arg(xml.lineNumber()));
	if (!sawFile)
		return fail(QStringLiteral("Not a VTK XML file."));
	if (!havePoints || !haveConnectivity || !haveOffsets || !haveTypes)
		return fail(QStringLiteral("The file is missing points, connectivity, offsets or cell types."));
	if (dataset->nodeCount() != declaredNodes)
		return fail(QStringLiteral("Piece declares %1 points but %2 were read.").arg(declaredNodes).arg(dataset->nodeCount()));
	if (types.size() != declaredCells || offsets.size() != declaredCells)
		return fail(QStringLiteral("Piece declares %1 cells but %2 types and %3 offsets were read.")
			.arg(declaredCells).arg(types.size()).arg(offsets.size()));

	dataset->cellConnectivity = std::move(connectivity);
	dataset->cellOffsets.reserve(offsets.size() + 1);
	dataset->cellOffsets.push_back(0);
	dataset->cellOffsets.insert(dataset->cellOffsets.end(), offsets.begin(), offsets.end());
	dataset->cellTypes.reserve(types.size());
	for (std::uint8_t t : types)
	{
		dataset->cellTypes.push_back(resultCellTypeFromVtk(t));
	}
	// Polyhedra: cell c's entry in `faces` is [number of faces, then for each face its node count and its nodes], and faceoffsets[c] is
	// where that entry ends (-1 for a cell that is not a polyhedron).
	if (!polyFaces.empty() && polyFaceOffsets.size() == declaredCells)
	{
		std::size_t pos = 0;
		dataset->cellFaceOffsets.push_back(0);
		dataset->faceOffsets.push_back(0);
		for (std::size_t c = 0; c < declaredCells; ++c)
		{
			if (types[c] == 42 && polyFaceOffsets[c] >= 0)
			{
				const std::size_t end = static_cast<std::size_t>(polyFaceOffsets[c]);
				if (end > polyFaces.size() || pos >= end)
					return fail(QStringLiteral("The faces of polyhedron %1 are outside the faces array.").arg(c));
				const std::int64_t faceCount = polyFaces[pos++];
				for (std::int64_t f = 0; f < faceCount; ++f)
				{
					if (pos >= end)
						return fail(QStringLiteral("The faces of polyhedron %1 end early.").arg(c));
					const std::int64_t nodes = polyFaces[pos++];
					if (nodes < 3 || pos + static_cast<std::size_t>(nodes) > end)
						return fail(QStringLiteral("A face of polyhedron %1 has %2 nodes or runs past the cell's faces.").arg(c).arg(nodes));
					for (std::int64_t k = 0; k < nodes; ++k)
						dataset->faceNodes.push_back(static_cast<std::uint32_t>(polyFaces[pos++]));
					dataset->cellFaces.push_back(static_cast<std::uint32_t>(dataset->faceOffsets.size() - 1));
					dataset->faceOffsets.push_back(static_cast<std::uint32_t>(dataset->faceNodes.size()));
				}
				pos = end;
			}
			dataset->cellFaceOffsets.push_back(static_cast<std::uint32_t>(dataset->cellFaces.size()));
		}
	}
	outcome.warnings << resultCellTypeWarnings(*dataset);

	ResultStep step;
	step.time = timeValue;
	dataset->steps.push_back(step);

	const QString problem = dataset->validate();
	if (!problem.isEmpty())
		return fail(QStringLiteral("Invalid dataset: %1").arg(problem));

	outcome.dataset = std::move(dataset);
	return outcome;
}
