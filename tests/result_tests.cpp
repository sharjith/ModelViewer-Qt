// GUI-free tests for the simulation result reader and boundary extraction (docs/simulation_results_design.md).
// Build with -DMV_BUILD_TESTS=ON; run the `result_tests` executable. Exit code = number of failed checks.
//
// Every fixture is generated in code (no binary files in the repo) in each VTK XML data encoding, so a
// bug in header/offset/compression handling shows up as a mismatch against the same mesh written as
// plain ASCII. NOTE: the writers here are written from the VTK format description, like the reader, so
// they cannot catch a shared misreading of the spec - real files (FreeCAD/CalculiX/ParaView output) are
// the final check and are listed in docs/simulation_results_test_data.md.

#include "ResultBoundary.h"
#include "ResultReader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                        \
	do                                                                                     \
	{                                                                                      \
		++g_checks;                                                                        \
		if (!(cond))                                                                       \
		{                                                                                  \
			++g_failures;                                                                  \
			std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
		}                                                                                  \
	} while (0)

namespace
{
	enum class Enc
	{
		Ascii, InlineB64, InlineB64Zlib, InlineB64Header64, InlineB64BigEndian, AppendedRaw, AppendedB64, AppendedRawZlib
	};

	struct Mesh
	{
		std::vector<float> pts;
		std::vector<int> conn, offs, types;
		std::vector<double> pointScalar; // 1 per node (optional)
		std::vector<float> cellVector;   // 3 per cell (optional)
	};

	template <class T>
	QByteArray toBytes(const std::vector<T>& v, bool big)
	{
		QByteArray b;
		for (T x : v)
		{
			unsigned char raw[sizeof(T)];
			std::memcpy(raw, &x, sizeof(T));
			if (big)
				std::reverse(raw, raw + sizeof(T));
			b.append(reinterpret_cast<const char*>(raw), sizeof(T));
		}
		return b;
	}

	QByteArray headerInt(quint64 v, int headerSize, bool big)
	{
		QByteArray b;
		if (headerSize == 8)
		{
			unsigned char raw[8];
			std::memcpy(raw, &v, 8);
			if (big)
				std::reverse(raw, raw + 8);
			b.append(reinterpret_cast<const char*>(raw), 8);
		}
		else
		{
			quint32 v32 = static_cast<quint32>(v);
			unsigned char raw[4];
			std::memcpy(raw, &v32, 4);
			if (big)
				std::reverse(raw, raw + 4);
			b.append(reinterpret_cast<const char*>(raw), 4);
		}
		return b;
	}

	// One VTK data blob (see the reader's decodeBlob() comment for the layout).
	QByteArray encodeBlob(const QByteArray& raw, bool compressed, bool base64, int headerSize, bool big)
	{
		QByteArray header, data;
		if (!compressed)
		{
			header = headerInt(static_cast<quint64>(raw.size()), headerSize, big);
			data = raw;
		}
		else
		{
			const int blockSize = 32; // small, to force several blocks and a partial last block
			std::vector<QByteArray> blocks;
			for (int pos = 0; pos < raw.size(); pos += blockSize)
				blocks.push_back(qCompress(raw.mid(pos, blockSize), 6).mid(4)); // drop qCompress's size prefix
			const quint64 last = static_cast<quint64>(raw.size() % blockSize);
			header = headerInt(blocks.size(), headerSize, big) + headerInt(blockSize, headerSize, big)
				+ headerInt(last, headerSize, big);
			for (const QByteArray& b : blocks)
			{
				header += headerInt(static_cast<quint64>(b.size()), headerSize, big);
				data += b;
			}
		}
		if (base64)
			return header.toBase64() + data.toBase64();
		return header + data;
	}

	QByteArray asciiText(const std::vector<double>& v)
	{
		QByteArray t;
		for (double x : v)
			t += QByteArray::number(x, 'g', 12) + ' ';
		return t;
	}

	QByteArray buildVtu(const Mesh& m, Enc enc, const char* fileType = "UnstructuredGrid",
	                    const char* compressorOverride = nullptr)
	{
		const bool big = enc == Enc::InlineB64BigEndian;
		const bool zlib = enc == Enc::InlineB64Zlib || enc == Enc::AppendedRawZlib;
		const int headerSize = enc == Enc::InlineB64Header64 ? 8 : 4;
		const bool appended = enc == Enc::AppendedRaw || enc == Enc::AppendedB64 || enc == Enc::AppendedRawZlib;
		const bool appendedB64 = enc == Enc::AppendedB64;

		QByteArray appendedData;
		auto arrayXml = [&](const char* name, const char* type, int comps, const QByteArray& raw,
		                    const std::vector<double>& asciiValues) -> QByteArray
		{
			QByteArray x = QByteArray("<DataArray type=\"") + type + "\" Name=\"" + name + "\" NumberOfComponents=\""
				+ QByteArray::number(comps) + "\" ";
			if (enc == Enc::Ascii)
				return x + "format=\"ascii\">" + asciiText(asciiValues) + "</DataArray>\n";
			if (appended)
			{
				const QByteArray blob = encodeBlob(raw, zlib, appendedB64, headerSize, big);
				x += "format=\"appended\" offset=\"" + QByteArray::number(appendedData.size()) + "\"/>\n";
				appendedData += blob;
				return x;
			}
			return x + "format=\"binary\">" + encodeBlob(raw, zlib, true, headerSize, big) + "</DataArray>\n";
		};
		auto ints = [](const std::vector<int>& v) { return std::vector<double>(v.begin(), v.end()); };
		auto floats = [](const std::vector<float>& v) { return std::vector<double>(v.begin(), v.end()); };

		std::vector<std::int64_t> conn64(m.conn.begin(), m.conn.end());
		std::vector<std::int64_t> offs64(m.offs.begin(), m.offs.end());
		std::vector<std::uint8_t> types8(m.types.begin(), m.types.end());

		QByteArray out = "<?xml version=\"1.0\"?>\n<VTKFile type=\"";
		out += fileType;
		out += "\" version=\"1.0\" byte_order=\"";
		out += big ? "BigEndian" : "LittleEndian";
		out += "\" header_type=\"";
		out += headerSize == 8 ? "UInt64" : "UInt32";
		out += "\"";
		if (compressorOverride)
			out += QByteArray(" compressor=\"") + compressorOverride + "\"";
		else if (zlib)
			out += " compressor=\"vtkZLibDataCompressor\"";
		out += ">\n<UnstructuredGrid>\n<Piece NumberOfPoints=\"" + QByteArray::number(int(m.pts.size() / 3))
			+ "\" NumberOfCells=\"" + QByteArray::number(int(m.types.size())) + "\">\n";
		out += "<PointData>\n";
		if (!m.pointScalar.empty())
			out += arrayXml("T", "Float64", 1, toBytes(m.pointScalar, big), m.pointScalar);
		out += "</PointData>\n<CellData>\n";
		if (!m.cellVector.empty())
			out += arrayXml("V", "Float32", 3, toBytes(m.cellVector, big), floats(m.cellVector));
		out += "</CellData>\n<Points>\n";
		out += arrayXml("Points", "Float32", 3, toBytes(m.pts, big), floats(m.pts));
		out += "</Points>\n<Cells>\n";
		out += arrayXml("connectivity", "Int64", 1, toBytes(conn64, big), ints(m.conn));
		out += arrayXml("offsets", "Int64", 1, toBytes(offs64, big), ints(m.offs));
		out += arrayXml("types", "UInt8", 1, toBytes(types8, big), ints(m.types));
		out += "</Cells>\n</Piece>\n</UnstructuredGrid>\n";
		if (appended)
		{
			out += appendedB64 ? "<AppendedData encoding=\"base64\">\n_" : "<AppendedData encoding=\"raw\">\n_";
			out += appendedData;
			out += "\n</AppendedData>\n";
		}
		out += "</VTKFile>\n";
		return out;
	}

	QTemporaryDir& tempDir()
	{
		static QTemporaryDir dir;
		return dir;
	}

	ResultReadOutcome readBytes(const QByteArray& bytes, const QString& name = QStringLiteral("t.vtu"))
	{
		const QString path = tempDir().filePath(name);
		QFile f(path);
		CHECK(f.open(QIODevice::WriteOnly));
		f.write(bytes);
		f.close();
		return readResultFile(path);
	}

	// ---- Meshes ------------------------------------------------------------------------------------

	Mesh singleTet()
	{
		Mesh m;
		m.pts = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		m.conn = { 0, 1, 2, 3 };
		m.offs = { 4 };
		m.types = { 10 };
		m.pointScalar = { 1.5, 2.5, 3.5, 4.5 };
		m.cellVector = { 7, 8, 9 };
		return m;
	}

	Mesh twoTets() // tets 0-1-2-3 and 1-2-3-4 share the face 1-2-3
	{
		Mesh m;
		m.pts = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1 };
		m.conn = { 0, 1, 2, 3, 1, 2, 3, 4 };
		m.offs = { 4, 8 };
		m.types = { 10, 10 };
		m.pointScalar = { 0, 1, 2, 3, 4 };
		m.cellVector = { 1, 2, 3, 4, 5, 6 };
		return m;
	}

	Mesh hexes(int count) // `count` unit cubes in a row along x; 4 nodes per x-plane
	{
		Mesh m;
		for (int i = 0; i <= count; ++i)
		{
			const float x = static_cast<float>(i);
			m.pts.insert(m.pts.end(), { x, 0, 0, x, 1, 0, x, 1, 1, x, 0, 1 });
		}
		for (int c = 0; c < count; ++c)
		{
			const int a = c * 4, b = (c + 1) * 4;
			// "bottom" quad = plane x=c, "top" quad = plane x=c+1 (same ring order).
			m.conn.insert(m.conn.end(), { a, a + 1, a + 2, a + 3, b, b + 1, b + 2, b + 3 });
			m.offs.push_back((c + 1) * 8);
			m.types.push_back(12);
		}
		return m;
	}

	// Every boundary triangle must face away from the cell it came from.
	bool trianglesFaceOutward(const ResultDataset& ds, const ResultBoundarySurface& s)
	{
		for (std::size_t t = 0; t < s.triangleCount(); ++t)
		{
			if (s.triangleFace[t] == ResultBoundarySurface::kNoFace)
				continue;
			const std::uint32_t cell = s.triangleCell[t];
			double cx = 0, cy = 0, cz = 0;
			const std::size_t n = ds.cellOffsets[cell + 1] - ds.cellOffsets[cell];
			for (std::size_t k = ds.cellOffsets[cell]; k < ds.cellOffsets[cell + 1]; ++k)
			{
				cx += ds.nodePositions[ds.cellConnectivity[k] * 3 + 0];
				cy += ds.nodePositions[ds.cellConnectivity[k] * 3 + 1];
				cz += ds.nodePositions[ds.cellConnectivity[k] * 3 + 2];
			}
			cx /= n; cy /= n; cz /= n;
			const float* p0 = &s.positions[s.triangles[t * 3 + 0] * 3];
			const float* p1 = &s.positions[s.triangles[t * 3 + 1] * 3];
			const float* p2 = &s.positions[s.triangles[t * 3 + 2] * 3];
			const double ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
			const double bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
			const double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
			const double mx = (p0[0] + p1[0] + p2[0]) / 3 - cx, my = (p0[1] + p1[1] + p2[1]) / 3 - cy,
			             mz = (p0[2] + p1[2] + p2[2]) / 3 - cz;
			if (nx * mx + ny * my + nz * mz <= 0.0)
				return false;
		}
		return true;
	}

	ResultBoundarySurface extract(const ResultDataset& ds)
	{
		ResultBoundarySurface s;
		QString err;
		CHECK(extractBoundarySurface(ds, s, nullptr, &err));
		return s;
	}

	// ---- Tests -------------------------------------------------------------------------------------

	void testSingleTetAscii()
	{
		ResultReadOutcome r = readBytes(buildVtu(singleTet(), Enc::Ascii));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.nodeCount() == 4);
		CHECK(ds.cellCount() == 1);
		CHECK(ds.cellTypes[0] == ResultCellType::Tetra);
		CHECK(ds.stepCount() == 1);

		const ResultField* t = ds.findField(QStringLiteral("T"), ResultFieldAssociation::Node);
		CHECK(t && t->components == 1 && t->tupleCount(0) == 4);
		if (t)
		{
			CHECK(std::fabs(t->stepData[0][0] - 1.5f) < 1e-6f);
			CHECK(std::fabs(t->stepData[0][3] - 4.5f) < 1e-6f);
		}
		const ResultField* v = ds.findField(QStringLiteral("V"), ResultFieldAssociation::Cell);
		CHECK(v && v->components == 3 && v->tupleCount(0) == 1);
		if (v)
			CHECK(v->stepData[0][2] == 9.0f);

		const ResultBoundarySurface s = extract(ds);
		CHECK(s.triangleCount() == 4);
		CHECK(s.vertexCount() == 4);
		CHECK(trianglesFaceOutward(ds, s));
		for (std::size_t i = 0; i < s.vertexCount(); ++i)
			CHECK(s.vertexNode[i] < ds.nodeCount());
	}

	void testEncodingsMatchAscii()
	{
		const Mesh mesh = twoTets();
		ResultReadOutcome ref = readBytes(buildVtu(mesh, Enc::Ascii));
		CHECK(ref.ok());
		if (!ref.ok())
			return;
		const Enc all[] = { Enc::InlineB64, Enc::InlineB64Zlib, Enc::InlineB64Header64, Enc::InlineB64BigEndian,
		                    Enc::AppendedRaw, Enc::AppendedB64, Enc::AppendedRawZlib };
		for (Enc e : all)
		{
			ResultReadOutcome r = readBytes(buildVtu(mesh, e));
			if (!r.ok())
				std::fprintf(stderr, "  encoding %d failed: %s\n", int(e), qPrintable(r.error));
			CHECK(r.ok());
			if (!r.ok())
				continue;
			CHECK(r.dataset->nodePositions == ref.dataset->nodePositions);
			CHECK(r.dataset->cellConnectivity == ref.dataset->cellConnectivity);
			CHECK(r.dataset->cellOffsets == ref.dataset->cellOffsets);
			CHECK(r.dataset->cellTypes == ref.dataset->cellTypes);
			CHECK(r.dataset->fields.size() == ref.dataset->fields.size());
			for (std::size_t f = 0; f < std::min(r.dataset->fields.size(), ref.dataset->fields.size()); ++f)
				CHECK(r.dataset->fields[f].stepData == ref.dataset->fields[f].stepData);
		}
	}

	// Real VTK writers nest <InformationKey> elements inside a DataArray (found on FreeCAD/VTK output);
	// the reader must skip them and still decode the array's own text.
	void testInformationKeyChildren()
	{
		const Mesh mesh = twoTets();
		ResultReadOutcome ref = readBytes(buildVtu(mesh, Enc::Ascii));
		CHECK(ref.ok());
		for (Enc e : { Enc::Ascii, Enc::InlineB64, Enc::InlineB64Zlib })
		{
			QByteArray xml = buildVtu(mesh, e);
			const QByteArray child = "\n  <InformationKey name=\"L2_NORM_RANGE\" location=\"vtkDataArray\" length=\"2\">"
				"\n    <Value index=\"0\">\n      0\n    </Value>\n    <Value index=\"1\">\n      1\n    </Value>\n  </InformationKey>\n";
			xml.replace("</DataArray>", child + "</DataArray>");
			ResultReadOutcome r = readBytes(xml);
			if (!r.ok())
				std::fprintf(stderr, "  InformationKey encoding %d failed: %s\n", int(e), qPrintable(r.error));
			CHECK(r.ok());
			if (r.ok() && ref.ok())
			{
				CHECK(r.dataset->nodePositions == ref.dataset->nodePositions);
				CHECK(r.dataset->fields.size() == ref.dataset->fields.size());
				for (std::size_t f = 0; f < std::min(r.dataset->fields.size(), ref.dataset->fields.size()); ++f)
					CHECK(r.dataset->fields[f].stepData == ref.dataset->fields[f].stepData);
			}
		}
	}

	void testSharedFaceIsInterior()
	{
		ResultReadOutcome r = readBytes(buildVtu(twoTets(), Enc::Ascii));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultBoundarySurface s = extract(*r.dataset);
		CHECK(s.triangleCount() == 6); // 2 tets x 4 faces - the shared face counted once per tet = 8 - 2
		CHECK(trianglesFaceOutward(*r.dataset, s));
	}

	void testHexes()
	{
		{
			ResultReadOutcome r = readBytes(buildVtu(hexes(1), Enc::Ascii));
			CHECK(r.ok());
			if (r.ok())
			{
				const ResultBoundarySurface s = extract(*r.dataset);
				CHECK(s.triangleCount() == 12);
				CHECK(s.vertexCount() == 8);
				CHECK(trianglesFaceOutward(*r.dataset, s));
			}
		}
		{
			ResultReadOutcome r = readBytes(buildVtu(hexes(2), Enc::Ascii));
			CHECK(r.ok());
			if (r.ok())
			{
				const ResultBoundarySurface s = extract(*r.dataset);
				CHECK(s.triangleCount() == 20); // 10 outer quads
				CHECK(s.vertexCount() == 12);
				CHECK(trianglesFaceOutward(*r.dataset, s));
			}
		}
	}

	void testWedgeAndPyramid()
	{
		Mesh w;
		w.pts = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1 };
		w.conn = { 0, 1, 2, 3, 4, 5 };
		w.offs = { 6 };
		w.types = { 13 };
		ResultReadOutcome rw = readBytes(buildVtu(w, Enc::Ascii));
		CHECK(rw.ok());
		if (rw.ok())
		{
			const ResultBoundarySurface s = extract(*rw.dataset);
			CHECK(s.triangleCount() == 8); // 2 triangles + 3 quads (6)
			CHECK(trianglesFaceOutward(*rw.dataset, s));
		}

		Mesh p;
		p.pts = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0.5f, 0.5f, 1 };
		p.conn = { 0, 1, 2, 3, 4 };
		p.offs = { 5 };
		p.types = { 14 };
		ResultReadOutcome rp = readBytes(buildVtu(p, Enc::Ascii));
		CHECK(rp.ok());
		if (rp.ok())
		{
			const ResultBoundarySurface s = extract(*rp.dataset);
			CHECK(s.triangleCount() == 6); // 4 side triangles + base quad (2)
			CHECK(trianglesFaceOutward(*rp.dataset, s));
		}
	}

	void testQuadraticCells()
	{
		// One tet10: 4 corners, then mid-edge nodes for edges 0-1, 1-2, 2-0, 0-3, 1-3, 2-3 (VTK order).
		Mesh m;
		m.pts = { 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2,
		          1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1 };
		m.conn = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
		m.offs = { 10 };
		m.types = { 24 };
		ResultReadOutcome r = readBytes(buildVtu(m, Enc::Ascii));
		CHECK(r.ok());
		if (!r.ok())
			return;
		CHECK(r.dataset->cellTypes[0] == ResultCellType::Tetra10);
		CHECK(!r.warnings.isEmpty()); // "shown through corner nodes only"
		const ResultBoundarySurface s = extract(*r.dataset);
		CHECK(s.triangleCount() == 4);
		CHECK(s.vertexCount() == 4); // corner nodes only
		CHECK(s.skippedCells == 0);
		CHECK(trianglesFaceOutward(*r.dataset, s));
		for (std::size_t i = 0; i < s.vertexCount(); ++i)
			CHECK(s.vertexNode[i] < 4); // mid-edge nodes (4..9) never appear

		// A wrong node count for a quadratic type must be rejected by validate().
		Mesh bad = m;
		bad.conn = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
		bad.offs = { 9 };
		CHECK(!readBytes(buildVtu(bad, Enc::Ascii)).ok());

		// Two tet10 sharing the corner face 1-2-3 (and its mid-edge nodes 5, 8, 9): that face is interior.
		Mesh two = m;
		two.pts.insert(two.pts.end(), { 2, 2, 2,  2, 1, 1,  1, 2, 1,  1, 1, 2 }); // node 10 = apex, 11-13 = its mid-edge nodes
		two.conn = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,   1, 2, 3, 10, 5, 9, 8, 11, 12, 13 };
		two.offs = { 10, 20 };
		two.types = { 24, 24 };
		ResultReadOutcome r2 = readBytes(buildVtu(two, Enc::Ascii));
		CHECK(r2.ok());
		if (r2.ok())
			CHECK(extract(*r2.dataset).triangleCount() == 6);
	}

	// ---- Legacy .vtk ---------------------------------------------------------------------------------

	ResultReadOutcome readLegacy(const QByteArray& bytes)
	{
		return readBytes(bytes, QStringLiteral("t.vtk"));
	}

	template <class T>
	QByteArray beBytes(const std::vector<T>& v)
	{
		QByteArray b;
		for (T x : v)
		{
			unsigned char raw[sizeof(T)];
			std::memcpy(raw, &x, sizeof(T));
			std::reverse(raw, raw + sizeof(T)); // tests run on little-endian hosts; legacy binary is big-endian
			b.append(reinterpret_cast<const char*>(raw), sizeof(T));
		}
		return b;
	}

	const char* kTetAscii =
		"# vtk DataFile Version 3.0\nsingle tet\nASCII\nDATASET UNSTRUCTURED_GRID\n"
		"POINTS 4 float\n0 0 0  1 0 0\n0 1 0  0 0 1\n"
		"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\n"
		"CELL_DATA 1\nSCALARS cellval int 1\nLOOKUP_TABLE default\n7\n"
		"POINT_DATA 4\nSCALARS temp float\nLOOKUP_TABLE default\n1.5 2.5 3.5 4.5\n"
		"VECTORS disp float\n0 0 0  1 0 0  0 1 0  0 0 1\n";

	void testLegacyAsciiAndBinary()
	{
		ResultReadOutcome a = readLegacy(QByteArray(kTetAscii));
		if (!a.ok())
			std::fprintf(stderr, "  legacy ascii failed: %s\n", qPrintable(a.error));
		CHECK(a.ok());
		if (a.ok())
		{
			CHECK(a.dataset->findField(QStringLiteral("disp"), ResultFieldAssociation::Node) != nullptr);
			CHECK(a.dataset->findField(QStringLiteral("temp"), ResultFieldAssociation::Node)->stepData[0][3] == 4.5f);
			CHECK(a.dataset->findField(QStringLiteral("cellval"), ResultFieldAssociation::Cell)->stepData[0][0] == 7.0f);
			CHECK(a.dataset->cellCount() == 1 && a.dataset->nodeCount() == 4);
			CHECK(extract(*a.dataset).triangleCount() == 4);
		}

		// Windows line endings.
		QByteArray crlf = kTetAscii;
		crlf.replace("\n", "\r\n");
		ResultReadOutcome c = readLegacy(crlf);
		CHECK(c.ok());
		if (c.ok())
			CHECK(c.dataset->nodePositions == a.dataset->nodePositions);

		// The same mesh as a big-endian BINARY file.
		QByteArray bin = "# vtk DataFile Version 3.0\nsingle tet\nBINARY\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n";
		bin += beBytes(std::vector<float>{ 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 });
		bin += "\nCELLS 1 5\n";
		bin += beBytes(std::vector<std::int32_t>{ 4, 0, 1, 2, 3 });
		bin += "\nCELL_TYPES 1\n";
		bin += beBytes(std::vector<std::int32_t>{ 10 });
		bin += "\nPOINT_DATA 4\nSCALARS temp float 1\nLOOKUP_TABLE default\n";
		bin += beBytes(std::vector<float>{ 1.5f, 2.5f, 3.5f, 4.5f });
		bin += "\nVECTORS disp double\n";
		bin += beBytes(std::vector<double>{ 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 });
		bin += "\n";
		ResultReadOutcome b = readLegacy(bin);
		if (!b.ok())
			std::fprintf(stderr, "  legacy binary failed: %s\n", qPrintable(b.error));
		CHECK(b.ok());
		if (b.ok() && a.ok())
		{
			CHECK(b.dataset->nodePositions == a.dataset->nodePositions);
			CHECK(b.dataset->cellConnectivity == a.dataset->cellConnectivity);
			CHECK(b.dataset->findField(QStringLiteral("temp"), ResultFieldAssociation::Node)->stepData[0]
			      == a.dataset->findField(QStringLiteral("temp"), ResultFieldAssociation::Node)->stepData[0]);
			CHECK(b.dataset->findField(QStringLiteral("disp"), ResultFieldAssociation::Node)->stepData[0]
			      == a.dataset->findField(QStringLiteral("disp"), ResultFieldAssociation::Node)->stepData[0]);
		}

		// Truncated binary data must fail cleanly.
		CHECK(!readLegacy(bin.left(bin.indexOf("CELLS") + 20)).ok());
	}

	void testLegacyNewCellLayoutAndFieldData()
	{
		// VTK 5.x layout: OFFSETS / CONNECTIVITY, plus dataset-level and cell-level FIELD arrays.
		const QByteArray v5 =
			"# vtk DataFile Version 5.1\ntwo tets\nASCII\nDATASET UNSTRUCTURED_GRID\n"
			"FIELD FieldData 1\nTIME 1 1 double\n2.5\n"
			"POINTS 5 double\n0 0 0  1 0 0  0 1 0  0 0 1  1 1 1\n"
			"CELLS 3 8\nOFFSETS vtktypeint64\n0 4 8\nCONNECTIVITY vtktypeint64\n0 1 2 3  1 2 3 4\n"
			"CELL_TYPES 2\n10\n10\n"
			"CELL_DATA 2\nFIELD attributes 2\nstress 1 2 float\n10 20\nignored 1 9 float\n1 2 3 4 5 6 7 8 9\n";
		ResultReadOutcome r = readLegacy(v5);
		if (!r.ok())
			std::fprintf(stderr, "  legacy 5.x failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (!r.ok())
			return;
		CHECK(r.dataset->cellCount() == 2 && r.dataset->nodeCount() == 5);
		CHECK(r.dataset->steps.front().time == 2.5);
		const ResultField* s = r.dataset->findField(QStringLiteral("stress"), ResultFieldAssociation::Cell);
		CHECK(s && s->tupleCount(0) == 2);
		CHECK(r.dataset->findField(QStringLiteral("ignored"), ResultFieldAssociation::Cell) == nullptr);
		CHECK(!r.warnings.isEmpty()); // the 9-tuple metadata array
		CHECK(extract(*r.dataset).triangleCount() == 6);
	}

	void testLegacyPolyData()
	{
		const QByteArray poly =
			"# vtk DataFile Version 3.0\npoly\nASCII\nDATASET POLYDATA\n"
			"POINTS 5 float\n0 0 0  1 0 0  1 1 0  0 1 0  0.5 1.5 0\n"
			"LINES 2 7\n2 0 1  3 1 2 3\n"
			"POLYGONS 3 15\n3 0 1 2  4 0 1 2 3  5 0 1 2 3 4\n"
			"CELL_DATA 5\nSCALARS id int\nLOOKUP_TABLE default\n0 1 2 3 4\n";
		ResultReadOutcome r = readLegacy(poly);
		if (!r.ok())
			std::fprintf(stderr, "  legacy polydata failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.cellCount() == 5);
		// VTK cell order for polydata: vertices, lines, polygons, strips.
		CHECK(ds.cellTypes[0] == ResultCellType::Line);
		CHECK(ds.cellTypes[1] == ResultCellType::Unsupported); // 3-point polyline
		CHECK(ds.cellTypes[2] == ResultCellType::Triangle);
		CHECK(ds.cellTypes[3] == ResultCellType::Quad);
		CHECK(ds.cellTypes[4] == ResultCellType::Unsupported); // pentagon
		CHECK(!r.warnings.isEmpty());
		const ResultBoundarySurface s = extract(ds);
		CHECK(s.triangleCount() == 3);
		CHECK(s.skippedCells == 3);
	}

	void testLegacyStructured()
	{
		// STRUCTURED_POINTS 3x3x3 -> 8 hexes; boundary = 6 faces x 4 quads = 48 triangles on 26 vertices.
		QByteArray sp = "# vtk DataFile Version 2.0\nvol\nASCII\nDATASET STRUCTURED_POINTS\nDIMENSIONS 3 3 3\n"
		                "ORIGIN 1 2 3\nSPACING 0.5 0.5 0.5\nPOINT_DATA 27\nSCALARS s float\nLOOKUP_TABLE default\n";
		for (int i = 0; i < 27; ++i)
			sp += QByteArray::number(i) + '\n';
		ResultReadOutcome r = readLegacy(sp);
		if (!r.ok())
			std::fprintf(stderr, "  legacy structured points failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (r.ok())
		{
			CHECK(r.dataset->nodeCount() == 27 && r.dataset->cellCount() == 8);
			CHECK(r.dataset->nodePositions[0] == 1.0f && r.dataset->nodePositions[2] == 3.0f);
			CHECK(r.dataset->nodePositions[3] == 1.5f); // second node is one spacing along x
			const ResultBoundarySurface s = extract(*r.dataset);
			CHECK(s.triangleCount() == 48);
			CHECK(s.vertexCount() == 26);
			CHECK(trianglesFaceOutward(*r.dataset, s));
		}

		// STRUCTURED_GRID 2x2x2 -> one hexahedron.
		const QByteArray sg = "# vtk DataFile Version 3.0\ng\nASCII\nDATASET STRUCTURED_GRID\nDIMENSIONS 2 2 2\nPOINTS 8 float\n"
		                      "0 0 0 1 0 0 0 1 0 1 1 0 0 0 1 1 0 1 0 1 1 1 1 1\n";
		ResultReadOutcome g = readLegacy(sg);
		CHECK(g.ok());
		if (g.ok())
		{
			CHECK(g.dataset->cellCount() == 1 && g.dataset->cellTypes[0] == ResultCellType::Hexahedron);
			CHECK(extract(*g.dataset).triangleCount() == 12);
		}

		// RECTILINEAR_GRID 3x2x1 -> a plane of 2 quads.
		const QByteArray rg = "# vtk DataFile Version 3.0\nr\nASCII\nDATASET RECTILINEAR_GRID\nDIMENSIONS 3 2 1\n"
		                      "X_COORDINATES 3 float\n0 1 3\nY_COORDINATES 2 float\n0 2\nZ_COORDINATES 1 float\n5\n";
		ResultReadOutcome rr = readLegacy(rg);
		CHECK(rr.ok());
		if (rr.ok())
		{
			CHECK(rr.dataset->nodeCount() == 6 && rr.dataset->cellCount() == 2);
			CHECK(rr.dataset->cellTypes[0] == ResultCellType::Quad);
			CHECK(extract(*rr.dataset).triangleCount() == 4);
		}
	}

	void testLegacyErrors()
	{
		// Attribute-only file (like VTKData's blowAttr.vtk).
		ResultReadOutcome noDataset = readLegacy("# vtk DataFile Version 1.0\nattrs\nASCII\n\nFIELD time0 1\nt 1 2 float\n1 2\n");
		CHECK(!noDataset.ok());
		CHECK(noDataset.error.contains(QStringLiteral("DATASET")));

		CHECK(!readLegacy("not a vtk file at all").ok());
		CHECK(!readLegacy("# vtk DataFile Version 3.0\nx\nASCII\nDATASET FOO\n").ok());

		// CELLS/CELL_TYPES disagree.
		ResultReadOutcome mismatch = readLegacy(
			"# vtk DataFile Version 3.0\nx\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 2\n10 10\n");
		CHECK(!mismatch.ok());

		// A node index past the point count is caught by validation.
		ResultReadOutcome badIndex = readLegacy(
			"# vtk DataFile Version 3.0\nx\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 9\nCELL_TYPES 1\n10\n");
		CHECK(!badIndex.ok());
		CHECK(badIndex.error.contains(QStringLiteral("references node")));

		// A field whose tuple count does not match is dropped with a warning, not fatal.
		ResultReadOutcome badField = readLegacy(
			"# vtk DataFile Version 3.0\nx\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\nPOINT_DATA 3\nSCALARS s float\nLOOKUP_TABLE default\n1 2 3\n");
		CHECK(badField.ok());
		if (badField.ok())
		{
			CHECK(badField.dataset->fields.empty());
			CHECK(!badField.warnings.isEmpty());
		}
	}

	void testShellAndSkippedCells()
	{
		Mesh m;
		m.pts = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 2, 0, 0 };
		// triangle, quad, line, and a polyhedron (type 42, unsupported in Phase 0) with 9 arbitrary nodes.
		m.conn = { 0, 1, 2, 0, 1, 2, 3, 0, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4 };
		m.offs = { 3, 7, 9, 19 };
		m.types = { 5, 9, 3, 42 };
		ResultReadOutcome r = readBytes(buildVtu(m, Enc::Ascii));
		CHECK(r.ok());
		if (!r.ok())
			return;
		CHECK(!r.warnings.isEmpty()); // the unsupported polyhedron
		const ResultBoundarySurface s = extract(*r.dataset);
		CHECK(s.triangleCount() == 3); // 1 triangle + quad as 2
		CHECK(s.skippedCells == 2);    // the line and the unsupported cell
		for (std::size_t t = 0; t < s.triangleCount(); ++t)
			CHECK(s.triangleFace[t] == ResultBoundarySurface::kNoFace);
	}

	void testErrors()
	{
		CHECK(!readResultFile(tempDir().filePath(QStringLiteral("missing.vtu"))).ok());
		CHECK(!readResultFile(QStringLiteral("x.unknown")).ok());
		CHECK(!isSupportedResultFile(QStringLiteral("x.stl")));
		CHECK(isSupportedResultFile(QStringLiteral("a/b/C.VTU")));

		ResultReadOutcome poly = readBytes(buildVtu(singleTet(), Enc::Ascii, "PolyData"));
		CHECK(!poly.ok());

		ResultReadOutcome lz4 = readBytes(buildVtu(singleTet(), Enc::Ascii, "UnstructuredGrid", "vtkLZ4DataCompressor"));
		CHECK(!lz4.ok());
		CHECK(lz4.error.contains(QStringLiteral("compressor")));

		Mesh bad = singleTet();
		bad.conn = { 0, 1, 2, 9 }; // node 9 does not exist
		ResultReadOutcome r = readBytes(buildVtu(bad, Enc::Ascii));
		CHECK(!r.ok());
		CHECK(r.error.contains(QStringLiteral("references node")));

		ResultReadOutcome garbage = readBytes(QByteArray("this is not xml"));
		CHECK(!garbage.ok());
	}

	void testCancellation()
	{
		const QString path = tempDir().filePath(QStringLiteral("c.vtu"));
		QFile f(path);
		CHECK(f.open(QIODevice::WriteOnly));
		f.write(buildVtu(singleTet(), Enc::Ascii));
		f.close();
		std::atomic<bool> cancel(true);
		ResultReadOutcome r = readResultFile(path, &cancel);
		CHECK(!r.ok());
		CHECK(r.error == QStringLiteral("cancelled"));

		ResultReadOutcome ok = readResultFile(path);
		CHECK(ok.ok());
		if (ok.ok())
		{
			ResultBoundarySurface s;
			QString err;
			CHECK(!extractBoundarySurface(*ok.dataset, s, &cancel, &err));
			CHECK(err == QStringLiteral("cancelled"));
		}
	}

	void testLargeMeshPartitioning()
	{
		// An n x n x n hex block: the boundary is 6*n*n quads = 12*n*n triangles, and the unique boundary
		// vertices are all nodes except the (n-1)^3 fully interior ones.
		const int n = 30;
		Mesh m;
		auto node = [n](int i, int j, int k) { return i + (n + 1) * (j + (n + 1) * k); };
		for (int k = 0; k <= n; ++k)
			for (int j = 0; j <= n; ++j)
				for (int i = 0; i <= n; ++i)
					m.pts.insert(m.pts.end(), { float(i), float(j), float(k) });
		for (int k = 0; k < n; ++k)
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					m.conn.insert(m.conn.end(),
						{ node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k),
						  node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1) });
					m.offs.push_back(static_cast<int>(m.conn.size()));
					m.types.push_back(12);
				}
		ResultReadOutcome r = readBytes(buildVtu(m, Enc::InlineB64Zlib), QStringLiteral("big.vtu"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const std::size_t expectedTriangles = 12u * n * n;
		const std::size_t expectedVertices = (n + 1u) * (n + 1u) * (n + 1u) - (n - 1u) * (n - 1u) * (n - 1u);

		// Default (single partition) and a tiny partition size that forces the multi-pass hashing
		// (162,000 faces / 500 => clamped to 64 partitions) must give identical results.
		const ResultBoundarySurface single = extract(*r.dataset);
		ResultBoundarySurface multi;
		QString err;
		CHECK(extractBoundarySurface(*r.dataset, multi, nullptr, &err, 500));
		CHECK(single.triangleCount() == expectedTriangles);
		CHECK(single.vertexCount() == expectedVertices);
		CHECK(multi.triangles == single.triangles);
		CHECK(multi.positions == single.positions);
		CHECK(multi.triangleCell == single.triangleCell);
		CHECK(multi.triangleFace == single.triangleFace);
		CHECK(trianglesFaceOutward(*r.dataset, multi));
	}
}

// `result_tests <file.vtu> [more.vtu ...]` loads real files instead of running the synthetic tests and
// prints what was read - the check against output from FreeCAD/ParaView/etc. that fixtures cannot give.
static int inspectFiles(int argc, char** argv)
{
	int failed = 0;
	for (int i = 1; i < argc; ++i)
	{
		const QString path = QString::fromLocal8Bit(argv[i]);
		std::printf("\n%s\n", qPrintable(path));
		ResultReadOutcome r = readResultFile(path);
		for (const QString& w : r.warnings)
			std::printf("  warning: %s\n", qPrintable(w));
		if (!r.ok())
		{
			std::printf("  FAILED: %s\n", qPrintable(r.error));
			++failed;
			continue;
		}
		const ResultDataset& ds = *r.dataset;
		std::size_t byType[16] = {};
		for (ResultCellType t : ds.cellTypes)
			++byType[static_cast<int>(t)];
		std::printf("  nodes: %zu   cells: %zu   steps: %zu (time %g)\n", ds.nodeCount(), ds.cellCount(), ds.stepCount(),
			ds.steps.empty() ? 0.0 : ds.steps.front().time);
		std::printf("  cell types: unsupported %zu, line %zu, tri %zu, quad %zu, tet %zu, hex %zu, wedge %zu, pyramid %zu\n",
			byType[0], byType[1], byType[2], byType[3], byType[4], byType[5], byType[6], byType[7]);
		std::printf("  quadratic: tri6 %zu, quad8 %zu, tet10 %zu, hex20 %zu, wedge15 %zu, pyramid13 %zu\n",
			byType[8], byType[9], byType[10], byType[11], byType[12], byType[13]);
		if (ds.nodeCount() > 0)
		{
			float lo[3] = { ds.nodePositions[0], ds.nodePositions[1], ds.nodePositions[2] }, hi[3] = { lo[0], lo[1], lo[2] };
			for (std::size_t n = 1; n < ds.nodeCount(); ++n)
				for (int k = 0; k < 3; ++k)
				{
					lo[k] = std::min(lo[k], ds.nodePositions[n * 3 + k]);
					hi[k] = std::max(hi[k], ds.nodePositions[n * 3 + k]);
				}
			std::printf("  bounds: (%g, %g, %g) .. (%g, %g, %g)\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
		}
		for (const ResultField& f : ds.fields)
		{
			const std::vector<float>* v = f.stepData.empty() ? nullptr : &f.stepData.front();
			float mn = 0, mx = 0;
			if (v && !v->empty())
			{
				mn = *std::min_element(v->begin(), v->end());
				mx = *std::max_element(v->begin(), v->end());
			}
			std::printf("  %s field '%s': %d comp, %zu tuples, values %g .. %g\n",
				f.association == ResultFieldAssociation::Node ? "node" : "cell", qPrintable(f.name), f.components,
				f.tupleCount(0), mn, mx);
		}
		ResultBoundarySurface s;
		QString err;
		if (extractBoundarySurface(ds, s, nullptr, &err))
			std::printf("  boundary: %zu triangles, %zu vertices, %zu skipped cells\n", s.triangleCount(), s.vertexCount(), s.skippedCells);
		else
		{
			std::printf("  boundary FAILED: %s\n", qPrintable(err));
			++failed;
		}
	}
	return failed;
}

int main(int argc, char** argv)
{
	if (argc > 1)
		return inspectFiles(argc, argv);

	testSingleTetAscii();
	testEncodingsMatchAscii();
	testInformationKeyChildren();
	testSharedFaceIsInterior();
	testHexes();
	testWedgeAndPyramid();
	testQuadraticCells();
	testLegacyAsciiAndBinary();
	testLegacyNewCellLayoutAndFieldData();
	testLegacyPolyData();
	testLegacyStructured();
	testLegacyErrors();
	testShellAndSkippedCells();
	testErrors();
	testCancellation();
	testLargeMeshPartitioning();

	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
