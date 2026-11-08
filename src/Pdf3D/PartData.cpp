#include "PartData.h"

#include <QCoreApplication>
#include <QSet>

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

PartSummary summarisePartData(const U3dScene& scene)
{
    PartSummary s;
    QSet<QString> countedMasses;   // part groups whose mass is already in the total
    for (const U3dNode& node : scene.nodes)
    {
        if (node.overlay || node.meshIndex < 0 || node.meshIndex >= scene.meshes.size())
            continue;
        ++s.parts;
        if (!node.declaredMaterial.isEmpty())
        {
            ++s.withMaterial;
            if (!s.materials.contains(node.declaredMaterial))
                s.materials.append(node.declaredMaterial);
        }
        if (node.declaredMassKg >= 0.0 && std::isfinite(node.declaredMassKg))
        {
            ++s.withMass;
            // Meshes of one part share a group and carry the part's one mass; a node without a group counts on its own.
            const QString key = node.declaredMassGroup.isEmpty() ? QStringLiteral("node:") + node.name : node.declaredMassGroup;
            if (!countedMasses.contains(key))
            {
                countedMasses.insert(key);
                s.massKg += node.declaredMassKg;
            }
        }
    }

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

} // namespace Pdf3D
