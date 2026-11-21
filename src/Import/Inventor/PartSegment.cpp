// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "Inventor/PartSegment.h"

#include "Inventor/CompoundFile.h"
#include "SegmentData.h"

#include <QByteArrayView>
#include <QtEndian>


#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace Inventor {
namespace {

using namespace Detail; // the segment helpers shared with the assembly reader (SegmentData.h)

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

constexpr quint32 GraphicsBodyNode = 0x9A676A50;
constexpr quint32 GraphicsPartNode = 0xCA7163A3;
constexpr quint32 GraphicsShellNode = 0xA79EACCF;
constexpr quint32 GraphicsFaceNode = 0x5194E9A3;
constexpr quint32 GraphicsAttributeList = 0x120284EF;
constexpr quint32 GraphicsDrawColor = 0x022AC1B5;
constexpr quint32 GraphicsStyleList = 0x48EB8607;
constexpr quint32 GraphicsPrimaryColor = 0xAF48560F;

// Reads colours out of a decoded graphics node table.
class GraphicsReader
{
public:
    explicit GraphicsReader(const std::vector<GraphicsNode>& nodes) : _nodes(nodes) {}

    GraphicsCursor node(quint32 ref, quint32 type) const
    {
        if (ref >= _nodes.size() || _nodes[ref].type != type)
            unsupportedGraphics();
        return GraphicsCursor(_nodes[ref].data);
    }

    // The first colour of the given kind in a list of node references.
    std::optional<DiffuseColor> firstColor(GraphicsCursor refs, quint32 type) const
    {
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
            if (ref >= _nodes.size())
                unsupportedGraphics();
            if (_nodes[ref].type != type)
                continue;
            GraphicsCursor value(_nodes[ref].data);
            if (type == GraphicsDrawColor) {
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
    }

    std::optional<DiffuseColor> attributeColor(quint32 ref) const
    {
        if (ref == 0)
            return std::nullopt;
        auto attributes = node(ref, GraphicsAttributeList);
        attributes.skip(5);
        return firstColor(attributes, GraphicsDrawColor);
    }

    // The colour of a graphics body: its own, else its part's. shellRef receives the body's shell node.
    std::optional<DiffuseColor> bodyColor(const GraphicsNode& body, quint32& shellRef) const
    {
        GraphicsCursor fields(body.data);
        fields.skip(10);
        auto color = attributeColor(fields.ref());
        fields.skip(1);
        auto part = node(fields.ref(), GraphicsPartNode);
        if (!color) {
            part.skip(10);
            color = attributeColor(part.ref());
        }
        fields.skip(27);
        shellRef = fields.ref();
        node(shellRef, GraphicsShellNode);
        return color;
    }

    // The persistent key of a graphics face, held in the final three-word trailer.
    static quint32 faceKey(const GraphicsNode& face)
    {
        if (face.data.size() < 34)
            unsupportedGraphics();
        GraphicsCursor trailer(face.data.last(12));
        return trailer.read<quint32>();
    }

    // The colour override of a graphics face node, if it has one; shellRef receives the shell the face belongs to.
    std::optional<DiffuseColor> faceColor(const GraphicsNode& face, quint32& shellRef) const
    {
        GraphicsCursor fields(face.data);
        fields.skip(10);
        const auto stylesRef = fields.ref();
        fields.skip(4);
        shellRef = fields.ref();
        if (stylesRef == 0)
            return std::nullopt;
        return firstColor(node(stylesRef, GraphicsStyleList), GraphicsPrimaryColor);
    }

private:
    const std::vector<GraphicsNode>& _nodes;
};

// The appearance of the one graphics body of a single-body part: the body colour and each face's override.
SavedAppearance decodeAppearance(const std::vector<GraphicsNode>& nodes)
{
    const GraphicsNode* body = nullptr;
    for (const auto& candidate : nodes) {
        if (candidate.type == GraphicsBodyNode) {
            if (body)
                unsupportedGraphics();
            body = &candidate;
        }
    }
    if (!body)
        unsupportedGraphics();
    const GraphicsReader reader(nodes);
    quint32 shellRef = 0;
    SavedAppearance appearance;
    appearance.body = reader.bodyColor(*body, shellRef);
    for (const auto& candidate : nodes) {
        if (candidate.type != GraphicsFaceNode)
            continue;
        quint32 faceShell = 0;
        const auto override = reader.faceColor(candidate, faceShell);
        if (faceShell != shellRef)
            continue;
        // The qualified graphics face profile has its persistent key in the final three-word trailer. Require a full key
        // match in the B-rep.
        if (!appearance.faces.emplace(GraphicsReader::faceKey(candidate), override).second)
            unsupportedGraphics();
    }
    return appearance;
}

// Every graphics face of a part with several bodies, whichever shell it hangs from. A key that two faces share with
// different colours is ambiguous and is left out, so a face is only coloured when the file says so unambiguously.
PartColorTable decodeColorTable(const std::vector<GraphicsNode>& nodes)
{
    const GraphicsReader reader(nodes);
    const GraphicsNode* body = nullptr;
    for (const auto& candidate : nodes) {
        if (candidate.type == GraphicsBodyNode) {
            body = &candidate;
            break;
        }
    }
    if (!body)
        unsupportedGraphics();
    PartColorTable table;
    quint32 shellRef = 0;
    table.defaultColor = reader.bodyColor(*body, shellRef);

    std::unordered_map<quint32, std::optional<DiffuseColor>> seen;
    std::unordered_set<quint32> conflicted;
    for (const auto& candidate : nodes) {
        if (candidate.type != GraphicsFaceNode)
            continue;
        quint32 faceShell = 0;
        const auto color = reader.faceColor(candidate, faceShell);
        const auto key = GraphicsReader::faceKey(candidate);
        const auto [position, inserted] = seen.emplace(key, color);
        if (!inserted && position->second != color)
            conflicted.insert(key);
    }
    for (const auto& [key, color] : seen) {
        if (color && conflicted.count(key) == 0)
            table.faces.emplace(key, *color);
    }
    table.conflicts = conflicted.size();
    return table;
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
        const auto graphics = readPartner(file, *index, GraphicsDataLimit);
        return decodeAppearance(readGraphicsNodes(metadata, graphics));
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

std::optional<PartColorTable> readPartColorTable(CompoundFile& file)
{
    try {
        static const QByteArray label("\x11\0\0\0P\0m\0" "G\0r\0a\0p\0h\0i\0c\0s\0" "S\0e\0g\0m\0e\0n\0t\0", 38);
        const auto index = findSegmentMetadata(file, label);
        if (!index)
            return std::nullopt;
        const auto metadata = file.readStream(*index, MetadataLimit);
        const auto graphics = readPartner(file, *index, GraphicsDataLimit);
        return decodeColorTable(readGraphicsNodes(metadata, graphics));
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}


std::vector<PartBodyInfo> readPartBodyInfo(CompoundFile& file)
{
    try {
        // The names live in the model browser segment, stored as records "tag, count, UTF-16 text": tag 0x0109 is a
        // feature, tag 0x010a a body. A derived-surface feature record is followed directly by the body it owns.
        static const QByteArray browserLabel("\x10\0\0\0P\0m\0" "B\0r\0o\0w\0s\0e\0r\0" "S\0e\0g\0m\0e\0n\0t\0", 36);
        const auto browserIndex = findSegmentMetadata(file, browserLabel);
        if (!browserIndex)
            return {};
        const QByteArray browser = readPartner(file, *browserIndex, qsizetype(256) * 1024 * 1024);

        struct Entry {
            qsizetype offset;
            qsizetype end;
            QString text;
        };
        std::vector<Entry> features;
        std::vector<Entry> bodies;
        qsizetype acceptedEnd = 0;
        for (qsizetype i = 0; i + 8 <= browser.size(); ++i) {
            const auto tag = static_cast<unsigned char>(browser[i]);
            if ((tag != 0x09 && tag != 0x0a) || browser[i + 1] != 1 || browser[i + 2] != 0 || browser[i + 3] != 0 || i < acceptedEnd)
                continue;
            const auto count = qFromLittleEndian<quint32>(browser.constData() + i + 4);
            if (count < 2 || count > 256 || i + 8 + qsizetype(count) * 2 > browser.size())
                continue;
            QString text(static_cast<qsizetype>(count), Qt::Uninitialized);
            std::memcpy(text.data(), browser.constData() + i + 8, static_cast<std::size_t>(count) * 2);
            if (!std::all_of(text.begin(), text.end(), [](QChar c) { return c.isPrint() && !c.isSurrogate(); }))
                continue;
            const qsizetype end = i + 8 + qsizetype(count) * 2;
            (tag == 0x09 ? features : bodies).push_back({i, end, text});
            acceptedEnd = end;
        }

        // The owning feature is the one saved just before the body.
        std::vector<PartBodyInfo> plain;
        std::vector<PartBodyInfo> derived;
        for (const auto& body : bodies) {
            PartBodyInfo info;
            info.name = body.text;
            for (const auto& feature : features) {
                if (feature.end <= body.offset && body.offset - feature.end < 400)
                    info.feature = feature.text;
            }
            (info.feature.isEmpty() ? plain : derived).push_back(std::move(info));
        }
        // The graphics lists the plain bodies first, then the bodies of derived features in the order they were saved.
        std::vector<PartBodyInfo> named = std::move(plain);
        for (auto& info : derived)
            named.push_back(std::move(info));

        static const QByteArray graphicsLabel("\x11\0\0\0P\0m\0" "G\0r\0a\0p\0h\0i\0c\0s\0" "S\0e\0g\0m\0e\0n\0t\0", 38);
        const auto graphicsIndex = findSegmentMetadata(file, graphicsLabel);
        if (!graphicsIndex)
            return {};
        const auto metadata = file.readStream(*graphicsIndex, MetadataLimit);
        const auto graphics = readPartner(file, *graphicsIndex, GraphicsDataLimit);
        const auto nodes = readGraphicsNodes(metadata, graphics);
        const GraphicsReader reader(nodes);

        std::unordered_map<quint32, std::size_t> facesOfShell;
        for (const auto& candidate : nodes) {
            if (candidate.type != GraphicsFaceNode)
                continue;
            quint32 shell = 0;
            reader.faceColor(candidate, shell);
            ++facesOfShell[shell];
        }
        // A graphics body owns the shells saved just before its own shell node (its display and its full representation).
        std::vector<std::vector<std::size_t>> shellFaceCounts;
        for (const auto& candidate : nodes) {
            if (candidate.type != GraphicsBodyNode)
                continue;
            quint32 bodyShell = 0;
            reader.bodyColor(candidate, bodyShell);
            std::vector<std::size_t> counts;
            for (quint32 shell = bodyShell > 3 ? bodyShell - 3 : 1; shell < bodyShell; ++shell) {
                const auto found = facesOfShell.find(shell);
                if (found != facesOfShell.end())
                    counts.push_back(found->second);
            }
            shellFaceCounts.push_back(std::move(counts));
        }
        if (shellFaceCounts.size() != named.size())
            return {}; // the browser and the graphics do not list the same bodies: names are not used
        for (std::size_t i = 0; i < named.size(); ++i)
            named[i].faceCounts = std::move(shellFaceCounts[i]);
        return named;
    } catch (const std::runtime_error&) {
        return {};
    }
}

} // namespace Inventor
