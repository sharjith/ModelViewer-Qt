#pragma once

#include <QString>

#include "LanguageManager.h"
#include "RenderEnums.h"

// Icon of an axonometric compass corner. The letters drawn on the cube faces are the compass initials of the
// UI language, so each corner has three variants (res/isometric_<corner>[suffix].png):
//   ""    S E N W   English
//   "_de" S O N W   German (Ost)
//   "_o"  S E N O   Spanish (Oeste), French (Ouest), Italian (Ovest)
// Regenerate them with gen_iso_corner_icons.py. Callers refresh their icons when the language changes.
inline QString isoCornerIconPath(IsoCorner corner)
{
    static const char* const kCornerNames[] = { "se", "ne", "nw", "sw" };
    const QString language = LanguageManager::instance().currentLanguage().section(QLatin1Char('_'), 0, 0);
    QString suffix;
    if (language == QLatin1String("de"))
        suffix = QStringLiteral("_de");
    else if (language == QLatin1String("es") || language == QLatin1String("fr") || language == QLatin1String("it"))
        suffix = QStringLiteral("_o");
    return QStringLiteral(":/icons/res/isometric_%1%2.png")
        .arg(QLatin1String(kCornerNames[static_cast<int>(corner)]), suffix);
}
