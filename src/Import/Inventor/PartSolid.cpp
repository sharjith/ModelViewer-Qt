// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "Inventor/PartSolid.h"

#include "Inventor/CompoundFile.h"
#include "Inventor/PartSegment.h"
#include "MemoryGuard.h"

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
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLProp_SLProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomConvert.hxx>
#include <GeomFill.hxx>
#include <Standard_Failure.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
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
#include <ShapeBuild_ReShape.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_HArray1.hxx>
#include <NCollection_Array2.hxx>
#include <TopExp.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <QtEndian>

#include <array>
#include <algorithm>
#include <BRepTools_WireExplorer.hxx>
#include <cmath>
#include <limits>
#include <cstring>
#include <functional>
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

// Absolute ceilings that only stop malformed input; the free-memory guard (MemoryGuard.h) bounds real files.
constexpr qsizetype SegmentLimit = qsizetype(4) * 1024 * 1024 * 1024;
constexpr std::size_t RecordLimit = 100'000'000;
constexpr std::size_t FieldLimit = 65'536;
constexpr std::size_t TotalFieldLimit = 2'000'000'000;
constexpr std::size_t MemoryCheckInterval = 65'536;
constexpr double CheckToleranceMm = 1e-5;
constexpr double MaxSplineToleranceMm = 0.05;
// Upper limit for a tolerance the file itself saves (vertex, edge, intersection curve): the largest on the oversized part is
// 0.41 mm. A measured residual of a fitted curve still has to stay within MaxSplineToleranceMm.
constexpr double MaxSavedToleranceMm = 0.5;
// A saved cone or cylinder whose two radii differ by no more than this is built as a circular one (see buildSurface).
constexpr double MaxCircularApproximationMm = 0.01;
constexpr double TwoPi = 6.2831853071795864769;

// Raised (only when not strict) by the plain single-shell path when a shell cannot become a solid but some of its faces
// were built. It carries those faces so the caller can still show them, marked incomplete.
struct IncompleteShell : std::runtime_error
{
    IncompleteShell(const TopoDS_Shape& faces, const QString& description, bool openSurface = false)
        : std::runtime_error(description.toStdString()), shape(faces), message(description), surface(openSurface) {}
    TopoDS_Shape shape;
    QString message;
    bool surface; // every face was built; the faces just do not enclose a volume (a sheet body)
};

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
enum class SplineToken { Open, Close, Reference, Polynomial, Rational, HelixCurve, HelixSurface };
using Triple = std::array<double, 3>;
using Field = std::variant<std::monostate, Link, Triple, bool, qint32, double, SplineToken>;

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
        // Older Inventor files save the same layout under the name of the original ACIS format.
        static const QByteArray magic("ASM BinaryFile4");
        static const QByteArray legacyMagic("ACIS BinaryFile");
        QByteArray name = magic;
        qsizetype start = _bytes.indexOf(name);
        if (start < 0) {
            name = legacyMagic;
            start = _bytes.indexOf(name);
        }
        if (start < 0 || _bytes.indexOf(name, start + name.size()) >= 0)
            fail("Expected a single saved ASM model in Inventor part");
        _offset = start + name.size();

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
            if (records.size() % MemoryCheckInterval == 0)
                Detail::requireFreeMemory("reading the saved model records");

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
        case 0x02: return static_cast<qint32>(integer<quint8>());
        case 0x03: return static_cast<qint32>(integer<qint16>());
        case 0x04: return integer<qint32>();
        case 0x15: return integer<qint32>();
        case 0x05: return single();
        case 0x06: return real();
        case 0x07: case 0x0d: case 0x0e: case 0x12: {
            const auto size = integer<quint8>();
            const QByteArrayView text(take(size), size);
            if (text == "ref") return SplineToken::Reference;
            if (text == "nubs") return SplineToken::Polynomial;
            if (text == "nurbs") return SplineToken::Rational;
            if (text == "helix_int_cur") return SplineToken::HelixCurve;
            if (text == "helix_spl_line") return SplineToken::HelixSurface;
            break;
        }
        case 0x08: skip(integer<quint16>()); break;
        case 0x09: skip(integer<quint32>()); break;
        case 0x0a: return true;
        case 0x0b: return false;
        case 0x0c: return Link{integer<qint32>()};
        case 0x0f: return SplineToken::Open;
        case 0x10: return SplineToken::Close;
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

bool tokenAt(const Record& record, std::size_t index, SplineToken token)
{
    if (index >= record.fields.size())
        return false;
    const auto* value = std::get_if<SplineToken>(&record.fields[index]);
    return value && *value == token;
}

class SplineFields
{
public:
    SplineFields(const Record& record, std::size_t start) : _record(record), _offset(start) {}

    qint32 integer() { return _record.get<qint32>(_offset++); }
    double real() { return _record.number(_offset++); }
    Triple xyz() { return {real(), real(), real()}; }

    // Skips one saved text enumeration (for example the rationality word `both` of an exact spline surface).
    bool skipText()
    {
        if (_offset < _record.fields.size() && std::holds_alternative<std::monostate>(_record.fields[_offset])) {
            ++_offset;
            return true;
        }
        return false;
    }

private:
    const Record& _record;
    std::size_t _offset;
};

struct SplineAxis {
    int degree;
    int poles;
    NCollection_Array1<double> knots;
    NCollection_Array1<int> multiplicities;
};

SplineAxis readClampedAxis(SplineFields& fields, int degree, int count)
{
    if (degree < 1 || degree > 25 || count < 2 || count > 4096)
        fail("Unsupported Inventor spline degree or knot count");
    SplineAxis axis{degree, 0, NCollection_Array1<double>(1, count), NCollection_Array1<int>(1, count)};
    int multiplicitySum = 0;
    for (int i = 1; i <= count; ++i) {
        const double knot = fields.real();
        int multiplicity = fields.integer();
        const bool end = i == 1 || i == count;
        if ((i > 1 && knot <= axis.knots(i - 1)) || multiplicity <= 0 || multiplicity > degree ||
            (end && multiplicity != degree))
            fail("Unsupported Inventor clamped knot sequence");
        if (end)
            ++multiplicity; // The ASM endpoint representation omits one copy of each end knot.
        axis.knots(i) = knot;
        axis.multiplicities(i) = multiplicity;
        multiplicitySum += multiplicity;
    }
    axis.poles = multiplicitySum - degree - 1;
    return axis;
}

class SolidBuilder
{
public:
    SolidBuilder(const std::vector<Record>& records, double scale,
                 const std::function<bool()>& cancelled, bool strict = true)
        : _records(records), _scale(scale), _cancelled(cancelled), _strict(strict) {}

    // Colour the built faces from this table (the saved colours of a multi-body part). Without it only a single-body part
    // gets colours, through build()'s SavedAppearance.
    void useColorTable(const PartColorTable* table) { _colorTable = table; }

    std::vector<BodyOutcome> build(const SavedAppearance* saved = nullptr, PartAppearance* appearance = nullptr)
    {
        for (const auto& record : _records) {
            checkCancellation(_cancelled);
            for (std::size_t i = 0; i < record.fields.size(); ++i) {
                if (tokenAt(record, i, SplineToken::Open) && !tokenAt(record, i + 1, SplineToken::Reference))
                    _splineBlocks.emplace_back(&record, i);
            }
        }
        findCircularBands();
        std::vector<BodyOutcome> outcomes;
        int bodyRecord = 0;
        for (const auto& record : _records) {
            if (record.type == "body" && !isWireOnlyBody(record)) {
                checkCancellation(_cancelled);
                const std::size_t firstOfBody = outcomes.size();
                _bodyFaceCounts.push_back(countFaces(record));
                try {
                    for (auto& outcome : buildBody(record, saved, appearance))
                        outcomes.push_back(std::move(outcome));
                } catch (const Detail::ResourceError&) {
                    throw;
                } catch (const std::exception& error) {
                    if (_strict || (_cancelled && _cancelled()))
                        throw;
                    clearExtras();
                    outcomes.push_back({TopoDS_Solid(), QString::fromUtf8(error.what()), TopoDS_Shape()});
                }
                for (std::size_t i = firstOfBody; i < outcomes.size(); ++i)
                    outcomes[i].bodyRecord = bodyRecord;
                ++bodyRecord;
            }
        }
        const bool anySolid = std::any_of(outcomes.begin(), outcomes.end(), [](const BodyOutcome& outcome) {
            return !outcome.solid.IsNull() || !outcome.incompleteShape.IsNull();
        });
        if (!anySolid) {
            if (outcomes.empty())
                fail("Inventor part has no saved solid bodies");
            throw std::runtime_error(outcomes.front().error.toStdString());
        }
        const bool anyFailure = std::any_of(outcomes.begin(), outcomes.end(),
                                            [](const BodyOutcome& outcome) { return !outcome.error.isEmpty(); });
        if (appearance && _colorTable) {
            if (_colorTable->defaultColor) {
                const auto& c = *_colorTable->defaultColor;
                appearance->body = Quantity_Color(c[0], c[1], c[2], Quantity_TOC_RGB);
            }
            appearance->faces = std::move(_tableColours);
        } else if (appearance && (outcomes.size() != 1 || anyFailure)) {
            *appearance = {};
        }
        return outcomes;
    }

private:
    // Face counts of the saved bodies that hold geometry, in record order (see BodyOutcome::bodyRecord).
public:
    const std::vector<std::size_t>& bodyFaceCounts() const { return _bodyFaceCounts; }

private:
    std::size_t countFaces(const Record& body) const
    {
        std::size_t faces = 0;
        try {
            for (qint32 id = body.link(3); id != -1; id = get(id, "lump").link(3)) {
                for (qint32 shellId = get(id, "lump").link(4); shellId != -1; shellId = get(shellId, "shell").link(3)) {
                    for (qint32 faceId = get(shellId, "shell").link(5); faceId != -1; faceId = get(faceId, "face").link(3))
                        ++faces;
                }
            }
        } catch (const std::exception&) {
            return 0;
        }
        return faces;
    }

    bool isWireOnlyBody(const Record& body) const
    {
        std::unordered_set<qint32> visited;
        for (qint32 id = body.link(3); id != -1;) {
            checkCancellation(_cancelled);
            if (!visited.insert(id).second)
                fail("Cyclic Inventor lump list");
            const Record& lump = get(id, "lump");
            const Record& shell = get(lump.link(4), "shell");
            if (shell.link(3) != -1 || shell.link(4) != -1 || shell.link(5) != -1 || shell.link(6) < 0)
                return false;
            get(shell.link(6), "wire");
            id = lump.link(3);
        }
        return !visited.empty();
    }

    bool hasIdentityTransform(const Record& body) const
    {
        if (body.link(5) == -1)
            return true;
        const Record& transform = get(body.link(5), "transform");
        return transform.get<Triple>(2) == Triple{1, 0, 0} &&
               transform.get<Triple>(3) == Triple{0, 1, 0} &&
               transform.get<Triple>(4) == Triple{0, 0, 1} &&
               transform.get<Triple>(5) == Triple{0, 0, 0} && transform.number(6) == 1;
    }

    // A body may hold several lumps; each lump is a separate solid. When not strict, a lump that cannot be
    // built is reported in its place and the other lumps are still built.
    std::vector<BodyOutcome> buildBody(const Record& body, const SavedAppearance* saved, PartAppearance* appearance)
    {
        if (body.link(4) != -1 || !hasIdentityTransform(body))
            fail("Unsupported Inventor body transform or wire geometry");
        std::vector<BodyOutcome> result;
        std::unordered_set<qint32> lumps;
        for (qint32 id = body.link(3); id != -1;) {
            if (!lumps.insert(id).second)
                fail("Cyclic Inventor lump list");
            const Record& lump = get(id, "lump");
            try {
                result.push_back({buildLump(lump, saved, appearance), QString()});
                for (auto& solid : _extraSolids)
                    result.push_back({std::move(solid), QString()});
                for (auto& outcome : _extraFailures)
                    result.push_back(std::move(outcome));
                clearExtras();
            } catch (const Detail::ResourceError&) {
                throw;
            } catch (const IncompleteShell& incomplete) {
                if (_strict || (_cancelled && _cancelled()))
                    throw;
                clearExtras();
                result.push_back({TopoDS_Solid(), incomplete.message, incomplete.shape, incomplete.surface});
            } catch (const std::exception& error) {
                if (_strict || (_cancelled && _cancelled()))
                    throw;
                clearExtras();
                result.push_back({TopoDS_Solid(), QString::fromUtf8(error.what()), TopoDS_Shape()});
            }
            id = lump.link(3);
        }
        if (result.empty())
            fail("Inventor body has no lumps");
        return result;
    }

    // A lump with several shells is one solid with internal voids: the shell enclosing the largest volume is
    // the exterior and every other shell must lie inside it.
    TopoDS_Solid buildLump(const Record& lump, const SavedAppearance* saved, PartAppearance* appearance)
    {
        Detail::requireFreeMemory("building a solid");
        std::vector<const Record*> shells;
        std::unordered_set<qint32> seen;
        for (qint32 id = lump.link(4); id != -1;) {
            if (!seen.insert(id).second)
                fail("Cyclic Inventor shell list");
            const Record& shell = get(id, "shell");
            if (shell.link(4) != -1 || shell.link(6) != -1)
                fail("Inventor import currently supports one shell without wire geometry");
            shells.push_back(&shell);
            id = shell.link(3);
        }
        if (shells.empty())
            fail("Inventor lump has no shells");
        if (shells.size() == 1)
            return buildShell(*shells.front(), saved, appearance, false);

        // Several shells in one lump: a shell whose saved face senses enclose a positive volume is an independent solid
        // (a lump may hold hundreds of separate parts); a shell with a negative volume has its normals pointing into
        // a cavity and is a void of the solid that contains it.
        std::vector<TopoDS_Solid> outers;
        std::vector<TopoDS_Solid> voids;
        std::vector<QString> voidFailures;
        for (const Record* shell : shells) {
            _plainShellSign = 1.0;
            try {
                const TopoDS_Solid built = buildShell(*shell, nullptr, nullptr, false);
                (_plainShellSign < 0 ? voids : outers).push_back(built);
            } catch (const Detail::ResourceError&) {
                throw;
            } catch (const IncompleteShell& incomplete) {
                if (_strict || (_cancelled && _cancelled()))
                    throw;
                if (_plainShellSign < 0 && !incomplete.surface)
                    voidFailures.push_back(incomplete.message);
                else if (_plainShellSign < 0)
                    _extraFailures.push_back({TopoDS_Solid(), QString(), incomplete.shape, true});
                else
                    _extraFailures.push_back({TopoDS_Solid(), incomplete.message, incomplete.shape, incomplete.surface});
            } catch (const std::exception& error) {
                if (_strict || (_cancelled && _cancelled()))
                    throw;
                if (_plainShellSign < 0)
                    voidFailures.push_back(QString::fromUtf8(error.what()));
                else
                    _extraFailures.push_back({TopoDS_Solid(), QString::fromUtf8(error.what()), TopoDS_Shape()});
            }
        }
        if (outers.empty()) {
            if (!_extraFailures.empty()) {
                // Every outer shell failed: report the first as the lump's failure.
                const BodyOutcome first = _extraFailures.front();
                _extraFailures.clear();
                // An open surface has no error text; its faces are kept as the lump's outcome, not refused with an empty message.
                if (!first.incompleteShape.IsNull())
                    throw IncompleteShell(first.incompleteShape, first.error, first.isSurface);
                throw std::runtime_error(first.error.isEmpty() ? std::string("Inventor lump has no shell that could be built")
                                                               : first.error.toStdString());
            }
            fail("Inventor lump has only void shells");
        }
        std::vector<Bnd_Box> boxes(outers.size());
        std::vector<GProp_GProps> volumes(outers.size());
        for (std::size_t i = 0; i < outers.size(); ++i) {
            BRepBndLib::Add(outers[i], boxes[i]);
            BRepGProp::VolumeProperties(outers[i], volumes[i]);
        }
        std::vector<std::vector<std::size_t>> voidsOf(outers.size());
        std::vector<TopoDS_Solid> standalone;
        for (std::size_t v = 0; v < voids.size(); ++v) {
            checkCancellation(_cancelled);
            Bnd_Box voidBox;
            BRepBndLib::Add(voids[v], voidBox);
            const TopExp_Explorer vertex(voids[v], TopAbs_VERTEX);
            if (!vertex.More()) {
                voidFailures.push_back(QStringLiteral("a void shell has no vertices"));
                continue;
            }
            const gp_Pnt probe = BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));
            // The smallest solid whose bounding box holds the void and which really contains it.
            std::size_t best = outers.size();
            double bestVolume = 0;
            for (std::size_t o = 0; o < outers.size(); ++o) {
                if (boxes[o].IsOut(voidBox) || boxes[o].IsOut(probe))
                    continue;
                if (best != outers.size() && volumes[o].Mass() >= bestVolume)
                    continue;
                BRepClass3d_SolidClassifier classifier(outers[o], probe, Precision::Confusion());
                if (classifier.State() != TopAbs_IN)
                    continue;
                best = o;
                bestVolume = volumes[o].Mass();
            }
            if (best == outers.size())
                standalone.push_back(voids[v]); // Negative sense but inside nothing: an independent part, already oriented outward.
            else
                voidsOf[best].push_back(v);
        }
        std::vector<TopoDS_Solid> finished;
        for (std::size_t o = 0; o < outers.size(); ++o) {
            if (voidsOf[o].empty()) {
                finished.push_back(outers[o]);
                continue;
            }
            BRepBuilderAPI_MakeSolid builder(TopoDS::Shell(TopoDS_Iterator(outers[o]).Value()));
            for (const std::size_t v : voidsOf[o])
                builder.Add(TopoDS::Shell(TopoDS_Iterator(voids[v]).Value().Reversed()));
            const TopoDS_Solid withVoids = builder.Solid();
            try {
                validateSolid(withVoids);
                finished.push_back(withVoids);
            } catch (const std::exception& error) {
                if (_strict)
                    throw;
                finished.push_back(outers[o]);
                voidFailures.push_back(QString::fromUtf8(error.what()));
            }
        }
        for (const TopoDS_Solid& solid : standalone)
            finished.push_back(solid);
        for (const QString& why : voidFailures) {
            if (_strict)
                throw std::runtime_error(why.toStdString());
            _extraFailures.push_back({TopoDS_Solid(), QStringLiteral("A void of this part could not be applied: %1").arg(why), TopoDS_Shape()});
        }
        for (std::size_t i = 1; i < finished.size(); ++i)
            _extraSolids.push_back(finished[i]);
        return finished.front();
    }

    TopoDS_Solid buildShell(const Record& shell, const SavedAppearance* saved, PartAppearance* appearance,
                            bool repairCavity)
    {
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
        TangentCut cut;
        try {
            cut = findTangentCut(faces);
        } catch (const Detail::ResourceError&) {
            throw;
        } catch (const std::exception&) {
            // The edges of this shell are shared in a way the solid rules do not cover (a sheet body, or a contact the
            // rules do not know). When not strict, build the faces on their own and show them as an open surface.
            if (_strict || (_cancelled && _cancelled()))
                throw;
            return sewSolidOrIncomplete(faces, {}, repairCavity, nullptr, nullptr);
        }
        if (!cut.omittedFaces.empty()) {
            faces.erase(std::remove_if(faces.begin(), faces.end(),
                                       [&](qint32 id) { return cut.omittedFaces.count(id) != 0; }),
                        faces.end());
            cut = findTangentCut(faces);
        }
        if (!cut.pieces.empty()) {
            // Separate solids that touch along an edge. The first is returned; the others are collected by build().
            // Each piece stands on its own: when not strict, a piece that cannot be built is reported in its place (as an
            // incomplete surface when some of its faces built) and the other pieces are still returned.
            TopoDS_Solid first;
            std::vector<BodyOutcome> failures;
            for (const auto& piece : cut.pieces) {
                checkCancellation(_cancelled);
                try {
                    TopoDS_Solid solid = sewSolidOrIncomplete(piece, cut.omittedLoops, repairCavity, nullptr, nullptr);
                    if (first.IsNull())
                        first = solid;
                    else
                        _extraSolids.push_back(solid);
                } catch (const Detail::ResourceError&) {
                    throw;
                } catch (const IncompleteShell& incomplete) {
                    if (_strict || (_cancelled && _cancelled()))
                        throw;
                    failures.push_back({TopoDS_Solid(), incomplete.message, incomplete.shape, incomplete.surface});
                } catch (const std::exception& error) {
                    if (_strict || (_cancelled && _cancelled()))
                        throw;
                    failures.push_back({TopoDS_Solid(), QString::fromUtf8(error.what()), TopoDS_Shape()});
                }
            }
            if (first.IsNull()) {
                // Nothing was built as a solid: the first failure stands for the whole shell.
                if (failures.empty())
                    fail("Inventor shell has no pieces");
                if (!failures.front().incompleteShape.IsNull())
                    throw IncompleteShell(failures.front().incompleteShape, failures.front().error, failures.front().isSurface);
                throw std::runtime_error(failures.front().error.toStdString());
            }
            for (auto& failure : failures)
                _extraFailures.push_back(std::move(failure));
            return first;
        }
        if (cut.cavity.empty()) {
            if (saved && matchesAppearance(faces, *saved))
                return sewSolidOrIncomplete(faces, cut.omittedLoops, repairCavity, saved, appearance);
            return sewSolidOrIncomplete(faces, cut.omittedLoops, repairCavity, nullptr, nullptr);
        }

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
        // Faces whose whole boundary is one edge traced out and back; they enclose nothing and are dropped.
        std::unordered_set<qint32> omittedFaces;
        // Closed shells that touch one another along pinch edges; each is a separate solid.
        std::vector<std::vector<qint32>> pieces;
        std::vector<qint32> outside;
        std::vector<qint32> cavity;
    };
    TangentCut findTangentCut(const std::vector<qint32>& faces) const;
    bool isTangentContact(qint32 faceId, qint32 edgeId, qint32 cylinderId) const;
    bool isToroidalContact(qint32 faceId, qint32 edgeId, qint32 firstFace, qint32 secondFace) const;

    bool matchesAppearance(const std::vector<qint32>& faces, const SavedAppearance& saved) const
    {
        if (faces.size() != saved.faces.size())
            return false;
        std::unordered_set<quint32> keys;
        for (const auto face : faces) {
            const auto& fields = get(face, "face").fields;
            const auto* value = fields.size() > 1 ? std::get_if<qint32>(&fields[1]) : nullptr;
            if (!value || *value < 0)
                return false;
            const auto key = static_cast<quint32>(*value);
            if (saved.faces.count(key) == 0 || !keys.insert(key).second)
                return false;
        }
        return true;
    }

    TopoDS_Solid sewSolid(const std::vector<qint32>& faces, const std::unordered_set<qint32>& omittedLoops,
                          bool repairCavity, const SavedAppearance* saved = nullptr, PartAppearance* appearance = nullptr)
    {
        _edgeUses.clear();
        _pendingColours.clear();
        BRepBuilderAPI_Sewing sewing(CheckToleranceMm);
        std::vector<TopoDS_Face> measuredFaces;
        PartAppearance colors;
        const auto quantity = [](const DiffuseColor& c) {
            return Quantity_Color(c[0], c[1], c[2], Quantity_TOC_RGB);
        };
        if (saved && saved->body)
            colors.body = quantity(*saved->body);
        for (const qint32 id : faces) {
            checkCancellation(_cancelled);
            try {
                const Record& record = get(id, "face");
                const auto face = buildFace(record, id, omittedLoops);
                sewing.Add(face);
                if (_collectFaces)
                    _builtFaces.push_back(face);
                if (_measureSignedVolume)
                    measuredFaces.push_back(face);
                if (saved) {
                    const auto& color = saved->faces.at(static_cast<quint32>(record.get<qint32>(1)));
                    if (color)
                        colors.faces.emplace_back(face, quantity(*color));
                } else if (_colorTable) {
                    const auto found = _colorTable->faces.find(static_cast<quint32>(record.get<qint32>(1)));
                    if (found != _colorTable->faces.end()) {
                        colors.faces.emplace_back(face, quantity(found->second));
                        _pendingColours.emplace_back(face, quantity(found->second));
                    }
                }
            } catch (const Detail::ResourceError&) {
                throw;
            } catch (const std::exception& error) {
                if (_collectFaces && !(_cancelled && _cancelled())) {
                    // Keep going: the faces that can be built are still worth showing.
                    if (_failedFaces == 0)
                        _firstFaceFailure = QStringLiteral("face %1: %2").arg(id).arg(QString::fromUtf8(error.what()));
                    ++_failedFaces;
                    continue;
                }
                throw std::runtime_error("Inventor ASM face " + std::to_string(id) + ": " + error.what());
            }
        }
        if (_measureSignedVolume && !measuredFaces.empty()) {
            // Integrate over the faces with their saved senses (no sewing involved): the sign says outer or void.
            BRep_Builder compoundBuilder;
            TopoDS_Compound compound;
            compoundBuilder.MakeCompound(compound);
            for (const TopoDS_Face& face : measuredFaces)
                compoundBuilder.Add(compound, face);
            GProp_GProps properties;
            BRepGProp::VolumeProperties(compound, properties);
            _lastSignedVolume = properties.Mass();
        }
        if (_collectFaces && _failedFaces != 0)
            fail("Some Inventor faces could not be built");
        for (const auto& use : _edgeUses) {
            const Record& usedEdge = get(use.first, "edge");
            if (usedEdge.link(8) < 0 && usedEdge.link(3) == usedEdge.link(5))
                continue; // Degenerate edge at a singular point; it has no partner.
            if (use.second != 3)
                fail("Inventor shell has an unpaired or non-manifold edge");
        }

        sewing.Perform();
        TopoDS_Shape sewn = sewing.SewedShape();
        if (faces.size() == 1 && !sewn.IsNull() && sewn.ShapeType() == TopAbs_FACE) {
            // One face that closes on itself (a full torus or sphere) sews to the face, not to a shell.
            TopoDS_Shell single;
            BRep_Builder().MakeShell(single);
            BRep_Builder().Add(single, sewn);
            sewn = single;
        }
        if (sewn.IsNull() || sewn.ShapeType() != TopAbs_SHELL ||
            sewing.NbFreeEdges() != 0 || sewing.NbMultipleEdges() != 0)
            fail("Inventor faces did not form one closed OCCT shell");
        for (auto& item : colors.faces) {
            TopoDS_Shape replacement = item.first;
            if (sewing.IsModifiedSubShape(replacement))
                replacement = sewing.ModifiedSubShape(replacement);
            else if (sewing.IsModified(replacement))
                replacement = sewing.Modified(replacement);
            item.first = !replacement.IsNull() && replacement.ShapeType() == TopAbs_FACE
                ? TopoDS::Face(replacement) : TopoDS_Face();
        }
        BRepLib::SameParameter(sewn, CheckToleranceMm, true);
        TopoDS_Solid solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewn)).Solid();
        if (solid.IsNull() || !BRepLib::OrientClosedSolid(solid))
            fail("Inventor shape could not be oriented as an OCCT solid");
        const bool hasSplineFaces = std::any_of(faces.begin(), faces.end(), [&](qint32 id) {
            return get(get(id, "face").link(7)).type == "spline-surface";
        });
        const bool hasOnlyPlaneAndConeFaces = std::all_of(faces.begin(), faces.end(), [&](qint32 id) {
            const auto& type = get(get(id, "face").link(7)).type;
            return type == "plane-surface" || type == "cone-surface";
        });
        if ((repairCavity || hasSplineFaces || hasOnlyPlaneAndConeFaces || hasExplicitToroidalTrims(faces)) &&
            !BRepCheck_Analyzer(solid).IsValid()) {
            ShapeFix_Shape fix(solid);
            fix.SetPrecision(CheckToleranceMm);
            fix.SetMaxTolerance(CheckToleranceMm);
            fix.FixVertexPositionMode() = 0;
            const auto& wireFix = fix.FixWireTool();
            wireFix->ModifyTopologyMode() = false;
            wireFix->ModifyGeometryMode() = false;
            if (hasSplineFaces || hasOnlyPlaneAndConeFaces) {
                // A natural bound added to a periodic support can turn a
                // small saved patch into the complement of that patch.
                fix.FixFaceTool()->FixAddNaturalBoundMode() = 0;
                fix.FixFaceTool()->FixSplitFaceMode() = 0;
                fix.FixFaceTool()->FixLoopWiresMode() = 0;
            }
            fix.Perform();
            for (auto& item : colors.faces) {
                if (item.first.IsNull())
                    continue;
                const auto replacement = fix.Context()->Apply(item.first);
                item.first = !replacement.IsNull() && replacement.ShapeType() == TopAbs_FACE
                    ? TopoDS::Face(replacement) : TopoDS_Face();
            }
            if (fix.Shape().IsNull() || fix.Shape().ShapeType() != TopAbs_SOLID || fix.Shape().NbChildren() != 1)
                fail("Inventor trim repair changed the solid or shell count");
            std::size_t repairedFaces = 0;
            for (TopExp_Explorer face(fix.Shape(), TopAbs_FACE); face.More(); face.Next())
                ++repairedFaces;
            if (repairedFaces != faces.size())
                fail("Inventor trim repair changed the face count");
            solid = TopoDS::Solid(fix.Shape());
            if (!BRepLib::OrientClosedSolid(solid))
                fail("Inventor solid could not be oriented after trim repair");
        }
        validateSolid(solid);
        if (saved && appearance) {
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> finalFaces;
            TopExp::MapShapes(solid, TopAbs_FACE, finalFaces);
            const auto unmapped = std::find_if(colors.faces.begin(), colors.faces.end(), [&](const auto& item) {
                return item.first.IsNull() || !finalFaces.Contains(item.first);
            });
            if (unmapped == colors.faces.end())
                *appearance = std::move(colors);
        }
        if (_colorTable) {
            // The faces were replaced by sewing and repair; keep the colour of every face that is still in the solid.
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> finalFaces;
            TopExp::MapShapes(solid, TopAbs_FACE, finalFaces);
            for (const auto& item : colors.faces) {
                if (!item.first.IsNull() && finalFaces.Contains(item.first))
                    _tableColours.push_back(item);
            }
        }
        return solid;
    }

    // sewSolid for a plain shell. When not strict and the shell cannot become a solid but some faces were built, the
    // faces are returned in an IncompleteShell instead of being thrown away.
    TopoDS_Solid sewSolidOrIncomplete(const std::vector<qint32>& faces, const std::unordered_set<qint32>& omittedLoops,
                                      bool repairCavity, const SavedAppearance* saved, PartAppearance* appearance)
    {
        _lastSignedVolume = 0;
        _measureSignedVolume = true;
        struct MeasureReset {
            bool& flag;
            double& volume;
            double& sign;
            ~MeasureReset()
            {
                flag = false;
                sign = volume != 0 ? volume : 1.0;
            }
        } measureReset{_measureSignedVolume, _lastSignedVolume, _plainShellSign};
        if (_strict)
            return sewSolid(faces, omittedLoops, repairCavity, saved, appearance);
        _builtFaces.clear();
        _failedFaces = 0;
        _firstFaceFailure.clear();
        _collectFaces = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{_collectFaces};
        try {
            return sewSolid(faces, omittedLoops, repairCavity, saved, appearance);
        } catch (const Detail::ResourceError&) {
            throw;
        } catch (const std::exception& error) {
            if (_builtFaces.empty() || (_cancelled && _cancelled()))
                throw;
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (const TopoDS_Face& face : _builtFaces)
                builder.Add(compound, face);
            const QString message = _failedFaces != 0
                ? QStringLiteral("%1 of %2 faces could not be built (first: %3). The other faces are shown as an open "
                                 "surface; it has no volume.")
                      .arg(_failedFaces).arg(faces.size()).arg(_firstFaceFailure)
                : QStringLiteral("The faces could not be closed into a solid (%1). They are shown as an open surface; "
                                 "it has no volume.").arg(QString::fromUtf8(error.what()));
            _tableColours.insert(_tableColours.end(), _pendingColours.begin(), _pendingColours.end());
            throw IncompleteShell(compound, _failedFaces != 0 ? message : QString(), _failedFaces == 0);
        }
    }

    bool hasExplicitToroidalTrims(const std::vector<qint32>& faces) const
    {
        bool hasTorus = false;
        for (const qint32 face : faces) {
            if (get(get(face, "face").link(7)).type != "torus-surface")
                continue;
            if (_circularBands.count(face) == 0)
                return false;
            hasTorus = true;
        }
        return hasTorus;
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
            throw std::runtime_error("Inventor ASM link " + std::to_string(id) + " points outside the " +
                                     std::to_string(_records.size()) + " saved body records");
        return _records[id];
    }

    const Record& get(qint32 id, const char* type) const
    {
        const Record& record = get(id);
        const QByteArrayView expected(type);
        if (record.type != expected &&
            !(expected == "coedge" && record.type == "tcoedge-coedge") &&
            !(expected == "edge" && record.type == "tedge-edge") &&
            !(expected == "vertex" && record.type == "tvertex-vertex"))
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
    struct SplinePayload {
        const Record& record;
        std::size_t fields;
        bool rational;
    };
    SplinePayload splinePayload(const Record& record, bool surface) const;
    SplinePayload splinePayloadAt(const Record* record, std::size_t start, bool surface) const;
    Handle(Geom_BSplineCurve) buildSplineCurve(const Record& record, double& tolerance) const;
    Handle(Geom_BSplineCurve) splineCurveFrom(const SplinePayload& payload, double& tolerance) const;
    Handle(Geom_BSplineSurface) buildSplineSurface(const Record& record) const;

    // Procedural helix: X(u) = C + M cos u + N sin u + P u / (2 pi), lengths in millimetres.
    // A helix curve may also spiral in or out: its radius is the saved one times (1 + radial * u / 2 pi), where radial is the
    // fraction of the radius gained per turn (0 for a plain helix). Found in a helix edge whose end vertex lies on a smaller
    // radius than its start: the saved radial term reproduces both radii to 5 digits.
    struct Helix {
        gp_Pnt center;
        gp_Vec major, minor, pitch;
        double radial = 0.0;
        gp_Pnt at(double u) const
        {
            return center.Translated((major * std::cos(u) + minor * std::sin(u)) * (1.0 + radial * u / TwoPi) + pitch * (u / TwoPi));
        }
        gp_Vec tangent(double u) const
        {
            return (major * -std::sin(u) + minor * std::cos(u)) * (1.0 + radial * u / TwoPi) +
                   (major * std::cos(u) + minor * std::sin(u)) * (radial / TwoPi) + pitch * (1.0 / TwoPi);
        }
    };
    // radialField: the field after the pitch vector holds the radial term (helix curves; not yet checked for helix surfaces).
    Helix readHelix(const Record& record, std::size_t first, bool radialField = false) const
    {
        Helix helix{point(record.get<Triple>(first)), vector(record.get<Triple>(first + 1)),
                    vector(record.get<Triple>(first + 2)), vector(record.get<Triple>(first + 3))};
        if (radialField && first + 4 < record.fields.size()) {
            if (const double* radial = std::get_if<double>(&record.fields[first + 4]); radial && std::isfinite(*radial))
                helix.radial = *radial;
        }
        return helix;
    }
    Handle(Geom_BSplineCurve) interpolateCurve(const std::function<gp_Pnt(double)>& position,
                                               const std::function<gp_Vec(double)>& derivative,
                                               double first, double last) const;
    Handle(Geom_Surface) buildHelixSurface(const Record& record, std::size_t start) const;
    // The block that defines a spline: the record itself, or the earlier block it refers to.
    std::pair<const Record*, std::size_t> resolveBlock(const Record& record) const
    {
        const Record* block = &record;
        std::size_t start = 4;
        if (tokenAt(record, start, SplineToken::Open) && tokenAt(record, start + 1, SplineToken::Reference)) {
            const auto index = record.get<qint32>(start + 2);
            if (index < 0 || static_cast<std::size_t>(index) >= _splineBlocks.size())
                fail("Invalid Inventor spline definition reference");
            block = _splineBlocks[index].first;
            start = _splineBlocks[index].second;
        }
        return {block, start};
    }

    struct Surface {
        Handle(Geom_Surface) geometry;
        bool inverted = false;
    };
    struct FaceEdge {
        TopoDS_Edge shape;
        unsigned uses = 0;
    };
    struct CircularBand {
        Handle(Geom_Surface) surface;
        double firstV;
        double lastV;
    };
    // coneHeights: for an elliptic cone, the range of heights along its axis that the face under construction needs.
    Surface buildSurface(const Record& surface, const std::pair<double, double>* coneHeights = nullptr) const;
    // A cone whose cross-section is an ellipse too eccentric to be taken as a circle (OCCT has no surface for it).
    struct EllipticCone {
        gp_Pnt origin;
        gp_Dir axis;
        gp_Dir major;
        double radius;   // major radius at the origin
        double ratio;    // minor / major
        double tangent;  // growth of the radii per unit height, relative to the major radius at the origin
        bool inverted;
    };
    bool ellipticCone(const Record& surface, EllipticCone& cone) const;
    std::optional<std::pair<double, double>> coneHeightRange(const Record& face, qint32 faceId,
                                                             const std::unordered_set<qint32>& omittedLoops,
                                                             const EllipticCone& cone);
    Handle(Geom_Surface) ellipticConeSurface(const EllipticCone& cone, double low, double high) const;
    void findCircularBands();
    TopoDS_Face joinWindingBoundaries(const TopoDS_Face& face, const Handle(Geom_Surface)& surface) const;
    TopoDS_Face joinToroidalBoundaries(const TopoDS_Face& face, const Geom_ToroidalSurface& torus,
                                      bool reverseWires) const;

    TopoDS_Face buildFace(const Record& face, qint32 faceId, const std::unordered_set<qint32>& omittedLoops)
    {
        if (face.link(4) < 0 && !face.get<bool>(9)) {
            // A face with no loop at all covers a complete closed surface (a full torus or sphere).
            const Surface whole = buildSurface(get(face.link(7)));
            if (!Handle(Geom_ToroidalSurface)::DownCast(whole.geometry).IsNull() ||
                !Handle(Geom_SphericalSurface)::DownCast(whole.geometry).IsNull()) {
                BRepBuilderAPI_MakeFace closed(whole.geometry, Precision::Confusion());
                if (!closed.IsDone())
                    fail("Could not build a complete closed Inventor surface");
                TopoDS_Face closedFace = closed.Face();
                if (face.get<bool>(8) != whole.inverted)
                    closedFace.Reverse();
                return closedFace;
            }
        }
        if (face.get<bool>(9) || face.link(4) < 0)
            fail("Inventor face uses unsupported unbounded or sheet geometry");
        const Record& supportRecord = get(face.link(7));
        std::optional<std::pair<double, double>> coneHeights;
        if (EllipticCone elliptic; ellipticCone(supportRecord, elliptic))
            coneHeights = coneHeightRange(face, faceId, omittedLoops, elliptic);
        Surface support = buildSurface(supportRecord, coneHeights ? &*coneHeights : nullptr);
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
                if (wire.IsNull()) {
                    loopId = loop.link(3);
                    continue;
                }
                if (firstWire.IsNull())
                    firstWire = wire;
                builder.Add(result, wire);
                ++loopCount;
            }
            loopId = loop.link(3);
        }
        if (firstWire.IsNull())
            fail("Inventor face has no remaining boundary");
        // On the inner (lemon) part of a spindle torus, where major + minor * cos v is negative, the surface normal of the
        // parametrisation (dS/du x dS/dv) points the opposite way to the saved outward normal, so the saved face sense has
        // to be read the other way round there. The part is told from a point of the boundary: it lies on the tube circle
        // at a signed distance from the axis, and the sign that makes the tube equation hold is the sign of the distance.
        bool lemon = false;
        if (const auto spindle = Handle(Geom_ToroidalSurface)::DownCast(support.geometry);
            !spindle.IsNull() && spindle->MinorRadius() > spindle->MajorRadius() && !edges.empty()) {
            double first, last;
            const Handle(Geom_Curve) curve = BRep_Tool::Curve(edges.begin()->second.shape, first, last);
            if (!curve.IsNull()) {
                const gp_Vec offset(spindle->Location(), curve->Value((first + last) / 2));
                const gp_Vec axisVector(spindle->Axis().Direction());
                const double axial = offset.Dot(axisVector);
                const double radial = (offset - axisVector * axial).Magnitude();
                const auto miss = [&](double signedRadial) {
                    return std::abs(std::hypot(signedRadial - spindle->MajorRadius(), axial) - spindle->MinorRadius());
                };
                lemon = radial > Precision::Confusion() && miss(-radial) < miss(radial);
            }
        }
        const bool senseFlipped = face.get<bool>(8) != (support.inverted != lemon);
        if (const auto found = _circularBands.find(faceId); found != _circularBands.end()) {
            const CircularBand& band = found->second;
            BRepBuilderAPI_MakeFace faceBuilder(band.surface, 0.0, TwoPi, band.firstV, band.lastV, CheckToleranceMm);
            if (!faceBuilder.IsDone())
                fail("Could not add the seam of a closed Inventor circular band");
            result = faceBuilder.Face();
            if (senseFlipped)
                result.Reverse();
            return result;
        }
        if (const auto sphere = Handle(Geom_SphericalSurface)::DownCast(support.geometry);
            !sphere.IsNull() && loopCount >= 2) {
            // Loops that straddle the saved seam meridian cannot bound a face in parameter space. Turn the
            // sphere about its axis so the seam runs through the widest gap between boundary points.
            std::vector<double> azimuths;
            const gp_Ax3 position = sphere->Position();
            for (const auto& item : edges) {
                double first, last;
                const Handle(Geom_Curve) curve = BRep_Tool::Curve(item.second.shape, first, last);
                if (curve.IsNull())
                    continue;
                for (int i = 0; i <= 48; ++i) {
                    const gp_Vec offset(position.Location(), curve->Value(first + (last - first) * i / 48.0));
                    const double x = offset.Dot(gp_Vec(position.XDirection()));
                    const double y = offset.Dot(gp_Vec(position.YDirection()));
                    if (std::hypot(x, y) > 1e-3 * sphere->Radius())
                        azimuths.push_back(std::atan2(y, x));
                }
            }
            if (azimuths.size() > 1) {
                std::sort(azimuths.begin(), azimuths.end());
                double bestGap = azimuths.front() + TwoPi - azimuths.back();
                double seam = azimuths.back() + bestGap / 2;
                for (std::size_t i = 1; i < azimuths.size(); ++i) {
                    if (azimuths[i] - azimuths[i - 1] > bestGap) {
                        bestGap = azimuths[i] - azimuths[i - 1];
                        seam = azimuths[i - 1] + bestGap / 2;
                    }
                }
                const gp_Ax3 turned = position.Rotated(gp_Ax1(position.Location(), position.Direction()), seam);
                support.geometry = new Geom_SphericalSurface(turned, sphere->Radius());
                TopoDS_Face reseamed;
                builder.MakeFace(reseamed, support.geometry, Precision::Confusion());
                for (TopoDS_Iterator w(result, false); w.More(); w.Next())
                    builder.Add(reseamed, w.Value());
                result = reseamed;
            }
        }
        const Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(support.geometry);
        if (plane.IsNull()) {
            const bool toroidalJoin = !Handle(Geom_ToroidalSurface)::DownCast(support.geometry).IsNull() && loopCount == 3;
            if (senseFlipped && !toroidalJoin) {
                // The saved loops run counter-clockwise about the outward normal, which is opposite to the
                // surface normal here. Orient the wires for the surface now instead of leaving the choice to
                // ShapeFix_Face, which guesses wrongly for wires that span most of a periodic parameter.
                TopoDS_Face oriented;
                builder.MakeFace(oriented, support.geometry, Precision::Confusion());
                for (TopoDS_Iterator w(result, false); w.More(); w.Next())
                    builder.Add(oriented, w.Value().Reversed());
                result = oriented;
            }
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
                // Tolerant vertices carry their own saved tolerance, which the edge inherits.
                const double planeTolerance = std::max(CheckToleranceMm, BRep_Tool::Tolerance(item.second.shape));
                for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    // The comparison is made with the confusion margin: a tolerance taken from the residual of the edge's own end
                    // vertex would otherwise fail by rounding.
                    if (!(plane->Pln().Distance(curve->Value(first + fraction * (last - first))) <= planeTolerance + Precision::Confusion()))
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
            if (face.get<bool>(8) != support.inverted) {
                // Orient the wires for the plane now; see the note for the other surfaces above.
                TopoDS_Face oriented;
                builder.MakeFace(oriented, support.geometry, Precision::Confusion());
                for (TopoDS_Iterator w(result, false); w.More(); w.Next())
                    builder.Add(oriented, w.Value().Reversed());
                result = oriented;
            }
        }

        if (!Handle(Geom_SphericalSurface)::DownCast(support.geometry).IsNull() && loopCount >= 2) {
            // Every loop that runs clockwise in parameter space is a hole. When there is no counter-clockwise
            // loop, the face is the whole sphere minus those holes, so the whole sphere is its outer boundary.
            bool allHoles = true;
            for (TopoDS_Iterator w(result, false); w.More() && allHoles; w.Next()) {
                double area = 0;
                gp_Pnt2d previous;
                bool havePrevious = false;
                const TopoDS_Wire wire = TopoDS::Wire(w.Value());
                const auto accumulate = [&](const TopoDS_Edge& edge, bool reversedEdge) {
                    double f0, f1;
                    const auto pcurve = BRep_Tool::CurveOnSurface(edge, result, f0, f1);
                    if (pcurve.IsNull())
                        return;
                    for (int i = 0; i <= 40; ++i) {
                        const double s = reversedEdge ? f1 - (f1 - f0) * i / 40.0 : f0 + (f1 - f0) * i / 40.0;
                        const gp_Pnt2d q = pcurve->Value(s);
                        if (havePrevious)
                            area += 0.5 * (previous.X() * q.Y() - q.X() * previous.Y());
                        previous = q;
                        havePrevious = true;
                    }
                };
                if (wire.NbChildren() == 1) {
                    const TopoDS_Edge edge = TopoDS::Edge(TopoDS_Iterator(wire, false).Value());
                    accumulate(edge, edge.Orientation() == TopAbs_REVERSED);
                } else {
                    for (BRepTools_WireExplorer explorer(wire, result); explorer.More(); explorer.Next())
                        accumulate(explorer.Current(), explorer.Orientation() == TopAbs_REVERSED);
                }
                allHoles = area < 0;
            }
            if (allHoles) {
                BRepBuilderAPI_MakeFace whole(support.geometry, 0.0, TwoPi, -M_PI / 2, M_PI / 2, Precision::Confusion());
                if (whole.IsDone()) {
                    TopoDS_Face withHoles = whole.Face();
                    for (TopoDS_Iterator w(result, false); w.More(); w.Next())
                        builder.Add(withHoles, w.Value());
                    result = withHoles;
                }
            }
        }
        const auto torus = Handle(Geom_ToroidalSurface)::DownCast(support.geometry);
        if (!torus.IsNull() && loopCount == 3) {
            const auto joined = joinToroidalBoundaries(result, *torus, senseFlipped);
            if (!joined.IsNull())
                result = joined;
        }
        if (!(!torus.IsNull() && loopCount == 3) && loopCount <= 2) {
            const auto joined = joinWindingBoundaries(result, support.geometry);
            if (!joined.IsNull())
                result = joined;
        }
        ShapeFix_Face faceFix(result);
        faceFix.SetPrecision(CheckToleranceMm);
        faceFix.SetMaxTolerance(CheckToleranceMm);
        // A sphere face whose loops all enclose holes needs the whole sphere as its outer boundary.
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
        if (senseFlipped)
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
        unsigned wireEdges = 0;
        do {
            if (!visited.insert(id).second)
                fail("Cyclic Inventor face boundary");
            const Record& coedge = get(id, "coedge");
            if (coedge.link(8) != loopId)
                fail("Inventor coedge belongs to a different loop");
            if (_spikeCoedges.count(id) != 0) {
                id = coedge.link(3);
                continue;
            }
            const qint32 edgeId = coedge.link(6);
            const Record& edge = get(edgeId, "edge");
            const bool reversed = coedge.get<bool>(7);
            const unsigned sense = reversed ? 2 : 1;
            unsigned& use = _edgeUses[edgeId];
            // A solid traverses every edge once in each direction. In the lenient path the faces may belong to a sheet
            // body, where neighbouring faces can legitimately run the same way; the shell is then simply open.
            if ((use & sense) != 0 && !_collectFaces)
                fail("Inventor shell has a non-manifold edge or inconsistent edge orientation");
            use |= sense;

            const qint32 startVertex = edge.link(reversed ? 5 : 3);
            if (initialVertex == -1)
                initialVertex = startVertex;
            else if (endVertex != startVertex)
                fail("Inventor face boundary has disconnected edges");
            endVertex = edge.link(reversed ? 3 : 5);

            // A curveless edge that starts and ends at one vertex collapses to a point (cone apex,
            // sphere pole). Leave it out; ShapeFix_Face re-creates the degenerated edge from the
            // neighbouring pcurves on the singular surface.
            if (edge.link(8) >= 0 || edge.link(3) != edge.link(5)) {
                auto [found, inserted] = edges.try_emplace(edgeId);
                FaceEdge& local = found->second;
                if (inserted)
                    local.shape = buildEdge(edgeId, vertices);
                ++local.uses;
                builder.Add(wire, local.shape.Oriented(reversed ? TopAbs_REVERSED : TopAbs_FORWARD));
                ++wireEdges;
            }
            id = coedge.link(3);
        } while (id != first);

        if (initialVertex != endVertex)
            fail("Inventor face boundary is not closed");
        if (wireEdges == 0)
            return TopoDS_Wire(); // A pole loop; the singular point needs no boundary of its own.
        wire.Closed(true);
        return wire;
    }

    const std::vector<Record>& _records;
    double _scale;
    const std::function<bool()>& _cancelled;
    std::unordered_map<qint32, unsigned> _edgeUses;
    void clearExtras()
    {
        _extraSolids.clear();
        _extraFailures.clear();
    }

    bool _strict = true;
    // Faces built by the plain-shell path, kept so an incomplete shell can still be shown (see sewSolidOrIncomplete).
    bool _collectFaces = false;
    // Signed volume of the faces of the shell last built by sewSolidOrIncomplete, from the saved face senses: positive
    // for an outer shell, negative for a void whose normals point into the cavity (see buildLump).
    bool _measureSignedVolume = false;
    double _lastSignedVolume = 0;
    double _plainShellSign = 1;
    std::vector<BodyOutcome> _extraFailures; // other shells of the lump that could not be built or are incomplete
    std::vector<TopoDS_Face> _builtFaces;
    std::size_t _failedFaces = 0;
    QString _firstFaceFailure;
    mutable std::unordered_set<qint32> _spikeCoedges;
    std::vector<TopoDS_Solid> _extraSolids;
    // Colours of a part with several bodies (see useColorTable). _pendingColours holds the coloured faces of the shell being
    // built; they move to _tableColours once that shell is final (mapped to its final faces) or shown as an incomplete shell.
    const PartColorTable* _colorTable = nullptr;
    std::vector<std::pair<TopoDS_Face, Quantity_Color>> _pendingColours;
    std::vector<std::pair<TopoDS_Face, Quantity_Color>> _tableColours;
    std::vector<std::size_t> _bodyFaceCounts;
    std::unordered_map<qint32, CircularBand> _circularBands;
    std::unordered_map<qint32, gp_Dir> _circleSeams;
    std::vector<std::pair<const Record*, std::size_t>> _splineBlocks;
};

SolidBuilder::SplinePayload SolidBuilder::splinePayload(const Record& source, bool surface) const
{
    const Record* record = &source;
    std::size_t start = 4;
    if (!tokenAt(*record, start, SplineToken::Open))
        fail("Missing Inventor spline definition");
    if (tokenAt(*record, start + 1, SplineToken::Reference)) {
        const auto index = record->get<qint32>(start + 2);
        if (index < 0 || static_cast<std::size_t>(index) >= _splineBlocks.size() ||
            !tokenAt(*record, start + 3, SplineToken::Close))
            fail("Invalid Inventor spline definition reference");
        const auto& target = _splineBlocks[index];
        record = target.first;
        start = target.second;
    }
    return splinePayloadAt(record, start, surface);
}

SolidBuilder::SplinePayload SolidBuilder::splinePayloadAt(const Record* record, std::size_t start, bool surface) const
{
    unsigned depth = 0;
    std::size_t candidate = 0;
    for (std::size_t i = start; i < record->fields.size(); ++i) {
        if (tokenAt(*record, i, SplineToken::Open))
            ++depth;
        else if (tokenAt(*record, i, SplineToken::Close)) {
            if (--depth == 0)
                break;
        } else if (depth == 1 && (tokenAt(*record, i, SplineToken::Polynomial) ||
                                  tokenAt(*record, i, SplineToken::Rational))) {
            candidate = i;
            if (!surface) // The 3D curve precedes the parameter-space splines.
                break;
        }
    }
    if (candidate == 0)
        fail("Missing saved Inventor B-spline data");
    return {*record, candidate + 1, tokenAt(*record, candidate, SplineToken::Rational)};
}

Handle(Geom_BSplineCurve) SolidBuilder::buildSplineCurve(const Record& record, double& tolerance) const
{
    return splineCurveFrom(splinePayload(record, false), tolerance);
}

Handle(Geom_BSplineCurve) SolidBuilder::splineCurveFrom(const SplinePayload& payload, double& tolerance) const
{
    SplineFields fields(payload.record, payload.fields);
    const int degree = fields.integer();
    const int form = fields.integer();
    if (form < 0 || form > 2)
        fail("Unsupported Inventor spline curve form");
    const int count = fields.integer();
    const auto axis = readClampedAxis(fields, degree, count);
    NCollection_Array1<gp_Pnt> poles(1, axis.poles);
    NCollection_Array1<double> weights(1, axis.poles);
    for (int i = 1; i <= axis.poles; ++i) {
        poles(i) = point(fields.xyz());
        if (payload.rational) {
            weights(i) = fields.real();
            if (!(weights(i) > 0.0))
                fail("Invalid Inventor spline weight");
        }
    }
    const double savedTolerance = length(fields.real());
    if (savedTolerance < 0 || savedTolerance > MaxSavedToleranceMm)
        throw std::runtime_error("Unsupported Inventor intersection curve tolerance " + std::to_string(savedTolerance) + " mm");
    tolerance = std::max(CheckToleranceMm, savedTolerance);
    if (payload.rational)
        return new Geom_BSplineCurve(poles, weights, axis.knots, axis.multiplicities, degree);
    return new Geom_BSplineCurve(poles, axis.knots, axis.multiplicities, degree);
}

Handle(Geom_BSplineSurface) SolidBuilder::buildSplineSurface(const Record& record) const
{
    const auto payload = splinePayload(record, true);
    SplineFields fields(payload.record, payload.fields);
    const int uDegree = fields.integer();
    const int vDegree = fields.integer();
    fields.skipText();
    const int uForm = fields.integer();
    const int vForm = fields.integer();
    if ((uForm != 0 && uForm != 2) || (vForm != 0 && vForm != 2))
        fail("Unsupported Inventor spline surface form");
    fields.integer(); // Saved pole classifications. The explicit grid is used below.
    fields.integer();
    const int uCount = fields.integer();
    const int vCount = fields.integer();
    const auto u = readClampedAxis(fields, uDegree, uCount);
    const auto v = readClampedAxis(fields, vDegree, vCount);
    if (static_cast<std::size_t>(u.poles) * v.poles > FieldLimit / 3)
        fail("Inventor spline surface pole limit exceeded");
    NCollection_Array2<gp_Pnt> poles(1, u.poles, 1, v.poles);
    NCollection_Array2<double> weights(1, u.poles, 1, v.poles);
    for (int j = 1; j <= v.poles; ++j) {
        for (int i = 1; i <= u.poles; ++i) {
            poles(i, j) = point(fields.xyz());
            if (payload.rational) {
                weights(i, j) = fields.real();
                if (!(weights(i, j) > 0.0))
                    fail("Invalid Inventor spline surface weight");
            }
        }
    }
    Handle(Geom_BSplineSurface) result;
    if (payload.rational)
        result = new Geom_BSplineSurface(poles, weights, u.knots, v.knots, u.multiplicities, v.multiplicities, u.degree, v.degree);
    else
        result = new Geom_BSplineSurface(poles, u.knots, v.knots, u.multiplicities, v.multiplicities, u.degree, v.degree);
    if (uForm == 2) {
        if (!result->IsUClosed())
            fail("Saved Inventor periodic surface does not close");
        result->SetUPeriodic();
    }
    if (vForm == 2) {
        if (!result->IsVClosed())
            fail("Saved Inventor periodic surface does not close");
        result->SetVPeriodic();
    }
    return result;
}

Handle(Geom_BSplineCurve) SolidBuilder::interpolateCurve(const std::function<gp_Pnt(double)>& position,
                                                         const std::function<gp_Vec(double)>& derivative,
                                                         double first, double last) const
{
    // A cubic Hermite interpolant with 48 samples per turn stays within a few 1e-6 mm for helices of radius 10 mm.
    const double span = last - first;
    if (!(span > 0) || span > 2000.0)
        fail("Unsupported Inventor helix parameter range");
    const int count = std::max(8, static_cast<int>(std::ceil(span / TwoPi * 48.0)) + 1);
    // The NCollection templates directly: OCCT 8 deprecates the TColStd/TColgp typedef headers (same types on 7.x).
    Handle(NCollection_HArray1<gp_Pnt>) points = new NCollection_HArray1<gp_Pnt>(1, count);
    Handle(NCollection_HArray1<double>) parameters = new NCollection_HArray1<double>(1, count);
    NCollection_Array1<gp_Vec> tangents(1, count);
    Handle(NCollection_HArray1<bool>) flags = new NCollection_HArray1<bool>(1, count);
    for (int i = 1; i <= count; ++i) {
        const double u = i == count ? last : first + span * (i - 1) / (count - 1);
        points->SetValue(i, position(u));
        parameters->SetValue(i, u);
        tangents(i) = derivative(u);
        flags->SetValue(i, true);
    }
    GeomAPI_Interpolate interpolate(points, parameters, false, 1e-9);
    interpolate.Load(tangents, flags, false);
    interpolate.Perform();
    if (!interpolate.IsDone())
        fail("Could not interpolate an Inventor helix");
    return interpolate.Curve();
}

Handle(Geom_Surface) SolidBuilder::buildHelixSurface(const Record& record, std::size_t start) const
{
    // The generator is a unit vector d = a M(u) + b N(u) + c A that turns with the helix frame.
    const double v0 = record.number(start + 4) * _scale, v1 = record.number(start + 6) * _scale;
    const double u0 = record.number(start + 8), u1 = record.number(start + 10);
    if (!(v1 > v0) || !(u1 > u0))
        fail("Unsupported Inventor helix surface ranges");
    const Helix helix = readHelix(record, start + 15);
    const Triple majorRaw = record.get<Triple>(start + 16), minorRaw = record.get<Triple>(start + 17),
                 axisRaw = record.get<Triple>(start + 20);
    const Triple coefficients = record.get<Triple>(start + 25);
    const auto combine = [&](double cm, double cn) {
        // cm * M + cn * N, in model units.
        return gp_Vec(majorRaw[0] * cm + minorRaw[0] * cn, majorRaw[1] * cm + minorRaw[1] * cn,
                      majorRaw[2] * cm + minorRaw[2] * cn);
    };
    const auto generator = [&](double u) {
        const gp_Vec radial = combine(std::cos(u), std::sin(u)), tangential = combine(-std::sin(u), std::cos(u));
        return radial * coefficients[0] + tangential * coefficients[1] +
               gp_Vec(axisRaw[0], axisRaw[1], axisRaw[2]) * coefficients[2];
    };
    const auto generatorRate = [&](double u) {
        const gp_Vec radial = combine(-std::sin(u), std::cos(u)), tangential = combine(-std::cos(u), -std::sin(u));
        return radial * coefficients[0] + tangential * coefficients[1];
    };
    const auto rail = [&](double v) {
        return interpolateCurve([&](double u) { return helix.at(u).Translated(generator(u) * v); },
                                [&](double u) { return helix.tangent(u) + generatorRate(u) * v; }, u0, u1);
    };
    return GeomFill::Surface(rail(v0), rail(v1));
}

TopoDS_Face SolidBuilder::joinWindingBoundaries(const TopoDS_Face& face, const Handle(Geom_Surface)& surface) const
{
    // Faces on a periodic surface whose boundaries wind once around it (a band, or a cap around a pole or
    // apex) need a seam edge that OCCT cannot infer. Build it here, joining the boundaries along a straight
    // parameter-space segment, and close the wire.
    struct Use {
        TopoDS_Edge edge;
        Handle(Geom2d_Curve) curve;
        double first, last;
        gp_Pnt2d start() const { return curve->Value(edge.Orientation() == TopAbs_REVERSED ? last : first); }
        gp_Pnt2d end() const { return curve->Value(edge.Orientation() == TopAbs_REVERSED ? first : last); }
    };
    using Ring = std::vector<Use>;
    const auto place = [](Ring& ring, gp_Pnt2d near) {
        for (auto& use : ring) {
            const auto start = use.start();
            use.curve->Translate(gp_Vec2d(std::round((near.X() - start.X()) / TwoPi) * TwoPi,
                                          std::round((near.Y() - start.Y()) / TwoPi) * TwoPi));
            near = use.end();
        }
    };
    constexpr double tolerance = 1e-3; // pcurves of neighbouring edges are only matched to about 1e-4
    struct Winding {
        Ring ring;
        int axis = -1;
        int sign = 0;
    };
    std::vector<Winding> windings;
    for (TopoDS_Iterator w(face); w.More(); w.Next()) {
        Winding item;
        for (TopoDS_Iterator e(w.Value()); e.More(); e.Next()) {
            if (item.ring.size() == 4096)
                return {};
            const auto edge = TopoDS::Edge(e.Value());
            double first, last;
            const auto curve = BRep_Tool::CurveOnSurface(edge, face, first, last);
            if (curve.IsNull())
                return {};
            item.ring.push_back({edge, Handle(Geom2d_Curve)::DownCast(curve->Copy()), first, last});
        }
        if (item.ring.empty())
            return {};
        if (w.Value().Orientation() == TopAbs_REVERSED)
            std::reverse(item.ring.begin(), item.ring.end());
        place(item.ring, item.ring.front().start());
        const gp_Vec2d winding(item.ring.front().start(), item.ring.back().end());
        for (int axis = 0; axis < 2; ++axis) {
            const double along = axis == 0 ? winding.X() : winding.Y();
            const double across = axis == 0 ? winding.Y() : winding.X();
            if (std::abs(across) <= tolerance && std::abs(std::abs(along) - TwoPi) <= tolerance &&
                (axis == 0 ? surface->IsUPeriodic() : surface->IsVPeriodic())) {
                item.axis = axis;
                item.sign = along > 0 ? 1 : -1;
            }
        }
        if (item.axis < 0)
            return {};
        windings.push_back(std::move(item));
    }
    if (windings.empty() || windings.size() > 2)
        return {};
    const int axis = windings.front().axis;
    // Parameter-space helpers: "along" is the periodic direction, "across" the other one.
    const auto cross = [&](const gp_Pnt2d& p, double shift) {
        return axis == 0 ? gp_Pnt2d(p.X() + shift, p.Y()) : gp_Pnt2d(p.X(), p.Y() + shift);
    };
    const auto along = [&](const gp_Pnt2d& p) { return axis == 0 ? p.X() : p.Y(); };
    const auto across = [&](const gp_Pnt2d& p) { return axis == 0 ? p.Y() : p.X(); };
    if (windings.size() == 2 && (windings[1].axis != axis || windings[0].sign == windings[1].sign))
        return {};
    if (windings.size() == 1 && axis != 0)
        return {};

    BRep_Builder builder;
    if (windings.size() == 2 && !(axis == 0 ? surface->IsVPeriodic() : surface->IsUPeriodic())) {
        // Between two parallels of a surface that is not periodic across them, the face can only be the band.
        // Make the lower ring travel in the positive direction, whatever sense the saved loops carry.
        const auto meanAcross = [&](const Winding& item) {
            double sum = 0;
            for (const auto& use : item.ring)
                sum += across(use.start());
            return sum / static_cast<double>(item.ring.size());
        };
        const Winding& lowest = meanAcross(windings[0]) < meanAcross(windings[1]) ? windings[0] : windings[1];
        if (lowest.sign < 0) {
            for (auto& item : windings) {
                std::reverse(item.ring.begin(), item.ring.end());
                for (auto& use : item.ring)
                    use.edge.Reverse();
                place(item.ring, item.ring.front().start());
                item.sign = -item.sign;
            }
        }
    }
    // The ring that travels in the positive direction is the lower boundary; the region lies on its left.
    Winding* lowerWinding = nullptr;
    Winding* upperWinding = nullptr;
    for (auto& item : windings)
        (item.sign > 0 ? lowerWinding : upperWinding) = &item;

    // Pole (or apex) standing in for the missing boundary of a cap.
    TopoDS_Vertex poleVertex;
    double poleParameter = 0;
    if (windings.size() == 1) {
        gp_Pnt polePoint;
        const Handle(Geom_SphericalSurface) sphere = Handle(Geom_SphericalSurface)::DownCast(surface);
        const Handle(Geom_ConicalSurface) cone = Handle(Geom_ConicalSurface)::DownCast(surface);
        double ringParameter = 0;
        for (const auto& use : windings.front().ring)
            ringParameter += across(use.start());
        ringParameter /= static_cast<double>(windings.front().ring.size());
        const bool poleAbove = lowerWinding != nullptr;
        if (sphere) {
            poleParameter = poleAbove ? M_PI / 2 : -M_PI / 2;
            polePoint = sphere->Value(0, poleParameter);
        } else if (cone) {
            poleParameter = -cone->RefRadius() / std::sin(cone->SemiAngle());
            if (poleAbove ? !(poleParameter > ringParameter) : !(poleParameter < ringParameter))
                return {};
            polePoint = cone->Apex();
        } else {
            return {};
        }
        poleVertex = BRepBuilderAPI_MakeVertex(polePoint).Vertex();
    }

    // Seam end points: a vertex of the lower boundary and a vertex of the upper boundary.
    Ring lowerRing, upperRing;
    if (lowerWinding)
        lowerRing = lowerWinding->ring;
    if (upperWinding)
        upperRing = upperWinding->ring;
    std::size_t lowerIndex = 0, upperIndex = 0;
    gp_Pnt2d lowerStart, upperStart;
    TopoDS_Vertex lowerVertex, upperVertex;
    const auto startVertex = [](const Use& use) {
        TopoDS_Vertex first, last;
        TopExp::Vertices(use.edge, first, last, true);
        return first;
    };
    if (windings.size() == 2) {
        double bestLength = 1e300;
        bool found = false;
        gp_Pnt2d bestUpper;
        for (std::size_t i = 0; i < lowerRing.size(); ++i) {
            checkCancellation(_cancelled);
            const gp_Pnt2d a = lowerRing[i].start();
            for (std::size_t j = 0; j < upperRing.size() * 5; ++j) {
                // Try the upper vertex in neighbouring periods too: the boundary may sag past a straight seam.
                const gp_Pnt2d b0 = upperRing[j / 5].start();
                const int period = static_cast<int>(std::lround((along(a) - along(b0)) / TwoPi)) + static_cast<int>(j % 5) - 2;
                const gp_Pnt2d b = cross(b0, period * TwoPi);
                const double length = a.Distance(b);
                if (!(across(b) > across(a)) || length >= bestLength || length < 1e-9)
                    continue;
                // The segment must not touch any boundary except at its end points.
                const Handle(Geom2d_Line) line = new Geom2d_Line(a, gp_Dir2d(gp_Vec2d(a, b)));
                const Handle(Geom2d_TrimmedCurve) segment = new Geom2d_TrimmedCurve(line, 0.0, length);
                bool clear = true;
                for (const Ring* ring : {&lowerRing, &upperRing}) {
                    for (const auto& use : *ring) {
                        for (const int k : {-2, -1, 0, 1, 2}) {
                            Handle(Geom2d_Curve) moved = Handle(Geom2d_Curve)::DownCast(use.curve->Copy());
                            moved->Translate(axis == 0 ? gp_Vec2d(k * TwoPi, 0) : gp_Vec2d(0, k * TwoPi));
                            const Handle(Geom2d_TrimmedCurve) trimmed = new Geom2d_TrimmedCurve(moved, use.first, use.last);
                            Geom2dAPI_InterCurveCurve intersection(segment, trimmed, 1e-7);
                            for (int n = 1; n <= intersection.NbPoints() && clear; ++n) {
                                const gp_Pnt2d hit = intersection.Point(n);
                                if (hit.Distance(a) > tolerance && hit.Distance(b) > tolerance) {
                                    clear = false;
                                }
                            }
                            if (!clear)
                                break;
                        }
                        if (!clear)
                            break;
                    }
                    if (!clear)
                        break;
                }
                if (!clear)
                    continue;
                bestLength = length;
                lowerIndex = i;
                upperIndex = j / 5;
                bestUpper = b;
                found = true;
            }
        }
        if (!found)
            return {};
        lowerStart = lowerRing[lowerIndex].start();
        upperStart = bestUpper;
        lowerVertex = startVertex(lowerRing[lowerIndex]);
        upperVertex = startVertex(upperRing[upperIndex]);
    } else if (lowerWinding) {
        lowerStart = lowerRing[0].start();
        lowerVertex = startVertex(lowerRing[0]);
        upperStart = gp_Pnt2d(lowerStart.X(), poleParameter);
        upperVertex = poleVertex;
    } else {
        upperStart = upperRing[0].start();
        upperVertex = startVertex(upperRing[0]);
        lowerStart = gp_Pnt2d(upperStart.X(), poleParameter);
        lowerVertex = poleVertex;
    }

    const double seamLength = lowerStart.Distance(upperStart);
    if (!(seamLength > 1e-9))
        return {};
    const gp_Dir2d seamDirection(gp_Vec2d(lowerStart, upperStart));
    const int samples = std::max(8, static_cast<int>(std::ceil(seamLength * 8.0)) + 1);
    Handle(NCollection_HArray1<gp_Pnt>) seamPoints = new NCollection_HArray1<gp_Pnt>(1, samples);
    Handle(NCollection_HArray1<double>) seamParameters = new NCollection_HArray1<double>(1, samples);
    for (int i = 1; i <= samples; ++i) {
        const double s = seamLength * (i - 1) / (samples - 1);
        seamPoints->SetValue(i, surface->Value(lowerStart.X() + seamDirection.X() * s,
                                               lowerStart.Y() + seamDirection.Y() * s));
        seamParameters->SetValue(i, s);
    }
    GeomAPI_Interpolate interpolate(seamPoints, seamParameters, false, 1e-9);
    interpolate.Perform();
    if (!interpolate.IsDone())
        return {};
    const Handle(Geom_Curve) seamCurve = interpolate.Curve();
    const double seamTolerance = 1e-4;

    auto result = TopoDS::Face(face.EmptyCopied());
    TopoDS_Edge seam;
    builder.MakeEdge(seam, seamCurve, seamTolerance);
    builder.Add(seam, lowerVertex.Oriented(TopAbs_FORWARD));
    builder.Add(seam, upperVertex.Oriented(TopAbs_REVERSED));
    builder.UpdateVertex(lowerVertex, 0.0, seam, seamTolerance);
    builder.UpdateVertex(upperVertex, seamLength, seam, seamTolerance);
    builder.Range(seam, 0.0, seamLength);
    const Handle(Geom2d_Curve) nearCurve = new Geom2d_Line(lowerStart, seamDirection);
    const Handle(Geom2d_Curve) farCurve = new Geom2d_Line(cross(lowerStart, TwoPi), seamDirection);
    builder.UpdateEdge(seam, farCurve, nearCurve, result, seamTolerance);
    builder.Range(seam, result, 0.0, seamLength);

    const auto addRing = [&](TopoDS_Wire& wire, Ring& ring, std::size_t index, gp_Pnt2d startAt) {
        std::rotate(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(index), ring.end());
        place(ring, startAt);
        for (const auto& use : ring) {
            builder.UpdateEdge(use.edge, use.curve, result, BRep_Tool::Tolerance(use.edge));
            builder.Range(use.edge, result, use.first, use.last);
            builder.Add(wire, use.edge);
        }
    };
    const auto addPole = [&](TopoDS_Wire& wire, bool forward) {
        TopoDS_Edge degenerate;
        builder.MakeEdge(degenerate);
        builder.Degenerated(degenerate, true);
        builder.Add(degenerate, poleVertex.Oriented(TopAbs_FORWARD));
        builder.Add(degenerate, poleVertex.Oriented(TopAbs_REVERSED));
        const Handle(Geom2d_Curve) around = new Geom2d_Line(gp_Pnt2d(lowerStart.X(), poleParameter), gp_Dir2d(1, 0));
        builder.UpdateEdge(degenerate, around, result, seamTolerance);
        builder.Range(degenerate, result, 0.0, TwoPi);
        builder.Add(wire, forward ? degenerate : degenerate.Reversed());
    };
    TopoDS_Wire outer;
    builder.MakeWire(outer);
    if (lowerWinding)
        addRing(outer, lowerRing, lowerIndex, lowerStart);
    else
        addPole(outer, true);
    builder.Add(outer, seam);
    if (upperWinding)
        addRing(outer, upperRing, upperIndex, cross(upperStart, TwoPi));
    else
        addPole(outer, false);
    builder.Add(outer, seam.Reversed());
    outer.Closed(true);
    builder.Add(result, outer);
    return result;
}

TopoDS_Face SolidBuilder::joinToroidalBoundaries(const TopoDS_Face& face, const Geom_ToroidalSurface& torus,
                                                bool reverseWires) const
{
    struct Use {
        TopoDS_Edge edge;
        Handle(Geom2d_Curve) curve;
        double first, last;
        gp_Pnt2d start() const { return curve->Value(edge.Orientation() == TopAbs_REVERSED ? last : first); }
        gp_Pnt2d end() const { return curve->Value(edge.Orientation() == TopAbs_REVERSED ? first : last); }
    };
    using Ring = std::vector<Use>;
    const auto place = [](Ring& ring, gp_Pnt2d near) {
        for (auto& use : ring) {
            const auto start = use.start();
            use.curve->Translate(gp_Vec2d(std::round((near.X() - start.X()) / TwoPi) * TwoPi,
                                          std::round((near.Y() - start.Y()) / TwoPi) * TwoPi));
            near = use.end();
        }
    };
    constexpr double angularTolerance = 1e-5;
    Ring rising, falling, hole;
    for (TopoDS_Iterator w(face); w.More(); w.Next()) {
        Ring ring;
        for (TopoDS_Iterator e(w.Value()); e.More(); e.Next()) {
            if (ring.size() == 4096)
                return {};
            const auto edge = TopoDS::Edge(e.Value());
            double first, last;
            const auto curve = BRep_Tool::CurveOnSurface(edge, face, first, last);
            if (curve.IsNull())
                return {};
            ring.push_back({edge, Handle(Geom2d_Curve)::DownCast(curve->Copy()), first, last});
        }
        if (ring.empty())
            return {};
        if (reverseWires) {
            std::reverse(ring.begin(), ring.end());
            for (auto& use : ring)
                use.edge.Reverse();
        }
        place(ring, ring.front().start());
        const gp_Vec2d winding(ring.front().start(), ring.back().end());
        if (std::abs(winding.X()) > angularTolerance)
            return {};
        Ring* destination = nullptr;
        if (std::abs(winding.Y() - TwoPi) <= angularTolerance)
            destination = &rising;
        else if (std::abs(winding.Y() + TwoPi) <= angularTolerance)
            destination = &falling;
        else if (std::abs(winding.Y()) <= angularTolerance)
            destination = &hole;
        if (!destination || !destination->empty())
            return {};
        *destination = std::move(ring);
    }
    if (rising.empty() || falling.empty() || hole.empty())
        return {};

    // Find existing vertices on a common torus parallel, away from the hole.
    double holeLow = hole.front().start().Y();
    double holeHigh = holeLow;
    for (const auto& use : hole) {
        for (const double f : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const double v = use.curve->Value(use.first + f * (use.last - use.first)).Y();
            holeLow = std::min(holeLow, v);
            holeHigh = std::max(holeHigh, v);
        }
    }
    const double holeMiddle = (holeLow + holeHigh) * 0.5;
    auto right = rising.end();
    auto left = falling.end();
    double clearance = angularTolerance;
    double width = 0;
    for (auto a = rising.begin(); a != rising.end(); ++a) {
        checkCancellation(_cancelled);
        const auto pa = a->start();
        const double fromHole = std::abs(std::remainder(holeMiddle - pa.Y(), TwoPi));
        const double gap = fromHole - (holeHigh - holeLow) * 0.5;
        if (gap <= clearance)
            continue;
        for (auto b = falling.begin(); b != falling.end(); ++b) {
            const auto pb = b->start();
            const double separation = std::remainder(pa.X() - pb.X(), TwoPi);
            if (separation <= angularTolerance ||
                std::abs(std::remainder(pa.Y() - pb.Y(), TwoPi)) > angularTolerance)
                continue;
            right = a;
            left = b;
            width = separation;
            clearance = gap;
            break;
        }
    }
    if (right == rising.end())
        return {};
    std::rotate(rising.begin(), right, rising.end());
    std::rotate(falling.begin(), left, falling.end());
    const gp_Pnt2d rightStart = rising.front().start();
    place(rising, rightStart);
    place(falling, gp_Pnt2d(rightStart.X() - width, rightStart.Y() + TwoPi));
    const auto leftStart = falling.front().start();
    const double low = leftStart.X();
    const double high = rightStart.X();
    const double seamV = rightStart.Y();

    TopoDS_Vertex rightVertex, leftVertex, unused;
    TopExp::Vertices(rising.front().edge, rightVertex, unused, true);
    TopExp::Vertices(falling.front().edge, leftVertex, unused, true);
    BRepBuilderAPI_MakeEdge make(torus.VIso(seamV), leftVertex, rightVertex, low, high);
    if (!make.IsDone())
        return {};
    const auto seam = make.Edge();
    const Handle(Geom2d_Curve) nearCurve = new Geom2d_Line(gp_Pnt2d(0, seamV), gp_Dir2d(1, 0));
    const Handle(Geom2d_Curve) farCurve = new Geom2d_Line(gp_Pnt2d(0, seamV + TwoPi), gp_Dir2d(1, 0));
    auto result = TopoDS::Face(face.EmptyCopied());
    BRep_Builder builder;
    builder.UpdateEdge(seam, nearCurve, farCurve, result, BRep_Tool::Tolerance(seam));
    builder.Range(seam, result, low, high);
    const auto addRing = [&](TopoDS_Wire& wire, const Ring& ring) {
        for (const auto& use : ring) {
            builder.UpdateEdge(use.edge, use.curve, result, BRep_Tool::Tolerance(use.edge));
            builder.Range(use.edge, result, use.first, use.last);
            builder.Add(wire, use.edge);
        }
    };
    TopoDS_Wire outer, inner;
    builder.MakeWire(outer);
    addRing(outer, rising);
    builder.Add(outer, seam.Reversed());
    addRing(outer, falling);
    builder.Add(outer, seam);
    outer.Closed(true);
    builder.Add(result, outer);
    place(hole, gp_Pnt2d((low + high) * 0.5, seamV + TwoPi * 0.5));
    builder.MakeWire(inner);
    addRing(inner, hole);
    inner.Closed(true);
    builder.Add(result, inner);
    return result;
}

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
    std::vector<qint32> pinched;
    std::vector<std::pair<qint32, qint32>> contacts;
    qint32 toroidalContactFace = -1;
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
        if (slit == uses.end()) {
            // A face whose boundary runs out along an edge and straight back (a zero-width spike)
            // uses that edge on both sides. The spike encloses no area; the two other uses are the real
            // manifold pair, so the spike coedges are left out of the boundary.
            const auto spike = std::adjacent_find(uses.begin(), uses.end(), [&](const Use& a, const Use& b) {
                if (a.loop != b.loop || a.face != b.face)
                    return false;
                const Record& first = get(a.coedge, "coedge");
                const Record& second = get(b.coedge, "coedge");
                return first.link(3) == b.coedge && first.get<bool>(7) != second.get<bool>(7) &&
                       second.link(3) != a.coedge;
            });
            if (spike == uses.end()) {
                // Four faces meet along the edge and no face doubles back on itself: the saved shell touches
                // itself along this edge (for example a ridge of a pin that fits exactly into its bore).
                pinched.push_back(edgeId);
                continue;
            }
            _spikeCoedges.insert(spike->coedge);
            _spikeCoedges.insert((spike + 1)->coedge);
            uses.erase(spike, spike + 2);
            continue;
        }
        const Use retraced = *slit;
        uses.erase(slit, slit + 2);
        if (isToroidalContact(retraced.face, edgeId, uses[0].face, uses[1].face)) {
            cut.omittedLoops.insert(retraced.loop);
            toroidalContactFace = retraced.face;
            continue;
        }
        const qint32 support = get(uses[0].face, "face").link(7);
        if (uses[0].face == uses[1].face || support != get(uses[1].face, "face").link(7) ||
            !isTangentContact(retraced.face, edgeId, support)) {
            // Not the cavity-forming tangent contact. The retraced loop encloses no area, so it is left out
            // (or the face is dropped when it has no other loop); the other two uses are the manifold pair.
            const Record& retracedFace = get(retraced.face, "face");
            if (retracedFace.link(4) == retraced.loop && get(retraced.loop, "loop").link(3) == -1)
                cut.omittedFaces.insert(retraced.face);
            else
                cut.omittedLoops.insert(retraced.loop);
            continue;
        }
        cut.omittedLoops.insert(retraced.loop);
        contacts.emplace_back(retraced.face, uses[0].face);
    }
    if (!pinched.empty()) {
        if (!contacts.empty() || toroidalContactFace >= 0)
            fail(unsupported);
        // Split the faces into the pieces that stay connected without the pinch edges; every other edge must
        // be paired. Each pinch edge must then give exactly two of its uses to every piece it touches.
        std::unordered_map<qint32, std::vector<qint32>> links;
        const std::unordered_set<qint32> pinchSet(pinched.begin(), pinched.end());
        for (const auto& [edgeId, uses] : edgeUses) {
            if (pinchSet.count(edgeId) != 0)
                continue;
            const Record& usedEdge = get(edgeId, "edge");
            if (usedEdge.link(8) < 0 && usedEdge.link(3) == usedEdge.link(5))
                continue; // Degenerate edge at a singular point; it has no partner.
            if (uses.size() != 2)
                fail("Inventor shell has an unpaired or non-manifold edge");
            links[uses[0].face].push_back(uses[1].face);
            links[uses[1].face].push_back(uses[0].face);
        }
        std::unordered_map<qint32, int> pieceOf;
        for (const qint32 root : faces) {
            if (pieceOf.count(root) != 0 || cut.omittedFaces.count(root) != 0)
                continue;
            const int index = static_cast<int>(cut.pieces.size());
            cut.pieces.emplace_back();
            std::vector<qint32> pending{root};
            pieceOf[root] = index;
            while (!pending.empty()) {
                checkCancellation(_cancelled);
                const qint32 face = pending.back();
                pending.pop_back();
                cut.pieces[index].push_back(face);
                for (const qint32 neighbor : links[face]) {
                    if (pieceOf.emplace(neighbor, index).second)
                        pending.push_back(neighbor);
                }
            }
        }
        // Around a pinch edge the material wedges alternate with voids. For each of the four faces take the
        // direction pointing into it (in the plane normal to the edge) and the side its material lies on; the
        // partner is the nearest face in that direction, and the pairing must be mutual. Paired faces are
        // joined, which reconnects the pieces that make up each solid.
        std::vector<int> group(cut.pieces.size());
        for (std::size_t i = 0; i < group.size(); ++i)
            group[i] = static_cast<int>(i);
        const std::function<int(int)> root = [&](int i) { return group[i] == i ? i : group[i] = root(group[i]); };
        for (const qint32 edgeId : pinched) {
            const auto& uses = edgeUses.at(edgeId);
            if (uses.size() != 4)
                fail(unsupported);
            std::unordered_map<qint32, TopoDS_Vertex> edgeVertices;
            const TopoDS_Edge edge = buildEdge(edgeId, edgeVertices);
            double first, last;
            const Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
            gp_Pnt middle;
            gp_Vec tangent;
            curve->D1(0.5 * (first + last), middle, tangent);
            if (tangent.Magnitude() <= Precision::Confusion())
                fail(unsupported);
            tangent.Normalize();
            std::array<gp_Vec, 4> inward, material;
            std::array<double, 4> bend{};
            for (std::size_t i = 0; i < 4; ++i) {
                const Record& face = get(uses[i].face, "face");
                const Surface support = buildSurface(get(face.link(7)));
                GeomAPI_ProjectPointOnSurf projection(middle, support.geometry);
                if (projection.NbPoints() == 0)
                    fail(unsupported);
                double u, v;
                projection.LowerDistanceParameters(u, v);
                GeomLProp_SLProps properties(support.geometry, u, v, 1, 1e-9);
                if (!properties.IsNormalDefined())
                    fail(unsupported);
                gp_Vec normal(properties.Normal());
                if (face.get<bool>(8) != support.inverted)
                    normal.Reverse();
                const gp_Vec travel = get(uses[i].coedge, "coedge").get<bool>(7) ? tangent.Reversed() : tangent;
                inward[i] = normal.Crossed(travel).Normalized();
                material[i] = normal.Reversed();
                // Faces that touch tangentially leave the edge in the same direction. Which one curves to which
                // side decides their order; measure it a short step along the face.
                constexpr double step = 1e-3;
                const gp_Pnt probe = middle.Translated(inward[i] * step);
                GeomAPI_ProjectPointOnSurf onFace(probe, support.geometry);
                if (onFace.NbPoints() > 0)
                    bend[i] = std::atan2(gp_Vec(probe, onFace.NearestPoint()).Dot(tangent.Crossed(inward[i])), step);
            }
            const gp_Vec e1 = inward[0], e2 = tangent.Crossed(e1);
            std::array<double, 4> angle;
            for (std::size_t i = 0; i < 4; ++i)
                angle[i] = std::atan2(inward[i].Dot(e2), inward[i].Dot(e1)) + bend[i];
            std::array<int, 4> partner;
            for (std::size_t i = 0; i < 4; ++i) {
                const double side = material[i].Dot(tangent.Crossed(inward[i]));
                if (std::abs(side) < 1e-9)
                    fail(unsupported);
                const double direction = side > 0 ? 1.0 : -1.0;
                double best = TwoPi + 1;
                partner[i] = -1;
                for (std::size_t k = 0; k < 4; ++k) {
                    if (k == i)
                        continue;
                    double delta = std::fmod(direction * (angle[k] - angle[i]), TwoPi);
                    if (delta < 0)
                        delta += TwoPi;
                    if (delta < 1e-9)
                        fail(unsupported);
                    if (delta < best) {
                        best = delta;
                        partner[i] = static_cast<int>(k);
                    }
                }
            }
            for (std::size_t i = 0; i < 4; ++i) {
                if (partner[i] < 0 || partner[partner[i]] != static_cast<int>(i))
                    fail("Inventor shell pinch has no consistent pairing of its four faces");
                group[root(pieceOf.at(uses[i].face))] = root(pieceOf.at(uses[partner[i]].face));
            }
        }
        std::unordered_map<int, std::size_t> merged;
        std::vector<std::vector<qint32>> solids;
        for (std::size_t i = 0; i < cut.pieces.size(); ++i) {
            const auto [found, inserted] = merged.try_emplace(root(static_cast<int>(i)), solids.size());
            if (inserted)
                solids.emplace_back();
            solids[found->second].insert(solids[found->second].end(), cut.pieces[i].begin(), cut.pieces[i].end());
        }
        cut.pieces = std::move(solids);
        if (cut.pieces.size() < 2)
            fail(unsupported);
        return cut;
    }
    if (contacts.empty() && toroidalContactFace < 0)
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
    if (toroidalContactFace >= 0) {
        if (!contacts.empty() || connectedFaces(toroidalContactFace).size() != faces.size())
            fail("Inventor toroidal contact does not belong to one connected shell");
        return cut;
    }
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

bool SolidBuilder::isToroidalContact(qint32 faceId, qint32 edgeId, qint32 firstFace, qint32 secondFace) const
{
    const Record& torusRecord = get(get(faceId, "face").link(7));
    if (torusRecord.type != "torus-surface")
        return false;
    const Record* planeRecord = &get(get(firstFace, "face").link(7));
    const Record* splineRecord = &get(get(secondFace, "face").link(7));
    if (planeRecord->type == "spline-surface")
        std::swap(planeRecord, splineRecord);
    if (planeRecord->type != "plane-surface" || splineRecord->type != "spline-surface")
        return false;

    const auto torus = Handle(Geom_ToroidalSurface)::DownCast(buildSurface(torusRecord).geometry);
    const gp_Pln plane = Handle(Geom_Plane)::DownCast(buildSurface(*planeRecord).geometry)->Pln();
    // The torus touches this plane at a tube extremum along its axis.
    const double tolerance = MaxSplineToleranceMm;
    if (!torus->Axis().Direction().IsParallel(plane.Axis().Direction(), Precision::Angular()) ||
        std::abs(plane.Distance(torus->Location()) - torus->MinorRadius()) > tolerance)
        return false;
    std::unordered_map<qint32, TopoDS_Vertex> vertices;
    const TopoDS_Edge edge = buildEdge(edgeId, vertices);
    double first, last;
    const auto curve = BRep_Tool::Curve(edge, first, last);
    const double curveTolerance = std::max(CheckToleranceMm, BRep_Tool::Tolerance(edge));
    const gp_Vec axis(torus->Axis().Direction());
    for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const auto p = curve->Value(first + fraction * (last - first));
        const gp_Vec offset(torus->Location(), p);
        const double height = offset.Dot(axis);
        const double radius = (offset - axis * height).Magnitude();
        const double tubeDistance = std::hypot(height, radius - torus->MajorRadius());
        if (plane.Distance(p) > curveTolerance ||
            std::abs(tubeDistance - torus->MinorRadius()) > curveTolerance)
            return false;
    }
    return true;
}

void SolidBuilder::findCircularBands()
{
    std::unordered_map<qint32, unsigned> vertexEdges;
    std::unordered_map<qint32, unsigned> closedLoops;
    std::unordered_map<qint32, unsigned> edgeCoedges;
    for (const Record& record : _records) {
        if (record.type == "edge" || record.type == "tedge-edge") {
            ++vertexEdges[record.link(3)];
            if (record.link(3) != record.link(5))
                ++vertexEdges[record.link(5)];
        } else if (record.type == "coedge" || record.type == "tcoedge-coedge") {
            ++edgeCoedges[record.link(6)];
        } else if (record.type == "loop" && record.link(4) >= 0) {
            const Record& coedge = get(record.link(4));
            if ((coedge.type == "coedge" || coedge.type == "tcoedge-coedge") && coedge.link(3) == record.link(4))
                ++closedLoops[coedge.link(6)];
        }
    }

    for (std::size_t faceId = 0; faceId < _records.size(); ++faceId) {
        checkCancellation(_cancelled);
        const Record& face = _records[faceId];
        if (face.type != "face" || face.link(7) < 0 || face.link(4) < 0 || face.get<bool>(9))
            continue;
        const Record& support = get(face.link(7));
        const bool toroidal = support.type == "torus-surface";
        const bool circularCone = support.type == "cone-surface" && std::abs(support.number(6) - 1.0) <= 1e-9;
        const bool cylindrical = circularCone && std::abs(support.number(9)) <= 1e-12;
        const bool conical = circularCone && !cylindrical && std::abs(support.number(10)) > 1e-12;
        if (!toroidal && !cylindrical && !conical)
            continue;
        const double semiAngle = conical ? std::atan(support.number(9) / support.number(10)) : 0.0;
        const Record& lower = get(face.link(4), "loop");
        if (lower.link(3) < 0)
            continue;
        const Record& upper = get(lower.link(3), "loop");
        if (upper.link(3) != -1)
            continue;

        const gp_Pnt origin = point(support.get<Triple>(3));
        const gp_Dir axis = direction(support.get<Triple>(4));
        const double radius = toroidal ? std::abs(length(support.number(5))) : vector(support.get<Triple>(5)).Magnitude();
        const double tubeRadius = toroidal ? length(support.number(6)) : 0.0;
        // A spindle torus (tube larger than the major radius) is allowed; its meridian circle crosses the axis.
        if (toroidal && (std::abs(tubeRadius) <= Precision::Confusion() || radius <= Precision::Confusion()))
            continue;
        // Adjacent bands can store opposite axes. Pick the same physical
        // meridian for both so their shared circles can be sewn.
        const std::array<double, 3> components{axis.X(), axis.Y(), axis.Z()};
        const auto dominant = std::max_element(components.begin(), components.end(),
            [](double a, double b) { return std::abs(a) < std::abs(b); });
        const gp_Dir seam = gp_Ax2(origin, *dominant < 0 ? axis.Reversed() : axis).XDirection();
        qint32 edgeIds[2];
        double parameters[2];
        bool lowerBoundary[2];
        bool singleEdge[2] = {false, false};
        bool usable = true;
        for (unsigned i = 0; i < 2; ++i) {
            const Record& loop = i == 0 ? lower : upper;
            const Record& coedge = get(loop.link(4), "coedge");
            const qint32 edgeId = coedge.link(6);
            // A boundary is either one closed circle edge or a ring of arcs that together close one circle.
            std::vector<const Record*> ring;
            qint32 walk = loop.link(4);
            do {
                const Record& item = get(walk, "coedge");
                ring.push_back(&item);
                walk = item.link(3);
            } while (walk != loop.link(4) && ring.size() <= 64);
            if (walk != loop.link(4)) {
                usable = false;
                break;
            }
            const bool single = ring.size() == 1;
            if (single && closedLoops[edgeId] != 2) {
                usable = false;
                break;
            }
            const Record& edge = get(edgeId, "edge");
            if (single && (edge.link(8) < 0 || edge.link(3) != edge.link(5) || vertexEdges[edge.link(3)] != 1 ||
                           std::abs(edge.number(6) - edge.number(4) - TwoPi) > Precision::PConfusion())) {
                usable = false;
                break;
            }
            if (edge.link(8) < 0) {
                usable = false;
                break;
            }
            const Record& circle = get(edge.link(8));
            if (circle.type != "ellipse-curve" || circle.number(6) != 1.0 ||
                !axis.IsParallel(direction(circle.get<Triple>(4)), Precision::Angular())) {
                usable = false;
                break;
            }
            bool senseOfFirst = false;
            if (!single) {
                double span = 0.0;
                for (std::size_t k = 0; k < ring.size() && usable; ++k) {
                    const Record& arcEdge = get(ring[k]->link(6), "edge");
                    if (edgeCoedges[ring[k]->link(6)] != 2 || arcEdge.link(8) < 0) {
                        usable = false;
                        break;
                    }
                    const Record& arcCircle = get(arcEdge.link(8));
                    if (arcCircle.type != "ellipse-curve" || arcCircle.number(6) != 1.0 ||
                        (point(arcCircle.get<Triple>(3)).Distance(point(circle.get<Triple>(3))) > CheckToleranceMm) ||
                        std::abs(vector(arcCircle.get<Triple>(5)).Magnitude() - vector(circle.get<Triple>(5)).Magnitude()) > CheckToleranceMm ||
                        !direction(arcCircle.get<Triple>(4)).IsParallel(direction(circle.get<Triple>(4)), Precision::Angular())) {
                        usable = false;
                        break;
                    }
                    const bool arcSense = (axis.Dot(direction(arcCircle.get<Triple>(4))) < 0) ^ arcEdge.get<bool>(9) ^
                                          ring[k]->get<bool>(7);
                    if (k == 0)
                        senseOfFirst = arcSense;
                    else if (arcSense != senseOfFirst)
                        usable = false;
                    span += std::abs(arcEdge.number(6) - arcEdge.number(4));
                }
                if (!usable || std::abs(span - TwoPi) > 1e-6) {
                    usable = false;
                    break;
                }
            }
            const auto prior = _circleSeams.find(edgeId);
            if (single && prior != _circleSeams.end() && !seam.IsEqual(prior->second, Precision::Angular())) {
                usable = false;
                break;
            }
            singleEdge[i] = single;
            const gp_Vec offset(origin, point(circle.get<Triple>(3)));
            const double axial = offset.Dot(gp_Vec(axis));
            const double circleRadius = vector(circle.get<Triple>(5)).Magnitude();
            double radial = circleRadius - radius;
            bool lemonParallel = false;
            if (toroidal) {
                // The parallel may lie on the far side of the axis of a spindle torus (negative distance), on its inner
                // (lemon) part, where the parametrisation's normal is opposite to the saved one (see buildFace).
                const double farRadial = -circleRadius - radius;
                if (std::abs(std::hypot(farRadial, axial) - std::abs(tubeRadius)) <
                    std::abs(std::hypot(radial, axial) - std::abs(tubeRadius))) {
                    radial = farRadial;
                    lemonParallel = true;
                }
            }
            // On a cone the parallel radius grows linearly with the axial offset.
            const double radialError = toroidal ? std::abs(std::hypot(radial, axial) - std::abs(tubeRadius))
                                     : conical  ? std::abs(circleRadius - (radius + axial * std::tan(semiAngle)))
                                                : std::abs(radial);
            if (radialError > CheckToleranceMm || (offset - gp_Vec(axis) * axial).Magnitude() > CheckToleranceMm) {
                usable = false;
                break;
            }
            edgeIds[i] = edgeId;
            parameters[i] = toroidal ? std::atan2(axial, radial) : conical ? axial / std::cos(semiAngle) : axial;
            const bool reversed = axis.Dot(direction(circle.get<Triple>(4))) < 0;
            lowerBoundary[i] = !(reversed ^ edge.get<bool>(9) ^ coedge.get<bool>(7) ^
                                 face.get<bool>(8) ^ (tubeRadius < 0) ^ lemonParallel);
        }
        if (!usable || edgeIds[0] == edgeIds[1])
            continue;
        double firstV = std::min(parameters[0], parameters[1]);
        double lastV = std::max(parameters[0], parameters[1]);
        if (toroidal) {
            if (lowerBoundary[0] == lowerBoundary[1])
                continue;
            // On a forward face the lower parallel is traversed in increasing U.
            // The saved coedges and face sense choose the meridian arc, which may cross V = 0.
            firstV = parameters[lowerBoundary[0] ? 0 : 1];
            lastV = parameters[lowerBoundary[0] ? 1 : 0];
            if (lastV < firstV)
                lastV += TwoPi;
            if (lastV - firstV >= TwoPi - Precision::PConfusion())
                continue;
        }
        if (lastV - firstV <= (toroidal ? Precision::PConfusion() : CheckToleranceMm))
            continue;
        // These two trims are whole circles. Moving their closure vertices to
        // the same meridian preserves geometry and gives OCCT a valid seam.
        if (singleEdge[0])
            _circleSeams.emplace(edgeIds[0], seam);
        if (singleEdge[1])
            _circleSeams.emplace(edgeIds[1], seam);
        const gp_Ax3 axes(origin, axis, seam);
        Handle(Geom_Surface) surface;
        if (toroidal)
            surface = new Geom_ToroidalSurface(axes, radius, std::abs(tubeRadius));
        else if (conical)
            surface = new Geom_ConicalSurface(axes, semiAngle, radius);
        else
            surface = new Geom_CylindricalSurface(axes, radius);
        _circularBands.emplace(static_cast<qint32>(faceId), CircularBand{surface, firstV, lastV});
    }
}

TopoDS_Vertex SolidBuilder::vertex(qint32 id, std::unordered_map<qint32, TopoDS_Vertex>& vertices) const
{
    auto [found, inserted] = vertices.try_emplace(id);
    if (inserted) {
        const Record& vertexRecord = get(id, "vertex");
        const Record& record = get(vertexRecord.link(5), "point");
        found->second = BRepBuilderAPI_MakeVertex(point(record.get<Triple>(3))).Vertex();
        if (vertexRecord.type == "tvertex-vertex") {
            // A saved tolerance of -1 (file units) marks a vertex without a stored tolerance.
            // Some vertices leave that field at -1 and keep the value in the next field; the gap between such
            // a vertex and its curve equals that value.
            std::size_t toleranceField = 7;
            if (vertexRecord.number(7) == -1.0 && vertexRecord.fields.size() > 8 && vertexRecord.number(8) > 0)
                toleranceField = 8;
            if (vertexRecord.number(toleranceField) != -1.0) {
                const double tolerance = length(vertexRecord.number(toleranceField));
                if (tolerance < 0 || tolerance > MaxSavedToleranceMm)
                    throw std::runtime_error("Unsupported Inventor vertex tolerance " + std::to_string(tolerance) + " mm");
                BRep_Builder().UpdateVertex(found->second, tolerance);
            }
        }
    }
    return found->second;
}

TopoDS_Edge SolidBuilder::buildEdge(qint32 id, std::unordered_map<qint32, TopoDS_Vertex>& vertices) const
{
    const Record& edge = get(id, "edge");
    if (edge.link(8) < 0)
        throw std::runtime_error("Inventor edge " + std::to_string(id) + " has no saved curve (vertices " +
                                 std::to_string(edge.link(3)) + " and " + std::to_string(edge.link(5)) + ")");
    const Record& curve = get(edge.link(8));
    TopoDS_Vertex start = vertex(edge.link(3), vertices);
    TopoDS_Vertex finish = vertex(edge.link(5), vertices);
    const gp_Pnt startPoint = BRep_Tool::Pnt(start);
    const gp_Pnt endPoint = BRep_Tool::Pnt(finish);
    Handle(Geom_Curve) geometry;
    double first = edge.number(4);
    double last = edge.number(6);
    double tolerance = CheckToleranceMm;
    if (curve.type == "straight-curve") {
        geometry = new Geom_Line(point(curve.get<Triple>(3)), direction(curve.get<Triple>(4)));
        // Some saved lines use a displacement vector rather than a unit direction.
        const double millimetersPerParameter = vector(curve.get<Triple>(4)).Magnitude();
        first *= millimetersPerParameter;
        last *= millimetersPerParameter;
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
    } else if (curve.type == "intcurve-curve" && tokenAt(*resolveBlock(curve).first, resolveBlock(curve).second + 1, SplineToken::HelixCurve)) {
        const auto [block, start] = resolveBlock(curve);
        // An edge that runs against the curve has its parameters in the reversed curve, as for a spline: the helix is
        // evaluated at the negated parameter. The end-vertex check below confirms the sign on every such edge.
        const bool reversedHelix = edge.get<bool>(9) != curve.get<bool>(3);
        const double low = reversedHelix ? -last : first;
        const double high = reversedHelix ? -first : last;
        if (low < block->number(start + 4) - Precision::PConfusion() ||
            high > block->number(start + 6) + Precision::PConfusion())
            fail("Inventor helix trim is outside the saved curve domain");
        const Helix helix = readHelix(*block, start + 7, true);
        const double sign = reversedHelix ? -1.0 : 1.0;
        geometry = interpolateCurve([&](double t) { return helix.at(sign * t); },
                                    [&](double t) { return helix.tangent(sign * t) * sign; }, first, last);
        tolerance = std::max(tolerance, 1e-4);
    } else if (curve.type == "intcurve-curve") {
        const auto spline = buildSplineCurve(curve, tolerance);
        const bool reverse = edge.get<bool>(9) != curve.get<bool>(3);
        const double low = reverse ? -last : first;
        const double high = reverse ? -first : last;
        const double domainFirst = spline->FirstParameter();
        const double domainLast = spline->LastParameter();
        if (low < domainFirst - Precision::PConfusion() || high > domainLast + Precision::PConfusion()) {
            if (!spline->IsClosed())
                fail("Inventor spline trim is outside the saved curve domain");
            spline->SetPeriodic();
        }
        if (reverse) {
            first = spline->ReversedParameter(high);
            last = spline->ReversedParameter(low);
            spline->Reverse();
        }
        geometry = spline;
    } else {
        throw std::runtime_error("Unsupported Inventor edge curve: " + curve.type.toStdString());
    }

    if (edge.type == "tedge-edge") {
        const double savedTolerance = length(edge.number(11));
        if (savedTolerance < 0 || savedTolerance > MaxSavedToleranceMm)
            throw std::runtime_error("Unsupported Inventor edge tolerance " + std::to_string(savedTolerance) + " mm");
        tolerance = std::max(tolerance, savedTolerance);
    }
    if (curve.type != "intcurve-curve" && edge.get<bool>(9))
        geometry->Reverse();
    // A periodic spline is cut to the part the edge uses: the same curve over [first, last], but not periodic. Sewing cuts and
    // bounds the edge curves again and again, and OCCT 8.0.1 writes past a knot array while it does that to a periodic spline
    // whose knots are double (the heap check reports it; a hex nut with a thread crashed the importer).
    if (const Handle(Geom_BSplineCurve) periodic = Handle(Geom_BSplineCurve)::DownCast(geometry);
        !periodic.IsNull() && periodic->IsPeriodic() && first < last && last - first <= periodic->Period() + Precision::PConfusion()) {
        const Handle(Geom_BSplineCurve) trimmed = Handle(Geom_BSplineCurve)::DownCast(periodic->Copy());
        trimmed->Segment(first, last);
        geometry = trimmed;
    }
    if (!(first < last) || (geometry->IsPeriodic() && last - first > geometry->Period() + Precision::PConfusion()))
        throw std::runtime_error("Inventor edge " + std::to_string(id) + " endpoints disagree with the saved trim parameters");
    {
        // The end vertices must lie on the curve within the tolerance the file gives. A saved intersection curve is a fitted
        // spline, and its ends can miss the vertices by more than its own fit tolerance (about 0.01 mm against 0.004 mm on
        // the oversized part), so for those the residual is accepted up to the safety cap and carried into the edge and its
        // vertices below. Residuals of a millimetre or more still fail: that is a wrong curve, not a loose fit.
        // A helix curve is evaluated from its exact model, not fitted, so it keeps the strict check.
        const bool fittedSpline = curve.type == "intcurve-curve" &&
                                  !tokenAt(*resolveBlock(curve).first, resolveBlock(curve).second + 1, SplineToken::HelixCurve);
        const double startResidual = geometry->Value(first).Distance(startPoint);
        const double endResidual = geometry->Value(last).Distance(endPoint);
        const auto allowed = [&](const TopoDS_Vertex& vertex) {
            const double saved = std::max(tolerance, BRep_Tool::Tolerance(vertex));
            return fittedSpline ? std::max(saved, MaxSplineToleranceMm) : saved;
        };
        if (!(startResidual <= allowed(start)) || !(endResidual <= allowed(finish)))
            throw std::runtime_error("Inventor edge " + std::to_string(id) + " endpoints disagree with the saved trim parameters");
        if (fittedSpline)
            tolerance = std::max({tolerance, startResidual + Precision::Confusion(), endResidual + Precision::Confusion()});
    }

    if (const auto seam = _circleSeams.find(id); seam != _circleSeams.end()) {
        const Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(geometry);
        const gp_Ax2 axes(circle->Location(), circle->Axis().Direction(), seam->second);
        geometry = new Geom_Circle(axes, circle->Radius());
        start = BRepBuilderAPI_MakeVertex(geometry->Value(0.0)).Vertex();
        finish = start;
        first = 0.0;
        last = TwoPi;
    }

    BRep_Builder builder;
    if (tolerance > CheckToleranceMm) {
        builder.UpdateVertex(start, tolerance);
        builder.UpdateVertex(finish, tolerance);
    }
    // OCCT checks the stored vertex tolerance when supplied a curve parameter.
    // Carry the accepted endpoint residual into the vertex without moving it.
    const auto allowEndpointResidual = [&](const TopoDS_Vertex& vertex, double parameter) {
        const double residual = geometry->Value(parameter).Distance(BRep_Tool::Pnt(vertex));
        if (residual > BRep_Tool::Tolerance(vertex))
            builder.UpdateVertex(vertex, std::min(tolerance, residual + Precision::Confusion()));
    };
    allowEndpointResidual(start, first);
    allowEndpointResidual(finish, last);
    BRepBuilderAPI_MakeEdge make(geometry, start, finish, first, last);
    if (!make.IsDone()) {
        const std::string prefix = "Could not construct Inventor edge " + std::to_string(id) + " in OCCT";
        if (make.Error() == BRepBuilderAPI_DifferentsPointAndParameter)
            throw std::runtime_error(prefix + ": trim parameter and vertex coordinates disagree");
        throw std::runtime_error(prefix + " (edge builder error " + std::to_string(make.Error()) + ")");
    }
    const TopoDS_Edge result = make.Edge();
    if (tolerance > CheckToleranceMm)
        builder.UpdateEdge(result, tolerance);
    return result;
}

bool SolidBuilder::ellipticCone(const Record& surface, EllipticCone& cone) const
{
    if (surface.type != "cone-surface")
        return false;
    const double ratio = surface.number(6);
    const double sine = surface.number(9);
    const double cosine = surface.number(10);
    const gp_Vec radial = vector(surface.get<Triple>(5));
    const double radius = radial.Magnitude();
    if (!std::isfinite(radius) || !std::isfinite(ratio) || radius <= Precision::Confusion() || std::abs(cosine) < 1e-12 ||
        ratio < 1e-3 || ratio > 1.0)
        return false;
    if (std::abs(ratio - 1) <= 1e-9 || radius * std::abs(1.0 - ratio) <= MaxCircularApproximationMm)
        return false; // circular for our purposes
    cone = {point(surface.get<Triple>(3)), direction(surface.get<Triple>(4)), gp_Dir(radial), radius, ratio, sine / cosine, cosine < 0};
    return true;
}

// The heights along the cone axis that the face's boundary reaches, with a margin. The face's edges are built once here and
// again by the face itself, so the edge-use counts of the shell are put back.
std::optional<std::pair<double, double>> SolidBuilder::coneHeightRange(const Record& face, qint32 faceId,
                                                                       const std::unordered_set<qint32>& omittedLoops,
                                                                       const EllipticCone& cone)
{
    const auto savedUses = _edgeUses;
    std::unordered_map<qint32, FaceEdge> edges;
    std::unordered_map<qint32, TopoDS_Vertex> vertices;
    std::unordered_set<qint32> visited;
    try {
        for (qint32 loopId = face.link(4); loopId != -1;) {
            if (!visited.insert(loopId).second)
                fail("Cyclic Inventor loop list");
            const Record& loop = get(loopId, "loop");
            if (loop.link(5) != faceId)
                fail("Inventor loop belongs to a different face");
            if (omittedLoops.count(loopId) == 0)
                buildWire(loop, loopId, edges, vertices);
            loopId = loop.link(3);
        }
    } catch (...) {
        _edgeUses = savedUses;
        throw;
    }
    _edgeUses = savedUses;
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (const auto& item : edges) {
        double first, last;
        const Handle(Geom_Curve) curve = BRep_Tool::Curve(item.second.shape, first, last);
        if (curve.IsNull())
            continue;
        for (int i = 0; i <= 32; ++i) {
            const double height = gp_Vec(cone.origin, curve->Value(first + (last - first) * i / 32.0)).Dot(gp_Vec(cone.axis));
            low = std::min(low, height);
            high = std::max(high, height);
        }
    }
    if (!(low < high))
        return std::nullopt;
    const double margin = 0.05 * (high - low) + 0.2;
    low -= margin;
    high += margin;
    // The section scale 1 + h * tangent / radius must stay positive: a face that reaches the apex is not supported.
    const double scaleLow = 1.0 + low * cone.tangent / cone.radius;
    const double scaleHigh = 1.0 + high * cone.tangent / cone.radius;
    if (!(scaleLow > 0.02) || !(scaleHigh > 0.02))
        return std::nullopt;
    return std::make_pair(low, high);
}

// An elliptic cone as a ruled surface between its elliptical sections at two heights: exact, since the sections are similar and
// the generators are straight. u runs round the ellipse from the major axis, v from the low section to the high one.
Handle(Geom_Surface) SolidBuilder::ellipticConeSurface(const EllipticCone& cone, double low, double high) const
{
    const auto section = [&](double height) {
        const double scale = 1.0 + height * cone.tangent / cone.radius;
        const gp_Ax2 axes(cone.origin.Translated(gp_Vec(cone.axis) * height), cone.axis, cone.major);
        return GeomConvert::CurveToBSplineCurve(new Geom_Ellipse(axes, cone.radius * scale, cone.radius * cone.ratio * scale));
    };
    Handle(Geom_Surface) ruled = GeomFill::Surface(section(low), section(high));
    if (const Handle(Geom_BSplineSurface) spline = Handle(Geom_BSplineSurface)::DownCast(ruled);
        !spline.IsNull() && spline->IsUClosed()) {
        try {
            spline->SetUPeriodic();
        } catch (const Standard_Failure&) {
            // Left as a closed, non-periodic surface.
        }
    }
    return ruled;
}

SolidBuilder::Surface SolidBuilder::buildSurface(const Record& surface, const std::pair<double, double>* coneHeights) const
{
    if (EllipticCone elliptic; ellipticCone(surface, elliptic)) {
        if (!coneHeights)
            fail("Unsupported Inventor conical surface profile");
        return {ellipticConeSurface(elliptic, coneHeights->first, coneHeights->second), elliptic.inverted};
    }
    if (surface.type == "spline-surface") {
        const auto [block, start] = resolveBlock(surface);
        if (tokenAt(*block, start + 1, SplineToken::HelixSurface))
            return {buildHelixSurface(*block, start), !surface.get<bool>(3)};
    }
    if (surface.type == "spline-surface")
        return {buildSplineSurface(surface), surface.get<bool>(3)};
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
        if (!std::isfinite(radius) || !std::isfinite(ratio) || radius <= Precision::Confusion() || std::abs(cosine) < 1e-12)
            fail("Unsupported Inventor conical surface profile");
        // The saved cone has a minor/major radius ratio. A ratio below 1 is an elliptical cone or cylinder, which OCCT has no
        // surface for. Many saved cones are circular up to rounding (ratio 0.9999, 0.9985): a circle of the mean radius is within
        // half the axis difference of the ellipse, accepted up to MaxCircularApproximationMm. Real ellipses are built by
        // ellipticConeSurface when the face supplies its height range; here they are refused.
        const double axisDifference = radius * std::abs(1.0 - ratio);
        if (axisDifference > MaxCircularApproximationMm && std::abs(ratio - 1) > 1e-9)
            fail("Unsupported Inventor conical surface profile");
        const double meanRadius = radius * (1.0 + std::min(ratio, 1.0)) / 2.0;
        const gp_Pnt origin = point(surface.get<Triple>(3));
        const gp_Ax3 axes(origin, direction(surface.get<Triple>(4)), gp_Dir(radial));
        if (std::abs(sine) < 1e-12)
            return {new Geom_CylindricalSurface(axes, meanRadius), cosine < 0};
        return {new Geom_ConicalSurface(axes, std::atan(sine / cosine), meanRadius), cosine < 0};
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
        // A minor radius larger than the major radius is a legal spindle torus; OCCT accepts it.
        if (std::abs(minor) <= Precision::Confusion() || major <= Precision::Confusion())
            throw std::runtime_error("Unsupported Inventor torus radii (major " + std::to_string(major) + " mm, minor " + std::to_string(minor) + " mm)");
        const gp_Ax3 axes(origin, direction(surface.get<Triple>(4)), direction(surface.get<Triple>(7)));
        return {new Geom_ToroidalSurface(axes, major, std::abs(minor)), minor < 0};
    }
    throw std::runtime_error("Unsupported Inventor face surface: " + surface.type.toStdString());
}

} // namespace

std::vector<BodyOutcome> readPartBodies(const QString& path, const std::function<bool()>& cancelled,
                                        PartAppearance* appearance, bool strict)
{
    checkCancellation(cancelled);
    CompoundFile container(path);
    const QByteArray bytes = readPartBRepSegment(container, SegmentLimit);
    SavedBodyReader reader(bytes, cancelled);
    const auto records = reader.read();
    std::optional<SavedAppearance> saved;
    std::optional<PartColorTable> colorTable;
    if (appearance) {
        *appearance = {};
        if (std::count_if(records.begin(), records.end(), [](const Record& row) { return row.type == "body"; }) == 1)
            saved = readPartAppearance(container);
        // Several bodies: the graphics hang from several shells. A single body whose strict reading finds no face colour at
        // all, while the faces of the part do carry some (they hang from another shell node than the body's), takes the
        // colours the same way as a part with several bodies. A strict reading that found face colours is kept as it was.
        const bool strictHasFaceColours = saved && std::any_of(saved->faces.begin(), saved->faces.end(),
                                                               [](const auto& item) { return item.second.has_value(); });
        if (!strictHasFaceColours) {
            colorTable = readPartColorTable(container);
            if (saved && colorTable && !colorTable->faces.empty()) {
                saved.reset();
            } else if (saved) {
                colorTable.reset();
            }
        }
        checkCancellation(cancelled);
    }
    SolidBuilder builder(records, reader.millimeters, cancelled, strict);
    if (colorTable)
        builder.useColorTable(&*colorTable);
    auto outcomes = builder.build(saved ? &*saved : nullptr, appearance);
    // A part whose graphics save no colour has the appearance its own colour style names (Inventor shows that colour, and its
    // STEP export carries it): the part's default, below any colour a face or the graphics body gives.
    if (appearance && !appearance->body) {
        if (const auto style = readPartStyleColor(container))
            appearance->body = Quantity_Color(style->color[0], style->color[1], style->color[2], Quantity_TOC_RGB);
    }

    // A part with several bodies: name them as the model browser does. A saved body is matched to its browser body by its
    // face count, which must equal a graphics shell of exactly one browser body; anything less certain keeps the numbered
    // names.
    if (builder.bodyFaceCounts().size() > 1) {
        const auto infos = readPartBodyInfo(container);
        for (std::size_t record = 0; record < builder.bodyFaceCounts().size(); ++record) {
            const std::size_t faces = builder.bodyFaceCounts()[record];
            const PartBodyInfo* match = nullptr;
            bool unique = faces != 0;
            for (const auto& info : infos) {
                if (std::find(info.faceCounts.begin(), info.faceCounts.end(), faces) == info.faceCounts.end())
                    continue;
                unique = unique && !match;
                match = &info;
            }
            if (!match || !unique)
                continue;
            for (auto& outcome : outcomes) {
                if (outcome.bodyRecord == static_cast<int>(record)) {
                    outcome.bodyName = match->name;
                    outcome.featureName = match->feature;
                }
            }
        }
    }
    return outcomes;
}

std::vector<TopoDS_Solid> readPartSolids(const QString& path, const std::function<bool()>& cancelled,
                                       PartAppearance* appearance)
{
    std::vector<TopoDS_Solid> solids;
    for (auto& outcome : readPartBodies(path, cancelled, appearance, true))
        solids.push_back(std::move(outcome.solid));
    return solids;
}

} // namespace Inventor
