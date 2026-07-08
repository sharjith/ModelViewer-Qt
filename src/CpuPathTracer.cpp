#include "CpuPathTracer.h"
#include "RtEmbreeScene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace
{
	constexpr float kPi = 3.14159265358979323846f;

	// Debug-visualization toggles - moved here from CpuPathTracer::Settings
	// (see that struct's comment) specifically so flipping one only
	// rebuilds this file, not every translation unit that includes
	// CpuPathTracer.h. Flip manually for debugging, then set back to
	// false before committing - see each flag's original doc comment
	// (now attached to its use site in tracePixel()) for what it does.
	constexpr bool kDebugVisualizeUV = false;
	constexpr bool kDebugVisualizeTransmission = false;
	constexpr bool kDebugVisualizeTransmissionBounceCount = false;

	// xorshift32 - fast, small, good enough for Monte Carlo path tracing noise
	// (not for cryptography). Seeded per-pixel-per-pass so successive
	// renderPass() calls with different sampleSeed values are decorrelated.
	struct Rng
	{
		uint32_t state;
		explicit Rng(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
		uint32_t nextU32()
		{
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return state;
		}
		float next01() { return (nextU32() >> 8) * (1.0f / 16777216.0f); } // 24-bit -> [0,1)
	};

	uint32_t hashCombine(uint32_t a, uint32_t b)
	{
		// Standard boost-style hash combine.
		a ^= b + 0x9E3779B9u + (a << 6) + (a >> 2);
		return a;
	}

	// Duff/Burley "Building an Orthonormal Basis, Revisited" - branchless,
	// numerically stable for any unit n (including near the poles).
	void buildOrthonormalBasis(const glm::vec3& n, glm::vec3& t, glm::vec3& b)
	{
		const float sign = n.z >= 0.0f ? 1.0f : -1.0f;
		const float a = -1.0f / (sign + n.z);
		const float bb = n.x * n.y * a;
		t = glm::vec3(1.0f + sign * n.x * n.x * a, sign * bb, -sign * n.x);
		b = glm::vec3(bb, sign + n.y * n.y * a, -n.y);
	}

	glm::vec3 localToWorld(const glm::vec3& local, const glm::vec3& n, const glm::vec3& t, const glm::vec3& b)
	{
		return local.x * t + local.y * b + local.z * n;
	}

	glm::vec3 cosineSampleHemisphere(float u1, float u2)
	{
		const float r   = std::sqrt(u1);
		const float phi = 2.0f * kPi * u2;
		const float z   = std::sqrt(std::max(0.0f, 1.0f - u1));
		return glm::vec3(r * std::cos(phi), r * std::sin(phi), z);
	}

	// GGX Visible Normal Distribution Function (VNDF) importance sampling -
	// Heitz 2018, "Sampling the GGX Distribution of Visible Normals" (JCGT).
	// Ve is the view direction in local tangent space (N=+Z), must have
	// Ve.z > 0. Returns the sampled half-vector, also in local tangent space.
	// Lower variance than plain GGX half-vector sampling because it accounts
	// for microfacet visibility (RayTrophiStudio's closesthit.rchit uses the
	// same method for its metallic GGX lobe). The algorithm as published is
	// already anisotropic-capable (separate x/y roughness scaling) - alphaX
	// and alphaY are only ever equal for isotropic materials, letting
	// KHR_materials_anisotropy's stretched lobe reuse this same sampler
	// rather than needing its own.
	glm::vec3 sampleGGXVNDF(const glm::vec3& Ve, float alphaX, float alphaY, float u1, float u2)
	{
		const glm::vec3 Vh = glm::normalize(glm::vec3(alphaX * Ve.x, alphaY * Ve.y, Ve.z));

		const float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
		const glm::vec3 T1 = lensq > 0.0f
			? glm::vec3(-Vh.y, Vh.x, 0.0f) * (1.0f / std::sqrt(lensq))
			: glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 T2 = glm::cross(Vh, T1);

		const float r   = std::sqrt(u1);
		const float phi = 2.0f * kPi * u2;
		float t1 = r * std::cos(phi);
		float t2 = r * std::sin(phi);
		const float s = 0.5f * (1.0f + Vh.z);
		t2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - t1 * t1)) + s * t2;

		const glm::vec3 Nh = t1 * T1 + t2 * T2 + std::sqrt(std::max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

		return glm::normalize(glm::vec3(alphaX * Nh.x, alphaY * Nh.y, std::max(0.0f, Nh.z)));
	}

	// Height-correlated Smith masking-shadowing (Heitz 2014), the pair that
	// VNDF sampling's throughput simplification (F * G2/G1) requires - kept
	// deliberately separate from geometrySmith() above, which is ported
	// as-is from main_scene.frag for NEE's raster-parity direct-lighting
	// evaluation. Mixing the two would be inconsistent: the *value* of the
	// BRDF (NEE) should match the raster shader; the *sampling weight* (BSDF
	// importance sampling) needs to match whichever G was used to derive the
	// VNDF pdf for the algebra to cancel correctly.
	float smithLambdaGGX(float NdotX, float alpha)
	{
		const float NdotX2 = NdotX * NdotX;
		const float tan2 = std::max(0.0f, 1.0f - NdotX2) / std::max(NdotX2, 1e-7f);
		return 0.5f * (-1.0f + std::sqrt(1.0f + alpha * alpha * tan2));
	}

	float smithG1GGX(float NdotX, float alpha)
	{
		return 1.0f / (1.0f + smithLambdaGGX(NdotX, alpha));
	}

	// Anisotropic generalization of smithLambdaGGX() - Xlocal is the
	// direction expressed in the (anisotropicT, anisotropicB, N) local frame
	// (dot(X,T), dot(X,B), dot(X,N)); reduces to smithLambdaGGX(Xlocal.z,
	// alpha) exactly when alphaX == alphaY (isotropic case), consistent with
	// how sampleGGXVNDF() above unifies the two cases.
	float smithLambdaGGXAniso(const glm::vec3& Xlocal, float alphaX, float alphaY)
	{
		const float NdotX2 = Xlocal.z * Xlocal.z;
		const float ax2 = alphaX * alphaX, ay2 = alphaY * alphaY;
		const float tan2Num = ax2 * Xlocal.x * Xlocal.x + ay2 * Xlocal.y * Xlocal.y;
		return 0.5f * (-1.0f + std::sqrt(1.0f + tan2Num / std::max(NdotX2, 1e-7f)));
	}

	float smithG1GGXAniso(const glm::vec3& Xlocal, float alphaX, float alphaY)
	{
		return 1.0f / (1.0f + smithLambdaGGXAniso(Xlocal, alphaX, alphaY));
	}

	float smithG2HeightCorrelatedGGXAniso(const glm::vec3& Vlocal, const glm::vec3& Llocal, float alphaX, float alphaY)
	{
		return 1.0f / (1.0f + smithLambdaGGXAniso(Vlocal, alphaX, alphaY) + smithLambdaGGXAniso(Llocal, alphaX, alphaY));
	}

	float smithG2HeightCorrelatedGGX(float NdotV, float NdotL, float alpha)
	{
		return 1.0f / (1.0f + smithLambdaGGX(NdotV, alpha) + smithLambdaGGX(NdotL, alpha));
	}

	// A fixed world-space epsilon (e.g. 1e-3) is fragile across this app's
	// full size range (small parts through large assemblies): too coarse for
	// tiny models, not enough to escape precision loss on ones with large
	// coordinate values. Scale it to the magnitude of the hit position
	// instead - a standard, bounded robustness technique (fuller approaches
	// like pbrt's reprojected-error offsetting exist but are more machinery
	// than this v1 needs).
	float selfIntersectionEpsilon(const glm::vec3& position)
	{
		return std::max(1e-4f, glm::length(position) * 1e-5f);
	}

	// ---- Cook-Torrance terms, ported verbatim from main_scene.frag ---------
	// (distributionGGX / geometrySchlickGGX / geometrySmith / fresnelSchlick)
	// so path-traced and raster PBR shading agree on the same material.
	float distributionGGX(float NdotH, float roughness)
	{
		const float a    = roughness * roughness;
		const float a2   = a * a;
		const float NdotH2 = NdotH * NdotH;
		float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
		denom = kPi * denom * denom;
		return a2 / std::max(denom, 0.001f);
	}

	float geometrySchlickGGX(float NdotX, float roughness)
	{
		const float r = roughness + 1.0f;
		const float k = (r * r) / 8.0f;
		return NdotX / (NdotX * (1.0f - k) + k);
	}

	float geometrySmith(float NdotV, float NdotL, float roughness)
	{
		return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
	}

	glm::vec3 fresnelSchlick(float cosTheta, const glm::vec3& F0)
	{
		return F0 + (glm::vec3(1.0f) - F0) * std::pow(std::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
	}

	// 3-arg variant ported from main_scene.frag's own fresnelSchlick(cosTheta,
	// F0, F90) overload - lets grazing-angle reflectance (F90) differ from 1.0,
	// which KHR_materials_specular's specularFactor uses to scale dielectric
	// reflectance down uniformly (see computeF0F90() below).
	glm::vec3 fresnelSchlick(float cosTheta, const glm::vec3& F0, const glm::vec3& F90)
	{
		return F0 + (F90 - F0) * std::pow(std::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
	}

	// Ported from computeDielectricF0()/computeF90() in main_scene.frag:
	// KHR_materials_ior replaces the fixed 0.04 dielectric F0 with a value
	// derived from the material's actual index of refraction, and
	// KHR_materials_specular further scales/tints it (with no effect on
	// metals - specular only affects the dielectric term before the
	// metal/dielectric F0 mix). Takes the already-textured baseColor/
	// metalness (SurfaceParams, post texture sampling) rather than the raw
	// RtMaterial factors, matching how main_scene.frag uses params.baseColor/
	// params.metallic (post-texture) for this same mix, not the flat factors.
	// texturedSpecularFactor/texturedSpecularColorFactor are the material
	// factors already multiplied by specularFactorMap's alpha / specularColorMap's
	// (sRGB-decoded) RGB where present (see evaluateSurface()) - mirrors
	// main_scene.frag's params.specularFactor/params.specularColor, which are
	// likewise texture-modulated before this same F0/F90 computation.
	void computeF0F90(const RtMaterial& mat, const glm::vec3& texturedBaseColor, float texturedMetalness,
		float texturedSpecularFactor, const glm::vec3& texturedSpecularColorFactor,
		glm::vec3& outF0, glm::vec3& outF90, glm::vec3& outDirectF0, glm::vec3& outDielectricF0, glm::vec3& outDielectricDirectF0)
	{
		const float f0FromIor = std::pow((mat.ior - 1.0f) / (mat.ior + 1.0f), 2.0f);
		glm::vec3 dielectricF0(f0FromIor);
		if (texturedSpecularFactor > 0.0f)
			dielectricF0 *= texturedSpecularColorFactor;
		dielectricF0 = glm::clamp(dielectricF0, glm::vec3(0.0f), glm::vec3(1.0f));
		outDielectricF0 = dielectricF0; // pre-metal-mix, WITHOUT the extra specularFactor multiply below - main_scene.frag's params.dielectricF0, consumed standalone by KHR_materials_iridescence's evalIridescence() calls and clearcoat's clearcoatF0Scalar-adjacent logic.

		outF0 = glm::mix(dielectricF0, texturedBaseColor, texturedMetalness);
		outF90 = glm::mix(glm::vec3(texturedSpecularFactor), glm::vec3(1.0f), texturedMetalness);

		// Direct-lighting (punctual light) F0 additionally scales the
		// dielectric term by specularFactor a second time - see
		// main_scene.frag's "dielectricDirectF0 = params.dielectricF0 *
		// params.specularFactor" at its direct-light BRDF call site, distinct
		// from the general F0 above (used for IBL/indirect bounces).
		// outDielectricDirectF0 is that quantity BEFORE the metal/dielectric
		// mix - main_scene.frag's KHR_materials_iridescence direct-lighting
		// branch (evaluateBaseDirect()) needs the pure dielectric term
		// standalone, separately from the already-metal-mixed outDirectF0.
		outDielectricDirectF0 = dielectricF0 * texturedSpecularFactor;
		outDirectF0 = glm::mix(outDielectricDirectF0, texturedBaseColor, texturedMetalness);
	}

	// Ported from decodeAnisotropyTexture() in main_scene.frag. Without a
	// texture this is a no-op (returns the raw uniform factors unchanged) -
	// see evaluateSurface(), which only bothers calling this when a texture
	// is actually present. With a texture, the RG channels ([0,1] -> [-1,1])
	// give a base direction that the uniform rotation then rotates further,
	// reduced here to a single final angle (outRotation) since that's all
	// buildAnisotropyBasis()-equivalent code needs downstream.
	void decodeAnisotropyTexture(const glm::vec3& texelRGB, float uniformStrength, float uniformRotation,
		float& outStrength, float& outRotation)
	{
		glm::vec2 direction = glm::vec2(texelRGB.x, texelRGB.y) * 2.0f - 1.0f;
		const float directionLength = glm::length(direction);
		direction = (directionLength < 0.0001f) ? glm::vec2(1.0f, 0.0f) : (direction / directionLength);

		outStrength = std::clamp(texelRGB.z * uniformStrength, 0.0f, 1.0f);

		const float c = std::cos(uniformRotation);
		const float s = std::sin(uniformRotation);
		const glm::vec2 rotated(c * direction.x - s * direction.y, s * direction.x + c * direction.y);
		outRotation = std::atan2(rotated.y, rotated.x);
	}

	// Applies the texture's actual declared wrap mode instead of assuming
	// GL_REPEAT unconditionally - a texture authored as a single centered
	// decal (CLAMP_TO_EDGE) tiled repeatedly across the whole surface when
	// wrap mode was ignored (a real, previously-unhandled gap).
	float applyWrap(float coord, unsigned int mode)
	{
		constexpr unsigned int kClampToEdge    = 0x812Fu;
		constexpr unsigned int kMirroredRepeat = 0x8370u;

		if (mode == kClampToEdge)
			return std::clamp(coord, 0.0f, 1.0f);

		if (mode == kMirroredRepeat)
		{
			const float t = std::fmod(std::abs(coord), 2.0f);
			return (t > 1.0f) ? (2.0f - t) : t;
		}

		// GL_REPEAT (0x2901) and anything unrecognized.
		return coord - std::floor(coord);
	}

	// Ported verbatim from sRGBToLinear() in main_scene.frag. Only baseColor
	// and emissive textures are sRGB-encoded per glTF convention (metallic/
	// roughness/normal maps are linear data already) - material *factors*
	// (baseColorFactor/emissiveFactor) are likewise already linear per spec,
	// only sampled texture bytes need this decode.
	glm::vec3 sRGBToLinear(const glm::vec3& c)
	{
		glm::vec3 result;
		for (int i = 0; i < 3; ++i)
			result[i] = (c[i] <= 0.04045f) ? (c[i] / 12.92f) : std::pow((c[i] + 0.055f) / 1.055f, 2.4f);
		return result;
	}

	// ---- Texture sampling (nearest-neighbour) --------------------------------
	// Deliberately replicates main_scene.frag's getTransformedUV() pipeline
	// exactly, step for step. main_scene.frag's getTransformedUV() applies a
	// single explicit "uv.y = 1 - uv.y" BEFORE scale/rotate/offset (see its
	// comment: compensating for glTF's image-space, top-origin UV convention),
	// then hands the result to texture(). This code reads the raw pixel array
	// directly instead of going through a GL sampler, so there is no second,
	// separate "GL-side" flip to additionally account for - doing so double-
	// counts the flip (a net zero-flip for identity-transform materials,
	// which is why an earlier version of this fix silently changed nothing:
	// it reduced to the exact same formula as not flipping at all). Exactly
	// one flip, matching the shader, is correct.
	glm::vec4 sampleTexture(const RtTextureSample& tex, const glm::vec2 (&texCoords)[4])
	{
		if (tex.width <= 0 || tex.height <= 0 || tex.rgba8.empty())
			return glm::vec4(1.0f);

		// A texture's KHR-declared texCoordIndex can reference any of the
		// mesh's 4 UV channels - hardcoding channel 0 for every texture
		// silently samples the wrong UV set whenever a material uses a
		// non-zero channel (this was a real, previously-unnoticed bug).
		const glm::vec2 rawUv = texCoords[std::clamp(tex.texCoordIndex, 0, 3)];

		// Shader's single explicit pre-transform UV.y flip.
		glm::vec2 uv(rawUv.x, 1.0f - rawUv.y);

		// KHR_texture_transform order: scale, then rotate, then offset.
		glm::vec2 st = uv * tex.uvScale;
		if (tex.uvRotation != 0.0f)
		{
			const float c = std::cos(tex.uvRotation);
			const float s = std::sin(tex.uvRotation);
			st = glm::vec2(st.x * c + st.y * s, -st.x * s + st.y * c);
		}
		st += tex.uvOffset;

		// Wrap per-axis using the texture's actual declared wrapS/wrapT.
		st.x = applyWrap(st.x, tex.wrapS);
		st.y = applyWrap(st.y, tex.wrapT);

		// st is now in the same space the shader hands to texture() - read
		// the array directly, row 0 = st.y = 0, no further inversion.
		int x = std::clamp(static_cast<int>(st.x * tex.width), 0, tex.width - 1);
		int y = std::clamp(static_cast<int>(st.y * tex.height), 0, tex.height - 1);

		const size_t idx = (static_cast<size_t>(y) * tex.width + x) * 4;
		return glm::vec4(
			tex.rgba8[idx + 0] / 255.0f,
			tex.rgba8[idx + 1] / 255.0f,
			tex.rgba8[idx + 2] / 255.0f,
			tex.rgba8[idx + 3] / 255.0f);
	}

	float applyChannelPacking(const glm::vec4& rgba, const RtTextureSample& tex)
	{
		float v;
		switch (tex.packingChannel)
		{
			case 0:  v = rgba.r; break;
			case 1:  v = rgba.g; break;
			case 2:  v = rgba.b; break;
			case 3:  v = rgba.a; break;
			default: v = rgba.r; break;
		}
		if (tex.packingInvert) v = 1.0f - v;
		return v * tex.packingScale + tex.packingBias;
	}

	// Resolved, texture-evaluated surface parameters at a hit point.
	struct SurfaceParams
	{
		glm::vec3 baseColor;
		float     metalness;
		float     roughness;
		glm::vec3 emissive;

		// KHR_materials_pbrSpecularGlossiness - see evaluateSurface() for
		// where baseColor/metalness/roughness/F0/F90 above get overridden
		// to this workflow's meaning, and evaluateDirectBRDF() for the
		// mix()-based (not additive) direct-lighting formula this flag
		// switches to, matching main_scene.frag's useSpecGloss branch.
		bool useSpecGloss = false;

		// Ambient occlusion - only ever multiplied into indirect/environment
		// contributions (see main_scene.frag: applied to diffuseIBLOut/
		// specularIBLOut/envColor, never to direct-light terms), not general
		// surface darkening. See where this is consumed in tracePixel().
		float ao = 1.0f;

		// Fresnel reflectance at normal incidence (F0) and at grazing angle
		// (F90) - see computeF0F90(). Computed once here (using the already-
		// textured baseColor/metalness above) rather than recomputed with a
		// hardcoded 0.04 dielectric constant at every Fresnel call site.
		glm::vec3 F0  = glm::vec3(0.04f);
		glm::vec3 F90 = glm::vec3(1.0f);

		// Direct-lighting (punctual light) variant of F0 - see computeF0F90().
		glm::vec3 directF0 = glm::vec3(0.04f);

		// Pre-metal-mix, general and direct-light-specific dielectric F0 -
		// see computeF0F90()'s outDielectricF0/outDielectricDirectF0
		// comments. Only consumed by the KHR_materials_iridescence branches.
		glm::vec3 dielectricF0       = glm::vec3(0.04f);
		glm::vec3 dielectricDirectF0 = glm::vec3(0.04f);

		// Textured specularFactor scalar (KHR_materials_specular), stored
		// standalone because the iridescence direct-lighting branch needs it
		// raw (as F90 for its own dielectric_fresnel), separate from surf.F90
		// (which is already mixed with metalness).
		float specularFactor = 1.0f;

		// KHR_materials_clearcoat - see evaluateClearcoatDirect()/tracePixel()'s
		// clearcoat-lobe handling for how these are consumed.
		float clearcoat          = 0.0f;
		float clearcoatRoughness = 0.0001f;

		// KHR_materials_sheen - see calculateSheen(). (0,0,0) means no sheen.
		glm::vec3 sheenColor     = glm::vec3(0.0f);
		float     sheenRoughness = 0.0001f;

		// KHR_materials_anisotropy - anisotropyRotation is already the final
		// combined angle (texture direction rotated by the uniform rotation,
		// or just the uniform rotation when untextured) - see
		// decodeAnisotropyTexture().
		float anisotropyStrength = 0.0f;
		float anisotropyRotation = 0.0f;

		// KHR_materials_iridescence - see evalIridescence(). iridescenceFactor
		// <= 0.001 means "no iridescence", matching main_scene.frag's own gate.
		float iridescenceFactor    = 0.0f;
		float iridescenceIor       = 1.3f;
		float iridescenceThickness = 400.0f;

		// KHR_materials_transmission + KHR_materials_volume - see tracePixel()'s
		// transmission handling and RtSceneSnapshot.h's comment on why the
		// authored thicknessFactor approximation is skipped in favor of a
		// real traced entry-to-exit distance (hasVolume's mere PRESENCE is
		// still used, as a thin-walled-vs-solid gate).
		float     transmission        = 0.0f;
		bool      hasVolume           = false;
		glm::vec3 attenuationColor    = glm::vec3(1.0f);
		float     attenuationDistance = std::numeric_limits<float>::infinity();
		float     dispersion          = 0.0f;

		// KHR_materials_diffuse_transmission - see tracePixel()'s NEE and
		// diffuse-lobe handling.
		float     diffuseTransmissionFactor = 0.0f;
		glm::vec3 diffuseTransmissionColor  = glm::vec3(1.0f);
	};

	// vertexColor is the interpolated COLOR_0 attribute (or the (1,1,1,1)
	// identity when the mesh has none - see RtSceneBuilder's hasVertexColors
	// gating), applied last and RGB-only, exactly matching computeBaseColor()
	// in main_scene.frag ("Apply vertex color last (in linear)" - COLOR_0 is
	// already linear per glTF spec, unlike textures, so no sRGB decode here).
	SurfaceParams evaluateSurface(const RtMaterial& mat, const glm::vec2 (&texCoords)[4], const glm::vec4& vertexColor)
	{
		SurfaceParams s;
		s.baseColor = mat.baseColor;
		if (mat.baseColorTexture)
		{
			const glm::vec4 t = sampleTexture(*mat.baseColorTexture, texCoords);
			s.baseColor *= sRGBToLinear(glm::vec3(t));
		}
		s.baseColor *= glm::vec3(vertexColor);

		s.metalness = mat.metalness;
		if (mat.metallicTexture)
			s.metalness *= applyChannelPacking(sampleTexture(*mat.metallicTexture, texCoords), *mat.metallicTexture);

		s.roughness = mat.roughness;
		if (mat.roughnessTexture)
			s.roughness *= applyChannelPacking(sampleTexture(*mat.roughnessTexture, texCoords), *mat.roughnessTexture);
		s.roughness = std::clamp(s.roughness, 0.03f, 1.0f); // avoid a singular perfect mirror (alpha=0)

		s.emissive = mat.emissive * mat.emissiveStrength;
		if (mat.emissiveTexture)
			s.emissive *= sRGBToLinear(glm::vec3(sampleTexture(*mat.emissiveTexture, texCoords)));

		// Ported verbatim from main_scene.frag: "clamp(mix(1.0, texAO,
		// occlusionStrength), 0.0001, 1.0)".
		s.ao = 1.0f;
		if (mat.aoTexture)
		{
			const float texAO = applyChannelPacking(sampleTexture(*mat.aoTexture, texCoords), *mat.aoTexture);
			s.ao = std::clamp(glm::mix(1.0f, texAO, mat.occlusionStrength), 0.0001f, 1.0f);
		}

		float texturedSpecularFactor = mat.specularFactor;
		if (mat.specularTexture)
			texturedSpecularFactor *= applyChannelPacking(sampleTexture(*mat.specularTexture, texCoords), *mat.specularTexture);

		glm::vec3 texturedSpecularColorFactor = mat.specularColorFactor;
		if (mat.specularColorTexture)
			texturedSpecularColorFactor *= sRGBToLinear(glm::vec3(sampleTexture(*mat.specularColorTexture, texCoords)));

		computeF0F90(mat, s.baseColor, s.metalness, texturedSpecularFactor, texturedSpecularColorFactor, s.F0, s.F90, s.directF0, s.dielectricF0, s.dielectricDirectF0);
		s.specularFactor = texturedSpecularFactor;

		// KHR_materials_pbrSpecularGlossiness - legacy alternate workflow;
		// completely REPLACES the metallic-roughness values just computed
		// above (matching main_scene.frag's gatherMaterialParams(), which
		// does the equivalent override in its own useSpecGloss branch).
		// baseColor becomes diffuseColor, F0 becomes the specular color
		// directly (an authored RGB reflectance, not IOR-derived), F90 is
		// forced to 1.0, metalness is forced to 0 (spec-gloss has no
		// metalness concept), and roughness is derived from glossiness's
		// inverse. dielectricF0/dielectricDirectF0 are also overridden so
		// KHR_materials_iridescence (if combined with spec-gloss) still
		// gets a sensible base reflectance to work from.
		s.useSpecGloss = mat.useSpecGloss;
		if (mat.useSpecGloss)
		{
			s.baseColor = mat.diffuseColor;
			if (mat.diffuseTexture)
			{
				const glm::vec4 t = sampleTexture(*mat.diffuseTexture, texCoords);
				s.baseColor *= sRGBToLinear(glm::vec3(t));
			}
			s.baseColor *= glm::vec3(vertexColor);

			glm::vec3 specGlossColor = mat.specGlossSpecularColor;
			float glossiness = mat.glossinessFactor;
			if (mat.specularGlossinessTexture)
			{
				const glm::vec4 packed = sampleTexture(*mat.specularGlossinessTexture, texCoords);
				specGlossColor *= sRGBToLinear(glm::vec3(packed));
				glossiness *= packed.a;
			}
			specGlossColor = glm::clamp(specGlossColor, glm::vec3(0.0f), glm::vec3(1.0f));

			s.roughness = std::clamp(1.0f - glossiness, 0.03f, 1.0f);
			s.metalness = 0.0f;
			s.F0 = specGlossColor;
			s.F90 = glm::vec3(1.0f);
			s.directF0 = specGlossColor;
			s.dielectricF0 = specGlossColor;
			s.dielectricDirectF0 = specGlossColor;
			s.specularFactor = 1.0f;
		}

		s.clearcoat = mat.clearcoat;
		if (mat.clearcoatTexture)
			s.clearcoat *= applyChannelPacking(sampleTexture(*mat.clearcoatTexture, texCoords), *mat.clearcoatTexture);
		s.clearcoat = std::clamp(s.clearcoat, 0.0f, 1.0f);

		s.clearcoatRoughness = mat.clearcoatRoughness;
		if (mat.clearcoatRoughnessTexture)
			s.clearcoatRoughness *= applyChannelPacking(sampleTexture(*mat.clearcoatRoughnessTexture, texCoords), *mat.clearcoatRoughnessTexture);
		s.clearcoatRoughness = std::clamp(s.clearcoatRoughness, 0.0001f, 1.0f);

		s.sheenColor = mat.sheenColor;
		if (mat.sheenColorTexture)
			s.sheenColor *= sRGBToLinear(glm::vec3(sampleTexture(*mat.sheenColorTexture, texCoords)));
		s.sheenColor = glm::clamp(s.sheenColor, glm::vec3(0.0f), glm::vec3(1.0f));

		s.sheenRoughness = mat.sheenRoughness;
		if (mat.sheenRoughnessTexture)
			s.sheenRoughness *= applyChannelPacking(sampleTexture(*mat.sheenRoughnessTexture, texCoords), *mat.sheenRoughnessTexture);
		s.sheenRoughness = std::clamp(s.sheenRoughness, 0.0001f, 1.0f);

		s.anisotropyStrength = mat.anisotropyStrength;
		s.anisotropyRotation = mat.anisotropyRotation;
		if (mat.anisotropyTexture)
		{
			const glm::vec3 texel(sampleTexture(*mat.anisotropyTexture, texCoords));
			decodeAnisotropyTexture(texel, mat.anisotropyStrength, mat.anisotropyRotation, s.anisotropyStrength, s.anisotropyRotation);
		}

		s.iridescenceFactor = mat.iridescenceFactor;
		if (mat.iridescenceTexture)
			s.iridescenceFactor *= applyChannelPacking(sampleTexture(*mat.iridescenceTexture, texCoords), *mat.iridescenceTexture);

		s.iridescenceIor = mat.iridescenceIor;

		// Ported from main_scene.frag: defaults to iridescenceThicknessMax
		// (not min!) when untextured, matching its own params.
		// iridescenceThickness = pbrLighting.iridescenceThicknessMax default.
		s.iridescenceThickness = mat.iridescenceThicknessMax;
		if (mat.iridescenceThicknessTexture)
			s.iridescenceThickness = applyChannelPacking(sampleTexture(*mat.iridescenceThicknessTexture, texCoords), *mat.iridescenceThicknessTexture);

		s.transmission = mat.transmission;
		if (mat.transmissionTexture)
			s.transmission *= applyChannelPacking(sampleTexture(*mat.transmissionTexture, texCoords), *mat.transmissionTexture);
		s.hasVolume           = mat.hasVolume;
		s.attenuationColor    = mat.attenuationColor;
		s.attenuationDistance = mat.attenuationDistance;
		s.dispersion          = mat.dispersion;

		s.diffuseTransmissionFactor = mat.diffuseTransmissionFactor;
		if (mat.diffuseTransmissionTexture)
			s.diffuseTransmissionFactor *= applyChannelPacking(sampleTexture(*mat.diffuseTransmissionTexture, texCoords), *mat.diffuseTransmissionTexture);
		s.diffuseTransmissionColor = mat.diffuseTransmissionColor;
		if (mat.diffuseTransmissionColorTexture)
			s.diffuseTransmissionColor *= sRGBToLinear(glm::vec3(sampleTexture(*mat.diffuseTransmissionColorTexture, texCoords)));

		return s;
	}

	// Ported from calcBumpedNormal() in main_scene.frag, minus its screen-
	// space-derivative fallback (dFdx/dFdy have no equivalent per-ray in a
	// path tracer) - when the mesh has no tangent data, this just returns N
	// unchanged rather than attempting a derivative-based tangent frame.
	glm::vec3 applyNormalMap(const glm::vec3& N, const glm::vec3& rawTangent, const glm::vec3& rawBitangent,
		const RtTextureSample* normalTex, const glm::vec2 (&texCoords)[4])
	{
		if (!normalTex)
			return N;
		if (glm::length(rawTangent) <= 0.01f)
			return N; // no tangent data (matches the shader's own hasTangents check)

		const glm::vec3 T = glm::normalize(rawTangent - glm::dot(rawTangent, N) * N);

		// Matches main_scene.frag's buildSurfaceFrame() exactly: the final
		// bitangent is NOT the imported rawBitangent's own (orthogonalized)
		// direction - it's reconstructed as a strictly perpendicular
		// cross(N, T), with the imported bitangent used only for its SIGN
		// (a handedness flip). Using the imported bitangent's actual
		// direction instead (an earlier version of this code did) can be
		// subtly non-orthogonal to cross(N,T) on curved/smoothed surfaces
		// (the vertex tangent/bitangent attributes aren't guaranteed exactly
		// orthogonal to a smoothed shading normal), skewing the TBN basis -
		// invisible on rough materials, but visibly warping normal-mapped
		// detail on a near-mirror surface (e.g. an embossed logo decal on a
		// highly reflective/iridescent sphere looking distorted/gapped).
		float handedness = 1.0f;
		if (glm::length(rawBitangent) > 0.01f)
		{
			const glm::vec3 importedBitangent = glm::normalize(rawBitangent - glm::dot(rawBitangent, N) * N);
			handedness = glm::sign(glm::dot(glm::cross(N, T), importedBitangent));
			if (handedness == 0.0f)
				handedness = 1.0f;
		}
		const glm::vec3 B = glm::normalize(glm::cross(N, T)) * handedness;

		const glm::vec4 sampled = sampleTexture(*normalTex, texCoords);
		const glm::vec3 tangentNormal = glm::vec3(sampled) * 2.0f - 1.0f;

		const glm::mat3 TBN(T, B, N);
		return glm::normalize(TBN * tangentNormal);
	}

	// Standard OpenGL cubemap direction->face+(s,t) convention (see e.g. the
	// OpenGL wiki's Cubemap_Texture page) - the same convention the GPU's own
	// samplerCube applies, so this reproduces exactly what the raster
	// skybox/reflections show for any given direction, regardless of how the
	// cubemap was populated (single equirect HDR, 6 face images, ...).
	void selectCubemapFaceUV(const glm::vec3& dir, int& face, float& u, float& v)
	{
		const float ax = std::abs(dir.x), ay = std::abs(dir.y), az = std::abs(dir.z);
		float sc, tc, ma;
		if (ax >= ay && ax >= az)
		{
			ma = ax;
			if (dir.x > 0.0f) { face = 0; sc = -dir.z; tc = -dir.y; } // +X
			else              { face = 1; sc =  dir.z; tc = -dir.y; } // -X
		}
		else if (ay >= ax && ay >= az)
		{
			ma = ay;
			if (dir.y > 0.0f) { face = 2; sc = dir.x; tc =  dir.z; } // +Y
			else              { face = 3; sc = dir.x; tc = -dir.z; } // -Y
		}
		else
		{
			ma = az;
			if (dir.z > 0.0f) { face = 4; sc =  dir.x; tc = -dir.y; } // +Z
			else              { face = 5; sc = -dir.x; tc = -dir.y; } // -Z
		}
		u = (sc / ma + 1.0f) * 0.5f;
		v = (tc / ma + 1.0f) * 0.5f;
	}

	glm::vec3 undoSkyboxRotation(const glm::vec3& direction, bool cameraUpAxisZUp, float skyBoxZRotationDegrees);

	glm::vec3 sampleCubemapFaces(const std::vector<float> faces[6], int size, const glm::vec3& direction)
	{
		int face;
		float u, v;
		selectCubemapFaceUV(glm::normalize(direction), face, u, v);

		const std::vector<float>& faceData = faces[face];
		if (faceData.size() != static_cast<size_t>(size) * size * 3)
			return glm::vec3(0.0f);

		const float fx = std::clamp(u * static_cast<float>(size) - 0.5f, 0.0f, static_cast<float>(size - 1));
		const float fy = std::clamp(v * static_cast<float>(size) - 0.5f, 0.0f, static_cast<float>(size - 1));

		const int x0 = static_cast<int>(fx);
		const int y0 = static_cast<int>(fy);
		const int x1 = std::min(x0 + 1, size - 1);
		const int y1 = std::min(y0 + 1, size - 1);
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		auto texel = [&faceData, size](int x, int y) -> glm::vec3
		{
			const size_t idx = (static_cast<size_t>(y) * size + x) * 3;
			return glm::vec3(faceData[idx], faceData[idx + 1], faceData[idx + 2]);
		};

		const glm::vec3 c00 = texel(x0, y0);
		const glm::vec3 c10 = texel(x1, y0);
		const glm::vec3 c01 = texel(x0, y1);
		const glm::vec3 c11 = texel(x1, y1);

		const glm::vec3 top    = glm::mix(c00, c10, tx);
		const glm::vec3 bottom = glm::mix(c01, c11, tx);
		return glm::mix(top, bottom, ty);
	}

	glm::vec3 flatGradientMiss(const glm::vec3& direction)
	{
		const float t = std::clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
		const glm::vec3 horizon(0.35f, 0.38f, 0.42f);
		const glm::vec3 zenith(0.10f, 0.14f, 0.22f);
		return glm::mix(horizon, zenith, t);
	}

	// Indirect/reflection bounces (bounce > 0): sharp (mip 0) cubemap lookup,
	// used regardless of whether the background sphere is visually toggled
	// on - mirror-like surfaces should still reflect the loaded environment.
	// Falls back to a flat two-tone gradient when no environment map is
	// loaded (environment.faceSize == 0).
	glm::vec3 sampleEnvironmentMiss(const RtEnvironment& environment, const glm::vec3& direction)
	{
		if (environment.faceSize <= 0)
			return flatGradientMiss(direction);
		// See undoSkyboxRotation()'s derivation below (used identically by
		// sampleEnvironmentBackground() for the directly-visible backdrop) -
		// the captured cubemap is stored in the skybox's rotated local space,
		// not world space, so ANY sample of it - direct or, as here, via a
		// reflection/refraction bounce - needs this same correction. Missing
		// it here (an earlier version of this code sampled with the raw,
		// un-rotated world direction) meant every reflected/refracted ray's
		// escape to the environment landed on the wrong part of the cubemap
		// - e.g. a transmissive surface's upward-facing regions incorrectly
		// showing the horizon/ground portion of the map instead of the sky
		// above, since the two are swapped by the skybox's un-undone
		// rotation.
		const glm::vec3 sampleDir = undoSkyboxRotation(direction, environment.cameraUpAxisZUp, environment.skyBoxZRotationDegrees);
		return sampleCubemapFaces(environment.faces, environment.faceSize, sampleDir);
	}

	// Primary-ray miss (bounce == 0, i.e. what the camera directly sees as
	// background): honors RtEnvironment::showBackground (mirrors the
	// Visualization panel's "Sky Box" checkbox - turning it off shows
	// raster's plain background gradient instead, matching what PBR mode
	// shows with the skybox disabled). When shown, samples the sharp captured
	// cubemap. Background pixels are restored from the raw accumulation after
	// denoising (see RtPathTracingSession::publishLatest()), so feeding the
	// camera a pre-blurred/downsampled skybox only makes the visible skybox
	// look like an irradiance map.
	// Undoes ViewportWidget::drawSkyBox()'s cube rotation - see
	// RtEnvironment::cameraUpAxisZUp/skyBoxZRotationDegrees for the full
	// derivation. drawSkyBox() composes, in order:
	//   model = Rot(upAxisConvention) * Rot(90,X) [raw env map] * Rot(skyBoxZRotation,Y)
	// and skybox.frag samples using the cube's *unrotated local* position, so
	// the world direction the camera looks along maps to cubemap sample
	// direction = inverse(model) * direction = R3^-1 * R2^-1 * R1^-1 * direction.
	glm::vec3 undoSkyboxRotation(const glm::vec3& direction, bool cameraUpAxisZUp, float skyBoxZRotationDegrees)
	{
		glm::vec4 v(direction, 0.0f);
		// R1^-1: undo up-axis convention rotation (identity if Z-up, else the
		// inverse of Rot(-90,X), i.e. Rot(+90,X)).
		if (!cameraUpAxisZUp)
			v = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * v;
		// R2^-1: undo the fixed "Z-up correction for raw env map" (Rot(90,X)).
		v = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * v;
		// R3^-1: undo the user-controlled skybox Z rotation.
		v = glm::rotate(glm::mat4(1.0f), glm::radians(-skyBoxZRotationDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) * v;
		return glm::vec3(v);
	}

	glm::vec3 sampleFallbackBackgroundGradient(const RtEnvironment& environment, const glm::vec2& screenUv)
	{
		const float u = std::clamp(screenUv.x, 0.0f, 1.0f);
		const float v = std::clamp(screenUv.y, 0.0f, 1.0f);
		float factor = v;

		if (environment.fallbackGradientStyle == 1)
			return glm::mix(environment.fallbackTopColor, environment.fallbackBottomColor, u);
		if (environment.fallbackGradientStyle == 2)
			factor = (u + (1.0f - v)) * 0.5f;
		else if (environment.fallbackGradientStyle == 3)
			factor = ((1.0f - u) + (1.0f - v)) * 0.5f;

		return glm::mix(environment.fallbackBottomColor, environment.fallbackTopColor, factor);
	}

	glm::vec3 sampleEnvironmentBackground(const RtEnvironment& environment, const glm::vec3& direction, const glm::vec2& screenUv)
	{
		if (!environment.showBackground)
			return sampleFallbackBackgroundGradient(environment, screenUv);

		const glm::vec3 sampleDir = undoSkyboxRotation(direction, environment.cameraUpAxisZUp, environment.skyBoxZRotationDegrees);

		return environment.faceSize > 0
			? sampleCubemapFaces(environment.faces, environment.faceSize, sampleDir)
			: flatGradientMiss(direction);
	}

	// Ported verbatim from evaluatePunctualLight() in main_scene.frag so
	// direct lighting matches the raster pass exactly.
	void evaluatePunctualLight(const RtLight& light, const glm::vec3& surfacePos,
		glm::vec3& outDir, glm::vec3& outIntensity, float& outDistance)
	{
		outDir = glm::vec3(0.0f);
		outIntensity = glm::vec3(0.0f);
		outDistance = std::numeric_limits<float>::max();

		if (light.type == 0) // Directional
		{
			outDir = -glm::normalize(light.direction);
			outIntensity = light.intensity * light.color;
			return;
		}

		const glm::vec3 pointToLight = light.position - surfacePos;
		const float distance = glm::length(pointToLight);
		if (distance <= 1e-6f)
			return;

		outDir = pointToLight / distance;
		outDistance = distance;

		float rangeAttenuation = 1.0f / (distance * distance);
		if (light.range > 0.0f)
		{
			const float distAttenuation = 1.0f - std::pow(distance / light.range, 4.0f);
			rangeAttenuation = std::clamp(distAttenuation, 0.0f, 1.0f) / (distance * distance);
		}

		float spotAttenuation = 1.0f;
		if (light.type == 2) // Spot
		{
			const glm::vec3 lightDirWorld = glm::normalize(light.direction);
			const float actualCos = glm::dot(lightDirWorld, glm::normalize(-pointToLight));
			if (actualCos > light.outerConeCos)
			{
				if (actualCos < light.innerConeCos)
				{
					const float angularAtten = (actualCos - light.outerConeCos) / (light.innerConeCos - light.outerConeCos);
					spotAttenuation = angularAtten * angularAtten;
				}
			}
			else
			{
				spotAttenuation = 0.0f;
			}
		}

		outIntensity = rangeAttenuation * spotAttenuation * light.intensity * light.color;
	}

	// Cook-Torrance direct-lighting contribution for one light sample.
	// KHR_materials_anisotropy, ported verbatim from D_GGX_anisotropic()/
	// V_GGX_anisotropic() in main_scene.frag. V_GGX_anisotropic already bakes
	// in the 1/(4*NdotV*NdotL) visibility term (Khronos spec's "V" function),
	// unlike the isotropic geometrySmith()/distributionGGX() pair which needs
	// that division applied separately at the call site.
	float distributionGGXAnisotropic(float NdotH, float TdotH, float BdotH, float at, float ab)
	{
		const float a2 = at * ab;
		const glm::vec3 f(ab * TdotH, at * BdotH, a2 * NdotH);
		const float w2 = a2 / glm::dot(f, f);
		return a2 * w2 * w2 / kPi;
	}

	float visibilityGGXAnisotropic(float NdotL, float NdotV, float BdotV, float TdotV, float TdotL, float BdotL, float at, float ab)
	{
		const float GGXV = NdotL * glm::length(glm::vec3(at * TdotV, ab * BdotV, NdotV));
		const float GGXL = NdotV * glm::length(glm::vec3(at * TdotL, ab * BdotL, NdotL));
		return std::clamp(0.5f / (GGXV + GGXL), 0.0f, 1.0f);
	}

	// ---- KHR_materials_iridescence, ported verbatim from main_scene.frag --

	inline float sqf(float a) { return a * a; }
	inline glm::vec3 sqf(const glm::vec3& a) { return a * a; }

	glm::vec3 fresnel0ToIor(const glm::vec3& fresnel0)
	{
		const glm::vec3 sqrtF0 = glm::sqrt(fresnel0);
		return (glm::vec3(1.0f) + sqrtF0) / (glm::vec3(1.0f) - sqrtF0);
	}

	glm::vec3 iorToFresnel0(const glm::vec3& transmittedIor, float incidentIor)
	{
		return sqf((transmittedIor - glm::vec3(incidentIor)) / (transmittedIor + glm::vec3(incidentIor)));
	}

	float iorToFresnel0(float transmittedIor, float incidentIor)
	{
		return sqf((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
	}

	float fSchlickIridescence(float f0, float cosTheta, float f90 = 1.0f)
	{
		return f0 + (f90 - f0) * std::pow(std::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
	}

	glm::vec3 fSchlickIridescence(const glm::vec3& f0, float cosTheta, const glm::vec3& f90)
	{
		return f0 + (f90 - f0) * std::pow(std::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
	}

	// XYZ color-matching-function sensitivity curves -> linear sRGB, giving
	// thin-film interference its vibrant, angle-dependent hue shift.
	glm::vec3 evalSensitivity(float OPD, const glm::vec3& shift)
	{
		const float phase = 2.0f * kPi * OPD * 1.0e-9f;
		const glm::vec3 val(5.4856e-13f, 4.4201e-13f, 5.2481e-13f);
		const glm::vec3 pos(1.6810e+06f, 1.7953e+06f, 2.2084e+06f);
		const glm::vec3 var(4.3278e+09f, 9.3046e+09f, 6.6121e+09f);

		glm::vec3 xyz;
		for (int i = 0; i < 3; ++i)
			xyz[i] = val[i] * std::sqrt(2.0f * kPi * var[i]) * std::cos(pos[i] * phase + shift[i]) * std::exp(-sqf(phase) * var[i]);
		xyz.x += 9.7470e-14f * std::sqrt(2.0f * kPi * 4.5282e+09f) * std::cos(2.2399e+06f * phase + shift[0]) * std::exp(-4.5282e+09f * sqf(phase));
		xyz /= 1.0685e-7f;

		// Matches main_scene.frag's XYZ_TO_REC709 mat3 literal exactly (GLSL's
		// mat3(...) 9-scalar constructor and glm::mat3's are both column-major,
		// so the same 9 values in the same order reproduce the same matrix).
		static const glm::mat3 kXyzToRec709(
			3.2404542f, -0.9692660f, 0.0556434f,
			-1.5371385f, 1.8760108f, -0.2040259f,
			-0.4985314f, 0.0415560f, 1.0572252f);
		return kXyzToRec709 * xyz;
	}

	// baseF90 defaults to 1.0 to match main_scene.frag's single-arg overload.
	glm::vec3 evalIridescence(float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness,
		const glm::vec3& baseF0, const glm::vec3& baseF90 = glm::vec3(1.0f))
	{
		const float iridescenceIor = glm::mix(outsideIOR, eta2, glm::smoothstep(0.0f, 0.03f, thinFilmThickness));
		const float sinTheta2Sq = sqf(outsideIOR / iridescenceIor) * (1.0f - sqf(cosTheta1));
		const float cosTheta2Sq = 1.0f - sinTheta2Sq;
		if (cosTheta2Sq < 0.0f)
			return glm::vec3(1.0f);
		const float cosTheta2 = std::sqrt(cosTheta2Sq);

		// First interface (air to iridescent film) - F90 at the air-film
		// interface is always 1.0.
		const float R0 = iorToFresnel0(iridescenceIor, outsideIOR);
		const float R12 = fSchlickIridescence(R0, cosTheta1, 1.0f);
		const float T121 = 1.0f - R12;
		float phi12 = 0.0f;
		if (iridescenceIor < outsideIOR) phi12 = kPi;
		const float phi21 = kPi - phi12;

		// Second interface (iridescent film to base material) - F90 here
		// uses the base material's own baseF90.
		const glm::vec3 baseIOR = fresnel0ToIor(glm::clamp(baseF0, glm::vec3(0.0f), glm::vec3(0.9999f)));
		const glm::vec3 R1 = iorToFresnel0(baseIOR, iridescenceIor);
		const glm::vec3 R23 = fSchlickIridescence(R1, cosTheta2, baseF90);
		glm::vec3 phi23(0.0f);
		if (baseIOR.x < iridescenceIor) phi23.x = kPi;
		if (baseIOR.y < iridescenceIor) phi23.y = kPi;
		if (baseIOR.z < iridescenceIor) phi23.z = kPi;

		const float OPD = 2.0f * iridescenceIor * thinFilmThickness * cosTheta2;
		const glm::vec3 phi = glm::vec3(phi21) + phi23;

		const glm::vec3 R123 = glm::clamp(glm::vec3(R12) * R23, glm::vec3(1e-5f), glm::vec3(0.9999f));
		const glm::vec3 r123 = glm::sqrt(R123);
		const glm::vec3 Rs = sqf(glm::vec3(T121)) * R23 / (glm::vec3(1.0f) - R123);

		glm::vec3 I = glm::vec3(R12) + Rs; // DC term

		glm::vec3 Cm = Rs - glm::vec3(T121);
		for (int m = 1; m <= 2; ++m)
		{
			Cm *= r123;
			const glm::vec3 Sm = 2.0f * evalSensitivity(static_cast<float>(m) * OPD, static_cast<float>(m) * phi);
			I += Cm * Sm;
		}
		return glm::max(I, glm::vec3(0.0f));
	}

	// Ported from rgb_mix() in main_scene.frag - an energy-conserving mix for
	// iridescent dielectric surfaces. A per-channel-varying Fresnel (e.g.
	// R=0.9, G=0.1, B=0.8) would let a plain per-channel mix() leave
	// low-Fresnel channels holding onto most of "base", inflating overall
	// brightness; this reduces base by the MAX channel's Fresnel uniformly
	// instead, so no channel keeps more base than the most-reflective
	// channel allows, while per-channel specular coloring is preserved.
	glm::vec3 rgbMix(const glm::vec3& base, const glm::vec3& layer, const glm::vec3& rgbAlpha)
	{
		const float rgbAlphaMax = std::max({ rgbAlpha.r, rgbAlpha.g, rgbAlpha.b });
		return (1.0f - rgbAlphaMax) * base + rgbAlpha * layer;
	}

	// Indirect/bounce-sampling side of KHR_materials_iridescence - unlike
	// direct lighting (evaluateDirectBRDF(), which replicates main_scene.
	// frag's evaluateBaseDirect() iridescence branch exactly), there's no
	// analytic prefiltered-IBL lookup here to replicate raster's
	// evaluateBaseIBL() iridescence branch against, so this instead blends
	// the ordinary Fresnel term toward evalIridescence()'s angle/thickness-
	// dependent color by iridescenceFactor - the same general-purpose
	// pattern main_scene.frag's own (declared but never called in that
	// shader) computeIridescentFresnel() helper describes. A single unified
	// F0 (surf.F0, already dielectric/metal-mixed) is used rather than
	// reconstructing separate dielectric/metal branches, consistent with how
	// sampleBSDFBounce() already treats F0 as one unified term elsewhere.
	glm::vec3 applyIridescenceToFresnel(const glm::vec3& baseFresnel, float cosTheta, const glm::vec3& F0, const SurfaceParams& surf)
	{
		if (surf.iridescenceFactor <= 0.001f || surf.iridescenceThickness <= 0.0f)
			return baseFresnel;
		const glm::vec3 iridescent = evalIridescence(1.0f, surf.iridescenceIor, std::clamp(cosTheta, 0.0f, 1.0f),
			surf.iridescenceThickness, glm::clamp(F0, glm::vec3(0.0f), glm::vec3(0.9999f)));
		return glm::mix(baseFresnel, iridescent, surf.iridescenceFactor);
	}

	// KHR_materials_volume's Beer-Lambert absorption, ported from
	// calculateVolumeAttenuation() in main_scene.frag - but fed the REAL
	// distance a refracted ray traveled inside the medium (see tracePixel()'s
	// transmission handling) rather than raster's authored thicknessFactor
	// approximation. attenuationDistance <= 0 is main_scene.frag's own
	// sentinel for "unset, no attenuation" (matching a texture/import
	// pipeline that never writes it, distinct from the spec's actual default
	// of +infinity, which this formula already handles correctly on its own -
	// pow(color, distance/infinity) == pow(color, 0) == 1, no special case
	// needed for that end).
	glm::vec3 calculateVolumeAttenuation(const glm::vec3& attenuationColor, float attenuationDistance, float distance)
	{
		if (attenuationDistance <= 0.0f)
			return glm::vec3(1.0f);
		return glm::pow(attenuationColor, glm::vec3(distance / attenuationDistance));
	}

	// hasAniso/anisoT/anisoB/at/ab mirror sampleBSDFBounce()'s anisotropic
	// parameters (see tracePixel()'s per-hit computation) - when hasAniso is
	// false this reduces to the plain isotropic Cook-Torrance path exactly
	// as before.
	glm::vec3 evaluateDirectBRDF(const glm::vec3& N, const glm::vec3& V, const glm::vec3& L,
		const SurfaceParams& surf, bool hasAniso, const glm::vec3& anisoT, const glm::vec3& anisoB, float at, float ab)
	{
		const float NdotL = std::max(glm::dot(N, L), 0.0f);
		const float NdotV = std::max(glm::dot(N, V), 0.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return glm::vec3(0.0f);

		const glm::vec3 H = glm::normalize(V + L);
		const float NdotH = std::max(glm::dot(N, H), 0.0f);
		const float VdotH = std::clamp(glm::dot(H, V), 0.0f, 1.0f);

		const glm::vec3 F = fresnelSchlick(VdotH, surf.directF0, surf.F90);

		glm::vec3 specularNoF;
		if (hasAniso)
		{
			const float D_aniso = distributionGGXAnisotropic(NdotH, glm::dot(anisoT, H), glm::dot(anisoB, H), at, ab);
			const float V_aniso = visibilityGGXAnisotropic(NdotL, NdotV,
				glm::dot(anisoB, V), glm::dot(anisoT, V), glm::dot(anisoT, L), glm::dot(anisoB, L), at, ab);
			specularNoF = glm::vec3(D_aniso * V_aniso);
		}
		else
		{
			const float D = distributionGGX(NdotH, surf.roughness);
			const float G = geometrySmith(NdotV, NdotL, surf.roughness);
			specularNoF = glm::vec3((D * G) / std::max(4.0f * NdotV * NdotL, 0.001f));
		}
		const glm::vec3 specular = specularNoF * F;

		// KHR_materials_pbrSpecularGlossiness - matches main_scene.frag's
		// useSpecGloss direct-lighting branch exactly: diffuse and specular
		// are MIXED by the dielectric Fresnel term (a legacy formulation
		// from the original spec-gloss reference shader), not added the
		// way the metallic-roughness workflow's diffuse+specular below is.
		// surf.directF0 already equals the spec-gloss specular color (see
		// evaluateSurface()'s override), so F here already IS that mix
		// weight - no separate Fresnel evaluation needed.
		if (surf.useSpecGloss)
		{
			const glm::vec3 l_diffuse = surf.baseColor / kPi;
			return glm::mix(l_diffuse, specular, F) * NdotL;
		}

		const glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - surf.metalness);
		// KHR_materials_transmission - matches main_scene.frag's
		// "diffuseOut = mix(diffuseOut, transmittedLight, transmission)":
		// at transmission=1, the diffuse response vanishes entirely (a
		// truly transparent point has no diffuse scattering left to show),
		// replaced instead by the refracted continuation ray handled in
		// tracePixel() - unlike a stochastic gate, this is a plain
		// deterministic scale-down, so it adds no variance/fireflies to
		// this direct-lighting term.
		// KHR_materials_diffuse_transmission - matches main_scene.frag's
		// "diffuseOut *= (1.0 - params.diffuseTransmissionFactor)": part of
		// the front-facing diffuse albedo is redirected to transmit through
		// to the back instead of reflecting (see tracePixel()'s NEE
		// back-hemisphere handling and the diffuse-lobe front/back split
		// below for where that redirected portion actually goes).
		const glm::vec3 diffuse = kD * surf.baseColor / kPi * (1.0f - surf.transmission) * (1.0f - surf.diffuseTransmissionFactor);

		// KHR_materials_iridescence - ported from evaluateBaseDirect()'s
		// iridescence branch in main_scene.frag, which entirely replaces the
		// diffuse+specular combination above with its own dielectric/metal
		// reconstruction (folding everything into what that function calls
		// specularOut, with diffuseOut zeroed) rather than adding a term on
		// top - so this branch returns instead of falling through.
		if (surf.iridescenceFactor > 0.001f && surf.iridescenceThickness > 0.0f)
		{
			const glm::vec3 l_diffuse = diffuse * NdotL;
			// KHR_materials_transmission - see the non-iridescence return
			// below for why the specular term is also scaled down here, not
			// just diffuse.
			const glm::vec3 l_specular = specularNoF * NdotL * (1.0f - surf.transmission);

			const glm::vec3 dielectricFresnel = fresnelSchlick(VdotH, surf.dielectricDirectF0, glm::vec3(surf.specularFactor));
			const glm::vec3 metalFresnel = fresnelSchlick(VdotH, surf.baseColor, glm::vec3(1.0f));
			glm::vec3 dielectricBrdf = glm::mix(l_diffuse, l_specular, dielectricFresnel);
			glm::vec3 metalBrdf = metalFresnel * l_specular;

			const glm::vec3 iridescenceFresnelDielectric = evalIridescence(1.0f, surf.iridescenceIor, NdotV, surf.iridescenceThickness, surf.dielectricF0);
			const glm::vec3 iridescenceFresnelMetallic = evalIridescence(1.0f, surf.iridescenceIor, NdotV, surf.iridescenceThickness, surf.baseColor);
			metalBrdf = glm::mix(metalBrdf, l_specular * iridescenceFresnelMetallic, surf.iridescenceFactor);
			dielectricBrdf = glm::mix(dielectricBrdf, rgbMix(l_diffuse, l_specular, iridescenceFresnelDielectric), surf.iridescenceFactor);

			return glm::mix(dielectricBrdf, metalBrdf, surf.metalness);
		}

		// KHR_materials_transmission - both DS's and RayTrophi's reference
		// implementations explicitly avoid evaluating a direct-light
		// specular BRDF against smooth/near-delta transmissive materials
		// (RayTrophi: "always mark transmission bounces as specular to
		// skip NEE... to avoid bright specular NEE fireflies through
		// glass"; DS's E_DELTA flagging skips NEE the same way). This
		// tracer doesn't yet support rough/glossy transmission (every
		// transmissive material is currently treated as a smooth mirror
		// regardless of authored roughness - a deferred v1 scope cut), so
		// for now every transmissive material's NEE specular response is
		// scaled down the same way diffuse already is, rather than only
		// gating it once real rough-transmission BSDF sampling exists.
		return (diffuse + specular * (1.0f - surf.transmission)) * NdotL;
	}

	// Ported from evaluateClearcoatDirect() in main_scene.frag. Note this
	// intentionally reuses distributionGGX()/geometrySmith() (which each
	// re-square their "roughness" argument internally) by passing them
	// clearcoatRoughness^2 (alpha) rather than clearcoatRoughness directly -
	// that double-squaring is exactly what the shader's own clearcoat call
	// site does, so replicating it here keeps the two lobes matched instead
	// of "fixing" it into a more standard single-squared GGX. Also note the
	// shader's clearcoat BRDF has no NdotL factor at all (divides by NdotV
	// only) - also kept verbatim for the same reason.
	glm::vec3 evaluateClearcoatDirect(const glm::vec3& Ncoat, const glm::vec3& V, const glm::vec3& L,
		float clearcoat, float clearcoatRoughness)
	{
		if (clearcoat <= 0.0f)
			return glm::vec3(0.0f);

		const glm::vec3 H = glm::normalize(V + L);
		const float NdotL = std::max(glm::dot(Ncoat, L), 0.0f);
		const float NdotV = std::max(glm::dot(Ncoat, V), 0.0f);
		const float NdotH = std::max(glm::dot(Ncoat, H), 0.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return glm::vec3(0.0f);

		const float alpha = clearcoatRoughness * clearcoatRoughness;
		const float D = distributionGGX(NdotH, alpha);
		const float G = geometrySmith(NdotV, NdotL, alpha);
		const float clearcoatBRDF = (D * G) / std::max(4.0f * NdotV, 0.001f);
		return glm::vec3(clearcoatBRDF * clearcoat);
	}

	// clamp(mat.ior, 1.0, inf)-derived dielectric F0, used as the clearcoat
	// layer's fixed Fresnel reflectance (main_scene.frag's clearcoatF0Scalar/
	// clearcoatFresnel) - unlike the base layer, the coat never tints via
	// specularColorFactor or mixes toward metalness.
	glm::vec3 computeClearcoatFresnel(float ior, const glm::vec3& Ncoat, const glm::vec3& V)
	{
		const float clearcoatIor = std::max(ior, 1.0f);
		const float f0Scalar = std::pow((clearcoatIor - 1.0f) / (clearcoatIor + 1.0f), 2.0f);
		const float NdotV = std::clamp(glm::dot(Ncoat, V), 0.0f, 1.0f);
		return fresnelSchlick(NdotV, glm::vec3(f0Scalar), glm::vec3(1.0f));
	}

	// ---- KHR_materials_sheen, ported verbatim from main_scene.frag --------

	float distributionCharlie(float NdotH, float roughness)
	{
		const float alpha = std::max(roughness * roughness, 0.000001f);
		const float invAlpha = 1.0f / alpha;
		const float sin2h = std::max(1.0f - NdotH * NdotH, 0.0078125f); // 2^(-7)
		return (2.0f + invAlpha) * std::pow(sin2h, invAlpha * 0.5f) / (2.0f * kPi);
	}

	float lambdaSheenNumericHelper(float x, float alphaG)
	{
		const float oneMinusAlphaSq = (1.0f - alphaG) * (1.0f - alphaG);
		const float a = glm::mix(21.5473f, 25.3245f, oneMinusAlphaSq);
		const float b = glm::mix(3.82987f, 3.32435f, oneMinusAlphaSq);
		const float c = glm::mix(0.19823f, 0.16801f, oneMinusAlphaSq);
		const float d = glm::mix(-1.97760f, -1.27393f, oneMinusAlphaSq);
		const float e = glm::mix(-4.32054f, -4.85967f, oneMinusAlphaSq);
		return a / (1.0f + b * std::pow(x, c)) + d * x + e;
	}

	float lambdaSheen(float cosTheta, float alphaG)
	{
		if (std::abs(cosTheta) < 0.5f)
			return std::exp(lambdaSheenNumericHelper(cosTheta, alphaG));
		return std::exp(2.0f * lambdaSheenNumericHelper(0.5f, alphaG) -
			lambdaSheenNumericHelper(1.0f - cosTheta, alphaG));
	}

	float visibilitySheen(float NdotL, float NdotV, float sheenRoughness)
	{
		sheenRoughness = std::max(sheenRoughness, 0.000001f);
		const float alphaG = sheenRoughness * sheenRoughness;
		return std::clamp(1.0f / ((1.0f + lambdaSheen(NdotV, alphaG) + lambdaSheen(NdotL, alphaG)) * (4.0f * NdotV * NdotL)), 0.0f, 1.0f);
	}

	// Ported from calculateSheen() in main_scene.frag. Additive (not blended
	// like clearcoat) - the shader also dampens the base layer's direct AND
	// indirect diffuse/specular by an energy-conservation factor derived
	// from a baked LUT (sheenELUT) when sheen is present; this is applied at
	// both NEE call sites (tracePixel()'s "albedoSheenScaling"/
	// "sheenIndirectDampening") using sampleSheenAlbedoLUT() as a stand-in
	// for that LUT - per a reference cross-check against the Dassault
	// Enterprise PBR spec, this dampening isn't optional polish, it's the
	// actual mechanism keeping the combined base+sheen material energy-
	// conserving, so it's applied rather than left as a known gap. Indirect/
	// environment sheen (main_scene.frag's evaluateSheenIBL()) turned out to be the
	// visually dominant contribution on IBL-only test scenes - direct sheen
	// alone looked "missing" next to raster. A first attempt added it as a
	// stochastic bounce lobe (like clearcoat's IBL), but unlike clearcoat -
	// whose IBL raster itself only approximates via a prefiltered mip - the
	// shader's sheen IBL is a single deterministic environment lookup with
	// zero noise; picking it up only a small, probability-weighted fraction
	// of the time made it read as noisy/near-invisible at ordinary sample
	// budgets rather than the smooth, always-present tint raster shows. See
	// sheenAlbedoLUT()/tracePixel()'s deterministic evaluation instead, which
	// - like the background/environment-miss lookup already elsewhere in
	// this file - is a direct analytic sample computed on every hit, not
	// something that needs to accumulate over many samples to be visible.
	glm::vec3 calculateSheen(const glm::vec3& N, const glm::vec3& V, const glm::vec3& L, const glm::vec3& sheenColor, float sheenRoughness)
	{
		const glm::vec3 H = glm::normalize(V + L);
		const float NdotL = std::clamp(glm::dot(N, L), 0.0f, 1.0f);
		const float NdotV = std::clamp(glm::dot(N, V), 0.0f, 1.0f);
		const float NdotH = std::clamp(glm::dot(N, H), 0.0f, 1.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return glm::vec3(0.0f);

		const float sheenRoughFinal = std::clamp(sheenRoughness, 0.000001f, 1.0f);
		const float D = distributionCharlie(NdotH, sheenRoughFinal);
		const float V_sheen = visibilitySheen(NdotL, NdotV, sheenRoughFinal);

		return sheenColor * D * V_sheen * NdotL;
	}

	// Replaces main_scene.frag's baked sheenELUT/charlieLUT textures - a
	// small hemispherical-directional-albedo table for the Charlie BRDF,
	// E(NdotV, roughness) = integral over the hemisphere of D_charlie*
	// V_sheen*NdotL dw, computed once via cosine-weighted-sample Monte Carlo
	// (the NdotL cancels analytically against the cosine pdf exactly as in
	// calculateSheen()'s call sites elsewhere in this file, so each bake
	// sample is just D*V_sheen*pi - no near-zero-NdotL division needed).
	// Built lazily on first use and cached for the process's lifetime - a
	// C++11 function-local static's initialization is already thread-safe,
	// and 32x32 texels x 256 samples (~262k evaluations of two closed-form
	// trig functions) takes well under a millisecond, so there's no need for
	// the fancier once-at-startup wiring a large asset-loading LUT would need.
	constexpr int kSheenLUTSize = 32;
	constexpr int kSheenLUTBakeSamples = 256;

	const std::vector<float>& sheenAlbedoLUT()
	{
		static const std::vector<float> lut = []()
		{
			std::vector<float> table(static_cast<size_t>(kSheenLUTSize) * kSheenLUTSize);
			Rng rng(0x5EEE17u); // fixed seed - deterministic bake, not tied to any pixel/frame RNG stream
			for (int ri = 0; ri < kSheenLUTSize; ++ri)
			{
				const float roughness = (ri + 0.5f) / kSheenLUTSize;
				for (int vi = 0; vi < kSheenLUTSize; ++vi)
				{
					const float NdotV = std::max((vi + 0.5f) / kSheenLUTSize, 1e-4f);
					const glm::vec3 V(std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV)), 0.0f, NdotV); // local frame, N = +Z

					float sum = 0.0f;
					for (int s = 0; s < kSheenLUTBakeSamples; ++s)
					{
						const glm::vec3 L = cosineSampleHemisphere(rng.next01(), rng.next01()); // local frame
						const float NdotL = std::max(L.z, 1e-4f);
						const glm::vec3 H = glm::normalize(V + L);
						const float NdotH = std::clamp(H.z, 0.0f, 1.0f);
						sum += distributionCharlie(NdotH, roughness) * visibilitySheen(NdotL, NdotV, roughness) * kPi;
					}
					table[static_cast<size_t>(ri) * kSheenLUTSize + vi] = sum / kSheenLUTBakeSamples;
				}
			}
			return table;
		}();
		return lut;
	}

	float sampleSheenAlbedoLUT(float NdotV, float roughness)
	{
		const std::vector<float>& lut = sheenAlbedoLUT();
		const int vi = std::clamp(static_cast<int>(NdotV * kSheenLUTSize), 0, kSheenLUTSize - 1);
		const int ri = std::clamp(static_cast<int>(roughness * kSheenLUTSize), 0, kSheenLUTSize - 1);
		return lut[static_cast<size_t>(ri) * kSheenLUTSize + vi];
	}

	// Stochastically samples one bounce direction from the BSDF (cosine-
	// weighted diffuse lobe or GGX specular lobe), returning the throughput
	// multiplier already divided by the sampling pdf and lobe-choice
	// probability (standard single-sample stochastic-lobe MC estimator - see
	// CpuPathTracer.h for why full MIS between NEE and BSDF sampling is not
	// implemented in v1).
	// hasAniso/anisoT/anisoB/alphaT/alphaB describe the anisotropic tangent
	// frame and stretched roughness (see tracePixel()'s per-hit computation,
	// mirroring main_scene.frag's buildAnisotropyBasis()/at,ab) - when
	// hasAniso is false the base specular lobe below samples isotropically
	// exactly as before (alphaT/alphaB are unused in that case).
	bool sampleBSDFBounce(const glm::vec3& N, const glm::vec3& Ncoat, const glm::vec3& V, const SurfaceParams& surf,
		const glm::vec3& clearcoatBlend, bool hasAniso, const glm::vec3& anisoT, const glm::vec3& anisoB,
		float alphaT, float alphaB, Rng& rng, glm::vec3& outDir, glm::vec3& outThroughput)
	{
		glm::vec3 T, B;
		buildOrthonormalBasis(N, T, B);

		const glm::vec3& F0 = surf.F0;
		// F0/metalness alone chronically under-samples glossy dielectrics: a
		// low-roughness floor/varnish/plastic (F0 ~ 0.04, metalness 0) still
		// clamps to the 5% floor here regardless of how narrow (and therefore
		// hard to resolve via the diffuse lobe alone) its specular lobe is -
		// e.g. a roughness-0.12 floor next to a bright reflective object
		// showed essentially no visible reflection even after the full
		// maxSamples budget, because only ~3 of 64 samples/pixel ever
		// explored the specular lobe at all. Blending in (1-roughness)^2
		// pushes smooth dielectrics toward much more specular sample
		// weight - rough materials (where this term is ~0) are unaffected.
		const float smoothness = 1.0f - surf.roughness;
		const float specProb = std::clamp((F0.r + F0.g + F0.b) / 3.0f + 0.5f * surf.metalness + 0.5f * smoothness * smoothness, 0.05f, 0.95f);

		// KHR_materials_clearcoat, indirect side: unlike the direct-light
		// path (which replicates main_scene.frag's analytic mix() exactly),
		// there's no analytic prefiltered-IBL lookup available here for the
		// coat, so it's instead added as a third stochastically-selected
		// lobe alongside diffuse/specular - the standard way a layered BSDF
		// is handled in a Monte-Carlo path tracer, selected with probability
		// proportional to the same clearcoat*Fresnel weight the direct path
		// uses to blend the two layers.
		const float coatProb = surf.clearcoat > 0.0f
			? std::clamp((clearcoatBlend.r + clearcoatBlend.g + clearcoatBlend.b) / 3.0f, 0.05f, 0.9f)
			: 0.0f;

		const float lobeXi = rng.next01();
		const float u1 = rng.next01();
		const float u2 = rng.next01();

		if (lobeXi < coatProb)
		{
			// GGX specular lobe over the coat's own normal/roughness/fixed
			// dielectric F0 (see computeClearcoatFresnel()) - same VNDF
			// importance-sampling machinery as the base specular lobe below.
			glm::vec3 Tc, Bc;
			buildOrthonormalBasis(Ncoat, Tc, Bc);

			const float alpha = surf.clearcoatRoughness * surf.clearcoatRoughness;
			const float NdotV0 = std::max(glm::dot(Ncoat, V), 1e-4f);
			const glm::vec3 Ve(glm::dot(V, Tc), glm::dot(V, Bc), NdotV0);

			const glm::vec3 hLocal = sampleGGXVNDF(Ve, alpha, alpha, u1, u2);
			const glm::vec3 H = localToWorld(hLocal, Ncoat, Tc, Bc);
			const glm::vec3 L = glm::reflect(-V, H);

			const float NdotL = glm::dot(Ncoat, L);
			const float NdotV = glm::dot(Ncoat, V);
			if (NdotL <= 0.0f || NdotV <= 0.0f)
				return false;

			const float VdotH = std::clamp(glm::dot(H, V), 0.0f, 1.0f);
			const glm::vec3 coatF0 = clearcoatBlend / std::max(surf.clearcoat, 1e-4f); // undo the *clearcoat factor - see computeClearcoatFresnel()
			const glm::vec3 F = fresnelSchlick(VdotH, coatF0, glm::vec3(1.0f));

			const float G1v = smithG1GGX(NdotV, alpha);
			const float G2  = smithG2HeightCorrelatedGGX(NdotV, NdotL, alpha);

			outThroughput = F * (G2 / std::max(G1v, 1e-6f)) / coatProb;
			outDir = L;
			return true;
		}

		const float remainingProb = 1.0f - coatProb;
		const float specProbScaled = specProb * remainingProb;

		if (lobeXi < coatProb + specProbScaled)
		{
			// GGX specular lobe via VNDF importance sampling (Heitz 2018 - see
			// sampleGGXVNDF). Ve must be expressed in the local tangent frame
			// (N = +Z) for the algorithm as published. When the material has
			// KHR_materials_anisotropy active, this samples in the rotated
			// anisotropic tangent frame with separate alphaT/alphaB instead
			// of the isotropic (T, B, alpha) basis - sampleGGXVNDF()/the
			// anisotropic Smith functions unify both cases already.
			const glm::vec3& basisN = N; // shading normal is unchanged; only the tangent frame/roughness differ
			const glm::vec3& Tb = hasAniso ? anisoT : T;
			const glm::vec3& Bb = hasAniso ? anisoB : B;
			const float alpha  = surf.roughness * surf.roughness;
			const float aT = hasAniso ? alphaT : alpha;
			const float aB = hasAniso ? alphaB : alpha;

			const float NdotV0 = std::max(glm::dot(basisN, V), 1e-4f);
			const glm::vec3 Ve(glm::dot(V, Tb), glm::dot(V, Bb), NdotV0);

			const glm::vec3 hLocal = sampleGGXVNDF(Ve, aT, aB, u1, u2);
			const glm::vec3 H = localToWorld(hLocal, basisN, Tb, Bb);
			const glm::vec3 L = glm::reflect(-V, H);

			const float NdotL = glm::dot(basisN, L);
			const float NdotV = glm::dot(basisN, V);
			if (NdotL <= 0.0f || NdotV <= 0.0f)
				return false;

			const float VdotH = std::clamp(glm::dot(H, V), 0.0f, 1.0f);
			const glm::vec3 F = applyIridescenceToFresnel(fresnelSchlick(VdotH, F0, surf.F90), VdotH, F0, surf);

			// VNDF sampling's throughput (BRDF(L)*NdotL/pdf(L)) simplifies to
			// F * G2/G1 - the standard result that makes VNDF sampling not
			// need D or NdotH at all in the final weight (see Heitz 2018 sec
			// 3.4, or RayTrophiStudio's ggxSampleVNDF/"VNDF weight = F*G1(L)"
			// comment for the same derivation using the non-height-correlated
			// form). G1/G2 here must be the pair the VNDF pdf was derived
			// from - NOT geometrySmith() above, which is the raster-matched
			// direct-lighting remapping used for NEE's BRDF value instead.
			float G1v, G2;
			if (hasAniso)
			{
				const glm::vec3 Vlocal(glm::dot(V, Tb), glm::dot(V, Bb), NdotV);
				const glm::vec3 Llocal(glm::dot(L, Tb), glm::dot(L, Bb), NdotL);
				G1v = smithG1GGXAniso(Vlocal, aT, aB);
				G2  = smithG2HeightCorrelatedGGXAniso(Vlocal, Llocal, aT, aB);
			}
			else
			{
				G1v = smithG1GGX(NdotV, alpha);
				G2  = smithG2HeightCorrelatedGGX(NdotV, NdotL, alpha);
			}

			outThroughput = F * (G2 / std::max(G1v, 1e-6f)) / specProbScaled;
			outDir = L;
			return true;
		}
		else
		{
			// Cosine-weighted diffuse lobe: BRDF(L)*NdotL/pdf(L) = kD*baseColor.
			//
			// KHR_materials_diffuse_transmission - stochastically pick
			// between this front-hemisphere reflection lobe (around N) and
			// a back-hemisphere transmission lobe (around -N), weighted by
			// diffuseTransmissionFactor (0 reduces to the original
			// front-only behavior exactly). Sampling -N with the SAME
			// (T,B) basis is valid since -N shares the same tangent plane
			// as N, just flipped. The stochastic pick weight cancels out
			// of the final throughput algebraically - E[outcome] = true
			// BRDF response either way - so both branches divide by the
			// same diffuseProb rather than diffuseProb*(1±dtf); see the
			// direct-light NEE handling above for the same result derived
			// for the analytic (non-stochastic) case.
			const bool transmitDiffuse = surf.diffuseTransmissionFactor > 0.0f && rng.next01() < surf.diffuseTransmissionFactor;
			const glm::vec3 lobeNormal = transmitDiffuse ? -N : N;
			const glm::vec3 local = cosineSampleHemisphere(u1, u2);
			outDir = localToWorld(local, lobeNormal, T, B);

			const float diffuseProb = std::max(remainingProb - specProbScaled, 1e-4f);
			if (transmitDiffuse)
			{
				outThroughput = surf.diffuseTransmissionColor / diffuseProb;
			}
			else
			{
				const float NdotV = std::max(glm::dot(N, V), 0.0f);
				const glm::vec3 F = applyIridescenceToFresnel(fresnelSchlick(NdotV, F0, surf.F90), NdotV, F0, surf);
				const glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - surf.metalness);
				outThroughput = (kD * surf.baseColor) / diffuseProb;
			}
			return true;
		}
	}

	glm::vec3 tracePixel(const RtEmbreeScene& scene, const RtSceneSnapshot& snapshot,
		const CpuPathTracer::Settings& settings, int px, int py, int width, int height, uint32_t rngSeed,
		bool* outPrimaryHit = nullptr)
	{
		Rng rng(hashCombine(hashCombine(static_cast<uint32_t>(px), static_cast<uint32_t>(py) * 9781u), rngSeed));

		// Jittered primary ray within the pixel (antialiasing across passes -
		// RtFrameAccumulator averages many passes, so per-pass jitter is free AA).
		const float jitterX = rng.next01();
		const float jitterY = rng.next01();
		const float ndcX = (2.0f * (px + jitterX) / width) - 1.0f;
		const float ndcY = 1.0f - (2.0f * (py + jitterY) / height);

		const RtCamera& cam = snapshot.camera;

		RtRay ray;
		if (cam.orthographic)
		{
			// Parallel rays: direction is constant, the origin sweeps across
			// the view plane instead (mirrors Camera's symmetric ortho frustum).
			ray.origin = cam.position
				+ ndcX * cam.aspectRatio * cam.orthoHalfHeight * cam.right
				+ ndcY * cam.orthoHalfHeight * cam.up;
			ray.direction = cam.forward;
		}
		else
		{
			ray.origin = cam.position;
			ray.direction = glm::normalize(
				cam.forward +
				ndcX * cam.aspectRatio * cam.tanHalfFovY * cam.right +
				ndcY * cam.tanHalfFovY * cam.up);
		}

		glm::vec3 radiance(0.0f);
		glm::vec3 throughput(1.0f);

		// AO only ever darkens indirect/ambient contributions (matching
		// main_scene.frag - see SurfaceParams::ao) - since this tracer's only
		// "ambient" source is the environment-miss placeholder, it's applied
		// specifically to that, scaled by the AO of the surface the missing
		// ray originated from (1.0 for the primary ray, before any surface).
		float lastHitAO = 1.0f;

		// prevHitPos lets the next hit measure the real distance traveled
		// since the last one, for Beer-Lambert absorption (see hitBackface
		// below - this tracer no longer carries "am I inside a medium"
		// state across bounce-loop iterations; see that comment for why).
		glm::vec3 prevHitPos = ray.origin;

		// Tracks whether the ray has resolved to its first REAL (non-alpha-
		// passed-through) hit yet - a base glTF alphaMode MASK/BLEND
		// fragment that the ray sees straight through doesn't count as
		// "hitting" anything for this purpose (see the alphaMode handling
		// below), so this can no longer just be "bounce == 0": several
		// alpha pass-throughs may occur before the ray reaches real geometry
		// or the background. Once resolved, it stays resolved - later
		// indirect bounces/refractions naturally use the env-miss (not
		// background-compositing) path regardless of how many pass-throughs
		// preceded the real hit.
		bool primaryHitResolved = false;

		// bounce tracks ordinary (opaque diffuse/specular/clearcoat/alpha-
		// pass-through) depth against settings.maxBounces, same as before.
		// transmissionDepth tracks KHR_materials_transmission reflect/
		// refract/TIR events SEPARATELY, against the much larger
		// settings.maxTransmissionBounces - see that setting's doc comment
		// for why a high-IOR dielectric's narrow total-internal-reflection
		// escape cone needs far more bounces than an ordinary opaque scene
		// budget allows, without inflating the cost of every other material.
		int bounce = 0;
		int transmissionDepth = 0;
		while (true)
		{
			if (bounce > settings.maxBounces)
				break; // matches the original "for (bounce=0; bounce<=maxBounces;...)" loop's exact termination point

			const RtHit hit = scene.intersect(ray);
			if (!hit.hit)
			{
				if (!primaryHitResolved)
				{
					if (outPrimaryHit)
						*outPrimaryHit = false;
					const glm::vec2 screenUv(
						(static_cast<float>(px) + 0.5f) / static_cast<float>(width),
						1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(height));
					radiance += throughput * lastHitAO * sampleEnvironmentBackground(snapshot.environment, ray.direction, screenUv);
				}
				else
				{
					radiance += throughput * lastHitAO * sampleEnvironmentMiss(snapshot.environment, ray.direction);
				}
				break;
			}

			if (kDebugVisualizeUV)
				return glm::vec3(hit.texCoords[0].x, hit.texCoords[0].y, 0.0f);

			if (hit.materialIndex >= snapshot.materials.size())
				break;

			const RtMaterial& mat = snapshot.materials[hit.materialIndex];
			SurfaceParams surf = evaluateSurface(mat, hit.texCoords, hit.vertexColor);
			lastHitAO = surf.ao;

			// Path regularization (Muller et al. 2018), per the RayTrophi
			// study: floor the effective roughness used for GGX specular/
			// clearcoat evaluation on any hit past the primary camera-
			// visible one. A perfectly (or near-) smooth specular lobe's
			// NEE evaluation against a point light, or its indirect BSDF-
			// sampled bounce direction, becomes increasingly narrow/
			// sensitive the more bounces deep it's evaluated at - exactly
			// the kind of chaotic, hard-to-converge noise seen as swirly
			// artifacts in concave transmissive cavities (confirmed via the
			// bounce-count heatmap diagnostic: nearby paths diverge more
			// the deeper they go). Flooring roughness only on secondary+
			// hits keeps the primary-ray-visible surface's own sharpness
			// untouched while taming this growth in variance/sensitivity.
			if (surf.transmission <= 0.001f && bounce + transmissionDepth > 0)
			{
				surf.roughness = std::max(surf.roughness, 0.1f);
				surf.clearcoatRoughness = std::max(surf.clearcoatRoughness, 0.1f);
			}

			// Stateless "am I currently inside this medium" test, per the
			// DS/RayTrophi reference-implementation study: both derive this
			// fresh at EVERY hit, purely from whether the ray struck the
			// front or back face of the CURRENT hit's own geometry
			// (dot(ray.direction, geometricNormal) > 0 means the ray is
			// travelling the same way as the outward normal, i.e. it hit
			// the surface from the inside/back) - never trusting state
			// carried forward from an earlier hit. This tracer previously
			// carried a persistent "insideMedium" bool across the whole
			// bounce loop instead, toggled once per crossing - a design
			// that has no way to self-correct if it ever drifts out of
			// sync with the true local geometry (e.g. across many rapid
			// internal bounces in a tight concave cavity, which is exactly
			// where this tracer was showing convoluted/swirly artifacts);
			// a stateless per-hit test can't drift by construction, since
			// it never depends on anything but the current hit.
			const bool hitBackface = glm::dot(ray.direction, hit.geometricNormal) > 0.0f;

			// Beer-Lambert absorption over the real distance traveled since
			// the previous hit - only applies on a back-face hit of a
			// material that actually has a volume (a thin-walled surface
			// has no interior to absorb through). Uses the CURRENT hit's
			// own material's attenuation properties, which assumes
			// non-nested transmissive volumes - matching both reference
			// implementations, neither of which tracks a nested-medium
			// stack either (see the transmission handling below).
			if (hitBackface && surf.hasVolume)
				throughput *= calculateVolumeAttenuation(surf.attenuationColor, surf.attenuationDistance, glm::length(hit.position - prevHitPos));
			prevHitPos = hit.position;

			glm::vec3 N = hit.normal;
			const glm::vec3 V = -ray.direction;
			if (glm::dot(N, V) < 0.0f)
				N = -N; // shade the side the ray actually hit (thin/backfacing geometry)

			// Smoothly-interpolated shading normal before any normal map is
			// applied - this is the analog of main_scene.frag's frame.Ng
			// (buildSurfaceFrame()'s getUnsignedWorldGeometryNormal(), which
			// is smoothly interpolated across the surface, NOT flat per-
			// triangle) - kept separately so the clearcoat normal below can
			// fall back to it instead of to hit.geometricNormal (the FLAT,
			// per-triangle normal this file uses only for ray-offset epsilon
			// - a different concept that happens to share the "Ng" name).
			// Using the flat normal there made untextured-clearcoat spheres
			// render visibly faceted, since nothing was left to smooth the
			// per-triangle discontinuity the way the base normal map (N) or a
			// clearcoat normal map otherwise would.
			const glm::vec3 Nsmooth = N;
			N = applyNormalMap(N, hit.tangent, hit.bitangent, mat.normalTexture.get(), hit.texCoords);

			// Geometric (flat, per-triangle) normal, consistently oriented with
			// N/V - used only for ray-offsetting (numerically robust regardless
			// of how far the smooth shading normal deviates from the true
			// triangle plane). An earlier version of this code also rejected/
			// terminated samples whenever N and Ng disagreed, meant to catch
			// rare silhouette-edge light leaks - but on densely-tessellated
			// curved geometry (e.g. accordion-folded surfaces) N and Ng
			// legitimately disagree almost everywhere, so that guard rejected
			// far more ordinary samples than it caught genuine leaks, producing
			// a visible per-triangle blotchy/noisy pattern. Removed for that
			// reason - see RtHit::geometricNormal for what still uses it.
			glm::vec3 Ng = hit.geometricNormal;
			if (glm::dot(Ng, V) < 0.0f)
				Ng = -Ng;

			// Scale-relative, not a fixed constant - see selfIntersectionEpsilon().
			// Computed here (rather than just before the NEE loop, where it
			// used to live) since the KHR_materials_transmission handling
			// below - which can end the iteration early via `continue` -
			// needs it too, for the refracted ray's origin offset.
			const float eps = selfIntersectionEpsilon(hit.position);

			// Visualization panel's "Self Shadows" checkbox (see
			// RtSceneSnapshot.h's selfShadowsEnabled doc comment) - when
			// off, every NEE shadow ray cast from this hit clears its own
			// instance's Embree geometry mask bit, so it still tests
			// occlusion against every OTHER instance but can't shadow
			// itself. When on (the default), the mask is all-bits-set - no
			// behavior change from before this was wired up.
			const uint32_t shadowRayMask = snapshot.selfShadowsEnabled
				? 0xFFFFFFFFu
				: ~(1u << (hit.instanceIndex % 32u));

			// Base glTF alphaMode (OPAQUE/MASK/BLEND) - previously
			// unimplemented in this tracer at all (a pre-existing gap, not
			// something the KHR extension work removed). A masked-out or
			// stochastically-rejected-BLEND fragment is treated as if the
			// surface weren't there for this sample at all: the ray
			// continues straight through undeviated with throughput
			// unchanged (no multiply/divide needed - unlike transmission's
			// Fresnel-weighted energy SPLIT, this is a binary "was the
			// surface here or not" existence pick, so whichever branch is
			// taken already carries its full, correct weight - the same
			// approach RayTrophi's OptiX/Vulkan any-hit and scatter-kernel
			// stochastic opacity handling uses), skipping this hit's
			// emissive/NEE/sheen/clearcoat/bounce shading entirely. MASK
			// uses a deterministic threshold test (matching main_scene.
			// frag's hard `discard`); BLEND stochastically picks per sample.
			if (mat.blendMode != 0) // not Opaque
			{
				float alphaTest = mat.opacity;
				if (mat.opacityTexture)
					alphaTest *= applyChannelPacking(sampleTexture(*mat.opacityTexture, hit.texCoords), *mat.opacityTexture);
				else if (mat.baseColorTexture)
					alphaTest *= sampleTexture(*mat.baseColorTexture, hit.texCoords).a; // main_scene.frag's PBR-mode sampleFallbackOpacity()
				alphaTest = std::clamp(alphaTest, 0.0f, 1.0f);

				const bool passThrough = (mat.blendMode == 1) // Masked
					? (alphaTest < mat.alphaThreshold)
					: (rng.next01() >= alphaTest); // Alpha (glTF BLEND)

				if (passThrough)
				{
					ray.origin = hit.position - Ng * eps;
					++bounce; // counts against the ordinary budget, same as before this loop was restructured
					continue;
				}
			}

			// This is the first REAL hit the ray has resolved to (opaque, or
			// an alphaMode surface that survived the test above) - see
			// primaryHitResolved's declaration for why this can no longer
			// just be "bounce == 0".
			if (!primaryHitResolved)
			{
				if (outPrimaryHit)
					*outPrimaryHit = true;
				primaryHitResolved = true;
			}

			// KHR_materials_unlit - per spec, "do not apply lighting" means
			// exactly that: no NEE, no BSDF bounce sampling, no clearcoat/
			// sheen/transmission, none of it - just the material's own
			// baseColor + emissive, output directly and the path
			// terminates there (matching main_scene.frag's unlit branch,
			// which returns baseColor+emissive immediately before any
			// lighting is evaluated at all - see main_scene.frag:3904-3909).
			// Tonemap/gamma are NOT applied here (unlike that raster
			// branch) since this tracer's return value is linear HDR,
			// consistent with every other radiance contribution in this
			// function - the accumulator/presenter tonemaps once at
			// display time, not per-sample.
			if (mat.unlit)
			{
				radiance += throughput * (surf.baseColor + surf.emissive);
				break;
			}

			// KHR_materials_clearcoat - the coat has its own normal (falls
			// back to the smooth pre-normal-map shading normal Nsmooth, NOT
			// the base-normal-mapped N - see main_scene.frag's
			// buildSurfaceFrame(): "frame.Ncoat = frame.Ng" before optionally
			// applying its own normal map) and a fixed ior-derived Fresnel
			// weight used to blend the coat over the base layer (see
			// composeLayeredPBR()). clearcoatBlend is 0 whenever clearcoat is
			// 0, so this is a no-op for the (common) non-clearcoat case below.
			const glm::vec3 Ncoat = applyNormalMap(Nsmooth, hit.tangent, hit.bitangent, mat.clearcoatNormalTexture.get(), hit.texCoords);
			const glm::vec3 clearcoatBlend = surf.clearcoat * computeClearcoatFresnel(mat.ior, Ncoat, V);

			radiance += throughput * surf.emissive * (glm::vec3(1.0f) - clearcoatBlend);

			// KHR_materials_sheen's base-layer INDIRECT energy-compensation
			// dampening (main_scene.frag's "iblSheenScaling") - computed
			// alongside the sheen env term below when sheen is present,
			// applied to the continuing bounce's throughput near this
			// function's final sampleBSDFBounce() call. 1.0 (no-op) for the
			// common non-sheen case.
			float sheenIndirectDampening = 1.0f;

			// KHR_materials_sheen, indirect/environment side - a genuine
			// stochastic integration of the actual live environment (a small
			// fixed count of RNG-jittered samples in a roughness-sized cone
			// around the mirror-reflect direction, taken every hit/every
			// pass - not gated behind a rare lobe-selection probability),
			// rather than trying to replicate raster's evaluateSheenIBL()
			// (a single lookup into a precomputed, roughness-mip-quantized
			// prefiltered cubemap). This is a genuinely more accurate result
			// than raster's baked approximation - real reflected environment
			// detail shows through (correctly blurred by the cone, not
			// flattened to one color), and RNG jitter (not a fixed offset
			// pattern - two earlier attempts at a fixed pattern produced a
			// Moire dot grid) means it accumulates into a clean blur across
			// passes rather than aliasing.
			if (surf.sheenColor != glm::vec3(0.0f))
			{
				const float sheenStrengthForIBL = std::max({ surf.sheenColor.r, surf.sheenColor.g, surf.sheenColor.b });
				const glm::vec3 R = glm::reflect(-V, N);
				glm::vec3 Tc, Bc;
				buildOrthonormalBasis(R, Tc, Bc);

				const float sheenRoughFinal = std::clamp(surf.sheenRoughness, 0.0001f, 1.0f);
				const float coneAngle = sheenRoughFinal * (kPi * 0.5f); // up to a full hemisphere spread at roughness 1

				constexpr int kSheenEnvSamples = 8;
				glm::vec3 envSum(0.0f);
				for (int s = 0; s < kSheenEnvSamples; ++s)
				{
					const float u1 = rng.next01();
					const float u2 = rng.next01();
					const float phi = 2.0f * kPi * u1;
					const float cosTheta = 1.0f - u2 * (1.0f - std::cos(coneAngle)); // uniform within the cone
					const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
					const glm::vec3 localDir(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
					envSum += sampleEnvironmentMiss(snapshot.environment, localToWorld(localDir, R, Tc, Bc));
				}
				envSum /= static_cast<float>(kSheenEnvSamples);

				const float NdotV_sheen = std::clamp(glm::dot(N, V), 0.0f, 1.0f);
				const float E_sheen = sampleSheenAlbedoLUT(NdotV_sheen, sheenRoughFinal);
				radiance += throughput * surf.ao * surf.sheenColor * envSum * E_sheen;

				// main_scene.frag's "iblSheenScaling" - dampens the base
				// layer's INDIRECT diffuse/specular the same way
				// l_albedoSheenScaling dampens the direct terms above (see
				// that comment for why this matters). Applied to the
				// continuing bounce's throughput below, right before the
				// final sampleBSDFBounce() call, since that continuing path
				// IS this hit's indirect diffuse+specular contribution in
				// this tracer's architecture (not a separate precomputed
				// term the way raster's baseDiffuseIBL/baseSpecularIBL are).
				sheenIndirectDampening = 1.0f - sheenStrengthForIBL * E_sheen;
			}

			// KHR_materials_anisotropy - stretches the specular lobe along a
			// tangent-space direction (see distributionGGXAnisotropic()/
			// visibilityGGXAnisotropic()/sampleBSDFBounce()'s anisotropic
			// branch). Ported from main_scene.frag's buildAnisotropyBasis(),
			// using Nsmooth as the analog of frame.Ng (see the comment on
			// Nsmooth above for why - NOT the flat hit.geometricNormal).
			// Requires real tangent data to build a meaningful basis, unlike
			// raster which has a screen-space-derivative fallback with no
			// per-ray equivalent here - untextured/tangentless meshes simply
			// render isotropically (hasAniso false), same scoping already
			// accepted for normal mapping in this file.
			const bool hasAniso = surf.anisotropyStrength > 0.0f && glm::length(hit.tangent) > 0.01f;
			glm::vec3 anisoT(1.0f, 0.0f, 0.0f), anisoB(0.0f, 1.0f, 0.0f);
			float anisoAlphaT = surf.roughness * surf.roughness, anisoAlphaB = anisoAlphaT;
			if (hasAniso)
			{
				glm::vec3 Tb = glm::normalize(hit.tangent - glm::dot(hit.tangent, Nsmooth) * Nsmooth);
				glm::vec3 Bb = glm::normalize(hit.bitangent - glm::dot(hit.bitangent, Nsmooth) * Nsmooth);
				if (glm::dot(glm::cross(Tb, Bb), Nsmooth) < 0.0f)
					Bb = -Bb;

				const glm::vec2 dir(std::cos(surf.anisotropyRotation), std::sin(surf.anisotropyRotation));
				anisoT = glm::normalize(dir.x * Tb + dir.y * Bb);
				anisoB = glm::normalize(glm::cross(Nsmooth, anisoT));
				if (glm::length(anisoB) < 0.0001f)
					anisoB = glm::normalize(glm::cross(Nsmooth, Tb));
				if (glm::dot(glm::cross(anisoT, anisoB), Nsmooth) < 0.0f)
					anisoB = -anisoB;

				const float alphaRoughness = std::max(surf.roughness * surf.roughness, 0.001f);
				anisoAlphaT = glm::mix(alphaRoughness, 1.0f, surf.anisotropyStrength * surf.anisotropyStrength);
				anisoAlphaB = std::clamp(alphaRoughness, 0.001f, 1.0f);
			}

			// Next-event estimation: sample every light directly (small light
			// counts in this app - PunctualLights::MAX_LIGHTS is 16 - so a
			// full loop is cheap and avoids extra light-selection-pdf variance).
			for (const RtLight& light : snapshot.lights)
			{
				glm::vec3 lightDir, lightIntensity;
				float lightDistance;
				evaluatePunctualLight(light, hit.position, lightDir, lightIntensity, lightDistance);
				if (lightIntensity == glm::vec3(0.0f))
					continue;

				const float NdotL = glm::dot(N, lightDir);
				if (NdotL <= 0.0f)
				{
					// KHR_materials_diffuse_transmission - a light on the
					// BACK side of the surface (relative to N) still
					// contributes if the material lets light diffusely
					// scatter through from behind (e.g. a leaf or curtain
					// lit from the far side) - a plain Lambertian term
					// using |NdotL| and tinted by diffuseTransmissionColor,
					// evaluated separately from evaluateDirectBRDF() (which
					// only handles the FRONT-hemisphere response) since this
					// is a distinct light-transport path, not a variant of
					// the same BRDF lobe. The shadow ray originates from the
					// back side (-Ng) since that's the side actually facing
					// this light.
					if (surf.diffuseTransmissionFactor > 0.0f)
					{
						RtRay backShadowRay;
						backShadowRay.origin = hit.position - Ng * eps;
						backShadowRay.direction = lightDir;
						backShadowRay.tFar = lightDistance - 2.0f * eps;
						backShadowRay.mask = shadowRayMask;
						if (!snapshot.shadowsEnabled || !scene.occluded(backShadowRay))
						{
							glm::vec3 diffuseBTDF = surf.diffuseTransmissionColor / kPi * std::abs(NdotL) * surf.diffuseTransmissionFactor;
							// KHR_materials_volume's attenuation, applied
							// using the AUTHORED thicknessFactor as the
							// absorption distance (matching main_scene.
							// frag's diffuseTransmissionThickness/
							// computeVolumeThickness) - unlike regular
							// KHR_materials_transmission elsewhere in this
							// function, this NEE term has no traced ray
							// path to measure a real distance from (it's a
							// single-point analytic contribution), so the
							// authored approximation is the only option
							// here, not a deliberately-skipped one.
							if (mat.hasVolume)
								diffuseBTDF *= calculateVolumeAttenuation(surf.attenuationColor, surf.attenuationDistance, mat.thicknessFactor);
							radiance += throughput * diffuseBTDF * lightIntensity;
						}
					}
					continue;
				}

				RtRay shadowRay;
				shadowRay.origin = hit.position + Ng * eps;
				shadowRay.direction = lightDir;
				shadowRay.tFar = lightDistance - 2.0f * eps;
				shadowRay.mask = shadowRayMask;
				if (snapshot.shadowsEnabled && scene.occluded(shadowRay))
					continue;

				glm::vec3 baseDirect = evaluateDirectBRDF(N, V, lightDir, surf, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB) * lightIntensity;

				// KHR_materials_sheen's base-layer energy-compensation
				// dampening, ported from main_scene.frag's own direct-light
				// sheen handling ("l_albedoSheenScaling"). This isn't an
				// optional add-on - per the Dassault Enterprise PBR spec,
				// it's the actual mechanism that keeps base+sheen combined
				// energy-conserving; without it a sheened surface's base
				// diffuse/specular respond as if the sheen fuzz weren't
				// there at all, reading as extra brightness on top of the
				// sheen glow instead of the fuzz partially replacing the
				// base response. sampleSheenAlbedoLUT() (already built for
				// the indirect sheen term) stands in for main_scene.frag's
				// separate sheenELUT here too - both represent the same
				// underlying Charlie-BRDF directional-albedo integral, so a
				// single combined LUT is a reasonable simplification rather
				// than baking a second, near-duplicate one.
				if (surf.sheenColor != glm::vec3(0.0f))
				{
					const float sheenStrength = std::max({ surf.sheenColor.r, surf.sheenColor.g, surf.sheenColor.b });
					const float NdotVSheen = std::clamp(glm::dot(N, V), 0.0f, 1.0f);
					const float albedoSheenScaling = std::min(
						1.0f - sheenStrength * sampleSheenAlbedoLUT(NdotVSheen, surf.sheenRoughness),
						1.0f - sheenStrength * sampleSheenAlbedoLUT(std::clamp(NdotL, 0.0f, 1.0f), surf.sheenRoughness));
					baseDirect *= albedoSheenScaling;
				}

				// Blend base vs. clearcoat exactly like composeLayeredPBR()'s
				// mix(baseColor, clearcoatLayer, clearcoat*clearcoatFresnel) -
				// clearcoatBlend is 0 for non-clearcoat materials, reducing to
				// baseDirect unchanged.
				if (surf.clearcoat > 0.0f)
				{
					const glm::vec3 coatDirect = evaluateClearcoatDirect(Ncoat, V, lightDir, surf.clearcoat, surf.clearcoatRoughness) * lightIntensity;
					radiance += throughput * glm::mix(baseDirect, coatDirect, clearcoatBlend);
				}
				else
				{
					radiance += throughput * baseDirect;
				}

				// KHR_materials_sheen - additive, not blended.
				if (surf.sheenColor != glm::vec3(0.0f))
					radiance += throughput * calculateSheen(N, V, lightDir, surf.sheenColor, surf.sheenRoughness) * lightIntensity;
			}

			// Russian roulette termination. Uses bounce+transmissionDepth
			// combined (transmissionDepth is 0 for non-transmissive
			// materials, so this is unchanged from before for ordinary
			// scenes) so a long TIR chain with genuinely low throughput
			// (e.g. from attenuationColor absorption) still gets a chance to
			// terminate early, rather than only ever stopping via the much
			// higher maxTransmissionBounces cap below.
			if (bounce + transmissionDepth >= settings.russianRouletteStartDepth)
			{
				const float p = std::clamp(std::max({ throughput.r, throughput.g, throughput.b }), 0.05f, 1.0f);
				if (rng.next01() > p)
					break;
				throughput /= p;
			}

			glm::vec3 bounceDir, bounceThroughput;

			// KHR_materials_transmission - see RtSceneSnapshot.h's comment on
			// why the real traced distance replaces the authored
			// thicknessFactor approximation. Bypasses the general multi-lobe
			// sampleBSDFBounce() entirely for transmissive materials: a
			// single Fresnel-weighted reflect-or-refract choice, computed
			// exactly once. An earlier version of this code instead wrapped
			// a SEPARATE transmit/opaque gate AROUND a full call into the
			// general lobe scheme - since that scheme already internally
			// weights its own rare specular lobe by 1/specProb, stacking a
			// second, independent rare-branch inverse-probability on top of
			// it multiplied the two together, producing extreme-variance
			// "firefly" outliers that the denoiser then smeared into a flat
			// white haze instead of the correct clear/refracted look (the
			// diffuse response is instead scaled down deterministically in
			// evaluateDirectBRDF(), not via a probability gate, since a
			// direct analytic term has no variance to manage). Rough/glossy
			// transmission and interaction with clearcoat/sheen/anisotropy
			// are out of scope for this v1 - transmissive materials are
			// treated as smooth dielectrics.
			if (surf.transmission > 0.001f)
			{
				// See settings.maxTransmissionBounces's doc comment - this
				// budget is tracked separately from (and is much larger
				// than) the ordinary bounce cap above, specifically so a
				// high-IOR dielectric's narrow TIR escape cone gets enough
				// attempts to actually find an exit. Exhausting it is a
				// rare edge case (an extremely narrow escape cone, or a
				// pathological geometry) - contributing nothing further here
				// is preferable to looping indefinitely.
				if (transmissionDepth >= settings.maxTransmissionBounces)
					break;
				++transmissionDepth;

				const float NdotVTransmission = std::clamp(glm::dot(N, V), 0.0f, 1.0f);

				// KHR_materials_transmission rough/glossy support (Walter et
				// al. 2007, "Microfacet Models for Refraction through Rough
				// Surfaces") - reuses the same GGX VNDF importance-sampling
				// machinery sampleBSDFBounce() already uses for the opaque
				// specular lobe. An earlier attempt at this used surf.roughness
				// directly and was bitten by the path-regularization floor
				// above forcing every transmissive material's SECOND (exit)
				// crossing to roughness>=0.1 regardless of how polished the
				// material actually was - see this file's git history/commit
				// messages for that regression. That's now fixed at the
				// source (the regularization block above is gated on
				// surf.transmission<=0.001, so it never touches a
				// transmissive material's roughness at all), so surf.roughness
				// is safe to use here directly. At the roughness floor
				// (~0.03) this collapses to Hm≈N, matching plain smooth-
				// dielectric behavior almost exactly. Deliberately NOT
				// applied to the thin-walled branch further down (glTF's
				// thin-walled model is an idealized infinitely-thin film,
				// not a rough microfacet surface). Known, deliberate
				// simplification: no eta^2 adjoint-radiance correction
				// factor (cancels out over any complete entry+exit pair, so
				// it can't break energy conservation for the common
				// closed-surface case - see Codex's review for the fuller
				// critique that this is a bounded VNDF approximation, not a
				// from-scratch derivation of the full Walter/DS Jacobian).
				glm::vec3 Ht, Bt;
				buildOrthonormalBasis(N, Ht, Bt);
				const float transmissionAlpha = surf.roughness * surf.roughness;
				const float NdotV0 = std::max(NdotVTransmission, 1e-4f);
				const glm::vec3 Ve(glm::dot(V, Ht), glm::dot(V, Bt), NdotV0);
				const glm::vec3 hLocal = sampleGGXVNDF(Ve, transmissionAlpha, transmissionAlpha, rng.next01(), rng.next01());
				const glm::vec3 Hm = localToWorld(hLocal, N, Ht, Bt);
				const float VdotHm = std::clamp(glm::dot(V, Hm), 0.0f, 1.0f);

				// KHR_materials_iridescence applies to the Fresnel reflectance
				// at ANY dielectric interface, not just an opaque one - a
				// transmissive material (soap bubble, iridescent glass) still
				// shows the same thin-film color shift on its reflected
				// portion. This was previously computed with a plain
				// fresnelSchlick() here, bypassing applyIridescenceToFresnel()
				// entirely, so transmissive+iridescent materials showed no
				// iridescence at all - a real gap, not a deliberate v1 cut.
				// Evaluated at the microfacet normal (VdotHm), matching
				// Walter et al.'s treatment, not the macro NdotV.
				const glm::vec3 transmissionFresnel = applyIridescenceToFresnel(
					fresnelSchlick(VdotHm, surf.dielectricF0, glm::vec3(1.0f)), VdotHm, surf.dielectricF0, surf);
				const float reflectProb = std::clamp((transmissionFresnel.r + transmissionFresnel.g + transmissionFresnel.b) / 3.0f, 0.05f, 0.95f);

				// kDebugVisualizeTransmission - false-colors this hit's
				// reflect/refract/TIR decision and bypasses the rest of the
				// loop entirely, so the boundary can be inspected directly
				// against the real (converged) image instead of inferred
				// from it. Colors: red = chose reflect; green = chose
				// refract and genuinely crossed the boundary (entering or
				// exiting the medium, or a thin-walled pass-through);
				// yellow = chose refract but hit true geometric TIR;
				// non-transmissive surfaces and env misses render black.
				// Flip the constant at the top of this file to enable.
				if (kDebugVisualizeTransmission && transmissionDepth == 1)
				{
					// TEMPORARY: both branches now sample REAL content
					// (reflected-direction environment for reflect, the
					// unchanged ray direction for thin-walled pass-through)
					// instead of a flat placeholder color for reflect - an
					// earlier version returned solid (1,0,0) for reflect,
					// which at any non-trivial reflectProb biases the
					// per-pixel AVERAGE toward red proportional to local
					// Fresnel reflectance (worse at oblique angles), making
					// the accumulated debug image misleading rather than a
					// clean isolation of "is the environment sample
					// correct." This version should show genuine composite
					// content directly comparable to the real render.
					if (rng.next01() < reflectProb)
						return sampleEnvironmentMiss(snapshot.environment, glm::reflect(ray.direction, N));
					if (!surf.hasVolume)
						return sampleEnvironmentMiss(snapshot.environment, ray.direction);
					const float etaDbg = hitBackface ? mat.ior : (1.0f / mat.ior);
					const glm::vec3 refractDirDbg = glm::refract(ray.direction, Hm, etaDbg);
					if (refractDirDbg == glm::vec3(0.0f))
						return glm::vec3(1.0f, 1.0f, 0.0f); // TIR
					// Genuine exit crossing - encode the actual computed exit
					// direction as RGB (dir*0.5+0.5) instead of a flat green,
					// so a sign/orientation bug in the exit refraction shows
					// up as a visibly wrong or discontinuous direction field
					// rather than being hidden behind a uniform "it crossed"
					// color. A correct exit direction field should vary
					// smoothly and divergently outward from the sphere,
					// mirroring the entry ray's approach direction.
					return refractDirDbg * 0.5f + glm::vec3(0.5f);
				}

				if (rng.next01() < reflectProb)
				{
					bounceDir = glm::reflect(ray.direction, Hm);
					const float NdotL = glm::dot(N, bounceDir);
					if (NdotL <= 0.0f)
						break; // sampled a microfacet below the macro surface - rare at low roughness
					const float G1v = smithG1GGX(NdotV0, transmissionAlpha);
					const float G2  = smithG2HeightCorrelatedGGX(NdotV0, NdotL, transmissionAlpha);
					bounceThroughput = transmissionFresnel * (G2 / std::max(G1v, 1e-6f)) / reflectProb;
					ray.origin = hit.position + Ng * eps;
				}
				else if (!surf.hasVolume)
				{
					// KHR_materials_transmission WITHOUT KHR_materials_volume
					// (thicknessFactor's own spec default is 0) means the
					// surface is implicitly thin-walled - glTF's intent is a
					// "hole"/idealized infinitely-thin film (a soap bubble,
					// not a solid lens), so the transmitted ray passes
					// straight through completely undeviated: no Snell bend,
					// no interior medium to be "inside" or absorb through.
					// Offsetting through Ng (not reflecting off it) still
					// avoids immediately re-hitting the same surface.
					//
					// Tinted by surf.baseColor, matching glTF spec intent - a
					// thin-walled transparent material's baseColor (including
					// any pattern/text baked into a baseColorTexture, e.g. a
					// printed paper lampshade) is meant to filter the light
					// passing through it, exactly like the volumetric branch
					// below. An earlier version of this code removed the tint
					// entirely, diagnosed against a test material whose
					// baseColorFactor happened to default to pure white -
					// tinting-by-white is a no-op, so that test could never
					// have shown whether removing the tint was actually
					// correct; it wasn't, and a patterned/colored thin-walled
					// material (like a printed shade around a bulb) lost its
					// entire pattern as a result. On concave, many-crossing
					// thin-walled geometry a non-white baseColor CAN compound
					// (baseColor^N for N crossings) - a real but narrower
					// limitation than initially thought, and matches both
					// DS's and RayTrophi's reference behavior (neither
					// special-cases this), so correctness for the common,
					// spec-intended case (a single- or few-crossing colored/
					// patterned film) takes priority over that rarer edge case.
					bounceDir = ray.direction;
					bounceThroughput = surf.baseColor * (glm::vec3(1.0f) - transmissionFresnel) / (1.0f - reflectProb);
					ray.origin = hit.position - Ng * eps;
				}
				else
				{
					// KHR_materials_dispersion - per-channel IOR spread on
					// refraction (prism/chromatic-aberration effect),
					// following the same halfSpread formula main_scene.frag
					// uses (getIBLVolumeRefractionPerChannel's iors =
					// ior-halfSpread/ior/ior+halfSpread for R/G/B). Rather
					// than tracing 3 separate rays per sample (tripling the
					// cost of every dispersive transmission bounce), this
					// stochastically picks ONE hero channel per sample with
					// equal 1/3 probability, refracts using ONLY that
					// channel's IOR, and masks+triples the resulting
					// throughput to that single channel - the standard
					// hero-wavelength unbiased-estimator trick (over many
					// accumulated samples, each channel gets its own
					// correctly-dispersed result 1/3 of the time, averaging
					// to the full per-channel spread). No dispersion (the
					// common case) skips all of this - dispersedIor equals
					// mat.ior exactly and channelMask is (1,1,1)/no-op.
					float dispersedIor = mat.ior;
					glm::vec3 channelMask(1.0f);
					if (surf.dispersion > 0.0f)
					{
						const float halfSpread = (mat.ior - 1.0f) * 0.025f * surf.dispersion;
						const float channelXi = rng.next01();
						if (channelXi < 1.0f / 3.0f)
						{
							dispersedIor = mat.ior - halfSpread;
							channelMask = glm::vec3(3.0f, 0.0f, 0.0f);
						}
						else if (channelXi < 2.0f / 3.0f)
						{
							channelMask = glm::vec3(0.0f, 3.0f, 0.0f);
						}
						else
						{
							dispersedIor = mat.ior + halfSpread;
							channelMask = glm::vec3(0.0f, 0.0f, 3.0f);
						}
					}

					const float eta = hitBackface ? dispersedIor : (1.0f / dispersedIor);
					glm::vec3 refractDir = glm::refract(ray.direction, Hm, eta);
					if (refractDir == glm::vec3(0.0f))
					{
						// Total internal reflection - stays in the same medium. This
						// is a genuine mirror bounce (100% reflectance, geometrically
						// forced by Snell's law), not a partial/tinted transmission
						// event - even though it's reached via the "attempt transmit"
						// branch of the stochastic Fresnel pick above. Deliberately
						// NOT tinted by surf.baseColor (glTF's transmitted-light tint
						// only applies to light that actually crosses the boundary);
						// an earlier version of this code fell through to the shared
						// baseColor-tinted throughput below, incorrectly coloring TIR
						// bounces as if they were successful transmission.
						refractDir = glm::reflect(ray.direction, Hm);
						const float NdotL = glm::dot(N, refractDir);
						if (NdotL <= 0.0f)
							break; // sampled a microfacet below the macro surface
						ray.origin = hit.position + Ng * eps;
						bounceDir = refractDir;
						const float G1v = smithG1GGX(NdotV0, transmissionAlpha);
						const float G2  = smithG2HeightCorrelatedGGX(NdotV0, NdotL, transmissionAlpha);
						// TIR is dispersion-neutral (a pure mirror bounce,
						// not a color-selective refraction event) - no
						// channelMask applied here, only for genuine
						// (dispersed) crossings below.
						bounceThroughput = glm::vec3(G2 / std::max(G1v, 1e-6f));
					}
					else
					{
						// No medium-state bookkeeping needed here anymore -
						// hitBackface (and Beer-Lambert absorption) are
						// re-derived fresh from each hit's own geometry, not
						// carried forward from this crossing.
						ray.origin = hit.position - Ng * eps; // crossing to the other side of the surface
						bounceDir = refractDir;

						// Pragmatic multi-scatter approximation for high-
						// roughness "frosted diffuser" materials (paper
						// lampshades, frosted glass) - a real frosted
						// material's strong opacity comes from many internal
						// scattering events, not a single microfacet bounce;
						// a single-bounce rough BTDF (the Hm-based refractDir
						// above) is a fundamentally weaker effect that reads
						// as "clear with some blur" rather than properly
						// diffuse/opaque, no matter how roughness/Fresnel are
						// tuned - confirmed by comparing a rough-roughness
						// lampshade against the Khronos Sample Viewer
						// reference. As a stand-in for real subsurface
						// scattering (deferred - see task list), blend the
						// transmitted direction toward a true Lambertian
						// cosine-weighted hemisphere (oriented on the far
						// side of the surface, -N) with probability =
						// roughness - at roughness 0 this never fires
						// (unchanged smooth/rough-specular behavior), at
						// roughness 1 it's effectively fully diffuse. This is
						// a stochastic MIX between two direction-sampling
						// strategies for the SAME already-decided "successful
						// transmission" outcome, so - by the same MC
						// cancellation argument used for
						// KHR_materials_diffuse_transmission's front/back
						// lobe mix - bounceThroughput below is unaffected by
						// which one was chosen. Uses sqrt(roughness) rather
						// than roughness directly - real frosted materials
						// (paper, frosted glass) read as strongly opaque/
						// diffuse well before roughness=1 in PBR terms
						// (their apparent opacity comes from real multi-
						// scattering, which this is only approximating), so
						// a linear mapping under-diffused moderate-roughness
						// materials in practice (e.g. a lampshade's holder
						// still visibly showing through) - sqrt(roughness)
						// biases the blend toward "diffuse" more aggressively
						// at moderate roughness while still reducing to 0 at
						// roughness 0 and 1 at roughness 1.
						const float diffuseBlendProb = std::sqrt(std::clamp(surf.roughness, 0.0f, 1.0f));
						if (diffuseBlendProb > 0.0f && rng.next01() < diffuseBlendProb)
						{
							glm::vec3 Td, Bd;
							buildOrthonormalBasis(-N, Td, Bd);
							const glm::vec3 diffuseLocal = cosineSampleHemisphere(rng.next01(), rng.next01());
							bounceDir = localToWorld(diffuseLocal, -N, Td, Bd);
						}

						// Smith masking-shadowing ratio for the transmitted
						// direction - NdotL is naturally negative here (L is
						// on the opposite side of N from V), so the
						// visibility term uses |NdotL|, the standard
						// treatment for a transmission lobe (masking/
						// shadowing is symmetric about which side of the
						// macro surface the direction is on).
						const float NdotL = std::abs(glm::dot(N, bounceDir));
						const float G1v = smithG1GGX(NdotV0, transmissionAlpha);
						const float G2  = smithG2HeightCorrelatedGGX(NdotV0, NdotL, transmissionAlpha);
						// channelMask is (1,1,1) (no-op) when dispersion==0;
						// otherwise masks+triples to the single hero channel
						// this crossing's eta was actually computed for -
						// see the dispersion setup above.
						bounceThroughput = channelMask * surf.baseColor * (glm::vec3(1.0f) - transmissionFresnel) * (G2 / std::max(G1v, 1e-6f)) / (1.0f - reflectProb); // glTF tints transmitted light by baseColor
					}
				}

				throughput *= bounceThroughput;
				if (throughput.r <= 0.0f && throughput.g <= 0.0f && throughput.b <= 0.0f)
					break;
				ray.direction = bounceDir;
				continue;
			}

			if (!sampleBSDFBounce(N, Ncoat, V, surf, clearcoatBlend, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB, rng, bounceDir, bounceThroughput))
				break;

			throughput *= bounceThroughput * sheenIndirectDampening;
			if (throughput.r <= 0.0f && throughput.g <= 0.0f && throughput.b <= 0.0f)
				break;

			// Offset toward whichever side bounceDir actually continues on -
			// ordinarily always the front (+Ng), but KHR_materials_
			// diffuse_transmission's back-hemisphere lobe (see
			// sampleBSDFBounce()) can return a direction on the far side of
			// the surface, which needs the opposite offset to avoid
			// immediately self-intersecting the same triangle.
			ray.origin = hit.position + Ng * (glm::dot(bounceDir, Ng) >= 0.0f ? eps : -eps);
			ray.direction = bounceDir;
			++bounce; // ordinary (non-transmission) bounce - counts against the loop's original budget
		}

		// kDebugVisualizeTransmissionBounceCount - a simple black->blue->
		// green->yellow->red->white heat ramp over
		// transmissionDepth's final value, normalized against
		// maxTransmissionBounces.
		if (kDebugVisualizeTransmissionBounceCount && transmissionDepth > 0)
		{
			const float t = std::clamp(static_cast<float>(transmissionDepth) /
				static_cast<float>(std::max(1, settings.maxTransmissionBounces)), 0.0f, 1.0f);
			if (t < 0.25f)
				return glm::mix(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), t / 0.25f);
			if (t < 0.5f)
				return glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), (t - 0.25f) / 0.25f);
			if (t < 0.75f)
				return glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), (t - 0.5f) / 0.25f);
			return glm::mix(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), (t - 0.75f) / 0.25f);
		}

		// Firefly/outlier suppression - see Settings::fireflyClampThreshold's
		// doc comment. Scales the whole sample down proportionally (rather
		// than clamping each channel independently) so an extreme-value
		// sample is dimmed without shifting its hue.
		const float maxChannel = std::max({ radiance.r, radiance.g, radiance.b });
		if (maxChannel > settings.fireflyClampThreshold && maxChannel > 0.0f)
			radiance *= (settings.fireflyClampThreshold / maxChannel);

		return radiance;
	}
}

void CpuPathTracer::renderPass(
	const RtEmbreeScene& scene,
	const RtSceneSnapshot& snapshot,
	int width, int height,
	uint32_t sampleSeed,
	std::vector<glm::vec3>& outRadiance,
	const std::atomic<bool>* cancelFlag,
	std::vector<uint8_t>* outPrimaryHitMask) const
{
	if (width <= 0 || height <= 0)
	{
		outRadiance.clear();
		if (outPrimaryHitMask) outPrimaryHitMask->clear();
		return;
	}

	outRadiance.assign(static_cast<size_t>(width) * height, glm::vec3(0.0f));
	if (outPrimaryHitMask)
		outPrimaryHitMask->assign(static_cast<size_t>(width) * height, 0);

	// Simple row-partitioned parallel-for: worker threads are spawned per
	// call and joined at the end. A persistent thread pool with a job queue
	// would only pay off if renderPass() were called many times per second;
	// each pass here takes milliseconds-to-seconds, so thread spawn/join
	// overhead (microseconds) is negligible - see CpuPathTracer.h.
	const unsigned int hwThreads = std::thread::hardware_concurrency();
	const unsigned int workerCount = std::max(1u, hwThreads <= 1u ? 1u : hwThreads - 1u);

	auto renderRows = [&](int rowStart, int rowEnd)
	{
		for (int y = rowStart; y < rowEnd; ++y)
		{
			// Checked once per scanline (not per-pixel, to keep the check's
			// own overhead negligible) so a cancelled pass returns within
			// about one row's tracing time - see header comment on cancelFlag.
			if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
				return;

			for (int x = 0; x < width; ++x)
			{
				bool primaryHit = false;
				outRadiance[static_cast<size_t>(y) * width + x] =
					tracePixel(scene, snapshot, _settings, x, y, width, height, sampleSeed,
						outPrimaryHitMask ? &primaryHit : nullptr);
				if (outPrimaryHitMask)
					(*outPrimaryHitMask)[static_cast<size_t>(y) * width + x] = primaryHit ? 1 : 0;
			}
		}
	};

	if (workerCount <= 1 || height < static_cast<int>(workerCount))
	{
		renderRows(0, height);
		return;
	}

	std::vector<std::thread> workers;
	workers.reserve(workerCount);
	const int rowsPerWorker = (height + static_cast<int>(workerCount) - 1) / static_cast<int>(workerCount);
	for (unsigned int w = 0; w < workerCount; ++w)
	{
		const int rowStart = static_cast<int>(w) * rowsPerWorker;
		const int rowEnd = std::min(height, rowStart + rowsPerWorker);
		if (rowStart >= rowEnd) break;
		workers.emplace_back(renderRows, rowStart, rowEnd);
	}
	for (std::thread& t : workers)
		t.join();
}
