#include "ResultStreamlines.h"

#include "ResultCellFaces.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace
{
	// The faces of cell `c` as rings of dataset node indices (rings r = ringNodes[ringStart[r] .. ringStart[r + 1])). Empty for a cell that has none.
	void gatherRings(const ResultDataset& ds, std::size_t c, std::vector<std::uint32_t>& ringNodes, std::vector<std::size_t>& ringStart)
	{
		ringNodes.clear();
		ringStart.assign(1, 0);
		const ResultCellType type = ds.cellTypes[c];
		if (type == ResultCellType::Polyhedron)
		{
			for (std::size_t k = 0; k < ds.polyhedronFaceCount(c); ++k)
			{
				const std::uint32_t face = ds.cellFaces[ds.cellFaceOffsets[c] + k];
				ringNodes.insert(ringNodes.end(), ds.faceNodes.begin() + ds.faceOffsets[face], ds.faceNodes.begin() + ds.faceOffsets[face + 1]);
				ringStart.push_back(ringNodes.size());
			}
			return;
		}
		const resultcell::FaceRing* rings = nullptr;
		const int count = resultcell::faceRingsFor(type, rings);
		const std::uint32_t* nodes = ds.cellConnectivity.data() + ds.cellOffsets[c];
		for (int r = 0; r < count; ++r)
		{
			for (int i = 0; i < rings[r].count; ++i)
				ringNodes.push_back(nodes[rings[r].v[i]]);
			ringStart.push_back(ringNodes.size());
		}
	}

	// A point of the tetrahedral decomposition with its field values.
	struct VertexData
	{
		double x[3];
		double v[3];
		double s;
	};

	void addTo(VertexData& sum, const VertexData& a)
	{
		for (int k = 0; k < 3; ++k)
		{
			sum.x[k] += a.x[k];
			sum.v[k] += a.v[k];
		}
		sum.s += a.s;
	}

	void scaleBy(VertexData& a, double f)
	{
		for (int k = 0; k < 3; ++k)
		{
			a.x[k] *= f;
			a.v[k] *= f;
		}
		a.s *= f;
	}

	void cross(const double a[3], const double b[3], double out[3])
	{
		out[0] = a[1] * b[2] - a[2] * b[1];
		out[1] = a[2] * b[0] - a[0] * b[2];
		out[2] = a[0] * b[1] - a[1] * b[0];
	}

	double dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

	// Barycentric weights of p in the tetrahedron (a, b, c, d); true when p is inside (with a small tolerance) and the tetrahedron is not degenerate.
	bool barycentric(const VertexData& a, const VertexData& b, const VertexData& c, const VertexData& d, const double p[3], double w[4])
	{
		double e1[3], e2[3], e3[3], r[3];
		for (int k = 0; k < 3; ++k)
		{
			e1[k] = b.x[k] - a.x[k];
			e2[k] = c.x[k] - a.x[k];
			e3[k] = d.x[k] - a.x[k];
			r[k] = p[k] - a.x[k];
		}
		double c23[3], cr3[3], c2r[3];
		cross(e2, e3, c23);
		const double det = dot(e1, c23);
		const double scale = std::sqrt(dot(e1, e1) * dot(e2, e2) * dot(e3, e3));
		if (!(std::fabs(det) > 1e-12 * scale) || scale == 0.0)
			return false;
		cross(r, e3, cr3);
		cross(e2, r, c2r);
		w[1] = dot(r, c23) / det;
		w[2] = dot(e1, cr3) / det;
		w[3] = dot(e1, c2r) / det;
		w[0] = 1.0 - w[1] - w[2] - w[3];
		const double tolerance = -1e-7;
		return w[0] >= tolerance && w[1] >= tolerance && w[2] >= tolerance && w[3] >= tolerance;
	}
}

CellLocator::CellLocator(const ResultDataset& dataset, const std::atomic<bool>* cancel)
	: _ds(dataset)
{
	std::vector<std::uint32_t> ringNodes;
	std::vector<std::size_t> ringStart;
	double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double hi[3] = { -lo[0], -lo[1], -lo[2] };
	for (std::size_t c = 0; c < _ds.cellCount(); ++c)
	{
		if ((c & 0x3FFF) == 0 && cancel && cancel->load(std::memory_order_acquire))
		{
			_cells.clear();
			_boxes.clear();
			return;
		}
		if (!resultCellIsVolume(_ds.cellTypes[c]) && _ds.cellTypes[c] != ResultCellType::Polyhedron)
			continue;
		gatherRings(_ds, c, ringNodes, ringStart);
		if (ringNodes.empty())
			continue;
		float box[6] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			             -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
		bool finite = true;
		for (std::uint32_t node : ringNodes)
			for (int k = 0; k < 3; ++k)
			{
				const float v = _ds.nodePositions[static_cast<std::size_t>(node) * 3 + k];
				finite = finite && std::isfinite(v);
				box[k] = std::min(box[k], v);
				box[3 + k] = std::max(box[3 + k], v);
			}
		if (!finite)
			continue;
		_cells.push_back(static_cast<std::uint32_t>(c));
		_boxes.insert(_boxes.end(), box, box + 6);
		for (int k = 0; k < 3; ++k)
		{
			lo[k] = std::min(lo[k], static_cast<double>(box[k]));
			hi[k] = std::max(hi[k], static_cast<double>(box[3 + k]));
		}
	}
	if (_cells.empty())
		return;

	double extent[3];
	double maxExtent = 0.0;
	for (int k = 0; k < 3; ++k)
	{
		extent[k] = hi[k] - lo[k];
		maxExtent = std::max(maxExtent, extent[k]);
	}
	if (!(maxExtent > 0.0))
		maxExtent = 1.0;
	for (int k = 0; k < 3; ++k)
		extent[k] = std::max(extent[k], maxExtent * 1e-3); // a flat model still gets a grid of thickness
	_diagonal = std::sqrt(dot(extent, extent));
	const double volume = extent[0] * extent[1] * extent[2];
	double h = std::cbrt(volume / static_cast<double>(_cells.size()));
	for (;;)
	{
		std::size_t total = 1;
		for (int k = 0; k < 3; ++k)
		{
			_dims[k] = std::clamp(static_cast<int>(std::ceil(extent[k] / h)), 1, 512);
			total *= static_cast<std::size_t>(_dims[k]);
		}
		if (total <= 8u * 1024u * 1024u)
			break;
		h *= 1.5;
	}
	for (int k = 0; k < 3; ++k)
	{
		_origin[k] = lo[k];
		_binSize[k] = extent[k] / _dims[k];
	}

	auto range = [&](std::size_t i, int k, int& a, int& b) {
		a = std::clamp(static_cast<int>(std::floor((static_cast<double>(_boxes[i * 6 + k]) - _origin[k]) / _binSize[k])), 0, _dims[k] - 1);
		b = std::clamp(static_cast<int>(std::floor((static_cast<double>(_boxes[i * 6 + 3 + k]) - _origin[k]) / _binSize[k])), 0, _dims[k] - 1);
	};
	const std::size_t binCount = static_cast<std::size_t>(_dims[0]) * _dims[1] * _dims[2];
	_binStart.assign(binCount + 1, 0);
	for (int pass = 0; pass < 2; ++pass)
	{
		std::vector<std::uint32_t> cursor;
		if (pass == 1)
		{
			for (std::size_t b = 0; b < binCount; ++b)
				_binStart[b + 1] += _binStart[b];
			_binCells.assign(_binStart[binCount], 0);
			cursor.assign(_binStart.begin(), _binStart.end() - 1);
		}
		for (std::size_t i = 0; i < _cells.size(); ++i)
		{
			int a0, a1, b0, b1, c0, c1;
			range(i, 0, a0, a1);
			range(i, 1, b0, b1);
			range(i, 2, c0, c1);
			for (int z = c0; z <= c1; ++z)
				for (int y = b0; y <= b1; ++y)
					for (int x = a0; x <= a1; ++x)
					{
						const std::size_t bin = static_cast<std::size_t>(x) + static_cast<std::size_t>(_dims[0]) * (static_cast<std::size_t>(y) + static_cast<std::size_t>(_dims[1]) * z);
						if (pass == 0)
							++_binStart[bin + 1];
						else
							_binCells[cursor[bin]++] = static_cast<std::uint32_t>(i);
					}
		}
	}
}

double CellLocator::cellSize(int cell) const
{
	if (cell < 0)
		return 0.0;
	const std::size_t i = static_cast<std::size_t>(cell);
	if (i >= _cells.size())
		return 0.0;
	double d[3];
	for (int k = 0; k < 3; ++k)
		d[k] = static_cast<double>(_boxes[i * 6 + 3 + k]) - _boxes[i * 6 + k];
	return std::sqrt(dot(d, d));
}

bool CellLocator::evalCell(std::size_t index, const double p[3], const std::vector<float>& vectors, const std::vector<float>* scalar, double vector[3], double& scalarValue) const
{
	thread_local std::vector<std::uint32_t> ringNodes;
	thread_local std::vector<std::size_t> ringStart;
	const std::size_t cell = _cells[index];
	gatherRings(_ds, cell, ringNodes, ringStart);
	if (ringNodes.empty())
		return false;

	auto nodeData = [&](std::uint32_t n, VertexData& out) {
		for (int k = 0; k < 3; ++k)
		{
			out.x[k] = _ds.nodePositions[static_cast<std::size_t>(n) * 3 + k];
			out.v[k] = vectors[static_cast<std::size_t>(n) * 3 + k];
		}
		out.s = scalar ? (*scalar)[n] : 0.0;
		return std::isfinite(out.v[0]) && std::isfinite(out.v[1]) && std::isfinite(out.v[2]) && std::isfinite(out.s);
	};

	// The cell's centre: the mean of its face nodes.
	VertexData centre = {};
	VertexData node;
	for (std::uint32_t n : ringNodes)
	{
		if (!nodeData(n, node))
			return false;
		addTo(centre, node);
	}
	scaleBy(centre, 1.0 / static_cast<double>(ringNodes.size()));

	VertexData faceCentre, a, b, c;
	double w[4];
	for (std::size_t r = 0; r + 1 < ringStart.size(); ++r)
	{
		const std::size_t begin = ringStart[r], end = ringStart[r + 1], n = end - begin;
		if (n < 3)
			continue;
		if (n == 3)
		{
			// A triangle is used as it is on both sides of the face, so the field is continuous across it.
			nodeData(ringNodes[begin], a);
			nodeData(ringNodes[begin + 1], b);
			nodeData(ringNodes[begin + 2], c);
			if (barycentric(centre, a, b, c, p, w))
			{
				for (int k = 0; k < 3; ++k)
					vector[k] = w[0] * centre.v[k] + w[1] * a.v[k] + w[2] * b.v[k] + w[3] * c.v[k];
				scalarValue = w[0] * centre.s + w[1] * a.s + w[2] * b.s + w[3] * c.s;
				return true;
			}
			continue;
		}
		faceCentre = {};
		for (std::size_t i = begin; i < end; ++i)
		{
			nodeData(ringNodes[i], node);
			addTo(faceCentre, node);
		}
		scaleBy(faceCentre, 1.0 / static_cast<double>(n));
		for (std::size_t i = begin; i < end; ++i)
		{
			nodeData(ringNodes[i], a);
			nodeData(ringNodes[i + 1 < end ? i + 1 : begin], b);
			if (barycentric(centre, faceCentre, a, b, p, w))
			{
				for (int k = 0; k < 3; ++k)
					vector[k] = w[0] * centre.v[k] + w[1] * faceCentre.v[k] + w[2] * a.v[k] + w[3] * b.v[k];
				scalarValue = w[0] * centre.s + w[1] * faceCentre.s + w[2] * a.s + w[3] * b.s;
				return true;
			}
		}
	}
	return false;
}

bool CellLocator::interpolate(const double p[3], const std::vector<float>& vectors, const std::vector<float>* scalar, int& hint, double vector[3], double& scalarValue) const
{
	if (_cells.empty() || vectors.size() != _ds.nodeCount() * 3 || (scalar && scalar->size() != _ds.nodeCount()))
		return false;
	auto inBox = [&](std::size_t i) {
		const float* box = &_boxes[i * 6];
		return p[0] >= box[0] && p[0] <= box[3] && p[1] >= box[1] && p[1] <= box[4] && p[2] >= box[2] && p[2] <= box[5];
	};
	if (hint >= 0 && static_cast<std::size_t>(hint) < _cells.size() && inBox(static_cast<std::size_t>(hint))
	    && evalCell(static_cast<std::size_t>(hint), p, vectors, scalar, vector, scalarValue))
		return true;
	int idx[3];
	for (int k = 0; k < 3; ++k)
	{
		const double f = std::floor((p[k] - _origin[k]) / _binSize[k]);
		if (!(f >= -1.0) || !(f <= _dims[k]))
			return false;
		idx[k] = std::clamp(static_cast<int>(f), 0, _dims[k] - 1);
	}
	const std::size_t bin = static_cast<std::size_t>(idx[0]) + static_cast<std::size_t>(_dims[0]) * (static_cast<std::size_t>(idx[1]) + static_cast<std::size_t>(_dims[1]) * idx[2]);
	for (std::uint32_t k = _binStart[bin]; k < _binStart[bin + 1]; ++k)
	{
		const std::size_t i = _binCells[k];
		if (inBox(i) && evalCell(i, p, vectors, scalar, vector, scalarValue))
		{
			hint = static_cast<int>(i);
			return true;
		}
	}
	return false;
}

std::vector<float> CellLocator::randomPoints(std::size_t count, std::uint32_t seed) const
{
	std::vector<float> points;
	if (_cells.empty())
		return points;
	std::mt19937 rng(seed);
	std::uniform_int_distribution<std::size_t> pick(0, _cells.size() - 1);
	std::uniform_real_distribution<double> unit(1e-9, 1.0);
	std::vector<std::uint32_t> ringNodes;
	std::vector<std::size_t> ringStart;
	std::vector<double> weight;
	points.reserve(count * 3);
	for (std::size_t n = 0; n < count; ++n)
	{
		gatherRings(_ds, _cells[pick(rng)], ringNodes, ringStart);
		if (ringNodes.empty())
			continue;
		weight.assign(ringNodes.size(), 0.0);
		double total = 0.0;
		for (double& x : weight)
		{
			x = -std::log(unit(rng));
			total += x;
		}
		double p[3] = { 0, 0, 0 };
		for (std::size_t i = 0; i < ringNodes.size(); ++i)
			for (int k = 0; k < 3; ++k)
				p[k] += weight[i] / total * _ds.nodePositions[static_cast<std::size_t>(ringNodes[i]) * 3 + k];
		points.insert(points.end(), { static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]) });
	}
	return points;
}

namespace
{
	struct TracePoint
	{
		double x[3];
		float value;
	};

	class Tracer
	{
	public:
		Tracer(const CellLocator& locator, const std::vector<float>& vectors, const std::vector<float>* scalar, const StreamlineOptions& options)
			: _loc(locator), _vectors(vectors), _scalar(scalar), _o(options)
		{
			for (std::size_t n = 0; n + 2 < vectors.size(); n += 3)
			{
				const double speed = std::sqrt(static_cast<double>(vectors[n]) * vectors[n] + static_cast<double>(vectors[n + 1]) * vectors[n + 1]
				                               + static_cast<double>(vectors[n + 2]) * vectors[n + 2]);
				if (std::isfinite(speed))
					_maxSpeed = std::max(_maxSpeed, speed);
			}
			_maxLength = options.maxLengthFactor * locator.diagonal();
		}

		// The unit direction of the field at p (times `sign`), its value there, and the cell it is in. False outside the mesh or where the field vanishes.
		bool sample(const double p[3], int& hint, double sign, double dir[3], float& value) const
		{
			double v[3], s = 0.0;
			if (!_loc.interpolate(p, _vectors, _scalar, hint, v, s))
				return false;
			const double speed = std::sqrt(dot(v, v));
			if (!(speed > 0.0) || !(speed > _maxSpeed * _o.stagnationFraction))
				return false;
			for (int k = 0; k < 3; ++k)
				dir[k] = sign * v[k] / speed;
			value = static_cast<float>(_scalar ? s : speed);
			return true;
		}

		// Marches from `seed` (already known to be inside, in cell `hint`) in one direction; `out` receives the points after the seed.
		void march(const double seed[3], int hint, double sign, std::vector<TracePoint>& out) const
		{
			double p[3] = { seed[0], seed[1], seed[2] };
			double k1[3];
			float value = 0.0f;
			if (!sample(p, hint, sign, k1, value))
				return;
			double length = 0.0;
			for (int step = 0; step < _o.maxStepsPerDirection && length < _maxLength; ++step)
			{
				double size = _loc.cellSize(hint);
				if (!(size > 0.0))
					size = _loc.diagonal() * 0.01;
				double ds = _o.stepFactor * size;
				bool advanced = false;
				for (int halving = 0; halving < 6 && !advanced; ++halving, ds *= 0.5)
				{
					double k2[3], k3[3], k4[3], q[3], next[3], k1Next[3];
					float unused = 0.0f, nextValue = 0.0f;
					int h = hint;
					for (int k = 0; k < 3; ++k)
						q[k] = p[k] + 0.5 * ds * k1[k];
					if (!sample(q, h, sign, k2, unused))
						continue;
					for (int k = 0; k < 3; ++k)
						q[k] = p[k] + 0.5 * ds * k2[k];
					if (!sample(q, h, sign, k3, unused))
						continue;
					for (int k = 0; k < 3; ++k)
						q[k] = p[k] + ds * k3[k];
					if (!sample(q, h, sign, k4, unused))
						continue;
					for (int k = 0; k < 3; ++k)
						next[k] = p[k] + ds / 6.0 * (k1[k] + 2.0 * k2[k] + 2.0 * k3[k] + k4[k]);
					if (!sample(next, h, sign, k1Next, nextValue))
						continue;
					for (int k = 0; k < 3; ++k)
					{
						p[k] = next[k];
						k1[k] = k1Next[k];
					}
					hint = h;
					value = nextValue;
					length += ds;
					out.push_back({ { p[0], p[1], p[2] }, value });
					advanced = true;
				}
				if (!advanced)
					return; // the line reached the boundary, or the field vanished
			}
		}

	private:
		const CellLocator& _loc;
		const std::vector<float>& _vectors;
		const std::vector<float>* _scalar;
		StreamlineOptions _o;
		double _maxSpeed = 0.0;
		double _maxLength = 0.0;
	};
}

bool traceStreamlines(const ResultDataset& dataset, const CellLocator& locator, const std::vector<float>& vectors, const std::vector<float>* scalar,
                      const std::vector<float>& seeds, const StreamlineOptions& options, StreamlineSet& out, const std::atomic<bool>* cancel)
{
	out = StreamlineSet();
	if (vectors.size() != dataset.nodeCount() * 3 || (scalar && scalar->size() != dataset.nodeCount()))
		return false;
	if (locator.volumeCellCount() == 0)
		return true; // nothing to trace in
	const Tracer tracer(locator, vectors, scalar, options);
	std::vector<TracePoint> backward, forward;
	for (std::size_t s = 0; s + 2 < seeds.size(); s += 3)
	{
		if (cancel && cancel->load(std::memory_order_acquire))
			return false;
		const double seed[3] = { seeds[s], seeds[s + 1], seeds[s + 2] };
		int hint = -1;
		double dir[3];
		float value = 0.0f;
		if (!tracer.sample(seed, hint, 1.0, dir, value))
			continue;
		backward.clear();
		forward.clear();
		tracer.march(seed, hint, -1.0, backward);
		tracer.march(seed, hint, 1.0, forward);
		if (backward.size() + forward.size() < 1)
			continue;
		if (out.lineOffsets.empty())
			out.lineOffsets.push_back(0);
		auto add = [&](const double x[3], float v) {
			out.points.insert(out.points.end(), { static_cast<float>(x[0]), static_cast<float>(x[1]), static_cast<float>(x[2]) });
			out.values.push_back(v);
		};
		for (auto it = backward.rbegin(); it != backward.rend(); ++it)
			add(it->x, it->value);
		add(seed, value);
		for (const TracePoint& p : forward)
			add(p.x, p.value);
		out.lineOffsets.push_back(static_cast<std::uint32_t>(out.values.size()));
	}
	return true;
}

std::vector<float> randomPointsOnTriangles(const std::vector<float>& positions, const std::vector<std::uint32_t>& triangles, std::size_t count, std::uint32_t seed)
{
	std::vector<float> points;
	const std::size_t triangleCount = triangles.size() / 3;
	if (triangleCount == 0 || count == 0)
		return points;
	// The cumulative area, to pick a triangle with a chance proportional to its size.
	std::vector<double> cumulative(triangleCount);
	double total = 0.0;
	for (std::size_t t = 0; t < triangleCount; ++t)
	{
		const float* a = &positions[static_cast<std::size_t>(triangles[t * 3]) * 3];
		const float* b = &positions[static_cast<std::size_t>(triangles[t * 3 + 1]) * 3];
		const float* c = &positions[static_cast<std::size_t>(triangles[t * 3 + 2]) * 3];
		const double u[3] = { static_cast<double>(b[0]) - a[0], static_cast<double>(b[1]) - a[1], static_cast<double>(b[2]) - a[2] };
		const double v[3] = { static_cast<double>(c[0]) - a[0], static_cast<double>(c[1]) - a[1], static_cast<double>(c[2]) - a[2] };
		double n[3];
		cross(u, v, n);
		total += 0.5 * std::sqrt(dot(n, n));
		cumulative[t] = total;
	}
	if (!(total > 0.0))
		return points;
	std::mt19937 rng(seed);
	std::uniform_real_distribution<double> unit(0.0, 1.0);
	points.reserve(count * 3);
	for (std::size_t n = 0; n < count; ++n)
	{
		const double target = unit(rng) * total;
		const std::size_t t = std::min<std::size_t>(std::lower_bound(cumulative.begin(), cumulative.end(), target) - cumulative.begin(), triangleCount - 1);
		double r1 = std::sqrt(unit(rng)), r2 = unit(rng);
		const double wa = 1.0 - r1, wb = r1 * (1.0 - r2), wc = r1 * r2;
		for (int k = 0; k < 3; ++k)
		{
			const double v = wa * positions[static_cast<std::size_t>(triangles[t * 3]) * 3 + k] + wb * positions[static_cast<std::size_t>(triangles[t * 3 + 1]) * 3 + k]
			               + wc * positions[static_cast<std::size_t>(triangles[t * 3 + 2]) * 3 + k];
			points.push_back(static_cast<float>(v));
		}
	}
	return points;
}

bool isStreamlineField(const ResultField& field)
{
	return field.association == ResultFieldAssociation::Node && field.components == 3 && resultFieldHasData(field);
}

int chooseDefaultStreamlineField(const ResultDataset& dataset)
{
	int first = -1;
	for (std::size_t i = 0; i < dataset.fields.size(); ++i)
	{
		if (!isStreamlineField(dataset.fields[i]))
			continue;
		if (dataset.fields[i].name.contains(QLatin1String("veloc"), Qt::CaseInsensitive))
			return static_cast<int>(i);
		if (first < 0)
			first = static_cast<int>(i);
	}
	return first;
}
