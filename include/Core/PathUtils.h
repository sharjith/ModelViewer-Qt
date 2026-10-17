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
        // Linux: the absolute path baked in from cmake is CMAKE_INSTALL_PREFIX at BUILD time,
        // which for a packaged build (.deb) is the build machine's install folder, not where the
        // package puts the files (/usr/share/ModelViewer). Use it only if it actually exists,
        // otherwise resolve relative to the executable (<prefix>/bin -> <prefix>/share/ModelViewer),
        // which is correct for both the package and a plain install tree.
        static const QString dir = []() {
            const QString baked = QString(MODELVIEWER_DATA_DIR);
            if (QDir(baked).exists())
                return baked;
            const QString relative = QDir(QApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("../share/ModelViewer"));
            return QDir(relative).exists() ? QDir::cleanPath(relative) : baked;
        }();
        return dir;
#endif
    }
}
