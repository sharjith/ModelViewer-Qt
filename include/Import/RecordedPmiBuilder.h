#pragma once

#include "AnnotationData.h"
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

struct AnnotationResult
{
	QVector<Annotation> annotations;
	QStringList notes;
};

// Turns the dimensions recorded in the meshes' PMI metadata (see RecordedPmi.h) into Recorded measurements anchored on the
// nearest mesh surface: linear, diameter and radius as Distance (a diameter or radius is anchored on two opposite rim points),
// angle as a Point at its vertex. `meshes` are the meshes of ONE import; each PMI-carrying part contributes its own items, so
// every assembly instance gets its own set. `fileUnit` is the imported file's declared unit (Unknown when the file declared
// none): with it the authored value is converted to the dimension's display unit, without it the raw value is kept.
BuildResult buildDimensions(const std::vector<SceneMeshRecord>& meshes, LengthUnit fileUnit);

// Turns the recorded notes, balloon notes, datum targets, regions (datum points) and surface finish symbols into annotations
// anchored on the nearest mesh surface to the point each marks, with the text placed where the author put it.
AnnotationResult buildAnnotations(const std::vector<SceneMeshRecord>& meshes);
}
