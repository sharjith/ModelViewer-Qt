// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "Inventor/Assembly.h"

#include "Inventor/CompoundFile.h"
#include "SegmentData.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <gp_XYZ.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Inventor {
namespace {

using namespace Detail;

// What the assembly saves, in three typed segments (the same table layout as the part's graphics) and one stream:
//   AmRxSegment       the occurrence tree: one placement node per occurrence and the table that binds occurrence numbers
//                     to the referenced files;
//   AmDcSegment       the components: occurrence number, browser name, and whether the component is suppressed;
//   DesignViewSegment the occurrences hidden in the saved design view;
//   UFRxDoc           the saved pathnames of the assembly and of every referenced file.
struct AssemblySegment {
    QByteArray data;
    std::vector<GraphicsNode> nodes;
};

std::optional<AssemblySegment> readAssemblySegment(CompoundFile& file, QStringView name)
{
    QByteArray label(4 + 2 * name.size(), Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(name.size()), label.data());
    for (qsizetype i = 0; i < name.size(); ++i)
        qToLittleEndian<quint16>(name[i].unicode(), label.data() + 4 + 2 * i);
    const auto index = findSegmentMetadata(file, label);
    if (!index)
        return std::nullopt;
    AssemblySegment segment;
    segment.data = readPartner(file, *index, GraphicsDataLimit);
    segment.nodes = readGraphicsNodes(file.readStream(*index, MetadataLimit), segment.data);
    return segment;
}

[[noreturn]] void unsupportedAssembly(const char* reason)
{
    throw std::runtime_error(std::string("Unsupported Inventor assembly: ") + reason);
}

quint32 word(QByteArrayView data, qsizetype offset)
{
    GraphicsCursor cursor(data);
    cursor.skip(offset);
    return cursor.read<quint32>();
}

const GraphicsNode& requireNode(const AssemblySegment& table, quint32 ref, quint32 type)
{
    if (ref == 0 || ref >= table.nodes.size() || table.nodes[ref].type != type)
        unsupportedAssembly("inconsistent occurrence reference");
    return table.nodes[ref];
}

std::vector<quint32> assemblyList(QByteArrayView data, qsizetype offset, bool references)
{
    GraphicsCursor cursor(data);
    cursor.skip(offset);
    if (cursor.read<quint32>() != ReferenceList)
        unsupportedAssembly("unrecognized occurrence list");
    const auto count = cursor.read<quint32>();
    if (count == 0)
        return {};
    if (count > 50000)
        unsupportedAssembly("too many occurrence records");
    cursor.skip(8);
    std::vector<quint32> list;
    list.reserve(count);
    for (quint32 i = 0; i < count; ++i)
        list.push_back(references ? cursor.ref() : cursor.read<quint32>());
    return list;
}

QStringList assemblyReferencePaths(CompoundFile& file, QString* savedAssemblyPath)
{
    for (std::size_t index = 0; index < file.streams().size(); ++index) {
        if (file.streams()[index].path != QStringList{QStringLiteral("UFRxDoc")})
            continue;
        const auto bytes = file.readStream(index, 2 * 1024 * 1024);
        QStringList paths;
        for (qsizetype offset = 0; offset + 4 <= bytes.size(); ++offset) {
            const auto count = qFromLittleEndian<quint32>(bytes.constData() + offset);
            if (count < 5 || count > 1000 || 2 * static_cast<qsizetype>(count) > bytes.size() - offset - 4)
                continue;
            GraphicsCursor cursor(QByteArrayView(bytes).sliced(offset));
            QString name = cursor.string16();
            if (!name.endsWith(QStringLiteral(".ipt"), Qt::CaseInsensitive) &&
                !name.endsWith(QStringLiteral(".iam"), Qt::CaseInsensitive))
                continue;
            if (std::any_of(name.begin(), name.end(), [](QChar c) { return c.unicode() < 32; }))
                continue;
            offset += 3 + 2 * count;
            // Some assemblies (a frame generated from a skeleton) list a component again as its bare file name right after its
            // full pathname; that entry is the same reference, not another component.
            if (!paths.isEmpty() && !name.contains(QLatin1Char('\\')) && !name.contains(QLatin1Char('/')) &&
                (paths.back().endsWith(QLatin1Char('\\') + name, Qt::CaseInsensitive) ||
                 paths.back().endsWith(QLatin1Char('/') + name, Qt::CaseInsensitive)))
                continue;
            paths.append(std::move(name));
        }
        if (paths.empty() || !paths.front().endsWith(QStringLiteral(".iam"), Qt::CaseInsensitive))
            unsupportedAssembly("missing document reference table");
        if (savedAssemblyPath)
            *savedAssemblyPath = paths.front();
        paths.removeFirst(); // The assembly's saved pathname precedes its component references.
        return paths;
    }
    unsupportedAssembly("missing UFRxDoc stream");
}

gp_Trsf occurrencePlacement(QByteArrayView bytes)
{
    constexpr quint32 affineMask = 0x70008000;
    const auto primaryMask = word(bytes, 10);
    const auto offset = (primaryMask & affineMask) == affineMask ? 10 : 14;
    GraphicsCursor cursor(bytes);
    cursor.skip(offset);
    const auto flags = cursor.read<quint32>();
    if ((flags & affineMask) != affineMask)
        unsupportedAssembly("unrecognized occurrence transform");
    // A 4x4 matrix stored sparsely: per element a flag for "is 1 or -1" and one for "is stored as zero / negative".
    std::array<double, 16> matrix{};
    for (int i = 0; i < 16; ++i) {
        const bool unit = (flags & (1u << i)) != 0;
        const bool storedZero = (flags & (1u << (16 + i))) != 0;
        if (unit) {
            matrix[i] = storedZero ? -1.0 : 1.0;
        } else if (!storedZero) {
            const auto bits = cursor.read<quint64>();
            std::memcpy(&matrix[i], &bits, sizeof(bits));
        }
        if (!std::isfinite(matrix[i]))
            unsupportedAssembly("non-finite occurrence transform");
    }
    if (matrix[12] != 0 || matrix[13] != 0 || matrix[14] != 0 || matrix[15] != 1)
        unsupportedAssembly("non-affine occurrence transform");
    const gp_XYZ x(matrix[0], matrix[4], matrix[8]);
    const gp_XYZ y(matrix[1], matrix[5], matrix[9]);
    const gp_XYZ z(matrix[2], matrix[6], matrix[10]);
    constexpr double tolerance = 1e-8;
    if (std::abs(x.SquareModulus() - 1) > tolerance || std::abs(y.SquareModulus() - 1) > tolerance ||
        std::abs(z.SquareModulus() - 1) > tolerance || std::abs(x.Dot(y)) > tolerance ||
        std::abs(y.Dot(z)) > tolerance || std::abs(z.Dot(x)) > tolerance ||
        std::abs(x.DotCross(y, z) - 1) > tolerance)
        unsupportedAssembly("placement is outside the supported rigid rotation profile");
    constexpr double millimetersPerCentimeter = 10.0; // the file stores centimetres
    gp_Trsf result;
    result.SetValues(matrix[0], matrix[1], matrix[2], matrix[3] * millimetersPerCentimeter,
                     matrix[4], matrix[5], matrix[6], matrix[7] * millimetersPerCentimeter,
                     matrix[8], matrix[9], matrix[10], matrix[11] * millimetersPerCentimeter);
    return result;
}

// The file name a row of the component table carries: after the row's reference list, a UTF-16 string stored with its
// character count ("Base", "Karet Penutup"). Empty when the row does not have the expected layout.
QString partsRowName(QByteArrayView data)
{
    try {
        GraphicsCursor cursor(data);
        if (cursor.read<quint32>() != ReferenceList)
            return {};
        const auto count = cursor.read<quint32>();
        if (count > 50000)
            return {};
        cursor.skip(8);
        cursor.skip(static_cast<qsizetype>(count) * 4);
        return cursor.string16();
    } catch (const std::runtime_error&) {
        return {};
    }
}

// The file name of a saved pathname without its folders and extension ("...\\Part Kran\\Base.ipt" gives "Base").
QString fileStem(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString file = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    const qsizetype dot = file.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? file.left(dot) : file;
}

// Which saved pathname belongs to each row of the component table. A row takes the first unused pathname whose file name
// (without the extension) equals the row's name, ignoring case; the rows that no name decides take the pathnames that are
// left, in order.
std::vector<qsizetype> bindRowsToPaths(const std::vector<QString>& names, const QStringList& paths)
{
    const auto stemOf = [](const QString& path) { return fileStem(path); };
    std::vector<qsizetype> result(names.size(), -1);
    std::vector<bool> used(static_cast<std::size_t>(paths.size()), false);
    for (std::size_t row = 0; row < names.size(); ++row) {
        if (names[row].isEmpty())
            continue;
        for (qsizetype path = 0; path < paths.size(); ++path) {
            if (!used[static_cast<std::size_t>(path)] && stemOf(paths[path]).compare(names[row], Qt::CaseInsensitive) == 0) {
                result[row] = path;
                used[static_cast<std::size_t>(path)] = true;
                break;
            }
        }
    }
    qsizetype next = 0;
    for (std::size_t row = 0; row < names.size(); ++row) {
        if (result[row] >= 0)
            continue;
        while (next < paths.size() && used[static_cast<std::size_t>(next)])
            ++next;
        if (next >= paths.size())
            unsupportedAssembly("component table has no matching pathname");
        result[row] = next;
        used[static_cast<std::size_t>(next)] = true;
    }
    return result;
}

// The browser name of an occurrence ("Part:1"): a UTF-16 string stored with its character count, followed by a 16-byte trailer
// that ends the record. The header before it is one 32-bit word shorter in some Inventor versions (the count is at offset 42
// in one file and at 38 in another), so the string is found from the end: the one position where a plausible count and its
// characters exactly fill the record up to the trailer.
QString browserName(QByteArrayView record)
{
    constexpr qsizetype trailer = 16;
    std::optional<qsizetype> found;
    for (qsizetype offset = 30; offset <= 60 && offset + 4 <= record.size(); offset += 2) {
        const auto count = qFromLittleEndian<quint32>(record.constData() + offset);
        if (count == 0 || count > 1024 || offset + 4 + 2 * static_cast<qsizetype>(count) + trailer != record.size())
            continue;
        if (found)
            unsupportedAssembly("ambiguous component name record");
        found = offset;
    }
    if (!found) {
        // A component saved without an explicit name: the character count is zero and nothing but the trailer follows. Inventor
        // names such an occurrence from its file and its instance number; the caller does the same.
        constexpr qsizetype emptyTail = 4 + trailer;
        if (record.size() >= emptyTail + 30 &&
            std::all_of(record.end() - emptyTail, record.end(), [](char c) { return c == 0; }))
            return QString();
        unsupportedAssembly("unrecognized component name record");
    }
    GraphicsCursor cursor(record);
    cursor.skip(*found);
    return cursor.string16();
}

std::unordered_map<quint32, AssemblyOccurrence> assemblyNames(const AssemblySegment& dc)
{
    constexpr quint32 Component = 0x90874D60, Occurrence = 0x90874D61, Bounds = 0xCB072B3B;
    constexpr quint32 BrowserName = 0x2B48A42B;
    const QByteArray marker("D\0C\0x\0", 6);
    std::unordered_map<quint32, AssemblyOccurrence> result;
    for (const auto& node : dc.nodes) {
        if (node.type != Component)
            continue;
        const auto at = node.data.lastIndexOf(marker);
        if (at < 18 || word(node.data, at - 4) != 3)
            unsupportedAssembly("unrecognized component record");
        const auto number = word(node.data, at - 8);
        const auto occurrence = requireNode(dc, word(node.data, at - 18) & 0x7fffffffu, Occurrence).data;
        const auto bounds = requireNode(dc, word(node.data, at - 14) & 0x7fffffffu, Bounds).data;
        const auto browser = requireNode(dc, word(occurrence, 6) & 0x7fffffffu, BrowserName).data;
        AssemblyOccurrence item;
        item.name = browserName(browser);
        if (bounds.size() <= 0x46 || !result.emplace(number, item).second)
            unsupportedAssembly("inconsistent component name or number");
        result.at(number).suppressed = bounds[0x46] == 0;
    }
    return result;
}

} // namespace

std::vector<AssemblyOccurrence> readAssemblyOccurrences(const QString& path, const std::function<bool()>& cancelled,
                                                        QString* savedPath)
{
    const auto check = [&] {
        if (cancelled && cancelled())
            throw std::runtime_error("Inventor import cancelled");
    };
    check();
    CompoundFile file(path);
    const auto rx = readAssemblySegment(file, u"AmRxSegment");
    const auto dc = readAssemblySegment(file, u"AmDcSegment");
    const auto view = readAssemblySegment(file, u"DesignViewSegment");
    if (!rx || !dc)
        unsupportedAssembly("missing occurrence segments");
    auto occurrences = assemblyNames(*dc);
    const auto paths = assemblyReferencePaths(file, savedPath);
    constexpr quint32 Root = 0xEC60C64B, PartsRow = 0x08B9DB46, RowMember = 0xA20D9DC5;
    constexpr quint32 RxOccurrence = 0x232792BC, RxComponent = 0xFDCD32FE, Hidden = 0x71C22321;
    const GraphicsNode* root = nullptr;
    // The component table has one row per referenced file: the occurrences of that file and the file's name. The saved
    // pathnames come in an order of their own (in one assembly the last three rows and pathnames are in different orders), so
    // a row is bound to the pathname whose file name equals the row's name; only a row that no name decides is bound by
    // position among the pathnames left.
    struct Row {
        QString name;
        std::vector<quint32> numbers;
    };
    std::vector<Row> rows;
    for (const auto& node : rx->nodes) {
        check();
        if (node.type == Root) {
            if (root)
                unsupportedAssembly("multiple occurrence roots");
            root = &node;
        } else if (node.type == PartsRow) {
            Row row;
            row.name = partsRowName(node.data);
            for (const auto member : assemblyList(node.data, 0, true)) {
                const auto entry = requireNode(*rx, member, RowMember);
                for (const auto number : assemblyList(entry.data, 0, false))
                    row.numbers.push_back(number);
            }
            rows.push_back(std::move(row));
        }
    }
    // An assembly without any component file (an empty template, or a weldment template whose only occurrence is the
    // weldment's own feature record) has nothing to place.
    if (root && occurrences.empty() && rows.empty() && paths.isEmpty())
        return {};
    if (!root || rows.size() != static_cast<std::size_t>(paths.size()))
        unsupportedAssembly(QStringLiteral("occurrence root and file references could not be associated (%1 table rows, %2 saved paths%3)")
                                .arg(rows.size()).arg(paths.size()).arg(root ? QString() : QStringLiteral(", no root")).toUtf8().constData());
    std::vector<QString> rowNames;
    rowNames.reserve(rows.size());
    for (const auto& row : rows)
        rowNames.push_back(row.name);
    const std::vector<qsizetype> pathOfRow = bindRowsToPaths(rowNames, paths);
    std::unordered_set<quint32> assigned;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (const auto number : rows[r].numbers) {
            const auto found = occurrences.find(number);
            if (found == occurrences.end() || !assigned.insert(number).second)
                unsupportedAssembly("ambiguous occurrence file binding");
            found->second.reference = paths[pathOfRow[r]];
        }
    }
    // An occurrence saved without a browser name is named by Inventor from its file and an instance number ("Base:1"), counted
    // per file in the order the occurrences were created (their saved numbers).
    {
        std::vector<quint32> numbers;
        numbers.reserve(occurrences.size());
        for (const auto& entry : occurrences)
            numbers.push_back(entry.first);
        std::sort(numbers.begin(), numbers.end());
        std::map<QString, int> instances;
        for (const quint32 number : numbers) {
            auto& occurrence = occurrences.at(number);
            if (occurrence.reference.isEmpty())
                continue;
            const QString stem = fileStem(occurrence.reference);
            const int instance = ++instances[stem.toLower()];
            if (occurrence.name.isEmpty())
                occurrence.name = QStringLiteral("%1:%2").arg(stem).arg(instance);
        }
    }

    if (view) {
        for (const auto& node : view->nodes) {
            if (node.type != Hidden)
                continue;
            const auto count = word(node.data, 0x0a);
            GraphicsCursor members(node.data);
            members.skip(0x16);
            if (count > static_cast<quint32>(members.rest().size() / 4))
                unsupportedAssembly("invalid saved visibility list");
            for (quint32 i = 0; i < count; ++i) {
                const auto found = occurrences.find(members.read<quint32>());
                if (found != occurrences.end())
                    found->second.visible = false;
            }
        }
    }

    std::vector<AssemblyOccurrence> result;
    std::unordered_set<quint32> placed;
    for (const auto id : assemblyList(root->data, 6, true)) {
        check();
        const auto placement = requireNode(*rx, id, RxOccurrence).data;
        const auto component = requireNode(*rx, word(placement, 6) & 0x7fffffffu, RxComponent).data;
        const auto number = word(component, 8);
        auto found = occurrences.find(number);
        if (found == occurrences.end() || !placed.insert(number).second || found->second.reference.isEmpty())
            unsupportedAssembly("missing or duplicated occurrence binding");
        if ((word(placement, placement.size() - 16) & 4) != 0)
            unsupportedAssembly("flexible occurrence placement");
        found->second.placement = occurrencePlacement(placement);
        result.push_back(found->second);
    }
    if (result.empty() || result.size() != occurrences.size())
        unsupportedAssembly("the saved occurrence list is incomplete");
    return result;
}

ComponentFile resolveAssemblyComponent(const QString& assembly, const QString& savedAssemblyPath, const QString& reference)
{
    QString native = reference;
    native.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (native.contains(QLatin1Char('*')))
        return {QString(), QStringLiteral("embedded component references are not supported")};
    const QDir directory = QFileInfo(assembly).absoluteDir();
    const auto found = [](const QString& candidate) {
        const QFileInfo info(candidate);
        return info.isFile() ? info.canonicalFilePath() : QString();
    };

    // 1. As given: a path that exists on this machine, or one relative to the assembly.
    if (QString file = found(QFileInfo(native).isAbsolute() ? native : directory.absoluteFilePath(native)); !file.isEmpty())
        return {file, QString()};

    // 2. Relative to the folder the assembly was saved in, applied to the folder it is in now.
    if (!savedAssemblyPath.isEmpty()) {
        QString savedFolder = savedAssemblyPath;
        savedFolder.replace(QLatin1Char('\\'), QLatin1Char('/'));
        savedFolder.truncate(savedFolder.lastIndexOf(QLatin1Char('/')));
        const QString relative = QDir(savedFolder).relativeFilePath(native);
        if (!QFileInfo(relative).isAbsolute()) {
            if (QString file = found(directory.absoluteFilePath(relative)); !file.isEmpty())
                return {file, QString()};
        }
    }

    // 3. The last components of the saved path under the assembly's folder and up to two folders above it (a project that was
    //    moved as a whole, or a shared folder next to the assembly's).
    const QStringList parts = native.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype keep = std::min<qsizetype>(parts.size(), 3); keep >= 1; --keep) {
        const QString tail = parts.mid(parts.size() - keep).join(QLatin1Char('/'));
        QDir base = directory;
        for (int up = 0; up <= 2; ++up) {
            if (QString file = found(base.absoluteFilePath(tail)); !file.isEmpty())
                return {file, QString()};
            if (!base.cdUp())
                break;
        }
    }
    return {QString(), QStringLiteral("the component file \"%1\" was not found near the assembly").arg(QFileInfo(native).fileName())};
}

} // namespace Inventor
