#pragma once
#include <QTreeWidget>
#include "Material.h"
#include <QList>
#include <QMap>
#include <QVector>
#include <QPair>

class QKeyEvent;

class MaterialLibraryWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit MaterialLibraryWidget(QWidget *parent = nullptr);

    void deleteSelectedMaterial();

    void selectMaterialByKey(const QString& key);

    // call this once at app startup to populate the shared material map + groups
    static bool loadAllMaterials(const QString& jsonPath, QString* err = nullptr);

    // reload from registry (e.g. after user edits or imports) — convenience wrapper
    static bool reloadAllMaterials(QString* err = nullptr) { return loadAllMaterials(s_jsonPath, err); }

    // Merge user-defined materials from per-user JSON into the shared cache.
    // Returns true on success (or when no user file exists). On error returns false and sets err if provided.
    static bool mergeUserMaterialsFromUserLocation(QString* err = nullptr);

    // Save a single material to user location with optional overwrite confirmation via parent widget.
    // If parent is non-null, shows a confirmation dialog when an item with same key exists.
    static bool saveUserMaterialToUserLocation(const QString& groupLabel,
        const QString& key,
        const QString& name,
        const Material& mat,
        QWidget* parent = nullptr,
        QString* err = nullptr);

    // Remove a user material (by key & group) from user's materials.json with confirmation.
    // If parent is non-null, shows a confirmation dialog.
    static bool removeUserMaterialFromUserLocation(const QString& groupLabel,
        const QString& key,
        QWidget* parent = nullptr,
        QString* err = nullptr);

    // Save all current shared materials (s_groups & s_materialMap) into the per-user file
    // or explicit filePath. If parent is provided and file exists and is different from default user path,
    // asks confirmation to overwrite.
    static bool saveAllUserMaterials(const QString& filePath = QString(),
        QWidget* parent = nullptr,
        QString* err = nullptr);


    // Accessors for read-only shared data (optional)
    static const QMap<QString, std::function<Material()>>& sharedMaterialMap() { return s_materialMap; }
    static const QVector<QPair<QString, QVector<QPair<QString, QString>>>>& sharedGroups() { return s_groups; }

    // Get the user materials root directory path
    static QString userMaterialsRootPath();
	static QString userMaterialsJsonPath() { return s_jsonPath; }

	// Refresh the material tree display (used after saving new materials)
	void refreshMaterialTree() { populateMaterials(); }

signals:
    void materialPreview(const Material& mat);
    void materialSelected(const Material &mat);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void handleItemEntered(QTreeWidgetItem* item, int column);
    void populateMaterials();

private:
    static QVector<QPair<QString, QVector<QPair<QString, QString>>>> populateMaterialMapWithBuiltIns(
        QMap<QString, std::function<Material()>>& materialMap);

public:
    // keys that came from user's file (for quick checks)
    static QSet<QString> s_userMaterialKeys;
private:
    // static storage
    static QMap<QString, std::function<Material()>> s_materialMap;
    // Groups: vector of (groupLabel, vector of (displayName, key))
    static QVector<QPair<QString, QVector<QPair<QString, QString>>>> s_groups;

    // remember jsonPath used for reloads (shipped catalogs)
    static QString s_jsonPath;
	// User materials JSON path (AppData folder) - separate from shipped catalogs
	static QString s_userJsonPath;

};
