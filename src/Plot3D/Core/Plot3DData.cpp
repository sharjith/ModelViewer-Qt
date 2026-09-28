#include "Plot3DData.h"

#include <QLocale>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace
{
	bool fail(QString* error, const QString& message)
	{
		if (error)
			*error = message;
		return false;
	}

	bool blankRow(const QStringList& row)
	{
		return std::all_of(row.cbegin(), row.cend(), [](const QString& cell) { return cell.trimmed().isEmpty(); });
	}

	bool parseRecords(QString text, QChar delimiter, std::vector<QStringList>& records, QString* error)
	{
		if (text.startsWith(QChar(0xfeff)))
			text.remove(0, 1);
		QString field;
		QStringList row;
		bool quoted = false, closedQuote = false, fieldWasQuoted = false;
		int physicalLine = 1;
		auto finishField = [&]() {
			row.push_back(fieldWasQuoted ? field : field.trimmed());
			field.clear();
			fieldWasQuoted = false;
			closedQuote = false;
		};
		auto finishRow = [&]() {
			finishField();
			if (!blankRow(row))
				records.push_back(row);
			row.clear();
		};

		for (qsizetype i = 0; i < text.size(); ++i)
		{
			const QChar ch = text[i];
			if (quoted)
			{
				if (ch == QLatin1Char('"'))
				{
					if (i + 1 < text.size() && text[i + 1] == QLatin1Char('"'))
					{
						field += QLatin1Char('"');
						++i;
					}
					else
					{
						quoted = false;
						closedQuote = true;
					}
				}
				else
				{
					field += ch;
					if (ch == QLatin1Char('\n'))
						++physicalLine;
				}
				continue;
			}

			if (closedQuote)
			{
				if (ch == delimiter)
				{
					finishField();
					continue;
				}
				if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
				{
					if (ch == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) ++i;
					finishRow();
					++physicalLine;
					continue;
				}
				if (ch.isSpace())
					continue;
				return fail(error, QStringLiteral("Unexpected text after a closing quote on line %1.").arg(physicalLine));
			}

			if (ch == QLatin1Char('"') && field.trimmed().isEmpty())
			{
				field.clear();
				fieldWasQuoted = true;
				quoted = true;
			}
			else if (ch == delimiter)
				finishField();
			else if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
			{
				if (ch == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) ++i;
				finishRow();
				++physicalLine;
			}
			else
				field += ch;
		}
		if (quoted)
			return fail(error, QStringLiteral("Unterminated quoted field at the end of the input."));
		if (!field.isEmpty() || !row.isEmpty() || closedQuote || fieldWasQuoted)
			finishRow();
		return true;
	}

	bool columnValid(const Plot3DCsvTable& table, int column) { return column >= 0 && column < table.columnCount(); }

	bool numberAt(const Plot3DCsvTable& table, std::size_t row, int column, const QString& role, double& value, QString* error)
	{
		if (!columnValid(table, column))
			return fail(error, QStringLiteral("The %1 column is not selected or is outside the table.").arg(role));
		bool ok = false;
		value = QLocale::c().toDouble(table.rows[row][column].trimmed(), &ok);
		if (!ok || !std::isfinite(value))
			return fail(error, QStringLiteral("Row %1 has an invalid %2 value in column '%3'.")
				.arg(row + 1).arg(role, table.headers[column]));
		return true;
	}

	bool optionalNumberAt(const Plot3DCsvTable& table, std::size_t row, int column, const QString& role,
		double fallback, double& value, QString* error)
	{
		if (column < 0)
		{
			value = fallback;
			return true;
		}
		return numberAt(table, row, column, role, value, error);
	}

	void includePoint(const Plot3DPoint& point, double lo[3], double hi[3], bool& any)
	{
		const double p[3] = { point.x, point.y, point.z };
		for (int axis = 0; axis < 3; ++axis)
		{
			lo[axis] = any ? std::min(lo[axis], p[axis]) : p[axis];
			hi[axis] = any ? std::max(hi[axis], p[axis]) : p[axis];
		}
		any = true;
	}
}

std::size_t Plot3DDataset::itemCount() const
{
	return std::visit([](const auto& data) -> std::size_t {
		using T = std::decay_t<decltype(data)>;
		if constexpr (std::is_same_v<T, Plot3DSurfaceData> || std::is_same_v<T, Plot3DLineData> || std::is_same_v<T, Plot3DScatterData>)
			return data.samples.size();
		else if constexpr (std::is_same_v<T, Plot3DBarData>)
			return data.bars.size();
		else if constexpr (std::is_same_v<T, Plot3DVoxelData>)
			return data.voxels.size();
		else
			return data.arrows.size();
	}, content);
}

bool parsePlot3DCsv(const QString& text, const Plot3DCsvOptions& options, Plot3DCsvTable& out, QString* error)
{
	out = Plot3DCsvTable();
	if (error) error->clear();
	if (options.delimiter.isNull() || options.delimiter == QLatin1Char('"') || options.delimiter == QLatin1Char('\r')
	    || options.delimiter == QLatin1Char('\n'))
		return fail(error, QStringLiteral("Choose a valid one-character delimiter."));
	std::vector<QStringList> records;
	if (!parseRecords(text, options.delimiter, records, error))
		return false;
	if (records.empty())
		return fail(error, QStringLiteral("The table is empty."));

	std::size_t firstData = 0;
	if (options.firstRowIsHeader)
	{
		out.headers = records.front();
		firstData = 1;
	}
	else
	{
		for (int column = 0; column < records.front().size(); ++column)
			out.headers.push_back(QStringLiteral("Column %1").arg(column + 1));
	}
	if (out.headers.isEmpty())
		return fail(error, QStringLiteral("The table has no columns."));
	for (int column = 0; column < out.headers.size(); ++column)
	{
		out.headers[column] = out.headers[column].trimmed();
		if (out.headers[column].isEmpty())
			out.headers[column] = QStringLiteral("Column %1").arg(column + 1);
	}
	for (std::size_t row = firstData; row < records.size(); ++row)
	{
		if (records[row].size() != out.headers.size())
			return fail(error, QStringLiteral("Row %1 has %2 columns; expected %3.")
				.arg(row + 1).arg(records[row].size()).arg(out.headers.size()));
		out.rows.push_back(std::move(records[row]));
	}
	if (out.rows.empty())
		return fail(error, QStringLiteral("The table has column names but no data rows."));
	return true;
}

bool buildPlot3DDataset(const Plot3DCsvTable& table, Plot3DPrimitive primitive, const Plot3DColumnMapping& mapping,
	Plot3DDataset& out, QString* error, const Plot3DBuildOptions& options)
{
	out = Plot3DDataset();
	if (error) error->clear();
	if (table.empty() || table.columnCount() == 0)
		return fail(error, QStringLiteral("The table has no data."));
	if (!(options.defaultBarWidth > 0.0) || !(options.defaultBarDepth > 0.0))
		return fail(error, QStringLiteral("Bar width and depth must be positive."));
	out.primitive = primitive;
	out.name = plot3DPrimitiveName(primitive);

	auto pointAt = [&](std::size_t row, Plot3DPoint& point) {
		return numberAt(table, row, mapping.x, QStringLiteral("X"), point.x, error)
			&& numberAt(table, row, mapping.y, QStringLiteral("Y"), point.y, error)
			&& numberAt(table, row, mapping.z, QStringLiteral("Z"), point.z, error);
	};
	auto sampleAt = [&](std::size_t row, Plot3DSample& sample) {
		if (!pointAt(row, sample.position)) return false;
		return optionalNumberAt(table, row, mapping.value, QStringLiteral("value"), sample.position.z, sample.value, error);
	};

	switch (primitive)
	{
	case Plot3DPrimitive::Surface:
	case Plot3DPrimitive::Line:
	case Plot3DPrimitive::Scatter:
	{
		std::vector<Plot3DSample> samples;
		samples.reserve(table.rows.size());
		for (std::size_t row = 0; row < table.rows.size(); ++row)
		{
			Plot3DSample sample;
			if (!sampleAt(row, sample)) return false;
			samples.push_back(sample);
		}
		if (primitive == Plot3DPrimitive::Surface) out.content = Plot3DSurfaceData{ std::move(samples) };
		else if (primitive == Plot3DPrimitive::Line) out.content = Plot3DLineData{ std::move(samples) };
		else out.content = Plot3DScatterData{ std::move(samples) };
		break;
	}
	case Plot3DPrimitive::Bar:
	{
		Plot3DBarData data;
		data.bars.reserve(table.rows.size());
		for (std::size_t row = 0; row < table.rows.size(); ++row)
		{
			Plot3DBar bar;
			if (!numberAt(table, row, mapping.x, QStringLiteral("X"), bar.x, error)
			    || !optionalNumberAt(table, row, mapping.y, QStringLiteral("Y"), 0.0, bar.y, error)
			    || !numberAt(table, row, mapping.z, QStringLiteral("height"), bar.height, error)
			    || !optionalNumberAt(table, row, mapping.base, QStringLiteral("base"), 0.0, bar.base, error)
			    || !optionalNumberAt(table, row, mapping.width, QStringLiteral("width"), options.defaultBarWidth, bar.width, error)
			    || !optionalNumberAt(table, row, mapping.depth, QStringLiteral("depth"), options.defaultBarDepth, bar.depth, error)
			    || !optionalNumberAt(table, row, mapping.value, QStringLiteral("value"), bar.height, bar.value, error))
				return false;
			if (!(bar.width > 0.0) || !(bar.depth > 0.0))
				return fail(error, QStringLiteral("Row %1 has a non-positive bar width or depth.").arg(row + 1));
			data.bars.push_back(bar);
		}
		out.content = std::move(data);
		break;
	}
	case Plot3DPrimitive::Voxel:
	{
		Plot3DVoxelData data;
		data.voxels.reserve(table.rows.size());
		for (std::size_t row = 0; row < table.rows.size(); ++row)
		{
			double xyz[3], occupancy = 1.0;
			if (!numberAt(table, row, mapping.x, QStringLiteral("X index"), xyz[0], error)
			    || !numberAt(table, row, mapping.y, QStringLiteral("Y index"), xyz[1], error)
			    || !numberAt(table, row, mapping.z, QStringLiteral("Z index"), xyz[2], error)
			    || !optionalNumberAt(table, row, mapping.value, QStringLiteral("occupancy"), 1.0, occupancy, error))
				return false;
			for (double coordinate : xyz)
				if (coordinate < 0.0 || coordinate > static_cast<double>(std::numeric_limits<int>::max()) || std::floor(coordinate) != coordinate)
					return fail(error, QStringLiteral("Row %1 has a voxel index that is not a non-negative integer.").arg(row + 1));
			if (occupancy < 0.0 || occupancy > 1.0)
				return fail(error, QStringLiteral("Row %1 has an occupancy outside the 0 to 1 range.").arg(row + 1));
			data.voxels.push_back({ static_cast<int>(xyz[0]), static_cast<int>(xyz[1]), static_cast<int>(xyz[2]), occupancy });
		}
		out.content = std::move(data);
		break;
	}
	case Plot3DPrimitive::Quiver:
	{
		Plot3DQuiverData data;
		data.arrows.reserve(table.rows.size());
		for (std::size_t row = 0; row < table.rows.size(); ++row)
		{
			Plot3DQuiver arrow;
			if (!pointAt(row, arrow.position)
			    || !numberAt(table, row, mapping.u, QStringLiteral("U"), arrow.vector.x, error)
			    || !numberAt(table, row, mapping.v, QStringLiteral("V"), arrow.vector.y, error)
			    || !numberAt(table, row, mapping.w, QStringLiteral("W"), arrow.vector.z, error)
			    || !optionalNumberAt(table, row, mapping.value, QStringLiteral("value"),
			       std::sqrt(arrow.vector.x * arrow.vector.x + arrow.vector.y * arrow.vector.y + arrow.vector.z * arrow.vector.z), arrow.value, error))
				return false;
			data.arrows.push_back(arrow);
		}
		out.content = std::move(data);
		break;
	}
	}
	return !out.empty();
}

bool plot3DDataBounds(const Plot3DDataset& dataset, double minimum[3], double maximum[3])
{
	if (!minimum || !maximum)
		return false;
	bool any = false;
	std::visit([&](const auto& data) {
		using T = std::decay_t<decltype(data)>;
		if constexpr (std::is_same_v<T, Plot3DSurfaceData> || std::is_same_v<T, Plot3DLineData> || std::is_same_v<T, Plot3DScatterData>)
			for (const Plot3DSample& sample : data.samples) includePoint(sample.position, minimum, maximum, any);
		else if constexpr (std::is_same_v<T, Plot3DBarData>)
			for (const Plot3DBar& bar : data.bars)
			{
				includePoint({ bar.x - bar.width * 0.5, bar.y - bar.depth * 0.5, bar.base }, minimum, maximum, any);
				includePoint({ bar.x + bar.width * 0.5, bar.y + bar.depth * 0.5, bar.base + bar.height }, minimum, maximum, any);
			}
		else if constexpr (std::is_same_v<T, Plot3DVoxelData>)
			for (const Plot3DVoxel& voxel : data.voxels)
			{
				includePoint({ static_cast<double>(voxel.x), static_cast<double>(voxel.y), static_cast<double>(voxel.z) }, minimum, maximum, any);
				includePoint({ static_cast<double>(voxel.x + 1), static_cast<double>(voxel.y + 1), static_cast<double>(voxel.z + 1) }, minimum, maximum, any);
			}
		else
			for (const Plot3DQuiver& arrow : data.arrows)
				includePoint(arrow.position, minimum, maximum, any);
	}, dataset.content);
	return any;
}

QString plot3DPrimitiveName(Plot3DPrimitive primitive)
{
	switch (primitive)
	{
	case Plot3DPrimitive::Surface: return QStringLiteral("Surface");
	case Plot3DPrimitive::Line: return QStringLiteral("Line");
	case Plot3DPrimitive::Scatter: return QStringLiteral("Scatter");
	case Plot3DPrimitive::Bar: return QStringLiteral("Bar");
	case Plot3DPrimitive::Voxel: return QStringLiteral("Voxel");
	case Plot3DPrimitive::Quiver: return QStringLiteral("Quiver");
	}
	return QString();
}
