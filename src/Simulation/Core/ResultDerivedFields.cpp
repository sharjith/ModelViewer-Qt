#include "ResultDerivedFields.h"

#include <algorithm>
#include <cmath>
#include <limits>

void symmetricPrincipalValues(double xx, double yy, double zz, double xy, double yz, double zx,
                              double& e1, double& e2, double& e3)
{
	const double p1 = xy * xy + yz * yz + zx * zx;
	if (p1 <= 0.0)
	{
		// Diagonal already: the eigenvalues are the diagonal entries.
		double d[3] = { xx, yy, zz };
		std::sort(d, d + 3, [](double a, double b) { return a > b; });
		e1 = d[0];
		e2 = d[1];
		e3 = d[2];
		return;
	}
	const double q = (xx + yy + zz) / 3.0;
	const double p2 = (xx - q) * (xx - q) + (yy - q) * (yy - q) + (zz - q) * (zz - q) + 2.0 * p1;
	const double p = std::sqrt(p2 / 6.0);
	// B = (A - qI) / p ; r = det(B) / 2
	const double bxx = (xx - q) / p, byy = (yy - q) / p, bzz = (zz - q) / p;
	const double bxy = xy / p, byz = yz / p, bzx = zx / p;
	const double det = bxx * (byy * bzz - byz * byz) - bxy * (bxy * bzz - byz * bzx) + bzx * (bxy * byz - byy * bzx);
	const double r = std::clamp(det / 2.0, -1.0, 1.0);
	const double phi = std::acos(r) / 3.0;
	const double pi = 3.14159265358979323846;
	e1 = q + 2.0 * p * std::cos(phi);
	e3 = q + 2.0 * p * std::cos(phi + 2.0 * pi / 3.0);
	e2 = 3.0 * q - e1 - e3;
}

double vonMisesStress(double xx, double yy, double zz, double xy, double yz, double zx)
{
	return std::sqrt(0.5 * ((xx - yy) * (xx - yy) + (yy - zz) * (yy - zz) + (zz - xx) * (zz - xx))
	                 + 3.0 * (xy * xy + yz * yz + zx * zx));
}

bool isStressTensorField(const ResultField& source)
{
	return source.components == 6 && (source.name.contains(QLatin1String("stress"), Qt::CaseInsensitive)
		|| source.name.contains(QLatin1String("sigm_"), Qt::CaseInsensitive)
		|| source.name.contains(QLatin1String("sief_"), Qt::CaseInsensitive));
}

namespace
{
	struct Vec3
	{
		double x, y, z;
	};
	Vec3 cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	double lengthSq(const Vec3& a) { return dot(a, a); }
	Vec3 normalized(const Vec3& a)
	{
		const double len = std::sqrt(lengthSq(a));
		return len > 1.0e-300 ? Vec3{ a.x / len, a.y / len, a.z / len } : Vec3{ 1.0, 0.0, 0.0 };
	}

	// A unit null vector of the symmetric matrix `m` (rows m[0..2], each 3 components): for A - e*I at a genuine
	// eigenvalue e, any two independent rows' cross product is parallel to the eigenvector (it is orthogonal to
	// both rows, and a symmetric 3x3 matrix's row space is exactly the orthogonal complement of its null space
	// when the null space is 1-D). Tries all three row pairs and keeps the most numerically stable (largest) one;
	// falls back to `fallback` when every pair is degenerate (the null space is 2-D or 3-D: e has multiplicity > 1).
	Vec3 nullVector(const Vec3 (&m)[3], const Vec3& fallback)
	{
		Vec3 best = { 0, 0, 0 };
		double bestLenSq = 0.0;
		for (const auto& pair : { std::pair<int, int>{ 0, 1 }, { 0, 2 }, { 1, 2 } })
		{
			const Vec3 candidate = cross(m[pair.first], m[pair.second]);
			const double lenSq = lengthSq(candidate);
			if (lenSq > bestLenSq)
			{
				bestLenSq = lenSq;
				best = candidate;
			}
		}
		// The matrix's own scale sets what "degenerate" means; compare against its Frobenius-ish norm squared.
		double scale = 0.0;
		for (const Vec3& row : m)
			scale += lengthSq(row);
		return bestLenSq > 1.0e-20 * scale * scale ? normalized(best) : fallback;
	}
}

void symmetricEigenDecomposition(double xx, double yy, double zz, double xy, double yz, double zx, double e[3], double vectors[3][3])
{
	symmetricPrincipalValues(xx, yy, zz, xy, yz, zx, e[0], e[1], e[2]);
	auto rowsFor = [&](double eigen, Vec3 m[3]) {
		m[0] = { xx - eigen, xy, zx };
		m[1] = { xy, yy - eigen, yz };
		m[2] = { zx, yz, zz - eigen };
	};

	Vec3 rows[3];
	rowsFor(e[0], rows);
	const Vec3 v1 = nullVector(rows, Vec3{ 1.0, 0.0, 0.0 });

	rowsFor(e[1], rows);
	// A reference axis least parallel to v1, so the Gram-Schmidt step below is well-conditioned even when the
	// e[1] null-vector search itself is degenerate (e[0] == e[1], an eigenplane: any vector in it works for v2).
	const Vec3 axis = std::fabs(v1.x) < std::fabs(v1.y) && std::fabs(v1.x) < std::fabs(v1.z) ? Vec3{ 1, 0, 0 }
	                 : (std::fabs(v1.y) < std::fabs(v1.z) ? Vec3{ 0, 1, 0 } : Vec3{ 0, 0, 1 });
	Vec3 v2raw = nullVector(rows, normalized(cross(v1, axis)));
	// Force exact orthogonality to v1 regardless of which branch produced v2raw.
	const double proj = dot(v2raw, v1);
	v2raw = { v2raw.x - proj * v1.x, v2raw.y - proj * v1.y, v2raw.z - proj * v1.z };
	// The subtraction left it near zero (v2raw was ~parallel to v1, an unlucky pick): fall back to the axis method.
	const Vec3 v2 = lengthSq(v2raw) > 1.0e-12 ? normalized(v2raw) : normalized(cross(v1, axis));

	const Vec3 v3 = normalized(cross(v1, v2));
	const Vec3 axes[3] = { v1, v2, v3 };
	for (int k = 0; k < 3; ++k)
	{
		vectors[k][0] = axes[k].x;
		vectors[k][1] = axes[k].y;
		vectors[k][2] = axes[k].z;
	}
}

namespace
{
	const QString kSuffixes[5] = { QStringLiteral(" von Mises"), QStringLiteral(" max principal"), QStringLiteral(" mid principal"), QStringLiteral(" min principal"),
	                               QStringLiteral(" max shear") };

	// The five derived fields of a source: their indices in dataset.fields, -1 where missing.
	void derivedIndices(const ResultDataset& dataset, std::size_t sourceIndex, int out[5])
	{
		const ResultField& source = dataset.fields[sourceIndex];
		for (int k = 0; k < 5; ++k)
		{
			out[k] = -1;
			for (std::size_t i = 0; i < dataset.fields.size(); ++i)
				if (dataset.fields[i].association == source.association && dataset.fields[i].name == source.name + kSuffixes[k]
				    && dataset.fields[i].derivedFromField == static_cast<int>(sourceIndex))
				{
					out[k] = static_cast<int>(i);
					break;
				}
		}
	}
}

void computeDerivedStressStep(ResultDataset& dataset, std::size_t step)
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	for (std::size_t sourceIndex = 0; sourceIndex < dataset.fields.size(); ++sourceIndex)
	{
		const ResultField& source = dataset.fields[sourceIndex];
		if (!isStressTensorField(source) || step >= source.stepData.size())
			continue;
		int index[5];
		derivedIndices(dataset, sourceIndex, index);
		if (index[0] < 0 || index[1] < 0 || index[2] < 0 || index[3] < 0 || index[4] < 0)
			continue;
		const std::size_t tuples = source.association == ResultFieldAssociation::Node ? dataset.nodeCount() : dataset.cellCount();
		const std::vector<float>& t = source.stepData[step];
		if (t.size() != tuples * 6)
			continue; // this step has no data for the field (or it is not loaded)
		std::vector<float>* out[5];
		for (int k = 0; k < 5; ++k)
		{
			ResultField& derived = dataset.fields[static_cast<std::size_t>(index[k])];
			if (step >= derived.stepData.size())
				derived.stepData.resize(std::max(dataset.steps.size(), step + 1));
			out[k] = &derived.stepData[step];
			out[k]->assign(tuples, nan);
		}
		for (std::size_t n = 0; n < tuples; ++n)
		{
			const float* c = &t[n * 6];
			bool finite = true;
			for (int i = 0; i < 6; ++i)
				finite = finite && std::isfinite(c[i]);
			if (!finite)
				continue;
			double e1, e2, e3;
			symmetricPrincipalValues(c[0], c[1], c[2], c[3], c[4], c[5], e1, e2, e3);
			(*out[0])[n] = static_cast<float>(vonMisesStress(c[0], c[1], c[2], c[3], c[4], c[5]));
			(*out[1])[n] = static_cast<float>(e1);
			(*out[2])[n] = static_cast<float>(e2);
			(*out[3])[n] = static_cast<float>(e3);
			(*out[4])[n] = static_cast<float>(0.5 * (e1 - e3));
		}
	}
}

void addDerivedStressFields(ResultDataset& dataset)
{
	std::vector<ResultField> added;

	auto exists = [&dataset](const QString& name, ResultFieldAssociation association)
	{
		for (const ResultField& f : dataset.fields)
			if (f.association == association && f.name == name)
				return true;
		return false;
	};

	const std::size_t sourceCount = dataset.fields.size();
	for (std::size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
	{
		const ResultField& source = dataset.fields[sourceIndex];
		if (!isStressTensorField(source))
			continue;
		if (exists(source.name + kSuffixes[0], source.association))
			continue;

		ResultField out[5];
		for (int k = 0; k < 5; ++k)
		{
			out[k].name = source.name + kSuffixes[k];
			out[k].association = source.association;
			out[k].components = 1;
			out[k].quantityKind = source.quantityKind;
			out[k].fileUnit = source.fileUnit;
			out[k].displayUnit = source.displayUnit;
			out[k].unitConfirmed = source.unitConfirmed;
			out[k].derivedFromField = static_cast<int>(sourceIndex);
			out[k].lazyData = source.lazyData;
			out[k].stepData.resize(source.stepData.size());
			added.push_back(std::move(out[k]));
		}
	}
	const std::size_t before = dataset.fields.size();
	for (ResultField& f : added)
		dataset.fields.push_back(std::move(f));
	if (dataset.fields.size() == before)
		return;
	// The steps that have their source's data now (all of them for an eager result, none yet for a lazy one: those are computed as they are loaded).
	std::size_t stepSlots = 0;
	for (const ResultField& f : dataset.fields)
		stepSlots = std::max(stepSlots, f.stepData.size());
	for (std::size_t s = 0; s < stepSlots; ++s)
		computeDerivedStressStep(dataset, s);
}
