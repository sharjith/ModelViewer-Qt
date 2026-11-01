#pragma once

#include "LengthUnits.h"
#include <QSet>
#include <QString>
#include <QStringList>

namespace Jt {
// Length unit a JT file declares through its JT_PROP_MEASUREMENT_UNITS string property, given every distinct value found on
// its nodes. The JT 10.6 reference lists millimeters, centimeters, meters, inches, feet, yards, micrometers, decimeters,
// kilometers, mils and miles; the five that LengthUnit can represent are mapped (only millimeters and meters have been seen in
// real exports). Any other value, or disagreeing nodes, leaves the unit Unknown and appends an explanation to notes. The unit is
// reported on the file node only - geometry is never scaled.
inline LengthUnit declaredLengthUnit(const QSet<QString>& values, QStringList& notes)
{
    if (values.isEmpty())
        return LengthUnit::Unknown;
    if (values.size() > 1) {
        QStringList all = values.values();
        all.sort();
        notes.append(QStringLiteral("JT nodes declare different length units (%1); the unit stays unset (use Import Units).").arg(all.join(QStringLiteral(", "))));
        return LengthUnit::Unknown;
    }
    const QString declared = *values.cbegin();
    if (declared.compare(QLatin1String("millimeters"), Qt::CaseInsensitive) == 0)
        return LengthUnit::Millimeter;
    if (declared.compare(QLatin1String("meters"), Qt::CaseInsensitive) == 0)
        return LengthUnit::Meter;
    if (declared.compare(QLatin1String("centimeters"), Qt::CaseInsensitive) == 0)
        return LengthUnit::Centimeter;
    if (declared.compare(QLatin1String("inches"), Qt::CaseInsensitive) == 0)
        return LengthUnit::Inch;
    if (declared.compare(QLatin1String("feet"), Qt::CaseInsensitive) == 0)
        return LengthUnit::Foot;
    notes.append(QStringLiteral("JT declares length unit \"%1\", which is not recognised; the unit stays unset (use Import Units).").arg(declared));
    return LengthUnit::Unknown;
}
}
