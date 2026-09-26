#include "ResultReader.h"

#include "CalculixFrdReader.h"
#include "CgnsReader.h"
#include "ExodusReader.h"
#include "OpenFoamReader.h"
#include "VtkHdfReader.h"
#include "VtkLegacyReader.h"
#include "VtkXmlReader.h"

#include <QFileInfo>

QStringList supportedResultExtensions()
{
	QStringList extensions = { QStringLiteral("vtu"), QStringLiteral("vtk"), QStringLiteral("frd"), QStringLiteral("foam") };
	extensions << exodusExtensions(); // empty in a build without NetCDF
	extensions << cgnsExtensions();   // empty in a build without the CGNS library
	extensions << vtkHdfExtensions(); // empty in a build without the HDF5 library
	return extensions;
}

bool isSupportedResultFile(const QString& path)
{
	return supportedResultExtensions().contains(QFileInfo(path).suffix().toLower());
}

QStringList supportedResultFileFilters()
{
	QStringList filters = { QStringLiteral("VTK XML Unstructured Grid (*.vtu)"), QStringLiteral("VTK Legacy (*.vtk)"),
	                        QStringLiteral("CalculiX Results (*.frd)"), QStringLiteral("OpenFOAM Case (*.foam)") };
	if (exodusSupported())
		filters << exodusFileFilter();
	if (cgnsSupported())
		filters << cgnsFileFilter();
	if (vtkHdfSupported())
		filters << vtkHdfFileFilter();
	return filters;
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
	if (suffix == QLatin1String("foam"))
		return readOpenFoamCase(path, cancel);
	if (exodusExtensions().contains(suffix))
		return readExodus(path, cancel);
	if (cgnsExtensions().contains(suffix))
		return readCgns(path, cancel);
	if (vtkHdfExtensions().contains(suffix))
		return readVtkHdf(path, cancel);

	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("Unsupported result file type '.%1'.").arg(suffix);
	return outcome;
}
