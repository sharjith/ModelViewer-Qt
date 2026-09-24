#include "ResultReader.h"

#include "CalculixFrdReader.h"
#include "VtkLegacyReader.h"
#include "VtkXmlReader.h"

#include <QFileInfo>

QStringList supportedResultExtensions()
{
	return { QStringLiteral("vtu"), QStringLiteral("vtk"), QStringLiteral("frd") };
}

bool isSupportedResultFile(const QString& path)
{
	return supportedResultExtensions().contains(QFileInfo(path).suffix().toLower());
}

QStringList supportedResultFileFilters()
{
	return { QStringLiteral("VTK XML Unstructured Grid (*.vtu)"), QStringLiteral("VTK Legacy (*.vtk)"),
	         QStringLiteral("CalculiX Results (*.frd)") };
}

ResultReadOutcome readResultFile(const QString& path, const std::atomic<bool>* cancel)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QLatin1String("vtu"))
		return readVtkXmlUnstructuredGrid(path, cancel);
	if (suffix == QLatin1String("vtk"))
		return readVtkLegacy(path, cancel);
	if (suffix == QLatin1String("frd"))
		return readCalculixFrd(path, cancel);

	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("Unsupported result file type '.%1'.").arg(suffix);
	return outcome;
}
