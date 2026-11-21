// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Regression tests of the native Inventor importer (parts .ipt, assemblies .iam).
//
//  1. Synthetic tests: need no sample. Segment decoding (zlib, Zstandard, limits, truncation), the bounds checks of the graphics
//     cursor, the compound-file container and the readers on malformed input (they must throw std::runtime_error, never crash).
//  2. Corpus tests: compare what the readers make of local sample files with the expectations in corpus_manifest.tsv. The
//     manifest is tracked and holds neutral IDs, SHA-256 hashes and expected values only. The samples and the map from ID to
//     local path (corpus_map.tsv) stay under the ignored out/ folder or outside the repository. A sample that is missing, or
//     whose hash differs, is skipped and reported; the corpus tests never fail for lack of samples.
//
// Usage:
//   inventor_tests [--manifest <file>] [--map <file>] [--max-mb <n>] [--only <id>]
//   inventor_tests --generate <samples-root> <manifest-out> <map-out> [--max-mb <n>]
//       scans the samples, keeps the IDs of hashes already in the manifest, gives new ones the next free ID, recomputes every
//       expectation and writes the manifest and the map. Review the manifest diff before committing it.
//
// Environment: MV_INVENTOR_CORPUS_MAP overrides the default map path.
#include <zlib.h>
#include <zstd.h>

#include "Inventor/Assembly.h"
#include "Inventor/CompoundFile.h"
#include "Inventor/PartSolid.h"
#include "SegmentData.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <vector>

#ifndef MV_REPO_DIR
#define MV_REPO_DIR "."
#endif

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

// The expression must throw std::runtime_error (the readers' error type for unsupported or malformed input).
#define CHECK_THROWS(expr)                                                                       \
    do {                                                                                         \
        ++g_checks;                                                                              \
        bool thrown = false;                                                                     \
        try {                                                                                    \
            (void)(expr);                                                                        \
        } catch (const std::runtime_error&) {                                                    \
            thrown = true;                                                                       \
        } catch (...) {                                                                          \
        }                                                                                        \
        if (!thrown) {                                                                           \
            std::fprintf(stderr, "FAIL %s:%d  did not throw std::runtime_error: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

// --------------------------------------------------------------------------------------------------------------------
// Synthetic tests
// --------------------------------------------------------------------------------------------------------------------

// A segment data stream: an 18-byte envelope (byte 16 = 4, byte 17 = packing: 1 zlib, 2 Zstandard), then the packed data.
QByteArray envelope(char packing, const QByteArray& packed)
{
    QByteArray bytes(18, '\0');
    bytes[16] = 4;
    bytes[17] = packing;
    bytes.append(packed);
    return bytes;
}

QByteArray zlibPack(const QByteArray& data)
{
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    QByteArray out(static_cast<qsizetype>(size), '\0');
    compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(data.constData()),
              static_cast<uLong>(data.size()), 6);
    out.resize(static_cast<qsizetype>(size));
    return out;
}

QByteArray zstdPack(const QByteArray& data, bool recordSize)
{
    QByteArray out(static_cast<qsizetype>(ZSTD_compressBound(static_cast<size_t>(data.size()))), '\0');
    ZSTD_CCtx* context = ZSTD_createCCtx();
    if (!recordSize)
        ZSTD_CCtx_setPledgedSrcSize(context, ZSTD_CONTENTSIZE_UNKNOWN); // the frame then has no content size
    ZSTD_inBuffer input{data.constData(), static_cast<size_t>(data.size()), 0};
    ZSTD_outBuffer output{out.data(), static_cast<size_t>(out.size()), 0};
    ZSTD_compressStream2(context, &output, &input, ZSTD_e_end);
    ZSTD_freeCCtx(context);
    out.resize(static_cast<qsizetype>(output.pos));
    return out;
}

QByteArray sampleData(int size)
{
    QByteArray data;
    data.reserve(size);
    quint32 state = 12345;
    while (data.size() < size) {
        state = state * 1664525u + 1013904223u;
        data.append(static_cast<char>((state >> 24) % 7 + 'a')); // compressible, deterministic
    }
    return data;
}

void testSegmentDecoding()
{
    using namespace Inventor::Detail;
    const QByteArray data = sampleData(100000);
    constexpr qsizetype limit = 1 << 20;

    CHECK(decodeSegment(envelope(1, zlibPack(data)), limit) == data);
    CHECK(decodeSegment(envelope(2, zstdPack(data, true)), limit) == data);
    CHECK(decodeSegment(envelope(2, zstdPack(data, false)), limit) == data); // no content size recorded in the frame
    CHECK(decodeSegment(envelope(1, zlibPack(QByteArray())), limit).isEmpty());

    // A result over the limit is refused, not truncated.
    CHECK_THROWS(decodeSegment(envelope(1, zlibPack(data)), 1000));
    CHECK_THROWS(decodeSegment(envelope(2, zstdPack(data, true)), 1000));
    CHECK_THROWS(decodeSegment(envelope(2, zstdPack(data, false)), 1000));

    // Truncated and damaged frames.
    const QByteArray packedZlib = zlibPack(data);
    CHECK_THROWS(decodeSegment(envelope(1, packedZlib.left(packedZlib.size() / 2)), limit));
    const QByteArray packedZstd = zstdPack(data, true);
    CHECK_THROWS(decodeSegment(envelope(2, packedZstd.left(packedZstd.size() / 2)), limit));
    const QByteArray packedZstdStream = zstdPack(data, false);
    CHECK_THROWS(decodeSegment(envelope(2, packedZstdStream.left(packedZstdStream.size() / 2)), limit));
    CHECK_THROWS(decodeSegment(envelope(1, QByteArray(40, '\x7f')), limit));

    // The envelope itself.
    CHECK_THROWS(decodeSegment(QByteArray(), limit));
    CHECK_THROWS(decodeSegment(QByteArray(18, '\0'), limit)); // no data after the header
    QByteArray wrongVersion = envelope(1, packedZlib);
    wrongVersion[16] = 3;
    CHECK_THROWS(decodeSegment(wrongVersion, limit));
    CHECK_THROWS(decodeSegment(envelope(3, packedZlib), limit)); // unknown packing
}

void testGraphicsCursor()
{
    using namespace Inventor::Detail;
    const QByteArray bytes("\x01\x00\x00\x00\x02\x00", 6);
    GraphicsCursor cursor{QByteArrayView(bytes)};
    CHECK(cursor.read<quint32>() == 1u);
    CHECK(cursor.read<quint16>() == 2u);
    CHECK_THROWS(cursor.read<quint8>());   // past the end
    GraphicsCursor skipper{QByteArrayView(bytes)};
    CHECK_THROWS(skipper.skip(7));
    CHECK_THROWS(skipper.skip(-1));
    GraphicsCursor empty{QByteArrayView()};
    CHECK_THROWS(empty.read<quint32>());
}

QString writeTemp(const QTemporaryDir& dir, const QString& name, const QByteArray& bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(bytes);
    return path;
}

void testMalformedFiles()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QByteArray oleMagic("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    const std::vector<std::pair<QString, QByteArray>> bad = {
        {QStringLiteral("empty.ipt"), QByteArray()},
        {QStringLiteral("text.ipt"), QByteArray("this is not an Inventor file\n")},
        {QStringLiteral("magic_only.iam"), oleMagic},
        {QStringLiteral("zeros.iam"), QByteArray(4096, '\0')},
        {QStringLiteral("magic_zeros.ipt"), oleMagic + QByteArray(4096, '\0')},
        {QStringLiteral("magic_ff.ipt"), oleMagic + QByteArray(4096, '\xff')},
    };
    for (const auto& [name, bytes] : bad) {
        const QString path = writeTemp(dir, name, bytes);
        CHECK_THROWS(Inventor::CompoundFile{path});
        CHECK_THROWS(Inventor::readPartBodies(path, {}, nullptr, false));
        CHECK_THROWS(Inventor::readPartBodies(path, {}, nullptr, true));
        CHECK_THROWS(Inventor::readAssemblyOccurrences(path));
    }
    CHECK_THROWS(Inventor::CompoundFile(dir.filePath(QStringLiteral("missing.ipt"))));
    CHECK_THROWS(Inventor::readAssemblyOccurrences(dir.filePath(QStringLiteral("missing.iam"))));

    // Cancellation is reported, not treated as a bad file.
    CHECK_THROWS(Inventor::readPartBodies(writeTemp(dir, QStringLiteral("c.ipt"), oleMagic), [] { return true; }, nullptr, false));
}

// A component reference that names no file reports a reason instead of guessing.
void testResolveComponent()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    QDir(dir.path()).mkpath(QStringLiteral("Sub"));
    writeTemp(dir, QStringLiteral("Part A.ipt"), QByteArray("x"));
    writeTemp(dir, QStringLiteral("Sub/Part B.ipt"), QByteArray("x"));
    const QString assembly = dir.filePath(QStringLiteral("Main.iam"));
    const QString saved = QStringLiteral("C:\\Users\\someone\\Work\\Project\\Main.iam");

    const auto a = Inventor::resolveAssemblyComponent(assembly, saved, QStringLiteral("C:\\Users\\someone\\Work\\Project\\Part A.ipt"));
    CHECK(QFileInfo(a.path).fileName() == QLatin1String("Part A.ipt"));
    const auto b = Inventor::resolveAssemblyComponent(assembly, saved, QStringLiteral("C:\\Users\\someone\\Work\\Project\\Sub\\Part B.ipt"));
    CHECK(QFileInfo(b.path).fileName() == QLatin1String("Part B.ipt"));
    const auto missing = Inventor::resolveAssemblyComponent(assembly, saved, QStringLiteral("C:\\Users\\someone\\Work\\Project\\Nothing.ipt"));
    CHECK(missing.path.isEmpty() && !missing.problem.isEmpty());
    const auto embedded = Inventor::resolveAssemblyComponent(assembly, saved, QStringLiteral("C:\\x\\*\\Part A.ipt"));
    CHECK(embedded.path.isEmpty() && !embedded.problem.isEmpty());
}

// --------------------------------------------------------------------------------------------------------------------
// Corpus tests
// --------------------------------------------------------------------------------------------------------------------

using Expectation = QMap<QString, QString>;

struct Entry {
    QString id;
    QString kind; // "ipt" or "iam"
    QString sha256;
    qint64 size = 0;
    Expectation expect;
};

QString joinExpectation(const Expectation& e)
{
    QStringList parts;
    for (auto it = e.cbegin(); it != e.cend(); ++it)
        parts << it.key() + QLatin1Char('=') + it.value();
    return parts.join(QLatin1Char(';'));
}

Expectation parseExpectation(const QString& text)
{
    Expectation e;
    for (const QString& part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype eq = part.indexOf(QLatin1Char('='));
        if (eq > 0)
            e.insert(part.left(eq), part.mid(eq + 1));
    }
    return e;
}

QString shaOf(const QString& path, qint64* size = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    if (size)
        *size = file.size();
    return QString::fromLatin1(hash.result().toHex());
}

QString fmt(double value)
{
    return QString::number(value, 'g', 10);
}

// What the part reader makes of a file, the way the viewer calls it (not strict).
Expectation measurePart(const QString& path)
{
    Expectation e;
    try {
        Inventor::PartAppearance appearance;
        const auto outcomes = Inventor::readPartBodies(path, {}, &appearance, false);
        int solids = 0, surfaces = 0, incomplete = 0, failed = 0;
        qint64 faces = 0;
        double volume = 0;
        for (const auto& outcome : outcomes) {
            TopoDS_Shape shape;
            if (!outcome.solid.IsNull()) {
                ++solids;
                shape = outcome.solid;
                GProp_GProps properties;
                BRepGProp::VolumeProperties(shape, properties);
                volume += properties.Mass();
            } else if (!outcome.incompleteShape.IsNull()) {
                (outcome.isSurface ? surfaces : incomplete) += 1;
                shape = outcome.incompleteShape;
            } else {
                ++failed;
            }
            if (!shape.IsNull()) {
                NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> map;
                TopExp::MapShapes(shape, TopAbs_FACE, map);
                faces += map.Extent();
            }
        }
        e.insert(QStringLiteral("solids"), QString::number(solids));
        e.insert(QStringLiteral("surfaces"), QString::number(surfaces));
        e.insert(QStringLiteral("incomplete"), QString::number(incomplete));
        e.insert(QStringLiteral("failed"), QString::number(failed));
        e.insert(QStringLiteral("faces"), QString::number(faces));
        e.insert(QStringLiteral("volume"), fmt(volume));
        e.insert(QStringLiteral("overrides"), QString::number(appearance.faces.size()));
        e.insert(QStringLiteral("colour"), appearance.body
            ? QStringLiteral("%1,%2,%3").arg(appearance.body->Red(), 0, 'f', 3).arg(appearance.body->Green(), 0, 'f', 3)
                  .arg(appearance.body->Blue(), 0, 'f', 3)
            : QStringLiteral("none"));
    } catch (const std::exception& error) {
        e.insert(QStringLiteral("error"), QString::fromUtf8(error.what()).left(160).replace(QLatin1Char(';'), QLatin1Char(',')).replace(QLatin1Char('='), QLatin1Char(':')));
    }
    return e;
}

// The direct occurrences of an assembly: counts, and a checksum of the placements.
Expectation measureAssembly(const QString& path)
{
    Expectation e;
    try {
        QString saved;
        const auto occurrences = Inventor::readAssemblyOccurrences(path, {}, &saved);
        int subassemblies = 0, suppressed = 0, hidden = 0, unresolved = 0;
        double checksum = 0;
        for (const auto& occurrence : occurrences) {
            if (occurrence.reference.endsWith(QStringLiteral(".iam"), Qt::CaseInsensitive))
                ++subassemblies;
            suppressed += occurrence.suppressed ? 1 : 0;
            hidden += occurrence.visible ? 0 : 1;
            if (Inventor::resolveAssemblyComponent(path, saved, occurrence.reference).path.isEmpty())
                ++unresolved;
            for (int row = 1; row <= 3; ++row)
                for (int column = 1; column <= 4; ++column)
                    checksum += (row * 4 + column) * occurrence.placement.Value(row, column);
        }
        e.insert(QStringLiteral("occurrences"), QString::number(occurrences.size()));
        e.insert(QStringLiteral("subassemblies"), QString::number(subassemblies));
        e.insert(QStringLiteral("suppressed"), QString::number(suppressed));
        e.insert(QStringLiteral("hidden"), QString::number(hidden));
        e.insert(QStringLiteral("unresolved"), QString::number(unresolved));
        e.insert(QStringLiteral("placements"), fmt(checksum));
    } catch (const std::exception& error) {
        e.insert(QStringLiteral("error"), QString::fromUtf8(error.what()).left(160).replace(QLatin1Char(';'), QLatin1Char(',')).replace(QLatin1Char('='), QLatin1Char(':')));
    }
    return e;
}

Expectation measure(const QString& kind, const QString& path)
{
    return kind == QLatin1String("iam") ? measureAssembly(path) : measurePart(path);
}

bool numericKey(const QString& key)
{
    return key == QLatin1String("volume") || key == QLatin1String("placements");
}

// Differences between an expectation and what was measured; empty when they agree.
QStringList differences(const Expectation& want, const Expectation& got)
{
    QStringList out;
    for (auto it = want.cbegin(); it != want.cend(); ++it) {
        const QString have = got.value(it.key(), QStringLiteral("<missing>"));
        bool same = have == it.value();
        if (!same && numericKey(it.key())) {
            const double a = it.value().toDouble(), b = have.toDouble();
            same = std::abs(a - b) <= 1e-4 + 1e-6 * std::max(std::abs(a), std::abs(b));
        }
        if (!same)
            out << QStringLiteral("%1: expected %2, got %3").arg(it.key(), it.value(), have);
    }
    for (auto it = got.cbegin(); it != got.cend(); ++it)
        if (!want.contains(it.key()))
            out << QStringLiteral("%1: not expected, got %2").arg(it.key(), it.value());
    return out;
}

QList<Entry> readManifest(const QString& path)
{
    QList<Entry> entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entries;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList cells = line.split(QLatin1Char('\t'));
        if (cells.size() < 5)
            continue;
        Entry entry;
        entry.id = cells[0];
        entry.kind = cells[1];
        entry.sha256 = cells[2];
        entry.size = cells[3].toLongLong();
        entry.expect = parseExpectation(cells[4]);
        entries.append(entry);
    }
    return entries;
}

QMap<QString, QString> readMap(const QString& path)
{
    QMap<QString, QString> map;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return map;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const qsizetype tab = line.indexOf(QLatin1Char('\t'));
        if (tab > 0 && !line.startsWith(QLatin1Char('#')))
            map.insert(line.left(tab), line.mid(tab + 1));
    }
    return map;
}

void testCorpus(const QString& manifestPath, const QString& mapPath, qint64 maxBytes, const QString& only)
{
    const QList<Entry> entries = readManifest(manifestPath);
    const QMap<QString, QString> map = readMap(mapPath);
    std::printf("corpus: %lld manifest entries, map %s (%lld paths)\n", static_cast<long long>(entries.size()),
                QFileInfo::exists(mapPath) ? "found" : "NOT FOUND (corpus tests skipped)", static_cast<long long>(map.size()));
    int ran = 0, skipped = 0, failedEntries = 0;
    for (const Entry& entry : entries) {
        if (!only.isEmpty() && entry.id != only)
            continue;
        const QString path = map.value(entry.id);
        if (path.isEmpty() || !QFileInfo::exists(path) || entry.size > maxBytes) {
            ++skipped;
            continue;
        }
        if (shaOf(path) != entry.sha256) {
            std::printf("SKIP %s: the file in the map is not the sample of the manifest (hash differs)\n", qPrintable(entry.id));
            ++skipped;
            continue;
        }
        ++ran;
        ++g_checks;
        const QStringList diff = differences(entry.expect, measure(entry.kind, path));
        if (diff.isEmpty())
            continue;
        ++g_failures;
        ++failedEntries;
        std::fprintf(stderr, "FAIL %s (%s)\n", qPrintable(entry.id), qPrintable(entry.kind));
        for (const QString& line : diff)
            std::fprintf(stderr, "    %s\n", qPrintable(line));
    }
    std::printf("corpus: %d compared, %d skipped (no file, hash differs, or over the size limit), %d differ\n", ran, skipped, failedEntries);
}

// Truncated copies of real samples must be refused with an error, never crash. The smallest few of each kind, cut at half and
// at one tenth of their size.
void testTruncatedSamples(const QString& manifestPath, const QString& mapPath, qint64 maxBytes)
{
    const QList<Entry> entries = readManifest(manifestPath);
    const QMap<QString, QString> map = readMap(mapPath);
    QTemporaryDir dir;
    for (const QString& kind : {QStringLiteral("ipt"), QStringLiteral("iam")}) {
        QList<Entry> mine;
        for (const Entry& entry : entries)
            if (entry.kind == kind && entry.size <= maxBytes && !map.value(entry.id).isEmpty() &&
                QFileInfo::exists(map.value(entry.id)) && !entry.expect.contains(QStringLiteral("error")))
                mine.append(entry);
        std::sort(mine.begin(), mine.end(), [](const Entry& a, const Entry& b) { return a.size < b.size; });
        for (int i = 0; i < std::min<qsizetype>(3, mine.size()); ++i) {
            QFile source(map.value(mine[i].id));
            if (!source.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bytes = source.readAll();
            for (const int divisor : {2, 10}) {
                const QString path = writeTemp(dir, QStringLiteral("cut_%1_%2.%3").arg(mine[i].id).arg(divisor).arg(kind),
                                               bytes.left(bytes.size() / divisor));
                if (kind == QLatin1String("iam")) {
                    CHECK_THROWS(Inventor::readAssemblyOccurrences(path));
                } else {
                    CHECK_THROWS(Inventor::readPartBodies(path, {}, nullptr, false));
                }
            }
        }
    }
}

int generate(const QString& root, const QString& manifestOut, const QString& mapOut, qint64 maxBytes)
{
    QMap<QString, Entry> byHash;
    int nextPart = 1, nextAssembly = 1;
    for (const Entry& entry : readManifest(manifestOut)) {
        byHash.insert(entry.sha256, entry);
        const int number = entry.id.mid(2).toInt();
        (entry.kind == QLatin1String("iam") ? nextAssembly : nextPart) = std::max(entry.kind == QLatin1String("iam") ? nextAssembly : nextPart, number + 1);
    }
    QStringList files;
    QDirIterator walk(root, {QStringLiteral("*.ipt"), QStringLiteral("*.iam")}, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext())
        files << walk.next();
    std::sort(files.begin(), files.end());

    QMap<QString, QString> idToPath;
    QSet<QString> seen;
    for (const QString& path : files) {
        qint64 size = 0;
        const QString sha = shaOf(path, &size);
        if (sha.isEmpty() || seen.contains(sha))
            continue;
        if (size > maxBytes) {
            std::printf("skipped (over the size limit): %s\n", qPrintable(QFileInfo(path).fileName()));
            continue;
        }
        seen.insert(sha);
        Entry entry = byHash.value(sha);
        const QString kind = path.endsWith(QStringLiteral(".iam"), Qt::CaseInsensitive) ? QStringLiteral("iam") : QStringLiteral("ipt");
        if (entry.id.isEmpty()) {
            entry.id = kind == QLatin1String("iam") ? QStringLiteral("IA%1").arg(nextAssembly++, 3, 10, QLatin1Char('0'))
                                                    : QStringLiteral("IP%1").arg(nextPart++, 3, 10, QLatin1Char('0'));
            entry.kind = kind;
            entry.sha256 = sha;
        }
        entry.size = size;
        std::printf("%s  %s\n", qPrintable(entry.id), qPrintable(QFileInfo(path).fileName()));
        entry.expect = measure(kind, path);
        byHash.insert(sha, entry);
        idToPath.insert(entry.id, QDir::toNativeSeparators(path));
    }

    QList<Entry> all = byHash.values();
    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
    QFile manifest(manifestOut);
    QFile map(mapOut);
    QDir().mkpath(QFileInfo(mapOut).absolutePath());
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text) || !map.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::fprintf(stderr, "cannot write the outputs\n");
        return 2;
    }
    QTextStream out(&manifest);
    out.setEncoding(QStringConverter::Utf8);
    out << "# Inventor importer corpus: neutral sample ID, kind, SHA-256 of the sample, size, expected result of the readers.\n"
           "# Tracked on purpose: no sample, name or path is recorded. Regenerate with `inventor_tests --generate` (see\n"
           "# inventor_tests.cpp) and review the diff. IP = part (.ipt), IA = assembly (.iam, direct occurrences only).\n";
    for (const Entry& entry : all)
        out << entry.id << '\t' << entry.kind << '\t' << entry.sha256 << '\t' << entry.size << '\t' << joinExpectation(entry.expect) << '\n';
    QTextStream mapOutStream(&map);
    mapOutStream.setEncoding(QStringConverter::Utf8);
    mapOutStream << "# Local only (never commit): sample ID, path of the sample on this machine.\n";
    for (auto it = idToPath.cbegin(); it != idToPath.cend(); ++it)
        mapOutStream << it.key() << '\t' << it.value() << '\n';
    std::printf("wrote %lld manifest entries (%lld paths in the map)\n", static_cast<long long>(all.size()), static_cast<long long>(idToPath.size()));
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QString manifest = QStringLiteral(MV_REPO_DIR "/tests/inventor/corpus_manifest.tsv");
    QString map = qEnvironmentVariable("MV_INVENTOR_CORPUS_MAP", QStringLiteral(MV_REPO_DIR "/out/inventor-tests/corpus_map.tsv"));
    qint64 maxBytes = qint64(30) * 1024 * 1024;
    QString only;
    for (qsizetype i = 0; i < args.size(); ++i) {
        const auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
        if (args[i] == QLatin1String("--manifest"))
            manifest = next();
        else if (args[i] == QLatin1String("--map"))
            map = next();
        else if (args[i] == QLatin1String("--max-mb"))
            maxBytes = next().toLongLong() * 1024 * 1024;
        else if (args[i] == QLatin1String("--only"))
            only = next();
        else if (args[i] == QLatin1String("--generate")) {
            const QString root = next(), manifestOut = next(), mapOut = next();
            if (root.isEmpty() || manifestOut.isEmpty() || mapOut.isEmpty()) {
                std::fprintf(stderr, "usage: inventor_tests --generate <samples-root> <manifest-out> <map-out> [--max-mb <n>]\n");
                return 2;
            }
            for (qsizetype j = i + 1; j + 1 < args.size(); ++j)
                if (args[j] == QLatin1String("--max-mb"))
                    maxBytes = args[j + 1].toLongLong() * 1024 * 1024;
            return generate(root, manifestOut, mapOut, maxBytes);
        }
    }

    if (only.isEmpty()) {
        testSegmentDecoding();
        testGraphicsCursor();
        testMalformedFiles();
        testResolveComponent();
        testTruncatedSamples(manifest, map, maxBytes);
    }
    testCorpus(manifest, map, maxBytes, only);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
