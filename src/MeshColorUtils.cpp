#include "MeshColorUtils.h"
#include "SceneMesh.h"
#include "Material.h"

#include <algorithm>
#include <vector>

QVector3D meshRepresentativeColor(const SceneMesh* mesh)
{
	if (!mesh)
		return QVector3D(0.0f, 0.0f, 0.0f);

	if (mesh->hasVertexColors())
	{
		const std::vector<Vertex> verts = mesh->vertices();
		if (!verts.empty())
		{
			double r = 0.0, g = 0.0, b = 0.0;
			float minR = 1.0f, maxR = 0.0f;
			float minG = 1.0f, maxG = 0.0f;
			float minB = 1.0f, maxB = 0.0f;
			for (const Vertex& v : verts)
			{
				r += v.Color.r;
				g += v.Color.g;
				b += v.Color.b;
				minR = std::min(minR, v.Color.r); maxR = std::max(maxR, v.Color.r);
				minG = std::min(minG, v.Color.g); maxG = std::max(maxG, v.Color.g);
				minB = std::min(minB, v.Color.b); maxB = std::max(maxB, v.Color.b);
			}

			const double n = static_cast<double>(verts.size());
			const QVector3D avg(static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n));

			// RenderableMesh::hasVertexColors() is true for essentially every
			// mesh, not just genuinely vertex-colored ones: SceneMesh.cpp
			// always uploads a colors buffer built from each Vertex::Color,
			// which defaults to white (1,1,1,1) when no real per-vertex data
			// was ever loaded - a harmless neutral multiplier for rendering,
			// but a useless signal here (confirmed real bug: every mesh in a
			// plain, non-vertex-colored import was reporting white,
			// regardless of its actual material color).
			//
			// The fix is specifically "reject a uniform WHITE buffer" - not
			// "reject any uniform buffer": a genuinely vertex-colored mesh
			// (Point Set Reconstruction output) can legitimately be a single
			// uniform non-white color throughout (e.g. a scan of a flat-
			// colored object), and that's real data, not a placeholder - an
			// earlier variance-only check incorrectly discarded that case
			// too (confirmed real bug). Only a uniform buffer that's ALSO
			// (close to) white is the tell-tale default-placeholder signature.
			constexpr float kVarianceEpsilon = 1e-3f;
			const bool hasVariance = (maxR - minR) > kVarianceEpsilon
				|| (maxG - minG) > kVarianceEpsilon
				|| (maxB - minB) > kVarianceEpsilon;

			constexpr float kWhiteEpsilon = 1e-3f;
			const bool looksLikeUniformWhiteDefault = !hasVariance
				&& avg.x() > (1.0f - kWhiteEpsilon)
				&& avg.y() > (1.0f - kWhiteEpsilon)
				&& avg.z() > (1.0f - kWhiteEpsilon);

			if (!looksLikeUniformWhiteDefault)
				return avg;
		}
	}

	// A material using the PBR Specular-Glossiness workflow
	// (KHR_materials_pbrSpecularGlossiness) stores its real color in
	// diffuseColor(), not albedoColor() (that's the separate metallic-
	// roughness field, which such a material never populates from its own
	// color) - see groupIndicesByCurrentMaterial()'s doc comment in
	// MaterialGrouping.h for the same workflow/field split mattering
	// elsewhere in this app. Ruled out as the cause of a since-diagnosed
	// Filter by Color report (logging confirmed albedoColor() was already
	// returning the correct, clean per-part color - the reported colors
	// just weren't close enough to the model's actual saturated primaries
	// to fall within tolerance), but kept as a correct, real fix for any
	// scene that DOES use this workflow.
	const Material& material = mesh->getMaterial();
	return material.getUseSpecularGlossiness() ? material.diffuseColor() : material.albedoColor();
}

QVector<QVector3D> dedupedColors(const QVector<QVector3D>& existing,
                                  const QVector<QVector3D>& candidates,
                                  float epsilon)
{
	QVector<QVector3D> result = existing;
	for (const QVector3D& candidate : candidates)
	{
		const bool isDuplicate = std::any_of(result.cbegin(), result.cend(),
			[&](const QVector3D& already) { return (already - candidate).length() <= epsilon; });
		if (!isDuplicate)
			result.push_back(candidate);
	}
	return result;
}
