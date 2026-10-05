#include "Plot3DPathlines.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>

bool tracePlot3DPathlines(const Plot3DUnsteadyField& field, const Plot3DPathlineDomain& domain, int seedCount,
	double t0, double t1, int steps, Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	if (seedCount < 2 || seedCount > 128 || !(domain.xMaximum > domain.xMinimum) || !(domain.yMaximum > domain.yMinimum)
		|| !(t1 > t0) || steps < 2 || steps > 2000)
	{
		if (error) *error = QObject::tr("Pathline ranges (X, Y and time) must increase, with 2 to 128 seeds and 2 to 2000 time steps.");
		return false;
	}
	const double dt = (t1 - t0) / steps;
	auto advance = [](const Plot3DVec3& p, const Plot3DVec3& k, double h) { return Plot3DVec3{ p.x + h * k.x, p.y + h * k.y, p.z + h * k.z }; };
	for (int seed = 0; seed < seedCount; ++seed)
	{
		Plot3DVec3 p{ (domain.xMinimum + domain.xMaximum) * 0.5,
			domain.yMinimum + (domain.yMaximum - domain.yMinimum) * seed / (seedCount - 1), domain.seedZ };
		for (int step = 0; step < steps; ++step)
		{
			const double t = t0 + dt * step;
			Plot3DVec3 k1, k2, k3, k4;
			if (!field(p, t, k1, error) || !field(advance(p, k1, dt * 0.5), t + dt * 0.5, k2, error)
				|| !field(advance(p, k2, dt * 0.5), t + dt * 0.5, k3, error) || !field(advance(p, k3, dt), t + dt, k4, error))
			{
				out = Plot3DMeshData();
				return false;
			}
			const Plot3DVec3 next{ p.x + dt / 6.0 * (k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x),
			                       p.y + dt / 6.0 * (k1.y + 2.0 * k2.y + 2.0 * k3.y + k4.y),
			                       p.z + dt / 6.0 * (k1.z + 2.0 * k2.z + 2.0 * k3.z + k4.z) };
			if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z)
				|| next.x < domain.xMinimum || next.x > domain.xMaximum || next.y < domain.yMinimum || next.y > domain.yMaximum
				|| (domain.boundZ && (next.z < domain.zMinimum || next.z > domain.zMaximum)))
				break; // left the domain: the trail ends where it left
			if (std::abs(next.x - p.x) + std::abs(next.y - p.y) + std::abs(next.z - p.z) < 1.0e-12)
			{
				p = next; // a stationary particle draws no segment
				continue;
			}
			out.positions.insert(out.positions.end(), { float(p.x), float(p.y), float(p.z), float(next.x), float(next.y), float(next.z) });
			out.normals.insert(out.normals.end(), { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f });
			out.values.push_back(t);
			out.values.push_back(t + dt);
			p = next;
		}
	}
	if (out.empty())
	{
		if (error) *error = QObject::tr("No pathline segments were generated in the selected domain.");
		return false;
	}
	return true;
}

namespace
{
	// Sorted unique values of one column of the table; false when a cell is not a finite number.
	bool columnValues(const Plot3DCsvTable& table, int column, const QString& name, std::vector<double>& values, QString* error)
	{
		values.resize(table.rows.size());
		for (std::size_t r = 0; r < table.rows.size(); ++r)
		{
			bool ok = false;
			const double v = table.rows[r][column].trimmed().toDouble(&ok);
			if (!ok || !std::isfinite(v))
			{
				if (error) *error = QObject::tr("Row %1: '%2' in the %3 column is not a number.").arg(r + 1).arg(table.rows[r][column]).arg(name);
				return false;
			}
			values[r] = v;
		}
		return true;
	}

	std::vector<double> uniqueSorted(std::vector<double> values)
	{
		std::sort(values.begin(), values.end());
		values.erase(std::unique(values.begin(), values.end()), values.end());
		return values;
	}

	// Lower node index and the fraction toward the next one for `value` on a sorted axis (clamped).
	void locate(const std::vector<double>& axis, double value, std::size_t& index, double& fraction)
	{
		if (axis.size() < 2 || value <= axis.front()) { index = 0; fraction = 0.0; return; }
		if (value >= axis.back()) { index = axis.size() - 2; fraction = 1.0; return; }
		index = static_cast<std::size_t>(std::upper_bound(axis.begin(), axis.end(), value) - axis.begin()) - 1;
		fraction = (value - axis[index]) / (axis[index + 1] - axis[index]);
	}
}

bool Plot3DTimeSeriesField::load(const Plot3DCsvTable& table, const Plot3DTimeSeriesColumns& columns, QString* error)
{
	_t.clear(); _x.clear(); _y.clear(); _z.clear(); _u.clear(); _v.clear(); _w.clear();
	const int count = table.columnCount();
	auto valid = [count](int column) { return column >= 0 && column < count; };
	if (!valid(columns.time) || !valid(columns.x) || !valid(columns.y) || !valid(columns.u) || !valid(columns.v) || !valid(columns.w)
		|| (columns.z != -1 && !valid(columns.z)))
	{
		if (error) *error = QObject::tr("Choose the Time, X, Y, U, V and W columns (Z is optional).");
		return false;
	}
	if (table.rows.empty())
	{
		if (error) *error = QObject::tr("The table has no rows.");
		return false;
	}

	std::vector<double> t, x, y, z, u, v, w;
	if (!columnValues(table, columns.time, QObject::tr("Time"), t, error) || !columnValues(table, columns.x, QObject::tr("X"), x, error)
		|| !columnValues(table, columns.y, QObject::tr("Y"), y, error) || !columnValues(table, columns.u, QObject::tr("U"), u, error)
		|| !columnValues(table, columns.v, QObject::tr("V"), v, error) || !columnValues(table, columns.w, QObject::tr("W"), w, error))
		return false;
	if (columns.z != -1)
	{
		if (!columnValues(table, columns.z, QObject::tr("Z"), z, error))
			return false;
	}
	else
		z.assign(table.rows.size(), 0.0);

	_t = uniqueSorted(t); _x = uniqueSorted(x); _y = uniqueSorted(y); _z = uniqueSorted(z);
	const std::size_t nt = _t.size(), nx = _x.size(), ny = _y.size(), nz = _z.size();
	if (nt < 2 || nx < 2 || ny < 2)
	{
		if (error) *error = QObject::tr("The table needs at least two distinct values of Time, X and Y.");
		return false;
	}
	const std::size_t nodes = nt * nz * ny * nx;
	if (nodes > 5000000)
	{
		if (error) *error = QObject::tr("The grid has too many nodes (%1 x %2 x %3 x %4 = %5; the limit is 5,000,000).").arg(nt).arg(nz).arg(ny).arg(nx).arg(nodes);
		return false;
	}
	if (table.rows.size() != nodes)
	{
		if (error) *error = QObject::tr("The table is not a complete regular grid: %1 distinct times x %2 x %3 x %4 positions need %5 rows but it has %6.")
			.arg(nt).arg(nx).arg(ny).arg(nz).arg(nodes).arg(table.rows.size());
		return false;
	}

	_u.assign(nodes, 0.0); _v.assign(nodes, 0.0); _w.assign(nodes, 0.0);
	std::vector<char> filled(nodes, 0);
	auto indexOf = [](const std::vector<double>& axis, double value) {
		return static_cast<std::size_t>(std::lower_bound(axis.begin(), axis.end(), value) - axis.begin());
	};
	for (std::size_t r = 0; r < table.rows.size(); ++r)
	{
		const std::size_t it = indexOf(_t, t[r]), ix = indexOf(_x, x[r]), iy = indexOf(_y, y[r]), iz = indexOf(_z, z[r]);
		const std::size_t node = ((it * nz + iz) * ny + iy) * nx + ix;
		if (filled[node])
		{
			if (error) *error = QObject::tr("Row %1 repeats a grid node (t = %2, x = %3, y = %4).").arg(r + 1).arg(t[r]).arg(x[r]).arg(y[r]);
			return false;
		}
		filled[node] = 1;
		_u[node] = u[r]; _v[node] = v[r]; _w[node] = w[r];
	}
	return true;
}

bool Plot3DTimeSeriesField::sample(const Plot3DVec3& p, double time, Plot3DVec3& out) const
{
	if (_u.empty())
		return false;
	const std::size_t nx = _x.size(), ny = _y.size(), nz = _z.size();
	std::size_t it, ix, iy, iz;
	double ft, fx, fy, fz;
	locate(_t, time, it, ft); locate(_x, p.x, ix, fx); locate(_y, p.y, iy, fy); locate(_z, p.z, iz, fz);
	const std::size_t t1 = _t.size() > 1 ? 1 : 0, x1 = nx > 1 ? 1 : 0, y1 = ny > 1 ? 1 : 0, z1 = nz > 1 ? 1 : 0;
	double u = 0.0, v = 0.0, w = 0.0;
	for (std::size_t dt = 0; dt <= t1; ++dt)
		for (std::size_t dz = 0; dz <= z1; ++dz)
			for (std::size_t dy = 0; dy <= y1; ++dy)
				for (std::size_t dx = 0; dx <= x1; ++dx)
				{
					const double weight = (dt ? ft : 1.0 - ft) * (dz ? fz : 1.0 - fz) * (dy ? fy : 1.0 - fy) * (dx ? fx : 1.0 - fx);
					if (weight == 0.0)
						continue;
					const std::size_t node = (((it + dt) * nz + (iz + dz)) * ny + (iy + dy)) * nx + (ix + dx);
					u += weight * _u[node]; v += weight * _v[node]; w += weight * _w[node];
				}
	out = Plot3DVec3{ u, v, w };
	return true;
}

Plot3DPathlineDomain Plot3DTimeSeriesField::domain() const
{
	Plot3DPathlineDomain d;
	d.xMinimum = _x.front(); d.xMaximum = _x.back();
	d.yMinimum = _y.front(); d.yMaximum = _y.back();
	d.boundZ = _z.size() > 1;
	d.zMinimum = _z.front(); d.zMaximum = _z.back();
	d.seedZ = (_z.front() + _z.back()) * 0.5;
	return d;
}

bool buildPlot3DTimeSeriesPathlines(const Plot3DCsvTable& table, const Plot3DTimeSeriesColumns& columns, int seedCount, int steps,
	Plot3DMeshData& out, QString* error)
{
	out = Plot3DMeshData();
	Plot3DTimeSeriesField field;
	if (!field.load(table, columns, error))
		return false;
	const Plot3DUnsteadyField sampler = [&field](const Plot3DVec3& p, double t, Plot3DVec3& v, QString*) { return field.sample(p, t, v); };
	return tracePlot3DPathlines(sampler, field.domain(), seedCount, field.timeMinimum(), field.timeMaximum(), steps, out, error);
}
