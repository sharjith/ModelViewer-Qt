#include "MaterialGrouping.h"
#include "SceneMesh.h"
#include "Material.h"

#include <QHash>
#include <QString>
#include <QVector2D>

#include <algorithm>
#include <numeric>

std::vector<std::vector<int>> groupIndicesByMaterial(const QVector<SceneMesh*>& meshes,
                                                       const std::vector<int>& indices)
{
	std::vector<std::vector<int>> groups;
	QHash<QString, int> groupIndexByKey; // composite key -> index into groups

	for (int idx : indices)
	{
		SceneMesh* mesh = meshes[idx];
		const int materialIndex = mesh->getOriginalMaterialIndex();
		if (materialIndex < 0)
		{
			groups.push_back({ idx });
			continue;
		}

		const QString key = mesh->getSourceFile() + QLatin1Char('|') + QString::number(materialIndex)
			+ QLatin1Char('|') + QString::number(static_cast<int>(mesh->getPrimitiveMode()));
		const auto it = groupIndexByKey.constFind(key);
		if (it == groupIndexByKey.cend())
		{
			groupIndexByKey.insert(key, static_cast<int>(groups.size()));
			groups.push_back({ idx });
		}
		else
		{
			groups[it.value()].push_back(idx);
		}
	}

	return groups;
}

std::vector<int> meshIndicesUsingSameMaterialAs(const QVector<SceneMesh*>& meshes, int meshIndex)
{
	if (meshIndex < 0 || meshIndex >= meshes.size())
		return {};

	std::vector<int> allIndices(meshes.size());
	std::iota(allIndices.begin(), allIndices.end(), 0);

	for (const std::vector<int>& group : groupIndicesByMaterial(meshes, allIndices))
	{
		if (std::find(group.begin(), group.end(), meshIndex) != group.end())
			return group;
	}
	return {};
}

namespace
{
	QString vec2Key(const QVector2D& v)
	{
		return QString::number(v.x(), 'f', 4) + QLatin1Char(',')
			+ QString::number(v.y(), 'f', 4);
	}

	QString vec3Key(const QVector3D& v)
	{
		return QString::number(v.x(), 'f', 4) + QLatin1Char(',')
			+ QString::number(v.y(), 'f', 4) + QLatin1Char(',')
			+ QString::number(v.z(), 'f', 4);
	}

	QString f(float v) { return QString::number(v, 'f', 4); }
	QString b(bool v) { return v ? QStringLiteral("1") : QStringLiteral("0"); }

	QString packingKey(const Material::ChannelPacking& p)
	{
		return QString::number(p.channel) + QLatin1Char(',')
			+ b(p.invert) + QLatin1Char(',')
			+ f(p.scale) + QLatin1Char(',')
			+ f(p.bias);
	}

	// The LIVE per-type texture path - deliberately NOT texture(type).path.
	// Material::setTexture() (the "Unified Texture API") populates
	// _textures[type] and one-way-syncs it INTO these named per-type fields
	// via syncTextureParameters() - so immediately after import (which does
	// call setTexture()) the two agree. But every live-editing path in this
	// app (MaterialPropertiesPanel, the eyedropper's sampled-material apply,
	// etc.) writes ONLY through the named setters (setAlbedoMap() and its 24
	// siblings), which never write back to _textures[] - so for any material
	// edited after import, texture(type).path is stale while the named
	// accessor is current. Confirmed against resolveMaterialTextures() (the
	// function that actually resolves what gets rendered), which reads the
	// path via albedoMapPath()-style accessors, not texture(type).path -
	// same "two storage locations, only one is live" trap as
	// textureTransform() vs. texture(type)'s own transform fields, just for
	// paths instead of UV transforms. This function is the 1:1 map from
	// TextureType to its correct live accessor - every one of the 25 values
	// has its own separately-named accessor, no generic pattern to loop over.
	QString liveTexturePath(const Material& mat, Material::TextureType type)
	{
		using T = Material::TextureType;
		switch (type)
		{
		case T::Albedo:                   return mat.albedoMapPath();
		case T::Metallic:                 return mat.metallicMapPath();
		case T::Roughness:                return mat.roughnessMapPath();
		case T::Normal:                   return mat.normalMapPath();
		case T::AmbientOcclusion:         return mat.aoMapPath();
		case T::Opacity:                  return mat.opacityMapPath();
		case T::Emissive:                 return mat.emissiveMapPath();
		case T::Height:                   return mat.heightMapPath();
		case T::Transmission:             return mat.transmissionMapPath();
		case T::IOR:                      return mat.iorMapPath();
		case T::SheenColor:               return mat.sheenColorMapPath();
		case T::SheenRoughness:           return mat.sheenRoughnessMapPath();
		case T::ClearcoatColor:           return mat.clearcoatColorMapPath();
		case T::ClearcoatRoughness:       return mat.clearcoatRoughnessMapPath();
		case T::ClearcoatNormal:          return mat.clearcoatNormalMapPath();
		case T::Iridescence:              return mat.iridescenceMap();
		case T::IridescenceThickness:     return mat.iridescenceThicknessMap();
		case T::SpecularFactor:           return mat.specularFactorMap();
		case T::SpecularColor:            return mat.specularColorMap();
		case T::Anisotropy:               return mat.anisotropyMap();
		case T::DiffuseTransmission:      return mat.diffuseTransmissionMap();
		case T::DiffuseTransmissionColor: return mat.diffuseTransmissionColorMap();
		case T::Thickness:                return mat.thicknessMap();
		case T::Diffuse:                  return mat.diffuseMapPath();
		case T::SpecularGlossiness:       return mat.specularGlossinessMap();
		case T::Count:                    break;
		}
		return QString();
	}

	// Exhaustive over every non-transient, LIVE field Material exposes - built
	// by enumerating its complete public getter list directly (not a hand-
	// picked subset), after three rounds of real audit findings on earlier,
	// narrower versions of this function: (1) transmission/IOR/clearcoat
	// missing, (2) specularColorFactor/diffuseColor/glossinessFactor missing,
	// (3) channel packing missing AND texture(type).path/rotation/offset/
	// scale being the WRONG, import-time-stale copy for any material edited
	// since import (see liveTexturePath()/textureTransform() above - the
	// live source for path and transform respectively is NOT texture(type)
	// itself). Covers:
	//  - the "new" PBR/glTF field set (metalness/roughness workflow)
	//  - the legacy Phong field set (ambient/diffuse/specular/shininess/
	//    metallic) - separate storage from the PBR fields above, not a
	//    renamed alias of them
	//  - the PBR Specular-Glossiness workflow field set (diffuseColor/
	//    specularColor/specularColorFactor/glossinessFactor/
	//    useSpecularGlossiness) - a THIRD, separate field set again
	//  - every misc scalar/bool not covered by any of the three sets above
	//    (multiScatterColor, hasVolumeScattering, isOpacityMapInverted,
	//    hasThicknessAlpha, the legacy global uvTiling/uvOffset floats)
	//  - channel packing for the 4 packable single-channel maps (metallic/
	//    roughness/ao/opacity), consumed by RenderableMesh.cpp when
	//    unpacking a grayscale map
	// Deliberately excluded: GPU texture-id handles (transient runtime
	// resources, not authored identity) and the "hasXMap" booleans (all pure
	// derivations of a path already covered by the texture loop, which now
	// reads the SAME live path source those booleans check).
	QString currentMaterialKey(const Material& mat)
	{
		QString key = mat.name() + QLatin1Char('|')
			// -- PBR/glTF field set --
			+ vec3Key(mat.albedoColor()) + QLatin1Char('|')
			+ f(mat.metalness()) + QLatin1Char('|')
			+ f(mat.roughness()) + QLatin1Char('|')
			+ f(mat.ior()) + QLatin1Char('|')
			+ f(mat.opacity()) + QLatin1Char('|')
			+ vec3Key(mat.emissive()) + QLatin1Char('|')
			+ f(mat.emissiveStrength()) + QLatin1Char('|')
			+ f(mat.clearcoat()) + QLatin1Char('|')
			+ f(mat.clearcoatRoughness()) + QLatin1Char('|')
			+ f(mat.clearcoatNormalScale()) + QLatin1Char('|')
			+ vec3Key(mat.sheenColor()) + QLatin1Char('|')
			+ f(mat.sheenRoughness()) + QLatin1Char('|')
			+ f(mat.transmission()) + QLatin1Char('|')
			+ f(mat.thicknessFactor()) + QLatin1Char('|')
			+ f(mat.attenuationDistance()) + QLatin1Char('|')
			+ vec3Key(mat.attenuationColor()) + QLatin1Char('|')
			+ f(mat.dispersion()) + QLatin1Char('|')
			+ f(mat.normalScale()) + QLatin1Char('|')
			+ f(mat.heightScale()) + QLatin1Char('|')
			+ f(mat.occlusionStrength()) + QLatin1Char('|')
			+ f(mat.anisotropyStrength()) + QLatin1Char('|')
			+ f(mat.anisotropyRotation()) + QLatin1Char('|')
			+ f(mat.diffuseTransmissionFactor()) + QLatin1Char('|')
			+ vec3Key(mat.diffuseTransmissionColorFactor()) + QLatin1Char('|')
			+ f(mat.specularFactor()) + QLatin1Char('|')
			+ vec3Key(mat.specularColorFactor()) + QLatin1Char('|')
			+ f(mat.iridescenceFactor()) + QLatin1Char('|')
			+ f(mat.iridescenceIor()) + QLatin1Char('|')
			+ f(mat.iridescenceThicknessMin()) + QLatin1Char('|')
			+ f(mat.iridescenceThicknessMax()) + QLatin1Char('|')
			+ f(mat.alphaThreshold()) + QLatin1Char('|')
			+ QString::number(static_cast<int>(mat.shadingModel())) + QLatin1Char('|')
			+ QString::number(static_cast<int>(mat.blendMode())) + QLatin1Char('|')
			+ b(mat.twoSided()) + QLatin1Char('|')
			+ b(mat.wireframe()) + QLatin1Char('|')
			+ b(mat.isUnlit()) + QLatin1Char('|')
			// -- Legacy Phong field set (separate storage from PBR above) --
			+ vec3Key(mat.ambient()) + QLatin1Char('|')
			+ vec3Key(mat.diffuse()) + QLatin1Char('|')
			+ vec3Key(mat.specular()) + QLatin1Char('|')
			+ f(mat.shininess()) + QLatin1Char('|')
			+ b(mat.metallic()) + QLatin1Char('|')
			// -- PBR Specular-Glossiness workflow field set (separate again) --
			+ vec3Key(mat.diffuseColor()) + QLatin1Char('|')
			+ vec3Key(mat.specularColor()) + QLatin1Char('|')
			+ f(mat.glossinessFactor()) + QLatin1Char('|')
			+ b(mat.getUseSpecularGlossiness()) + QLatin1Char('|')
			// -- Misc, not covered by any set above --
			+ vec3Key(mat.multiScatterColor()) + QLatin1Char('|')
			+ b(mat.hasVolumeScattering()) + QLatin1Char('|')
			+ b(mat.isOpacityMapInverted()) + QLatin1Char('|')
			+ b(mat.hasThicknessAlpha()) + QLatin1Char('|')
			+ f(mat.uvTilingU()) + QLatin1Char('|')
			+ f(mat.uvTilingV()) + QLatin1Char('|')
			+ f(mat.uvOffsetU()) + QLatin1Char('|')
			+ f(mat.uvOffsetV()) + QLatin1Char('|')
			// -- Channel packing (grayscale-map channel/invert/scale/bias) -
			// the only 4 keys packingFor() recognizes; the renderer reads
			// these when unpacking a single-channel map (RenderableMesh.cpp).
			+ packingKey(mat.packingFor(QStringLiteral("metallic"))) + QLatin1Char('|')
			+ packingKey(mat.packingFor(QStringLiteral("roughness"))) + QLatin1Char('|')
			+ packingKey(mat.packingFor(QStringLiteral("ao"))) + QLatin1Char('|')
			+ packingKey(mat.packingFor(QStringLiteral("opacity")));

		// Every texture slot: the LIVE bound image path (see
		// liveTexturePath()'s own doc comment for why NOT texture(type).path),
		// sampler settings (wrap/filter - confirmed live on the Texture
		// struct itself, per resolveMaterialTextures()'s own identical
		// usage), and the LIVE per-type UV transform via textureTransform(type)
		// - deliberately NOT texture(type)'s own transform fields, which
		// Material.h's own doc comment on textureTransform() warns are a
		// separate, never-animated copy that only reflects whatever was
		// baked in at import time.
		for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
		{
			const auto type = static_cast<Material::TextureType>(i);
			const Material::Texture& tex = mat.texture(type);
			const Material::TextureTransform xform = mat.textureTransform(type);

			key += QLatin1Char('|');
			key += liveTexturePath(mat, type);
			key += QLatin1Char(',') + QString::number(tex.wrapS);
			key += QLatin1Char(',') + QString::number(tex.wrapT);
			key += QLatin1Char(',') + QString::number(tex.magFilter);
			key += QLatin1Char(',') + QString::number(tex.minFilter);
			key += QLatin1Char(',') + QString::number(xform.texCoord);
			key += QLatin1Char(',') + vec2Key(xform.texScale);
			key += QLatin1Char(',') + vec2Key(xform.texOffset);
			key += QLatin1Char(',') + f(xform.texRotation);
		}

		return key;
	}
}

std::vector<std::vector<int>> groupIndicesByCurrentMaterial(const QVector<SceneMesh*>& meshes,
                                                              const std::vector<int>& indices)
{
	std::vector<std::vector<int>> groups;
	QHash<QString, int> groupIndexByKey;

	for (int idx : indices)
	{
		SceneMesh* mesh = meshes[idx];
		const QString key = currentMaterialKey(mesh->getMaterial());
		const auto it = groupIndexByKey.constFind(key);
		if (it == groupIndexByKey.cend())
		{
			groupIndexByKey.insert(key, static_cast<int>(groups.size()));
			groups.push_back({ idx });
		}
		else
		{
			groups[it.value()].push_back(idx);
		}
	}

	return groups;
}
