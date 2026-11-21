// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Internal to the Inventor importer: how a segment of the compound file is found, unpacked and split into typed nodes. Shared
// by the part reader (PartSegment.cpp) and the assembly reader (Assembly.cpp).
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QtEndian>

#include <optional>
#include <vector>

namespace Inventor {

class CompoundFile;

namespace Detail {

// A segment's metadata grows with the part (the graphics metadata of a 90 000-face part is about 170 KB), so the limit has
// to leave room for it; the stream is only read and searched, never trusted.
constexpr qsizetype MetadataLimit = 16 * 1024 * 1024;
// The graphics data of a large multi-body part is tens of megabytes compressed and several times that unpacked.
constexpr qsizetype GraphicsDataLimit = qsizetype(1024) * 1024 * 1024;
constexpr quint32 MaxGraphicsNodes = 8000000;
constexpr quint32 ReferenceList = 0x30000002;

// Unpack a zlib stream or a Zstandard frame into at most maxBytes (also bounded by the free memory).
QByteArray inflateSegment(QByteArrayView compressed, qsizetype maxBytes);
QByteArray decodeZstdFrame(QByteArrayView compressed, qsizetype maxBytes);
// A segment data stream: an 18-byte envelope, then zlib or Zstandard data.
QByteArray decodeSegment(const QByteArray& bytes, qsizetype maxBytes);

// The metadata stream of the segment whose name (UTF-16, preceded by its character count) is label; empty when there is none.
std::optional<std::size_t> findSegmentMetadata(CompoundFile& file, QByteArrayView label);
// The unpacked data stream that belongs to a metadata stream.
QByteArray readPartner(CompoundFile& file, std::size_t metadata, qsizetype maxBytes);

[[noreturn]] void unsupportedGraphics();

class GraphicsCursor
{
public:
    explicit GraphicsCursor(QByteArrayView bytes) : _bytes(bytes) {}

    template<typename T> T read()
    {
        return qFromLittleEndian<T>(take(sizeof(T)).data());
    }

    QByteArrayView take(qsizetype size)
    {
        if (size < 0 || size > _bytes.size() - _offset)
            unsupportedGraphics();
        const auto value = _bytes.sliced(_offset, size);
        _offset += size;
        return value;
    }

    void skip(qsizetype size) { take(size); }
    void skipList(qsizetype width) { skip(static_cast<qsizetype>(read<quint32>()) * width); }
    quint32 ref() { return read<quint32>() & 0x7fffffffu; }
    QByteArrayView rest() const { return _bytes.sliced(_offset); }

    // A UTF-16 string stored as its character count (32 bits) followed by the characters.
    QString string16()
    {
        const auto count = read<quint32>();
        if (count > 4096)
            unsupportedGraphics();
        const auto raw = take(static_cast<qsizetype>(count) * 2);
        QString text(static_cast<qsizetype>(count), Qt::Uninitialized);
        for (quint32 i = 0; i < count; ++i)
            text[static_cast<qsizetype>(i)] = QChar(qFromLittleEndian<quint16>(raw.data() + 2 * i));
        return text;
    }

private:
    QByteArrayView _bytes;
    qsizetype _offset = 0;
};

struct GraphicsNode {
    quint32 type;
    QByteArrayView data;
};

// The nodes of a typed segment (the part's graphics, or an assembly's occurrence tables). Node references are one-based:
// the first entry is a placeholder. Throws when the layout is not the one recognised.
std::vector<GraphicsNode> readGraphicsNodes(QByteArrayView metadata, QByteArrayView data);

} // namespace Detail
} // namespace Inventor
