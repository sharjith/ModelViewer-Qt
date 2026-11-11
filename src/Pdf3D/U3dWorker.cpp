#include "U3dWorker.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <exception>
#include <limits>
#include <utility>

namespace Pdf3D
{
namespace
{

constexpr quint32 kSceneFormat = 0x4d565531;   // MVU1, private to matching parent and worker builds
constexpr int kChunkBytes = 1024 * 1024;

bool canceled(const std::function<bool()>& isCanceled)
{
    return isCanceled && isCanceled();
}

template<typename T>
bool writeMeshArray(QDataStream& stream, const QVector<T>& values, const std::function<bool()>& isCanceled)
{
    stream << quint64(values.size());
    // Raw scalars are shared only between instances of the same executable.
    const char* data = reinterpret_cast<const char*>(values.constData());
    qint64 remaining = qint64(values.size()) * sizeof(T);
    while (remaining > 0)
    {
        const int chunk = int(qMin<qint64>(remaining, kChunkBytes));
        if (canceled(isCanceled) || stream.writeRawData(data, chunk) != chunk)
            return false;
        remaining -= chunk;
        data += chunk;
    }
    return stream.status() == QDataStream::Ok;
}

template<typename T>
bool readMeshArray(QDataStream& stream, QVector<T>& values)
{
    quint64 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > std::numeric_limits<int>::max() ||
        count > quint64(stream.device()->bytesAvailable()) / sizeof(T))
        return false;

    values.resize(qsizetype(count));
    char* data = reinterpret_cast<char*>(values.data());
    qint64 remaining = qint64(count) * sizeof(T);
    while (remaining > 0)
    {
        const int chunk = int(qMin<qint64>(remaining, kChunkBytes));
        if (stream.readRawData(data, chunk) != chunk)
            return false;
        remaining -= chunk;
        data += chunk;
    }
    return true;
}

template<typename T, typename WriteItem>
bool writeItems(QDataStream& stream, const QVector<T>& items, const std::function<bool()>& isCanceled, WriteItem writeItem)
{
    if (items.size() > std::numeric_limits<int>::max())
        return false;
    stream << quint32(items.size());
    for (const T& item : items)
    {
        if (canceled(isCanceled) || !writeItem(item) || stream.status() != QDataStream::Ok)
            return false;
    }
    return stream.status() == QDataStream::Ok;
}

template<typename T, typename ReadItem>
bool readItems(QDataStream& stream, QVector<T>& items, ReadItem readItem)
{
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > std::numeric_limits<int>::max() ||
        count > quint64(stream.device()->bytesAvailable()))
        return false;

    items.reserve(qMin<quint32>(count, 100000));
    for (quint32 i = 0; i < count; ++i)
    {
        T item;
        if (!readItem(item) || stream.status() != QDataStream::Ok)
            return false;
        items.append(std::move(item));
    }
    return true;
}

bool saveScene(const QString& path, const U3dScene& scene, const std::function<bool()>& isCanceled)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kSceneFormat << scene.groups << scene.groupParents << scene.overlayItems
           << scene.animation.name << scene.animation.playing;

    return writeItems(stream, scene.materials, isCanceled, [&](const U3dMaterial& material) {
        stream << material.name << material.diffuse << material.emissive << material.opacity << material.vertexColors;
        return true;
    }) && writeItems(stream, scene.meshes, isCanceled, [&](const U3dMesh& mesh) {
        stream << mesh.name << mesh.materialIndex;
        return writeMeshArray(stream, mesh.positions, isCanceled) && writeMeshArray(stream, mesh.normals, isCanceled) &&
            writeMeshArray(stream, mesh.colors, isCanceled) && writeMeshArray(stream, mesh.indices, isCanceled);
    }) && writeItems(stream, scene.nodes, isCanceled, [&](const U3dNode& node) {
        stream << node.name << node.displayName << node.meshIndex << node.overlay << node.parent << node.overlayItem << node.follows
               << node.declaredMassKg << node.declaredMassGroup << node.declaredMaterial << node.world;
        return true;
    }) && writeItems(stream, scene.animation.tracks, isCanceled, [&](const U3dMotionTrack& track) {
        stream << track.node << track.times << track.keys;
        return true;
    }) && file.flush();
}

bool loadScene(const QString& path, U3dScene& scene)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 format = 0;
    stream >> format;
    if (format != kSceneFormat)
        return false;
    stream >> scene.groups >> scene.groupParents >> scene.overlayItems >> scene.animation.name >> scene.animation.playing;

    return readItems(stream, scene.materials, [&](U3dMaterial& material) {
        stream >> material.name >> material.diffuse >> material.emissive >> material.opacity >> material.vertexColors;
        return true;
    }) && readItems(stream, scene.meshes, [&](U3dMesh& mesh) {
        stream >> mesh.name >> mesh.materialIndex;
        return readMeshArray(stream, mesh.positions) && readMeshArray(stream, mesh.normals) &&
            readMeshArray(stream, mesh.colors) && readMeshArray(stream, mesh.indices);
    }) && readItems(stream, scene.nodes, [&](U3dNode& node) {
        stream >> node.name >> node.displayName >> node.meshIndex >> node.overlay >> node.parent >> node.overlayItem >> node.follows
               >> node.declaredMassKg >> node.declaredMassGroup >> node.declaredMaterial >> node.world;
        return true;
    }) && readItems(stream, scene.animation.tracks, [&](U3dMotionTrack& track) {
        stream >> track.node >> track.times >> track.keys;
        return true;
    }) && file.atEnd();
}

U3dWriteResult cancelResult()
{
    U3dWriteResult result;
    result.canceled = true;
    return result;
}

U3dWriteResult errorResult(const QString& error)
{
    U3dWriteResult result;
    result.error = error;
    return result;
}

} // namespace

int runU3dWorker(const QString& workspace)
{
    const QDir dir(workspace);
    const QString outputPath = dir.filePath(QStringLiteral("model.u3d"));
    const QFileInfo output(outputPath);
    if (workspace.isEmpty() || !dir.isAbsolute() || output.exists() || output.isSymLink())
        return 1;

    QString error;
    try
    {
        U3dScene scene;
        if (!loadScene(dir.filePath(QStringLiteral("scene.dat")), scene))
            error = QStringLiteral("The encoder could not read the export scene.");
        else
            error = U3dWriter::writeTemporaryFile(scene, outputPath);
    }
    catch (const std::exception& exception)
    {
        error = QString::fromLocal8Bit(exception.what());
    }
    if (error.isEmpty())
        return 0;

    QFile errorFile(dir.filePath(QStringLiteral("error.txt")));
    if (errorFile.open(QIODevice::WriteOnly))
        errorFile.write(error.toUtf8());
    return 1;
}

U3dWriteResult encodeU3dInWorker(const QString& executable, const U3dScene& scene, const std::function<bool()>& isCanceled)
{
    if (canceled(isCanceled))
        return cancelResult();

    const QTemporaryDir workspace(QDir::temp().filePath(QStringLiteral("mv-u3d-XXXXXX")));
    if (!workspace.isValid())
        return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The temporary encoder folder could not be created."));
    const QDir dir(workspace.path());
    if (!saveScene(dir.filePath(QStringLiteral("scene.dat")), scene, isCanceled))
    {
        if (canceled(isCanceled))
            return cancelResult();
        return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The export scene could not be written to the encoder folder."));
    }
    if (canceled(isCanceled))
        return cancelResult();

    QProcess encoder;
    encoder.setStandardOutputFile(QProcess::nullDevice());
    encoder.setStandardErrorFile(QProcess::nullDevice());
    encoder.start(executable, {QString::fromLatin1(kU3dWorkerArgument), workspace.path()});
    while (encoder.state() != QProcess::NotRunning)
    {
        if (canceled(isCanceled))
        {
            encoder.kill();
            encoder.waitForFinished(-1);
            return cancelResult();
        }
        encoder.waitForFinished(50);
    }

    if (canceled(isCanceled))
        return cancelResult();
    if (encoder.error() == QProcess::FailedToStart)
        return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The 3D encoder could not start: %1").arg(encoder.errorString()));
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0)
    {
        QFile errorFile(dir.filePath(QStringLiteral("error.txt")));
        if (errorFile.open(QIODevice::ReadOnly))
            return errorResult(QString::fromUtf8(errorFile.read(kChunkBytes)));
        return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The 3D encoder stopped before completing the model."));
    }

    QFile model(dir.filePath(QStringLiteral("model.u3d")));
    if (!model.open(QIODevice::ReadOnly) || model.size() <= 0)
        return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The 3D encoder did not produce a model."));
    const qint64 size = model.size();
    if (size > std::numeric_limits<qsizetype>::max())
        return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The encoded model is too large to embed."));

    U3dWriteResult result;
    result.data.resize(qsizetype(size));
    char* const data = result.data.data();
    for (qint64 offset = 0; offset < size;)
    {
        if (canceled(isCanceled))
            return cancelResult();
        const qint64 count = qMin<qint64>(kChunkBytes, size - offset);
        if (model.read(data + offset, count) != count)
            return errorResult(QCoreApplication::translate("Pdf3DExportProgress", "The encoded model could not be read."));
        offset += count;
    }
    result.ok = true;
    return result;
}

} // namespace Pdf3D
