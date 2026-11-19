// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "InventorPartSolid.h"

#include "InventorCompoundFile.h"
#include "InventorPartSegment.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepLib.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Circle.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_Plane.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Precision.hxx>
#include <ShapeFix_Edge.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Wire.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <QtEndian>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Inventor {
namespace {

constexpr qsizetype SegmentLimit = 64 * 1024 * 1024;
constexpr std::size_t RecordLimit = 250'000;
constexpr std::size_t FieldLimit = 512;
constexpr std::size_t TotalFieldLimit = 4'000'000;
constexpr double CheckToleranceMm = 1e-5;
constexpr double TwoPi = 6.2831853071795864769;

[[noreturn]] void fail(const char* message)
{
    throw std::runtime_error(message);
}

void checkCancellation(const std::function<bool()>& cancelled)
{
    if (cancelled && cancelled())
        fail("Inventor import cancelled");
}

std::string validationErrors(const TopoDS_Shape& shape, const BRepCheck_Analyzer& checker)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> subshapes;
    TopExp::MapShapes(shape, subshapes);
    std::ostringstream result;
    std::unordered_set<int> seen;
    const auto report = [&](const auto& statuses) {
        for (const auto status : statuses) {
            if (status != BRepCheck_NoError && seen.insert(status).second) {
                result << (seen.size() == 1 ? ": " : ", ");
                std::ostringstream name;
                BRepCheck::Print(status, name);
                result << name.str().substr(0, name.str().find_first_of("\r\n"));
            }
        }
    };
    for (int i = 1; i <= subshapes.Extent(); ++i) {
        const auto& checked = checker.Result(subshapes(i));
        if (checked.IsNull())
            continue;
        report(checked->Status());
        for (checked->InitContextIterator(); checked->MoreShapeInContext(); checked->NextShapeInContext())
            report(checked->StatusOnShape());
    }
    return result.str();
}

struct Link { qint32 index; };
using Triple = std::array<double, 3>;
using Field = std::variant<std::monostate, Link, Triple, bool, double>;

struct Record {
    QByteArray type;
    std::vector<Field> fields;

    template<typename T> const T& get(std::size_t index) const
    {
        if (index < fields.size()) {
            if (const T* value = std::get_if<T>(&fields[index]))
                return *value;
        }
        fail("Unsupported Inventor ASM record layout");
    }

    qint32 link(std::size_t index) const { return get<Link>(index).index; }

    double number(std::size_t index) const
    {
        const double value = get<double>(index);
        if (!std::isfinite(value))
            fail("Invalid Inventor ASM real value");
        return value;
    }
};

class SavedBodyReader
{
public:
    SavedBodyReader(const QByteArray& bytes, const std::function<bool()>& cancelled)
        : _bytes(bytes), _cancelled(cancelled) {}

    double millimeters = 0;

    std::vector<Record> read()
    {
        static const QByteArray magic("ASM BinaryFile4");
        const qsizetype start = _bytes.indexOf(magic);
        if (start < 0 || _bytes.indexOf(magic, start + magic.size()) >= 0)
            fail("Expected a single saved ASM model in Inventor part");
        _offset = start + magic.size();

        // BinaryFile4 header, followed by tagged program, kernel and timestamp strings.
        skip(16);
        for (int i = 0; i < 3; ++i) {
            if (integer<quint8>() != 0x07)
                fail("Unsupported Inventor ASM header strings");
            skip(integer<quint8>());
        }
        millimeters = headerReal();
        headerReal(); // spatial resolution
        headerReal(); // normal resolution
        if (!std::isfinite(millimeters) || millimeters <= 0)
            fail("Invalid Inventor ASM length scale");

        std::vector<Record> records;
        std::size_t fields = 0;
        for (;;) {
            checkCancellation(_cancelled);
            QByteArray type = recordType();
            // The pre-history records describe the saved current state in this profile.
            if (type == "Begin-of-ASM-History-Data" || type == "End-of-ASM-data")
                break;
            if (records.size() == RecordLimit)
                fail("Inventor ASM record limit exceeded");

            Record& record = records.emplace_back();
            record.type = std::move(type);
            for (quint8 tag = integer<quint8>(); tag != 0x11; tag = integer<quint8>()) {
                if (record.fields.size() == FieldLimit)
                    fail("Inventor ASM field limit exceeded");
                if (++fields > TotalFieldLimit)
                    fail("Inventor ASM total field limit exceeded");
                record.fields.push_back(field(tag));
            }
        }
        if (records.empty() || records.front().type != "asmheader")
            fail("Missing Inventor ASM header record");
        return records;
    }

private:
    const char* take(qsizetype bytes)
    {
        if (bytes < 0 || bytes > _bytes.size() - _offset)
            fail("Truncated Inventor ASM payload");
        const char* result = _bytes.constData() + _offset;
        _offset += bytes;
        return result;
    }

    void skip(qsizetype bytes) { take(bytes); }

    template<typename T> T integer() { return qFromLittleEndian<T>(take(sizeof(T))); }

    double real()
    {
        const quint64 bits = integer<quint64>();
        double value;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double single()
    {
        const quint32 bits = integer<quint32>();
        float value;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double headerReal()
    {
        if (integer<quint8>() != 0x06)
            fail("Unsupported Inventor ASM header scale fields");
        return real();
    }

    QByteArray recordType()
    {
        QByteArray type;
        for (int component = 0; component < 8; ++component) {
            const auto tag = integer<quint8>();
            if (tag != 0x0d && tag != 0x0e)
                fail("Missing Inventor ASM record type");
            const auto length = integer<quint8>();
            if (length == 0)
                fail("Empty Inventor ASM record type");
            if (component != 0)
                type += '-';
            type.append(take(length), length);
            if (tag == 0x0d)
                return type;
        }
        fail("Inventor ASM record type has too many components");
    }

    Field field(quint8 tag)
    {
        switch (tag) {
        case 0x02: skip(1); break;
        case 0x03: skip(2); break;
        case 0x04: case 0x15: skip(4); break;
        case 0x05: return single();
        case 0x06: return real();
        case 0x07: case 0x0d: case 0x0e: case 0x12: skip(integer<quint8>()); break;
        case 0x08: skip(integer<quint16>()); break;
        case 0x09: skip(integer<quint32>()); break;
        case 0x0a: return true;
        case 0x0b: return false;
        case 0x0c: return Link{integer<qint32>()};
        case 0x0f: case 0x10: break;
        case 0x13: case 0x14: return Triple{real(), real(), real()};
        case 0x16: skip(16); break;
        default: fail("Unsupported Inventor ASM field tag");
        }
        return {};
    }

    const QByteArray& _bytes;
    const std::function<bool()>& _cancelled;
    qsizetype _offset = 0;
};

class SolidBuilder
{
public:
    SolidBuilder(const std::vector<Record>& records, double scale,
                 const std::function<bool()>& cancelled)
        : _records(records), _scale(scale), _cancelled(cancelled) {}

    std::vector<TopoDS_Solid> build()
    {
        findCylindricalBands();
        std::vector<TopoDS_Solid> solids;
        for (const auto& record : _records) {
            if (record.type == "body") {
                checkCancellation(_cancelled);
                solids.push_back(buildBody(record));
            }
        }
        if (solids.empty())
            fail("Inventor part has no saved solid bodies");
        return solids;
    }

private:
    TopoDS_Solid buildBody(const Record& body)
    {
        if (body.link(4) != -1 || body.link(5) != -1)
            fail("Unsupported Inventor body transform or wire geometry");
        const Record& lump = get(body.link(3), "lump");
        const Record& shell = get(lump.link(4), "shell");
        if (lump.link(3) != -1 || shell.link(3) != -1 || shell.link(4) != -1 || shell.link(6) != -1)
            fail("Inventor import currently supports one shell without wire geometry");

        std::vector<qint32> faces;
        std::unordered_set<qint32> visited;
        for (qint32 id = shell.link(5); id != -1;) {
            checkCancellation(_cancelled);
            if (!visited.insert(id).second)
                fail("Cyclic Inventor face list");
            faces.push_back(id);
            id = get(id, "face").link(3);
        }
        if (faces.empty())
            fail("Inventor shell has no faces");
        const TangentCut cut = findTangentCut(faces);
        if (cut.omittedLoops.empty())
            return sewSolid(faces, {}, false);

        const TopoDS_Solid outside = sewSolid(cut.outside, cut.omittedLoops, false);
        const TopoDS_Solid cavity = sewSolid(cut.cavity, {}, true);
        // Confirm containment without modifying either input shell.
        NCollection_List<TopoDS_Shape> arguments, tools;
        arguments.Append(cavity);
        tools.Append(outside);
        BRepAlgoAPI_Cut remainder;
        remainder.SetArguments(arguments);
        remainder.SetTools(tools);
        remainder.SetNonDestructive(true);
        checkCancellation(_cancelled);
        remainder.Build();
        checkCancellation(_cancelled);
        if (!remainder.IsDone() || remainder.HasErrors() || remainder.Shape().IsNull() ||
            TopExp_Explorer(remainder.Shape(), TopAbs_SOLID).More())
            fail("Inventor tangent cavity is not contained by its exterior shell");

        const TopoDS_Shell outerShell = TopoDS::Shell(TopoDS_Iterator(outside).Value());
        const TopoDS_Shell innerShell = TopoDS::Shell(TopoDS_Iterator(cavity).Value().Reversed());
        const TopoDS_Solid solid = BRepBuilderAPI_MakeSolid(outerShell, innerShell).Solid();
        validateSolid(solid);
        return solid;
    }

    struct TangentCut {
        std::unordered_set<qint32> omittedLoops;
        std::vector<qint32> outside;
        std::vector<qint32> cavity;
    };
    TangentCut findTangentCut(const std::vector<qint32>& faces) const;
    bool isTangentContact(qint32 faceId, qint32 edgeId, qint32 cylinderId) const;

    TopoDS_Solid sewSolid(const std::vector<qint32>& faces, const std::unordered_set<qint32>& omittedLoops,
                          bool repairCavity)
    {
        _edgeUses.clear();
        BRepBuilderAPI_Sewing sewing(CheckToleranceMm);
        for (const qint32 id : faces) {
            checkCancellation(_cancelled);
            try {
                sewing.Add(buildFace(get(id, "face"), id, omittedLoops));
            } catch (const std::exception& error) {
                throw std::runtime_error("Inventor ASM face " + std::to_string(id) + ": " + error.what());
            }
        }
        for (const auto& use : _edgeUses) {
            if (use.second != 3)
                fail("Inventor shell has an unpaired or non-manifold edge");
        }

        sewing.Perform();
        const TopoDS_Shape& sewn = sewing.SewedShape();
        if (sewn.IsNull() || sewn.ShapeType() != TopAbs_SHELL ||
            sewing.NbFreeEdges() != 0 || sewing.NbMultipleEdges() != 0)
            fail("Inventor faces did not form one closed OCCT shell");
        BRepLib::SameParameter(sewn, CheckToleranceMm, true);
        TopoDS_Solid solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewn)).Solid();
        if (solid.IsNull() || !BRepLib::OrientClosedSolid(solid))
            fail("Inventor shape could not be oriented as an OCCT solid");
        if (repairCavity && !BRepCheck_Analyzer(solid).IsValid()) {
            ShapeFix_Shape fix(solid);
            fix.SetPrecision(CheckToleranceMm);
            fix.SetMaxTolerance(CheckToleranceMm);
            fix.FixVertexPositionMode() = 0;
            const auto& wireFix = fix.FixWireTool();
            wireFix->ModifyTopologyMode() = false;
            wireFix->ModifyGeometryMode() = false;
            fix.Perform();
            if (fix.Shape().ShapeType() != TopAbs_SOLID || fix.Shape().NbChildren() != 1)
                fail("Inventor tangent cavity repair changed the shell count");
            solid = TopoDS::Solid(fix.Shape());
            if (!BRepLib::OrientClosedSolid(solid))
                fail("Inventor tangent cavity could not be oriented");
        }
        validateSolid(solid);
        return solid;
    }

    void validateSolid(const TopoDS_Solid& solid) const
    {
        const BRepCheck_Analyzer checker(solid);
        if (!checker.IsValid())
            throw std::runtime_error("Inventor shape failed OCCT solid validation" + validationErrors(solid, checker));
    }

    const Record& get(qint32 id) const
    {
        if (id < 0 || static_cast<std::size_t>(id) >= _records.size())
            fail("Inventor ASM link points outside the saved body records");
        return _records[id];
    }

    const Record& get(qint32 id, const char* type) const
    {
        const Record& record = get(id);
        if (record.type != type)
            throw std::runtime_error("Expected Inventor " + std::string(type) + ", found " + record.type.toStdString());
        return record;
    }

    gp_Pnt point(const Triple& xyz) const
    {
        const Triple scaled{xyz[0] * _scale, xyz[1] * _scale, xyz[2] * _scale};
        for (double value : scaled) {
            if (!std::isfinite(value))
                fail("Invalid Inventor ASM coordinate");
        }
        return {scaled[0], scaled[1], scaled[2]};
    }

    gp_Dir direction(const Triple& xyz) const
    {
        const double magnitude = std::hypot(xyz[0], xyz[1], xyz[2]);
        if (!std::isfinite(magnitude) || magnitude < Precision::Confusion())
            fail("Invalid Inventor ASM direction");
        return {xyz[0] / magnitude, xyz[1] / magnitude, xyz[2] / magnitude};
    }

    gp_Vec vector(const Triple& xyz) const
    {
        const gp_Pnt scaled = point(xyz);
        return {scaled.X(), scaled.Y(), scaled.Z()};
    }

    double length(double value) const
    {
        const double scaled = value * _scale;
        if (!std::isfinite(scaled))
            fail("Invalid Inventor ASM length");
        return scaled;
    }

    TopoDS_Vertex vertex(qint32 id, std::unordered_map<qint32, TopoDS_Vertex>& vertices) const;
    TopoDS_Edge buildEdge(qint32 id, std::unordered_map<qint32, TopoDS_Vertex>& vertices) const;

    struct Surface {
        Handle(Geom_Surface) geometry;
        bool inverted = false;
    };
    struct FaceEdge {
        TopoDS_Edge shape;
        unsigned uses = 0;
    };
    struct CylinderBand {
        Handle(Geom_CylindricalSurface) surface;
        double firstV;
        double lastV;
    };
    Surface buildSurface(const Record& surface) const;
    void findCylindricalBands();

    TopoDS_Face buildFace(const Record& face, qint32 faceId, const std::unordered_set<qint32>& omittedLoops)
    {
        if (face.get<bool>(9) || face.link(4) < 0)
            fail("Inventor face uses unsupported unbounded or sheet geometry");
        const Surface support = buildSurface(get(face.link(7)));
        BRep_Builder builder;
        TopoDS_Face result;
        builder.MakeFace(result, support.geometry, Precision::Confusion());
        std::unordered_map<qint32, TopoDS_Vertex> vertices;
        std::unordered_map<qint32, FaceEdge> edges;
        std::unordered_set<qint32> visited;
        unsigned loopCount = 0;
        TopoDS_Wire firstWire;
        for (qint32 loopId = face.link(4); loopId != -1;) {
            if (!visited.insert(loopId).second)
                fail("Cyclic Inventor loop list");
            const Record& loop = get(loopId, "loop");
            if (loop.link(5) != faceId)
                fail("Inventor loop belongs to a different face");
            if (omittedLoops.count(loopId) == 0) {
                const TopoDS_Wire wire = buildWire(loop, loopId, edges, vertices);
                if (firstWire.IsNull())
                    firstWire = wire;
                builder.Add(result, wire);
                ++loopCount;
            }
            loopId = loop.link(3);
        }
        if (firstWire.IsNull())
            fail("Inventor face has no remaining boundary");
        if (const auto found = _cylinderBands.find(faceId); found != _cylinderBands.end()) {
            const CylinderBand& band = found->second;
            BRepBuilderAPI_MakeFace faceBuilder(band.surface, 0.0, TwoPi, band.firstV, band.lastV, CheckToleranceMm);
            if (!faceBuilder.IsDone())
                fail("Could not add the seam of a closed Inventor cylindrical band");
            result = faceBuilder.Face();
            if (face.get<bool>(8) != support.inverted)
                result.Reverse();
            return result;
        }
        const Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(support.geometry);
        if (plane.IsNull()) {
            ShapeFix_Edge edgeFix;
            for (const auto& item : edges) {
                const FaceEdge& edge = item.second;
                edgeFix.FixAddPCurve(edge.shape, result, edge.uses == 2, CheckToleranceMm);
                double first, last;
                if (BRep_Tool::CurveOnSurface(edge.shape, result, first, last).IsNull())
                    fail("Could not project an Inventor edge onto its OCCT surface");
            }
        } else {
            for (const auto& item : edges) {
                double first, last;
                const Handle(Geom_Curve) curve = BRep_Tool::Curve(item.second.shape, first, last);
                for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    if (!(plane->Pln().Distance(curve->Value(first + fraction * (last - first))) <= CheckToleranceMm))
                        fail("Inventor edge does not lie on its saved planar face");
                }
            }
            if (loopCount == 1) {
                BRepBuilderAPI_MakeFace planarFace(plane->Pln(), firstWire, true);
                if (!planarFace.IsDone())
                    fail("Could not construct the saved planar Inventor face");
                result = planarFace.Face();
                if (face.get<bool>(8))
                    result.Reverse();
                return result;
            }
        }

        ShapeFix_Face faceFix(result);
        faceFix.SetPrecision(CheckToleranceMm);
        faceFix.SetMaxTolerance(CheckToleranceMm);
        faceFix.FixAddNaturalBoundMode() = 0;
        faceFix.FixSmallAreaWireMode() = 0;
        faceFix.FixIntersectingWiresMode() = 0;
        faceFix.FixLoopWiresMode() = 0;
        faceFix.FixSplitFaceMode() = 0;
        faceFix.Perform();
        const TopoDS_Shape fixed = faceFix.Result();
        if (fixed.IsNull() || fixed.ShapeType() != TopAbs_FACE)
            fail("Could not construct a single trimmed Inventor face");
        result = TopoDS::Face(fixed);
        if (face.get<bool>(8) != support.inverted)
            result.Reverse();
        return result;
    }

    TopoDS_Wire buildWire(const Record& loop, qint32 loopId, std::unordered_map<qint32, FaceEdge>& edges,
                          std::unordered_map<qint32, TopoDS_Vertex>& vertices)
    {
        BRep_Builder builder;
        TopoDS_Wire wire;
        builder.MakeWire(wire);
        std::unordered_set<qint32> visited;
        const qint32 first = loop.link(4);
        qint32 id = first;
        qint32 initialVertex = -1;
        qint32 endVertex = -1;
        do {
            if (!visited.insert(id).second)
                fail("Cyclic Inventor face boundary");
            const Record& coedge = get(id, "coedge");
            if (coedge.link(8) != loopId)
                fail("Inventor coedge belongs to a different loop");
            const qint32 edgeId = coedge.link(6);
            const Record& edge = get(edgeId, "edge");
            const bool reversed = coedge.get<bool>(7);
            const unsigned sense = reversed ? 2 : 1;
            unsigned& use = _edgeUses[edgeId];
            if ((use & sense) != 0)
                fail("Inventor shell has a non-manifold edge or inconsistent edge orientation");
            use |= sense;

            const qint32 startVertex = edge.link(reversed ? 5 : 3);
            if (id == first)
                initialVertex = startVertex;
            else if (endVertex != startVertex)
                fail("Inventor face boundary has disconnected edges");
            endVertex = edge.link(reversed ? 3 : 5);

            auto [found, inserted] = edges.try_emplace(edgeId);
            FaceEdge& local = found->second;
            if (inserted)
                local.shape = buildEdge(edgeId, vertices);
            ++local.uses;
            builder.Add(wire, local.shape.Oriented(reversed ? TopAbs_REVERSED : TopAbs_FORWARD));
            id = coedge.link(3);
        } while (id != first);

        if (initialVertex != endVertex)
            fail("Inventor face boundary is not closed");
        wire.Closed(true);
        return wire;
    }

    const std::vector<Record>& _records;
    double _scale;
    const std::function<bool()>& _cancelled;
    std::unordered_map<qint32, unsigned> _edgeUses;
    std::unordered_map<qint32, CylinderBand> _cylinderBands;
    std::unordered_map<qint32, gp_Dir> _circleSeams;
};

SolidBuilder::TangentCut SolidBuilder::findTangentCut(const std::vector<qint32>& faces) const
{
    struct Use {
        qint32 face;
        qint32 loop;
        qint32 coedge;
    };
    std::unordered_map<qint32, std::vector<Use>> edgeUses;
    for (const qint32 faceId : faces) {
        checkCancellation(_cancelled);
        std::unordered_set<qint32> visited;
        for (qint32 loopId = get(faceId, "face").link(4); loopId != -1;) {
            const Record& loop = get(loopId, "loop");
            if (!visited.insert(loopId).second || loop.link(5) != faceId)
                fail("Invalid Inventor face loop list");
            const qint32 first = loop.link(4);
            qint32 id = first;
            do {
                const Record& coedge = get(id, "coedge");
                if (!visited.insert(id).second || coedge.link(8) != loopId)
                    fail("Invalid Inventor face coedge list");
                edgeUses[coedge.link(6)].push_back({faceId, loopId, id});
                id = coedge.link(3);
            } while (id != first);
            loopId = loop.link(3);
        }
    }

    TangentCut cut;
    std::vector<std::pair<qint32, qint32>> contacts;
    constexpr auto unsupported = "Unsupported Inventor non-manifold edge at tangent contact";
    for (auto& [edgeId, uses] : edgeUses) {
        if (uses.size() <= 2)
            continue;
        if (uses.size() != 4)
            fail(unsupported);
        // Uses in each loop were collected consecutively in saved boundary order.
        const auto slit = std::adjacent_find(uses.begin(), uses.end(), [&](const Use& a, const Use& b) {
            if (a.loop != b.loop)
                return false;
            const Record& first = get(a.coedge, "coedge");
            const Record& second = get(b.coedge, "coedge");
            return first.link(3) == b.coedge && second.link(3) == a.coedge &&
                   first.get<bool>(7) != second.get<bool>(7);
        });
        if (slit == uses.end())
            fail(unsupported);
        const Use planar = *slit;
        uses.erase(slit, slit + 2);
        const qint32 support = get(uses[0].face, "face").link(7);
        if (uses[0].face == uses[1].face || support != get(uses[1].face, "face").link(7) ||
            !isTangentContact(planar.face, edgeId, support))
            fail(unsupported);
        cut.omittedLoops.insert(planar.loop);
        contacts.emplace_back(planar.face, uses[0].face);
    }
    if (contacts.empty())
        return cut;

    std::unordered_map<qint32, std::vector<qint32>> neighbors;
    for (const auto& [edgeId, uses] : edgeUses) {
        if (uses.size() != 2)
            fail("Inventor tangent cut has an unpaired edge");
        neighbors[uses[0].face].push_back(uses[1].face);
        neighbors[uses[1].face].push_back(uses[0].face);
    }
    const auto connectedFaces = [&](qint32 root) {
        std::unordered_set<qint32> connected{root};
        std::vector<qint32> pending{root};
        while (!pending.empty()) {
            checkCancellation(_cancelled);
            const qint32 face = pending.back();
            pending.pop_back();
            for (const qint32 neighbor : neighbors[face]) {
                if (connected.insert(neighbor).second)
                    pending.push_back(neighbor);
            }
        }
        return connected;
    };
    const auto outside = connectedFaces(contacts.front().first);
    const auto cavity = connectedFaces(contacts.front().second);
    for (const auto& [outerFace, cavityFace] : contacts) {
        if (outside.count(outerFace) == 0 || cavity.count(cavityFace) == 0 || outside.count(cavityFace) != 0)
            fail("Inventor tangent cut could not be separated into an exterior and one cavity");
    }
    if (outside.size() + cavity.size() != faces.size())
        fail("Inventor tangent cut has additional disconnected faces");
    for (const qint32 face : faces) {
        if (outside.count(face) != 0)
            cut.outside.push_back(face);
        else
            cut.cavity.push_back(face);
    }
    return cut;
}

bool SolidBuilder::isTangentContact(qint32 faceId, qint32 edgeId, qint32 cylinderId) const
{
    const Record& plane = get(get(faceId, "face").link(7));
    const Record& cylinder = get(cylinderId);
    const Record& curve = get(get(edgeId, "edge").link(8));
    if (plane.type != "plane-surface" || cylinder.type != "cone-surface" ||
        std::abs(cylinder.number(9)) >= 1e-12 || curve.type != "straight-curve")
        return false;
    const gp_Pln planar = Handle(Geom_Plane)::DownCast(buildSurface(plane).geometry)->Pln();
    const auto cylindrical = Handle(Geom_CylindricalSurface)::DownCast(buildSurface(cylinder).geometry);
    const gp_Ax1 axis = cylindrical->Axis();
    const gp_Pnt lineOrigin = point(curve.get<Triple>(3));
    const gp_Dir lineDirection = direction(curve.get<Triple>(4));
    return lineDirection.IsParallel(axis.Direction(), Precision::Angular()) &&
           lineDirection.IsNormal(planar.Axis().Direction(), Precision::Angular()) &&
           planar.Distance(lineOrigin) <= CheckToleranceMm &&
           std::abs(gp_Lin(axis).Distance(lineOrigin) - cylindrical->Radius()) <= CheckToleranceMm &&
           std::abs(planar.Distance(axis.Location()) - cylindrical->Radius()) <= CheckToleranceMm;
}

void SolidBuilder::findCylindricalBands()
{
    std::unordered_map<qint32, unsigned> vertexEdges;
    std::unordered_map<qint32, unsigned> closedLoops;
    for (const Record& record : _records) {
        if (record.type == "edge") {
            ++vertexEdges[record.link(3)];
            if (record.link(3) != record.link(5))
                ++vertexEdges[record.link(5)];
        } else if (record.type == "loop" && record.link(4) >= 0) {
            const Record& coedge = get(record.link(4));
            if (coedge.type == "coedge" && coedge.link(3) == record.link(4))
                ++closedLoops[coedge.link(6)];
        }
    }

    for (std::size_t faceId = 0; faceId < _records.size(); ++faceId) {
        const Record& face = _records[faceId];
        if (face.type != "face" || face.link(7) < 0 || face.link(4) < 0 || face.get<bool>(9))
            continue;
        const Record& support = get(face.link(7));
        if (support.type != "cone-surface" || std::abs(support.number(9)) > 1e-12 ||
            std::abs(support.number(6) - 1.0) > 1e-9)
            continue;
        const Record& lower = get(face.link(4), "loop");
        if (lower.link(3) < 0)
            continue;
        const Record& upper = get(lower.link(3), "loop");
        if (upper.link(3) != -1)
            continue;

        const gp_Pnt origin = point(support.get<Triple>(3));
        const gp_Dir axis = direction(support.get<Triple>(4));
        const double radius = vector(support.get<Triple>(5)).Magnitude();
        const gp_Dir seam = gp_Ax2(origin, axis).XDirection();
        qint32 edgeIds[2];
        double axial[2];
        bool usable = true;
        for (unsigned i = 0; i < 2; ++i) {
            const Record& loop = i == 0 ? lower : upper;
            const Record& coedge = get(loop.link(4), "coedge");
            const qint32 edgeId = coedge.link(6);
            if (coedge.link(3) != loop.link(4) || closedLoops[edgeId] != 2) {
                usable = false;
                break;
            }
            const Record& edge = get(edgeId, "edge");
            if (edge.link(8) < 0 || edge.link(3) != edge.link(5) || vertexEdges[edge.link(3)] != 1 ||
                std::abs(edge.number(6) - edge.number(4) - TwoPi) > Precision::PConfusion()) {
                usable = false;
                break;
            }
            const Record& circle = get(edge.link(8));
            if (circle.type != "ellipse-curve" || circle.number(6) != 1.0 ||
                std::abs(vector(circle.get<Triple>(5)).Magnitude() - radius) > CheckToleranceMm ||
                !axis.IsParallel(direction(circle.get<Triple>(4)), Precision::Angular())) {
                usable = false;
                break;
            }
            const auto prior = _circleSeams.find(edgeId);
            if (prior != _circleSeams.end() && !seam.IsEqual(prior->second, Precision::Angular())) {
                usable = false;
                break;
            }
            const gp_Vec offset(origin, point(circle.get<Triple>(3)));
            axial[i] = offset.Dot(gp_Vec(axis));
            if ((offset - gp_Vec(axis) * axial[i]).Magnitude() > CheckToleranceMm) {
                usable = false;
                break;
            }
            edgeIds[i] = edgeId;
        }
        if (!usable || edgeIds[0] == edgeIds[1] || std::abs(axial[0] - axial[1]) <= CheckToleranceMm)
            continue;
        // These two trims are whole circles. Moving their closure vertices to
        // the same meridian preserves geometry and gives OCCT a valid seam.
        _circleSeams.emplace(edgeIds[0], seam);
        _circleSeams.emplace(edgeIds[1], seam);
        const gp_Ax3 axes(origin, axis, seam);
        _cylinderBands.emplace(static_cast<qint32>(faceId), CylinderBand{
            new Geom_CylindricalSurface(axes, radius), std::min(axial[0], axial[1]), std::max(axial[0], axial[1])});
    }
}

TopoDS_Vertex SolidBuilder::vertex(qint32 id, std::unordered_map<qint32, TopoDS_Vertex>& vertices) const
{
    auto [found, inserted] = vertices.try_emplace(id);
    if (inserted) {
        const Record& record = get(get(id, "vertex").link(5), "point");
        found->second = BRepBuilderAPI_MakeVertex(point(record.get<Triple>(3))).Vertex();
    }
    return found->second;
}

TopoDS_Edge SolidBuilder::buildEdge(qint32 id, std::unordered_map<qint32, TopoDS_Vertex>& vertices) const
{
    const Record& edge = get(id, "edge");
    const Record& curve = get(edge.link(8));
    TopoDS_Vertex start = vertex(edge.link(3), vertices);
    TopoDS_Vertex finish = vertex(edge.link(5), vertices);
    const gp_Pnt startPoint = BRep_Tool::Pnt(start);
    const gp_Pnt endPoint = BRep_Tool::Pnt(finish);
    Handle(Geom_Curve) geometry;
    double first = edge.number(4);
    double last = edge.number(6);
    if (curve.type == "straight-curve") {
        geometry = new Geom_Line(point(curve.get<Triple>(3)), direction(curve.get<Triple>(4)));
        first = length(first);
        last = length(last);
    } else if (curve.type == "ellipse-curve") {
        const gp_Vec majorAxis = vector(curve.get<Triple>(5));
        const double major = majorAxis.Magnitude();
        const double minor = major * curve.number(6);
        if (!std::isfinite(minor) || minor <= Precision::Confusion() || minor > major)
            fail("Invalid Inventor ellipse radii");
        const gp_Ax2 axes(point(curve.get<Triple>(3)), direction(curve.get<Triple>(4)), gp_Dir(majorAxis));
        if (minor == major)
            geometry = new Geom_Circle(axes, major);
        else
            geometry = new Geom_Ellipse(axes, major, minor);
    } else {
        throw std::runtime_error("Unsupported Inventor edge curve: " + curve.type.toStdString());
    }

    if (edge.get<bool>(9))
        geometry->Reverse();
    if (!(first < last) || (geometry->IsPeriodic() && last - first > geometry->Period() + Precision::PConfusion()) ||
        !(geometry->Value(first).Distance(startPoint) <= CheckToleranceMm) ||
        !(geometry->Value(last).Distance(endPoint) <= CheckToleranceMm))
        fail("Inventor edge endpoints disagree with the saved trim parameters");

    if (const auto seam = _circleSeams.find(id); seam != _circleSeams.end()) {
        const Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(geometry);
        const gp_Ax2 axes(circle->Location(), circle->Axis().Direction(), seam->second);
        geometry = new Geom_Circle(axes, circle->Radius());
        start = BRepBuilderAPI_MakeVertex(geometry->Value(0.0)).Vertex();
        finish = start;
        first = 0.0;
        last = TwoPi;
    }

    BRepBuilderAPI_MakeEdge make(geometry, start, finish, first, last);
    if (!make.IsDone())
        fail("Could not construct a trimmed Inventor edge in OCCT");
    return make.Edge();
}

SolidBuilder::Surface SolidBuilder::buildSurface(const Record& surface) const
{
    if (surface.type == "plane-surface") {
        const gp_Pnt origin = point(surface.get<Triple>(3));
        const gp_Ax3 axes(origin, direction(surface.get<Triple>(4)), direction(surface.get<Triple>(5)));
        return {new Geom_Plane(axes)};
    }
    if (surface.type == "cone-surface") {
        const double ratio = surface.number(6);
        const double sine = surface.number(9);
        const double cosine = surface.number(10);
        const gp_Vec radial = vector(surface.get<Triple>(5));
        const double radius = radial.Magnitude();
        if (std::abs(ratio - 1) > 1e-9 || !std::isfinite(radius) ||
            radius <= Precision::Confusion() || std::abs(cosine) < 1e-12)
            fail("Unsupported Inventor conical surface profile");
        const gp_Pnt origin = point(surface.get<Triple>(3));
        const gp_Ax3 axes(origin, direction(surface.get<Triple>(4)), gp_Dir(radial));
        if (std::abs(sine) < 1e-12)
            return {new Geom_CylindricalSurface(axes, radius), cosine < 0};
        return {new Geom_ConicalSurface(axes, std::atan(sine / cosine), radius), cosine < 0};
    }
    if (surface.type == "sphere-surface") {
        const gp_Pnt origin = point(surface.get<Triple>(3));
        const double radius = length(surface.number(4));
        if (std::abs(radius) <= Precision::Confusion())
            fail("Invalid Inventor sphere radius");
        const gp_Ax3 axes(origin, direction(surface.get<Triple>(6)), direction(surface.get<Triple>(5)));
        return {new Geom_SphericalSurface(axes, std::abs(radius)), radius < 0};
    }
    if (surface.type == "torus-surface") {
        const gp_Pnt origin = point(surface.get<Triple>(3));
        const double major = std::abs(length(surface.number(5)));
        const double minor = length(surface.number(6));
        if (std::abs(minor) <= Precision::Confusion() || major - std::abs(minor) <= Precision::Confusion())
            fail("Unsupported Inventor torus radii");
        const gp_Ax3 axes(origin, direction(surface.get<Triple>(4)), direction(surface.get<Triple>(7)));
        return {new Geom_ToroidalSurface(axes, major, std::abs(minor)), minor < 0};
    }
    throw std::runtime_error("Unsupported Inventor face surface: " + surface.type.toStdString());
}

} // namespace

std::vector<TopoDS_Solid> readPartSolids(const QString& path, const std::function<bool()>& cancelled)
{
    checkCancellation(cancelled);
    CompoundFile container(path);
    const QByteArray bytes = readPartBRepSegment(container, SegmentLimit);
    SavedBodyReader reader(bytes, cancelled);
    const auto records = reader.read();
    return SolidBuilder(records, reader.millimeters, cancelled).build();
}

} // namespace Inventor
