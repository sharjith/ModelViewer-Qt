// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "InventorPartSegment.h"

#include "InventorCompoundFile.h"

#include <QByteArrayView>

#include <zlib.h>
#include <zstd.h>

#include <array>
#include <limits>
#include <optional>
#include <stdexcept>

namespace Inventor {
namespace {

constexpr qsizetype HeaderBytes = 18;
constexpr qsizetype MetadataLimit = 64 * 1024;

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

    const auto size = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN)
        throw std::runtime_error("Inventor Zstandard frame has no valid declared size");
    if (size > static_cast<quint64>(maxBytes))
        throw std::runtime_error("Inventor B-rep segment exceeds the requested byte limit");
    QByteArray output(static_cast<qsizetype>(size), Qt::Uninitialized);
    const auto written = ZSTD_decompress(output.data(), size, compressed.data(), compressed.size());
    if (ZSTD_isError(written) || written != size)
        throw std::runtime_error("Invalid or truncated Inventor Zstandard frame");
    return output;
}

} // namespace

QByteArray readPartBRepSegment(CompoundFile& file, qsizetype maxBytes)
{
    if (maxBytes <= 0)
        throw std::runtime_error("Invalid Inventor segment byte limit");

    // In the observed RSe metadata profile, the segment name is UTF-16LE
    // preceded by its 32-bit character count. Its M/B stream pair shares a suffix.
    static const QByteArray label("\x0d\0\0\0P\0m\0" "B\0R\0" "e\0p\0S\0" "e\0g\0m\0" "e\0n\0t\0", 30);
    std::optional<QString> dataName;
    const auto& streams = file.streams();
    for (std::size_t index = 0; index < streams.size(); ++index) {
        const auto& stream = streams[index];
        if (stream.path.size() != 2 || stream.path[0] != QStringLiteral("RSeStorage") ||
            !stream.path[1].startsWith(QLatin1Char('M')) || stream.size > MetadataLimit)
            continue;
        const qsizetype nameOffset = file.readStream(index, MetadataLimit).indexOf(label);
        if (nameOffset < 0 || nameOffset > 128)
            continue;
        if (dataName)
            throw std::runtime_error("Multiple Inventor B-rep segments matched the saved-part profile");
        dataName = QLatin1Char('B') + stream.path[1].mid(1);
    }
    if (!dataName)
        throw std::runtime_error("No supported saved part B-rep segment found in Inventor file");

    const QStringList dataPath{QStringLiteral("RSeStorage"), *dataName};
    for (std::size_t index = 0; index < streams.size(); ++index) {
        if (streams[index].path == dataPath)
            return decodeSegment(file.readStream(index, maxBytes), maxBytes);
    }
    throw std::runtime_error("Inventor B-rep metadata refers to a missing data stream");
}

} // namespace Inventor
