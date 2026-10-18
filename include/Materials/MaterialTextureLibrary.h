#pragma once

#include "MaterialScanner.h"
#include <QMutex>

/**
 * MaterialTextureLibrary
 * ----------------------
 * Singleton that loads and caches all material texture presets once at startup.
 * Provides global access to the MaterialsMap.
 */
class MaterialTextureLibrary
{
public:
    static MaterialTextureLibrary& instance();

    // Non-copyable
    MaterialTextureLibrary(const MaterialTextureLibrary&) = delete;
    MaterialTextureLibrary& operator=(const MaterialTextureLibrary&) = delete;

    // Access the parsed materials
    const MaterialsMap& materials() const { return _materials; }
    const MaterialsMap& factoryMaterials() const { return _factoryMaterials; }
    const MaterialsMap& userMaterials() const { return _userMaterials; }

    // Reload from folder (optional)
    void reload(const QString& rootFolder = QString());

    QString factoryRoot() const;
    QString userRoot() const;
    QString presetFolder(const QString& presetName, bool userPreset = false) const;
    bool isUserPreset(const QString& presetName) const;

private:
    MaterialTextureLibrary();   // private ctor
    ~MaterialTextureLibrary() = default;

    void loadAllMaterials(const QString& factoryRoot, const QString& userRoot);

    MaterialsMap _materials;
    MaterialsMap _factoryMaterials;
    MaterialsMap _userMaterials;
    mutable QMutex _mutex;
};
