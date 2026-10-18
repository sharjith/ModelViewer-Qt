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

namespace Jt {
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("Model loading cancelled by user.") {} };
struct Options {
    std::function<bool()> cancelled;
    std::function<void(const QString&,int,int)> progress;
    // Optional independently licensed XT backend. Empty in the native mesh-only build.
    std::function<TopoDS_Shape(const QByteArray&)> exactDecoder;
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
