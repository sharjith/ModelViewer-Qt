#include "ExodusReader.h"

#if MV_HAVE_NETCDF

#include "ResultDerivedFields.h"

#include <netcdf.h>

#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
	struct NcFile
	{
		int id = -1;
		~NcFile()
		{
			if (id >= 0)
				nc_close(id);
		}
	};

	bool dimensionLength(int ncid, const char* name, std::size_t& length)
	{
		int dim = -1;
		if (nc_inq_dimid(ncid, name, &dim) != NC_NOERR)
			return false;
		return nc_inq_dimlen(ncid, dim, &length) == NC_NOERR;
	}

	bool variableId(int ncid, const QString& name, int& varid)
	{
		return nc_inq_varid(ncid, name.toLatin1().constData(), &varid) == NC_NOERR;
	}

	// Total number of elements of a variable (product of its dimension lengths).
	bool variableElementCount(int ncid, int varid, std::size_t& count)
	{
		int ndims = 0;
		if (nc_inq_varndims(ncid, varid, &ndims) != NC_NOERR)
			return false;
		std::vector<int> dims(static_cast<std::size_t>(ndims));
		if (ndims > 0 && nc_inq_vardimid(ncid, varid, dims.data()) != NC_NOERR)
			return false;
		count = 1;
		for (int d : dims)
		{
			std::size_t length = 0;
			if (nc_inq_dimlen(ncid, d, &length) != NC_NOERR)
				return false;
			count *= length;
		}
		return true;
	}

	// Integers as 64-bit whatever the file stores (Exodus "large model" files use 64-bit ids; NetCDF converts).
	bool readInt64(int ncid, const QString& name, std::vector<long long>& out)
	{
		int varid = -1;
		std::size_t count = 0;
		if (!variableId(ncid, name, varid) || !variableElementCount(ncid, varid, count))
			return false;
		out.assign(count, 0);
		return count == 0 || nc_get_var_longlong(ncid, varid, out.data()) == NC_NOERR;
	}

	bool readDoubles(int ncid, const QString& name, std::vector<double>& out)
	{
		int varid = -1;
		std::size_t count = 0;
		if (!variableId(ncid, name, varid) || !variableElementCount(ncid, varid, count))
			return false;
		out.assign(count, 0.0);
		return count == 0 || nc_get_var_double(ncid, varid, out.data()) == NC_NOERR;
	}

	// A character array [n][len] as n strings (padding and trailing blanks removed).
	QStringList readNames(int ncid, const QString& name)
	{
		QStringList names;
		int varid = -1;
		if (!variableId(ncid, name, varid))
			return names;
		int ndims = 0;
		if (nc_inq_varndims(ncid, varid, &ndims) != NC_NOERR || ndims != 2)
			return names;
		int dims[2];
		std::size_t rows = 0, length = 0;
		if (nc_inq_vardimid(ncid, varid, dims) != NC_NOERR || nc_inq_dimlen(ncid, dims[0], &rows) != NC_NOERR
		    || nc_inq_dimlen(ncid, dims[1], &length) != NC_NOERR || rows == 0 || length == 0)
			return names;
		std::vector<char> buffer(rows * length, '\0');
		if (nc_get_var_text(ncid, varid, buffer.data()) != NC_NOERR)
			return names;
		for (std::size_t r = 0; r < rows; ++r)
		{
			const char* row = buffer.data() + r * length;
			std::size_t n = 0;
			while (n < length && row[n] != '\0')
				++n;
			names << QString::fromLatin1(row, static_cast<qsizetype>(n)).trimmed();
		}
		return names;
	}

	QString readTextAttribute(int ncid, int varid, const char* attribute)
	{
		std::size_t length = 0;
		if (nc_inq_attlen(ncid, varid, attribute, &length) != NC_NOERR || length == 0)
			return QString();
		std::vector<char> buffer(length + 1, '\0');
		if (nc_get_att_text(ncid, varid, attribute, buffer.data()) != NC_NOERR)
			return QString();
		return QString::fromLatin1(buffer.data()).trimmed();
	}

	// The cell type of an element block from its elem_type text and nodes per element.
	ResultCellType cellTypeFor(const QString& elemType, std::size_t nodesPerElement)
	{
		const QString t = elemType.toUpper();
		auto is = [&t](const char* prefix) { return t.startsWith(QLatin1String(prefix)); };
		if (is("HEX"))
			return nodesPerElement == 8 ? ResultCellType::Hexahedron : (nodesPerElement == 20 ? ResultCellType::Hexahedron20 : ResultCellType::Unsupported);
		if (is("TET"))
			return nodesPerElement == 4 ? ResultCellType::Tetra : (nodesPerElement == 10 ? ResultCellType::Tetra10 : ResultCellType::Unsupported);
		if (is("WEDGE"))
			return nodesPerElement == 6 ? ResultCellType::Wedge : (nodesPerElement == 15 ? ResultCellType::Wedge15 : ResultCellType::Unsupported);
		if (is("PYRAMID"))
			return nodesPerElement == 5 ? ResultCellType::Pyramid : (nodesPerElement == 13 ? ResultCellType::Pyramid13 : ResultCellType::Unsupported);
		if (is("TRI"))
			return nodesPerElement == 3 ? ResultCellType::Triangle : (nodesPerElement == 6 ? ResultCellType::Triangle6 : ResultCellType::Unsupported);
		if (is("QUAD") || is("SHELL"))
			return nodesPerElement == 4 ? ResultCellType::Quad : (nodesPerElement == 8 ? ResultCellType::Quad8 : ResultCellType::Unsupported);
		return ResultCellType::Unsupported; // beams, trusses, spheres, polygons, polyhedra
	}

	// ---- Variables: one Exodus variable per component, gathered into vector / tensor fields ------------------------

	struct FieldSpec
	{
		QString name;
		std::vector<int> variables; // Exodus variable indices (0-based), one per component; -1 = a zero component (2-D vectors)
		std::vector<QString> componentNames;
	};

	std::vector<FieldSpec> groupVariables(const QStringList& names)
	{
		static const QRegularExpression suffix(QStringLiteral("^(.+)_(xx|yy|zz|xy|yz|zx|xz|x|y|z)$"), QRegularExpression::CaseInsensitiveOption);
		std::map<QString, std::map<QString, int>> byBase; // base -> lower-case suffix -> variable index
		for (int i = 0; i < names.size(); ++i)
		{
			const QRegularExpressionMatch m = suffix.match(names[i]);
			if (m.hasMatch())
				byBase[m.captured(1)][m.captured(2).toLower()] = i;
		}

		std::vector<FieldSpec> specs;
		std::vector<bool> consumed(static_cast<std::size_t>(names.size()), false);
		for (int i = 0; i < names.size(); ++i)
		{
			if (consumed[static_cast<std::size_t>(i)])
				continue;
			const QRegularExpressionMatch m = suffix.match(names[i]);
			if (m.hasMatch())
			{
				const QString base = m.captured(1);
				const std::map<QString, int>& s = byBase[base];
				auto has = [&s](const char* key) { return s.count(QString::fromLatin1(key)) > 0; };
				auto at = [&s](const char* key) { return s.at(QString::fromLatin1(key)); };
				FieldSpec spec;
				spec.name = base;
				if (has("xx") && has("yy") && has("zz") && has("xy") && has("yz") && (has("zx") || has("xz")))
				{
					spec.variables = { at("xx"), at("yy"), at("zz"), at("xy"), at("yz"), has("zx") ? at("zx") : at("xz") };
					spec.componentNames = { QStringLiteral("XX"), QStringLiteral("YY"), QStringLiteral("ZZ"),
					                        QStringLiteral("XY"), QStringLiteral("YZ"), QStringLiteral("ZX") };
				}
				else if (has("x") && has("y") && has("z"))
					spec.variables = { at("x"), at("y"), at("z") };
				else if (has("x") && has("y") && !has("z"))
					spec.variables = { at("x"), at("y"), -1 }; // 2-D: z is zero
				if (!spec.variables.empty())
				{
					for (int v : spec.variables)
						if (v >= 0)
							consumed[static_cast<std::size_t>(v)] = true;
					specs.push_back(std::move(spec));
					continue;
				}
			}
			FieldSpec scalar;
			scalar.name = names[i];
			scalar.variables = { i };
			consumed[static_cast<std::size_t>(i)] = true;
			specs.push_back(std::move(scalar));
		}
		return specs;
	}

	// Values of one variable at one step: `tuples` doubles -> floats appended interleaved into `out` at component c.
	void interleave(std::vector<float>& out, int components, int component, const std::vector<float>& values)
	{
		const std::size_t comps = static_cast<std::size_t>(components);
		for (std::size_t t = 0; t < values.size(); ++t)
			out[t * comps + static_cast<std::size_t>(component)] = values[t];
	}
}

bool exodusSupported() { return true; }
QStringList exodusExtensions() { return { QStringLiteral("e"), QStringLiteral("exo"), QStringLiteral("ex2"), QStringLiteral("g") }; }
QString exodusFileFilter() { return QStringLiteral("Exodus II (*.e *.exo *.ex2 *.g)"); }

ResultReadOutcome readExodus(const QString& path, const std::atomic<bool>* cancel)
{
	ResultReadOutcome outcome;
	auto cancelled = [cancel]() { return cancel && cancel->load(std::memory_order_acquire); };
	auto fail = [&outcome](const QString& message) {
		outcome.error = message;
		return std::move(outcome);
	};

	NcFile file;
	const QByteArray native = QFile::encodeName(path);
	const int openStatus = nc_open(native.constData(), NC_NOWRITE, &file.id);
	if (openStatus != NC_NOERR)
	{
		file.id = -1;
		return fail(QStringLiteral("Cannot open '%1' as an Exodus (NetCDF) file: %2").arg(path, QString::fromLatin1(nc_strerror(openStatus))));
	}
	const int ncid = file.id;

	std::size_t numNodes = 0, numDim = 0, numElem = 0, numBlocks = 0;
	if (!dimensionLength(ncid, "num_nodes", numNodes) || !dimensionLength(ncid, "num_dim", numDim)
	    || !dimensionLength(ncid, "num_elem", numElem) || !dimensionLength(ncid, "num_el_blk", numBlocks))
		return fail(QStringLiteral("'%1' is a NetCDF file but not an Exodus II mesh (num_nodes, num_dim, num_elem or num_el_blk is missing).").arg(path));
	if (numNodes == 0 || numElem == 0 || numBlocks == 0 || numDim < 2 || numDim > 3)
		return fail(QStringLiteral("The Exodus file has no mesh (%1 nodes, %2 elements, %3 blocks, %4 dimensions).").arg(numNodes).arg(numElem).arg(numBlocks).arg(numDim));

	auto dataset = std::make_unique<ResultDataset>();
	dataset->sourcePath = path;
	dataset->solverName = QStringLiteral("Exodus");

	// ---- Coordinates ---------------------------------------------------------------------------------------------
	dataset->nodePositions.assign(numNodes * 3, 0.0f);
	{
		std::vector<double> axis;
		const char* axisNames[3] = { "coordx", "coordy", "coordz" };
		bool separate = true;
		for (std::size_t d = 0; d < numDim && separate; ++d)
		{
			if (!readDoubles(ncid, QString::fromLatin1(axisNames[d]), axis) || axis.size() != numNodes)
			{
				separate = false;
				break;
			}
			for (std::size_t n = 0; n < numNodes; ++n)
				dataset->nodePositions[n * 3 + d] = static_cast<float>(axis[n]);
		}
		if (!separate)
		{
			std::vector<double> all; // the older layout: coord[num_dim][num_nodes]
			if (!readDoubles(ncid, QStringLiteral("coord"), all) || all.size() != numNodes * numDim)
				return fail(QStringLiteral("The Exodus file has no readable node coordinates."));
			for (std::size_t d = 0; d < numDim; ++d)
				for (std::size_t n = 0; n < numNodes; ++n)
					dataset->nodePositions[n * 3 + d] = static_cast<float>(all[d * numNodes + n]);
		}
	}
	{
		std::vector<long long> ids;
		if (readInt64(ncid, QStringLiteral("node_num_map"), ids) && ids.size() == numNodes)
			dataset->nodeIds.assign(ids.begin(), ids.end());
	}

	// ---- Element blocks --------------------------------------------------------------------------------------------
	struct Block
	{
		std::size_t first = 0; // index of its first element in the global numbering
		std::size_t count = 0;
	};
	std::vector<Block> blocks;
	std::size_t unsupportedBlocks = 0;
	dataset->cellOffsets.push_back(0);
	for (std::size_t b = 1; b <= numBlocks; ++b)
	{
		if (cancelled())
			return fail(QStringLiteral("cancelled"));
		std::size_t inBlock = 0, perElement = 0;
		if (!dimensionLength(ncid, QStringLiteral("num_el_in_blk%1").arg(b).toLatin1().constData(), inBlock)
		    || !dimensionLength(ncid, QStringLiteral("num_nod_per_el%1").arg(b).toLatin1().constData(), perElement))
		{
			// A block without elements (or without a node dimension) contributes nothing but keeps the numbering intact.
			blocks.push_back({ dataset->cellTypes.size(), 0 });
			continue;
		}
		int connectVar = -1;
		std::vector<long long> connect;
		if (!variableId(ncid, QStringLiteral("connect%1").arg(b), connectVar) || !readInt64(ncid, QStringLiteral("connect%1").arg(b), connect)
		    || connect.size() != inBlock * perElement)
			return fail(QStringLiteral("The connectivity of element block %1 cannot be read.").arg(b));
		const ResultCellType type = cellTypeFor(readTextAttribute(ncid, connectVar, "elem_type"), perElement);
		if (type == ResultCellType::Unsupported)
			++unsupportedBlocks;
		blocks.push_back({ dataset->cellTypes.size(), inBlock });
		for (std::size_t e = 0; e < inBlock; ++e)
		{
			for (std::size_t k = 0; k < perElement; ++k)
			{
				const long long node = connect[e * perElement + k];
				if (node < 1 || static_cast<std::size_t>(node) > numNodes)
					return fail(QStringLiteral("Element block %1 references node %2 but the file has %3 nodes.").arg(b).arg(node).arg(numNodes));
				dataset->cellConnectivity.push_back(static_cast<std::uint32_t>(node - 1)); // Exodus is 1-based
			}
			dataset->cellTypes.push_back(type);
			dataset->cellOffsets.push_back(static_cast<std::uint32_t>(dataset->cellConnectivity.size()));
		}
	}
	if (dataset->cellTypes.size() != numElem)
		outcome.warnings << QStringLiteral("The blocks hold %1 elements but the file declares %2.").arg(dataset->cellTypes.size()).arg(numElem);
	if (dataset->cellTypes.empty())
		return fail(QStringLiteral("The Exodus file has no element blocks with elements."));
	{
		std::vector<long long> ids;
		if (readInt64(ncid, QStringLiteral("elem_num_map"), ids) && ids.size() == dataset->cellTypes.size())
			dataset->cellIds.assign(ids.begin(), ids.end());
	}
	if (unsupportedBlocks > 0)
		outcome.warnings << QStringLiteral("%1 element block(s) of a type that cannot be displayed yet (beams, spheres, polygons or polyhedra) are kept in the numbering but not drawn.").arg(unsupportedBlocks);

	// ---- Time steps ------------------------------------------------------------------------------------------------
	std::size_t stepCount = 0;
	dimensionLength(ncid, "time_step", stepCount);
	std::vector<double> times;
	readDoubles(ncid, QStringLiteral("time_whole"), times);
	const std::size_t steps = std::max<std::size_t>(1, stepCount);
	for (std::size_t s = 0; s < steps; ++s)
	{
		ResultStep step;
		step.time = s < times.size() ? times[s] : static_cast<double>(s);
		dataset->steps.push_back(step);
	}

	// ---- Node and element variables --------------------------------------------------------------------------------
	const float nan = std::numeric_limits<float>::quiet_NaN();

	// Node variables: vals_nod_var<i> [time_step][num_nodes] (one step at a time keeps the read buffer small).
	{
		std::size_t variableCount = 0;
		dimensionLength(ncid, "num_nod_var", variableCount);
		QStringList names = readNames(ncid, QStringLiteral("name_nod_var"));
		while (static_cast<std::size_t>(names.size()) < variableCount)
			names << QStringLiteral("nod_var%1").arg(names.size() + 1);
		std::vector<std::vector<std::vector<float>>> data(variableCount); // [variable][step][node]
		for (std::size_t v = 0; v < variableCount && stepCount > 0; ++v)
		{
			if (cancelled())
				return fail(QStringLiteral("cancelled"));
			int varid = -1;
			if (!variableId(ncid, QStringLiteral("vals_nod_var%1").arg(v + 1), varid))
				continue;
			int ndims = 0;
			nc_inq_varndims(ncid, varid, &ndims);
			data[v].resize(steps);
			std::vector<double> buffer(numNodes);
			for (std::size_t s = 0; s < steps; ++s)
			{
				const std::size_t start[2] = { s, 0 };
				const std::size_t count[2] = { 1, numNodes };
				const int status = ndims >= 2 ? nc_get_vara_double(ncid, varid, start, count, buffer.data())
				                              : nc_get_var_double(ncid, varid, buffer.data());
				if (status != NC_NOERR)
				{
					data[v].clear();
					break;
				}
				data[v][s].assign(buffer.begin(), buffer.end());
			}
		}
		for (const FieldSpec& spec : groupVariables(names))
		{
			ResultField field;
			field.name = spec.name;
			field.association = ResultFieldAssociation::Node;
			field.components = static_cast<int>(spec.variables.size());
			field.componentNames = spec.componentNames;
			bool any = false;
			for (std::size_t s = 0; s < steps; ++s)
			{
				std::vector<float> values(numNodes * spec.variables.size(), 0.0f);
				bool complete = true;
				for (std::size_t c = 0; c < spec.variables.size(); ++c)
				{
					const int v = spec.variables[c];
					if (v < 0)
						continue; // a zero component
					if (static_cast<std::size_t>(v) >= data.size() || s >= data[static_cast<std::size_t>(v)].size())
					{
						complete = false;
						break;
					}
					interleave(values, field.components, static_cast<int>(c), data[static_cast<std::size_t>(v)][s]);
				}
				if (complete)
				{
					field.stepData.push_back(std::move(values));
					any = true;
				}
				else
					field.stepData.emplace_back();
			}
			if (any)
				dataset->fields.push_back(std::move(field));
		}
	}

	// Element variables: vals_elem_var<i>eb<j> [time_step][num_el_in_blk<j>]; blocks that do not define one are NaN.
	{
		std::size_t variableCount = 0;
		dimensionLength(ncid, "num_elem_var", variableCount);
		QStringList names = readNames(ncid, QStringLiteral("name_elem_var"));
		while (static_cast<std::size_t>(names.size()) < variableCount)
			names << QStringLiteral("elem_var%1").arg(names.size() + 1);
		const std::size_t cells = dataset->cellTypes.size();
		std::vector<std::vector<std::vector<float>>> data(variableCount); // [variable][step][cell]
		for (std::size_t v = 0; v < variableCount && stepCount > 0; ++v)
		{
			if (cancelled())
				return fail(QStringLiteral("cancelled"));
			data[v].assign(steps, std::vector<float>(cells, nan));
			bool found = false;
			for (std::size_t b = 0; b < blocks.size(); ++b)
			{
				int varid = -1;
				if (blocks[b].count == 0 || !variableId(ncid, QStringLiteral("vals_elem_var%1eb%2").arg(v + 1).arg(b + 1), varid))
					continue;
				found = true;
				std::vector<double> buffer(blocks[b].count);
				for (std::size_t s = 0; s < steps; ++s)
				{
					const std::size_t start[2] = { s, 0 };
					const std::size_t count[2] = { 1, blocks[b].count };
					if (nc_get_vara_double(ncid, varid, start, count, buffer.data()) != NC_NOERR)
						continue;
					for (std::size_t e = 0; e < blocks[b].count; ++e)
						data[v][s][blocks[b].first + e] = static_cast<float>(buffer[e]);
				}
			}
			if (!found)
				data[v].clear();
		}
		for (const FieldSpec& spec : groupVariables(names))
		{
			ResultField field;
			field.name = spec.name;
			field.association = ResultFieldAssociation::Cell;
			field.components = static_cast<int>(spec.variables.size());
			field.componentNames = spec.componentNames;
			bool any = false;
			for (std::size_t s = 0; s < steps; ++s)
			{
				std::vector<float> values(cells * spec.variables.size(), 0.0f);
				bool complete = true;
				for (std::size_t c = 0; c < spec.variables.size(); ++c)
				{
					const int v = spec.variables[c];
					if (v < 0)
						continue;
					if (static_cast<std::size_t>(v) >= data.size() || s >= data[static_cast<std::size_t>(v)].size())
					{
						complete = false;
						break;
					}
					interleave(values, field.components, static_cast<int>(c), data[static_cast<std::size_t>(v)][s]);
				}
				if (complete)
				{
					field.stepData.push_back(std::move(values));
					any = true;
				}
				else
					field.stepData.emplace_back();
			}
			if (any)
				dataset->fields.push_back(std::move(field));
		}
	}

	addDerivedStressFields(*dataset); // von Mises, principals, max shear of a "stress" tensor gathered above

	const QString invalid = dataset->validate();
	if (!invalid.isEmpty())
		return fail(QStringLiteral("The Exodus file is inconsistent: %1").arg(invalid));
	outcome.dataset = std::move(dataset);
	return outcome;
}

#else // no NetCDF in this build

bool exodusSupported() { return false; }
QStringList exodusExtensions() { return {}; }
QString exodusFileFilter() { return QString(); }

ResultReadOutcome readExodus(const QString&, const std::atomic<bool>*)
{
	ResultReadOutcome outcome;
	outcome.error = QStringLiteral("This build of ModelViewer was made without NetCDF, so Exodus II result files cannot be read.");
	return outcome;
}

#endif
