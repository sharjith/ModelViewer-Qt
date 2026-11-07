#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <vector>

namespace RecordedPmi
{
// A consistency check of one import's recorded dimensions against the mesh they were anchored on. It is not a verdict on any
// single dimension: the authored value comes from the CAD system and is the truth, the mesh value is limited by tessellation and
// by how exactly the end points snap. What it can catch is a systematic fault of our reading, such as a value stored in a
// different unit than the geometry (every ratio the same, far from 1) or end points anchored on the wrong geometry.
struct SanityReport
{
	int compared = 0;
	int off = 0;            // dimensions whose authored/mesh ratio is beyond OffTolerance
	double medianRatio = 1.0;
	QStringList notes;      // for the import log
	QString warning;        // non-empty only for a gross deviation worth showing to the user
};

constexpr double OffTolerance = 0.10;      // a ratio outside 1 +/- 10% is counted and logged
constexpr double GrossLow = 0.5;           // a median ratio outside [GrossLow, GrossHigh] is shown as a warning
constexpr double GrossHigh = 2.0;

// `ratios` holds authored / mesh for each comparable dimension, both in the same unit (angles and dimensions without a mesh value
// are left out by the caller).
inline SanityReport checkAgainstMesh(std::vector<double> ratios)
{
	SanityReport report;
	ratios.erase(std::remove_if(ratios.begin(), ratios.end(), [](double r) { return !(r > 0.0) || !std::isfinite(r); }), ratios.end());
	report.compared = int(ratios.size());
	if (ratios.empty())
		return report;
	for (double r : ratios)
		if (std::abs(r - 1.0) > OffTolerance)
			++report.off;
	std::sort(ratios.begin(), ratios.end());
	const size_t mid = ratios.size() / 2;
	report.medianRatio = ratios.size() % 2 ? ratios[mid] : 0.5 * (ratios[mid - 1] + ratios[mid]);
	if (report.off > 0)
		report.notes.append(QObject::tr("Recorded PMI: %1 of %2 dimensions differ from the mesh by more than %3%. The authored values are kept; "
			"the mesh is only an approximation of the CAD geometry.").arg(report.off).arg(report.compared).arg(int(OffTolerance * 100.0)));
	if (report.medianRatio < GrossLow || report.medianRatio > GrossHigh)
		report.warning = QObject::tr("The recorded dimensions of this file are, typically, %1 times the size measured on its mesh. "
			"The file's length unit may be wrong, or the dimensions may be attached to other geometry. "
			"The authored values are shown unchanged.").arg(report.medianRatio, 0, 'g', 3);
	return report;
}

// The same kind of check for recorded angles. `ratios` holds authored / drawn for each angle whose stored drawing has two readable
// extension lines (RecordedPmi::drawnAngleDegrees), both in degrees. This compares the authored value with the CAD system's own
// drawing, not with the mesh, so a mismatch means a wrongly read value (for example radians shown as degrees) and not tessellation.
inline SanityReport checkAngles(std::vector<double> ratios)
{
	SanityReport report;
	ratios.erase(std::remove_if(ratios.begin(), ratios.end(), [](double r) { return !(r > 0.0) || !std::isfinite(r); }), ratios.end());
	report.compared = int(ratios.size());
	if (ratios.empty())
		return report;
	for (double r : ratios)
		if (std::abs(r - 1.0) > OffTolerance)
			++report.off;
	std::sort(ratios.begin(), ratios.end());
	const size_t mid = ratios.size() / 2;
	report.medianRatio = ratios.size() % 2 ? ratios[mid] : 0.5 * (ratios[mid - 1] + ratios[mid]);
	if (report.off > 0)
		report.notes.append(QObject::tr("Recorded PMI: %1 of %2 angles differ by more than %3% from the extension lines drawn with them. "
			"The authored values are kept.").arg(report.off).arg(report.compared).arg(int(OffTolerance * 100.0)));
	if (report.medianRatio < GrossLow || report.medianRatio > GrossHigh)
		report.warning = QObject::tr("The recorded angles of this file are, typically, %1 times the angle of the lines drawn with them. "
			"The angle unit may have been read wrongly. The authored values are shown unchanged.").arg(report.medianRatio, 0, 'g', 3);
	return report;
}
}
