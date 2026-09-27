#pragma once
#include <QObject>
#include <QList>
#include <QMap>
#include <QVariantMap>
#include <QMutex>

class Material;

class MaterialRegistry : public QObject
{
    Q_OBJECT
public:
    struct Item
    {
        QString key;
        QString name;
        QVariantMap props; // raw properties read from JSON (variant map)
    };
    struct Group
    {
        QString id;
        QString label;
        QList<Item> items;
    };

    static MaterialRegistry& instance();

    // Load JSON file. Returns true on success; error message returned in err if provided.
    bool loadFromJsonFile(const QString& path, QString* err = nullptr);

    // Returns groups in the order loaded
    QList<Group> groups() const;

    // Returns a material instance for key (cached). If key not found returns an optional default material (constructed with defaults).
    Material materialForKey(const QString& key);

    // Returns whether registry has a key
    bool hasKey(const QString& key) const;

signals:
    // emitted after successful load (useful for UI to rebuild)
    void registryLoaded();
    void materialsChanged();

private:
    MaterialRegistry(QObject* parent = nullptr);
    ~MaterialRegistry() override = default;
    MaterialRegistry(const MaterialRegistry&) = delete;
    MaterialRegistry& operator=(const MaterialRegistry&) = delete;

    QList<Group> _groups;
    QMap<QString, QVariantMap> _rawByKey;
    // Item::name lives on the Group entries but "key" isn't unique across
    // groups' insertion order the way a flat lookup needs - mirrors
    // _rawByKey's key->value shape so materialForKey() can stamp the
    // catalog's own display name onto the Material it builds, the same way
    // every other Material::fromVariantMap() caller in this codebase already
    // does explicitly (props deliberately excludes "name"/"key" - see the
    // populating loop's own comment on why).
    QMap<QString, QString> _nameByKey;
    mutable QMap<QString, QSharedPointer<Material>> _cache;
    mutable QMutex _cacheMutex;
};
