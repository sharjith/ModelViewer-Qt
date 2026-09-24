#include "ResultReader.h"

#include "VtkLegacyReader.h"
#include "VtkXmlReader.h"

#include <QFileInfo>

bool isSupportedResultFile(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	return suffix == QLatin1String("vtu") || suffix == QLatin1String("vtk");
}

QStringList supportedResultFileFilters()
{
	return { QStringLiteral("VTK XML Unstructured Grid (*.vtu)"), QStringLiteral("VTK Legacy (*.vtk)") };
}

ResultReadOutcome readResultFile(const QString& path, const std::atomic<bool>* cancel)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QLatin1String("vtu"))
		return readVtkXmlUnstructuredGrid(path, cancel);
	if (suffix == QLatin1String("vtk"))
		return readVtkLegacy(path, cancel);

	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("Unsupported result file type '.%1'.").arg(suffix);
	return outcome;
}
