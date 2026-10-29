#pragma once
#include <QByteArray>
#include <QSettings>
#include <QStringList>
#include <QSet>
#include <QtGlobal>
#include <TopoDS_Shape.hxx>
#include <assimp/scene.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
#include <array>
#include <optional>
#include <cstddef>
#include <limits>

namespace Jt {
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("Model loading cancelled by user.") {} };
struct Options {
    // Cumulative charged allocations, including released scratch arrays; not peak RSS.
    // The decoder also retains a separate 512 MiB per-allocation limit.
    size_t decodedAllocationBudget=size_t(16)*1024*1024*1024;
    std::function<bool()> cancelled;
    std::function<void(const QString&,int,int)> progress;
    // Match the measured exporter profile (DM 10.8.0.1 / JT 10.5, 10.7-10.10) duplicate
    // fallback-material convention. False retains published list order.
    bool duplicateFallbackMaterialCompatibility=true;
    // Optional independently licensed XT backend. Empty in the native mesh-only build.
    std::function<TopoDS_Shape(const QByteArray&)> exactDecoder;
    // Optional import-time eye position in JT world coordinates / source units.
    // Unset selects the first RangeLOD child; this is not runtime LOD switching.
    std::optional<std::array<double,3>> lodEyePoint;
    // Meshes of one leaf part that share a material and vertex layout become one mesh (each shape's transform is baked into
    // its vertices). The part is one body; thousands of shape meshes make a large assembly slow to display. The individual
    // shape nodes and their metadata are not kept. MODELVIEWER_JT_MERGE_PART_MESHES=0 turns it off in fromEnvironment().
    bool mergePartMeshes=true;
    // Threads that decode shape payloads ahead of the (single-threaded) scene build. 0 = automatic (cores - 1), 1 = off.
    // The decoder keeps its limits per thread and opens its own file stream for every segment, so payloads decode independently.
    // MODELVIEWER_JT_DECODE_THREADS overrides it in fromEnvironment().
    unsigned decodeThreads=0;
    void check() const { if(cancelled && cancelled()) throw Cancelled(); }
    // Opt-in desktop runtime override. Plain Options and explicit caller budgets
    // remain deterministic; the viewer calls this when starting a JT import.
    static Options fromEnvironment() {
        Options result;
        // Settings dialog (Debug tab) values first; an environment variable, when set, overrides them.
        QSettings settings;
        if(!settings.value(QStringLiteral("jtMergePartMeshes"),true).toBool())result.mergePartMeshes=false;
        {const int threads=settings.value(QStringLiteral("jtDecodeThreads"),0).toInt();if(threads>=0)result.decodeThreads=unsigned(threads);}
        {const int gib=settings.value(QStringLiteral("jtBudgetGiB"),16).toInt();
         if(gib>=1&&gib<=256)result.decodedAllocationBudget=size_t(gib)*size_t(1024)*1024*1024;}
        if(qgetenv("MODELVIEWER_JT_MERGE_PART_MESHES")=="0")result.mergePartMeshes=false;
        {bool ok=false;const int threads=qEnvironmentVariableIntValue("MODELVIEWER_JT_DECODE_THREADS",&ok);if(ok&&threads>=0)result.decodeThreads=unsigned(threads);}
        const auto value=qgetenv("MODELVIEWER_JT_BUDGET_GIB");
        if(value.isNull())return result;
        bool digits=!value.isEmpty();
        for(const auto c:value)digits=digits && c>='0' && c<='9';
        bool ok=false;const auto gib=value.toULongLong(&ok);
        constexpr size_t unit=size_t(1024)*1024*1024;
        if(!digits || !ok || gib<1 || gib>256 || gib>std::numeric_limits<size_t>::max()/unit)
            throw std::runtime_error("Invalid MODELVIEWER_JT_BUDGET_GIB: use a whole number from 1 to 256 (GiB), or remove the variable for the 16 GiB default");
        result.decodedAllocationBudget=size_t(gib)*unit;
        return result;
    }
};
struct ImportResult {
    std::unique_ptr<aiScene> scene;
    QStringList warnings;
    // Informational report of the part-mesh merge (counts, per-part provenance); shown with the warnings, not counted as one.
    QStringList notes;
    QSet<const aiNode*> hiddenNodes;
    std::vector<TopoDS_Shape> exactShapes;
    unsigned storedMeshes=0, exactParts=0, fallbackParts=0;
};
class Importer {
public:
    static ImportResult read(const QString& path,const Options& options={});
};
}
