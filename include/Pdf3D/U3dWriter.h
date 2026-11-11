#pragma once

#include "U3dScene.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Writes a Universal 3D (ECMA-363) stream from a U3dScene, in-process through the u3d library (overlay port overlay-ports/u3d).
// The library keeps its core and plugins in four run-time modules that must sit next to the executable; modulesAvailable()
// checks for them (a missing module crashes the library instead of returning an error). Never call the library's
// IFXSetDefaultLocale(): it changes the process-wide locale.
namespace Pdf3D
{

struct U3dWriteResult
{
    bool       ok = false;
    QByteArray data;     // the .u3d file contents
    QString    error;    // set on failure
    bool       canceled = false;
};

class U3dWriter
{
public:
    // True when IFXCore, IFXExporting, IFXImporting and IFXScheduling exist in the application folder. Otherwise *missing
    // (if given) lists the absent file names.
    static bool modulesAvailable(QStringList* missing = nullptr);

    // Converts the scene. Calls serialize access to the library's shared state.
    static U3dWriteResult write(const U3dScene& scene);

    // Encodes directly to a caller-owned temporary file. An empty return value means success.
    static QString writeTemporaryFile(const U3dScene& scene, const QString& path);
};

} // namespace Pdf3D
