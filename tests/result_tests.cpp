// GUI-free tests for the simulation result reader and boundary extraction (docs/simulation_results_design.md).
// Build with -DMV_BUILD_TESTS=ON; run the `result_tests` executable. Exit code = number of failed checks.
//
// Every fixture is generated in code (no binary files in the repo) in each VTK XML data encoding, so a
// bug in header/offset/compression handling shows up as a mismatch against the same mesh written as
// plain ASCII. NOTE: the writers here are written from the VTK format description, like the reader, so
// they cannot catch a shared misreading of the spec - real files (FreeCAD/CalculiX/ParaView output) are
// the final check and are listed in docs/simulation_results_test_data.md.

#include "ComparePaneLayout.h"
#include "CgnsReader.h"
#include "ExodusReader.h"
#include "ResultBoundary.h"
#include "ResultDerivedFields.h"
#include "ResultReader.h"
#include "ResultSnapshot.h"
#include "ResultUnits.h"
#include "SimulationGlyphs.h"
#include "SimulationResultDisplay.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <atomic>
#if MV_HAVE_NETCDF
#include <netcdf.h>
#endif
#if MV_HAVE_CGNS
#include <cgnslib.h>
#endif
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
			// only a CELL vector field exists: it is the default, shown as its magnitude
			DisplayScalar d;
			CHECK(chooseDefaultDisplayScalar(*rb.dataset, d) && d.cellData && d.component == -1);
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
			CHECK(defaultViewState(*rb.dataset, &scalar).fieldIndex >= 0); // the cell vector field
			CHECK(scalar.valid() && scalar.cellData);
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

	// A transient thermal result: a scalar temperature field over many time steps.
	void testThermalTransient()
	{
		CHECK(guessQuantityKind(QStringLiteral("NDTEMP")) == QStringLiteral("temperature"));
		const QString path = QStringLiteral(MV_SIMULATION_SAMPLES_DIR) + QStringLiteral("/FEM_box_thermal_transient.frd");
		if (!QFile::exists(path))
		{
			std::printf("  (skipping thermal sample test: sample not found)\n");
			return;
		}
		const LoadedSimulationResult r = loadSimulationResult(path);
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.stepCount() == 20);
		CHECK(approx(ds.steps[0].time, 0.5) && approx(ds.steps[19].time, 10.0));
		CHECK(!isModalResult(ds));                 // steps are times, not frequencies
		CHECK(stepDescription(ds, 0) == QStringLiteral("t = 0.5"));

		const int t = fieldIndexOf(ds, QStringLiteral("NDTEMP"));
		CHECK(t >= 0);
		if (t < 0)
			return;
		const ResultField& field = ds.fields[static_cast<std::size_t>(t)];
		CHECK(field.components == 1 && field.quantityKind == QStringLiteral("temperature"));
		CHECK(field.fileUnit.isEmpty()); // a temperature unit is never guessed
		CHECK(findDisplacementField(ds) < 0); // nothing to deform

		// heat flows in: the hot face stays at 100, the mean rises step by step towards it, the coldest node warms
		double previousMean = 0.0;
		float previousMin = 0.0f;
		for (int step = 0; step < 20; ++step)
		{
			DisplayScalar s;
			CHECK(buildDisplayScalar(ds, t, -1, s, step));
			CHECK(approx(s.maxValue, 100.0, 1e-4));
			double sum = 0.0;
			for (float v : s.nodeValues)
				sum += v;
			const double mean = sum / static_cast<double>(s.nodeValues.size());
			if (step > 0)
			{
				CHECK(mean > previousMean);
				CHECK(s.minValue >= previousMin);
			}
			previousMean = mean;
			previousMin = s.minValue;
		}
		DisplayScalar first, last;
		CHECK(buildDisplayScalar(ds, t, -1, first, 0) && buildDisplayScalar(ds, t, -1, last, 19));
		CHECK(first.minValue > 20.0f && first.minValue < 30.0f); // CalculiX: 23.39 after the first 0.5 s
		CHECK(last.minValue > 95.0f);

		// an automatic range over all steps is a fixed 23.39 .. 100 scale, so the frames are comparable
		float lo = 0.0f, hi = 0.0f;
		CHECK(computeAllStepsRange(ds, t, -1, lo, hi) && approx(lo, first.minValue) && approx(hi, 100.0, 1e-4));
	}

	// ---- MVF snapshot codec ----------------------------------------------------------------------------------

	bool nameIn(const ResultDataset& ds, const char* name) { return fieldIndexOf(ds, QString::fromLatin1(name)) >= 0; }

	// Encode -> decode with the surface as the "mesh" the reader would have.
	bool roundTrip(const LoadedSimulationResult& r, const SimulationViewState& state, const SnapshotOptions& options,
	               ResultSnapshot& snap, DecodedSnapshot& decoded, QString* error = nullptr)
	{
		if (!encodeResultSnapshot(*r.dataset, r.surface, state, options, snap, error))
			return false;
		return decodeResultSnapshot(snap.json, snap.blobs, r.surface.vertexCount(), r.surface.triangles, decoded, error);
	}

	void testSnapshotCodec()
	{
		// shuffle is its own inverse, for every element size and odd lengths
		QByteArray bytes;
		for (int i = 0; i < 4 * 37; ++i)
			bytes.append(static_cast<char>((i * 31 + 7) & 0xFF));
		CHECK(unshuffleBytes(shuffleBytes(bytes, 4), 4) == bytes);
		QByteArray eight = bytes.left(8 * 17);
		CHECK(unshuffleBytes(shuffleBytes(eight, 8), 8) == eight);
		CHECK(shuffleBytes(bytes, 1) == bytes);

		// step subsampling: everything when it fits, otherwise evenly spaced with the first and last kept
		CHECK(snapshotStepIndices(0, 100).empty());
		CHECK(snapshotStepIndices(5, 100) == std::vector<int>({ 0, 1, 2, 3, 4 }));
		const std::vector<int> some = snapshotStepIndices(101, 5);
		CHECK(some == std::vector<int>({ 0, 25, 50, 75, 100 }));
		const std::vector<int> two = snapshotStepIndices(300, 2);
		CHECK(two == std::vector<int>({ 0, 299 }));
	}

	void testSnapshotRoundTrip()
	{
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		if (!QFile::exists(dir + QStringLiteral("/FEM_box_static.frd")))
		{
			std::printf("  (skipping snapshot sample tests: samples not found)\n");
			return;
		}
		const LoadedSimulationResult box = loadSimulationResult(dir + QStringLiteral("/FEM_box_static.frd"));
		CHECK(box.ok());
		if (!box.ok())
			return;
		const ResultDataset& src = *box.dataset;
		SimulationViewState state = defaultViewState(src);
		state.colormap = 1;
		state.bands = 12;
		state.deform = true;
		state.deformScale = 250.0;
		state.markExtrema = true;

		// ---- everything stored
		SnapshotOptions all;
		all.content = SnapshotOptions::Content::AllFields;
		ResultSnapshot snap;
		DecodedSnapshot dec;
		QString err;
		CHECK(roundTrip(box, state, all, snap, dec, &err));
		if (!dec.dataset)
		{
			std::printf("  snapshot round trip failed: %s\n", qPrintable(err));
			return;
		}
		const ResultDataset& out = *dec.dataset;
		CHECK(out.nodeCount() == box.surface.vertexCount() && out.cellCount() == box.surface.triangleCount());
		CHECK(out.nodePositions == box.surface.positions && dec.restPositions == box.surface.positions);
		CHECK(out.stepCount() == src.stepCount());
		for (std::size_t v = 0; v < out.nodeCount(); ++v)
			if (out.nodeId(v) != src.nodeId(box.surface.vertexNode[v]))
			{
				CHECK(false);
				break;
			}

		// source fields carry exactly the boundary vertex values; units travel with them
		for (const char* name : { "DISP", "STRESS", "TOSTRAIN" })
		{
			const int a = fieldIndexOf(src, QString::fromLatin1(name)), b = fieldIndexOf(out, QString::fromLatin1(name));
			CHECK(a >= 0 && b >= 0);
			if (a < 0 || b < 0)
				continue;
			const ResultField& fa = src.fields[static_cast<std::size_t>(a)];
			const ResultField& fb = out.fields[static_cast<std::size_t>(b)];
			CHECK(fa.components == fb.components && fa.componentNames == fb.componentNames);
			CHECK(fa.quantityKind == fb.quantityKind && fa.fileUnit == fb.fileUnit && fa.displayUnit == fb.displayUnit
			      && fa.unitConfirmed == fb.unitConfirmed);
			bool same = fb.stepData.size() == fa.stepData.size();
			for (std::size_t s = 0; same && s < fa.stepData.size(); ++s)
			{
				const std::size_t comps = static_cast<std::size_t>(fa.components);
				for (std::size_t v = 0; same && v < box.surface.vertexCount(); ++v)
					for (std::size_t c = 0; same && c < comps; ++c)
						same = fb.stepData[s][v * comps + c] == fa.stepData[s][box.surface.vertexNode[v] * comps + c];
			}
			CHECK(same);
		}

		// derived fields are rebuilt (never stored) and agree with the originals; the legend range is the FULL model's
		const char* derived[] = { "STRESS von Mises", "STRESS max principal", "STRESS min principal", "STRESS max shear" };
		for (const char* name : derived)
		{
			const int a = fieldIndexOf(src, QString::fromLatin1(name)), b = fieldIndexOf(out, QString::fromLatin1(name));
			CHECK(a >= 0 && b >= 0);
			if (a < 0 || b < 0)
				continue;
			CHECK(out.fields[static_cast<std::size_t>(b)].fileUnit == src.fields[static_cast<std::size_t>(a)].fileUnit);
			DisplayScalar full, snapshot;
			CHECK(buildDisplayScalar(src, a, -1, full) && buildDisplayScalar(out, b, -1, snapshot));
			CHECK(snapshot.minValue == full.minValue && snapshot.maxValue == full.maxValue); // whole-model range kept
			CHECK(snapshot.unit == full.unit && snapshot.unitAssumed == full.unitAssumed);
			bool close = true;
			for (std::size_t v = 0; close && v < box.surface.vertexCount(); ++v)
				close = approx(snapshot.nodeValues[v], full.nodeValues[box.surface.vertexNode[v]], 1e-4, 1e-3);
			CHECK(close);
		}
		// a component of a stored tensor and the magnitude of the displacement widen the same way
		{
			const int a = fieldIndexOf(src, QStringLiteral("STRESS")), b = fieldIndexOf(out, QStringLiteral("STRESS"));
			const int da = fieldIndexOf(src, QStringLiteral("DISP")), db = fieldIndexOf(out, QStringLiteral("DISP"));
			DisplayScalar fullC, snapC, fullM, snapM;
			CHECK(buildDisplayScalar(src, a, 2, fullC) && buildDisplayScalar(out, b, 2, snapC));
			CHECK(snapC.minValue == fullC.minValue && snapC.maxValue == fullC.maxValue);
			CHECK(buildDisplayScalar(src, da, -1, fullM) && buildDisplayScalar(out, db, -1, snapM));
			CHECK(snapM.minValue == fullM.minValue && snapM.maxValue == fullM.maxValue);
		}

		// the view comes back, its field found again by name
		CHECK(dec.state.fieldIndex >= 0 && out.fields[static_cast<std::size_t>(dec.state.fieldIndex)].name
		      == src.fields[static_cast<std::size_t>(state.fieldIndex)].name);
		CHECK(dec.state.colormap == 1 && dec.state.bands == 12 && dec.state.deform && dec.state.deformScale == 250.0 && dec.state.markExtrema);
		CHECK(out.validate().isEmpty());
		CHECK(snap.size.rawBytes > 0 && snap.size.storedBytes <= snap.size.rawBytes && !snap.size.estimated);
		CHECK(snap.notes.isEmpty()); // nothing was dropped

		// ---- only the shown field (STRESS von Mises -> its tensor) and the displacement
		SnapshotOptions shown;
		shown.shownField = state.fieldIndex;
		ResultSnapshot snap2;
		DecodedSnapshot dec2;
		CHECK(roundTrip(box, state, shown, snap2, dec2));
		if (dec2.dataset)
		{
			CHECK(nameIn(*dec2.dataset, "DISP") && nameIn(*dec2.dataset, "STRESS") && nameIn(*dec2.dataset, "STRESS von Mises"));
			CHECK(!nameIn(*dec2.dataset, "TOSTRAIN"));
			CHECK(snap2.size.rawBytes < snap.size.rawBytes);
			CHECK(dec2.state.fieldIndex >= 0);
		}

		// ---- compression is lossless and never larger than the raw data
		SnapshotOptions raw = all;
		raw.compress = false;
		ResultSnapshot snapRaw;
		DecodedSnapshot decRaw;
		CHECK(roundTrip(box, state, raw, snapRaw, decRaw));
		CHECK(snapRaw.size.storedBytes == snapRaw.size.rawBytes && snapRaw.size.rawBytes == snap.size.rawBytes);
		CHECK(snap.size.storedBytes <= snapRaw.size.storedBytes); // the box surface is tiny: blobs under 512 bytes stay raw
		if (decRaw.dataset)
			CHECK(decRaw.dataset->fields.size() == out.fields.size());
		CHECK(snap.size.storedBytes == [&] { std::uint64_t n = 0; for (const QByteArray& b : snap.blobs) n += static_cast<std::uint64_t>(b.size()); return n; }());

		// ---- a wrong mesh, or damaged data, is refused instead of showing misaligned values
		std::vector<std::uint32_t> fewer(box.surface.triangles.begin(), box.surface.triangles.end() - 3);
		CHECK(!decodeResultSnapshot(snap.json, snap.blobs, box.surface.vertexCount(), fewer, dec));
		CHECK(!decodeResultSnapshot(snap.json, snap.blobs, box.surface.vertexCount() + 1, box.surface.triangles, dec));
		std::vector<QByteArray> damaged = snap.blobs;
		damaged[3] = damaged[3].left(damaged[3].size() / 2);
		QString damagedError;
		CHECK(!decodeResultSnapshot(snap.json, damaged, box.surface.vertexCount(), box.surface.triangles, dec, &damagedError) && !damagedError.isEmpty());
		std::vector<QByteArray> missing = snap.blobs;
		missing.pop_back();
		CHECK(!decodeResultSnapshot(snap.json, missing, box.surface.vertexCount(), box.surface.triangles, dec));
		QJsonObject newer = snap.json;
		newer.insert(QStringLiteral("version"), 99);
		CHECK(!decodeResultSnapshot(newer, snap.blobs, box.surface.vertexCount(), box.surface.triangles, dec));

		// ---- the size estimate: exact raw size, plausible stored size
		const SnapshotSize estimate = estimateSnapshotSize(src, box.surface, all);
		CHECK(estimate.rawBytes == snap.size.rawBytes && estimate.estimated);
		CHECK(estimate.storedBytes > 0 && estimate.storedBytes <= estimate.rawBytes);
	}

	// Compression needs blobs big enough to be worth deflating: hexa.vtk has thousands of surface vertices and a
	// smooth scalar. The snapshot must be lossless and clearly smaller than the raw floats.
	void testSnapshotCompression()
	{
		const QString path = QStringLiteral(MV_SIMULATION_SAMPLES_DIR) + QStringLiteral("/hexa.vtk");
		if (!QFile::exists(path))
		{
			std::printf("  (skipping snapshot compression test: hexa.vtk not found)\n");
			return;
		}
		const LoadedSimulationResult r = loadSimulationResult(path);
		CHECK(r.ok());
		if (!r.ok())
			return;
		const SimulationViewState state = defaultViewState(*r.dataset);
		SnapshotOptions options;
		options.content = SnapshotOptions::Content::AllFields;
		ResultSnapshot packed, plain;
		DecodedSnapshot decPacked, decPlain;
		CHECK(roundTrip(r, state, options, packed, decPacked));
		options.compress = false;
		CHECK(roundTrip(r, state, options, plain, decPlain));
		CHECK(packed.size.rawBytes == plain.size.rawBytes && plain.size.storedBytes == plain.size.rawBytes);
		CHECK(packed.size.storedBytes < packed.size.rawBytes * 9 / 10); // at least 10% smaller
		if (decPacked.dataset && decPlain.dataset)
		{
			CHECK(decPacked.dataset->nodePositions == decPlain.dataset->nodePositions);
			bool identical = decPacked.dataset->fields.size() == decPlain.dataset->fields.size();
			for (std::size_t f = 0; identical && f < decPacked.dataset->fields.size(); ++f)
				identical = decPacked.dataset->fields[f].stepData == decPlain.dataset->fields[f].stepData;
			CHECK(identical); // lossless: compressed and uncompressed decode to the same numbers
		}
		const SnapshotSize estimate = estimateSnapshotSize(*r.dataset, r.surface, SnapshotOptions{ SnapshotOptions::Content::AllFields, -1, 100, true });
		CHECK(estimate.storedBytes < estimate.rawBytes);
		const double actual = static_cast<double>(packed.size.storedBytes), guessed = static_cast<double>(estimate.storedBytes);
		CHECK(guessed > 0.5 * actual && guessed < 2.0 * actual); // the sampled estimate is in the right range
		std::printf("  hexa.vtk snapshot: %llu raw -> %llu stored bytes (estimate %llu)\n",
		            static_cast<unsigned long long>(packed.size.rawBytes), static_cast<unsigned long long>(packed.size.storedBytes),
		            static_cast<unsigned long long>(estimate.storedBytes));
	}

	void testSnapshotSteps()
	{
		const QString dir = QStringLiteral(MV_SIMULATION_SAMPLES_DIR);
		if (!QFile::exists(dir + QStringLiteral("/FEM_box_thermal_transient.frd")) || !QFile::exists(dir + QStringLiteral("/FEM_box_modes.frd")))
		{
			std::printf("  (skipping snapshot step tests: samples not found)\n");
			return;
		}
		const LoadedSimulationResult heat = loadSimulationResult(dir + QStringLiteral("/FEM_box_thermal_transient.frd"));
		CHECK(heat.ok());
		if (!heat.ok())
			return;
		SimulationViewState state = defaultViewState(*heat.dataset);
		state.step = 17;
		state.allStepsRange = true;

		SnapshotOptions options;
		options.content = SnapshotOptions::Content::AllFields;
		ResultSnapshot snap;
		DecodedSnapshot dec;
		CHECK(roundTrip(heat, state, options, snap, dec));
		if (!dec.dataset)
			return;
		CHECK(dec.dataset->stepCount() == 20 && dec.state.step == 17);
		CHECK(approx(dec.dataset->steps[19].time, 10.0));
		// the all-steps range is the full model's, from the stored ranges
		float loA = 0, hiA = 0, loB = 0, hiB = 0;
		const int ta = fieldIndexOf(*heat.dataset, QStringLiteral("NDTEMP")), tb = fieldIndexOf(*dec.dataset, QStringLiteral("NDTEMP"));
		CHECK(computeAllStepsRange(*heat.dataset, ta, -1, loA, hiA) && computeAllStepsRange(*dec.dataset, tb, -1, loB, hiB));
		CHECK(loA == loB && hiA == hiB);

		// a step limit keeps the first and last steps, remaps the saved step and says what was dropped
		options.maxSteps = 4;
		ResultSnapshot few;
		DecodedSnapshot decFew;
		CHECK(roundTrip(heat, state, options, few, decFew));
		if (decFew.dataset)
		{
			CHECK(decFew.dataset->stepCount() == 4);
			CHECK(approx(decFew.dataset->steps[0].time, 0.5) && approx(decFew.dataset->steps[3].time, 10.0));
			CHECK(decFew.state.step == 3); // step 17 of 20 -> the kept step nearest to it is the last one
			CHECK(few.notes.size() == 1 && few.size.rawBytes < snap.size.rawBytes);
		}

		// modal results keep their frequencies (time units) and labels
		const LoadedSimulationResult modes = loadSimulationResult(dir + QStringLiteral("/FEM_box_modes.frd"));
		CHECK(modes.ok());
		if (modes.ok())
		{
			SnapshotOptions shown;
			shown.shownField = defaultViewState(*modes.dataset).fieldIndex;
			ResultSnapshot msnap;
			DecodedSnapshot mdec;
			CHECK(roundTrip(modes, defaultViewState(*modes.dataset), shown, msnap, mdec));
			if (mdec.dataset)
			{
				CHECK(mdec.dataset->stepCount() == 6 && isModalResult(*mdec.dataset));
				CHECK(mdec.dataset->steps[2].label == QStringLiteral("Mode 3") && mdec.dataset->steps[2].timeUnit == QStringLiteral("Hz"));
				CHECK(findDisplacementField(*mdec.dataset) >= 0);
			}
		}
	}

	// Cell (element-wise) fields: constant over each cell, one value per boundary triangle, stored per triangle.
	void testCellData()
	{
		const QString path = QStringLiteral(MV_SIMULATION_SAMPLES_DIR) + QStringLiteral("/cell_data_cube.vtk");
		if (!QFile::exists(path))
		{
			std::printf("  (skipping cell data tests: cell_data_cube.vtk not found)\n");
			return;
		}
		const LoadedSimulationResult r = loadSimulationResult(path);
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.cellCount() == 512 && r.surface.triangleCount() == 768); // 6 faces x 64 quads x 2 triangles
		const ResultField* stress = ds.findField(QStringLiteral("Element_Stress"), ResultFieldAssociation::Cell);
		const ResultField* group = ds.findField(QStringLiteral("Element_Group"), ResultFieldAssociation::Cell);
		CHECK(stress != nullptr && group != nullptr);
		if (!stress || !group)
			return;
		const int stressIndex = static_cast<int>(stress - ds.fields.data());

		// a scalar over the cells, shown as such
		DisplayScalar scalar;
		CHECK(buildDisplayScalar(ds, stressIndex, -1, scalar) && scalar.cellData && scalar.nodeValues.size() == 512);
		CHECK(scalar.minValue > 15.0f && scalar.minValue < 15.1f && scalar.maxValue > 115.7f && scalar.maxValue < 115.8f);
		// with no node field in the file, the default is the first cell scalar
		DisplayScalar chosen;
		CHECK(chooseDefaultDisplayScalar(ds, chosen) && chosen.cellData && chosen.fieldIndex == stressIndex);
		CHECK(defaultViewState(ds).fieldIndex == stressIndex);

		// every boundary triangle takes the value of its cell (two triangles per quad face share it)
		const std::vector<float> faces = boundaryFaceValues(r.surface, scalar.nodeValues);
		CHECK(faces.size() == r.surface.triangleCount());
		bool faceOk = true;
		for (std::size_t t = 0; t < faces.size(); ++t)
			faceOk = faceOk && faces[t] == scalar.nodeValues[r.surface.triangleCell[t]];
		CHECK(faceOk);
		// the per-vertex form (baked colours) averages the triangles at a vertex, staying inside the data range
		const std::vector<float> perVertex = surfaceVertexValues(r.surface, scalar);
		bool vertexOk = perVertex.size() == r.surface.vertexCount();
		for (float v : perVertex)
			vertexOk = vertexOk && std::isfinite(v) && v >= scalar.minValue && v <= scalar.maxValue;
		CHECK(vertexOk);
		CHECK(boundaryFaceValues(r.surface, {}).size() == r.surface.triangleCount()); // no values: all NaN, never out of range

		// the probe reports the cell's value and id, without interpolating
		const std::size_t triangle = 100;
		const ProbeSample sample = sampleSurfaceScalar(ds, r.surface, scalar, triangle, 0.2f, 0.3f, 0.5f, scalar.minValue, scalar.maxValue);
		CHECK(sample.valid && sample.cell && sample.value == faces[triangle]);
		CHECK(sample.node == r.surface.triangleCell[triangle] && sample.nodeId == ds.cellId(r.surface.triangleCell[triangle]));
		CHECK(!sampleSurfaceScalar(ds, r.surface, scalar, r.surface.triangleCount(), 1.0f, 0.0f, 0.0f, 0.0f, 1.0f).valid);

		// a saved snapshot keeps cell fields (per triangle), the full-model range, the cell ids and the view
		SimulationViewState state = defaultViewState(ds);
		SnapshotOptions options;
		options.content = SnapshotOptions::Content::AllFields;
		ResultSnapshot snap;
		DecodedSnapshot dec;
		QString err;
		CHECK(encodeResultSnapshot(ds, r.surface, state, options, snap, &err));
		CHECK(snap.json.value(QStringLiteral("version")).toInt() == 2); // cell fields need the newer layout
		CHECK(decodeResultSnapshot(snap.json, snap.blobs, r.surface.vertexCount(), r.surface.triangles, dec, &err));
		if (!dec.dataset)
		{
			std::printf("  cell snapshot failed: %s\n", qPrintable(err));
			return;
		}
		const ResultDataset& out = *dec.dataset;
		const ResultField* decoded = out.findField(QStringLiteral("Element_Stress"), ResultFieldAssociation::Cell);
		CHECK(decoded != nullptr && out.cellCount() == r.surface.triangleCount());
		if (!decoded)
			return;
		CHECK(decoded->tupleCount() == r.surface.triangleCount() && decoded->stepData[0] == faces);
		CHECK(dec.state.fieldIndex >= 0 && out.fields[static_cast<std::size_t>(dec.state.fieldIndex)].association == ResultFieldAssociation::Cell);
		bool idsOk = true;
		for (std::size_t t = 0; t < r.surface.triangleCount(); ++t)
			idsOk = idsOk && out.cellId(t) == ds.cellId(r.surface.triangleCell[t]);
		CHECK(idsOk);
		DisplayScalar back;
		CHECK(buildDisplayScalar(out, static_cast<int>(decoded - out.fields.data()), -1, back) && back.cellData);
		CHECK(back.minValue == scalar.minValue && back.maxValue == scalar.maxValue); // range of ALL 512 cells, interior included
		// the probe on the decoded result agrees with the live one
		const ProbeSample again = sampleSurfaceScalar(out, ResultBoundarySurface{ out.nodePositions, {}, r.surface.triangles,
			[&] { std::vector<std::uint32_t> id(r.surface.triangleCount()); for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<std::uint32_t>(i); return id; }(),
			{}, 0 }, back, triangle, 0.2f, 0.3f, 0.5f, back.minValue, back.maxValue);
		CHECK(again.valid && again.cell && again.value == sample.value && again.nodeId == sample.nodeId);

		// the size estimate counts per-triangle arrays, not per-vertex ones
		const SnapshotSize estimate = estimateSnapshotSize(ds, r.surface, options);
		CHECK(estimate.rawBytes == snap.size.rawBytes);

		// a result with node fields only is still written in the original layout
		const QString boxPath = QStringLiteral(MV_SIMULATION_SAMPLES_DIR) + QStringLiteral("/FEM_box_static.frd");
		if (QFile::exists(boxPath))
		{
			const LoadedSimulationResult box = loadSimulationResult(boxPath);
			ResultSnapshot nodeSnap;
			CHECK(box.ok() && encodeResultSnapshot(*box.dataset, box.surface, defaultViewState(*box.dataset), options, nodeSnap));
			CHECK(nodeSnap.json.value(QStringLiteral("version")).toInt() == 1 && !nodeSnap.json.contains(QStringLiteral("cellIds")));
		}
	}

	// ---- OpenFOAM case reader --------------------------------------------------------------------------------------

	void writeText(const QString& path, const QByteArray& text)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);
		if (file.open(QIODevice::WriteOnly))
			file.write(text);
	}

	QByteArray foamHeader(const char* className, const char* object, const char* format = "ascii")
	{
		return QByteArray("/*--------------------------------*- C++ -*----------------------------------*\\\n"
		                  "  =========                 |\n\\*---------------------------------------------------------------------------*/\n"
		                  "FoamFile\n{\n    version     2.0;\n    format      ")
			+ format + QByteArray(";\n    arch        \"LSB;label=32;scalar=64\";\n    class       ") + className
			+ QByteArray(";\n    location    \"x\";\n    object      ") + object + QByteArray(";\n}\n// * * * //\n\n");
	}

	// One cell with 7 faces: a pentagonal prism (two pentagons and five quads), all boundary faces. Nodes 0-4 are the
	// counter-clockwise pentagon at z = 0, 5-9 the same at z = 1; the face lists are outward-facing as OpenFOAM writes them.
	void writePrismCase(const QString& dir)
	{
		const QByteArray points = foamHeader("vectorField", "points")
			+ "10\n(\n(0 0 0)\n(2 0 0)\n(3 1 0)\n(1 2 0)\n(-1 1 0)\n(0 0 1)\n(2 0 1)\n(3 1 1)\n(1 2 1)\n(-1 1 1)\n)\n";
		const QByteArray faces = foamHeader("faceList", "faces")
			+ "7\n(\n5(0 4 3 2 1)\n5(5 6 7 8 9)\n4(0 1 6 5)\n4(1 2 7 6)\n4(2 3 8 7)\n4(3 4 9 8)\n4(4 0 5 9)\n)\n";
		const QByteArray owner = foamHeader("labelList", "owner") + "7\n(\n0\n0\n0\n0\n0\n0\n0\n)\n";
		const QByteArray neighbour = foamHeader("labelList", "neighbour") + "0()\n";
		writeText(dir + QStringLiteral("/constant/polyMesh/points"), points);
		writeText(dir + QStringLiteral("/constant/polyMesh/faces"), faces);
		writeText(dir + QStringLiteral("/constant/polyMesh/owner"), owner);
		writeText(dir + QStringLiteral("/constant/polyMesh/neighbour"), neighbour);
		writeText(dir + QStringLiteral("/case.foam"), QByteArray());
		// two time directories: a uniform value, then a one-entry nonuniform list
		writeText(dir + QStringLiteral("/0/p"), foamHeader("volScalarField", "p") + "dimensions [1 -1 -2 0 0 0 0];\ninternalField uniform 5;\nboundaryField\n{\n}\n");
		writeText(dir + QStringLiteral("/1/p"), foamHeader("volScalarField", "p") + "dimensions [1 -1 -2 0 0 0 0];\ninternalField nonuniform List<scalar> 1(7);\nboundaryField\n{\n}\n");
		writeText(dir + QStringLiteral("/0/notAField"), foamHeader("dictionary", "notAField") + "x 1;\n");
		writeText(dir + QStringLiteral("/1/phi"), foamHeader("surfaceScalarField", "phi") + "dimensions [0 3 -1 0 0 0 0];\ninternalField uniform 0;\n");
	}

	void testOpenFoamPolyhedral()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		const QString dir = tmp.path();
		writePrismCase(dir);
		const ResultReadOutcome r = readResultFile(dir + QStringLiteral("/case.foam"));
		if (!r.ok())
			std::printf("  OpenFOAM prism case failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.solverName == QStringLiteral("OpenFOAM") && ds.lengthUnit == QStringLiteral("m"));
		CHECK(ds.nodeCount() == 10 && ds.cellCount() == 1 && ds.cellTypes[0] == ResultCellType::Polyhedron);
		CHECK(ds.validate().isEmpty());
		CHECK(ds.stepCount() == 2 && approx(ds.steps[0].time, 0.0) && approx(ds.steps[1].time, 1.0));
		// only the volScalarField is a field: the dictionary and the surface field are ignored
		CHECK(ds.fields.size() == 1 && ds.fields[0].name == QStringLiteral("p") && ds.fields[0].association == ResultFieldAssociation::Cell);
		CHECK(ds.fields[0].stepData[0] == std::vector<float>({ 5.0f }) && ds.fields[0].stepData[1] == std::vector<float>({ 7.0f }));
		// dimensions [1 -1 -2] state a pressure in Pa
		CHECK(ds.fields[0].quantityKind == QStringLiteral("pressure") && ds.fields[0].fileUnit == QStringLiteral("Pa") && ds.fields[0].unitConfirmed);

		// the boundary: 2 pentagons (3 triangles each) + 5 quads (2 each), all outward-facing, all of cell 0
		CHECK(ds.boundaryTriangles.size() == 16 * 3 && ds.boundaryTriangleCells.size() == 16);
		ResultBoundarySurface surface;
		QString error;
		CHECK(extractBoundarySurface(ds, surface, nullptr, &error));
		CHECK(surface.triangleCount() == 16 && surface.vertexCount() == 10);
		double cx = 0, cy = 0, cz = 0;
		for (std::size_t v = 0; v < 10; ++v)
		{
			cx += surface.positions[v * 3];
			cy += surface.positions[v * 3 + 1];
			cz += surface.positions[v * 3 + 2];
		}
		cx /= 10;
		cy /= 10;
		cz /= 10;
		bool outward = true, allCellZero = true;
		for (std::size_t t = 0; t < surface.triangleCount(); ++t)
		{
			const float* a = &surface.positions[surface.triangles[t * 3] * 3];
			const float* b = &surface.positions[surface.triangles[t * 3 + 1] * 3];
			const float* c = &surface.positions[surface.triangles[t * 3 + 2] * 3];
			const double nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
			const double ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
			const double nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
			const double mx = (a[0] + b[0] + c[0]) / 3 - cx, my = (a[1] + b[1] + c[1]) / 3 - cy, mz = (a[2] + b[2] + c[2]) / 3 - cz;
			outward = outward && nx * mx + ny * my + nz * mz > 0.0;
			allCellZero = allCellZero && surface.triangleCell[t] == 0;
		}
		CHECK(outward && allCellZero);

		// through the application's loader, cell data is what is shown by default
		const LoadedSimulationResult loaded = loadSimulationResult(dir + QStringLiteral("/case.foam"));
		CHECK(loaded.ok() && loaded.surface.triangleCount() == 16);
		if (loaded.ok())
		{
			DisplayScalar d;
			CHECK(chooseDefaultDisplayScalar(*loaded.dataset, d) && d.cellData && d.unit == QStringLiteral("Pa") && !d.unitAssumed);
		}
	}

	void testOpenFoamErrors()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		const QString dir = tmp.path();

		// not a case at all
		writeText(dir + QStringLiteral("/a/case.foam"), QByteArray());
		const ResultReadOutcome none = readResultFile(dir + QStringLiteral("/a/case.foam"));
		CHECK(!none.ok() && none.error.contains(QStringLiteral("polyMesh")));

		// a decomposed case says what to do
		writeText(dir + QStringLiteral("/b/case.foam"), QByteArray());
		QDir().mkpath(dir + QStringLiteral("/b/processor0"));
		const ResultReadOutcome decomposed = readResultFile(dir + QStringLiteral("/b/case.foam"));
		CHECK(!decomposed.ok() && decomposed.error.contains(QStringLiteral("reconstructPar")));

		// binary files are refused with the way out, never misread
		writePrismCase(dir + QStringLiteral("/c"));
		writeText(dir + QStringLiteral("/c/constant/polyMesh/points"), foamHeader("vectorField", "points", "binary") + "10\n(garbage)\n");
		const ResultReadOutcome binary = readResultFile(dir + QStringLiteral("/c/case.foam"));
		CHECK(!binary.ok() && binary.error.contains(QStringLiteral("ASCII")) && binary.error.contains(QStringLiteral("points")));

		// compressed mesh files
		writePrismCase(dir + QStringLiteral("/d"));
		QFile::remove(dir + QStringLiteral("/d/constant/polyMesh/faces"));
		writeText(dir + QStringLiteral("/d/constant/polyMesh/faces.gz"), QByteArray("x"));
		const ResultReadOutcome gz = readResultFile(dir + QStringLiteral("/d/case.foam"));
		CHECK(!gz.ok() && gz.error.contains(QStringLiteral("compress")));

		// damaged lists and inconsistent meshes
		writePrismCase(dir + QStringLiteral("/e"));
		writeText(dir + QStringLiteral("/e/constant/polyMesh/owner"), foamHeader("labelList", "owner") + "7\n(\n0\n0\n)\n"); // too short
		CHECK(!readResultFile(dir + QStringLiteral("/e/case.foam")).ok());
		writePrismCase(dir + QStringLiteral("/f"));
		writeText(dir + QStringLiteral("/f/constant/polyMesh/faces"), foamHeader("faceList", "faces") + "1\n(\n3(0 1 99)\n)\n"); // a node that does not exist
		writeText(dir + QStringLiteral("/f/constant/polyMesh/owner"), foamHeader("labelList", "owner") + "1\n(\n0\n)\n");
		CHECK(!readResultFile(dir + QStringLiteral("/f/case.foam")).ok());

		// a field with the wrong number of values, or in binary, is skipped with a warning; the geometry still loads
		writePrismCase(dir + QStringLiteral("/g"));
		writeText(dir + QStringLiteral("/g/1/p"), foamHeader("volScalarField", "p") + "dimensions [0 0 0 0 0 0 0];\ninternalField nonuniform List<scalar> 2(1 2);\n");
		writeText(dir + QStringLiteral("/g/1/q"), foamHeader("volScalarField", "q", "binary") + "internalField nonuniform List<scalar> 1(1);\n");
		const ResultReadOutcome skipped = readResultFile(dir + QStringLiteral("/g/case.foam"));
		CHECK(skipped.ok());
		if (skipped.ok())
		{
			CHECK(skipped.dataset->stepCount() == 1 && skipped.dataset->fields.size() == 1); // only the time-0 p survives
			bool warned = false;
			for (const QString& w : skipped.warnings)
				warned = warned || w.contains(QStringLiteral("skipped"));
			CHECK(warned);
		}

		// a case with a mesh but no fields at all still loads (uncoloured)
		writePrismCase(dir + QStringLiteral("/h"));
		QDir(dir + QStringLiteral("/h/0")).removeRecursively();
		QDir(dir + QStringLiteral("/h/1")).removeRecursively();
		const ResultReadOutcome bare = readResultFile(dir + QStringLiteral("/h/case.foam"));
		CHECK(bare.ok() && bare.dataset->fields.empty() && bare.dataset->stepCount() == 0);
	}

	void testOpenFoamSample()
	{
		const QString path = QStringLiteral(MV_SIMULATION_SAMPLES_DIR) + QStringLiteral("/openfoam_cavity/cavity.foam");
		if (!QFile::exists(path))
		{
			std::printf("  (skipping OpenFOAM sample test: openfoam_cavity not found)\n");
			return;
		}
		const LoadedSimulationResult r = loadSimulationResult(path);
		if (!r.ok())
			std::printf("  OpenFOAM sample failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (!r.ok())
			return;
		const ResultDataset& ds = *r.dataset;
		CHECK(ds.cellCount() == 400 && ds.nodeCount() == 882);
		// 20 x 20 x 1 cells: 880 boundary faces (80 side + 800 front/back) as 1760 triangles, each belonging to one of the 400 cells
		CHECK(r.surface.triangleCount() == 1760);
		bool cellsOk = true;
		for (std::uint32_t cell : r.surface.triangleCell)
			cellsOk = cellsOk && cell < 400;
		CHECK(cellsOk);
		CHECK(ds.stepCount() == 5 && approx(ds.steps[0].time, 0.0) && approx(ds.steps[4].time, 2.0));

		const int T = fieldIndexOf(ds, QStringLiteral("T")), U = fieldIndexOf(ds, QStringLiteral("U"));
		const int p = fieldIndexOf(ds, QStringLiteral("p")), sigma = fieldIndexOf(ds, QStringLiteral("sigma"));
		CHECK(T >= 0 && U >= 0 && p >= 0 && sigma >= 0 && ds.fields.size() == 4);
		if (T < 0 || U < 0 || p < 0 || sigma < 0)
			return;
		for (const ResultField& f : ds.fields)
			CHECK(f.association == ResultFieldAssociation::Cell && f.stepData.size() == 5);

		// units come from the file's own dimensions: K, m/s, Pa; the kinematic pressure gets none
		const ResultField& fT = ds.fields[static_cast<std::size_t>(T)];
		CHECK(fT.quantityKind == QStringLiteral("temperature") && fT.fileUnit == QStringLiteral("K") && fT.unitConfirmed);
		CHECK(ds.fields[static_cast<std::size_t>(U)].quantityKind == QStringLiteral("velocity") && ds.fields[static_cast<std::size_t>(U)].fileUnit == QStringLiteral("m/s"));
		CHECK(ds.fields[static_cast<std::size_t>(p)].fileUnit.isEmpty());
		CHECK(ds.fields[static_cast<std::size_t>(sigma)].quantityKind == QStringLiteral("pressure") && ds.fields[static_cast<std::size_t>(sigma)].fileUnit == QStringLiteral("Pa"));

		// uniform values at time 0: T = 300 everywhere, U = 0
		bool uniformOk = true;
		for (float v : fT.stepData[0])
			uniformOk = uniformOk && v == 300.0f;
		for (float v : ds.fields[static_cast<std::size_t>(U)].stepData[0])
			uniformOk = uniformOk && v == 0.0f;
		CHECK(uniformOk && fT.stepData[0].size() == 400 && ds.fields[static_cast<std::size_t>(U)].stepData[0].size() == 1200);

		// the temperature rises with the spin-up: 300 .. about 325 at the end, monotonic in the mean
		DisplayScalar first, last;
		CHECK(buildDisplayScalar(ds, T, -1, first, 0) && buildDisplayScalar(ds, T, -1, last, 4));
		CHECK(first.cellData && approx(first.maxValue, 300.0) && last.minValue > 300.0f && last.maxValue > 315.0f && last.maxValue < 330.0f);
		CHECK(last.unit == QStringLiteral("K") && !last.unitAssumed);
		// the velocity field is a real vortex at the end
		DisplayScalar speed;
		CHECK(buildDisplayScalar(ds, U, -1, speed, 4) && speed.maxValue > 0.5f);

		// the symmetric tensor is reordered from XX XY XZ YY YZ ZZ = (100 10 0 200 0 300) to XX YY ZZ XY YZ ZX
		const ResultField& fs = ds.fields[static_cast<std::size_t>(sigma)];
		CHECK(fs.components == 6 && fs.componentNames.size() == 6 && fs.componentNames[3] == QStringLiteral("XY"));
		const std::vector<float>& last6 = fs.stepData[4];
		CHECK(last6.size() == 400u * 6u);
		CHECK(last6[0] == 100.0f && last6[1] == 200.0f && last6[2] == 300.0f && last6[3] == 10.0f && last6[4] == 0.0f && last6[5] == 0.0f);

		// the default is the first cell scalar (the file has cell data only); the timeline has five steps
		DisplayScalar chosen;
		CHECK(chooseDefaultDisplayScalar(ds, chosen) && chosen.cellData);

		// it survives a save/restore like any other result: cell fields, units, steps
		SimulationViewState state = defaultViewState(ds);
		SnapshotOptions options;
		options.content = SnapshotOptions::Content::AllFields;
		ResultSnapshot snap;
		DecodedSnapshot dec;
		QString err;
		CHECK(encodeResultSnapshot(ds, r.surface, state, options, snap, &err));
		CHECK(decodeResultSnapshot(snap.json, snap.blobs, r.surface.vertexCount(), r.surface.triangles, dec, &err));
		if (dec.dataset)
		{
			const int Tb = fieldIndexOf(*dec.dataset, QStringLiteral("T"));
			CHECK(Tb >= 0 && dec.dataset->stepCount() == 5 && dec.dataset->fields.size() == 4);
			DisplayScalar back;
			CHECK(buildDisplayScalar(*dec.dataset, Tb, -1, back, 4) && back.cellData && back.unit == QStringLiteral("K"));
			CHECK(back.minValue == last.minValue && back.maxValue == last.maxValue); // whole-model range, all 400 cells
		}
	}

	// ---- Compare-mode pane geometry ---------------------------------------------------------------------------------

	void testComparePanes()
	{
		// two panes side by side in 1000 x 600 with a 4 px gutter
		std::vector<ComparePane> two = computeComparePanes(1000, 600, 2, CompareArrangement::SideBySide, 4);
		CHECK(two.size() == 2);
		CHECK(two[0].rect == QRect(0, 0, 498, 600) && two[1].rect == QRect(502, 0, 498, 600));
		CHECK(two[0].glScissor == QRect(0, 0, 498, 600) && two[1].glScissor == QRect(502, 0, 498, 600));
		// each pane is the full-size view shifted onto the pane: same viewport size, centred on the pane
		CHECK(two[0].glViewport == QRect(-251, 0, 1000, 600) && two[1].glViewport == QRect(251, 0, 1000, 600));
		CHECK(two[0].toWindow == QPoint(251, 0) && two[1].toWindow == QPoint(-251, 0));
		CHECK(comparePaneAt(two, QPoint(10, 10)) == 0 && comparePaneAt(two, QPoint(600, 300)) == 1);
		CHECK(comparePaneAt(two, QPoint(499, 100)) == -1 && comparePaneAt(two, QPoint(1000, 300)) == -1 && comparePaneAt(two, QPoint(-1, 0)) == -1);
		CHECK(comparePaneAt(two, QPoint(497, 599)) == 0 && comparePaneAt(two, QPoint(502, 0)) == 1);

		// stacked, odd height: the pane below starts after the gutter, GL's origin is at the bottom
		std::vector<ComparePane> stacked = computeComparePanes(800, 601, 2, CompareArrangement::Stacked, 1);
		CHECK(stacked.size() == 2 && stacked[0].rect == QRect(0, 0, 800, 300) && stacked[1].rect == QRect(0, 301, 800, 300));
		CHECK(stacked[0].glScissor == QRect(0, 301, 800, 300) && stacked[1].glScissor == QRect(0, 0, 800, 300));

		// a 2 x 2 grid in reading order
		std::vector<ComparePane> grid = computeComparePanes(800, 600, 4, CompareArrangement::Grid, 4);
		CHECK(grid.size() == 4 && grid[0].rect == QRect(0, 0, 398, 298) && grid[1].rect == QRect(402, 0, 398, 298)
		      && grid[2].rect == QRect(0, 302, 398, 298) && grid[3].rect == QRect(402, 302, 398, 298));
		CHECK(comparePaneAt(grid, QPoint(500, 400)) == 3 && comparePaneAt(grid, QPoint(400, 300)) == -1);
		// three panes in a grid: the fourth slot stays empty; a grid of two is just a row
		CHECK(computeComparePanes(800, 600, 3, CompareArrangement::Grid, 4).size() == 3);
		CHECK(computeComparePanes(1000, 600, 2, CompareArrangement::Grid, 4)[1].rect == QRect(502, 0, 498, 600));

		// the count is clamped, one pane is the whole window and needs no shift
		CHECK(computeComparePanes(640, 480, 0, CompareArrangement::SideBySide).size() == 1);
		CHECK(computeComparePanes(640, 480, 9, CompareArrangement::SideBySide).size() == 4);
		const ComparePane whole = computeComparePanes(640, 480, 1, CompareArrangement::SideBySide)[0];
		CHECK(whole.rect == QRect(0, 0, 640, 480) && whole.toWindow == QPoint(0, 0) && whole.glViewport == QRect(0, 0, 640, 480));

		// invariants over many sizes, counts and arrangements: panes stay inside the window and never overlap, and every
		// pane's shifted full-size view is centred on the pane (to a pixel)
		const CompareArrangement arrangements[] = { CompareArrangement::SideBySide, CompareArrangement::Stacked, CompareArrangement::Grid };
		bool inside = true, disjoint = true, centred = true, sized = true, scissorOk = true;
		const int sizes[][2] = { { 1000, 600 }, { 801, 599 }, { 1920, 1080 }, { 333, 777 }, { 100, 100 }, { 9, 5 }, { 5, 5 } };
		for (const auto& sz : sizes)
			for (CompareArrangement arrangement : arrangements)
				for (int count = 1; count <= 4; ++count)
					for (int gutter : { 0, 1, 4, 11 })
					{
						const int w = sz[0], h = sz[1];
						const std::vector<ComparePane> panes = computeComparePanes(w, h, count, arrangement, gutter);
						sized = sized && static_cast<int>(panes.size()) == count;
						for (std::size_t i = 0; i < panes.size(); ++i)
						{
							const ComparePane& a = panes[i];
							inside = inside && a.rect.width() >= 0 && a.rect.height() >= 0 && a.rect.left() >= 0 && a.rect.top() >= 0
								&& a.rect.right() < w + (a.rect.width() == 0 ? 1 : 0) && a.rect.bottom() < h + (a.rect.height() == 0 ? 1 : 0);
							const int cx = a.rect.x() + a.rect.width() / 2, cy = a.rect.y() + a.rect.height() / 2;
							centred = centred && std::abs(cx + a.toWindow.x() - w / 2) <= 1 && std::abs(cy + a.toWindow.y() - h / 2) <= 1
								&& a.glViewport.width() == w && a.glViewport.height() == h
								&& std::abs((a.glViewport.x() + w / 2) - cx) <= 1 && std::abs((a.glViewport.y() + h / 2) - (h - cy)) <= 1;
							scissorOk = scissorOk && a.glScissor.width() == a.rect.width() && a.glScissor.height() == a.rect.height()
								&& a.glScissor.y() == h - (a.rect.y() + a.rect.height());
							for (std::size_t j = i + 1; j < panes.size(); ++j)
								if (!a.rect.isEmpty() && !panes[j].rect.isEmpty())
									disjoint = disjoint && !a.rect.intersects(panes[j].rect);
						}
					}
		CHECK(sized && inside && disjoint && centred && scissorOk);
	}

	// ---- Exodus II reader (needs NetCDF; skipped when the build has none) ------------------------------------------

#if MV_HAVE_NETCDF
	// A small Exodus II file written through the NetCDF API: two HEX8 blocks of one element each that share a face (a
	// 3 x 2 x 2 grid of nodes), three time steps, node variables disp_x/disp_y/disp_z and temperature (plus a symmetric
	// stress tensor in the six stress_xx ... variables when asked) and one element variable "vm".
	bool writeExodusFixture(const QString& path, bool netcdf4, bool withStress)
	{
		int ncid = -1;
		const QByteArray native = QFile::encodeName(path);
		if (nc_create(native.constData(), NC_CLOBBER | (netcdf4 ? NC_NETCDF4 : 0), &ncid) != NC_NOERR)
			return false;
		bool ok = true;
		auto dim = [&](const char* name, std::size_t length) {
			int id = -1;
			ok = ok && nc_def_dim(ncid, name, length, &id) == NC_NOERR;
			return id;
		};
		auto var = [&](const char* name, nc_type type, std::vector<int> dims) {
			int id = -1;
			ok = ok && nc_def_var(ncid, name, type, static_cast<int>(dims.size()), dims.data(), &id) == NC_NOERR;
			return id;
		};
		const std::size_t nodeVars = withStress ? 10 : 4;
		const int dLen = dim("len_string", 33), dDim = dim("num_dim", 3), dNodes = dim("num_nodes", 12), dElem = dim("num_elem", 2);
		const int dBlocks = dim("num_el_blk", 2), dTime = dim("time_step", NC_UNLIMITED);
		const int dNodVar = dim("num_nod_var", nodeVars), dElemVar = dim("num_elem_var", 1);
		const int dEl1 = dim("num_el_in_blk1", 1), dNpe1 = dim("num_nod_per_el1", 8);
		const int dEl2 = dim("num_el_in_blk2", 1), dNpe2 = dim("num_nod_per_el2", 8);
		(void)dDim; (void)dElem; (void)dBlocks;
		const int vx = var("coordx", NC_DOUBLE, { dNodes }), vy = var("coordy", NC_DOUBLE, { dNodes }), vz = var("coordz", NC_DOUBLE, { dNodes });
		const int vc1 = var("connect1", NC_INT, { dEl1, dNpe1 }), vc2 = var("connect2", NC_INT, { dEl2, dNpe2 });
		ok = ok && nc_put_att_text(ncid, vc1, "elem_type", 4, "HEX8") == NC_NOERR && nc_put_att_text(ncid, vc2, "elem_type", 4, "HEX8") == NC_NOERR;
		const int vTime = var("time_whole", NC_DOUBLE, { dTime });
		const int vNodNames = var("name_nod_var", NC_CHAR, { dNodVar, dLen });
		std::vector<int> nodVarIds;
		for (std::size_t i = 1; i <= nodeVars; ++i)
			nodVarIds.push_back(var(QStringLiteral("vals_nod_var%1").arg(i).toLatin1().constData(), NC_DOUBLE, { dTime, dNodes }));
		const int vElemNames = var("name_elem_var", NC_CHAR, { dElemVar, dLen });
		const int vVm1 = var("vals_elem_var1eb1", NC_DOUBLE, { dTime, dEl1 }), vVm2 = var("vals_elem_var1eb2", NC_DOUBLE, { dTime, dEl2 });
		ok = ok && nc_enddef(ncid) == NC_NOERR;

		double coordX[12], coordY[12], coordZ[12];
		for (int n = 0; n < 12; ++n)
		{
			coordX[n] = n % 3;
			coordY[n] = (n / 3) % 2;
			coordZ[n] = n / 6;
		}
		const int connect1[8] = { 1, 2, 5, 4, 7, 8, 11, 10 }, connect2[8] = { 2, 3, 6, 5, 8, 9, 12, 11 };
		const double times[3] = { 0.0, 0.5, 1.0 };
		const std::size_t startTime[1] = { 0 }, countTime[1] = { 3 };
		ok = ok && nc_put_var_double(ncid, vx, coordX) == NC_NOERR && nc_put_var_double(ncid, vy, coordY) == NC_NOERR
		     && nc_put_var_double(ncid, vz, coordZ) == NC_NOERR && nc_put_var_int(ncid, vc1, connect1) == NC_NOERR
		     && nc_put_var_int(ncid, vc2, connect2) == NC_NOERR && nc_put_vara_double(ncid, vTime, startTime, countTime, times) == NC_NOERR;

		const char* names[10] = { "disp_x", "disp_y", "disp_z", "temperature", "stress_xx", "stress_yy", "stress_zz", "stress_xy", "stress_yz", "stress_zx" };
		std::vector<char> nameBuffer(nodeVars * 33, '\0');
		for (std::size_t i = 0; i < nodeVars; ++i)
			std::snprintf(&nameBuffer[i * 33], 33, "%s", names[i]);
		ok = ok && nc_put_var_text(ncid, vNodNames, nameBuffer.data()) == NC_NOERR;
		std::vector<char> elemName(33, '\0');
		std::snprintf(elemName.data(), 33, "vm");
		ok = ok && nc_put_var_text(ncid, vElemNames, elemName.data()) == NC_NOERR;

		for (std::size_t s = 0; s < 3; ++s)
		{
			const std::size_t start[2] = { s, 0 }, count[2] = { 1, 12 };
			for (std::size_t v = 0; v < nodeVars; ++v)
			{
				double values[12];
				for (int n = 0; n < 12; ++n)
				{
					const double scale = static_cast<double>(s + 1);
					switch (v)
					{
					case 0: values[n] = scale * 1.0e-3 * (n + 1); break;   // disp_x
					case 1: values[n] = 0.0; break;                        // disp_y
					case 2: values[n] = scale * 2.0e-3; break;             // disp_z
					case 3: values[n] = 20.0 + 10.0 * static_cast<double>(s) + n; break; // temperature
					case 4: values[n] = 100.0; break;                      // stress_xx: uniaxial, von Mises 100
					default: values[n] = 0.0; break;
					}
				}
				ok = ok && nc_put_vara_double(ncid, nodVarIds[v], start, count, values) == NC_NOERR;
			}
			const std::size_t startE[2] = { s, 0 }, countE[2] = { 1, 1 };
			const double vm1 = 100.0 + static_cast<double>(s), vm2 = 200.0 + static_cast<double>(s);
			ok = ok && nc_put_vara_double(ncid, vVm1, startE, countE, &vm1) == NC_NOERR && nc_put_vara_double(ncid, vVm2, startE, countE, &vm2) == NC_NOERR;
		}
		return nc_close(ncid) == NC_NOERR && ok;
	}
#endif

#if MV_HAVE_NETCDF
	// A larger Exodus II file for trying the reader in the application: an n x n x n block of HEX8 elements, five time
	// steps of a bending-and-warming cube (disp_x/y/z, temperature, a symmetric stress tensor) and an element variable.
	// Written with `result_tests --write-exodus-sample <file.exo>` (classic NetCDF, the layout most Exodus tools write).
	bool writeExodusBlockSample(const char* path, int n = 8)
	{
		const int nodesPerSide = n + 1, nodeCount = nodesPerSide * nodesPerSide * nodesPerSide, elemCount = n * n * n, steps = 5;
		const int nodeVars = 10;
		int ncid = -1;
		if (nc_create(path, NC_CLOBBER | NC_64BIT_OFFSET, &ncid) != NC_NOERR)
			return false;
		bool ok = true;
		auto dim = [&](const char* name, std::size_t length) { int id = -1; ok = ok && nc_def_dim(ncid, name, length, &id) == NC_NOERR; return id; };
		auto var = [&](const char* name, nc_type type, std::vector<int> dims) {
			int id = -1;
			ok = ok && nc_def_var(ncid, name, type, static_cast<int>(dims.size()), dims.data(), &id) == NC_NOERR;
			return id;
		};
		const int dLen = dim("len_string", 33), dNodes = dim("num_nodes", static_cast<std::size_t>(nodeCount)), dTime = dim("time_step", NC_UNLIMITED);
		dim("num_dim", 3);
		dim("num_elem", static_cast<std::size_t>(elemCount));
		dim("num_el_blk", 1);
		const int dNodVar = dim("num_nod_var", nodeVars), dElemVar = dim("num_elem_var", 1);
		const int dEl = dim("num_el_in_blk1", static_cast<std::size_t>(elemCount)), dNpe = dim("num_nod_per_el1", 8);
		const int vx = var("coordx", NC_DOUBLE, { dNodes }), vy = var("coordy", NC_DOUBLE, { dNodes }), vz = var("coordz", NC_DOUBLE, { dNodes });
		const int vc = var("connect1", NC_INT, { dEl, dNpe });
		ok = ok && nc_put_att_text(ncid, vc, "elem_type", 4, "HEX8") == NC_NOERR;
		const int vTime = var("time_whole", NC_DOUBLE, { dTime });
		const int vNames = var("name_nod_var", NC_CHAR, { dNodVar, dLen });
		std::vector<int> valueVars;
		for (int i = 1; i <= nodeVars; ++i)
			valueVars.push_back(var(QStringLiteral("vals_nod_var%1").arg(i).toLatin1().constData(), NC_DOUBLE, { dTime, dNodes }));
		const int vElemNames = var("name_elem_var", NC_CHAR, { dElemVar, dLen });
		const int vElemVals = var("vals_elem_var1eb1", NC_DOUBLE, { dTime, dEl });
		ok = ok && nc_enddef(ncid) == NC_NOERR;

		std::vector<double> cx(static_cast<std::size_t>(nodeCount)), cy(cx.size()), cz(cx.size());
		auto node = [&](int i, int j, int k) { return i + nodesPerSide * (j + nodesPerSide * k); };
		for (int k = 0; k < nodesPerSide; ++k)
			for (int j = 0; j < nodesPerSide; ++j)
				for (int i = 0; i < nodesPerSide; ++i)
				{
					const std::size_t id = static_cast<std::size_t>(node(i, j, k));
					cx[id] = i;
					cy[id] = j;
					cz[id] = k;
				}
		std::vector<int> connect;
		for (int k = 0; k < n; ++k)
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
					for (int c : { node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k),
					               node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1) })
						connect.push_back(c + 1); // 1-based
		ok = ok && nc_put_var_double(ncid, vx, cx.data()) == NC_NOERR && nc_put_var_double(ncid, vy, cy.data()) == NC_NOERR
		     && nc_put_var_double(ncid, vz, cz.data()) == NC_NOERR && nc_put_var_int(ncid, vc, connect.data()) == NC_NOERR;

		const char* names[10] = { "disp_x", "disp_y", "disp_z", "temperature", "stress_xx", "stress_yy", "stress_zz", "stress_xy", "stress_yz", "stress_zx" };
		std::vector<char> nameBuffer(static_cast<std::size_t>(nodeVars) * 33, '\0');
		for (int i = 0; i < nodeVars; ++i)
			std::snprintf(&nameBuffer[static_cast<std::size_t>(i) * 33], 33, "%s", names[i]);
		std::vector<char> elemName(33, '\0');
		std::snprintf(elemName.data(), 33, "element_quality");
		ok = ok && nc_put_var_text(ncid, vNames, nameBuffer.data()) == NC_NOERR && nc_put_var_text(ncid, vElemNames, elemName.data()) == NC_NOERR;

		for (int s = 0; s < steps; ++s)
		{
			const double t = static_cast<double>(s) / (steps - 1), amp = t; // 0 .. 1
			const std::size_t timeStart[1] = { static_cast<std::size_t>(s) }, one[1] = { 1 };
			ok = ok && nc_put_vara_double(ncid, vTime, timeStart, one, &t) == NC_NOERR;
			const std::size_t start[2] = { static_cast<std::size_t>(s), 0 }, count[2] = { 1, static_cast<std::size_t>(nodeCount) };
			std::vector<double> values(static_cast<std::size_t>(nodeCount));
			for (int v = 0; v < nodeVars; ++v)
			{
				for (std::size_t id = 0; id < values.size(); ++id)
				{
					const double x = cx[id] / n, y = cy[id] / n, z = cz[id] / n; // 0 .. 1
					switch (v)
					{
					case 0: values[id] = -amp * 0.15 * x * (z - 0.5); break;      // disp_x: the cube bends about y ...
					case 1: values[id] = 0.0; break;
					case 2: values[id] = amp * 0.15 * x * x; break;              // ... its free end (x = 1) dropping most
					case 3: values[id] = 20.0 + 80.0 * amp * x; break;           // temperature rising towards x = 1
					case 4: values[id] = amp * 200.0 * x * (z - 0.5); break;     // stress_xx
					case 5: values[id] = amp * 20.0 * (1.0 - x); break;          // stress_yy
					case 6: values[id] = amp * 10.0 * y; break;                  // stress_zz
					case 7: values[id] = amp * 30.0 * z * (1.0 - x); break;      // stress_xy
					case 8: values[id] = amp * 15.0 * y * z; break;              // stress_yz
					default: values[id] = amp * 25.0 * x * y; break;             // stress_zx
					}
				}
				ok = ok && nc_put_vara_double(ncid, valueVars[static_cast<std::size_t>(v)], start, count, values.data()) == NC_NOERR;
			}
			std::vector<double> quality(static_cast<std::size_t>(elemCount));
			for (int e = 0; e < elemCount; ++e)
				quality[static_cast<std::size_t>(e)] = 0.5 + 0.5 * std::sin(0.1 * e + 2.0 * t);
			const std::size_t startE[2] = { static_cast<std::size_t>(s), 0 }, countE[2] = { 1, static_cast<std::size_t>(elemCount) };
			ok = ok && nc_put_vara_double(ncid, vElemVals, startE, countE, quality.data()) == NC_NOERR;
		}
		return nc_close(ncid) == NC_NOERR && ok;
	}
#endif

	void testExodus()
	{
#if MV_HAVE_NETCDF
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		CHECK(exodusSupported() && !exodusFileFilter().isEmpty());
		CHECK(supportedResultExtensions().contains(QStringLiteral("exo")) && supportedResultExtensions().contains(QStringLiteral("e")));
		CHECK(isSupportedResultFile(QStringLiteral("run.EXO")) && isSupportedResultFile(QStringLiteral("mesh.g")));

		for (int variant = 0; variant < 2; ++variant)
		{
			const bool netcdf4 = variant == 1, withStress = variant == 1; // classic without stress, netCDF-4/HDF5 with it
			const QString path = tmp.path() + (netcdf4 ? QStringLiteral("/v4.exo") : QStringLiteral("/v3.exo"));
			CHECK(writeExodusFixture(path, netcdf4, withStress));
			const ResultReadOutcome r = readResultFile(path);
			if (!r.ok())
				std::printf("  Exodus %s failed: %s\n", netcdf4 ? "netCDF-4" : "classic", qPrintable(r.error));
			CHECK(r.ok());
			if (!r.ok())
				continue;
			const ResultDataset& ds = *r.dataset;
			CHECK(ds.solverName == QStringLiteral("Exodus"));
			CHECK(ds.nodeCount() == 12 && ds.cellCount() == 2);
			CHECK(ds.cellTypes[0] == ResultCellType::Hexahedron && ds.cellTypes[1] == ResultCellType::Hexahedron);
			const std::vector<std::uint32_t> firstHex = { 0, 1, 4, 3, 6, 7, 10, 9 }; // Exodus is 1-based
			CHECK(std::vector<std::uint32_t>(ds.cellConnectivity.begin(), ds.cellConnectivity.begin() + 8) == firstHex);
			CHECK(approx(ds.nodePositions[5 * 3 + 0], 2.0) && approx(ds.nodePositions[5 * 3 + 1], 1.0) && approx(ds.nodePositions[5 * 3 + 2], 0.0, 1e-4, 1e-9));
			CHECK(ds.stepCount() == 3 && approx(ds.steps[0].time, 0.0) && approx(ds.steps[1].time, 0.5) && approx(ds.steps[2].time, 1.0));
			CHECK(ds.validate().isEmpty());

			// disp_x/_y/_z become one vector field, temperature stays a scalar, the element variable is a cell field
			const int disp = fieldIndexOf(ds, QStringLiteral("disp")), temperature = fieldIndexOf(ds, QStringLiteral("temperature"));
			const ResultField* vm = ds.findField(QStringLiteral("vm"), ResultFieldAssociation::Cell);
			CHECK(disp >= 0 && temperature >= 0 && vm != nullptr);
			CHECK(ds.fields.size() == (withStress ? 9u : 3u)); // + the stress tensor and its five derived fields
			if (disp >= 0 && temperature >= 0 && vm)
			{
				const ResultField& fd = ds.fields[static_cast<std::size_t>(disp)];
				CHECK(fd.components == 3 && fd.association == ResultFieldAssociation::Node && fd.stepData.size() == 3);
				CHECK(approx(fd.stepData[2][5 * 3 + 0], 0.018) && approx(fd.stepData[2][5 * 3 + 1], 0.0, 1e-4, 1e-9) && approx(fd.stepData[2][5 * 3 + 2], 0.006));
				const ResultField& ft = ds.fields[static_cast<std::size_t>(temperature)];
				CHECK(ft.components == 1 && approx(ft.stepData[1][3], 33.0) && approx(ft.stepData[2][11], 51.0));
				CHECK(vm->components == 1 && vm->stepData[2].size() == 2 && approx(vm->stepData[2][0], 102.0) && approx(vm->stepData[2][1], 202.0));
			}

			// two hexahedra sharing a face: 12 faces - 2 shared = 10 boundary quads = 20 triangles
			ResultBoundarySurface surface;
			CHECK(extractBoundarySurface(ds, surface, nullptr, nullptr));
			CHECK(surface.triangleCount() == 20 && surface.vertexCount() == 12);

			if (withStress)
			{
				// the six stress components form one symmetric tensor and get the derived structural fields
				const int stress = fieldIndexOf(ds, QStringLiteral("stress")), mises = fieldIndexOf(ds, QStringLiteral("stress von Mises"));
				CHECK(stress >= 0 && mises >= 0 && ds.fields[static_cast<std::size_t>(stress)].components == 6);
				const LoadedSimulationResult loaded = loadSimulationResult(path);
				CHECK(loaded.ok());
				if (loaded.ok())
				{
					DisplayScalar d;
					CHECK(chooseDefaultDisplayScalar(*loaded.dataset, d) && d.label == QStringLiteral("stress von Mises") && approx(d.maxValue, 100.0));
				}
			}
		}

		// problems are reported, never crashed on
		const ResultReadOutcome missing = readResultFile(tmp.path() + QStringLiteral("/missing.exo"));
		CHECK(!missing.ok() && missing.error.contains(QStringLiteral("Cannot open")));
		writeText(tmp.path() + QStringLiteral("/bad.exo"), QByteArray("this is not a NetCDF file"));
		CHECK(!readResultFile(tmp.path() + QStringLiteral("/bad.exo")).ok());
		{
			int ncid = -1, dimid = -1;
			const QByteArray plain = QFile::encodeName(tmp.path() + QStringLiteral("/plain.nc"));
			CHECK(nc_create(plain.constData(), NC_CLOBBER, &ncid) == NC_NOERR);
			nc_def_dim(ncid, "foo", 3, &dimid);
			nc_enddef(ncid);
			nc_close(ncid);
			QFile::copy(tmp.path() + QStringLiteral("/plain.nc"), tmp.path() + QStringLiteral("/plain.exo"));
			const ResultReadOutcome notExodus = readResultFile(tmp.path() + QStringLiteral("/plain.exo"));
			CHECK(!notExodus.ok() && notExodus.error.contains(QStringLiteral("not an Exodus II mesh")));
		}
#else
		std::printf("  (skipping Exodus tests: this build has no NetCDF)\n");
		CHECK(!exodusSupported() && exodusExtensions().isEmpty() && !isSupportedResultFile(QStringLiteral("run.exo")));
		CHECK(!readResultFile(QStringLiteral("run.exo")).ok());
#endif
	}

	// ---- CGNS reader (needs the CGNS library; skipped when the build has none) ---------------------------------------

#if MV_HAVE_CGNS
	// A small CGNS file written through the CGNS API: one unstructured zone of 12 nodes (the 3 x 2 x 2 grid of the Exodus
	// fixture) holding two HEXA_8 cells (or, `mixed`, one HEXA_8 and one TETRA_4 in a MIXED section), a QUAD_4 boundary
	// section that must NOT become cells, three steps of Vertex solutions (Temperature, VelocityX/Y/Z) and CellCenter
	// solutions (Quality), and BaseIterativeData/TimeValues.
	bool writeCgnsFixture(const char* path, bool mixed, bool withSolutions)
	{
		int fn = 0, base = 0, zone = 0;
		if (cg_open(path, CG_MODE_WRITE, &fn) != CG_OK)
			return false;
		bool ok = cg_base_write(fn, "Base", 3, 3, &base) == CG_OK;
		const cgsize_t size[3] = { 12, 2, 0 };
		ok = ok && cg_zone_write(fn, base, "Zone1", size, CGNS_ENUMV(Unstructured), &zone) == CG_OK;
		double x[12], y[12], z[12];
		for (int n = 0; n < 12; ++n)
		{
			x[n] = n % 3;
			y[n] = (n / 3) % 2;
			z[n] = n / 6;
		}
		int index = 0;
		ok = ok && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateX", x, &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateY", y, &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateZ", z, &index) == CG_OK;
		if (mixed)
		{
			// CGNS 4 writes MIXED sections through the polyhedral call: each element's type precedes its nodes and one offset per
			// element (plus the end) says where it starts. (The older call is kept as a fallback for other library versions.)
			const cgsize_t mix[14] = { CGNS_ENUMV(HEXA_8), 1, 2, 5, 4, 7, 8, 11, 10, CGNS_ENUMV(TETRA_4), 2, 3, 6, 8 };
			const cgsize_t mixOffsets[3] = { 0, 9, 14 };
			const bool poly = cg_poly_section_write(fn, base, zone, "Mixed", CGNS_ENUMV(MIXED), 1, 2, 0, mix, mixOffsets, &index) == CG_OK;
			ok = ok && (poly || cg_section_write(fn, base, zone, "Mixed", CGNS_ENUMV(MIXED), 1, 2, 0, mix, &index) == CG_OK);
		}
		else
		{
			const cgsize_t hexes[16] = { 1, 2, 5, 4, 7, 8, 11, 10, 2, 3, 6, 5, 8, 9, 12, 11 };
			const cgsize_t walls[8] = { 1, 2, 5, 4, 7, 8, 11, 10 }; // two quads: boundary elements 3 and 4, not cells
			ok = ok && cg_section_write(fn, base, zone, "Hexas", CGNS_ENUMV(HEXA_8), 1, 2, 0, hexes, &index) == CG_OK
			     && cg_section_write(fn, base, zone, "Walls", CGNS_ENUMV(QUAD_4), 3, 4, 0, walls, &index) == CG_OK;
		}
		if (withSolutions)
		{
			for (int s = 0; s < 3; ++s)
			{
				double temperature[12], vx[12], vy[12], vz[12];
				for (int n = 0; n < 12; ++n)
				{
					temperature[n] = 20.0 + 10.0 * s + n;
					vx[n] = (s + 1) * 0.1 * n;
					vy[n] = 0.0;
					vz[n] = (s + 1) * 0.2;
				}
				int sol = 0, field = 0;
				const QByteArray vertexName = QByteArray("FlowSolution") + QByteArray::number(s);
				ok = ok && cg_sol_write(fn, base, zone, vertexName.constData(), CGNS_ENUMV(Vertex), &sol) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Temperature", temperature, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityX", vx, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityY", vy, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityZ", vz, &field) == CG_OK;
				// a symmetric stress tensor: uniaxial 100 (StressXX), the rest zero -> von Mises 100
				double stressXX[12], zero[12];
				for (int n = 0; n < 12; ++n)
				{
					stressXX[n] = 100.0;
					zero[n] = 0.0;
				}
				ok = ok && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "StressXX", stressXX, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "StressYY", zero, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "StressZZ", zero, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "StressXY", zero, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "StressYZ", zero, &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "StressXZ", zero, &field) == CG_OK;
				const double quality[2] = { 100.0 + s, 200.0 + s };
				const QByteArray cellName = QByteArray("CellSolution") + QByteArray::number(s);
				ok = ok && cg_sol_write(fn, base, zone, cellName.constData(), CGNS_ENUMV(CellCenter), &sol) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Quality", quality, &field) == CG_OK;
			}
			const double times[3] = { 0.0, 0.5, 1.0 };
			const cgsize_t count = 3;
			ok = ok && cg_biter_write(fn, base, "TimeIterValues", 3) == CG_OK
			     && cg_goto(fn, base, "BaseIterativeData_t", 1, "end") == CG_OK
			     && cg_array_write("TimeValues", CGNS_ENUMV(RealDouble), 1, &count, times) == CG_OK;
		}
		return cg_close(fn) == CG_OK && ok;
	}

	// A larger CGNS file for trying the reader in the application: an n x n x n block of HEXA_8 cells, five steps of a
	// warming, accelerating cube (Temperature, Pressure and VelocityX/Y/Z at the vertices, Quality per cell).
	// Written with `result_tests --write-cgns-sample <file.cgns>`.
	bool writeCgnsBlockSample(const char* path, int n = 8)
	{
		const int side = n + 1, nodeCount = side * side * side, cells = n * n * n, steps = 5;
		int fn = 0, base = 0, zone = 0, index = 0;
		if (cg_open(path, CG_MODE_WRITE, &fn) != CG_OK)
			return false;
		bool ok = cg_base_write(fn, "Base", 3, 3, &base) == CG_OK;
		const cgsize_t size[3] = { nodeCount, cells, 0 };
		ok = ok && cg_zone_write(fn, base, "Block", size, CGNS_ENUMV(Unstructured), &zone) == CG_OK;
		std::vector<double> cx(static_cast<std::size_t>(nodeCount)), cy(cx.size()), cz(cx.size());
		auto node = [&](int i, int j, int k) { return i + side * (j + side * k); };
		for (int k = 0; k < side; ++k)
			for (int j = 0; j < side; ++j)
				for (int i = 0; i < side; ++i)
				{
					const std::size_t id = static_cast<std::size_t>(node(i, j, k));
					cx[id] = i;
					cy[id] = j;
					cz[id] = k;
				}
		std::vector<cgsize_t> connect;
		for (int k = 0; k < n; ++k)
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
					for (int c : { node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k),
					               node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1) })
						connect.push_back(c + 1);
		ok = ok && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateX", cx.data(), &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateY", cy.data(), &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateZ", cz.data(), &index) == CG_OK
		     && cg_section_write(fn, base, zone, "Elements", CGNS_ENUMV(HEXA_8), 1, cells, 0, connect.data(), &index) == CG_OK;
		std::vector<double> times;
		for (int s = 0; s < steps; ++s)
		{
			const double t = static_cast<double>(s) / (steps - 1);
			times.push_back(t);
			std::vector<double> temperature(cx.size()), pressure(cx.size()), vx(cx.size()), vy(cx.size()), vz(cx.size());
			for (std::size_t id = 0; id < cx.size(); ++id)
			{
				const double x = cx[id] / n, y = cy[id] / n, z = cz[id] / n;
				temperature[id] = 300.0 + 60.0 * t * x;
				pressure[id] = 101325.0 + 500.0 * t * (1.0 - x) * (y + z);
				vx[id] = t * 2.0 * y * (1.0 - y) * 4.0;
				vy[id] = t * 0.5 * std::sin(3.14159265 * x);
				vz[id] = t * 0.25 * (z - 0.5);
			}
			int sol = 0, field = 0;
			const QByteArray vertexName = QByteArray("FlowSolution") + QByteArray::number(s);
			ok = ok && cg_sol_write(fn, base, zone, vertexName.constData(), CGNS_ENUMV(Vertex), &sol) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Temperature", temperature.data(), &field) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Pressure", pressure.data(), &field) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityX", vx.data(), &field) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityY", vy.data(), &field) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityZ", vz.data(), &field) == CG_OK;
			std::vector<double> quality(static_cast<std::size_t>(cells));
			for (int e = 0; e < cells; ++e)
				quality[static_cast<std::size_t>(e)] = 0.5 + 0.5 * std::sin(0.1 * e + 2.0 * t);
			const QByteArray cellName = QByteArray("CellSolution") + QByteArray::number(s);
			ok = ok && cg_sol_write(fn, base, zone, cellName.constData(), CGNS_ENUMV(CellCenter), &sol) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Quality", quality.data(), &field) == CG_OK;
		}
		const cgsize_t stepCount = steps;
		ok = ok && cg_biter_write(fn, base, "TimeIterValues", steps) == CG_OK && cg_goto(fn, base, "BaseIterativeData_t", 1, "end") == CG_OK
		     && cg_array_write("TimeValues", CGNS_ENUMV(RealDouble), 1, &stepCount, times.data()) == CG_OK;
		return cg_close(fn) == CG_OK && ok;
	}
#endif

#if MV_HAVE_CGNS
	// A multi-block structured CGNS file for trying the reader in the application: two curved blocks (quarter rings, 12 x 6 x 4
	// cells each) that together make a half-ring duct, four steps of a swirling flow (Temperature, Pressure, VelocityX/Y/Z at
	// the points, Quality per cell). Written with `result_tests --write-cgns-structured-sample <file.cgns>`.
	bool writeCgnsStructuredSample(const char* path)
	{
		const int ni = 13, nj = 7, nk = 5, steps = 4; // points per direction
		const double pi = 3.14159265358979323846;
		int fn = 0, base = 0;
		if (cg_open(path, CG_MODE_WRITE, &fn) != CG_OK)
			return false;
		bool ok = cg_base_write(fn, "Duct", 3, 3, &base) == CG_OK;
		const std::size_t points = static_cast<std::size_t>(ni * nj * nk), cells = static_cast<std::size_t>((ni - 1) * (nj - 1) * (nk - 1));
		for (int block = 0; block < 2; ++block)
		{
			int zone = 0, index = 0;
			const cgsize_t size[9] = { ni, nj, nk, ni - 1, nj - 1, nk - 1, 0, 0, 0 };
			const QByteArray zoneName = QByteArray("Block") + QByteArray::number(block + 1);
			ok = ok && cg_zone_write(fn, base, zoneName.constData(), size, CGNS_ENUMV(Structured), &zone) == CG_OK;
			std::vector<double> x(points), y(points), z(points), theta(points), radius(points);
			for (int k = 0; k < nk; ++k)
				for (int j = 0; j < nj; ++j)
					for (int i = 0; i < ni; ++i)
					{
						const std::size_t id = static_cast<std::size_t>(i + ni * (j + nj * k)); // i fastest
						theta[id] = 0.5 * pi * (block + static_cast<double>(i) / (ni - 1));
						radius[id] = 1.0 + static_cast<double>(j) / (nj - 1);
						x[id] = radius[id] * std::cos(theta[id]);
						y[id] = radius[id] * std::sin(theta[id]);
						z[id] = static_cast<double>(k) / (nk - 1);
					}
			ok = ok && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateX", x.data(), &index) == CG_OK
			     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateY", y.data(), &index) == CG_OK
			     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateZ", z.data(), &index) == CG_OK;
			for (int s = 0; s < steps; ++s)
			{
				const double t = static_cast<double>(s + 1) / steps;
				std::vector<double> temperature(points), pressure(points), vx(points), vy(points), vz(points);
				for (std::size_t id = 0; id < points; ++id)
				{
					const double r = radius[id], a = theta[id];
					const double speed = t * 12.0 * (r - 1.0) * (2.0 - r); // fastest mid-duct, zero at the walls
					temperature[id] = 300.0 + 60.0 * t * a / pi + 20.0 * (r - 1.0);
					pressure[id] = 101325.0 + 400.0 * t * (1.0 - a / pi);
					vx[id] = -speed * std::sin(a);
					vy[id] = speed * std::cos(a);
					vz[id] = 0.3 * t * (z[id] - 0.5);
				}
				int sol = 0, field = 0;
				const QByteArray vertexName = QByteArray("FlowSolution") + QByteArray::number(s);
				ok = ok && cg_sol_write(fn, base, zone, vertexName.constData(), CGNS_ENUMV(Vertex), &sol) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Temperature", temperature.data(), &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Pressure", pressure.data(), &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityX", vx.data(), &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityY", vy.data(), &field) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "VelocityZ", vz.data(), &field) == CG_OK;
				std::vector<double> quality(cells);
				for (std::size_t c = 0; c < cells; ++c)
					quality[c] = 0.5 + 0.5 * std::sin(0.05 * static_cast<double>(c) + 2.0 * t + block);
				const QByteArray cellName = QByteArray("CellSolution") + QByteArray::number(s);
				ok = ok && cg_sol_write(fn, base, zone, cellName.constData(), CGNS_ENUMV(CellCenter), &sol) == CG_OK
				     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Quality", quality.data(), &field) == CG_OK;
			}
		}
		std::vector<double> times;
		for (int s = 0; s < steps; ++s)
			times.push_back(0.5 * (s + 1));
		const cgsize_t stepCount = steps;
		ok = ok && cg_biter_write(fn, base, "TimeIterValues", steps) == CG_OK && cg_goto(fn, base, "BaseIterativeData_t", 1, "end") == CG_OK
		     && cg_array_write("TimeValues", CGNS_ENUMV(RealDouble), 1, &stepCount, times.data()) == CG_OK;
		return cg_close(fn) == CG_OK && ok;
	}

#endif

	void testCgns()
	{
#if MV_HAVE_CGNS
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		CHECK(cgnsSupported() && !cgnsFileFilter().isEmpty());
		CHECK(supportedResultExtensions().contains(QStringLiteral("cgns")) && isSupportedResultFile(QStringLiteral("run.CGNS")));

		// ---- fixed-type section, boundary section, solutions with times
		const QString path = tmp.path() + QStringLiteral("/fixture.cgns");
		CHECK(writeCgnsFixture(QFile::encodeName(path).constData(), false, true));
		const ResultReadOutcome r = readResultFile(path);
		if (!r.ok())
			std::printf("  CGNS failed: %s\n", qPrintable(r.error));
		CHECK(r.ok());
		if (r.ok())
		{
			const ResultDataset& ds = *r.dataset;
			CHECK(ds.solverName == QStringLiteral("CGNS"));
			CHECK(ds.nodeCount() == 12 && ds.cellCount() == 2); // the two QUAD_4 boundary elements are not cells
			CHECK(ds.cellTypes[0] == ResultCellType::Hexahedron && ds.cellTypes[1] == ResultCellType::Hexahedron);
			const std::vector<std::uint32_t> firstHex = { 0, 1, 4, 3, 6, 7, 10, 9 };
			CHECK(std::vector<std::uint32_t>(ds.cellConnectivity.begin(), ds.cellConnectivity.begin() + 8) == firstHex);
			CHECK(approx(ds.nodePositions[5 * 3], 2.0) && approx(ds.nodePositions[5 * 3 + 1], 1.0) && approx(ds.nodePositions[5 * 3 + 2], 0.0, 1e-4, 1e-9));
			CHECK(ds.stepCount() == 3 && approx(ds.steps[0].time, 0.0) && approx(ds.steps[1].time, 0.5) && approx(ds.steps[2].time, 1.0));
			CHECK(ds.validate().isEmpty());

			const int temperature = fieldIndexOf(ds, QStringLiteral("Temperature")), velocity = fieldIndexOf(ds, QStringLiteral("Velocity"));
			const ResultField* quality = ds.findField(QStringLiteral("Quality"), ResultFieldAssociation::Cell);
			// Temperature, Velocity (3), Quality (cell), Stress (6) and its five derived fields
			if (ds.fields.size() != 9)
				for (const ResultField& f : ds.fields)
					std::printf("  CGNS field: '%s' (%s, %d comp)\n", qPrintable(f.name), f.association == ResultFieldAssociation::Cell ? "cell" : "node", f.components);
			CHECK(temperature >= 0 && velocity >= 0 && quality != nullptr && ds.fields.size() == 9);
			// the six StressXX ... components are ONE tensor, not a vector "StressX" plus strays
			const int stress = fieldIndexOf(ds, QStringLiteral("Stress")), mises = fieldIndexOf(ds, QStringLiteral("Stress von Mises"));
			CHECK(stress >= 0 && ds.fields[static_cast<std::size_t>(stress)].components == 6 && mises >= 0);
			CHECK(fieldIndexOf(ds, QStringLiteral("StressX")) < 0 && fieldIndexOf(ds, QStringLiteral("StressXX")) < 0);
			DisplayScalar vm;
			CHECK(mises >= 0 && buildDisplayScalar(ds, mises, -1, vm, 2) && approx(vm.maxValue, 100.0));
			const std::vector<float>& tensorData = ds.fields[static_cast<std::size_t>(stress)].stepData[0];
			CHECK(tensorData.size() == 12u * 6u && tensorData[0] == 100.0f && tensorData[1] == 0.0f && tensorData[3] == 0.0f);
			if (temperature >= 0 && velocity >= 0 && quality)
			{
				const ResultField& ft = ds.fields[static_cast<std::size_t>(temperature)];
				CHECK(ft.association == ResultFieldAssociation::Node && ft.components == 1 && approx(ft.stepData[1][3], 33.0) && approx(ft.stepData[2][11], 51.0));
				// VelocityX/Y/Z became one vector field
				const ResultField& fv = ds.fields[static_cast<std::size_t>(velocity)];
				CHECK(fv.components == 3 && fv.association == ResultFieldAssociation::Node);
				CHECK(approx(fv.stepData[2][5 * 3 + 0], 1.5) && approx(fv.stepData[2][5 * 3 + 1], 0.0, 1e-4, 1e-9) && approx(fv.stepData[2][5 * 3 + 2], 0.6));
				CHECK(quality->stepData[2].size() == 2 && approx(quality->stepData[2][0], 102.0) && approx(quality->stepData[2][1], 202.0));
			}
			ResultBoundarySurface surface;
			CHECK(extractBoundarySurface(ds, surface, nullptr, nullptr));
			CHECK(surface.triangleCount() == 20); // two hexahedra sharing a face
		}

		// ---- a MIXED section keeps each element's own type
		const QString mixedPath = tmp.path() + QStringLiteral("/mixed.cgns");
		CHECK(writeCgnsFixture(QFile::encodeName(mixedPath).constData(), true, false));
		const ResultReadOutcome mixed = readResultFile(mixedPath);
		CHECK(mixed.ok());
		if (mixed.ok())
		{
			const ResultDataset& ds = *mixed.dataset;
			CHECK(ds.cellCount() == 2 && ds.cellTypes[0] == ResultCellType::Hexahedron && ds.cellTypes[1] == ResultCellType::Tetra);
			CHECK(ds.cellOffsets[1] == 8 && ds.cellOffsets[2] == 12);
			CHECK(ds.stepCount() == 1 && ds.fields.empty()); // a mesh without solutions loads uncoloured
		}

		// ---- problems are reported, never crashed on
		const ResultReadOutcome missing = readResultFile(tmp.path() + QStringLiteral("/missing.cgns"));
		CHECK(!missing.ok() && missing.error.contains(QStringLiteral("Cannot open")));
		writeText(tmp.path() + QStringLiteral("/bad.cgns"), QByteArray("this is not a CGNS file"));
		CHECK(!readResultFile(tmp.path() + QStringLiteral("/bad.cgns")).ok());
#else
		std::printf("  (skipping CGNS tests: this build has no CGNS library)\n");
		CHECK(!cgnsSupported() && cgnsExtensions().isEmpty() && !isSupportedResultFile(QStringLiteral("run.cgns")));
		CHECK(!readResultFile(QStringLiteral("run.cgns")).ok());
#endif
	}

	// ---- Regression tests from the code review of the branch --------------------------------------------------------

	// A result without any time step (an OpenFOAM mesh with no fields) must survive a snapshot: encode and decode with an
	// empty step list, and its saved view must not index into it.
	void testSnapshotWithoutSteps()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		writePrismCase(tmp.path());
		QDir(tmp.path() + QStringLiteral("/0")).removeRecursively();
		QDir(tmp.path() + QStringLiteral("/1")).removeRecursively();
		const LoadedSimulationResult r = loadSimulationResult(tmp.path() + QStringLiteral("/case.foam"));
		CHECK(r.ok() && r.dataset->stepCount() == 0 && r.dataset->fields.empty());
		if (!r.ok())
			return;
		for (int policy = 0; policy < 2; ++policy)
		{
			SnapshotOptions options;
			options.content = policy == 0 ? SnapshotOptions::Content::AllFields : SnapshotOptions::Content::ShownAndDisplacement;
			options.shownField = -1;
			SimulationViewState state = defaultViewState(*r.dataset);
			state.step = 3; // a stale step index must not matter
			ResultSnapshot snap;
			DecodedSnapshot dec;
			QString err;
			CHECK(roundTrip(r, state, options, snap, dec, &err));
			CHECK(dec.dataset && dec.dataset->stepCount() == 0 && dec.dataset->fields.empty() && dec.dataset->validate().isEmpty());
			CHECK(dec.state.step == 0 && dec.state.fieldIndex == -1);
			const SnapshotSize estimate = estimateSnapshotSize(*r.dataset, r.surface, options);
			CHECK(estimate.rawBytes > 0 && estimate.storedBytes <= estimate.rawBytes);
		}
	}

	// A field an analysis only writes from a later step on is a real field: it has data, it is listed, and it can be the default.
	void testFieldsStartingAfterStepZero()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		const QString dir = tmp.path();
		writePrismCase(dir);
		QFile::remove(dir + QStringLiteral("/0/p"));
		QFile::remove(dir + QStringLiteral("/1/p"));
		writeText(dir + QStringLiteral("/0/a"), foamHeader("volScalarField", "a") + "dimensions [0 0 0 0 0 0 0];\ninternalField uniform 1;\n");
		writeText(dir + QStringLiteral("/1/a"), foamHeader("volScalarField", "a") + "dimensions [0 0 0 0 0 0 0];\ninternalField uniform 2;\n");
		writeText(dir + QStringLiteral("/1/b"), foamHeader("volScalarField", "b") + "dimensions [0 0 0 0 0 0 0];\ninternalField uniform 9;\n"); // starts at step 1
		ResultReadOutcome r = readResultFile(dir + QStringLiteral("/case.foam"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		ResultDataset& ds = *r.dataset;
		const int a = fieldIndexOf(ds, QStringLiteral("a")), b = fieldIndexOf(ds, QStringLiteral("b"));
		CHECK(ds.stepCount() == 2 && a >= 0 && b >= 0);
		if (a < 0 || b < 0)
			return;
		const ResultField& fb = ds.fields[static_cast<std::size_t>(b)];
		CHECK(fb.stepData[0].empty() && !fb.stepData[1].empty());
		CHECK(resultFieldHasData(fb) && resultFieldFirstStep(fb) == 1 && resultFieldFirstStep(ds.fields[static_cast<std::size_t>(a)]) == 0);
		ResultField none;
		none.stepData.assign(2, std::vector<float>());
		CHECK(!resultFieldHasData(none) && resultFieldFirstStep(none) == -1);
		// with no other field, the default is the late one, at the step where it has data
		ds.fields[static_cast<std::size_t>(a)].stepData.assign(2, std::vector<float>());
		DisplayScalar chosen;
		CHECK(chooseDefaultDisplayScalar(ds, chosen) && chosen.fieldIndex == b && chosen.step == 1 && chosen.maxValue == 9.0f);
		const SimulationViewState state = defaultViewState(ds);
		CHECK(state.fieldIndex == b && state.step == 1);
	}

	// A CELL tensor named like a stress gets derived cell fields, exactly like a node tensor gets derived node fields.
	void testDerivedStressOnCells()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		writePrismCase(tmp.path());
		ResultReadOutcome r = readResultFile(tmp.path() + QStringLiteral("/case.foam"));
		CHECK(r.ok());
		if (!r.ok())
			return;
		ResultDataset& ds = *r.dataset;
		ResultField tensor;
		tensor.name = QStringLiteral("elementStress");
		tensor.association = ResultFieldAssociation::Cell;
		tensor.components = 6;
		tensor.stepData.assign(ds.stepCount(), std::vector<float>());
		tensor.stepData[0] = { 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // uniaxial 100: von Mises 100, principals 100/0/0
		ds.fields.push_back(tensor);
		addDerivedStressFields(ds);
		const ResultField* mises = ds.findField(QStringLiteral("elementStress von Mises"), ResultFieldAssociation::Cell);
		const ResultField* maxP = ds.findField(QStringLiteral("elementStress max principal"), ResultFieldAssociation::Cell);
		CHECK(mises != nullptr && maxP != nullptr);
		if (mises && maxP)
		{
			CHECK(mises->components == 1 && mises->stepData[0].size() == 1 && approx(mises->stepData[0][0], 100.0) && mises->stepData[1].empty());
			CHECK(approx(maxP->stepData[0][0], 100.0) && mises->derivedFromField >= 0);
		}
		CHECK(ds.findField(QStringLiteral("elementStress von Mises"), ResultFieldAssociation::Node) == nullptr); // not a node field
		CHECK(ds.validate().isEmpty());
		const std::size_t before = ds.fields.size();
		addDerivedStressFields(ds); // not added twice
		CHECK(ds.fields.size() == before);
	}

	// validate() is the contract every consumer relies on: steps, component names and stored ranges must line up.
	void testValidateFieldShape()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		writePrismCase(tmp.path());
		ResultReadOutcome r = readResultFile(tmp.path() + QStringLiteral("/case.foam"));
		CHECK(r.ok() && !r.dataset->fields.empty());
		if (!r.ok() || r.dataset->fields.empty())
			return;
		ResultDataset& ds = *r.dataset;
		CHECK(ds.validate().isEmpty());
		ResultField& f = ds.fields[0];

		f.stepData.push_back(std::vector<float>()); // one data slot more than there are steps
		CHECK(!ds.validate().isEmpty());
		f.stepData.pop_back();
		CHECK(ds.validate().isEmpty());

		f.componentNames = { QStringLiteral("only one") }; // names for 1 component are fine on a scalar, 2 are not
		CHECK(ds.validate().isEmpty());
		f.componentNames = { QStringLiteral("a"), QStringLiteral("b") };
		CHECK(!ds.validate().isEmpty());
		f.componentNames.clear();

		f.storedRange = { 0.0f, 1.0f }; // one step, one selector: 2 numbers per step
		CHECK(ds.validate().isEmpty() == (ds.stepCount() == 1));
		f.storedRange.assign(ds.stepCount() * static_cast<std::size_t>(resultRangeSelectorCount(f.components)) * 2, 0.0f);
		CHECK(ds.validate().isEmpty());
		f.storedRange.push_back(1.0f);
		CHECK(!ds.validate().isEmpty());
		f.storedRange.clear();
		CHECK(ds.validate().isEmpty());
	}

#if MV_HAVE_CGNS
	// Steps must follow the solutions' own ordering, not the order the file happens to list them: three Vertex solutions whose
	// values are their number, with the step pointers naming them in step order (or, without pointers, natural name order).
	bool writeCgnsOrderFixture(const char* path, bool pointers)
	{
		int fn = 0, base = 0, zone = 0, index = 0;
		if (cg_open(path, CG_MODE_WRITE, &fn) != CG_OK)
			return false;
		bool ok = cg_base_write(fn, "Base", 3, 3, &base) == CG_OK;
		const cgsize_t size[3] = { 12, 2, 0 };
		ok = ok && cg_zone_write(fn, base, "Zone1", size, CGNS_ENUMV(Unstructured), &zone) == CG_OK;
		double x[12], y[12], z[12];
		for (int n = 0; n < 12; ++n)
		{
			x[n] = n % 3;
			y[n] = (n / 3) % 2;
			z[n] = n / 6;
		}
		const cgsize_t hexes[16] = { 1, 2, 5, 4, 7, 8, 11, 10, 2, 3, 6, 5, 8, 9, 12, 11 };
		ok = ok && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateX", x, &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateY", y, &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateZ", z, &index) == CG_OK
		     && cg_section_write(fn, base, zone, "Hexas", CGNS_ENUMV(HEXA_8), 1, 2, 0, hexes, &index) == CG_OK;
		// pointers: created out of order (B, A, C) and listed in step order (A, B, C) = values 1, 2, 3.
		// no pointers: created as Sol10, Sol2, Sol1 and expected in natural order Sol1, Sol2, Sol10 = values 1, 2, 10.
		const char* names[3] = { pointers ? "SolB" : "Sol10", pointers ? "SolA" : "Sol2", pointers ? "SolC" : "Sol1" };
		const double numbers[3] = { pointers ? 2.0 : 10.0, pointers ? 1.0 : 2.0, pointers ? 3.0 : 1.0 };
		for (int s = 0; s < 3; ++s)
		{
			double values[12];
			for (int n = 0; n < 12; ++n)
				values[n] = numbers[s] * 100.0 + n;
			int sol = 0, field = 0;
			ok = ok && cg_sol_write(fn, base, zone, names[s], CGNS_ENUMV(Vertex), &sol) == CG_OK
			     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Temperature", values, &field) == CG_OK;
		}
		if (pointers)
		{
			char text[32 * 3];
			std::memset(text, ' ', sizeof text);
			const char* order[3] = { "SolA", "SolB", "SolC" };
			for (int step = 0; step < 3; ++step)
				std::memcpy(text + 32 * step, order[step], 4);
			const cgsize_t dims[2] = { 32, 3 };
			ok = ok && cg_ziter_write(fn, base, zone, "ZoneIterativeData") == CG_OK
			     && cg_goto(fn, base, "Zone_t", zone, "ZoneIterativeData_t", 1, "end") == CG_OK
			     && cg_array_write("FlowSolutionPointers", CGNS_ENUMV(Character), 2, dims, text) == CG_OK;
		}
		return cg_close(fn) == CG_OK && ok;
	}
#endif

#if MV_HAVE_CGNS
	// A zone with two Vertex solutions ("S0", "S1") whose fields are the given names; the value of the k-th field is k + 1 (+ 100 in
	// the second solution), so an assembled group can be checked component by component.
	bool writeCgnsNamedFields(const char* path, const std::vector<std::string>& first, const std::vector<std::string>& second)
	{
		int fn = 0, base = 0, zone = 0, index = 0;
		if (cg_open(path, CG_MODE_WRITE, &fn) != CG_OK)
			return false;
		bool ok = cg_base_write(fn, "Base", 3, 3, &base) == CG_OK;
		const cgsize_t size[3] = { 12, 2, 0 };
		ok = ok && cg_zone_write(fn, base, "Zone1", size, CGNS_ENUMV(Unstructured), &zone) == CG_OK;
		double x[12], y[12], z[12];
		for (int n = 0; n < 12; ++n)
		{
			x[n] = n % 3;
			y[n] = (n / 3) % 2;
			z[n] = n / 6;
		}
		const cgsize_t hexes[16] = { 1, 2, 5, 4, 7, 8, 11, 10, 2, 3, 6, 5, 8, 9, 12, 11 };
		ok = ok && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateX", x, &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateY", y, &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateZ", z, &index) == CG_OK
		     && cg_section_write(fn, base, zone, "Hexas", CGNS_ENUMV(HEXA_8), 1, 2, 0, hexes, &index) == CG_OK;
		const std::vector<std::string>* solutions[2] = { &first, &second };
		for (int s = 0; s < 2; ++s)
		{
			int sol = 0, field = 0;
			ok = ok && cg_sol_write(fn, base, zone, s == 0 ? "S0" : "S1", CGNS_ENUMV(Vertex), &sol) == CG_OK;
			for (std::size_t k = 0; k < solutions[s]->size(); ++k)
			{
				double values[12];
				for (int n = 0; n < 12; ++n)
					values[n] = static_cast<double>(k + 1) + (s == 1 ? 100.0 : 0.0);
				ok = ok && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), (*solutions[s])[k].c_str(), values, &field) == CG_OK;
			}
		}
		return cg_close(fn) == CG_OK && ok;
	}
#endif

#if MV_HAVE_CGNS
	// A structured zone: 3 x 3 x 2 points (2 x 2 x 1 hexahedra) in a 3-D base, or 3 x 3 points (2 x 2 quads) in a 2-D one.
	// Temperature at the points = the point number, Quality per cell = 100 + the cell number.
	bool writeCgnsStructured(const char* path, bool threeD)
	{
		int fn = 0, base = 0, zone = 0, index = 0;
		if (cg_open(path, CG_MODE_WRITE, &fn) != CG_OK)
			return false;
		bool ok = cg_base_write(fn, "Base", threeD ? 3 : 2, 3, &base) == CG_OK;
		const int nk = threeD ? 2 : 1;
		const cgsize_t size[9] = { 3, 3, 2, 2, 2, 1, 0, 0, 0 };
		// 3-D: { NI, NJ, NK, cells I, J, K, 0, 0, 0 }; 2-D: { NI, NJ, cells I, J, 0, 0 }
		const cgsize_t size2d[6] = { 3, 3, 2, 2, 0, 0 };
		ok = ok && cg_zone_write(fn, base, "Block", threeD ? size : size2d, CGNS_ENUMV(Structured), &zone) == CG_OK;
		std::vector<double> x, y, z, temperature;
		for (int k = 0; k < nk; ++k)
			for (int j = 0; j < 3; ++j)
				for (int i = 0; i < 3; ++i)
				{
					x.push_back(i);
					y.push_back(j);
					z.push_back(k);
					temperature.push_back(static_cast<double>(x.size() - 1));
				}
		std::vector<double> quality;
		for (int c = 0; c < 4; ++c)
			quality.push_back(100.0 + c);
		int sol = 0, field = 0;
		ok = ok && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateX", x.data(), &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateY", y.data(), &index) == CG_OK
		     && cg_coord_write(fn, base, zone, CGNS_ENUMV(RealDouble), "CoordinateZ", z.data(), &index) == CG_OK
		     && cg_sol_write(fn, base, zone, "Points", CGNS_ENUMV(Vertex), &sol) == CG_OK
		     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Temperature", temperature.data(), &field) == CG_OK
		     && cg_sol_write(fn, base, zone, "Cells", CGNS_ENUMV(CellCenter), &sol) == CG_OK
		     && cg_field_write(fn, base, zone, sol, CGNS_ENUMV(RealDouble), "Quality", quality.data(), &field) == CG_OK;
		return cg_close(fn) == CG_OK && ok;
	}
#endif

	void testCgnsStructured()
	{
#if MV_HAVE_CGNS
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;

		// ---- a 3-D block: hexahedra, i fastest
		const QString path3d = tmp.path() + QStringLiteral("/block3d.cgns");
		CHECK(writeCgnsStructured(QFile::encodeName(path3d).constData(), true));
		const ResultReadOutcome r3 = readResultFile(path3d);
		if (!r3.ok())
			std::printf("  CGNS structured failed: %s\n", qPrintable(r3.error));
		CHECK(r3.ok());
		if (r3.ok())
		{
			const ResultDataset& ds = *r3.dataset;
			CHECK(ds.nodeCount() == 18 && ds.cellCount() == 4);
			bool allHex = true;
			for (ResultCellType t : ds.cellTypes)
				allHex = allHex && t == ResultCellType::Hexahedron;
			CHECK(allHex);
			// cell 0 sits at the origin corner; cell 1 is the next along i, cell 2 the next along j
			const std::vector<std::uint32_t> firstHex = { 0, 1, 4, 3, 9, 10, 13, 12 };
			CHECK(std::vector<std::uint32_t>(ds.cellConnectivity.begin(), ds.cellConnectivity.begin() + 8) == firstHex);
			CHECK(ds.cellConnectivity[8] == 1 && ds.cellConnectivity[16] == 3);
			const ResultField* temperature = ds.findField(QStringLiteral("Temperature"), ResultFieldAssociation::Node);
			const ResultField* quality = ds.findField(QStringLiteral("Quality"), ResultFieldAssociation::Cell);
			CHECK(temperature && quality && temperature->tupleCount(0) == 18 && quality->tupleCount(0) == 4);
			if (temperature && quality)
				CHECK(approx(temperature->stepData[0][13], 13.0) && approx(quality->stepData[0][2], 102.0));
			CHECK(ds.validate().isEmpty());
			// the block's outer faces: 2 x (4 + 2 + 2) quads = 32 triangles
			const ResultBoundarySurface surface = extract(ds);
			CHECK(surface.triangleCount() == 32);
		}

		// ---- a 2-D block: quads
		const QString path2d = tmp.path() + QStringLiteral("/block2d.cgns");
		CHECK(writeCgnsStructured(QFile::encodeName(path2d).constData(), false));
		const ResultReadOutcome r2 = readResultFile(path2d);
		if (!r2.ok())
			std::printf("  CGNS structured 2-D failed: %s\n", qPrintable(r2.error));
		CHECK(r2.ok());
		if (r2.ok())
		{
			const ResultDataset& ds = *r2.dataset;
			CHECK(ds.nodeCount() == 9 && ds.cellCount() == 4);
			bool allQuad = true;
			for (ResultCellType t : ds.cellTypes)
				allQuad = allQuad && t == ResultCellType::Quad;
			CHECK(allQuad);
			const std::vector<std::uint32_t> firstQuad = { 0, 1, 4, 3 };
			CHECK(std::vector<std::uint32_t>(ds.cellConnectivity.begin(), ds.cellConnectivity.begin() + 4) == firstQuad);
			CHECK(ds.findField(QStringLiteral("Quality"), ResultFieldAssociation::Cell) != nullptr);
			CHECK(ds.validate().isEmpty());
		}

		// ---- the two-block sample: zones are concatenated, every step and field comes with them
		const QString pathDuct = tmp.path() + QStringLiteral("/duct.cgns");
		CHECK(writeCgnsStructuredSample(QFile::encodeName(pathDuct).constData()));
		const ResultReadOutcome rd = readResultFile(pathDuct);
		if (!rd.ok())
			std::printf("  CGNS duct failed: %s\n", qPrintable(rd.error));
		CHECK(rd.ok());
		if (rd.ok())
		{
			const ResultDataset& ds = *rd.dataset;
			CHECK(ds.nodeCount() == 2u * 13u * 7u * 5u && ds.cellCount() == 2u * 12u * 6u * 4u && ds.stepCount() == 4);
			const ResultField* velocity = ds.findField(QStringLiteral("Velocity"), ResultFieldAssociation::Node);
			const ResultField* quality = ds.findField(QStringLiteral("Quality"), ResultFieldAssociation::Cell);
			CHECK(velocity && velocity->components == 3 && quality && quality->tupleCount(0) == ds.cellCount());
			CHECK(ds.validate().isEmpty());
			const ResultBoundarySurface surface = extract(ds);
			CHECK(surface.triangleCount() > 0);
		}
#else
		std::printf("  (skipping CGNS structured tests: this build has no CGNS library)\n");
#endif
	}

	void testCgnsComponentGroups()
	{
#if MV_HAVE_CGNS
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		auto read = [&](const char* name, const std::vector<std::string>& a, const std::vector<std::string>& b) {
			const QString path = tmp.path() + QStringLiteral("/") + QString::fromLatin1(name);
			const bool written = writeCgnsNamedFields(QFile::encodeName(path).constData(), a, b);
			CHECK(written);
			ResultReadOutcome r = readResultFile(path);
			CHECK(r.ok());
			return r;
		};

		// lower-case component names form a tensor too (matched case-insensitively)
		const std::vector<std::string> lower = { "Sigmaxx", "Sigmayy", "Sigmazz", "Sigmaxy", "Sigmayz", "Sigmaxz" };
		const ResultReadOutcome a = read("lower.cgns", lower, lower);
		if (a.ok())
		{
			const int sigma = fieldIndexOf(*a.dataset, QStringLiteral("Sigma"));
			CHECK(sigma >= 0 && a.dataset->fields.size() == 1 && a.dataset->fields[static_cast<std::size_t>(sigma)].components == 6);
			if (sigma >= 0)
			{
				const std::vector<float>& d = a.dataset->fields[static_cast<std::size_t>(sigma)].stepData[0];
				CHECK(d.size() == 12u * 6u && d[0] == 1.0f && d[1] == 2.0f && d[2] == 3.0f && d[3] == 4.0f && d[4] == 5.0f && d[5] == 6.0f);
			}
		}

		// both XZ and ZX: the tensor takes XZ, and ZX stays a field of its own - nothing is dropped
		const std::vector<std::string> both = { "SigmaXX", "SigmaYY", "SigmaZZ", "SigmaXY", "SigmaYZ", "SigmaXZ", "SigmaZX" };
		const ResultReadOutcome b = read("both.cgns", both, both);
		if (b.ok())
		{
			const int sigma = fieldIndexOf(*b.dataset, QStringLiteral("Sigma")), leftover = fieldIndexOf(*b.dataset, QStringLiteral("SigmaZX"));
			CHECK(sigma >= 0 && leftover >= 0 && b.dataset->fields.size() == 2);
			if (sigma >= 0 && leftover >= 0)
			{
				CHECK(b.dataset->fields[static_cast<std::size_t>(sigma)].stepData[0][5] == 6.0f);   // XZ fills the ZX slot
				CHECK(b.dataset->fields[static_cast<std::size_t>(leftover)].stepData[0][0] == 7.0f); // the extra component is still there
			}
		}

		// all nine components: one full (non-symmetric) tensor in the stored order
		const std::vector<std::string> nine = { "AXX", "AXY", "AXZ", "AYX", "AYY", "AYZ", "AZX", "AZY", "AZZ" };
		const ResultReadOutcome c = read("nine.cgns", nine, nine);
		if (c.ok())
		{
			const int t = fieldIndexOf(*c.dataset, QStringLiteral("A"));
			CHECK(t >= 0 && c.dataset->fields.size() == 1 && c.dataset->fields[static_cast<std::size_t>(t)].components == 9);
			if (t >= 0)
			{
				const std::vector<float>& d = c.dataset->fields[static_cast<std::size_t>(t)].stepData[0];
				bool inOrder = d.size() == 12u * 9u;
				for (int k = 0; inOrder && k < 9; ++k)
					inOrder = d[static_cast<std::size_t>(k)] == static_cast<float>(k + 1);
				CHECK(inOrder);
			}
		}

		// a step with only some of a tensor's components is left empty and reported, never filled with zeros or NaN
		const std::vector<std::string> full = { "SigmaXX", "SigmaYY", "SigmaZZ", "SigmaXY", "SigmaYZ", "SigmaXZ" };
		const std::vector<std::string> missing = { "SigmaXX", "SigmaYY", "SigmaZZ", "SigmaXY", "SigmaYZ" }; // no XZ in the second solution
		const ResultReadOutcome d = read("partial.cgns", full, missing);
		if (d.ok())
		{
			const int sigma = fieldIndexOf(*d.dataset, QStringLiteral("Sigma"));
			CHECK(sigma >= 0 && d.dataset->stepCount() == 2);
			if (sigma >= 0 && d.dataset->stepCount() == 2)
			{
				const ResultField& f = d.dataset->fields[static_cast<std::size_t>(sigma)];
				CHECK(!f.stepData[0].empty() && f.stepData[1].empty());
			}
			bool warned = false;
			for (const QString& w : d.warnings)
				warned = warned || w.contains(QStringLiteral("some of their components"));
			CHECK(warned);
		}

		// vectors, lower-case axes included; an X without a Y is just a scalar
		const std::vector<std::string> vec = { "Vx", "Vy", "Vz", "Alonex" };
		const ResultReadOutcome e = read("vec.cgns", vec, vec);
		if (e.ok())
		{
			const int v = fieldIndexOf(*e.dataset, QStringLiteral("V")), lonely = fieldIndexOf(*e.dataset, QStringLiteral("Alonex"));
			CHECK(v >= 0 && lonely >= 0 && e.dataset->fields[static_cast<std::size_t>(v)].components == 3 && e.dataset->fields.size() == 2);
		}

		// all-lower-case vector names ("velocityx" ends in an axis PAIR, 'y' + 'x') are still a vector
		const std::vector<std::string> lowerVec = { "velocityx", "velocityy", "velocityz" };
		const ResultReadOutcome f = read("lowervec.cgns", lowerVec, lowerVec);
		if (f.ok())
		{
			const int v = fieldIndexOf(*f.dataset, QStringLiteral("velocity"));
			CHECK(v >= 0 && f.dataset->fields.size() == 1 && f.dataset->fields[static_cast<std::size_t>(v)].components == 3);
		}

		// off-diagonals stored as XY/YZ/XZ in one solution and YX/ZY/ZX in the next still merge into one tensor over both steps
		const std::vector<std::string> upperTri = { "SigmaXX", "SigmaYY", "SigmaZZ", "SigmaXY", "SigmaYZ", "SigmaXZ" };
		const std::vector<std::string> lowerTri = { "SigmaXX", "SigmaYY", "SigmaZZ", "SigmaYX", "SigmaZY", "SigmaZX" };
		const ResultReadOutcome g = read("alias.cgns", upperTri, lowerTri);
		if (g.ok())
		{
			const int sigma = fieldIndexOf(*g.dataset, QStringLiteral("Sigma"));
			CHECK(sigma >= 0 && g.dataset->fields.size() == 1 && g.dataset->stepCount() == 2);
			if (sigma >= 0 && g.dataset->stepCount() == 2)
			{
				const ResultField& t = g.dataset->fields[static_cast<std::size_t>(sigma)];
				CHECK(t.components == 6 && !t.stepData[0].empty() && !t.stepData[1].empty());
			}
		}
#else
		std::printf("  (skipping CGNS component-group tests: this build has no CGNS library)\n");
#endif
	}

	void testGlyphs()
	{
		// ---- site selection: about `target` evenly spread points, ascending, never a non-finite one
		std::vector<float> grid;
		for (int y = 0; y < 10; ++y)
			for (int x = 0; x < 10; ++x)
				grid.insert(grid.end(), { static_cast<float>(x), static_cast<float>(y), 0.0f });
		const std::vector<std::uint32_t> few = selectGlyphSites(grid, 25);
		CHECK(few.size() >= 12 && few.size() <= 40);
		CHECK(std::is_sorted(few.begin(), few.end()) && std::adjacent_find(few.begin(), few.end()) == few.end());
		CHECK(selectGlyphSites(grid, 1000).size() == 100); // no more points than asked for: all of them
		CHECK(selectGlyphSites(grid, 0).empty());
		grid[5 * 3] = std::numeric_limits<float>::quiet_NaN();
		const std::vector<std::uint32_t> withGap = selectGlyphSites(grid, 1000);
		CHECK(withGap.size() == 99 && std::find(withGap.begin(), withGap.end(), 5u) == withGap.end());
		const std::vector<float> same = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
		CHECK(selectGlyphSites(same, 2).size() == 1); // coincident points: one arrow

		// ---- arrows of a vector field
		ResultReadOutcome r = readBytes(buildVtu(singleTet(), Enc::Ascii));
		CHECK(r.ok());
		if (!r.ok())
			return;
		ResultDataset& ds = *r.dataset;
		ResultField displacement;
		displacement.name = QStringLiteral("Displacement");
		displacement.components = 3;
		displacement.stepData = { std::vector<float>(12, 1.0f) };
		ds.fields.push_back(displacement);
		ResultField velocity;
		velocity.name = QStringLiteral("Velocity");
		velocity.components = 3;
		// node 0: 1 along x, node 1: 2 along y, node 2: zero (no arrow), node 3: 3 along x
		velocity.stepData = { std::vector<float>{ 1, 0, 0, 0, 2, 0, 0, 0, 0, 3, 0, 0 } };
		ds.fields.push_back(velocity);
		const int velocityIndex = static_cast<int>(ds.fields.size()) - 1;
		CHECK(chooseDefaultGlyphField(ds) == velocityIndex); // velocity comes before displacement
		CHECK(isGlyphField(ds.fields[static_cast<std::size_t>(velocityIndex)]));

		const ResultBoundarySurface surface = extract(ds);
		double extentX = 0.0, extentY = 0.0, extentZ = 0.0;
		CHECK(surfaceExtents(surface, extentX, extentY, extentZ) && approx(extentX, 1.0) && approx(extentY, 1.0) && approx(extentZ, 1.0));
		CHECK(!surfaceExtents(ResultBoundarySurface(), extentX, extentY, extentZ));
		const std::vector<std::uint32_t> sites = selectSurfaceGlyphSites(surface, false, 100);
		CHECK(sites.size() == surface.vertexCount());
		GlyphOptions options;
		GlyphSet set;
		CHECK(buildGlyphSet(ds, surface, velocityIndex, 0, sites, 10.0, options, 0.0f, set));
		CHECK(set.count() == 3 && set.anchors.size() == 9 && set.vectors.size() == 9); // the zero vector gets no arrow
		CHECK(approx(set.fieldMax, 3.0) && approx(set.fieldMin, 0.0));
		bool lengthsRight = true, sawLongest = false;
		for (std::size_t i = 0; i < set.count(); ++i)
		{
			const double length = std::sqrt(static_cast<double>(set.vectors[i * 3]) * set.vectors[i * 3]
			                                + static_cast<double>(set.vectors[i * 3 + 1]) * set.vectors[i * 3 + 1]
			                                + static_cast<double>(set.vectors[i * 3 + 2]) * set.vectors[i * 3 + 2]);
			// the largest magnitude (3) is 5 % of the diagonal (10) = 0.5 long; the others in proportion
			lengthsRight = lengthsRight && approx(length, 0.5 * set.values[i] / 3.0, 1e-4, 1e-6);
			if (approx(set.values[i], 3.0))
			{
				sawLongest = true;
				lengthsRight = lengthsRight && set.vectors[i * 3] > 0.0f && approx(set.vectors[i * 3 + 1], 0.0, 1e-6, 1e-9); // along +x
			}
		}
		CHECK(lengthsRight && sawLongest);

		// a fixed reference (the largest over all steps) shortens the arrows of a smaller step; uniform length ignores it
		CHECK(buildGlyphSet(ds, surface, velocityIndex, 0, sites, 10.0, options, 6.0f, set));
		double longest = 0.0;
		for (std::size_t i = 0; i < set.count(); ++i)
			longest = std::max(longest, static_cast<double>(std::fabs(set.vectors[i * 3])));
		CHECK(approx(longest, 0.25, 1e-4, 1e-6)); // 3 of 6 = half of 0.5
		options.scaleByMagnitude = false;
		CHECK(buildGlyphSet(ds, surface, velocityIndex, 0, sites, 10.0, options, 0.0f, set));
		bool allEqual = set.count() == 3;
		for (std::size_t i = 0; i < set.count(); ++i)
		{
			const double length = std::sqrt(static_cast<double>(set.vectors[i * 3]) * set.vectors[i * 3]
			                                + static_cast<double>(set.vectors[i * 3 + 1]) * set.vectors[i * 3 + 1]
			                                + static_cast<double>(set.vectors[i * 3 + 2]) * set.vectors[i * 3 + 2]);
			allEqual = allEqual && approx(length, 0.5, 1e-4, 1e-6);
		}
		CHECK(allEqual);

		// nothing to draw: a scalar field, a missing step, no size
		CHECK(!buildGlyphSet(ds, surface, fieldIndexOf(ds, QStringLiteral("T")), 0, sites, 10.0, options, 0.0f, set));
		CHECK(!buildGlyphSet(ds, surface, velocityIndex, 5, sites, 10.0, options, 0.0f, set));
		CHECK(!buildGlyphSet(ds, surface, velocityIndex, 0, sites, 0.0, options, 0.0f, set));
	}

	void testCellVectorDefault()
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		writePrismCase(tmp.path());
		QFile::remove(tmp.path() + QStringLiteral("/0/p"));
		QFile::remove(tmp.path() + QStringLiteral("/1/p"));
		writeText(tmp.path() + QStringLiteral("/0/U"), foamHeader("volVectorField", "U") + "dimensions [0 1 -1 0 0 0 0];\ninternalField uniform (3 4 0);\n");
		const ResultReadOutcome r = readResultFile(tmp.path() + QStringLiteral("/case.foam"));
		CHECK(r.ok() && r.dataset->fields.size() == 1);
		if (!r.ok() || r.dataset->fields.empty())
			return;
		DisplayScalar d;
		// a result whose only field is a cell VECTOR still starts on a field: its magnitude (5)
		CHECK(chooseDefaultDisplayScalar(*r.dataset, d) && d.cellData && d.component == -1 && d.minValue == 5.0f && d.maxValue == 5.0f);
		CHECK(defaultViewState(*r.dataset).fieldIndex == 0);
	}

	void testCgnsStepOrder()
	{
#if MV_HAVE_CGNS
		QTemporaryDir tmp;
		CHECK(tmp.isValid());
		if (!tmp.isValid())
			return;
		for (int pointers = 0; pointers < 2; ++pointers)
		{
			const QString path = tmp.path() + (pointers ? QStringLiteral("/pointers.cgns") : QStringLiteral("/natural.cgns"));
			CHECK(writeCgnsOrderFixture(QFile::encodeName(path).constData(), pointers != 0));
			const ResultReadOutcome r = readResultFile(path);
			CHECK(r.ok());
			if (!r.ok())
				continue;
			const int t = fieldIndexOf(*r.dataset, QStringLiteral("Temperature"));
			CHECK(r.dataset->stepCount() == 3 && t >= 0);
			if (t < 0 || r.dataset->stepCount() != 3)
				continue;
			const ResultField& f = r.dataset->fields[static_cast<std::size_t>(t)];
			const double expected[3] = { 100.0, pointers ? 200.0 : 200.0, pointers ? 300.0 : 1000.0 }; // node 0 of each step
			for (std::size_t step = 0; step < 3; ++step)
				CHECK(f.stepData[step].size() == 12 && approx(f.stepData[step][0], expected[step]));
			// without pointers the order is a guess and the user is told; with pointers it is explicit and nothing is said
			bool guessWarning = false;
			for (const QString& w : r.warnings)
				guessWarning = guessWarning || w.contains(QStringLiteral("FlowSolutionPointers"));
			CHECK(guessWarning == (pointers == 0));
		}
#else
		std::printf("  (skipping CGNS step-order tests: this build has no CGNS library)\n");
#endif
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
#if MV_HAVE_NETCDF
	// result_tests --write-exodus-sample <file.exo>: writes the larger Exodus file used to try the reader in the application.
	if (argc == 3 && std::strcmp(argv[1], "--write-exodus-sample") == 0)
	{
		const bool ok = writeExodusBlockSample(argv[2]);
		std::printf(ok ? "wrote %s\n" : "could not write %s\n", argv[2]);
		return ok ? 0 : 1;
	}
#endif
#if MV_HAVE_CGNS
	// result_tests --write-cgns-sample <file.cgns>: writes the larger CGNS file used to try the reader in the application.
	if (argc == 3 && std::strcmp(argv[1], "--write-cgns-sample") == 0)
	{
		const bool ok = writeCgnsBlockSample(argv[2]);
		std::printf(ok ? "wrote %s\n" : "could not write %s\n", argv[2]);
		return ok ? 0 : 1;
	}
	// result_tests --write-cgns-structured-sample <file.cgns>: a two-block structured duct.
	if (argc == 3 && std::strcmp(argv[1], "--write-cgns-structured-sample") == 0)
	{
		const bool ok = writeCgnsStructuredSample(argv[2]);
		std::printf(ok ? "wrote %s\n" : "could not write %s\n", argv[2]);
		return ok ? 0 : 1;
	}
#endif
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
	testThermalTransient();
	testSnapshotCodec();
	testSnapshotRoundTrip();
	testSnapshotCompression();
	testSnapshotSteps();
	testCellData();
	testOpenFoamPolyhedral();
	testOpenFoamErrors();
	testOpenFoamSample();
	testComparePanes();
	testExodus();
	testCgns();
	testSnapshotWithoutSteps();
	testFieldsStartingAfterStepZero();
	testDerivedStressOnCells();
	testValidateFieldShape();
	testCgnsStepOrder();
	testGlyphs();
	testCellVectorDefault();
	testCgnsComponentGroups();
	testCgnsStructured();
	testLoadSimulationResult();
	testShellAndSkippedCells();
	testErrors();
	testCancellation();
	testLargeMeshPartitioning();

	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
