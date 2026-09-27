#pragma once

#include <QString>
#include <QJsonObject>

class SceneMesh;
class SceneGraph;

// ---------------------------------------------------------------------------
// A real per-document/per-import unit policy, replacing the hardcoded
// millimetre assumption MeshProperties/MassPropertiesDialog used to make
// silently. See resolveEffectiveImportUnit()'s own doc comment for the
// resolution order, and SceneNode::importUnit/importUnitUserOverridden for
// where a per-import unit is actually stored.
// ---------------------------------------------------------------------------
enum class LengthUnit
{
	Unknown,
	Millimeter,
	Centimeter,
	Meter,
	Inch,
	Foot
};

// Never called with LengthUnit::Unknown by any caller in this app -
// resolveEffectiveImportUnit() always resolves to a concrete unit (falling
// back to Millimeter) before this is used for an actual conversion. Returns
// 1.0 for Unknown anyway (never a 0/negative value that could corrupt a
// downstream multiplication) rather than asserting, since this is a pure
// conversion-table lookup with no other way to signal misuse.
double lengthUnitToMillimeters(LengthUnit unit);

// For persistence (MVF JSON) and the "Import Units..." override combo box -
// stable, non-localized identifier strings, not display text.
QString lengthUnitToString(LengthUnit unit);
LengthUnit lengthUnitFromString(const QString& text, LengthUnit fallback);

// Distinguishes an unresolved fallback from a genuinely known unit - a bare
// LengthUnit return value can't tell "explicitly set to Millimeter" apart
// from "nothing was ever set, defaulted to Millimeter", and a caller
// surfacing this to the user (e.g. Mass Properties' units-disclosure note)
// needs exactly that distinction.
struct ResolvedLengthUnit
{
	LengthUnit unit = LengthUnit::Millimeter;
	// True only when resolved from a real, explicitly-set source (a file
	// node's own importUnit, or a document-level default) - false when
	// nothing matched and this fell through to the hardcoded Millimeter
	// fallback (resolution step 3 below).
	bool wasExplicit = false;
};

// Resolution order:
//  1. The mesh's own imported file's SceneNode::importUnit, if resolvable
//     and not Unknown (sceneGraph->findFileNode(mesh->getSourceFile())) -
//     the correct fit for "per-import" since one document can contain
//     several imported files with genuinely different native units.
//  1b. Failing that, the SceneNode that owns the mesh or the nearest ancestor with a unit set - how a simulation result's
//     surface (which has no imported file) takes the length unit of its result file.
//  2. viewerState["defaultImportUnit"], if present - the document-level
//     fallback for a mesh with no recorded import (programmatically created
//     geometry) or a file node that hasn't had its unit set.
//  3. LengthUnit::Millimeter - the hardcoded fallback that preserves every
//     existing scene/old MVF file's exact prior numeric behavior. Nothing
//     sets SceneNode::importUnit or viewerState["defaultImportUnit"] yet
//     (see this app's own units-policy plan, Steps 8/9) - every mesh
//     resolves here today by construction, not as a special case.
ResolvedLengthUnit resolveEffectiveImportUnit(SceneMesh* mesh, SceneGraph* sceneGraph, const QJsonObject& viewerState);
