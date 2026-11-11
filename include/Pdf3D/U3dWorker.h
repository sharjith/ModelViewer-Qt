#pragma once

#include "U3dWriter.h"

#include <functional>

namespace Pdf3D
{

inline constexpr char kU3dWorkerArgument[] = "--internal-u3d-worker";

// The worker receives only its private workspace. Its output is never the user's PDF.
int runU3dWorker(const QString& workspace);

U3dWriteResult encodeU3dInWorker(const QString& executable, const U3dScene& scene, const std::function<bool()>& isCanceled);

} // namespace Pdf3D
