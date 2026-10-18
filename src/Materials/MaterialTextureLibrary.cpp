#include "MaterialTextureLibrary.h"
#include "PathUtils.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

namespace {
QString userTextureMaterialsRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#ifdef Q_OS_MAC
    if (base.isEmpty())
        base = QDir::homePath() + "/Library/Application Support/ModelViewer";
#endif
    if (base.isEmpty())
        base = QDir::homePath() + "/.modelviewer";

    QDir d(base);
    if (!d.exists())
        d.mkpath(".");
    return d.filePath("texture-materials");
}
}

MaterialTextureLibrary& MaterialTextureLibrary::instance()
{
    static MaterialTextureLibrary inst;
    return inst;
}

MaterialTextureLibrary::MaterialTextureLibrary()
{
    loadAllMaterials(factoryRoot(), userRoot());
}

void MaterialTextureLibrary::reload(const QString& rootFolder)
{
    QMutexLocker locker(&_mutex);
    const QString factory = rootFolder.isEmpty() ? factoryRoot() : rootFolder;
    loadAllMaterials(factory, userRoot());
    qDebug() << "MaterialTextureLibrary reloaded from" << factory
        << "with" << _materials.size() << "materials";
}

QString MaterialTextureLibrary::factoryRoot() const
{
    return PathUtils::getDataDirectory() + "/textures/materials";
}

QString MaterialTextureLibrary::userRoot() const
{
    return userTextureMaterialsRoot();
}

QString MaterialTextureLibrary::presetFolder(const QString& presetName, bool userPreset) const
{
    const QString root = userPreset ? userRoot() : factoryRoot();
    return QDir(root).filePath(presetName);
}

bool MaterialTextureLibrary::isUserPreset(const QString& presetName) const
{
    return _userMaterials.contains(presetName);
}

void MaterialTextureLibrary::loadAllMaterials(const QString& factoryRoot, const QString& userRoot)
{
    _materials.clear();
    _factoryMaterials.clear();
    _userMaterials.clear();

    if (!QDir(factoryRoot).exists())
    {
        qWarning() << "MaterialTextureLibrary: material root folder not found:" << factoryRoot;
    }
    else
    {
        _factoryMaterials = MaterialScanner::parseMaterialsFolder(factoryRoot);
    }

    if (QDir(userRoot).exists())
    {
        _userMaterials = MaterialScanner::parseMaterialsFolder(userRoot);
    }

    _materials = _factoryMaterials;
    for (auto it = _userMaterials.constBegin(); it != _userMaterials.constEnd(); ++it)
    {
        _materials[it.key()] = it.value();
    }
}
