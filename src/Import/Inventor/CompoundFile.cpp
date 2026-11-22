// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "Inventor/CompoundFile.h"

#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Inventor {
namespace {

constexpr quint32 FreeSector = 0xffffffff;
constexpr quint32 EndOfChain = 0xfffffffe;
constexpr quint32 FatSector = 0xfffffffd;
constexpr quint32 DifatSector = 0xfffffffc;
constexpr quint32 SectorLimit = 0xfffffffa;
constexpr quint32 NoDirectoryEntry = 0xffffffff;
constexpr quint32 MiniSectorSize = 64;
constexpr quint32 MiniStreamCutoff = 4096;
constexpr qsizetype DirectoryEntrySize = 128;
constexpr std::size_t MaxDirectoryEntries = 1'000'000;
constexpr unsigned MaxStorageDepth = 128;

[[noreturn]] void invalid(const char* message)
{
    throw std::runtime_error(message);
}

quint64 sectorsFor(quint64 bytes, quint32 sectorSize)
{
    return bytes / sectorSize + (bytes % sectorSize != 0);
}

struct DirectoryEntry {
    QString name;
    quint8 type = 0;
    quint32 left = NoDirectoryEntry;
    quint32 right = NoDirectoryEntry;
    quint32 child = NoDirectoryEntry;
    quint32 firstSector = EndOfChain;
    quint64 size = 0;
};

} // namespace

CompoundFile::CompoundFile(const QString& path) : _file(path)
{
    if (!_file.open(QIODevice::ReadOnly))
        throw std::runtime_error(_file.errorString().toStdString());

    const QByteArray header = readAt(0, 512);
    const char* bytes = header.constData();
    if (qFromLittleEndian<quint64>(bytes) != 0xe11ab1a1e011cfd0ULL)
        invalid("Invalid compound file signature");

    _majorVersion = qFromLittleEndian<quint16>(bytes + 26);
    const quint16 sectorShift = qFromLittleEndian<quint16>(bytes + 30);
    const quint16 expectedShift = _majorVersion == 3 ? 9 : 12;
    if ((_majorVersion != 3 && _majorVersion != 4) || sectorShift != expectedShift ||
        qFromLittleEndian<quint16>(bytes + 28) != 0xfffe ||
        qFromLittleEndian<quint16>(bytes + 32) != 6 ||
        qFromLittleEndian<quint32>(bytes + 56) != MiniStreamCutoff)
        invalid("Unsupported compound file header");

    _sectorSize = 1u << sectorShift;
    const qint64 totalSectors = _file.size() / _sectorSize;
    if (totalSectors < 3 || totalSectors - 1 > SectorLimit || _file.size() % _sectorSize != 0)
        invalid("Invalid compound file length");
    _sectorCount = static_cast<quint32>(totalSectors - 1);
    _fat = readFat(header);

    const quint32 firstDirectory = qFromLittleEndian<quint32>(bytes + 48);
    const quint32 directoryCount = qFromLittleEndian<quint32>(bytes + 40);
    const std::size_t directoryLimit = std::min<std::size_t>(
        _sectorCount, MaxDirectoryEntries * DirectoryEntrySize / _sectorSize);
    const auto expectedDirectory = _majorVersion == 4
        ? std::optional<std::size_t>(directoryCount) : std::nullopt;
    readDirectory(sectorChain(firstDirectory, _fat, directoryLimit, expectedDirectory));

    const quint32 miniFatCount = qFromLittleEndian<quint32>(bytes + 64);
    if (miniFatCount != 0) {
        const quint32 firstMiniFat = qFromLittleEndian<quint32>(bytes + 60);
        const auto miniFatSectors = sectorChain(firstMiniFat, _fat, _sectorCount, miniFatCount);
        for (const quint32 sector : miniFatSectors) {
            const QByteArray block = readSector(sector);
            for (qsizetype offset = 0; offset < block.size(); offset += 4)
                _miniFat.push_back(qFromLittleEndian<quint32>(block.constData() + offset));
        }
    }
    if (_miniStreamSize != 0) {
        const quint64 count = sectorsFor(_miniStreamSize, _sectorSize);
        if (count > _sectorCount)
            invalid("Invalid compound file mini stream size");
        _miniStreamSectors = sectorChain(_firstMiniStreamSector, _fat, _sectorCount,
                                        static_cast<std::size_t>(count));
    }
}

QByteArray CompoundFile::readAt(qint64 offset, qsizetype size)
{
    if (!_file.seek(offset))
        invalid("Could not seek within compound file");
    QByteArray data = _file.read(size);
    if (data.size() != size)
        invalid("Truncated compound file");
    return data;
}

QByteArray CompoundFile::readSector(quint32 sector)
{
    if (sector >= _sectorCount)
        invalid("Compound file sector is out of range");
    return readAt((static_cast<qint64>(sector) + 1) * _sectorSize, _sectorSize);
}

std::vector<quint32> CompoundFile::readFat(const QByteArray& header)
{
    const char* bytes = header.constData();
    const quint32 fatCount = qFromLittleEndian<quint32>(bytes + 44);
    const quint32 difatCount = qFromLittleEndian<quint32>(bytes + 72);
    const quint32 entriesPerFat = _sectorSize / 4;
    if (fatCount == 0 || quint64(fatCount) + difatCount > _sectorCount ||
        quint64(fatCount) * entriesPerFat < _sectorCount)
        invalid("Invalid compound file allocation table size");

    QSet<quint32> fatSectors;
    QSet<quint32> difatSectors;
    std::vector<quint32> fat;
    fat.reserve(_sectorCount);
    auto appendFatSector = [&](quint32 sector) {
        if (sector == FreeSector)
            return;
        if (fatSectors.size() >= fatCount || fatSectors.contains(sector))
            invalid("Invalid compound file FAT sector list");
        fatSectors.insert(sector);
        const QByteArray block = readSector(sector);
        for (qsizetype offset = 0; offset < block.size() && fat.size() < _sectorCount; offset += 4)
            fat.push_back(qFromLittleEndian<quint32>(block.constData() + offset));
    };

    for (qsizetype offset = 76; offset < header.size(); offset += 4)
        appendFatSector(qFromLittleEndian<quint32>(bytes + offset));

    quint32 nextDifat = qFromLittleEndian<quint32>(bytes + 68);
    for (quint32 i = 0; i < difatCount; ++i) {
        if (difatSectors.contains(nextDifat))
            invalid("Cyclic compound file DIFAT chain");
        difatSectors.insert(nextDifat);
        const QByteArray block = readSector(nextDifat);
        const qsizetype linkOffset = block.size() - 4;
        for (qsizetype offset = 0; offset < linkOffset; offset += 4)
            appendFatSector(qFromLittleEndian<quint32>(block.constData() + offset));
        nextDifat = qFromLittleEndian<quint32>(block.constData() + linkOffset);
    }
    if (fatSectors.size() != fatCount || (difatCount != 0 && nextDifat != EndOfChain))
        invalid("Incomplete compound file allocation tables");

    for (const quint32 sector : fatSectors) {
        if (fat[sector] != FatSector)
            invalid("Compound file FAT sector has the wrong allocation marker");
    }
    for (const quint32 sector : difatSectors) {
        if (fat[sector] != DifatSector)
            invalid("Compound file DIFAT sector has the wrong allocation marker");
    }
    return fat;
}

std::vector<quint32> CompoundFile::sectorChain(quint32 first, const std::vector<quint32>& table,
                                                std::size_t sectorLimit,
                                                std::optional<std::size_t> expected) const
{
    const std::size_t limit = std::min(sectorLimit, table.size());
    if (expected && *expected > limit)
        invalid("Compound file chain is larger than its allocation table");

    std::vector<quint32> sectors;
    if (expected)
        sectors.reserve(*expected);
    for (quint32 id = first; id != EndOfChain; id = table[id]) {
        // A cycle cannot terminate within the declared or physical sector count.
        if (id >= table.size() || sectors.size() == expected.value_or(limit))
            invalid("Invalid or cyclic compound file sector chain");
        sectors.push_back(id);
    }
    if (expected && sectors.size() != *expected)
        invalid("Compound file sector chain ended early");
    return sectors;
}

void CompoundFile::readDirectory(const std::vector<quint32>& sectors)
{
    std::vector<DirectoryEntry> entries;
    entries.reserve(sectors.size() * (_sectorSize / DirectoryEntrySize));
    for (const quint32 sector : sectors) {
        const QByteArray block = readSector(sector);
        for (qsizetype offset = 0; offset < block.size(); offset += DirectoryEntrySize) {
            const char* data = block.constData() + offset;
            DirectoryEntry& entry = entries.emplace_back();
            entry.type = static_cast<quint8>(data[66]);
            if (entry.type == 0)
                continue;

            const quint16 nameBytes = qFromLittleEndian<quint16>(data + 64);
            if (nameBytes < 4 || nameBytes > 64 || nameBytes % 2 != 0 ||
                qFromLittleEndian<quint16>(data + nameBytes - 2) != 0)
                invalid("Invalid compound file directory entry name");
            for (quint16 i = 0; i + 2 < nameBytes; i += 2) {
                const quint16 codeUnit = qFromLittleEndian<quint16>(data + i);
                if (codeUnit == 0)
                    invalid("Invalid null character in compound file directory entry name");
                entry.name.append(QChar(codeUnit));
            }
            entry.left = qFromLittleEndian<quint32>(data + 68);
            entry.right = qFromLittleEndian<quint32>(data + 72);
            entry.child = qFromLittleEndian<quint32>(data + 76);
            entry.firstSector = qFromLittleEndian<quint32>(data + 116);
            entry.size = _majorVersion == 3 ? qFromLittleEndian<quint32>(data + 120)
                                             : qFromLittleEndian<quint64>(data + 120);
        }
    }
    if (entries.empty() || entries.front().type != 5)
        invalid("Missing compound file root entry");

    const DirectoryEntry& root = entries.front();
    _firstMiniStreamSector = root.firstSector;
    _miniStreamSize = root.size;

    struct Pending {
        quint32 entry;
        quint32 parent;
        unsigned depth;
    };
    std::vector<Pending> pending{{root.child, 0, 1}};
    std::vector<quint32> parents(entries.size(), NoDirectoryEntry);
    parents[0] = 0;
    while (!pending.empty()) {
        const Pending node = pending.back();
        pending.pop_back();
        if (node.entry == NoDirectoryEntry)
            continue;
        if (node.entry >= entries.size() || parents[node.entry] != NoDirectoryEntry ||
            node.depth > MaxStorageDepth)
            invalid("Invalid compound file directory tree");
        parents[node.entry] = node.parent;

        const DirectoryEntry& entry = entries[node.entry];
        pending.push_back({entry.right, node.parent, node.depth});
        pending.push_back({entry.left, node.parent, node.depth});
        if (entry.type == 1) {
            pending.push_back({entry.child, node.entry, node.depth + 1});
        } else if (entry.type == 2 && entry.child == NoDirectoryEntry) {
            Stream& stream = _streams.emplace_back();
            stream.size = entry.size;
            stream.firstSector = entry.firstSector;
            for (quint32 id = node.entry; id != 0; id = parents[id])
                stream.path.prepend(entries[id].name);
        } else {
            invalid("Unsupported compound file directory entry type");
        }
    }
}

QByteArray CompoundFile::readStream(std::size_t index, qsizetype maxBytes)
{
    const Stream& stream = _streams.at(index);
    if (maxBytes < 0 || stream.size > static_cast<quint64>(maxBytes))
        invalid("Compound file stream exceeds the requested byte limit");
    if (stream.size == 0)
        return {};

    const bool mini = stream.size < MiniStreamCutoff;
    const quint32 stride = mini ? MiniSectorSize : _sectorSize;
    const quint64 limit = mini ? sectorsFor(_miniStreamSize, MiniSectorSize) : _sectorCount;
    const quint64 count = sectorsFor(stream.size, stride);
    if (count > limit)
        invalid("Compound file stream exceeds available sectors");
    const auto chain = sectorChain(stream.firstSector, mini ? _miniFat : _fat,
                                   static_cast<std::size_t>(limit), static_cast<std::size_t>(count));

    QByteArray data;
    data.reserve(static_cast<qsizetype>(stream.size));
    quint64 remaining = stream.size;
    for (const quint32 sector : chain) {
        const qsizetype countBytes = static_cast<qsizetype>(std::min<quint64>(remaining, stride));
        const quint64 offset = quint64(sector) * stride;
        qint64 fileOffset = (static_cast<qint64>(sector) + 1) * _sectorSize;
        if (mini) {
            if (offset + countBytes > _miniStreamSize)
                invalid("Compound file mini sector exceeds the mini stream");
            const quint32 backing = _miniStreamSectors[static_cast<std::size_t>(offset / _sectorSize)];
            fileOffset = (static_cast<qint64>(backing) + 1) * _sectorSize +
                         static_cast<qint64>(offset % _sectorSize);
        }
        data.append(readAt(fileOffset, countBytes));
        remaining -= countBytes;
    }
    return data;
}

} // namespace Inventor
