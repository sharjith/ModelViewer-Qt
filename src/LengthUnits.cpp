#include "LengthUnits.h"
#include "SceneMesh.h"
#include "SceneGraph.h"
#include "SceneNode.h"

double lengthUnitToMillimeters(LengthUnit unit)
{
	switch (unit)
	{
	case LengthUnit::Millimeter: return 1.0;
	case LengthUnit::Centimeter: return 10.0;
	case LengthUnit::Meter:      return 1000.0;
	case LengthUnit::Inch:       return 25.4;
	case LengthUnit::Foot:       return 304.8;
	case LengthUnit::Unknown:    return 1.0; // never actually reached via resolveEffectiveImportUnit() - see its own doc comment
	}
	return 1.0;
}

QString lengthUnitToString(LengthUnit unit)
{
	switch (unit)
	{
	case LengthUnit::Millimeter: return QStringLiteral("mm");
	case LengthUnit::Centimeter: return QStringLiteral("cm");
	case LengthUnit::Meter:      return QStringLiteral("m");
	case LengthUnit::Inch:       return QStringLiteral("in");
	case LengthUnit::Foot:       return QStringLiteral("ft");
	case LengthUnit::Unknown:    return QStringLiteral("unknown");
	}
	return QStringLiteral("unknown");
}

LengthUnit lengthUnitFromString(const QString& text, LengthUnit fallback)
{
	if (text == QLatin1String("mm")) return LengthUnit::Millimeter;
	if (text == QLatin1String("cm")) return LengthUnit::Centimeter;
	if (text == QLatin1String("m"))  return LengthUnit::Meter;
	if (text == QLatin1String("in")) return LengthUnit::Inch;
	if (text == QLatin1String("ft")) return LengthUnit::Foot;
	if (text == QLatin1String("unknown")) return LengthUnit::Unknown;
	return fallback;
}

ResolvedLengthUnit resolveEffectiveImportUnit(SceneMesh* mesh, SceneGraph* sceneGraph, const QJsonObject& viewerState)
{
	if (mesh && sceneGraph)
	{
		if (const SceneNode* fileNode = sceneGraph->findFileNode(mesh->getSourceFile()))
		{
			if (fileNode->importUnit != LengthUnit::Unknown)
				return ResolvedLengthUnit{ fileNode->importUnit, true };
		}
		// A mesh that is not part of an imported file (a simulation result's surface) can have a unit on the node that owns
		// it or on an ancestor: the nearest one that is set wins.
		for (const SceneNode* node = sceneGraph->findNodeForMesh(mesh->uuid()); node; node = node->parent)
			if (node->importUnit != LengthUnit::Unknown)
				return ResolvedLengthUnit{ node->importUnit, true };
	}

	if (viewerState.contains(QStringLiteral("defaultImportUnit")))
	{
		const LengthUnit documentDefault = lengthUnitFromString(
			viewerState.value(QStringLiteral("defaultImportUnit")).toString(), LengthUnit::Unknown);
		if (documentDefault != LengthUnit::Unknown)
			return ResolvedLengthUnit{ documentDefault, true };
	}

	return ResolvedLengthUnit{ LengthUnit::Millimeter, false };
}
