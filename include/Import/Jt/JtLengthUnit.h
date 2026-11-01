#pragma once

#include "LengthUnits.h"
#include <QSet>
#include <QString>
#include <QStringList>

namespace Jt {
// Length unit a JT file declares through its JT_PROP_MEASUREMENT_UNITS string property, given every distinct value found on
// its nodes. Only the spellings seen in real Solid Edge exports are mapped; any other value, or disagreeing nodes, leaves the
// unit Unknown and appends an explanation to notes. The unit is reported on the file node only - geometry is never scaled.
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
    notes.append(QStringLiteral("JT declares length unit \"%1\", which is not recognised; the unit stays unset (use Import Units).").arg(declared));
    return LengthUnit::Unknown;
}
}
