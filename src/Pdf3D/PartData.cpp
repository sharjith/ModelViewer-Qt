#include "PartData.h"

#include <QCoreApplication>

#include <cmath>

namespace Pdf3D
{

QString renderMaterialLabel(const QString& name, const QStringList& fileBaseNames)
{
    QString base = name;
    const QString variantSuffix = QStringLiteral(" (vertex colours)");
    if (base.endsWith(variantSuffix))
        base.chop(variantSuffix.size());
    for (const QString& fileBase : fileBaseNames)
    {
        if (!fileBase.isEmpty() && base.startsWith(fileBase + QStringLiteral(" (")) && base.endsWith(QLatin1Char(')')))
            return QString();   // "<file> (<part>)": the part's name again
    }
    if (base.startsWith(QLatin1String("Material")) && base.size() > 8)
    {
        bool digits = true;
        for (int i = 8; i < base.size(); ++i)
            digits = digits && base.at(i).isDigit();
        if (digits)
            return QString();   // an unnamed material
    }
    return base.trimmed();
}

QString formatMass(double kilograms)
{
    if (!std::isfinite(kilograms) || kilograms < 0.0)
        return QString();
    if (kilograms > 0.0 && kilograms < 0.001)
        return QCoreApplication::translate("Pdf3DExporter", "%1 g").arg(QString::number(kilograms * 1000.0, 'g', 4));
    const QString number = kilograms >= 10000.0 ? QString::number(kilograms, 'f', 0) : QString::number(kilograms, 'g', 4);
    return QCoreApplication::translate("Pdf3DExporter", "%1 kg").arg(number);
}

void PartSummaryBuilder::addPart(const QString& material, double massKg, const QString& massGroup)
{
    ++_counts.parts;
    if (!material.isEmpty())
    {
        ++_counts.withMaterial;
        if (!_counts.materials.contains(material))
            _counts.materials.append(material);
    }
    if (massKg >= 0.0 && std::isfinite(massKg))
    {
        ++_counts.withMass;
        if (massGroup.isEmpty() || !_countedMasses.contains(massGroup))
        {
            _counts.massKg += massKg;
            if (!massGroup.isEmpty())
                _countedMasses.insert(massGroup);
        }
    }
}

PartSummary PartSummaryBuilder::summary() const
{
    PartSummary s = _counts;
    const auto coverage = [&](int declared) {
        return declared < s.parts
            ? QCoreApplication::translate("Pdf3DExporter", " (%1 of %2 parts)").arg(declared).arg(s.parts)
            : QString();
    };
    if (!s.materials.isEmpty())
    {
        const QString base = s.materials.size() == 1 ? s.materials.first()
                                                      : QCoreApplication::translate("Pdf3DExporter", "%1 materials").arg(s.materials.size());
        s.material = base + coverage(s.withMaterial);
    }
    if (s.withMass > 0)
        s.mass = formatMass(s.massKg) + coverage(s.withMass);
    return s;
}

PartSummary summarisePartData(const U3dScene& scene)
{
    PartSummaryBuilder summary;
    for (const U3dNode& node : scene.nodes)
    {
        if (node.overlay || node.meshIndex < 0 || node.meshIndex >= scene.meshes.size())
            continue;
        const QString key = node.declaredMassKg >= 0.0 && node.declaredMassGroup.isEmpty()
            ? QStringLiteral("node:") + node.name : node.declaredMassGroup;
        summary.addPart(node.declaredMaterial, node.declaredMassKg, key);
    }
    return summary.summary();
}

} // namespace Pdf3D
