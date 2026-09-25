// GUI-free tests for the simulation result reader and boundary extraction (docs/simulation_results_design.md).
// Build with -DMV_BUILD_TESTS=ON; run the `result_tests` executable. Exit code = number of failed checks.
//
// Every fixture is generated in code (no binary files in the repo) in each VTK XML data encoding, so a
// bug in header/offset/compression handling shows up as a mismatch against the same mesh written as
// plain ASCII. NOTE: the writers here are written from the VTK format description, like the reader, so
// they cannot catch a shared misreading of the spec - real files (FreeCAD/CalculiX/ParaView output) are
// the final check and are listed in docs/simulation_results_test_data.md.

#include "ResultBoundary.h"
#include "ResultDerivedFields.h"
#include "ResultReader.h"
#include "ResultUnits.h"
#include "SimulationResultDisplay.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
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

	// ---- Simulation result display logic --------------------------------------------------------------

	void testSimulationDisplay()
	{
		// Fields (in order): aaa (scalar), von Mises Stress (scalar, name percent-encoded in the legacy file),
		// disp (vector). The default must prefer the von Mises field over the first scalar.
		const QByteArray fixture =
			"# vtk DataFile Version 3.0\nt\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\nPOINT_DATA 4\n"
			"SCALARS aaa float\nLOOKUP_TABLE default\n1 2 3 4\n"
			"SCALARS von%20Mises%20Stress float\nLOOKUP_TABLE default\n10 20 30 40\n"
			"VECTORS disp float\n0 0 0  3 4 0  0 0 0  0 0 12\n";
		ResultReadOutcome r = readLegacy(fixture);
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.fields.size() == 3);
		CHECK(ds.fields[1].name == QStringLiteral("von Mises Stress"));

		DisplayScalar def;
		CHECK(chooseDefaultDisplayScalar(ds, def));
		CHECK(def.fieldIndex == 1);
		CHECK(def.label == QStringLiteral("von Mises Stress"));
		CHECK(def.minValue == 10.0f && def.maxValue == 40.0f);

		// Vector magnitude and single components.
		DisplayScalar mag;
		CHECK(buildDisplayScalar(ds, 2, -1, mag));
		CHECK(mag.nodeValues.size() == 4);
		CHECK(std::fabs(mag.nodeValues[1] - 5.0f) < 1e-6f && std::fabs(mag.nodeValues[3] - 12.0f) < 1e-6f);
		CHECK(mag.minValue == 0.0f && mag.maxValue == 12.0f);
		CHECK(mag.label.contains(QStringLiteral("magnitude")));
		DisplayScalar comp;
		CHECK(buildDisplayScalar(ds, 2, 1, comp));
		CHECK(comp.nodeValues[1] == 4.0f && comp.nodeValues[3] == 0.0f);
		CHECK(!buildDisplayScalar(ds, 2, 5, comp)); // no such component
		CHECK(!buildDisplayScalar(ds, 7, -1, comp)); // no such field

		// With only a vector field, the default falls back to its magnitude.
		const QByteArray vectorOnly =
			"# vtk DataFile Version 3.0\nt\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\nPOINT_DATA 4\nVECTORS disp float\n0 0 0  3 4 0  0 0 0  0 0 12\n";
		ResultReadOutcome rv = readLegacy(vectorOnly);
		CHECK(rv.ok());
		if (rv.ok())
		{
			DisplayScalar d;
			CHECK(chooseDefaultDisplayScalar(*rv.dataset, d));
			CHECK(d.label.contains(QStringLiteral("magnitude")));
		}

		// No node field at all: nothing to colour by (the geometry is still displayable).
		Mesh bare = singleTet();
		bare.pointScalar.clear();
		ResultReadOutcome rb = readBytes(buildVtu(bare, Enc::Ascii));
		CHECK(rb.ok());
		if (rb.ok())
		{
			DisplayScalar d;
			CHECK(!chooseDefaultDisplayScalar(*rb.dataset, d)); // only a CELL vector field exists
		}

		// Non-finite values are excluded from the range; an all-non-finite field is rejected.
		const QByteArray withNan =
			"# vtk DataFile Version 3.0\nt\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\nPOINT_DATA 4\nSCALARS s float\nLOOKUP_TABLE default\n2 nan 5 3\n"
			"SCALARS allnan float\nLOOKUP_TABLE default\nnan nan nan nan\n";
		ResultReadOutcome rn = readLegacy(withNan);
		CHECK(rn.ok());
		if (rn.ok())
		{
			DisplayScalar d;
			CHECK(buildDisplayScalar(*rn.dataset, 0, -1, d));
			CHECK(d.minValue == 2.0f && d.maxValue == 5.0f);
			CHECK(!buildDisplayScalar(*rn.dataset, 1, -1, d));
		}

		// Per-vertex values follow the boundary's vertex -> node map.
		const ResultBoundarySurface s = extract(ds);
		const std::vector<float> perVertex = boundaryVertexValues(s, def.nodeValues);
		CHECK(perVertex.size() == s.vertexCount());
		for (std::size_t v = 0; v < s.vertexCount(); ++v)
			CHECK(perVertex[v] == def.nodeValues[s.vertexNode[v]]);

		// Smooth normals: unit length and pointing away from the tetrahedron's centroid.
		const std::vector<float> n = computeSmoothVertexNormals(s);
		CHECK(n.size() == s.vertexCount() * 3);
		for (std::size_t v = 0; v < s.vertexCount(); ++v)
		{
			const float* nv = &n[v * 3];
			CHECK(std::fabs(std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]) - 1.0f) < 1e-5f);
			const float* p = &s.positions[v * 3];
			const float dot = nv[0] * (p[0] - 0.25f) + nv[1] * (p[1] - 0.25f) + nv[2] * (p[2] - 0.25f);
			CHECK(dot > 0.0f);
		}
	}

	void testViewState()
	{
		// resolveViewRange: automatic uses the data range, custom uses the state's, degenerate ranges are widened.
		DisplayScalar s;
		s.minValue = 2.0f;
		s.maxValue = 8.0f;
		SimulationViewState st;
		float lo = 0, hi = 0;
		CHECK(resolveViewRange(s, st, lo, hi));
		CHECK(lo == 2.0f && hi == 8.0f);

		st.customRange = true;
		st.rangeMin = -5.0;
		st.rangeMax = 10.0;
		CHECK(resolveViewRange(s, st, lo, hi));
		CHECK(lo == -5.0f && hi == 10.0f);

		st.rangeMax = st.rangeMin; // min == max
		CHECK(resolveViewRange(s, st, lo, hi));
		CHECK(hi > lo);
		st.rangeMax = -20.0;       // min > max
		CHECK(resolveViewRange(s, st, lo, hi));
		CHECK(hi > lo);
		st.rangeMin = std::nan("");
		CHECK(!resolveViewRange(s, st, lo, hi));

		s.minValue = s.maxValue = 5.0f; // a constant field, automatic range
		SimulationViewState autoState;
		CHECK(resolveViewRange(s, autoState, lo, hi));
		CHECK(hi > lo);

		// Band count: smooth (0/1) is a fine 256 levels, otherwise the chosen count.
		SimulationViewState b;
		CHECK(simulationShaderBands(b) == kSimulationSmoothBands);
		b.bands = 1;
		CHECK(simulationShaderBands(b) == kSimulationSmoothBands);
		b.bands = 12;
		CHECK(simulationShaderBands(b) == 12);

		// defaultViewState: the von Mises scalar of a fixture, and "nothing to colour by" when there is no node field.
		const QByteArray fixture =
			"# vtk DataFile Version 3.0\nt\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\nPOINT_DATA 4\n"
			"SCALARS aaa float\nLOOKUP_TABLE default\n1 2 3 4\n"
			"SCALARS von%20Mises%20Stress float\nLOOKUP_TABLE default\n10 20 30 40\n";
		ResultReadOutcome r = readLegacy(fixture);
		CHECK(r.ok());
		if (r.ok())
		{
			DisplayScalar scalar;
			const SimulationViewState d = defaultViewState(*r.dataset, &scalar);
			CHECK(d.fieldIndex == 1);
			CHECK(!d.customRange && d.colormap == 0 && d.bands == 0);
			CHECK(scalar.valid() && scalar.minValue == 10.0f);
		}
		Mesh bare = singleTet();
		bare.pointScalar.clear();
		ResultReadOutcome rb = readBytes(buildVtu(bare, Enc::Ascii));
		CHECK(rb.ok());
		if (rb.ok())
		{
			DisplayScalar scalar;
			CHECK(defaultViewState(*rb.dataset, &scalar).fieldIndex == -1);
			CHECK(!scalar.valid());
		}
	}

	// ---- CalculiX .frd ---------------------------------------------------------------------------------------------

	// Fixed-width record writers, as CalculiX writes them (E12.5 fields touch each other for negative numbers).
	std::string frdNodeLine(long id, double x, double y, double z, int idWidth = 10)
	{
		char b[160];
		std::snprintf(b, sizeof b, " -1%*ld%12.5E%12.5E%12.5E\n", idWidth, id, x, y, z);
		return b;
	}

	std::string frdValueLines(long id, const std::vector<double>& v, int idWidth = 10)
	{
		std::string out;
		char b[160];
		std::snprintf(b, sizeof b, " -1%*ld", idWidth, id);
		out += b;
		for (std::size_t i = 0; i < v.size(); ++i)
		{
			if (i > 0 && i % 6 == 0)
			{
				out += "\n";
				std::snprintf(b, sizeof b, " -2%*s", idWidth, "");
				out += b;
			}
			std::snprintf(b, sizeof b, "%12.5E", v[i]);
			out += b;
		}
		return out + "\n";
	}

	// nodes 10,20,30,40 (non-contiguous ids), one TE4 element, two result times; node 40 is left out of DISP.
	std::string makeFrd(int format = 1, int typeCode = 3, long elementNode4 = 40)
	{
		const int w = format == 0 ? 5 : 10;
		std::string f = "    1C\n    1UUSER\n";
		f += "    2C                             4                                     " + std::to_string(format) + "\n";
		f += frdNodeLine(10, 0, 0, 0, w) + frdNodeLine(20, 1, 0, 0, w) + frdNodeLine(30, 0, 1, 0, w) + frdNodeLine(40, 0, 0, 1, w);
		f += " -3\n    3C                             1                                     " + std::to_string(format) + "\n";
		char b[160];
		std::snprintf(b, sizeof b, " -1%*ld%5d%5d%5d\n", w, 7L, typeCode, 0, 1);
		f += b;
		std::snprintf(b, sizeof b, " -2%*ld%*ld%*ld%*ld\n", w, 10L, w, 20L, w, 30L, w, elementNode4);
		f += b;
		f += " -3\n";
		auto header = [&](const char* time) {
			char h[200];
			std::snprintf(h, sizeof h, "    1PSTEP                         1           1           1\n  100CL  101 %s           4                     0    1           %d\n", time, format);
			return std::string(h);
		};
		// step 1: DISP (3 stored components + a calculated "ALL"), node 40 absent
		f += header("1.000000000");
		f += " -4  DISP        4    1\n -5  D1          1    2    1    0\n -5  D2          1    2    2    0\n -5  D3          1    2    3    0\n"
		     " -5  ALL         1    2    0    0    1ALL\n";
		f += frdValueLines(10, { 0, 0, 0 }, w) + frdValueLines(20, { -1.0e-3, 2.0e-3, 0.0 }, w) + frdValueLines(30, { 0, 0, 3.0e-3 }, w) + " -3\n";
		// step 1: STRESS, six components in one line, all four nodes
		f += header("1.000000000");
		f += " -4  STRESS      6    1\n -5  SXX         1    4    1    1\n -5  SYY         1    4    2    2\n -5  SZZ         1    4    3    3\n"
		     " -5  SXY         1    4    1    2\n -5  SYZ         1    4    2    3\n -5  SZX         1    4    3    1\n";
		f += frdValueLines(10, { 100, 0, 0, 0, 0, 0 }, w)       // uniaxial: von Mises 100, principals 100/0/0
		   + frdValueLines(20, { 0, 0, 0, 10, 0, 0 }, w)        // pure shear: principals 10/0/-10, von Mises sqrt(300)
		   + frdValueLines(30, { -5, -5, -5, 0, 0, 0 }, w)      // hydrostatic: von Mises 0
		   + frdValueLines(40, { 10, 20, 30, 4, 5, 6 }, w) + " -3\n";
		// step 2 (later time): DISP only
		f += header("2.000000000");
		f += " -4  DISP        4    1\n -5  D1          1    2    1    0\n -5  D2          1    2    2    0\n -5  D3          1    2    3    0\n"
		     " -5  ALL         1    2    0    0    1ALL\n";
		f += frdValueLines(10, { 1, 1, 1 }, w) + frdValueLines(20, { 2, 2, 2 }, w) + " -3\n 9999\n";
		return f;
	}

	bool approx(double a, double b, double relTol = 1e-4, double absTol = 1e-9)
	{
		return std::fabs(a - b) <= absTol + relTol * std::fabs(b);
	}

	void testDerivedStress()
	{
		double e1, e2, e3;
		symmetricPrincipalValues(100, 0, 0, 0, 0, 0, e1, e2, e3);
		CHECK(approx(e1, 100) && approx(e2, 0, 1e-4, 1e-9) && approx(e3, 0, 1e-4, 1e-9));
		symmetricPrincipalValues(0, 0, 0, 10, 0, 0, e1, e2, e3); // pure shear
		CHECK(approx(e1, 10) && approx(e2, 0, 1e-4, 1e-9) && approx(e3, -10));
		symmetricPrincipalValues(10, 20, 30, 4, 5, 6, e1, e2, e3); // general: trace and ordering
		CHECK(e1 >= e2 && e2 >= e3);
		CHECK(approx(e1 + e2 + e3, 60.0));
		// invariant: sum of pairwise products = xx*yy + yy*zz + zz*xx - xy^2 - yz^2 - zx^2
		CHECK(approx(e1 * e2 + e2 * e3 + e3 * e1, 10 * 20 + 20 * 30 + 30 * 10 - 16 - 25 - 36));
		CHECK(approx(vonMisesStress(100, 0, 0, 0, 0, 0), 100));
		CHECK(approx(vonMisesStress(0, 0, 0, 10, 0, 0), std::sqrt(300.0)));
		CHECK(approx(vonMisesStress(-5, -5, -5, 0, 0, 0), 0, 1e-4, 1e-9));
		CHECK(approx(vonMisesStress(10, 20, 30, 4, 5, 6), std::sqrt(531.0)));
	}

	void testFrdSynthetic()
	{
		for (int format : { 1, 0 }) // long and short ASCII
		{
			ResultReadOutcome r = readBytes(QByteArray::fromStdString(makeFrd(format)), QStringLiteral("t.frd"));
			if (!r.ok())
				std::fprintf(stderr, "  frd format %d failed: %s\n", format, qPrintable(r.error));
			CHECK(r.ok());
			if (!r.ok())
				continue;
			const ResultDataset& ds = *r.dataset;
			CHECK(ds.nodeCount() == 4 && ds.cellCount() == 1);
			CHECK(ds.nodeIds == (std::vector<std::int64_t>{ 10, 20, 30, 40 }));
			CHECK(ds.cellIds == (std::vector<std::int64_t>{ 7 }));
			CHECK(ds.cellTypes[0] == ResultCellType::Tetra);
			CHECK(ds.nodePositions[3] == 1.0f && ds.nodePositions[7] == 1.0f && ds.nodePositions[11] == 1.0f); // ids 20, 30, 40
			CHECK(ds.steps.size() == 2);
			CHECK(ds.steps[0].time == 1.0 && ds.steps[1].time == 2.0);

			const ResultField* disp = ds.findField(QStringLiteral("DISP"), ResultFieldAssociation::Node);
			CHECK(disp && disp->components == 3); // the calculated "ALL" is not a stored component
			if (disp)
			{
				CHECK(disp->componentNames.size() == 3 && disp->componentNames[0] == QStringLiteral("D1"));
				CHECK(disp->stepData.size() == 2);
				// glued negative numbers ("-1.00000E-03" fills its whole field) parse correctly
				CHECK(approx(disp->stepData[0][1 * 3 + 0], -1.0e-3) && approx(disp->stepData[0][1 * 3 + 1], 2.0e-3));
				CHECK(std::isnan(disp->stepData[0][3 * 3]));  // node 40 is absent from the step-1 DISP block
				CHECK(disp->stepData[1].size() == 12 && disp->stepData[1][0] == 1.0f);
			}
			const ResultField* stress = ds.findField(QStringLiteral("STRESS"), ResultFieldAssociation::Node);
			CHECK(stress && stress->components == 6 && stress->componentNames.size() == 6);
			if (stress)
			{
				CHECK(stress->stepData.size() == 2 && stress->stepData[1].empty()); // no STRESS at step 2
				CHECK(stress->componentNames[3] == QStringLiteral("SXY"));
			}

			// derived fields: node 10 uniaxial, node 20 pure shear, node 30 hydrostatic, node 40 general
			const ResultField* vm = ds.findField(QStringLiteral("STRESS von Mises"), ResultFieldAssociation::Node);
			const ResultField* p1 = ds.findField(QStringLiteral("STRESS max principal"), ResultFieldAssociation::Node);
			const ResultField* p3 = ds.findField(QStringLiteral("STRESS min principal"), ResultFieldAssociation::Node);
			const ResultField* sh = ds.findField(QStringLiteral("STRESS max shear"), ResultFieldAssociation::Node);
			CHECK(vm && p1 && p3 && sh && ds.findField(QStringLiteral("STRESS mid principal"), ResultFieldAssociation::Node));
			if (vm && p1 && p3 && sh)
			{
				CHECK(approx(vm->stepData[0][0], 100) && approx(vm->stepData[0][1], std::sqrt(300.0))
				      && approx(vm->stepData[0][2], 0, 1e-4, 1e-4) && approx(vm->stepData[0][3], std::sqrt(531.0)));
				CHECK(approx(p1->stepData[0][0], 100) && approx(p1->stepData[0][1], 10));
				CHECK(approx(p3->stepData[0][1], -10));
				CHECK(approx(sh->stepData[0][1], 10) && approx(sh->stepData[0][0], 50));
				CHECK(vm->stepData[1].empty());
			}

			const ResultBoundarySurface surface = extract(ds);
			CHECK(surface.triangleCount() == 4);

			// the default field is the derived von Mises
			DisplayScalar scalar;
			CHECK(chooseDefaultDisplayScalar(ds, scalar));
			CHECK(scalar.label == QStringLiteral("STRESS von Mises"));
		}
	}

	void testFrdErrors()
	{
		ResultReadOutcome rb = readBytes(QByteArray::fromStdString(makeFrd(2)), QStringLiteral("t.frd")); // format flag 2 = binary
		CHECK(!rb.ok());
		CHECK(rb.error.contains(QStringLiteral("Binary")));

		CHECK(!readBytes(QByteArray::fromStdString(makeFrd(1, 99)), QStringLiteral("t.frd")).ok());       // unknown element type
		CHECK(!readBytes(QByteArray::fromStdString(makeFrd(1, 3, 99)), QStringLiteral("t.frd")).ok());    // unknown node in an element
		CHECK(!readBytes(QByteArray("    1C\n    1UUSER\n 9999\n"), QStringLiteral("t.frd")).ok());      // no node block
		CHECK(!readBytes(QByteArray(""), QStringLiteral("t.frd")).ok());
	}

	// Real CalculiX files shipped in sample-models/Simulation, checked against the values FreeCAD's own test suite
	// expects for box_static (FreeCAD: Mod/Fem/femtest/data/calculix/box_static_expected_values, MPa / mm).
	struct Range { double lo, hi; };

	bool rangeOf(const ResultDataset& ds, const char* field, int component, Range& out)
	{
		for (std::size_t i = 0; i < ds.fields.size(); ++i)
			if (ds.fields[i].name == QLatin1String(field))
			{
				DisplayScalar scalar;
				if (!buildDisplayScalar(ds, static_cast<int>(i), component, scalar))
					return false;
				out = { scalar.minValue, scalar.maxValue };
				return true;
			}
		return false;
	}

	void checkRange(const ResultDataset& ds, const char* field, int component, double lo, double hi)
	{
		Range r{};
		const bool found = rangeOf(ds, field, component, r);
		if (!found)
			std::fprintf(stderr, "  field %s (component %d) not found\n", field, component);
		CHECK(found);
		if (found)
		{
			if (!approx(r.lo, lo, 5e-4, 1e-6) || !approx(r.hi, hi, 5e-4, 1e-6))
				std::fprintf(stderr, "  %s[%d]: got %g..%g, expected %g..%g\n", field, component, r.lo, r.hi, lo, hi);
			CHECK(approx(r.lo, lo, 5e-4, 1e-6));
			CHECK(approx(r.hi, hi, 5e-4, 1e-6));
		}
	}

	void testFrdRealFiles()
	{
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		const QString staticPath = dir + QStringLiteral("/FEM_box_static.frd");
		if (!QFile::exists(staticPath))
		{
			std::printf("  (skipping real .frd tests: %s not found)\n", qPrintable(staticPath));
			return;
		}

		ResultReadOutcome r = readResultFile(staticPath);
		if (!r.ok())
			std::fprintf(stderr, "  FEM_box_static.frd failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (r.ok())
		{
			const ResultDataset& ds = *r.dataset;
			CHECK(ds.nodeCount() == 280 && ds.cellCount() == 129);
			CHECK(ds.cellTypes[0] == ResultCellType::Tetra10);
			CHECK(ds.steps.size() == 1);
			CHECK(ds.findField(QStringLiteral("DISP"), ResultFieldAssociation::Node) != nullptr);
			CHECK(ds.findField(QStringLiteral("STRESS"), ResultFieldAssociation::Node) != nullptr);
			CHECK(ds.findField(QStringLiteral("TOSTRAIN"), ResultFieldAssociation::Node) != nullptr);
			CHECK(extract(ds).triangleCount() == 96);

			// FreeCAD's expected values for this exact file
			checkRange(ds, "DISP", 0, -0.0680669, 0.00296745);   // U1
			checkRange(ds, "DISP", 1, -0.0109484, 0.0110702);    // U2
			checkRange(ds, "DISP", 2, -0.0643181, 0.0);          // U3
			checkRange(ds, "DISP", -1, 0.0, 0.093738346);        // Uabs
			checkRange(ds, "STRESS von Mises", -1, 385.3799018170, 2203.5090958167);
			checkRange(ds, "STRESS max principal", -1, -924.0494419697, 1169.5484598644);
			checkRange(ds, "STRESS mid principal", -1, -1260.1000473504, 346.8740040676);
			checkRange(ds, "STRESS min principal", -1, -3276.2805106799, 3.2401143113);
			checkRange(ds, "STRESS max shear", -1, 218.1005303160, 1176.1155343551);

			DisplayScalar scalar;
			CHECK(chooseDefaultDisplayScalar(ds, scalar) && scalar.label == QStringLiteral("STRESS von Mises"));
		}

		// Modal file: one mode; the "time" is the frequency in Hz and the step is labelled with the mode number.
		ResultReadOutcome m = readResultFile(dir + QStringLiteral("/FEM_box_frequency.frd"));
		CHECK(m.ok());
		if (m.ok())
		{
			CHECK(m.dataset->nodeCount() == 280 && m.dataset->cellCount() == 129);
			CHECK(m.dataset->steps.size() == 1);
			CHECK(approx(m.dataset->steps[0].time, 1.93865e-2));
			CHECK(m.dataset->steps[0].label == QStringLiteral("Mode 1"));
			CHECK(m.dataset->findField(QStringLiteral("DISP"), ResultFieldAssociation::Node) != nullptr);
		}

		// beampl: 20-node hexahedra, and no element/node counts on the block header lines.
		ResultReadOutcome b = readResultFile(dir + QStringLiteral("/beampl.frd"));
		if (!b.ok())
			std::fprintf(stderr, "  beampl.frd failed: %s\n", qPrintable(b.error));
		CHECK(b.ok());
		if (b.ok())
		{
			CHECK(b.dataset->nodeCount() == 261 && b.dataset->cellCount() == 32);
			CHECK(b.dataset->cellTypes[0] == ResultCellType::Hexahedron20);
			CHECK(b.dataset->findField(QStringLiteral("STRESS von Mises"), ResultFieldAssociation::Node) != nullptr);
			CHECK(extract(*b.dataset).triangleCount() > 0);
		}
	}

	// ---- Units -----------------------------------------------------------------------------------------------------

	QString u16(const char16_t* text) { return QString::fromUtf16(text); }

	int fieldIndexOf(const ResultDataset& ds, const QString& name)
	{
		for (std::size_t i = 0; i < ds.fields.size(); ++i)
			if (ds.fields[i].name == name)
				return static_cast<int>(i);
		return -1;
	}

	void testUnitConversions()
	{
		auto conv = [](const char* kind, const QString& from, const QString& to) {
			return unitConversion(QString::fromLatin1(kind), from, to);
		};
		CHECK(approx(conv("length", u16(u"mm"), u16(u"m")).apply(1500.0), 1.5));
		CHECK(approx(conv("length", u16(u"m"), u16(u"mm")).apply(1.5), 1500.0));
		CHECK(approx(conv("length", u16(u"cm"), u16(u"in")).apply(2.54), 1.0));
		CHECK(approx(conv("pressure", u16(u"MPa"), u16(u"Pa")).apply(2.5), 2.5e6));
		CHECK(approx(conv("pressure", u16(u"MPa"), u16(u"psi")).apply(1.0), 145.0377377, 1e-6));
		CHECK(approx(conv("pressure", u16(u"psi"), u16(u"kPa")).apply(1.0), 6.894757293, 1e-6));
		CHECK(approx(conv("density", u16(u"t/mm\u00B3"), u16(u"kg/m\u00B3")).apply(7.85e-9), 7850.0));

		// temperatures are affine, not just scaled
		const QString K = u16(u"K"), C = u16(u"\u00B0C"), F = u16(u"\u00B0F");
		CHECK(approx(conv("temperature", C, K).apply(0.0), 273.15));
		CHECK(approx(conv("temperature", C, F).apply(100.0), 212.0));
		CHECK(approx(conv("temperature", K, C).apply(300.0), 26.85));
		CHECK(approx(conv("temperature", F, C).apply(32.0), 0.0, 1e-4, 1e-9));
		const UnitConversion there = conv("temperature", F, K), back = conv("temperature", K, F);
		CHECK(approx(back.apply(there.apply(98.6)), 98.6));

		CHECK(conv("length", u16(u"mm"), u16(u"mm")).isIdentity());
		CHECK(!conv("nonsense", u16(u"mm"), u16(u"m")).valid);
		CHECK(!conv("length", u16(u"furlong"), u16(u"m")).valid);
		CHECK(!conv("length", QString(), u16(u"m")).valid);
		CHECK(findQuantityKind(QStringLiteral("pressure")) != nullptr && findQuantityKind(QString()) == nullptr);
		CHECK(unitSymbols(QStringLiteral("length")).contains(u16(u"mm")) && unitSymbols(QStringLiteral("nope")).isEmpty());
	}

	void testUnitGuessing()
	{
		CHECK(guessQuantityKind(QStringLiteral("Displacement Magnitude")) == QStringLiteral("length"));
		CHECK(guessQuantityKind(QStringLiteral("DISP")) == QStringLiteral("length"));
		CHECK(guessQuantityKind(QStringLiteral("STRESS von Mises")) == QStringLiteral("pressure"));
		CHECK(guessQuantityKind(QStringLiteral("Major Principal Stress")) == QStringLiteral("pressure"));
		CHECK(guessQuantityKind(QStringLiteral("Pressure")) == QStringLiteral("pressure"));
		CHECK(guessQuantityKind(QStringLiteral("TOSTRAIN")) == QStringLiteral("strain"));
		CHECK(guessQuantityKind(QStringLiteral("Temperature")) == QStringLiteral("temperature"));
		CHECK(guessQuantityKind(QStringLiteral("scalars")).isEmpty());
		CHECK(guessQuantityKind(QStringLiteral("mode1")).isEmpty());

		// CalculiX result: the mm-N-MPa system, labelled as an unconfirmed guess; numbers untouched
		ResultReadOutcome r = readBytes(QByteArray::fromStdString(makeFrd()), QStringLiteral("t.frd"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		ResultDataset& ds = *r.dataset;
		const int disp = fieldIndexOf(ds, QStringLiteral("DISP")), stress = fieldIndexOf(ds, QStringLiteral("STRESS"));
		const int vm = fieldIndexOf(ds, QStringLiteral("STRESS von Mises"));
		CHECK(disp >= 0 && stress >= 0 && vm >= 0);
		const float rawDisp = ds.fields[static_cast<std::size_t>(disp)].stepData[0][3]; // node 20 D1 = -1e-3
		assignGuessedUnits(ds);
		CHECK(ds.fields[static_cast<std::size_t>(disp)].quantityKind == QStringLiteral("length"));
		CHECK(ds.fields[static_cast<std::size_t>(disp)].fileUnit == u16(u"mm"));
		CHECK(ds.fields[static_cast<std::size_t>(stress)].fileUnit == u16(u"MPa"));
		CHECK(ds.fields[static_cast<std::size_t>(vm)].fileUnit == u16(u"MPa"));
		CHECK(!ds.fields[static_cast<std::size_t>(disp)].unitConfirmed);
		CHECK(ds.fields[static_cast<std::size_t>(disp)].displayUnit == ds.fields[static_cast<std::size_t>(disp)].fileUnit);
		CHECK(ds.fields[static_cast<std::size_t>(disp)].stepData[0][3] == rawDisp); // a guess labels, never converts
		DisplayScalar d;
		CHECK(buildDisplayScalar(ds, disp, 0, d) && d.unit == u16(u"mm") && d.unitAssumed);

		// a VTK result is guessed as SI; a temperature is never guessed
		const QByteArray vtk =
			"# vtk DataFile Version 3.0\nt\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 4 float\n0 0 0 1 0 0 0 1 0 0 0 1\n"
			"CELLS 1 5\n4 0 1 2 3\nCELL_TYPES 1\n10\nPOINT_DATA 4\n"
			"SCALARS von%20Mises%20Stress float\nLOOKUP_TABLE default\n1 2 3 4\n"
			"SCALARS Temperature float\nLOOKUP_TABLE default\n20 21 22 23\n";
		ResultReadOutcome rv = readLegacy(vtk);
		CHECK(rv.ok());
		if (rv.ok())
		{
			assignGuessedUnits(*rv.dataset);
			CHECK(rv.dataset->fields[0].fileUnit == u16(u"Pa"));
			CHECK(rv.dataset->fields[1].quantityKind == QStringLiteral("temperature") && rv.dataset->fields[1].fileUnit.isEmpty());
		}
	}

	void testSetFieldUnits()
	{
		ResultReadOutcome r = readBytes(QByteArray::fromStdString(makeFrd()), QStringLiteral("t.frd"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		ResultDataset& ds = *r.dataset;
		assignGuessedUnits(ds);
		const int stress = fieldIndexOf(ds, QStringLiteral("STRESS"));
		const int vm = fieldIndexOf(ds, QStringLiteral("STRESS von Mises"));

		// editing the SOURCE field updates its derived fields; editing a derived field updates the source
		CHECK(setFieldUnits(ds, stress, QStringLiteral("pressure"), u16(u"MPa"), u16(u"Pa")));
		for (const char* name : { "STRESS", "STRESS von Mises", "STRESS max principal", "STRESS max shear" })
		{
			const int i = fieldIndexOf(ds, QString::fromLatin1(name));
			CHECK(i >= 0 && ds.fields[static_cast<std::size_t>(i)].displayUnit == u16(u"Pa") && ds.fields[static_cast<std::size_t>(i)].unitConfirmed);
		}
		DisplayScalar d;
		CHECK(buildDisplayScalar(ds, vm, -1, d));
		CHECK(d.unit == u16(u"Pa") && !d.unitAssumed);
		CHECK(approx(d.nodeValues[0], 100.0e6, 1e-6)); // node 10 uniaxial 100 MPa -> 1e8 Pa
		CHECK(approx(d.maxValue, 100.0e6, 1e-6)); // node 10 (uniaxial 100 MPa) is the largest von Mises

		CHECK(setFieldUnits(ds, vm, QStringLiteral("pressure"), u16(u"MPa"), u16(u"MPa"))); // via the derived field
		CHECK(ds.fields[static_cast<std::size_t>(stress)].displayUnit == u16(u"MPa"));
		CHECK(buildDisplayScalar(ds, vm, -1, d) && approx(d.nodeValues[0], 100.0));

		// invalid requests change nothing
		const QString before = ds.fields[static_cast<std::size_t>(stress)].fileUnit;
		CHECK(!setFieldUnits(ds, stress, QStringLiteral("pressure"), u16(u"furlong"), QString()));
		CHECK(!setFieldUnits(ds, stress, QStringLiteral("nonsense"), u16(u"Pa"), QString()));
		CHECK(!setFieldUnits(ds, 999, QStringLiteral("pressure"), u16(u"Pa"), QString()));
		CHECK(ds.fields[static_cast<std::size_t>(stress)].fileUnit == before);

		// a known quantity with no unit yet: kept, but nothing is converted or labelled
		CHECK(setFieldUnits(ds, stress, QStringLiteral("pressure"), QString(), QString()));
		CHECK(ds.fields[static_cast<std::size_t>(vm)].quantityKind == QStringLiteral("pressure")
		      && ds.fields[static_cast<std::size_t>(vm)].fileUnit.isEmpty());
		CHECK(buildDisplayScalar(ds, vm, -1, d) && d.unit.isEmpty() && approx(d.nodeValues[0], 100.0));

		// "not specified" clears the units (numbers are shown as written)
		CHECK(setFieldUnits(ds, stress, QString(), QString(), QString()));
		CHECK(buildDisplayScalar(ds, vm, -1, d) && d.unit.isEmpty() && approx(d.nodeValues[0], 100.0));
	}

	// The same physical result read from two files in different unit systems must agree once the units are handled:
	// the CalculiX .frd (mm, MPa) and FreeCAD's .vtu export of it (SI: m, Pa).
	void testUnitsAcrossFiles()
	{
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		const QString frdPath = dir + QStringLiteral("/FEM_box_static.frd"), vtuPath = dir + QStringLiteral("/FEM_box_static_stress.vtu");
		if (!QFile::exists(frdPath) || !QFile::exists(vtuPath))
		{
			std::printf("  (skipping cross-file unit test: samples not found)\n");
			return;
		}
		const LoadedSimulationResult frd = loadSimulationResult(frdPath), vtu = loadSimulationResult(vtuPath);
		CHECK(frd.ok() && vtu.ok());
		if (!frd.ok() || !vtu.ok())
			return;

		// loadSimulationResult() assigns the guessed units: mm/MPa for CalculiX, SI for the VTK file
		DisplayScalar frdVm, vtuVm, frdDisp, vtuDisp;
		CHECK(buildDisplayScalar(*frd.dataset, fieldIndexOf(*frd.dataset, QStringLiteral("STRESS von Mises")), -1, frdVm));
		CHECK(buildDisplayScalar(*vtu.dataset, fieldIndexOf(*vtu.dataset, QStringLiteral("von Mises Stress")), -1, vtuVm));
		CHECK(frdVm.unit == u16(u"MPa") && vtuVm.unit == u16(u"Pa") && frdVm.unitAssumed && vtuVm.unitAssumed);

		// show the FRD stress in Pa: it now matches the VTU's
		ResultDataset& frdData = *frd.dataset;
		CHECK(setFieldUnits(frdData, fieldIndexOf(frdData, QStringLiteral("STRESS")), QStringLiteral("pressure"), u16(u"MPa"), u16(u"Pa")));
		CHECK(buildDisplayScalar(frdData, fieldIndexOf(frdData, QStringLiteral("STRESS von Mises")), -1, frdVm));
		CHECK(approx(frdVm.minValue, vtuVm.minValue, 1e-4) && approx(frdVm.maxValue, vtuVm.maxValue, 1e-4));

		// displacement: FRD in mm shown in m == the VTU's Displacement Magnitude (m)
		CHECK(setFieldUnits(frdData, fieldIndexOf(frdData, QStringLiteral("DISP")), QStringLiteral("length"), u16(u"mm"), u16(u"m")));
		CHECK(buildDisplayScalar(frdData, fieldIndexOf(frdData, QStringLiteral("DISP")), -1, frdDisp));
		CHECK(buildDisplayScalar(*vtu.dataset, fieldIndexOf(*vtu.dataset, QStringLiteral("Displacement Magnitude")), -1, vtuDisp));
		CHECK(approx(frdDisp.maxValue, vtuDisp.maxValue, 1e-4));
	}

	void testLoadSimulationResult()
	{
		const QString path = tempDir().filePath(QStringLiteral("load_test.vtk"));
		QFile f(path);
		CHECK(f.open(QIODevice::WriteOnly));
		f.write(kTetAscii);
		f.close();
		const LoadedSimulationResult ok = loadSimulationResult(path);
		CHECK(ok.ok());
		if (ok.ok())
		{
			CHECK(ok.dataset->nodeCount() == 4);
			CHECK(ok.surface.triangleCount() == 4);
			CHECK(ok.error.isEmpty());
		}
		const LoadedSimulationResult missing = loadSimulationResult(tempDir().filePath(QStringLiteral("nope.vtk")));
		CHECK(!missing.ok());
		CHECK(!missing.error.isEmpty());
		std::atomic<bool> cancel(true);
		const LoadedSimulationResult cancelled = loadSimulationResult(path, &cancel);
		CHECK(!cancelled.ok());
		CHECK(cancelled.error == QStringLiteral("cancelled"));
	}

	// Time steps: step-aware scalars, the all-steps range and its cache, step text, and the multi-step samples.
	void testTimeSteps()
	{
		ResultReadOutcome r = readBytes(QByteArray::fromStdString(makeFrd()), QStringLiteral("t.frd"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		ResultDataset& ds = *r.dataset;
		CHECK(ds.stepCount() == 2);
		const int disp = fieldIndexOf(ds, QStringLiteral("DISP")), stress = fieldIndexOf(ds, QStringLiteral("STRESS"));
		CHECK(disp >= 0 && stress >= 0);

		DisplayScalar d0, d1;
		CHECK(buildDisplayScalar(ds, disp, 0, d0, 0) && buildDisplayScalar(ds, disp, 0, d1, 1));
		CHECK(approx(d0.maxValue, 0.0, 1e-4, 1e-12) && approx(d0.minValue, -1.0e-3));
		CHECK(approx(d1.minValue, 1.0) && approx(d1.maxValue, 2.0));
		DisplayScalar none;
		CHECK(!buildDisplayScalar(ds, stress, 0, none, 1)); // STRESS has no data at the second step
		CHECK(!buildDisplayScalar(ds, disp, 0, none, 2));   // out of range
		CHECK(!buildDisplayScalar(ds, disp, 0, none, -1));

		float lo = 0.0f, hi = 0.0f;
		CHECK(computeAllStepsRange(ds, disp, 0, lo, hi));
		CHECK(approx(lo, -1.0e-3) && approx(hi, 2.0));
		CHECK(computeAllStepsRange(ds, stress, 0, lo, hi)); // only one step has stress
		CHECK(approx(lo, -5.0) && approx(hi, 100.0)); // SXX: 100, 0, -5, 10

		// the cached all-steps range is keyed by field, component and units
		SimulationSession session;
		session.dataset = std::move(r.dataset); // ds keeps referring to the same object
		CHECK(cachedAllStepsRange(session, disp, 0, lo, hi) && approx(hi, 2.0));
		CHECK(session.rangeCache.valid);
		ds.fields[static_cast<std::size_t>(disp)].fileUnit = u16(u"mm");
		ds.fields[static_cast<std::size_t>(disp)].displayUnit = u16(u"mm");
		ds.fields[static_cast<std::size_t>(disp)].quantityKind = QStringLiteral("length");
		ds.fields[static_cast<std::size_t>(disp)].displayUnit = u16(u"m");
		CHECK(cachedAllStepsRange(session, disp, 0, lo, hi) && approx(hi, 2.0e-3)); // units changed: recomputed, converted
		CHECK(cachedAllStepsRange(session, disp, 1, lo, hi));                        // another component: recomputed
		CHECK(!cachedAllStepsRange(session, 999, 0, lo, hi));

		// step text
		ResultStep t;
		t.time = 0.5;
		CHECK(stepTimeText(t) == QStringLiteral("0.5"));
		ds.steps[1].time = 2.0;
		CHECK(stepDescription(ds, 1) == QStringLiteral("t = 2"));
		ds.steps[1].label = QStringLiteral("Mode 3");
		ds.steps[1].time = 73971.2;
		ds.steps[1].timeUnit = QStringLiteral("Hz");
		CHECK(stepDescription(ds, 1) == QStringLiteral("Mode 3 - 73971.2 Hz"));
		ds.steps[1].label.clear();
		CHECK(stepDescription(ds, 1) == QStringLiteral("73971.2 Hz"));
		CHECK(stepDescription(ds, 7).isEmpty() && stepDescription(ds, -1).isEmpty());

		// the multi-step samples
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		if (!QFile::exists(dir + QStringLiteral("/FEM_box_modes.frd")) || !QFile::exists(dir + QStringLiteral("/FEM_box_load_steps.frd")))
		{
			std::printf("  (skipping multi-step sample tests: samples not found)\n");
			return;
		}
		ResultReadOutcome one = readResultFile(dir + QStringLiteral("/FEM_box_frequency.frd"));
		CHECK(one.ok() && one.dataset->stepCount() == 1 && one.dataset->steps[0].timeUnit == QStringLiteral("Hz"));

		ResultReadOutcome modes = readResultFile(dir + QStringLiteral("/FEM_box_modes.frd"));
		CHECK(modes.ok());
		if (modes.ok())
		{
			const ResultDataset& m = *modes.dataset;
			CHECK(m.stepCount() == 6);
			const double expected[] = { 54279.6, 54317.5, 73971.2, 128657.9, 143335.9 };
			for (std::size_t i = 0; i < m.stepCount(); ++i)
			{
				CHECK(m.steps[i].label == QStringLiteral("Mode %1").arg(i + 1));
				CHECK(m.steps[i].timeUnit == QStringLiteral("Hz"));
				if (i > 0)
					CHECK(m.steps[i].time >= m.steps[i - 1].time);
				if (i < 5)
					CHECK(approx(m.steps[i].time, expected[i], 1e-3));
			}
			const int md = fieldIndexOf(m, QStringLiteral("DISP"));
			CHECK(md >= 0);
			for (int i = 0; i < 6; ++i)
			{
				DisplayScalar s;
				CHECK(buildDisplayScalar(m, md, -1, s, i) && s.maxValue > 0.0f);
			}
		}

		ResultReadOutcome ramp = readResultFile(dir + QStringLiteral("/FEM_box_load_steps.frd"));
		CHECK(ramp.ok());
		if (ramp.ok())
		{
			const ResultDataset& m = *ramp.dataset;
			CHECK(m.stepCount() == 4);
			const double times[] = { 0.25, 0.5, 0.875, 1.0 };
			for (std::size_t i = 0; i < 4 && i < m.stepCount(); ++i)
				CHECK(approx(m.steps[i].time, times[i], 1e-4));
			const int md = fieldIndexOf(m, QStringLiteral("DISP"));
			CHECK(md >= 0);
			float prev = 0.0f;
			for (int i = 0; i < 4; ++i)
			{
				DisplayScalar s;
				CHECK(buildDisplayScalar(m, md, -1, s, i));
				CHECK(s.maxValue > prev);
				prev = s.maxValue;
			}
			DisplayScalar first, last;
			CHECK(buildDisplayScalar(m, md, -1, first, 0) && buildDisplayScalar(m, md, -1, last, 3));
			const double ratio = first.maxValue / last.maxValue;
			CHECK(ratio > 0.2 && ratio < 0.3);
			float a = 0.0f, b = 0.0f;
			CHECK(computeAllStepsRange(m, md, -1, a, b) && approx(b, last.maxValue));
		}
	}

	// Deformed shape: displacement field detection, deformed positions, the automatic scale, normals.
	void testDeformation()
	{
		ResultReadOutcome r = readBytes(QByteArray::fromStdString(makeFrd()), QStringLiteral("t.frd"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		const int disp = fieldIndexOf(ds, QStringLiteral("DISP"));
		CHECK(disp >= 0 && findDisplacementField(ds) == disp);
		CHECK(fieldIndexOf(ds, QStringLiteral("STRESS")) >= 0); // 6 components: never taken for a displacement

		ResultBoundarySurface surface;
		CHECK(extractBoundarySurface(ds, surface, nullptr, nullptr));
		CHECK(surface.vertexCount() == 4);

		auto vertexOfNode = [&](std::uint32_t node) {
			for (std::size_t v = 0; v < surface.vertexNode.size(); ++v)
				if (surface.vertexNode[v] == node)
					return static_cast<int>(v);
			return -1;
		};
		std::vector<float> pos;
		CHECK(buildDeformedPositions(ds, surface, disp, 0, 100.0, pos) && pos.size() == surface.positions.size());
		const int v20 = vertexOfNode(1), v40 = vertexOfNode(3); // nodes are stored in file order: 10, 20, 30, 40
		CHECK(v20 >= 0 && v40 >= 0);
		if (v20 >= 0 && v40 >= 0)
		{
			CHECK(approx(pos[static_cast<std::size_t>(v20) * 3], 0.9) && approx(pos[static_cast<std::size_t>(v20) * 3 + 1], 0.2)
			      && approx(pos[static_cast<std::size_t>(v20) * 3 + 2], 0.0, 1e-4, 1e-6));
			// node 40 has no displacement value at step 0 (NaN): it stays where it was
			for (int k = 0; k < 3; ++k)
				CHECK(pos[static_cast<std::size_t>(v40) * 3 + static_cast<std::size_t>(k)] == surface.positions[static_cast<std::size_t>(v40) * 3 + static_cast<std::size_t>(k)]);
		}
		std::vector<float> rest;
		CHECK(buildDeformedPositions(ds, surface, disp, 0, 0.0, rest) && rest == surface.positions); // scale 0 = rest shape
		CHECK(!buildDeformedPositions(ds, surface, disp, 5, 1.0, pos));  // no such step
		CHECK(!buildDeformedPositions(ds, surface, 999, 0, 1.0, pos));   // no such field

		CHECK(approx(maxDisplacementMagnitude(ds, disp), std::sqrt(12.0))); // node 20 at the second step: (2, 2, 2)
		CHECK(maxDisplacementMagnitude(ds, 999) == 0.0);
		CHECK(autoDeformScale(ds, surface, disp) == 1.0); // displacement (3.5) is already far larger than a tenth of the model

		// the normals of an explicit position set match the surface form, and follow the shape
		const std::vector<float> n1 = computeSmoothVertexNormals(surface), n2 = computeSmoothVertexNormals(surface.positions, surface.triangles);
		CHECK(n1 == n2);
		CHECK(computeSmoothVertexNormals(rest, surface.triangles) == n1);

		// the sample box: a small displacement of a 10 mm box gets an exaggeration that is a 1/2/5 x 10^n
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		if (!QFile::exists(dir + QStringLiteral("/FEM_box_static.frd")))
		{
			std::printf("  (skipping deformation sample test: sample not found)\n");
			return;
		}
		const LoadedSimulationResult box = loadSimulationResult(dir + QStringLiteral("/FEM_box_static.frd"));
		CHECK(box.ok());
		if (!box.ok())
			return;
		const int bd = findDisplacementField(*box.dataset);
		CHECK(bd >= 0);
		const double maxD = maxDisplacementMagnitude(*box.dataset, bd);
		const double scale = autoDeformScale(*box.dataset, box.surface, bd);
		CHECK(maxD > 0.0 && scale >= 1.0);
		if (scale > 1.0)
		{
			const double mantissa = scale / std::pow(10.0, std::floor(std::log10(scale)));
			CHECK(approx(mantissa, 1.0) || approx(mantissa, 2.0) || approx(mantissa, 5.0));
			CHECK(scale * maxD <= 0.1 * std::sqrt(3.0) * 10.0 * 1.0001); // never more than a tenth of the 10 mm cube's diagonal
		}
		std::vector<float> deformed;
		CHECK(buildDeformedPositions(*box.dataset, box.surface, bd, 0, scale, deformed) && deformed != box.surface.positions);
		CHECK(!isModalResult(*box.dataset));

		// modal results: mode shapes are normalised (arbitrary eigenvector amplitude), one unit = a tenth of the model
		const LoadedSimulationResult modes = loadSimulationResult(dir + QStringLiteral("/FEM_box_modes.frd"));
		CHECK(modes.ok());
		if (modes.ok())
		{
			CHECK(isModalResult(*modes.dataset));
			const int md = findDisplacementField(*modes.dataset);
			CHECK(md >= 0 && autoDeformScale(*modes.dataset, modes.surface, md) == 1.0);
			for (int step = 0; step < static_cast<int>(modes.dataset->stepCount()); ++step)
			{
				// after the factor every mode reaches the same peak displacement: a tenth of the 10 mm cube's diagonal
				const double factor = modalDisplayFactor(*modes.dataset, modes.surface, md, step);
				const ResultField& f = modes.dataset->fields[static_cast<std::size_t>(md)];
				double peak = 0.0;
				const std::vector<float>& data = f.stepData[static_cast<std::size_t>(step)];
				for (std::size_t i = 0; i + 2 < data.size(); i += 3)
					peak = std::max(peak, std::sqrt(double(data[i]) * data[i] + double(data[i + 1]) * data[i + 1] + double(data[i + 2]) * data[i + 2]));
				CHECK(approx(peak * factor, 0.1 * std::sqrt(3.0) * 10.0, 1e-3));
			}
		}
		const LoadedSimulationResult ramp = loadSimulationResult(dir + QStringLiteral("/FEM_box_load_steps.frd"));
		CHECK(ramp.ok() && !isModalResult(*ramp.dataset)); // load increments are physical displacements
	}

	// Hover probe: the shown value under a point of the surface.
	void testProbe()
	{
		ResultReadOutcome r = readBytes(QByteArray::fromStdString(makeFrd()), QStringLiteral("t.frd"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		const int disp = fieldIndexOf(ds, QStringLiteral("DISP"));
		ResultBoundarySurface surface;
		CHECK(extractBoundarySurface(ds, surface, nullptr, nullptr));
		DisplayScalar scalar;
		CHECK(buildDisplayScalar(ds, disp, 0, scalar, 0)); // DISP D1 at the first step: nodes 0..3 = 0, -1e-3, 0, (no value)

		// the first triangle that has none of node index 3 (no value), and the first that has it
		std::size_t plain = surface.triangleCount(), withGap = surface.triangleCount();
		for (std::size_t t = 0; t < surface.triangleCount(); ++t)
		{
			bool hasGap = false, hasNode1 = false;
			for (int k = 0; k < 3; ++k)
			{
				const std::uint32_t node = surface.vertexNode[surface.triangles[t * 3 + static_cast<std::size_t>(k)]];
				hasGap = hasGap || node == 3;
				hasNode1 = hasNode1 || node == 1;
			}
			if (!hasGap && hasNode1 && plain == surface.triangleCount())
				plain = t;
			if (hasGap && hasNode1 && withGap == surface.triangleCount())
				withGap = t;
		}
		CHECK(plain < surface.triangleCount() && withGap < surface.triangleCount());
		if (plain >= surface.triangleCount() || withGap >= surface.triangleCount())
			return;
		auto slotOfNode = [&](std::size_t t, std::uint32_t node) {
			for (int k = 0; k < 3; ++k)
				if (surface.vertexNode[surface.triangles[t * 3 + static_cast<std::size_t>(k)]] == node)
					return k;
			return -1;
		};
		auto sample = [&](std::size_t t, float w0, float w1, float w2, float lo, float hi) {
			return sampleSurfaceScalar(ds, surface, scalar, t, w0, w1, w2, lo, hi);
		};
		auto weightsAt = [&](std::size_t t, std::uint32_t node, float wNode, float wOthers, float w[3]) {
			for (int k = 0; k < 3; ++k)
				w[k] = wOthers;
			w[slotOfNode(t, node)] = wNode;
		};

		float w[3];
		weightsAt(plain, 1, 1.0f, 0.0f, w); // exactly on node index 1 (file id 20)
		ProbeSample a = sample(plain, w[0], w[1], w[2], -1.0e-3f, 1.0e-3f);
		CHECK(a.valid && approx(a.value, -1.0e-3) && a.node == 1 && a.nodeId == 20 && approx(a.normalized, 0.0, 1e-4, 1e-6));

		weightsAt(plain, 1, 0.5f, 0.25f, w); // interpolated: 0.5 * -1e-3 + 0.5 * (a node with 0)
		ProbeSample b = sample(plain, w[0], w[1], w[2], -1.0e-3f, 1.0e-3f);
		CHECK(b.valid && approx(b.value, -5.0e-4) && b.node == 1 && approx(b.normalized, 0.25, 1e-3, 1e-6));

		weightsAt(withGap, 1, 0.8f, 0.1f, w); // one vertex has no value: the nearest vertex's value stands in
		ProbeSample c = sample(withGap, w[0], w[1], w[2], -1.0e-3f, 1.0e-3f);
		CHECK(c.valid && approx(c.value, -1.0e-3) && c.node == 1);

		weightsAt(withGap, 3, 0.8f, 0.1f, w); // nearest vertex has no value at all
		CHECK(!sample(withGap, w[0], w[1], w[2], -1.0e-3f, 1.0e-3f).valid);

		CHECK(!sample(surface.triangleCount() + 3, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f).valid); // no such triangle
		CHECK(sample(plain, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f).normalized == 0.0f);            // degenerate range does not divide by 0
	}

	// Min/max markers: which surface vertices carry the extreme values.
	void testExtrema()
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		std::size_t lo = 99, hi = 99;
		CHECK(findScalarExtrema({ 3.0f, -2.0f, 7.5f, 0.0f }, lo, hi) && lo == 1 && hi == 2);
		CHECK(findScalarExtrema({ nan, 4.0f, nan, -1.0f, 9.0f }, lo, hi) && lo == 3 && hi == 4); // no value: ignored
		CHECK(findScalarExtrema({ 5.0f, 5.0f, 5.0f }, lo, hi) && lo == 0 && hi == 0);             // ties: lowest index
		CHECK(findScalarExtrema({ nan, 2.0f }, lo, hi) && lo == 1 && hi == 1);                    // a single finite value
		lo = hi = 99;
		CHECK(!findScalarExtrema({ nan, nan }, lo, hi) && lo == 99 && hi == 99);                  // nothing finite: untouched
		CHECK(!findScalarExtrema({}, lo, hi));
		CHECK(findScalarExtrema({ std::numeric_limits<float>::infinity(), 1.0f, -std::numeric_limits<float>::infinity() }, lo, hi)
		      && lo == 1 && hi == 1); // infinities are not values

		// on a real result: the extreme vertices carry the surface's own min and max
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		if (!QFile::exists(dir + QStringLiteral("/FEM_box_static.frd")))
		{
			std::printf("  (skipping extrema sample test: sample not found)\n");
			return;
		}
		const LoadedSimulationResult box = loadSimulationResult(dir + QStringLiteral("/FEM_box_static.frd"));
		CHECK(box.ok());
		if (!box.ok())
			return;
		DisplayScalar scalar;
		CHECK(chooseDefaultDisplayScalar(*box.dataset, scalar));
		const std::vector<float> values = boundaryVertexValues(box.surface, scalar.nodeValues);
		CHECK(findScalarExtrema(values, lo, hi));
		CHECK(*std::min_element(values.begin(), values.end()) == values[lo] && *std::max_element(values.begin(), values.end()) == values[hi]);
		CHECK(values[lo] >= scalar.minValue && values[hi] <= scalar.maxValue); // the surface never exceeds the whole-model range
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
		CHECK(supportedResultExtensions().contains(QStringLiteral("vtu")) && supportedResultExtensions().contains(QStringLiteral("vtk")));
		CHECK(isSupportedResultFile(QStringLiteral("model.vtk")) && !isSupportedResultFile(QStringLiteral("model.stl")));

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
	testSimulationDisplay();
	testViewState();
	testDerivedStress();
	testFrdSynthetic();
	testFrdErrors();
	testFrdRealFiles();
	testUnitConversions();
	testUnitGuessing();
	testSetFieldUnits();
	testUnitsAcrossFiles();
	testTimeSteps();
	testDeformation();
	testProbe();
	testExtrema();
	testLoadSimulationResult();
	testShellAndSkippedCells();
	testErrors();
	testCancellation();
	testLargeMeshPartitioning();

	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
