#include "RepresentationReader.h"
#include "PackedReader.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
#include <set>

namespace ThreeDXml {
namespace {
[[noreturn]] void fail(const QString& reason) { throw Error(QStringLiteral("Unsupported/corrupt UVR: ")+reason); }
void need(bool condition, const char* message) { if (!condition) fail(QString::fromLatin1(message)); }
quint32 be(const QByteArray& data, qsizetype at) {
    need(at >= 0 && at <= data.size()-4, "truncated compound integer");
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()+at));
}
quint16 be16(const QByteArray& data, qsizetype at) {
    need(at >= 0 && at <= data.size()-2, "truncated compound short");
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()+at));
}
struct Extent { quint32 physical, size, logical; };
struct Entry { quint32 id, kind, total; QString name; std::vector<Extent> extents; };
Entry descriptor(const QByteArray& data, qsizetype offset, quint32 id, quint32 kind) {
    quint32 length = be(data, offset);
    need(length >= 34 && length <= 200034 && quint64(offset)+length <= quint64(data.size()), "descriptor bounds");
    need(be(data, offset+4) == id && be16(data, offset+8) == kind && be(data, offset+10) == kind && be(data, offset+18) == 0, "descriptor identity/type");
    quint32 count = be(data, offset+30);
    need(quint64(length) == 34ULL+20ULL*count, "extent table length");
    Entry entry{id, kind, be(data, offset+14), {}, {}};
    need(entry.total <= quint64(data.size()), "logical stream size limit");
    quint64 cursor = 0;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype q = offset+34+20*i;
        quint32 physical = be(data, q), stored = be(data, q+4);
        need(stored == be(data, q+8) && cursor == be(data, q+12) && be(data, q+16) == 0, "compressed/noncontiguous logical extent");
        need(physical >= 128 && quint64(physical)+stored <= quint64(data.size()), "physical extent bounds");
        entry.extents.push_back({physical, stored, static_cast<quint32>(cursor)}); cursor += stored;
    }
    need(cursor == entry.total, "extent total mismatch");
    return entry;
}
QByteArray join(const QByteArray& data, const Entry& entry) {
    QByteArray result; result.reserve(entry.total);
    for (const auto& e : entry.extents) result.append(data.constData()+e.physical, e.size);
    return result;
}
QString utf16(const QByteArray& data) {
    need(data.size() >= 2 && data.size()%2 == 0, "catalog name length");
    QString name;
    for (qsizetype i = 0; i < data.size(); i += 2) name.append(QChar(be16(data, i)));
    need(name.endsWith(QChar(0)) && !name.left(name.size()-1).contains(QChar(0)), "catalog name terminator");
    name.chop(1); return name;
}
std::vector<Entry> compound(const QByteArray& data, const Options& options) {
    need(data.startsWith(QByteArray("V5_CFV3\0", 8)) && data.size() <= 128*1024*1024, "compound signature/size");
    // The allocation-header bias varies with ancillary data. Resolve the root
    // through its descriptor and validate it against the independent catalog end.
    const quint32 rootId = 2;
    QByteArray rootPattern = QByteArray::fromHex("00000036000000020001");
    std::vector<Entry> rootCandidates;
    qsizetype rootSearch = -1;
    while ((rootSearch = data.indexOf(rootPattern, rootSearch+1)) >= 0) {
        try {
            auto e = descriptor(data, rootSearch, rootId, 1);
            if (e.extents.size()!=1) continue;
            const auto& extent=e.extents.front();
            if (quint64(extent.physical)+extent.size!=be(data,8)) continue;
            const auto catalog=join(data,e);
            if (catalog.size()>=12 && be(catalog,0)==catalog.size() && be(catalog,4)==rootId) rootCandidates.push_back(e);
        } catch(const Error&) { }
    }
    need(rootCandidates.size()==1,"root catalog descriptor");
    const qsizetype rootAt=rootCandidates.front().extents.front().physical;
    const quint32 rootSize=rootCandidates.front().total;
    std::vector<Entry> entries;
    QSet<quint32> seen;
    std::function<void(const QByteArray&, quint32, int)> children;
    children = [&](const QByteArray& catalog, quint32 id, int depth) {
        options.check();
        need(depth <= 128 && catalog.size() >= 12 && be(catalog, 0) == catalog.size() && be(catalog, 4) == id, "storage catalog header");
        const quint32 count = be(catalog, 8); need(count <= 10000, "catalog entry count");
        qsizetype cursor = 12;
        for (quint32 i = 0; i < count; ++i) {
            options.check();
            quint32 size = be(catalog, cursor), child = be(catalog, cursor+4), kind = be16(catalog, cursor+8), nameSize = be(catalog, cursor+10);
            need(size >= 16 && quint64(cursor)+size <= quint64(catalog.size()) && quint64(size) == 14ULL+nameSize, "catalog child bounds");
            need(!seen.contains(child) && (kind == 1 || kind == 2), "duplicate/unsupported catalog ID");
            seen.insert(child);
            QByteArray needle = catalog.mid(cursor+14, nameSize);
            QString name = utf16(needle);
            std::vector<Entry> matches;
            qsizetype at = -1;
            while ((at = data.indexOf(needle, at+1)) >= 0) {
                if (at < 4 || be(data, at-4) != needle.size()) continue;
                try { auto entry = descriptor(data, at+needle.size(), child, kind); entry.name = name; matches.push_back(std::move(entry)); }
                catch (const Error&) { }
            }
            need(matches.size() == 1, "absent/ambiguous catalog descriptor");
            Entry entry = std::move(matches.front()); entries.push_back(entry);
            if (kind == 1 && entry.total) children(join(data, entry), child, depth+1);
            cursor += size;
        }
        need(cursor == catalog.size(), "unconsumed catalog bytes");
    };
    children(data.mid(rootAt, rootSize), rootId, 0);
    const quint32 maximum = be16(data, 89);
    need(maximum >= 2 && maximum <= 10000, "compound ID limit");
    // Some empty storages announce metadata children via complete parent/name headers.
    for (quint32 id = 1; id <= maximum; ++id) {
        options.check();
        if (seen.contains(id) || id == 1 || id == rootId) continue;
        std::vector<Entry> matches;
        for (quint32 kind : {1U, 2U}) {
            QByteArray pattern(6, '\0'); qToBigEndian(id, reinterpret_cast<uchar*>(pattern.data()));
            qToBigEndian(static_cast<quint16>(kind), reinterpret_cast<uchar*>(pattern.data()+4));
            qsizetype at = 3;
            while ((at = data.indexOf(pattern, at+1)) >= 0) {
                const qsizetype q = at-4;
                Entry entry;
                try { entry = descriptor(data, q, id, kind); } catch (const Error&) { continue; }
                for (qsizetype bytes = 2; bytes <= std::min<qsizetype>(q-28, 4096); bytes += 2) {
                    const qsizetype p = q-bytes;
                    if (be(data, p-4) != bytes || be(data, p-24) != kind || be(data, p-20) != entry.total || be(data, p-16) != 0) continue;
                    QString name;
                    try { name = utf16(data.mid(p, bytes)); } catch (const Error&) { continue; }
                    const quint32 parent = be(data, p-28);
                    auto found = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == parent; });
                    if (parent != 1 && parent != rootId && found == entries.end()) continue;
                    need(found == entries.end() || found->kind == 1, "metadata parent is not storage");
                    entry.name = name; matches.push_back(entry);
                }
            }
        }
        need(matches.size() == 1, "absent/ambiguous metadata descriptor");
        entries.push_back(std::move(matches.front())); seen.insert(id);
    }
    QByteArray anchor(10, '\0');
    qToBigEndian(quint32(54), reinterpret_cast<uchar*>(anchor.data()));
    qToBigEndian(rootId, reinterpret_cast<uchar*>(anchor.data()+4));
    qToBigEndian(quint16(1), reinterpret_cast<uchar*>(anchor.data()+8));
    qsizetype at = -1; std::vector<Entry> roots;
    while ((at = data.indexOf(anchor, at+1)) >= 0) {
        try {
            auto e = descriptor(data, at, rootId, 1);
            if (e.extents.size() == 1 && e.extents[0].physical == rootAt && e.extents[0].size == rootSize && e.extents[0].logical == 0) roots.push_back(e);
        } catch (const Error&) { }
    }
    need(roots.size() == 1, "root descriptor anchor");
    seen.insert(1); seen.insert(rootId);
    need(seen.size() == maximum, "compound identity coverage");
    for (quint32 id = 1; id <= maximum; ++id) need(seen.contains(id), "missing compound identity");
    std::vector<std::pair<quint64, quint64>> ranges;
    entries.push_back(roots.front());
    for (const auto& e : entries) for (const auto& extent : e.extents) if (extent.size) ranges.emplace_back(extent.physical, quint64(extent.physical)+extent.size);
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 1; i < ranges.size(); ++i) need(ranges[i-1].second <= ranges[i].first, "overlapping physical extents");
    entries.pop_back(); return entries;
}

using Reader = PackedReader;

unsigned color(Reader& r, quint8* alphaByte = nullptr) {
    const QByteArray prefix(r.raw(5), 5);
    // Head byte observed as 20, 30 or b0; bytes 1-3 are always 04 04 ff. The fifth byte varies (ff, 7f, 00, 37):
    // it is kept as a candidate opacity byte for the caller, not interpreted here.
    const quint8 head = static_cast<quint8>(prefix[0]);
    need((head == 0x20 || head == 0x30 || head == 0xb0) && prefix.mid(1, 3) == QByteArray::fromHex("0404ff"), "face style");
    // Observed in the samples: head b0 appears only on the part the owner reports as transparent (fifth byte 37); every
    // other record is head 20/30 with ff. Head bit 0x80 means "opacity byte present": opacity = byte/255. Confirmed on
    // the engine block: CATIA transparency 200 (0 opaque, 255 clear) = opacity 55 = the stored 0x37.
    if (alphaByte) *alphaByte = (head & 0x80) ? static_cast<quint8>(prefix[4]) : quint8(255);
    const unsigned b = r.byte(), g = r.byte(), red = r.byte(); return (red<<16)|(g<<8)|b;
}

Face primitive(Reader& r, unsigned width, unsigned vertexCount, bool keep) {
    need(r.byte() == 1, "primitive envelope");
    const unsigned flags = r.byte(); need(flags >= 0x41 && flags <= 0x4f, "primitive flags");
    const unsigned lists = flags & 1 ? r.count() : 0, strips = flags & 2 ? r.count() : 0, fans = flags & 4 ? r.count() : 0;
    const unsigned count = r.count(); need(quint64(lists)+strips+fans <= count, "primitive counts");
    Face face; face.constant = flags & 8;
    if (face.constant) face.normal = r.vector();
    std::vector<unsigned> lengths(strips+fans);
    quint64 expected = 3ULL*lists;
    for (auto& n : lengths) { n = r.bits(width); need(n >= 3, "short strip/fan"); expected += n; }
    need(expected == count, "primitive length mismatch");
    std::vector<unsigned> indices(count);
    for (auto& i : indices) { i = r.bits(width); need(i < vertexCount, "position index out of range"); }
    if (!keep) return face;
    size_t cursor = 0;
    for (unsigned i = 0; i < lists; ++i, cursor += 3) face.triangles.push_back({indices[cursor],indices[cursor+1],indices[cursor+2]});
    for (unsigned group = 0; group < lengths.size(); ++group) {
        const unsigned length = lengths[group];
        for (unsigned i = 0; i < length-2; ++i) {
            if (group < strips) {
                unsigned a = indices[cursor+i], b = indices[cursor+i+1];
                if (i & 1) std::swap(a,b);
                face.triangles.push_back({a,b,indices[cursor+i+2]});
            } else face.triangles.push_back({indices[cursor],indices[cursor+i+1],indices[cursor+i+2]});
        }
        cursor += length;
    }
    need(cursor == count, "unused primitive indices"); return face;
}

Body mesh(Reader& r, const Options& options, qsizetype leafEnd) {
    Body body;
    const unsigned vertices = r.count(1000000), records = r.count(1000000);
    need(vertices > 10 && records > 0 && records <= vertices, "unsupported position/normal counts");
    r.expect("000000"); unsigned width = 0;
    // The stored width includes the count itself, including powers of two.
    for (unsigned v = vertices; v; v >>= 1) ++width;
    body.positions.resize(vertices);
    if (r.bits(1)) for (auto& p : body.positions) p = r.vector();
    else {
        const QByteArray symbols(r.raw((vertices+3)/4), (vertices+3)/4);
        quint64 expected = 0;
        auto length = [&](unsigned i) { return ((static_cast<unsigned char>(symbols[i/4]) >> (2*(i%4)))+3)%4; };
        for (unsigned i = 0; i < vertices; ++i) expected += length(i);
        need(r.count() == expected, "coordinate float count");
        aiVector3D previous;
        for (unsigned i = 0; i < vertices; ++i) {
            for (unsigned j = 3-length(i); j < 3; ++j) previous[j] = r.scalar();
            body.positions[i] = previous;
        }
    }
    const unsigned wordCount = r.count(2000000);
    body.normalWords.resize(wordCount);
    for (auto& word : body.normalWords) word = qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(r.raw(2)));
    const QByteArray packed(r.raw((records+1)/2), (records+1)/2);
    body.normalTags.resize(records); body.normals.reserve(records); size_t cursor = 0;
    for (unsigned i = 0; i < records; ++i) {
        const unsigned tag = (static_cast<unsigned char>(packed[i/2]) >> (4*(i%2))) & 15;
        need(tag <= 11, "normal tag"); body.normalTags[i] = tag; aiVector3D normal;
        if (tag < 6) {
            need(cursor+2 <= wordCount, "normal word count");
            double components[3] = {}; unsigned axis = tag/2;
            for (unsigned j = 0; j < 3; ++j) if (j != axis) components[j] = double(body.normalWords[cursor++])/32767.0;
            double remainder = 1;
            for (double v : components) remainder -= v*v;
            need(remainder >= -.0001, "normal outside unit sphere");
            components[axis] = (tag%2 ? -1 : 1)*std::sqrt(std::max(0.0, remainder));
            normal = aiVector3D(static_cast<ai_real>(components[0]), static_cast<ai_real>(components[1]), static_cast<ai_real>(components[2]));
        } else normal[(tag-6)/2] = tag%2 ? -1 : 1;
        body.normals.push_back(normal);
    }
    need(cursor == wordCount, "unconsumed normal words");
    r.scalar();
    const unsigned coarse = r.count(100000);
    for (unsigned i = 0; i < coarse; ++i) { options.check(); primitive(r,width,vertices,false); r.scalar(); color(r); }
    const qsizetype containerOffset = r.pos;
    const QByteArray container(r.raw(3),3);
    // First byte observed as 02, 03 or 04 (04 comes with an extra kind-0 tail group); its meaning is not established.
    const quint8 containerHead = static_cast<quint8>(container[0]);
    if (!(containerHead >= 2 && containerHead <= 4 && container.mid(1, 2) == QByteArray::fromHex("020a"))) {
        // Structural bytes only (no coordinates): lets an unrecognized exporter variant be reported without sharing the model.
        const qsizetype from = std::max<qsizetype>(0, containerOffset-12);
        fail(QStringLiteral("finest-face container %1 at offset 0x%2 (%3 coarse blocks, %4 vertices; preceding bytes %5)")
            .arg(QString::fromLatin1(container.toHex(' ')))
            .arg(containerOffset, 0, 16).arg(coarse).arg(vertices)
            .arg(QString::fromLatin1(r.data.mid(from, containerOffset-from).toHex(' '))));
    }
    const unsigned faces = r.count(1000000); body.faces.reserve(faces);
    for (unsigned i = 0; i < faces; ++i) { options.check(); body.faces.push_back(primitive(r,width,vertices,true)); }
    for (auto& face : body.faces) face.rgb = color(r, &face.alphaByte);
    bool groups[3] = {};
    // Tail groups (kind 0, 1 or 2) are consumed up to the end of the leaf record. Kinds 1 and 2 are kept as edge
    // polylines; kind 0 (seen once, not understood) is validated and dropped.
    const qsizetype tailEnd = std::min<qsizetype>(leafEnd, r.data.size());
    while (r.pos+2 <= tailEnd && r.data[r.pos] == 1 && static_cast<quint8>(r.data[r.pos+1]) <= 2) {
        r.byte(); const unsigned kind = r.byte(); need(!groups[kind], "duplicate tail group"); groups[kind] = true;
        const unsigned count = r.count(1000000);
        LineGroup group;
        for (unsigned i = 0; i < count; ++i) {
            options.check(); need(r.byte() == 2, "tail group prefix"); const unsigned n = r.count(1000000);
            std::vector<unsigned> polyline; polyline.reserve(n);
            for (unsigned j = 0; j < n; ++j) { const auto index = r.bits(width); need(index < vertices, "tail index range"); polyline.push_back(static_cast<unsigned>(index)); }
            if (polyline.size() >= 2) group.polylines.push_back(std::move(polyline));
        }
        const QByteArray style(r.raw(8),8);
        need(style.left(5) == QByteArray::fromHex("102404ffff") || style.left(5) == QByteArray::fromHex("101404ffff"), "tail style");
        // Last three style bytes are BGR, like the face styles.
        group.rgb = (unsigned(quint8(style[7])) << 16) | (unsigned(quint8(style[6])) << 8) | unsigned(quint8(style[5]));
        if (kind != 0 && options.edges && !group.polylines.empty()) body.lines.push_back(std::move(group));
    }
    return body;
}

void attributes(Reader& r, unsigned flags) {
    if (!(flags & 0x10000)) return;
    // Image-bearing tail shared by the image form and the hybrid form below. The pixels are structurally skipped;
    // the image (a placeholder environment map, or a file texture such as a wood material) is not yet displayed.
    auto imageTail = [&r]() {
        r.expect("010101"); r.count(std::numeric_limits<quint32>::max()); need(r.byte() == 3, "attribute image format");
        quint64 width = r.count(), height = r.count(), bytes = r.count(64*1024*1024);
        // Pixel marker after the counts: 03 01 with 4 bytes per pixel (the placeholder image in the first samples),
        // 02 00 with 3 bytes per pixel (a real RGB texture; inferred from one error report, layout after it assumed equal).
        const bool rgba = bytes == width*height*4, rgb = bytes == width*height*3;
        need(rgba || rgb, "attribute image dimensions"); r.expect(rgba ? "0301" : "0200"); r.scalar(); r.scalar(); r.raw(bytes);
        r.raw(r.count());
        // Mapping prefix: 00 00 01 05 03 01 01 00 after the placeholder environment image, 00 01 01 05 00 01 01 00 after
        // a file texture (the sample's name field is a texture path); the 19 mapping scalars after it are read alike.
        const QByteArray mapping(r.raw(8), 8);
        need(mapping == QByteArray::fromHex("0000010503010100") || mapping == QByteArray::fromHex("0001010500010100"), "attribute image mapping prefix");
        for (int i = 0; i < 19; ++i) r.scalar();
        r.expect("0100");
    };
    if (r.pos < r.data.size() && r.data[r.pos] == 0) {
        r.expect("0001");
        for (int i=0;i<15;++i) r.scalar();
        // Hybrid form: the image-free prefix followed by the image tail (seen on parts with an applied material).
        if (r.pos+3 <= r.data.size() && r.data.mid(r.pos, 3) == QByteArray::fromHex("010101")) { imageTail(); return; }
        r.expect("000001010500010100");
        for (int i=0;i<19;++i) r.scalar();
        r.expect("0100"); return;
    }
    need(r.byte() == 1, "graph attribute prefix");
    for (int i = 0; i < 15; ++i) r.scalar();
    imageTail();
}
}

Geometry RepresentationReader::read(const QByteArray& member, const Options& options) {
    const auto entries = compound(member, options);
    const bool hasSkeleton=std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.name==QLatin1String("Skeleton")&&e.kind==2;});
    if (!hasSkeleton) {
        const bool hasSurfaces=std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.name==QLatin1String("SurfacicReps");});
        const bool annotations=std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.name==QLatin1String("V4V5_FDT")&&e.kind==1;});
        need(!hasSurfaces&&annotations,"missing Skeleton without annotation stream");
        return {};
    }
    auto stream = [&](const QString& name) {
        const Entry* found = nullptr;
        for (const auto& e : entries) if (e.name == name && e.kind == 2) { need(!found, "ambiguous logical stream"); found = &e; }
        need(found != nullptr, "missing logical stream"); return join(member, *found);
    };
    const QByteArray skeleton = stream(QStringLiteral("Skeleton")); Reader r(skeleton);
    Geometry geometry;
    struct Node { std::vector<qsizetype> children; qint64 body = -1; };
    std::map<qsizetype, Node> graph;
    std::map<unsigned, qsizetype> cache;
    std::set<unsigned> active;
    std::vector<unsigned> surfaceOffsets;
    std::function<void(int)> node = [&](int depth) {
        options.check(); need(depth <= 128 && graph.size() < 100000, "Skeleton graph limit");
        const qsizetype start = r.pos; const unsigned tag = r.byte(); graph[start] = Node{};
        if (tag == 0x62 || tag == 0xa3) {
            const qsizetype spanStart = r.pos;
            const unsigned span = tag == 0xa3 ? r.count() : 0;
            r.raw(35); attributes(r, r.count(std::numeric_limits<quint32>::max())); const unsigned count = r.count(100000);
            for (unsigned i = 0; i < count; ++i) {
                const unsigned mode = r.byte(); qsizetype target;
                if (mode == 0) { target = r.pos; node(depth+1); }
                else if (mode == 1) {
                    const unsigned index = r.count();
                    if (cache.count(index)) { need(!active.count(index), "cyclic cached node"); target = cache.at(index); }
                    else { target = r.pos; cache[index] = target; active.insert(index); node(depth+1); active.erase(index); }
                } else { fail(QStringLiteral("graph child mode")); }
                graph[start].children.push_back(target);
            }
            if (tag == 0xa3) {
                need(r.scalar() == 1.0f, "non-unit extended group scalar");
                need(quint64(r.pos) == quint64(spanStart)+span+1, "extended group span");
            }
        } else if (tag == 0x95) {
            const qsizetype spanStart = r.pos; const unsigned span = r.count(); r.raw(35); r.expect("ffff020001");
            Body body = mesh(r, options, qsizetype(quint64(spanStart)+span)); body.offset = start;
            need(quint64(r.pos) == quint64(spanStart)+span, "mesh leaf span");
            surfaceOffsets.push_back(r.count(128*1024*1024));
            graph[start].body = geometry.bodies.size(); geometry.bodies.push_back(std::move(body));
        } else fail(QStringLiteral("graph record 0x%1 at %2").arg(tag,0,16).arg(start));
    };
    try {
        node(0); need(r.pos == skeleton.size() && r.reservoir == 0, "unconsumed Skeleton bytes/bits");
    } catch (const Error& e) {
        if (options.cancelled && options.cancelled()) throw;
        // Structural context for remote triage: stream offset and a few raw bytes around it (a few dozen bytes; no
        // geometry is decoded here), so an unrecognized exporter variant can be reported without sharing the model.
        const qsizetype from = std::max<qsizetype>(0, r.pos-24), to = std::min<qsizetype>(skeleton.size(), r.pos+8);
        throw Error(QStringLiteral("%1 [Skeleton offset 0x%2 of %3; bytes around it: %4]").arg(QString::fromUtf8(e.what())).arg(r.pos, 0, 16)
            .arg(skeleton.size()).arg(QString::fromLatin1(skeleton.mid(from, to-from).toHex(' '))));
    }
    if (!surfaceOffsets.empty()) {
        const QByteArray surfaces = stream(QStringLiteral("SurfacicReps")); Reader surface(surfaces);
        std::sort(surfaceOffsets.begin(),surfaceOffsets.end()); surfaceOffsets.erase(std::unique(surfaceOffsets.begin(),surfaceOffsets.end()),surfaceOffsets.end());
        for (unsigned offset : surfaceOffsets) {
            need(surface.pos == offset, "surface block partition"); unsigned size = surface.count(128*1024*1024);
            need(size >= unsigned(surface.pos-offset) && quint64(offset)+size <= quint64(surfaces.size()), "surface block size"); surface.pos = offset+size;
        }
        need(surface.pos == surfaces.size(), "unreferenced surface bytes");
    }
    size_t expanded = 0;
    std::function<void(qsizetype, const QString&, int)> expand = [&](qsizetype offset, const QString& path, int depth) {
        options.check(); need(depth <= 128 && ++expanded <= 100000, "expanded graph limit");
        const auto& item = graph.at(offset);
        if (item.body >= 0) geometry.occurrences.push_back({static_cast<size_t>(item.body),path});
        for (size_t i = 0; i < item.children.size(); ++i) expand(item.children[i], path.isEmpty() ? QString::number(i) : path+"_"+QString::number(i), depth+1);
    };
    expand(0,{},0); return geometry;
}
}
