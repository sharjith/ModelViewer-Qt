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

void addDerivedStressFields(ResultDataset& dataset)
{
	const std::size_t nodes = dataset.nodeCount();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	std::vector<ResultField> added;

	auto exists = [&dataset](const QString& name)
	{
		for (const ResultField& f : dataset.fields)
			if (f.association == ResultFieldAssociation::Node && f.name == name)
				return true;
		return false;
	};

	for (std::size_t sourceIndex = 0; sourceIndex < dataset.fields.size(); ++sourceIndex)
	{
		const ResultField& source = dataset.fields[sourceIndex];
		if (source.association != ResultFieldAssociation::Node || source.components != 6
			|| !source.name.contains(QLatin1String("stress"), Qt::CaseInsensitive))
			continue;

		const QString suffixes[5] = { QStringLiteral(" von Mises"), QStringLiteral(" max principal"),
		                              QStringLiteral(" mid principal"), QStringLiteral(" min principal"),
		                              QStringLiteral(" max shear") };
		if (exists(source.name + suffixes[0]))
			continue;

		ResultField out[5];
		for (int k = 0; k < 5; ++k)
		{
			out[k].name = source.name + suffixes[k];
			out[k].association = ResultFieldAssociation::Node;
			out[k].components = 1;
			out[k].quantityKind = source.quantityKind;
			out[k].fileUnit = source.fileUnit;
			out[k].displayUnit = source.displayUnit;
			out[k].unitConfirmed = source.unitConfirmed;
			out[k].derivedFromField = static_cast<int>(sourceIndex);
			out[k].stepData.resize(source.stepData.size());
		}

		for (std::size_t s = 0; s < source.stepData.size(); ++s)
		{
			const std::vector<float>& t = source.stepData[s];
			if (t.size() != nodes * 6)
				continue; // this step has no data for the field (or it is not loaded)
			for (int k = 0; k < 5; ++k)
				out[k].stepData[s].assign(nodes, nan);
			for (std::size_t n = 0; n < nodes; ++n)
			{
				const float* c = &t[n * 6];
				bool finite = true;
				for (int i = 0; i < 6; ++i)
					finite = finite && std::isfinite(c[i]);
				if (!finite)
					continue;
				double e1, e2, e3;
				symmetricPrincipalValues(c[0], c[1], c[2], c[3], c[4], c[5], e1, e2, e3);
				out[0].stepData[s][n] = static_cast<float>(vonMisesStress(c[0], c[1], c[2], c[3], c[4], c[5]));
				out[1].stepData[s][n] = static_cast<float>(e1);
				out[2].stepData[s][n] = static_cast<float>(e2);
				out[3].stepData[s][n] = static_cast<float>(e3);
				out[4].stepData[s][n] = static_cast<float>(0.5 * (e1 - e3));
			}
		}
		for (int k = 0; k < 5; ++k)
			added.push_back(std::move(out[k]));
	}
	for (ResultField& f : added)
		dataset.fields.push_back(std::move(f));
}
