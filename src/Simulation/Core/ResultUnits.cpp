#include "ResultUnits.h"

#include <cmath>

namespace
{
	QString U(const char16_t* text)
	{
		return QString::fromUtf16(text);
	}

	std::vector<QuantityKindInfo> buildKinds()
	{
		const double pi = 3.14159265358979323846;
		std::vector<QuantityKindInfo> kinds;

		kinds.push_back({ QStringLiteral("length"), QStringLiteral("Length"),
			{ { U(u"m"), 1.0, 0.0 }, { U(u"mm"), 1.0e-3, 0.0 }, { U(u"cm"), 1.0e-2, 0.0 }, { U(u"µm"), 1.0e-6, 0.0 },
			  { U(u"km"), 1.0e3, 0.0 }, { U(u"in"), 0.0254, 0.0 }, { U(u"ft"), 0.3048, 0.0 } } });

		kinds.push_back({ QStringLiteral("pressure"), QStringLiteral("Stress / pressure"),
			{ { U(u"Pa"), 1.0, 0.0 }, { U(u"kPa"), 1.0e3, 0.0 }, { U(u"MPa"), 1.0e6, 0.0 }, { U(u"GPa"), 1.0e9, 0.0 },
			  { U(u"bar"), 1.0e5, 0.0 }, { U(u"psi"), 6894.757293168, 0.0 }, { U(u"ksi"), 6894757.293168, 0.0 },
			  { U(u"atm"), 101325.0, 0.0 } } });

		kinds.push_back({ QStringLiteral("strain"), QStringLiteral("Strain"),
			{ { U(u"-"), 1.0, 0.0 }, { U(u"%"), 1.0e-2, 0.0 }, { U(u"µε"), 1.0e-6, 0.0 } } });

		// SI value is in kelvin: K = value * scale + offset.
		kinds.push_back({ QStringLiteral("temperature"), QStringLiteral("Temperature"),
			{ { U(u"K"), 1.0, 0.0 }, { U(u"°C"), 1.0, 273.15 }, { U(u"°F"), 5.0 / 9.0, 273.15 - 160.0 / 9.0 } } });

		kinds.push_back({ QStringLiteral("velocity"), QStringLiteral("Velocity"),
			{ { U(u"m/s"), 1.0, 0.0 }, { U(u"mm/s"), 1.0e-3, 0.0 }, { U(u"km/h"), 1.0 / 3.6, 0.0 },
			  { U(u"ft/s"), 0.3048, 0.0 }, { U(u"in/s"), 0.0254, 0.0 } } });

		kinds.push_back({ QStringLiteral("force"), QStringLiteral("Force"),
			{ { U(u"N"), 1.0, 0.0 }, { U(u"kN"), 1.0e3, 0.0 }, { U(u"MN"), 1.0e6, 0.0 },
			  { U(u"lbf"), 4.4482216152605, 0.0 }, { U(u"kgf"), 9.80665, 0.0 } } });

		kinds.push_back({ QStringLiteral("density"), QStringLiteral("Density"),
			{ { U(u"kg/m³"), 1.0, 0.0 }, { U(u"g/cm³"), 1.0e3, 0.0 }, { U(u"t/mm³"), 1.0e12, 0.0 },
			  { U(u"kg/mm³"), 1.0e9, 0.0 } } });

		kinds.push_back({ QStringLiteral("frequency"), QStringLiteral("Frequency"),
			{ { U(u"Hz"), 1.0, 0.0 }, { U(u"kHz"), 1.0e3, 0.0 }, { U(u"rad/s"), 1.0 / (2.0 * pi), 0.0 },
			  { U(u"rpm"), 1.0 / 60.0, 0.0 } } });

		kinds.push_back({ QStringLiteral("time"), QStringLiteral("Time"),
			{ { U(u"s"), 1.0, 0.0 }, { U(u"ms"), 1.0e-3, 0.0 }, { U(u"µs"), 1.0e-6, 0.0 },
			  { U(u"min"), 60.0, 0.0 }, { U(u"h"), 3600.0, 0.0 } } });

		kinds.push_back({ QStringLiteral("dimensionless"), QStringLiteral("Dimensionless"), { { U(u"-"), 1.0, 0.0 } } });
		return kinds;
	}

	const UnitDef* findUnit(const QuantityKindInfo& kind, const QString& symbol)
	{
		for (const UnitDef& u : kind.units)
			if (u.symbol == symbol)
				return &u;
		return nullptr;
	}

	// What a guess should assume for `kindId` in each unit system; "" = do not guess.
	QString guessedFileUnit(const QString& kindId, bool calculixSystem)
	{
		if (calculixSystem) // mm - N - MPa - tonne - s, the usual CalculiX/FreeCAD system
		{
			if (kindId == QLatin1String("length"))    return U(u"mm");
			if (kindId == QLatin1String("pressure"))  return U(u"MPa");
			if (kindId == QLatin1String("force"))     return U(u"N");
			if (kindId == QLatin1String("strain"))    return U(u"-");
			if (kindId == QLatin1String("velocity"))  return U(u"mm/s");
			if (kindId == QLatin1String("density"))   return U(u"t/mm³");
			if (kindId == QLatin1String("frequency")) return U(u"Hz");
			if (kindId == QLatin1String("time"))      return U(u"s");
			return QString();
		}
		// SI, the common convention of VTK exports (FreeCAD's .vtu writer, OpenFOAM, ...)
		if (kindId == QLatin1String("length"))    return U(u"m");
		if (kindId == QLatin1String("pressure"))  return U(u"Pa");
		if (kindId == QLatin1String("force"))     return U(u"N");
		if (kindId == QLatin1String("strain"))    return U(u"-");
		if (kindId == QLatin1String("velocity"))  return U(u"m/s");
		if (kindId == QLatin1String("density"))   return U(u"kg/m³");
		if (kindId == QLatin1String("frequency")) return U(u"Hz");
		if (kindId == QLatin1String("time"))      return U(u"s");
		return QString();
	}
}

const std::vector<QuantityKindInfo>& quantityKinds()
{
	static const std::vector<QuantityKindInfo> kinds = buildKinds();
	return kinds;
}

const QuantityKindInfo* findQuantityKind(const QString& kindId)
{
	for (const QuantityKindInfo& k : quantityKinds())
		if (k.id == kindId)
			return &k;
	return nullptr;
}

QStringList unitSymbols(const QString& kindId)
{
	QStringList symbols;
	if (const QuantityKindInfo* kind = findQuantityKind(kindId))
		for (const UnitDef& u : kind->units)
			symbols << u.symbol;
	return symbols;
}

UnitConversion unitConversion(const QString& kindId, const QString& fromUnit, const QString& toUnit)
{
	UnitConversion c;
	const QuantityKindInfo* kind = findQuantityKind(kindId);
	if (!kind)
		return c;
	const UnitDef* from = findUnit(*kind, fromUnit);
	const UnitDef* to = findUnit(*kind, toUnit);
	if (!from || !to)
		return c;
	// SI = v * from.scale + from.offset ; out = (SI - to.offset) / to.scale
	c.valid = true;
	c.scale = from->scale / to->scale;
	c.offset = (from->offset - to->offset) / to->scale;
	return c;
}

QString guessQuantityKind(const QString& fieldName)
{
	const QString n = fieldName.toLower();
	auto has = [&n](const char* text) { return n.contains(QLatin1String(text)); };

	if (has("strain"))
		return QStringLiteral("strain");
	if (has("stress") || has("von mises") || has("principal") || has("tresca") || has("pressure") || has("sigm_") || has("sief_"))
		return QStringLiteral("pressure");
	if (has("displacement") || has("depl") || n == QLatin1String("disp") || n.startsWith(QLatin1String("disp ")))
		return QStringLiteral("length");
	// "ndtemp" is CalculiX's nodal temperature block name in a .frd file
	if (has("temperature") || n == QLatin1String("temp") || n.startsWith(QLatin1String("temp ")) || n == QLatin1String("ndtemp"))
		return QStringLiteral("temperature");
	if (has("velocity") || n == QLatin1String("vel") || n.startsWith(QLatin1String("vel ")))
		return QStringLiteral("velocity");
	if (has("force") || n == QLatin1String("forc") || has("reaction"))
		return QStringLiteral("force");
	if (has("density"))
		return QStringLiteral("density");
	if (has("frequency") || n.startsWith(QLatin1String("freq")))
		return QStringLiteral("frequency");
	return QString();
}

void assignGuessedUnits(ResultDataset& dataset)
{
	const bool calculix = dataset.solverName == QLatin1String("CalculiX");
	for (ResultField& f : dataset.fields)
	{
		if (!f.fileUnit.isEmpty())
			continue; // node and cell fields alike; units a file states itself (OpenFOAM dimensions) are kept
		const QString kind = f.quantityKind.isEmpty() ? guessQuantityKind(f.name) : f.quantityKind;
		if (kind.isEmpty())
			continue;
		f.quantityKind = kind; // known quantity, even when there is no unit to guess (a temperature)
		const QString unit = guessedFileUnit(kind, calculix);
		if (unit.isEmpty())
			continue;
		f.fileUnit = unit;
		f.displayUnit = unit; // a guess labels the numbers, it never converts them
		f.unitConfirmed = false;
	}
}

bool setFieldUnits(ResultDataset& dataset, int fieldIndex, const QString& kindId, const QString& fileUnit,
                   const QString& displayUnit)
{
	if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= dataset.fields.size())
		return false;
	const QuantityKindInfo* kind = kindId.isEmpty() ? nullptr : findQuantityKind(kindId);
	QString file = fileUnit, display = displayUnit;
	if (kindId.isEmpty())
	{
		file.clear();
		display.clear();
	}
	else
	{
		if (!kind)
			return false;
		if (file.isEmpty())
			display.clear(); // quantity known, unit not: nothing to convert
		else
		{
			if (!findUnit(*kind, file))
				return false;
			if (display.isEmpty())
				display = file;
			if (!findUnit(*kind, display))
				return false;
		}
	}

	const int self = fieldIndex;
	const int source = dataset.fields[static_cast<std::size_t>(self)].derivedFromField;
	const int root = source >= 0 ? source : self;
	for (std::size_t i = 0; i < dataset.fields.size(); ++i)
	{
		ResultField& f = dataset.fields[i];
		if (static_cast<int>(i) != root && f.derivedFromField != root)
			continue;
		f.quantityKind = kindId;
		f.fileUnit = file;
		f.displayUnit = display;
		f.unitConfirmed = true;
	}
	return true;
}
