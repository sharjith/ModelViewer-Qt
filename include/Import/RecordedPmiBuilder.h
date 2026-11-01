#pragma once

#include "LengthUnits.h"
#include "MeasurementData.h"
#include "SceneMeshRecord.h"

#include <QStringList>
#include <QVector>
#include <vector>

namespace RecordedPmi
{
struct BuildResult
{
	QVector<Measurement> measurements;
	QStringList notes; // things skipped or capped, for the import log
};

// Turns the linear dimensions recorded in the meshes' PMI metadata (see RecordedPmi.h) into Recorded Distance measurements
// anchored on the nearest mesh surface. `meshes` are the meshes of ONE import; each PMI-carrying part contributes its own
// dimensions, so every assembly instance gets its own set. `fileUnit` is the imported file's declared unit (Unknown when the
// file declared none): with it the authored value is converted to the dimension's display unit, without it the raw value is kept.
BuildResult buildLinearDimensions(const std::vector<SceneMeshRecord>& meshes, LengthUnit fileUnit);
}
