// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "SegmentData.h"

#include "Inventor/CompoundFile.h"

// zlib before MemoryGuard.h: that header pulls in the Windows headers, whose macros break zlib's declarations.
#include <zlib.h>
#include <zstd.h>

#include "MemoryGuard.h"

#include <array>
#include <limits>
#include <stdexcept>

namespace Inventor {
namespace Detail {

namespace {
constexpr qsizetype HeaderBytes = 18;
} // namespace

QByteArray inflateSegment(QByteArrayView compressed, qsizetype maxBytes)
{
    if (static_cast<quint64>(compressed.size()) > std::numeric_limits<uInt>::max())
        throw std::runtime_error("Inventor zlib input is too large");
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        throw std::runtime_error("Could not initialize Inventor zlib decoder");
    struct InflateGuard {
        z_stream& stream;
        ~InflateGuard() { inflateEnd(&stream); }
    } guard{stream};

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    QByteArray output;
    std::array<char, 64 * 1024> buffer;
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
            throw std::runtime_error("Invalid or truncated Inventor zlib frame");
        const auto written = static_cast<qsizetype>(buffer.size() - stream.avail_out);
        if (written > maxBytes - output.size())
            throw std::runtime_error("Inventor B-rep segment exceeds the requested byte limit");
        output.append(buffer.data(), written);
        if ((output.size() & ((qsizetype(64) << 20) - 1)) < written)
            requireFreeMemory("decompressing the B-rep segment", static_cast<std::size_t>(output.size()) / 4);
    } while (status != Z_STREAM_END);
    return output;
}

QByteArray decodeSegment(const QByteArray& bytes, qsizetype maxBytes)
{
    if (bytes.size() <= HeaderBytes || bytes[16] != 4)
        throw std::runtime_error("Unsupported Inventor segment envelope");
    const QByteArrayView compressed = QByteArrayView(bytes).sliced(HeaderBytes);
    if (bytes[17] == 1)
        return inflateSegment(compressed, maxBytes);
    if (bytes[17] != 2)
        throw std::runtime_error("Unsupported Inventor segment compression");
    return decodeZstdFrame(compressed, maxBytes);
}

QByteArray decodeZstdFrame(QByteArrayView compressed, qsizetype maxBytes)
{
    const auto size = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (size == ZSTD_CONTENTSIZE_ERROR)
        throw std::runtime_error("Inventor Zstandard frame is invalid");
    if (size == ZSTD_CONTENTSIZE_UNKNOWN) {
        // The writer did not record the decompressed size; decode in bounded steps.
        struct Context {
            ZSTD_DCtx* context = ZSTD_createDCtx();
            ~Context() { ZSTD_freeDCtx(context); }
        } decoder;
        if (!decoder.context)
            throw std::runtime_error("Could not initialize Inventor Zstandard decoder");
        QByteArray output;
        std::vector<char> buffer(1024 * 1024);
        ZSTD_inBuffer input{compressed.data(), static_cast<size_t>(compressed.size()), 0};
        size_t status = 1;
        while (status != 0) {
            ZSTD_outBuffer target{buffer.data(), buffer.size(), 0};
            status = ZSTD_decompressStream(decoder.context, &target, &input);
            if (ZSTD_isError(status))
                throw std::runtime_error("Invalid or truncated Inventor Zstandard frame");
            if (target.pos > static_cast<size_t>(maxBytes - output.size()))
                throw std::runtime_error("Inventor B-rep segment exceeds the requested byte limit");
            output.append(buffer.data(), static_cast<qsizetype>(target.pos));
            if ((output.size() & ((qsizetype(64) << 20) - 1)) < static_cast<qsizetype>(target.pos))
                requireFreeMemory("decompressing the B-rep segment", static_cast<std::size_t>(output.size()) / 4);
            if (target.pos == 0 && input.pos == input.size && status != 0)
                throw std::runtime_error("Truncated Inventor Zstandard frame");
        }
        return output;
    }
    if (size > static_cast<quint64>(maxBytes))
        throw std::runtime_error("Inventor B-rep segment exceeds the requested byte limit");
    requireFreeMemory("decompressing the B-rep segment", static_cast<std::size_t>(size));
    QByteArray output(static_cast<qsizetype>(size), Qt::Uninitialized);
    const auto written = ZSTD_decompress(output.data(), size, compressed.data(), compressed.size());
    if (ZSTD_isError(written) || written != size)
        throw std::runtime_error("Invalid or truncated Inventor Zstandard frame");
    return output;
}

std::optional<std::size_t> findSegmentMetadata(CompoundFile& file, QByteArrayView label)
{
    std::optional<std::size_t> match;
    const auto& streams = file.streams();
    for (std::size_t index = 0; index < streams.size(); ++index) {
        const auto& stream = streams[index];
        if (stream.path.size() != 2 || stream.path[0] != QStringLiteral("RSeStorage") ||
            !stream.path[1].startsWith(QLatin1Char('M')) || stream.size > MetadataLimit)
            continue;
        const qsizetype nameOffset = file.readStream(index, MetadataLimit).indexOf(label);
        if (nameOffset < 0 || nameOffset > 128)
            continue;
        if (match)
            throw std::runtime_error("Multiple Inventor segments matched the saved-part profile");
        match = index;
    }
    return match;
}

QByteArray readPartner(CompoundFile& file, std::size_t metadata, qsizetype maxBytes)
{
    const auto& streams = file.streams();
    const QStringList dataPath{QStringLiteral("RSeStorage"), QLatin1Char('B') + streams[metadata].path[1].mid(1)};
    for (std::size_t index = 0; index < streams.size(); ++index) {
        if (streams[index].path == dataPath)
            return decodeSegment(file.readStream(index, maxBytes), maxBytes);
    }
    throw std::runtime_error("Inventor metadata refers to a missing data stream");
}

void unsupportedGraphics()
{
    throw std::runtime_error("Unsupported Inventor graphics segment layout");
}

std::vector<GraphicsNode> readGraphicsNodes(QByteArrayView metadata, QByteArrayView data)
{
    GraphicsCursor header(metadata);
    header.skipList(1);
    if (header.read<quint16>() != 8)
        unsupportedGraphics();
    header.skip(16);
    header.skipList(2);
    header.skip(28); // Segment identifier and flags.
    header.skipList(1);
    header.skipList(1);
    // How the node table is stored: 1 = zlib (small parts), 2 = Zstandard (larger parts).
    const quint8 packing = header.read<quint8>();
    if (packing != 1 && packing != 2)
        unsupportedGraphics();
    constexpr qsizetype TableLimit = qsizetype(256) * 1024 * 1024;
    const QByteArray unpacked = packing == 1 ? inflateSegment(header.rest(), TableLimit)
                                              : decodeZstdFrame(header.rest(), TableLimit);
    GraphicsCursor table(unpacked);
    table.skip(14);
    const auto sizeCount = table.read<quint32>();
    if (sizeCount > MaxGraphicsNodes)
        unsupportedGraphics();
    std::vector<quint32> sizes;
    sizes.reserve(sizeCount);
    for (quint32 i = 0; i < sizeCount; ++i) {
        const auto size = table.read<quint32>();
        if ((size & 0x80000000u) != 0)
            sizes.push_back(size & 0x7fffffffu);
    }
    table.skip(4);
    table.skipList(10);
    table.skip(4);
    table.skipList(28);
    table.skip(4);
    const auto typeCount = table.read<quint32>();
    if (typeCount > 256)
        unsupportedGraphics();
    std::vector<quint32> types;
    types.reserve(typeCount);
    for (quint32 i = 0; i < typeCount; ++i) {
        types.push_back(table.read<quint32>());
        table.skip(24);
    }

    GraphicsCursor blocks(data);
    std::vector<GraphicsNode> nodes(1); // Node references are one-based.
    nodes.reserve(sizes.size() + 1);
    for (const auto size : sizes) {
        const auto type = blocks.read<quint32>() & 0xffu;
        if (type >= types.size())
            unsupportedGraphics();
        nodes.push_back({types[type], blocks.take(size)});
        const auto repeatedSize = blocks.read<quint32>();
        if ((repeatedSize != 0 && repeatedSize != size) || blocks.read<quint8>() != 0)
            unsupportedGraphics();
    }
    return nodes;
}

} // namespace Detail
} // namespace Inventor
