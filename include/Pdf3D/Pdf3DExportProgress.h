#pragma once

#include "Pdf3DExporter.h"

class QWidget;

namespace Pdf3D
{

// Run the prepared export off the GUI thread and keep the modal dialog open until it finishes.
Pdf3DResult exportWithProgress(QWidget* parent, const U3dScene& scene, const Pdf3DOptions& options, const QString& path);

} // namespace Pdf3D
