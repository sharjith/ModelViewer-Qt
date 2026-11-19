// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "InventorPartSegment.h"

#include "InventorCompoundFile.h"

#include <QByteArrayView>
#include <QtEndian>

#include <zlib.h>
#include <zstd.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

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

[[noreturn]] void unsupportedGraphics()
{
    throw std::runtime_error("Unsupported Inventor graphics segment layout");
}

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

private:
    QByteArrayView _bytes;
    qsizetype _offset = 0;
};

struct GraphicsNode {
    quint32 type;
    QByteArrayView data;
};

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
    if (header.read<quint8>() != 1)
        unsupportedGraphics();
    const QByteArray unpacked = inflateSegment(header.rest(), 8 * 1024 * 1024);
    GraphicsCursor table(unpacked);
    table.skip(14);
    const auto sizeCount = table.read<quint32>();
    if (sizeCount > 200000)
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

constexpr quint32 ReferenceList = 0x30000002;

std::optional<DiffuseColor> diffuse(GraphicsCursor cursor)
{
    DiffuseColor rgb{};
    for (auto& channel : rgb) {
        const auto bits = cursor.read<quint32>();
        static_assert(sizeof(channel) == sizeof(bits));
        std::memcpy(&channel, &bits, sizeof(channel));
        if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f)
            return std::nullopt;
    }
    if (cursor.read<quint32>() != 0x3f800000u) // Opaque appearances in this profile.
        return std::nullopt;
    return rgb;
}

SavedAppearance decodeAppearance(const std::vector<GraphicsNode>& nodes)
{
    constexpr quint32 PartNode = 0xCA7163A3;
    constexpr quint32 BodyNode = 0x9A676A50;
    constexpr quint32 ShellNode = 0xA79EACCF;
    constexpr quint32 FaceNode = 0x5194E9A3;
    constexpr quint32 AttributeList = 0x120284EF;
    constexpr quint32 DrawColor = 0x022AC1B5;
    constexpr quint32 StyleList = 0x48EB8607;
    constexpr quint32 PrimaryColor = 0xAF48560F;
    const auto node = [&](quint32 ref, quint32 type) {
        if (ref >= nodes.size() || nodes[ref].type != type)
            unsupportedGraphics();
        return GraphicsCursor(nodes[ref].data);
    };
    const auto firstColor = [&](GraphicsCursor refs, quint32 type) -> std::optional<DiffuseColor> {
        if (refs.read<quint32>() != ReferenceList)
            unsupportedGraphics();
        const auto count = refs.read<quint32>();
        if (count == 0)
            return std::nullopt;
        refs.skip(4);
        if (refs.read<quint32>() != 0x10000000u || count > 1024)
            unsupportedGraphics();
        for (quint32 i = 0; i < count; ++i) {
            const auto ref = refs.ref();
            if (ref >= nodes.size())
                unsupportedGraphics();
            if (nodes[ref].type != type)
                continue;
            GraphicsCursor value(nodes[ref].data);
            if (type == DrawColor) {
                value.skip(9);
                if (value.read<quint32>() != ReferenceList)
                    unsupportedGraphics();
                value.skipList(4);
                value.skip(24);
            } else {
                value.skip(36);
            }
            return diffuse(value);
        }
        return std::nullopt;
    };
    const auto attributeColor = [&](quint32 ref) {
        if (ref == 0)
            return std::optional<DiffuseColor>{};
        auto attributes = node(ref, AttributeList);
        attributes.skip(5);
        return firstColor(attributes, DrawColor);
    };

    const GraphicsNode* body = nullptr;
    for (const auto& candidate : nodes) {
        if (candidate.type == BodyNode) {
            if (body)
                unsupportedGraphics();
            body = &candidate;
        }
    }
    if (!body)
        unsupportedGraphics();
    GraphicsCursor bodyFields(body->data);
    bodyFields.skip(10);
    auto color = attributeColor(bodyFields.ref());
    bodyFields.skip(1);
    auto part = node(bodyFields.ref(), PartNode);
    if (!color) {
        part.skip(10);
        color = attributeColor(part.ref());
    }
    bodyFields.skip(27);
    const auto shellRef = bodyFields.ref();
    node(shellRef, ShellNode);

    SavedAppearance appearance;
    appearance.body = color;
    for (const auto& candidate : nodes) {
        if (candidate.type != FaceNode)
            continue;
        GraphicsCursor face(candidate.data);
        face.skip(10);
        const auto stylesRef = face.ref();
        face.skip(4);
        if (face.ref() != shellRef)
            continue;
        // The qualified graphics face profile has its persistent key in the
        // final three-word trailer. Require a full key match in the B-rep.
        if (candidate.data.size() < 34)
            unsupportedGraphics();
        GraphicsCursor trailer(candidate.data.last(12));
        const auto key = trailer.read<quint32>();
        std::optional<DiffuseColor> override;
        if (stylesRef != 0)
            override = firstColor(node(stylesRef, StyleList), PrimaryColor);
        if (!appearance.faces.emplace(key, override).second)
            unsupportedGraphics();
    }
    return appearance;
}

} // namespace

QByteArray readPartBRepSegment(CompoundFile& file, qsizetype maxBytes)
{
    if (maxBytes <= 0)
        throw std::runtime_error("Invalid Inventor segment byte limit");
    // The segment name is UTF-16LE preceded by its 32-bit character count.
    static const QByteArray label("\x0d\0\0\0P\0m\0" "B\0R\0" "e\0p\0S\0" "e\0g\0m\0" "e\0n\0t\0", 30);
    const auto metadata = findSegmentMetadata(file, label);
    if (!metadata)
        throw std::runtime_error("No supported saved part B-rep segment found in Inventor file");
    return readPartner(file, *metadata, maxBytes);
}

std::optional<SavedAppearance> readPartAppearance(CompoundFile& file)
{
    try {
        static const QByteArray label("\x11\0\0\0P\0m\0" "G\0r\0a\0p\0h\0i\0c\0s\0" "S\0e\0g\0m\0e\0n\0t\0", 38);
        const auto index = findSegmentMetadata(file, label);
        if (!index)
            return std::nullopt;
        const auto metadata = file.readStream(*index, MetadataLimit);
        const auto graphics = readPartner(file, *index, 64 * 1024 * 1024);
        return decodeAppearance(readGraphicsNodes(metadata, graphics));
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

} // namespace Inventor
