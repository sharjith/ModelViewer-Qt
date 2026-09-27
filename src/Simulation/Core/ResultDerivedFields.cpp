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

namespace
{
	const QString kSuffixes[5] = { QStringLiteral(" von Mises"), QStringLiteral(" max principal"), QStringLiteral(" mid principal"), QStringLiteral(" min principal"),
	                               QStringLiteral(" max shear") };

	// "stress", or Code_Aster's SIGM_* / SIEF_* stress tensors
	bool isStressTensor(const ResultField& source)
	{
		return source.components == 6 && (source.name.contains(QLatin1String("stress"), Qt::CaseInsensitive) || source.name.contains(QLatin1String("sigm_"), Qt::CaseInsensitive)
		                                  || source.name.contains(QLatin1String("sief_"), Qt::CaseInsensitive));
	}

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
		if (!isStressTensor(source) || step >= source.stepData.size())
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
		if (!isStressTensor(source))
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
