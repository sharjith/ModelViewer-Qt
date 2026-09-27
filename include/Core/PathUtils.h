#pragma once

#include "config.h"
#include <QApplication>
#include <QDir>
#include <QString>

namespace PathUtils
{
    inline QString getDataDirectory()
    {
#if USE_RELATIVE_DATA_DIR
        // Windows: compute relative to exe at runtime
        return QDir(QApplication::applicationDirPath()).absoluteFilePath("../share/") + QString(MODELVIEWER_DATA_DIR);
#else
        // Linux: use absolute path from cmake
        return QString(MODELVIEWER_DATA_DIR);
#endif
    }
}
