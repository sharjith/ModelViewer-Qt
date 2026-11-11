#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <optional>

struct ViewOverlayVisibility
{
    QSet<QUuid> measurements;
    QSet<QUuid> annotations;
};

inline QJsonObject viewOverlayVisibilityToJson(const ViewOverlayVisibility& visibility)
{
    const auto ids = [](const QSet<QUuid>& visible) {
        QStringList sorted;
        for (const QUuid& id : visible)
            sorted.append(id.toString(QUuid::WithoutBraces));
        sorted.sort();
        return QJsonArray::fromStringList(sorted);
    };
    return {{QStringLiteral("measurements"), ids(visibility.measurements)},
            {QStringLiteral("annotations"), ids(visibility.annotations)}};
}

inline std::optional<ViewOverlayVisibility> viewOverlayVisibilityFromJson(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    const auto measurements = object.value(QStringLiteral("measurements"));
    const auto annotations = object.value(QStringLiteral("annotations"));
    if (!measurements.isArray() || !annotations.isArray())
        return std::nullopt;

    const auto ids = [](const QJsonArray& entries) {
        QSet<QUuid> visible;
        for (const auto& entry : entries)
        {
            const QUuid id(entry.toString());
            if (!id.isNull())
                visible.insert(id);
        }
        return visible;
    };
    return ViewOverlayVisibility{ids(measurements.toArray()), ids(annotations.toArray())};
}
