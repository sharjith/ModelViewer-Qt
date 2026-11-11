#pragma once

#include <QPageLayout>
#include <QString>
#include <QVariantMap>

#include <optional>

struct PrintPreset
{
    QPageLayout paper;
    QString templateId;
    bool useTemplate = true;
    int imageDpi = 300;
    double scaleRatio = 0.0;

    QVariantMap toSettings() const;
    static std::optional<PrintPreset> fromSettings(const QVariantMap& values);
};
