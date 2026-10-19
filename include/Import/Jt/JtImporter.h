#pragma once
#include <QByteArray>
#include <QStringList>
#include <QSet>
#include <TopoDS_Shape.hxx>
#include <assimp/scene.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
#include <array>
#include <optional>
#include <cstddef>

namespace Jt {
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("Model loading cancelled by user.") {} };
struct Options {
    // Cumulative charged allocations, including released scratch arrays; not peak RSS.
    // The decoder also retains a separate 256 MiB per-allocation limit.
    size_t decodedAllocationBudget=size_t(4)*1024*1024*1024;
    std::function<bool()> cancelled;
    std::function<void(const QString&,int,int)> progress;
    // Optional independently licensed XT backend. Empty in the native mesh-only build.
    std::function<TopoDS_Shape(const QByteArray&)> exactDecoder;
    // Optional import-time eye position in JT world coordinates / source units.
    // Unset selects the first RangeLOD child; this is not runtime LOD switching.
    std::optional<std::array<double,3>> lodEyePoint;
    void check() const { if(cancelled && cancelled()) throw Cancelled(); }
};
struct ImportResult {
    std::unique_ptr<aiScene> scene;
    QStringList warnings;
    QSet<const aiNode*> hiddenNodes;
    std::vector<TopoDS_Shape> exactShapes;
    unsigned storedMeshes=0, exactParts=0, fallbackParts=0;
};
class Importer {
public:
    static ImportResult read(const QString& path,const Options& options={});
};
}
