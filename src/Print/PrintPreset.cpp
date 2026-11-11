#include "PrintPreset.h"

#include <QMarginsF>
#include <QRectF>
#include <QSizeF>

#include <cmath>

QVariantMap PrintPreset::toSettings() const
{
    const QMarginsF margins = paper.margins(QPageLayout::Millimeter);
    return {
        {QStringLiteral("paperId"), static_cast<int>(paper.pageSize().id())},
        {QStringLiteral("sizeMm"), paper.pageSize().size(QPageSize::Millimeter)},
        {QStringLiteral("landscape"), paper.orientation() == QPageLayout::Landscape},
        {QStringLiteral("marginsMm"), QVariantList{margins.left(), margins.top(), margins.right(), margins.bottom()}},
        {QStringLiteral("templateId"), templateId},
        {QStringLiteral("useTemplate"), useTemplate},
        {QStringLiteral("imageDpi"), imageDpi},
        {QStringLiteral("scaleRatio"), scaleRatio},
    };
}

std::optional<PrintPreset> PrintPreset::fromSettings(const QVariantMap& values)
{
    bool validId = false;
    const int paperId = values.value(QStringLiteral("paperId")).toInt(&validId);
    if (!validId || paperId < 0 || paperId > QPageSize::LastPageSize)
        return std::nullopt;

    const auto id = static_cast<QPageSize::PageSizeId>(paperId);
    QPageSize size;
    if (id == QPageSize::Custom)
    {
        const QSizeF mm = values.value(QStringLiteral("sizeMm")).toSizeF();
        if (!std::isfinite(mm.width()) || !std::isfinite(mm.height()) || mm.isEmpty() ||
            mm.width() > 10000 || mm.height() > 10000)
            return std::nullopt;
        size = QPageSize(mm, QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
    }
    else
    {
        size = QPageSize(id);
    }
    if (!size.isValid())
        return std::nullopt;

    const QVariantList storedMargins = values.value(QStringLiteral("marginsMm")).toList();
    if (storedMargins.size() != 4)
        return std::nullopt;
    qreal margins[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        bool number = false;
        margins[i] = storedMargins[i].toDouble(&number);
        if (!number || !std::isfinite(margins[i]) || margins[i] < 0)
            return std::nullopt;
    }

    PrintPreset preset;
    const auto orientation = values.value(QStringLiteral("landscape")).toBool()
        ? QPageLayout::Landscape : QPageLayout::Portrait;
    preset.paper = QPageLayout(size, orientation, {}, QPageLayout::Millimeter);
    if (!preset.paper.setMargins(QMarginsF(margins[0], margins[1], margins[2], margins[3])) ||
        preset.paper.paintRect(QPageLayout::Point).isEmpty())
        return std::nullopt;

    preset.templateId = values.value(QStringLiteral("templateId")).toString();
    preset.useTemplate = values.value(QStringLiteral("useTemplate"), true).toBool();
    const int dpi = values.value(QStringLiteral("imageDpi")).toInt();
    if (dpi == 150 || dpi == 300 || dpi == 600)
        preset.imageDpi = dpi;
    const double ratio = values.value(QStringLiteral("scaleRatio")).toDouble();
    if (std::isfinite(ratio) && ratio > 0.0)
        preset.scaleRatio = ratio;
    return preset;
}
