#pragma once

// Units for simulation result fields - see docs/simulation_results_design.md section 7. GUI-free (QtCore only).
//
// Every node field carries three separate things (ResultField): its quantity kind (stress, length, ...), the
// unit its numbers are WRITTEN in (fileUnit) and the unit they are SHOWN in (displayUnit), plus whether the user
// confirmed the file unit. Result files carry no units (CalculiX is unit-less; VTK has no standard place), so the
// file unit is a labelled GUESS until confirmed: a guess only labels the numbers, it never converts them, and
// only choosing a different display unit converts. Derived fields (von Mises, principals) share their source's
// units.

#include "ResultDataset.h"

#include <QString>
#include <QStringList>

#include <vector>

struct UnitDef
{
	QString symbol; // shown in the UI and used as the stable identifier
	double scale;   // SI value = value * scale + offset
	double offset;  // non-zero only for temperatures
};

struct QuantityKindInfo
{
	QString id;    // stable identifier stored in ResultField::quantityKind ("" = not specified)
	QString label; // shown in the UI
	std::vector<UnitDef> units;
};

// All kinds this build knows: length, pressure (stress), strain, temperature, velocity, force, density,
// frequency, time, dimensionless. The unit lists are ordered small-system first.
const std::vector<QuantityKindInfo>& quantityKinds();
const QuantityKindInfo* findQuantityKind(const QString& kindId);
QStringList unitSymbols(const QString& kindId); // empty for an unknown kind

// value_out = value_in * scale + offset. Affine so temperatures (K / degC / degF) convert correctly.
struct UnitConversion
{
	bool valid = false;
	double scale = 1.0;
	double offset = 0.0;
	bool isIdentity() const { return valid && scale == 1.0 && offset == 0.0; }
	double apply(double v) const { return v * scale + offset; }
};

// Conversion between two units of one kind. Invalid when the kind or either unit is unknown/empty.
UnitConversion unitConversion(const QString& kindId, const QString& fromUnit, const QString& toUnit);

// Best-effort quantity kind from a field name ("Displacement", "STRESS von Mises", "TOSTRAIN", "Pressure", ...);
// "" when nothing matches. Deliberately conservative: a wrong kind is worse than none.
QString guessQuantityKind(const QString& fieldName);

// Gives every node field a guessed kind and file unit, labelled unconfirmed (displayUnit = fileUnit, so nothing
// is converted). CalculiX results (dataset.solverName == "CalculiX") are guessed as the usual mm-N-MPa-tonne
// system; everything else as SI. Temperatures are never guessed (K and degC are equally likely). Only fills
// fields whose file unit is still empty, so it never overrides a unit the file or the user supplied.
void assignGuessedUnits(ResultDataset& dataset);

// Sets kind / file unit / display unit of field `fieldIndex` AND of its source and derived siblings (a derived
// von Mises field always shares STRESS's units), marking them confirmed. `displayUnit` empty means "same as the
// file unit". An empty `kindId` clears the units; a kind with an empty `fileUnit` means "quantity known, unit not
// yet said" (no conversion, no unit label). Returns false for an out-of-range index or a unit that does not belong
// to the kind.
bool setFieldUnits(ResultDataset& dataset, int fieldIndex, const QString& kindId, const QString& fileUnit,
                   const QString& displayUnit);
