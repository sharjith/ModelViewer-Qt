#include "MaterialRegistry.h"
#include "Material.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

MaterialRegistry& MaterialRegistry::instance()
{
    static MaterialRegistry reg;
    return reg;
}

MaterialRegistry::MaterialRegistry(QObject* parent)
    : QObject(parent)
{
}

static QVariantMap jsonObjectToVariantMap(const QJsonObject& obj)
{
    QVariantMap map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        map.insert(it.key(), it.value().toVariant());
    }
    return map;
}

bool MaterialRegistry::loadFromJsonFile(const QString& path, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (err) *err = QString("Failed to open %1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray data = f.readAll();
    QJsonParseError jerr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &jerr);
    if (jerr.error != QJsonParseError::NoError)
    {
        if (err) *err = QString("JSON parse error: %1").arg(jerr.errorString());
        return false;
    }
    if (!doc.isObject())
    {
        if (err) *err = QString("JSON root is not an object");
        return false;
    }

    QJsonObject root = doc.object();

    // Expect either "groups" (array of groups) or a flat "materials" map
    _groups.clear();
    _rawByKey.clear();
    _nameByKey.clear();

    if (root.contains(QStringLiteral("groups")) && root.value(QStringLiteral("groups")).isArray())
    {
        QJsonArray groupArray = root.value(QStringLiteral("groups")).toArray();
        for (const QJsonValue& gval : groupArray)
        {
            if (!gval.isObject()) continue;
            QJsonObject gobj = gval.toObject();
            Group grp;
            grp.id = gobj.value(QStringLiteral("id")).toString(gobj.value(QStringLiteral("label")).toString()).toLower().replace(' ', '_');
            grp.label = gobj.value(QStringLiteral("label")).toString(grp.id);
            if (gobj.contains(QStringLiteral("items")) && gobj.value(QStringLiteral("items")).isArray())
            {
                QJsonArray items = gobj.value(QStringLiteral("items")).toArray();
                for (const QJsonValue& itVal : items)
                {
                    if (!itVal.isObject()) continue;
                    QJsonObject itObj = itVal.toObject();
                    Item item;
                    item.key = itObj.value(QStringLiteral("key")).toString();
                    item.name = itObj.value(QStringLiteral("name")).toString(item.key);
                    // Save whole object as variant map (so Material::fromVariantMap can consume)
                    item.props = jsonObjectToVariantMap(itObj);
                    // remove name/key from props to avoid duplication (optional)
                    item.props.remove(QStringLiteral("key"));
                    item.props.remove(QStringLiteral("name"));
                    grp.items.append(item);

                    if (!item.key.isEmpty())
                    {
                        _rawByKey.insert(item.key, item.props);
                        _nameByKey.insert(item.key, item.name);
                    }
                }
            }
            _groups.append(grp);
        }
    }
    else if (root.contains(QStringLiteral("materials")) && root.value(QStringLiteral("materials")).isObject())
    {
        // legacy flat map: materials: { KEY: { ... } }
        QJsonObject mats = root.value(QStringLiteral("materials")).toObject();
        Group all;
        all.id = "all";
        all.label = "All Materials";
        for (auto it = mats.constBegin(); it != mats.constEnd(); ++it)
        {
            Item item;
            item.key = it.key();
            item.name = it.key();
            if (it.value().isObject())
            {
                item.props = jsonObjectToVariantMap(it.value().toObject());
            }
            all.items.append(item);
            _rawByKey.insert(item.key, item.props);
            _nameByKey.insert(item.key, item.name);
        }
        _groups.append(all);
    }
    else
    {
        // unknown format
        if (err) *err = QString("JSON does not contain 'groups' or 'materials'");
        return false;
    }

    // Clear cache
    {
        QMutexLocker locker(&_cacheMutex);
        _cache.clear();
    }

    emit registryLoaded();
    return true;
}

QList<MaterialRegistry::Group> MaterialRegistry::groups() const
{
    return _groups;
}

bool MaterialRegistry::hasKey(const QString& key) const
{
    return _rawByKey.contains(key);
}

Material MaterialRegistry::materialForKey(const QString& key)
{
    // check cache
    {
        QMutexLocker locker(&_cacheMutex);
        if (_cache.contains(key))
        {
            QSharedPointer<Material> ptr = _cache.value(key);
            if (!ptr.isNull()) return *ptr;
        }
    }

    // build from raw props
    if (!_rawByKey.contains(key))
    {
        // Not found: return default material (call default ctor)
        return Material();
    }
    QVariantMap props = _rawByKey.value(key);

    Material mat = Material::fromVariantMap(props);
    // fromVariantMap() deliberately never reads "name" (props had it
    // stripped above, see the populating loop's own comment) - the catalog's
    // display name has to be stamped on explicitly here, or every material
    // this registry hands out (and anything grouping by Material::name(),
    // e.g. Mass Properties' per-material rollup) sees an empty name.
    mat.setName(_nameByKey.value(key));

    // cache
    {
        QMutexLocker locker(&_cacheMutex);
        _cache.insert(key, QSharedPointer<Material>(new Material(mat)));
    }

    return mat;
}
