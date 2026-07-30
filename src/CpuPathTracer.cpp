#include "CpuPathTracer.h"
#include "PathUtils.h"
#include "RtEmbreeScene.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

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
	constexpr bool kDebugVisualizeClearcoat = false;
	// False-colors the primary hit's raw front-hemisphere shadowTransmittance
	// (summed across all lights, BEFORE the default light's 0.15 floor clamp
	// is applied) - added to diagnose the DiffuseTransmissionPlant "leaves
	// aren't getting light under Shadows" report: distinguishes "shadow rays
	// are landing on the floor almost everywhere" (dense self-occlusion) from
	// "something else is dimming this." See tracePixel()'s NEE loop.
	constexpr bool kDebugVisualizeShadowTransmittance = false;
	// Post-loop heat-ramp of KHR_materials_volume_scatter's scatterBounces
	// counter (see tracePixel()'s free-flight walk) - scaled against a
	// small fixed cap distinct from settings.maxVolumeScatterBounces, chosen
	// purely for a useful visualization range.
	constexpr bool kDebugVisualizeVolumeScatterBounces = false;

	// KHR_materials_volume_scatter free-flight random-walk constants,
	// matching NVIDIA's vk_gltf_renderer reference exactly
	// (pathtrace_functions.h.slang:39-43) - see this feature's plan doc.
	// The free-budget scatter-bounce count itself (VOLUME_FREE_BUDGET there)
	// is user-adjustable - see CpuPathTracer::Settings::maxVolumeScatterBounces -
	// rather than a fixed constant here, since it's the main SSS-quality-vs-
	// render-time knob for volume-scatter-heavy scenes.
	constexpr float kVolumeMinScatter = 0.001f;
	constexpr float kVolumeRandFloor = 1.0e-10f;
	constexpr float kVolumeRrFloor = 0.001f;
	constexpr float kVolumeRrCap = 0.95f;
	// No scatterAnisotropy field exists anywhere in this codebase's
	// material pipeline yet (Material.h, glTF import/export) - hardcoded
	// isotropic (g=0) for v1; sampleHenyeyGreenstein()/henyeyGreensteinPdf()
	// are written generally so a future anisotropy factor needs no rewrite.
	constexpr float kVolumeScatterAnisotropy = 0.0f;
	// Visualization-only cap for kDebugVisualizeVolumeScatterBounces - NOT
	// the same as settings.maxVolumeScatterBounces, just a range that's
	// actually useful to look at.
	constexpr float kVolumeScatterDebugRampCap = 32.0f;

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

	// Wraps an integer texel index (which may be out-of-range - the whole
	// point of this function is resolving the neighbor-texel indices a
	// bilinear fetch needs at a tile edge/corner) according to the same wrap
	// mode applyWrap() uses for the continuous coordinate. Needed because a
	// bilinear sample near a REPEAT-wrapped tile's edge must blend with the
	// texel from the OPPOSITE edge (not clamp to it), and near a MIRRORED_
	// REPEAT edge must reflect back into the tile - a plain std::clamp would
	// silently behave like CLAMP_TO_EDGE for every wrap mode.
	int wrapTexelIndex(int idx, int size, unsigned int mode)
	{
		constexpr unsigned int kClampToEdge    = 0x812Fu;
		constexpr unsigned int kMirroredRepeat = 0x8370u;

		if (mode == kClampToEdge)
			return std::clamp(idx, 0, size - 1);

		if (mode == kMirroredRepeat)
		{
			const int period = 2 * size;
			int m = idx % period;
			if (m < 0) m += period;
			return (m < size) ? m : (period - 1 - m);
		}

		// GL_REPEAT and anything unrecognized.
		int m = idx % size;
		if (m < 0) m += size;
		return m;
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

	// ---- Texture sampling (bilinear) ------------------------------------------
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
	//
	// Bilinear (not nearest-neighbour, an earlier version of this function) -
	// a fine/high-frequency, tiled texture (e.g. a KHR_materials_iridescence
	// thicknessTexture with a KHR_texture_transform scale packing several
	// repeats into a small screen-space area) combined with nearest-neighbour
	// lookup meant each sample's AA sub-pixel jitter could land on a totally
	// different, uncorrelated texel from one sample to the next - true
	// texture aliasing, not Monte Carlo noise, so it never converged no
	// matter how many samples were accumulated (diagnosed against a swirly
	// iridescent car-paint material whose thickness map never resolved past
	// a speckled, non-converging grain even at 256 samples with denoising
	// disabled). Bilinear interpolation makes the sampled value vary
	// smoothly and continuously with sub-pixel position instead of snapping
	// between quantized texel values, matching what GPU texture units do
	// for raster (mip-mapping/trilinear would be the fully correct fix for
	// minification, but bilinear alone already removes the snap-between-
	// uncorrelated-texels behavior that was causing non-convergence here).
	// Bilinear sample of ONE mip level - factored out of sampleTexture() so
	// trilinear filtering (below) can call it twice (the two nearest levels)
	// and blend. st is already wrapped/transformed; width/height/rgba8/wrapS/
	// wrapT identify the specific level being sampled.
	glm::vec4 bilinearSampleLevel(int width, int height, const std::vector<uint8_t>& rgba8,
		unsigned int wrapS, unsigned int wrapT, const glm::vec2& st)
	{
		// Standard half-texel-centered bilinear: texel (0,0)'s center sits at
		// continuous position 0.5, matching GL's own texture-unit convention.
		const float fx = st.x * width  - 0.5f;
		const float fy = st.y * height - 0.5f;
		const int x0 = static_cast<int>(std::floor(fx));
		const int y0 = static_cast<int>(std::floor(fy));
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		// Each of the 2x2 neighbor texels is wrapped independently (not
		// clamped) - see wrapTexelIndex()'s doc comment for why a plain
		// clamp would be wrong for REPEAT/MIRRORED_REPEAT at a tile edge.
		auto texel = [&](int x, int y) -> glm::vec4
		{
			x = wrapTexelIndex(x, width,  wrapS);
			y = wrapTexelIndex(y, height, wrapT);
			const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
			return glm::vec4(
				rgba8[idx + 0] / 255.0f,
				rgba8[idx + 1] / 255.0f,
				rgba8[idx + 2] / 255.0f,
				rgba8[idx + 3] / 255.0f);
		};

		const glm::vec4 c00 = texel(x0,     y0);
		const glm::vec4 c10 = texel(x0 + 1, y0);
		const glm::vec4 c01 = texel(x0,     y0 + 1);
		const glm::vec4 c11 = texel(x0 + 1, y0 + 1);

		const glm::vec4 top    = glm::mix(c00, c10, tx);
		const glm::vec4 bottom = glm::mix(c01, c11, tx);
		return glm::mix(top, bottom, ty);
	}

	// lod: mip level to sample, trilinearly blended between floor(lod) and
	// ceil(lod) - see RtTextureSample::mips' doc comment for why this exists
	// (path-traced texture minification aliasing) and computeTextureLod()
	// for how callers derive it. Defaults to 0.0f (base level only, the
	// exact previous behavior) for call sites that don't have per-hit
	// triangle/camera context cheaply available (shadow rays, alpha-cutout
	// existence tests, transmission peeks) - those are boolean-ish existence
	// checks where minification aliasing is far less perceptually important
	// than on directly-shaded, visible surfaces.
	glm::vec4 sampleTexture(const RtTextureSample& tex, const glm::vec2 (&texCoords)[4], float lod = 0.0f)
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

		if (tex.mips.empty() || lod <= 0.0f)
			return bilinearSampleLevel(tex.width, tex.height, tex.rgba8, tex.wrapS, tex.wrapT, st);

		const float clampedLod = std::clamp(lod, 0.0f, static_cast<float>(tex.mips.size() - 1));
		const int level0 = static_cast<int>(std::floor(clampedLod));
		const int level1 = std::min(level0 + 1, static_cast<int>(tex.mips.size()) - 1);
		const float levelBlend = clampedLod - static_cast<float>(level0);

		const RtTextureMipLevel& mip0 = tex.mips[level0];
		const glm::vec4 sample0 = bilinearSampleLevel(mip0.width, mip0.height, mip0.rgba8, tex.wrapS, tex.wrapT, st);
		if (level1 == level0)
			return sample0;

		const RtTextureMipLevel& mip1 = tex.mips[level1];
		const glm::vec4 sample1 = bilinearSampleLevel(mip1.width, mip1.height, mip1.rgba8, tex.wrapS, tex.wrapT, st);
		return glm::mix(sample0, sample1, levelBlend);
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

		// KHR_materials_volume_scatter - multiScatterColor is the authored
		// multiscatterColorFactor, consumed by computeVolumeScatterCoefficients()/
		// multiToSingleScatterAlbedo() (see calculateVolumeAttenuation()'s
		// neighboring helpers) to derive the free-flight random walk's
		// scattering coefficient. hasVolumeScattering mirrors hasVolume's
		// presence-gate role above.
		glm::vec3 multiScatterColor   = glm::vec3(1.0f);
		bool      hasVolumeScattering = false;

		// KHR_materials_diffuse_transmission - see tracePixel()'s NEE and
		// diffuse-lobe handling.
		float     diffuseTransmissionFactor = 0.0f;
		glm::vec3 diffuseTransmissionColor  = glm::vec3(1.0f);
	};

	// Per-texture mip level for this specific texture's own resolution,
	// derived from the hit's resolution-independent footprintInUvArea (see
	// RtHit::uvAreaPerWorldArea's doc comment and tracePixel()'s own comment
	// on where footprintInUvArea comes from - the camera pixel's world-space
	// footprint at the hit, converted to UV-space via the triangle's own
	// UV/world-area ratio). footprintInUvArea<=0 means "no LOD info for this
	// hit" (e.g. a bounce/indirect hit, where this isn't computed) -
	// sampleTexture()'s own lod<=0 fast path already handles that by
	// sampling the base level only, matching this function's pre-mipmap
	// behavior exactly.
	float computeTextureLod(const RtTextureSample& tex, float footprintInUvArea)
	{
		if (footprintInUvArea <= 0.0f || tex.mips.size() <= 1)
			return 0.0f;
		const float footprintInTexelsSq = footprintInUvArea * static_cast<float>(tex.width) * static_cast<float>(tex.height);
		return 0.5f * std::log2(std::max(footprintInTexelsSq, 1.0f));
	}

	// vertexColor is the interpolated COLOR_0 attribute (or the (1,1,1,1)
	// identity when the mesh has none - see RtSceneBuilder's hasVertexColors
	// gating), applied last and RGB-only, exactly matching computeBaseColor()
	// in main_scene.frag ("Apply vertex color last (in linear)" - COLOR_0 is
	// already linear per glTF spec, unlike textures, so no sRGB decode here).
	// footprintInUvArea: see computeTextureLod()'s doc comment - 0.0f (the
	// default) reproduces this function's pre-mipmap behavior exactly
	// (every texture read samples its base level only).
	SurfaceParams evaluateSurface(const RtMaterial& mat, const glm::vec2 (&texCoords)[4], const glm::vec4& vertexColor,
		float footprintInUvArea = 0.0f)
	{
		SurfaceParams s;
		s.baseColor = mat.baseColor;
		if (mat.baseColorTexture)
		{
			const glm::vec4 t = sampleTexture(*mat.baseColorTexture, texCoords, computeTextureLod(*mat.baseColorTexture, footprintInUvArea));
			s.baseColor *= sRGBToLinear(glm::vec3(t));
		}
		s.baseColor *= glm::vec3(vertexColor);

		s.metalness = mat.metalness;
		if (mat.metallicTexture)
			s.metalness *= applyChannelPacking(sampleTexture(*mat.metallicTexture, texCoords, computeTextureLod(*mat.metallicTexture, footprintInUvArea)), *mat.metallicTexture);

		s.roughness = mat.roughness;
		if (mat.roughnessTexture)
			s.roughness *= applyChannelPacking(sampleTexture(*mat.roughnessTexture, texCoords, computeTextureLod(*mat.roughnessTexture, footprintInUvArea)), *mat.roughnessTexture);
		s.roughness = std::clamp(s.roughness, 0.0001f, 1.0f); // matches main_scene.frag's roughness floor

		s.emissive = mat.emissive * mat.emissiveStrength;
		if (mat.emissiveTexture)
			s.emissive *= sRGBToLinear(glm::vec3(sampleTexture(*mat.emissiveTexture, texCoords, computeTextureLod(*mat.emissiveTexture, footprintInUvArea))));

		// Ported verbatim from main_scene.frag: "clamp(mix(1.0, texAO,
		// occlusionStrength), 0.0001, 1.0)".
		s.ao = 1.0f;
		if (mat.aoTexture)
		{
			const float texAO = applyChannelPacking(sampleTexture(*mat.aoTexture, texCoords, computeTextureLod(*mat.aoTexture, footprintInUvArea)), *mat.aoTexture);
			s.ao = std::clamp(glm::mix(1.0f, texAO, mat.occlusionStrength), 0.0001f, 1.0f);
		}

		float texturedSpecularFactor = mat.specularFactor;
		if (mat.specularTexture)
			texturedSpecularFactor *= applyChannelPacking(sampleTexture(*mat.specularTexture, texCoords, computeTextureLod(*mat.specularTexture, footprintInUvArea)), *mat.specularTexture);

		glm::vec3 texturedSpecularColorFactor = mat.specularColorFactor;
		if (mat.specularColorTexture)
			texturedSpecularColorFactor *= sRGBToLinear(glm::vec3(sampleTexture(*mat.specularColorTexture, texCoords, computeTextureLod(*mat.specularColorTexture, footprintInUvArea))));

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
				const glm::vec4 t = sampleTexture(*mat.diffuseTexture, texCoords, computeTextureLod(*mat.diffuseTexture, footprintInUvArea));
				s.baseColor *= sRGBToLinear(glm::vec3(t));
			}
			s.baseColor *= glm::vec3(vertexColor);

			glm::vec3 specGlossColor = mat.specGlossSpecularColor;
			float glossiness = mat.glossinessFactor;
			if (mat.specularGlossinessTexture)
			{
				const glm::vec4 packed = sampleTexture(*mat.specularGlossinessTexture, texCoords, computeTextureLod(*mat.specularGlossinessTexture, footprintInUvArea));
				specGlossColor *= sRGBToLinear(glm::vec3(packed));
				glossiness *= packed.a;
			}
			specGlossColor = glm::clamp(specGlossColor, glm::vec3(0.0f), glm::vec3(1.0f));

			s.roughness = std::clamp(1.0f - glossiness, 0.0001f, 1.0f);
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
			s.clearcoat *= applyChannelPacking(sampleTexture(*mat.clearcoatTexture, texCoords, computeTextureLod(*mat.clearcoatTexture, footprintInUvArea)), *mat.clearcoatTexture);
		s.clearcoat = std::clamp(s.clearcoat, 0.0f, 1.0f);

		s.clearcoatRoughness = mat.clearcoatRoughness;
		if (mat.clearcoatRoughnessTexture)
			s.clearcoatRoughness *= applyChannelPacking(sampleTexture(*mat.clearcoatRoughnessTexture, texCoords, computeTextureLod(*mat.clearcoatRoughnessTexture, footprintInUvArea)), *mat.clearcoatRoughnessTexture);
		s.clearcoatRoughness = std::clamp(s.clearcoatRoughness, 0.0001f, 1.0f);

		s.sheenColor = mat.sheenColor;
		if (mat.sheenColorTexture)
			s.sheenColor *= sRGBToLinear(glm::vec3(sampleTexture(*mat.sheenColorTexture, texCoords, computeTextureLod(*mat.sheenColorTexture, footprintInUvArea))));
		s.sheenColor = glm::clamp(s.sheenColor, glm::vec3(0.0f), glm::vec3(1.0f));

		s.sheenRoughness = mat.sheenRoughness;
		if (mat.sheenRoughnessTexture)
			s.sheenRoughness *= applyChannelPacking(sampleTexture(*mat.sheenRoughnessTexture, texCoords, computeTextureLod(*mat.sheenRoughnessTexture, footprintInUvArea)), *mat.sheenRoughnessTexture);
		s.sheenRoughness = std::clamp(s.sheenRoughness, 0.0001f, 1.0f);

		s.anisotropyStrength = mat.anisotropyStrength;
		s.anisotropyRotation = mat.anisotropyRotation;
		if (mat.anisotropyTexture)
		{
			const glm::vec3 texel(sampleTexture(*mat.anisotropyTexture, texCoords, computeTextureLod(*mat.anisotropyTexture, footprintInUvArea)));
			decodeAnisotropyTexture(texel, mat.anisotropyStrength, mat.anisotropyRotation, s.anisotropyStrength, s.anisotropyRotation);
		}

		s.iridescenceFactor = mat.iridescenceFactor;
		if (mat.iridescenceTexture)
			s.iridescenceFactor *= applyChannelPacking(sampleTexture(*mat.iridescenceTexture, texCoords, computeTextureLod(*mat.iridescenceTexture, footprintInUvArea)), *mat.iridescenceTexture);

		s.iridescenceIor = mat.iridescenceIor;

		// Ported from main_scene.frag: defaults to iridescenceThicknessMax
		// (not min!) when untextured, matching its own params.
		// iridescenceThickness = pbrLighting.iridescenceThicknessMax default.
		s.iridescenceThickness = mat.iridescenceThicknessMax;
		if (mat.iridescenceThicknessTexture)
			s.iridescenceThickness = applyChannelPacking(sampleTexture(*mat.iridescenceThicknessTexture, texCoords, computeTextureLod(*mat.iridescenceThicknessTexture, footprintInUvArea)), *mat.iridescenceThicknessTexture);

		s.transmission = mat.transmission;
		if (mat.transmissionTexture)
			s.transmission *= applyChannelPacking(sampleTexture(*mat.transmissionTexture, texCoords, computeTextureLod(*mat.transmissionTexture, footprintInUvArea)), *mat.transmissionTexture);
		s.hasVolume           = mat.hasVolume;
		s.attenuationColor    = mat.attenuationColor;
		s.attenuationDistance = mat.attenuationDistance;
		s.dispersion          = mat.dispersion;
		s.multiScatterColor   = mat.multiScatterColor;
		s.hasVolumeScattering = mat.hasVolumeScattering;

		s.diffuseTransmissionFactor = mat.diffuseTransmissionFactor;
		if (mat.diffuseTransmissionTexture)
			s.diffuseTransmissionFactor *= applyChannelPacking(sampleTexture(*mat.diffuseTransmissionTexture, texCoords, computeTextureLod(*mat.diffuseTransmissionTexture, footprintInUvArea)), *mat.diffuseTransmissionTexture);
		s.diffuseTransmissionColor = mat.diffuseTransmissionColor;
		if (mat.diffuseTransmissionColorTexture)
			s.diffuseTransmissionColor *= sRGBToLinear(glm::vec3(sampleTexture(*mat.diffuseTransmissionColorTexture, texCoords, computeTextureLod(*mat.diffuseTransmissionColorTexture, footprintInUvArea))));

		return s;
	}

	// Ported from calcBumpedNormal() in main_scene.frag, minus its screen-
	// space-derivative fallback (dFdx/dFdy have no equivalent per-ray in a
	// path tracer) - when the mesh has no tangent data, this just returns N
	// unchanged rather than attempting a derivative-based tangent frame.
	// scale is glTF's normalTexture.scale (RtMaterial::normalScale/
	// clearcoatNormalScale) - dampens the tangent-plane X/Y perturbation
	// before renormalizing (main_scene.frag's sampleMappedNormal()), left at
	// its 1.0 default (full strength, matching glTF's own spec default) by
	// callers that don't have a specific value to pass. Previously always
	// implicitly 1.0 regardless of what the material actually authored - a
	// real, previously-unnoticed gap: a material with a deliberately gentle
	// bump (a low scale, meant to read as a subtle finish) instead got the
	// texture's full, undamped tilt, which cascades into every angle-
	// dependent calculation downstream (Fresnel, reflection direction,
	// KHR_materials_iridescence's hue) as excess per-pixel noise that no
	// amount of sampling or environment-side fixes can resolve, since the
	// underlying normal itself was varying far more than authored.
	glm::vec3 applyNormalMap(const glm::vec3& N, const glm::vec3& rawTangent, const glm::vec3& rawBitangent,
		const RtTextureSample* normalTex, const glm::vec2 (&texCoords)[4], float scale = 1.0f)
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
		glm::vec3 tangentNormal = glm::vec3(sampled) * 2.0f - 1.0f;
		// Matches main_scene.frag's sampleMappedNormal() exactly: only the
		// tangent-plane X/Y components are dampened by scale (glTF's
		// normalTexture.scale) - Z (how far the perturbed normal tilts out of
		// the tangent plane) is left alone, then the whole vector is
		// renormalized. Previously missing entirely - see this function's
		// scale parameter doc comment (RtMaterial::normalScale) for why that
		// was a real, previously-unnoticed bug, not a deliberate v1 cut.
		tangentNormal.x *= scale;
		tangentNormal.y *= scale;
		tangentNormal = glm::normalize(tangentNormal);

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
	glm::vec3 toPrefilterDirection(const glm::vec3& v);

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
		// envMapExposure scales environment light reaching a surface (bounce
		// > 0 - reflection/refraction escapes, NEE toward the environment,
		// sheen ambient sums - every call site of this function), matching
		// main_scene.frag's identical per-IBL-sample use of the same
		// uniform. Deliberately NOT applied in sampleEnvironmentBackground()
		// (the directly-visible bounce == 0 backdrop), which mirrors
		// skybox.frag - that shader only applies iblExposure, never
		// envMapExposure, to the background itself.
		if (environment.faceSize <= 0)
			return flatGradientMiss(direction) * environment.envMapExposure;
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
		// iblExposure is deliberately NOT applied here - it's applied exactly
		// once, uniformly, to the whole accumulated frame by the final
		// present/tonemap stage (path_traced_present.frag's applyToneMapping(),
		// RtTonemap::apply() for offline export). An earlier version of this
		// code applied it here too, which double-exposed env-sourced radiance
		// relative to purely punctual-lit surfaces, since the present stage
		// already re-applies it to everything.
		return sampleCubemapFaces(environment.faces, environment.faceSize, sampleDir) * environment.envMapExposure;
	}

	// Variance-reduced alternative to sampleEnvironmentMiss() for an indirect
	// bounce whose most recent surface interaction was a diffuse (cosine-
	// weighted) lobe - see RtEnvironment::irradianceFaces's doc comment for
	// the full rationale. Falls back to the raw map (sampleEnvironmentMiss())
	// if no irradiance map was captured, same graceful-degradation pattern as
	// the raw map's own "no environment loaded" fallback.
	glm::vec3 sampleEnvironmentDiffuse(const RtEnvironment& environment, const glm::vec3& direction)
	{
		if (environment.irradianceFaceSize <= 0)
			return sampleEnvironmentMiss(environment, direction);
		// Plain undoSkyboxRotation() - see toPrefilterDirection()'s doc
		// comment for why the irradiance map needs no extra swizzle,
		// unlike the prefilter map below.
		const glm::vec3 sampleDir = undoSkyboxRotation(direction, environment.cameraUpAxisZUp, environment.skyBoxZRotationDegrees);
		return sampleCubemapFaces(environment.irradianceFaces, environment.irradianceFaceSize, sampleDir) * environment.envMapExposure;
	}

	// Variance-reduced alternative to sampleEnvironmentMiss() for an indirect
	// bounce whose most recent surface interaction was the specular/coat
	// lobe - see RtEnvironment::prefilterMips's doc comment for the full
	// rationale. roughness selects (and linearly blends between) the two
	// nearest prefiltered mip levels, mirroring main_scene.frag's own
	// textureLod(prefilterMap, R, roughness * (prefilterMipLevels-1)).
	// Falls back to the raw map if no prefilter chain was captured.
	glm::vec3 sampleEnvironmentSpecular(const RtEnvironment& environment, const glm::vec3& direction, float roughness)
	{
		if (environment.prefilterMips.empty())
			return sampleEnvironmentMiss(environment, direction);
		// undoSkyboxRotation() first, THEN the extra toPrefilterDirection()
		// swizzle - see its doc comment for the derivation.
		const glm::vec3 sampleDir = toPrefilterDirection(
			undoSkyboxRotation(direction, environment.cameraUpAxisZUp, environment.skyBoxZRotationDegrees));

		const float maxLod = static_cast<float>(environment.prefilterMips.size() - 1);
		const float lod = std::clamp(roughness, 0.0f, 1.0f) * maxLod;
		const int mipLow = std::clamp(static_cast<int>(std::floor(lod)), 0, static_cast<int>(maxLod));
		const int mipHigh = std::min(mipLow + 1, static_cast<int>(maxLod));
		const float frac = lod - static_cast<float>(mipLow);

		const RtEnvironment::PrefilterMip& lowMip = environment.prefilterMips[static_cast<size_t>(mipLow)];
		const glm::vec3 colorLow = sampleCubemapFaces(lowMip.faces, lowMip.faceSize, sampleDir);
		if (mipHigh == mipLow)
			return colorLow * environment.envMapExposure;
		const RtEnvironment::PrefilterMip& highMip = environment.prefilterMips[static_cast<size_t>(mipHigh)];
		const glm::vec3 colorHigh = sampleCubemapFaces(highMip.faces, highMip.faceSize, sampleDir);
		return glm::mix(colorLow, colorHigh, frac) * environment.envMapExposure;
	}

	glm::vec3 sampleEnvironmentSheen(const RtEnvironment& environment, const glm::vec3& direction, float roughness)
	{
		if (environment.sheenPrefilterMips.empty())
			return sampleEnvironmentSpecular(environment, direction, roughness);

		const glm::vec3 sampleDir = toPrefilterDirection(
			undoSkyboxRotation(direction, environment.cameraUpAxisZUp, environment.skyBoxZRotationDegrees));

		const float maxLod = static_cast<float>(environment.sheenPrefilterMips.size() - 1);
		const float lod = std::clamp(roughness, 0.0f, 1.0f) * maxLod;
		const int mipLow = std::clamp(static_cast<int>(std::floor(lod)), 0, static_cast<int>(maxLod));
		const int mipHigh = std::min(mipLow + 1, static_cast<int>(maxLod));
		const float frac = lod - static_cast<float>(mipLow);

		const RtEnvironment::PrefilterMip& lowMip = environment.sheenPrefilterMips[static_cast<size_t>(mipLow)];
		const glm::vec3 colorLow = sampleCubemapFaces(lowMip.faces, lowMip.faceSize, sampleDir);
		if (mipHigh == mipLow)
			return colorLow * environment.envMapExposure;
		const RtEnvironment::PrefilterMip& highMip = environment.sheenPrefilterMips[static_cast<size_t>(mipHigh)];
		const glm::vec3 colorHigh = sampleCubemapFaces(highMip.faces, highMip.faceSize, sampleDir);
		return glm::mix(colorLow, colorHigh, frac) * environment.envMapExposure;
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

	// Additional transform needed ONLY for sampling the prefilter map for
	// specular/reflection bounces - NOT for the raw map or the irradiance
	// map. Derived from main_scene.frag's actual material-shading code (not
	// drawSkyBox()'s background-display logic, a different, unrelated
	// rotation an earlier version of this function incorrectly modeled
	// instead):
	//   - Specular reflections: Rprefilter = toPrefilterDirection(
	//     envMapRotationMatrix * R), where toPrefilterDirection(v) is the
	//     fixed swizzle (v.x, -v.z, v.y).
	//   - Diffuse/irradiance: Nibl = envMapRotationMatrix * N - no extra
	//     swizzle at all.
	// envMapRotationMatrix (ViewportWidget::updateEnvMapRotationMatrix())
	// is, algebraically, exactly what undoSkyboxRotation() below already
	// computes (both convert a world-space direction into the captured
	// cubemap's own local sample space) - so the irradiance map needs
	// PLAIN undoSkyboxRotation(), unchanged, and the prefilter map needs
	// undoSkyboxRotation() with this extra swizzle applied on top. An
	// earlier version of this function instead skipped undoSkyboxRotation()'s
	// middle "R2" step for BOTH maps, modeled on drawSkyBox()'s unrelated
	// background-cube-geometry rotation logic - algebraically a different
	// composition entirely, which read as approximately-plausible for some
	// viewing angles but was actually wrong (manifesting as a subtle
	// reflected-environment scale/framing mismatch against raster, not the
	// grosser 90 degree rotation the very first, even earlier version of
	// this code had).
	glm::vec3 toPrefilterDirection(const glm::vec3& v)
	{
		return glm::vec3(v.x, -v.z, v.y);
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

		// iblExposure is NOT applied here - see sampleEnvironmentMiss()'s
		// comment; the final present/tonemap stage applies it once to the
		// whole frame, so pre-multiplying it here would double it up for
		// every background/env-visible pixel.
		//
		// faceSize<=0 here means "skybox toggled on but no HDRI actually
		// loaded/captured" - falls back to the user's configured gradient
		// (sampleFallbackBackgroundGradient(), same as the !showBackground
		// branch above), NOT flatGradientMiss()'s hardcoded placeholder sky.
		// Using flatGradientMiss() here was invisible under normal alpha-
		// blended PT display (the real raster gradient showed through this
		// alpha=0 background pixel regardless of what this function
		// returned) but became visibly wrong once RtPresenter::draw()'s
		// forceOpaque path started using this function's return value
		// directly - see that parameter's doc comment.
		return environment.faceSize > 0
			? sampleCubemapFaces(environment.faces, environment.faceSize, sampleDir)
			: sampleFallbackBackgroundGradient(environment, screenUv);
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

		// Raster's built-in/default light (lightSource.* in main_scene.frag)
		// behaves like a positional light direction with CONSTANT intensity:
		// frame.L = normalize(lightSource.position - v_position), then
		// lightIntensity = lightSource.diffuse. RtSceneSnapshot encodes that
		// legacy light with range < 0 so glTF KHR_lights_punctual point/spot
		// lights keep their physically-specified inverse-square attenuation.
		float rangeAttenuation = (light.range < 0.0f) ? 1.0f : 1.0f / (distance * distance);
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

	// Kulla-Conty single-scatter albedo recovery from a target multi-scatter
	// albedo (Kulla & Conty Estevez 2017), ported verbatim from NVIDIA's
	// vk_gltf_renderer reference implementation
	// (gltf_material_eval.h.slang:125-129's multiToSingleScatterAlbedo()) -
	// same polynomial, same per-channel application, so this codebase's
	// random walk converges toward the same appearance as that reference.
	glm::vec3 multiToSingleScatterAlbedo(const glm::vec3& rhoMs)
	{
		const glm::vec3 t = glm::vec3(4.09712f) + 4.20863f * rhoMs
			- glm::sqrt(glm::vec3(9.59217f) + 41.6808f * rhoMs + 17.7126f * rhoMs * rhoMs);
		return glm::vec3(1.0f) - t * t;
	}

	// KHR_materials_volume_scatter's per-channel extinction/scatter
	// coefficients for the free-flight random walk (see tracePixel()'s
	// hitBackface/hasVolumeScattering gate). Mirrors NVIDIA's
	// vk_gltf_renderer exactly (gltf_material_eval.h.slang:318-320):
	// attenuationColor/attenuationDistance give an "absorption-only"
	// coefficient (the same formula as calculateVolumeAttenuation()'s own
	// sigma_t, just not yet exponentiated over a distance), and the
	// scattering coefficient is ADDED on top of it (not split out of it) -
	// so a volume_scatter material's real extinction ends up higher than
	// attenuationColor alone would suggest. This is a deliberate divergence
	// from the previous closed-form BSSRDF's sigma_t usage, needed to match
	// NVIDIA's reference hue (see this feature's plan doc for the full
	// reasoning - our BSSRDF's blue/cyan vs. NVIDIA's green result).
	void computeVolumeScatterCoefficients(const glm::vec3& attenuationColor, float attenuationDistance,
		const glm::vec3& multiScatterColor, glm::vec3& outExtinction, glm::vec3& outScatterCoeff)
	{
		const glm::vec3 clampedAtten = glm::max(attenuationColor, glm::vec3(0.001f));
		const float safeDistance = std::max(attenuationDistance, 0.001f);
		const glm::vec3 absCoeff = -glm::log(clampedAtten) / safeDistance;
		const glm::vec3 singleScatterAlbedo = glm::clamp(multiToSingleScatterAlbedo(multiScatterColor), glm::vec3(0.0f), glm::vec3(1.0f));
		outScatterCoeff = absCoeff * singleScatterAlbedo;
		outExtinction = absCoeff + outScatterCoeff;
	}

	// Henyey-Greenstein phase-function sampling/pdf (standard formula, e.g.
	// PBRT's HenyeyGreenstein - not proprietary). wi is the ray's incoming
	// travel direction (NOT negated - matches NVIDIA's own convention,
	// pathtrace_functions.h.slang:623-627); cosTheta is measured between wi
	// and the sampled outgoing direction, so g>0 biases toward continuing
	// forward (dot near 1). g=0 (isotropic) degenerates exactly to uniform-
	// sphere sampling - KHR_materials_volume_scatter has no anisotropy
	// factor anywhere in this codebase's material pipeline yet (confirmed:
	// no scatterAnisotropy field in Material.h or the glTF importer), so
	// every call site below passes g=0.0f for now. Kept general so a future
	// anisotropy factor doesn't need another rewrite.
	glm::vec3 sampleHenyeyGreenstein(const glm::vec3& wi, float g, float u1, float u2)
	{
		float cosTheta;
		if (std::abs(g) < 1e-3f)
		{
			cosTheta = 1.0f - 2.0f * u1;
		}
		else
		{
			const float sqrTerm = (1.0f - g * g) / (1.0f + g - 2.0f * g * u1);
			cosTheta = (1.0f + g * g - sqrTerm * sqrTerm) / (2.0f * g);
		}
		const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
		const float phi = 2.0f * kPi * u2;
		glm::vec3 t, b;
		buildOrthonormalBasis(wi, t, b);
		return glm::normalize(t * (sinTheta * std::cos(phi)) + b * (sinTheta * std::sin(phi)) + wi * cosTheta);
	}

	float henyeyGreensteinPdf(float cosTheta, float g)
	{
		const float denom = std::max(1.0f + g * g - 2.0f * g * cosTheta, 1e-6f);
		return (1.0f - g * g) / (4.0f * kPi * denom * std::sqrt(denom));
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

	float distributionCharlieLegacy(float NdotH, float roughness)
	{
		const float invAlpha = 1.0f / std::max(roughness, 0.001f);
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
	constexpr int kSheenLUTSize = 128;
	constexpr int kSheenLUTBakeSamples = 256;

	QString resolveKhronosLUTPath(const QString& fileName)
	{
		const QString dataCandidate = QDir(PathUtils::getDataDirectory()).absoluteFilePath(
			"textures/khronos/" + fileName);
		if (QFileInfo::exists(dataCandidate))
			return dataCandidate;
		const QString sourceCandidate = QDir(QDir::currentPath()).absoluteFilePath(
			"textures/khronos/" + fileName);
		if (QFileInfo::exists(sourceCandidate))
			return sourceCandidate;
		return dataCandidate;
	}

	std::vector<float> loadKhronosScalarLUT(const QString& fileName, int channel, int lutSize)
	{
		const QString path = resolveKhronosLUTPath(fileName);
		QImage image(path);
		if (image.isNull())
			return {};

		QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
		std::vector<float> table(static_cast<size_t>(lutSize) * lutSize);
		for (int ri = 0; ri < lutSize; ++ri)
		{
			const float roughness = (ri + 0.5f) / static_cast<float>(lutSize);
			const int y = std::clamp(static_cast<int>(roughness * static_cast<float>(rgba.height() - 1)), 0, rgba.height() - 1);
			for (int vi = 0; vi < lutSize; ++vi)
			{
				const float ndotv = (vi + 0.5f) / static_cast<float>(lutSize);
				const int x = std::clamp(static_cast<int>(ndotv * static_cast<float>(rgba.width() - 1)), 0, rgba.width() - 1);
				const QRgb pixel = rgba.pixel(x, y);
				const int component = channel == 0 ? qRed(pixel) : (channel == 1 ? qGreen(pixel) : (channel == 2 ? qBlue(pixel) : qAlpha(pixel)));
				table[static_cast<size_t>(ri) * lutSize + vi] = static_cast<float>(component) / 255.0f;
			}
		}
		return table;
	}

	const std::vector<float>& sheenAlbedoLUT()
	{
		static const std::vector<float> lut = []()
		{
			std::vector<float> loaded = loadKhronosScalarLUT(QStringLiteral("lut_sheen_E.png"), 0, kSheenLUTSize);
			if (!loaded.empty())
				return loaded;

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

	float sampleScalarLUT(const std::vector<float>& lut, float NdotV, float roughness)
	{
		const float x = std::clamp(NdotV, 0.0f, 1.0f) * static_cast<float>(kSheenLUTSize - 1);
		const float y = std::clamp(roughness, 0.0f, 1.0f) * static_cast<float>(kSheenLUTSize - 1);
		const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, kSheenLUTSize - 1);
		const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, kSheenLUTSize - 1);
		const int x1 = std::min(x0 + 1, kSheenLUTSize - 1);
		const int y1 = std::min(y0 + 1, kSheenLUTSize - 1);
		const float tx = x - static_cast<float>(x0);
		const float ty = y - static_cast<float>(y0);

		const float v00 = lut[static_cast<size_t>(y0) * kSheenLUTSize + x0];
		const float v10 = lut[static_cast<size_t>(y0) * kSheenLUTSize + x1];
		const float v01 = lut[static_cast<size_t>(y1) * kSheenLUTSize + x0];
		const float v11 = lut[static_cast<size_t>(y1) * kSheenLUTSize + x1];
		return glm::mix(glm::mix(v00, v10, tx), glm::mix(v01, v11, tx), ty);
	}

	float sampleSheenAlbedoLUT(float NdotV, float roughness)
	{
		return sampleScalarLUT(sheenAlbedoLUT(), NdotV, roughness);
	}

	float radicalInverseVdC(uint32_t bits)
	{
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return static_cast<float>(bits) * 2.3283064365386963e-10f;
	}

	glm::vec3 importanceSampleCharlie(const glm::vec2& xi, float sheenRoughness)
	{
		const float alpha = std::max(sheenRoughness, 0.001f);
		const float phi = 2.0f * kPi * xi.x;
		const float sinTheta = std::pow(xi.y, alpha / (2.0f * alpha + 1.0f));
		const float cosTheta = std::sqrt(std::max(1.0f - sinTheta * sinTheta, 0.0f));
		return glm::normalize(glm::vec3(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta));
	}

	const std::vector<float>& sheenCharlieLUT()
	{
		constexpr int kCharlieBakeSamples = 1024;
		static const std::vector<float> lut = []()
		{
			std::vector<float> loaded = loadKhronosScalarLUT(QStringLiteral("lut_charlie.png"), 2, kSheenLUTSize);
			if (!loaded.empty())
				return loaded;

			std::vector<float> table(static_cast<size_t>(kSheenLUTSize) * kSheenLUTSize);
			for (int ri = 0; ri < kSheenLUTSize; ++ri)
			{
				const float roughness = (ri + 0.5f) / kSheenLUTSize;
				for (int vi = 0; vi < kSheenLUTSize; ++vi)
				{
					const float NdotV = std::max((vi + 0.5f) / kSheenLUTSize, 1e-4f);
					const glm::vec3 V(std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV)), 0.0f, NdotV);

					float sum = 0.0f;
					for (int s = 0; s < kCharlieBakeSamples; ++s)
					{
						const glm::vec2 xi((s + 0.5f) / kCharlieBakeSamples, radicalInverseVdC(static_cast<uint32_t>(s)));
						const glm::vec3 H = importanceSampleCharlie(xi, roughness);
						const glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);
						const float NdotL = std::max(L.z, 0.0f);
						const float NdotH = std::max(H.z, 0.0f);
						if (NdotL > 0.0f && NdotH > 0.0f)
							sum += distributionCharlieLegacy(NdotH, roughness) * visibilitySheen(NdotL, NdotV, roughness) * NdotL;
					}
					table[static_cast<size_t>(ri) * kSheenLUTSize + vi] = sum / kCharlieBakeSamples;
				}
			}
			return table;
		}();
		return lut;
	}

	float sampleSheenCharlieLUT(float NdotV, float roughness)
	{
		return sampleScalarLUT(sheenCharlieLUT(), NdotV, roughness);
	}

	float sampleSheenIblEnergy(float NdotV, float roughness)
	{
		return std::min(sampleSheenCharlieLUT(NdotV, roughness), sampleSheenAlbedoLUT(NdotV, roughness));
	}

	// F0/metalness alone chronically under-samples glossy dielectrics: a
	// low-roughness floor/varnish/plastic (F0 ~ 0.04, metalness 0) still
	// clamps to the 5% floor here regardless of how narrow (and therefore
	// hard to resolve via the diffuse lobe alone) its specular lobe is -
	// e.g. a roughness-0.12 floor next to a bright reflective object
	// showed essentially no visible reflection even after the full
	// maxSamples budget, because only ~3 of 64 samples/pixel ever
	// explored the specular lobe at all. Blending in (1-roughness)^2
	// pushes smooth dielectrics toward much more specular sample weight -
	// rough materials (where this term is ~0) are unaffected.
	//
	// Factored out of sampleBSDFBounce() so evaluateBsdfPdf() (used for
	// environment-NEE's MIS weighting - see tracePixel()'s environment-NEE
	// block) computes lobe-choice probabilities from the exact same formula
	// sampleBSDFBounce() actually samples from, rather than risking the two
	// silently drifting apart.
	void computeLobeProbabilities(const SurfaceParams& surf, const glm::vec3& clearcoatBlend, float& outSpecProb, float& outCoatProb)
	{
		const glm::vec3& F0 = surf.F0;
		const float smoothness = 1.0f - surf.roughness;

		// KHR_materials_anisotropy under-samples the same way smooth
		// dielectrics/clearcoat did before their own smoothness boosts
		// above/below - but for a different reason. anisoAlphaB (the sharp/
		// minor axis) is no sharper than plain roughness already implies (the
		// smoothness term above already covers it); the actual problem is
		// that VNDF-sampling a STRETCHED lobe (anisoAlphaT widened toward 1,
		// see tracePixel()'s per-hit anisoAlphaT/anisoAlphaB derivation) has
		// higher per-sample variance than an isotropic lobe of the same
		// average roughness - resolving the smeared "brushed metal" highlight
		// cleanly needs more of the total sample budget directed at this
		// lobe, not just correct importance sampling within it once chosen.
		// Reported by the user comparing AnisotropyBarnLamp's raster (a
		// smooth, noise-free swept ring from the analytic prefiltered-IBL
		// lookup) against a visibly patchier/incomplete PT highlight at
		// ordinary (32) sample counts - bumping samples alone helped some,
		// confirming this is a variance/convergence problem this boost
		// specifically targets, not a biased/wrong result. Same "only
		// redistributes samples, never changes the converged mean" property
		// as the other boosts here since the estimator divides by whichever
		// probability was actually used.
		const float anisotropyBoost = surf.anisotropyStrength * surf.anisotropyStrength;
		outSpecProb = std::clamp((F0.r + F0.g + F0.b) / 3.0f + 0.5f * surf.metalness + 0.5f * smoothness * smoothness
			+ 0.5f * anisotropyBoost, 0.05f, 0.95f);

		// Same under-sampling problem as the base specular lobe above, for
		// the same reason: clearcoatBlend alone is small at near-normal
		// viewing angles (dielectric Fresnel is weak there, ~0.04-0.05 at
		// F0), clamping coatProb to its 5% floor across most of a curved
		// surface's visible area regardless of how narrow (and therefore
		// hard to resolve via the base lobes alone) a SMOOTH coat's
		// reflection is. At 128 samples/pixel that floor means only ~6
		// samples ever explore the coat lobe at those angles - nowhere near
		// enough to resolve a detailed, non-uniformly-bright reflection
		// (e.g. a background building) there, even though the coat's own
		// pdf/MIS math (see evaluateBsdfPdf()) is otherwise correct.
		// Blending in (1-clearcoatRoughness)^2, scaled by clearcoat's own
		// strength, pushes smooth coats toward more sample weight
		// independent of the current view angle's Fresnel term - rough
		// coats (where this term is ~0) are unaffected.
		const float coatSmoothness = 1.0f - surf.clearcoatRoughness;
		outCoatProb = surf.clearcoat > 0.0f
			? std::clamp((clearcoatBlend.r + clearcoatBlend.g + clearcoatBlend.b) / 3.0f
				+ 0.5f * surf.clearcoat * coatSmoothness * coatSmoothness, 0.05f, 0.9f)
			: 0.0f;
	}

	// Heitz 2018 VNDF importance-sampling pdf (isotropic case) - the pdf
	// that sampleBSDFBounce()'s specular/coat lobes actually sample from,
	// evaluated here for an ARBITRARY given L rather than a freshly-sampled
	// one (needed for environment-NEE's MIS weighting, which must know "how
	// likely was the BSDF to have sampled this exact direction on its own").
	float ggxVndfPdfIsotropic(const glm::vec3& N, const glm::vec3& V, const glm::vec3& L, float roughness)
	{
		const float NdotV = std::max(glm::dot(N, V), 0.0f);
		const float NdotL = std::max(glm::dot(N, L), 0.0f);
		if (NdotV <= 0.0f || NdotL <= 0.0f)
			return 0.0f;

		const glm::vec3 H = glm::normalize(V + L);
		const float NdotH = std::max(glm::dot(N, H), 0.0f);
		const float VdotH = std::max(glm::dot(V, H), 0.0f);
		if (NdotH <= 0.0f || VdotH <= 0.0f)
			return 0.0f;

		const float alpha = roughness * roughness;

		// NOT distributionGGX() - that helper is direct-light-BRDF-tuned and
		// deliberately floors its denominator at 0.001 to bound the response
		// against an exactly-aligned punctual light (a real, common hazard
		// for smooth materials evaluated against a delta light). For pdf
		// purposes that floor is actively wrong: it under-estimates D right
		// where the VNDF-sampled H lands (NdotH close to 1 for a smooth/
		// mirror-like material, exactly where a genuinely large pdf is
		// correct, not a clamped small one). Using the clamped version here
		// made evaluateBsdfPdf() return an artificially tiny pdf for smooth
		// metals/mirrors, which then made the MIS balance heuristic
		// incorrectly hand most of the weight to environment-NEE instead of
		// the BSDF-sampled escape - visibly darkening/losing mirror
		// reflections of the environment. This local, unclamped copy is
		// otherwise identical to distributionGGX()'s formula.
		const float a2 = alpha * alpha;
		const float NdotH2 = NdotH * NdotH;
		const float denomTerm = NdotH2 * a2 + (1.0f - NdotH2);
		const float D = a2 / std::max(kPi * denomTerm * denomTerm, 1e-12f);

		const float G1v = smithG1GGX(NdotV, alpha);
		// pdf(H) = G1(V)*D(H)*max(VdotH,0)/NdotV; pdf(L) = pdf(H)/(4*VdotH) -
		// the reflection-operator Jacobian.
		return (G1v * D * VdotH) / std::max(NdotV, 1e-6f) / std::max(4.0f * VdotH, 1e-6f);
	}

	// Combined sampling pdf of sampleBSDFBounce() for an arbitrary given
	// direction L - used only for environment-NEE's MIS weighting (see
	// traceShadowRay()'s sibling, the environment-NEE block in
	// tracePixel()). The coat lobe gets its OWN VNDF pdf term over Ncoat/
	// clearcoatRoughness, matching sampleBSDFBounce()'s coat branch exactly
	// - an earlier version of this function folded the coat lobe into the
	// base specular term (same N/roughness) as a simplification, which
	// under-estimated the pdf badly whenever clearcoatNormalTexture tilts
	// Ncoat away from N (any bump/weave-textured clearcoat): the MIS weight
	// then wrongly favored environment-NEE over the coat's own sharp BSDF-
	// sampled reflection, visibly dulling the clearcoat highlight/normal-
	// map detail that raster shows clearly. Reported by the user comparing
	// a clearcoat-normal-mapped wicker sphere and a clear-coated red paint
	// sphere against raster.
	float evaluateBsdfPdf(const glm::vec3& N, const glm::vec3& Ncoat, const glm::vec3& V, const glm::vec3& L, const SurfaceParams& surf,
		bool hasAniso, const glm::vec3& anisoT, const glm::vec3& anisoB, float alphaT, float alphaB,
		float specProb, float coatProb)
	{
		const float NdotL = glm::dot(N, L);
		if (NdotL <= 0.0f)
			return 0.0f;

		// Must match sampleBSDFBounce()'s actual three-way split exactly:
		// coat is picked first with probability coatProb, and ONLY the
		// remainder (1-coatProb) is then further split between spec/diffuse
		// - so the base specular lobe's true probability is
		// specProb*(1-coatProb), not specProb alone. An earlier version of
		// this function used raw specProb/(1-specProb-coatProb), which
		// doesn't match sampleBSDFBounce() (and doesn't even sum to 1) for
		// any material with both clearcoat and a strong base specular
		// response, skewing the MIS weighting for both lobes together.
		const float specProbScaled = specProb * (1.0f - coatProb);
		const float diffuseProb = std::max(1.0f - coatProb - specProbScaled, 0.0f);

		const float coatPdf = coatProb > 0.0f ? ggxVndfPdfIsotropic(Ncoat, V, L, surf.clearcoatRoughness) : 0.0f;

		float specPdf;
		if (hasAniso)
		{
			const float NdotV = std::max(glm::dot(N, V), 0.0f);
			const glm::vec3 H = glm::normalize(V + L);
			const float NdotH = std::max(glm::dot(N, H), 0.0f);
			const float VdotH = std::max(glm::dot(V, H), 0.0f);
			if (NdotV <= 0.0f || NdotH <= 0.0f || VdotH <= 0.0f)
				specPdf = 0.0f;
			else
			{
				const float TdotH = glm::dot(anisoT, H), BdotH = glm::dot(anisoB, H);
				const float D = distributionGGXAnisotropic(NdotH, TdotH, BdotH, alphaT, alphaB);
				const glm::vec3 Vlocal(glm::dot(V, anisoT), glm::dot(V, anisoB), NdotV);
				const float G1v = smithG1GGXAniso(Vlocal, alphaT, alphaB);
				specPdf = (G1v * D * VdotH) / std::max(NdotV, 1e-6f) / std::max(4.0f * VdotH, 1e-6f);
			}
		}
		else
		{
			specPdf = ggxVndfPdfIsotropic(N, V, L, surf.roughness);
		}

		const float cosinePdf = NdotL / kPi;
		return coatProb * coatPdf + specProbScaled * specPdf + diffuseProb * cosinePdf;
	}

	// KHR_materials_volume_scatter's genuine free-flight random walk lives
	// further down in tracePixel() (see the hitBackface/hasVolumeScattering
	// gate) - see this feature's plan doc for why the closed-form BSSRDF
	// diffusion-profile approach that used to live here was replaced.

	// Stochastically samples one bounce direction from the BSDF (cosine-
	// weighted diffuse lobe or GGX specular lobe), returning the throughput
	// multiplier already divided by the sampling pdf and lobe-choice
	// probability (standard single-sample stochastic-lobe MC estimator).
	// Environment escapes from this sampled direction are MIS-weighted
	// against RtEnvironmentSampler's pdf using evaluateBsdfPdf() above - see
	// tracePixel()'s use site.
	// hasAniso/anisoT/anisoB/alphaT/alphaB describe the anisotropic tangent
	// frame and stretched roughness (see tracePixel()'s per-hit computation,
	// mirroring main_scene.frag's buildAnisotropyBasis()/at,ab) - when
	// hasAniso is false the base specular lobe below samples isotropically
	// exactly as before (alphaT/alphaB are unused in that case).
	// outEnvRoughness reports which lobe this call actually sampled, for
	// sampleEnvironmentSpecular()/sampleEnvironmentDiffuse()'s benefit if
	// outDir eventually escapes to the environment: >=0 means the coat or
	// base specular lobe fired (its value is that lobe's own roughness,
	// clearcoatRoughness or surf.roughness respectively), negative
	// (sentinel) means the diffuse (cosine-weighted) lobe fired - see
	// tracePixel()'s use site.
	bool sampleBSDFBounce(const glm::vec3& N, const glm::vec3& Ncoat, const glm::vec3& V, const SurfaceParams& surf,
		const glm::vec3& clearcoatBlend, bool hasAniso, const glm::vec3& anisoT, const glm::vec3& anisoB,
		float alphaT, float alphaB, Rng& rng, glm::vec3& outDir, glm::vec3& outThroughput, float& outEnvRoughness,
		bool* outTransmittedDiffuse = nullptr)
	{
		if (outTransmittedDiffuse)
			*outTransmittedDiffuse = false;

		glm::vec3 T, B;
		buildOrthonormalBasis(N, T, B);

		const glm::vec3& F0 = surf.F0;
		float specProb, coatProb;
		computeLobeProbabilities(surf, clearcoatBlend, specProb, coatProb);

		// KHR_materials_clearcoat, indirect side: unlike the direct-light
		// path (which replicates main_scene.frag's analytic mix() exactly),
		// there's no analytic prefiltered-IBL lookup available here for the
		// coat, so it's instead added as a third stochastically-selected
		// lobe alongside diffuse/specular - the standard way a layered BSDF
		// is handled in a Monte-Carlo path tracer, selected with probability
		// proportional to the same clearcoat*Fresnel weight the direct path
		// uses to blend the two layers.
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

			// Missing surf.clearcoat factor here previously left this lobe's
			// throughput representing "the coat's own full-strength Fresnel
			// reflectance", not "how much of this point is actually coated" -
			// coatProb's denominator does NOT cancel this out for partial
			// (0<clearcoat<1) values (it's clamped and has an additive
			// smoothness term, not a pure multiple of clearcoat), so at
			// clearcoat==1 this was a no-op (masking the bug for uniformly-
			// coated materials) but at intermediate values - exactly
			// KHR_materials_clearcoat's clearcoatTexture use case, e.g. glTF's
			// ClearCoatTest.gltf "Partial Coating" bands - it made the coat's
			// indirect/environment reflection come out far too strong
			// relative to the true partial coat amount, washing out the
			// intended low-to-high gradient into a uniformly glossy result.
			// main_scene.frag's own analytic clearcoatIBL() does the same
			// multiply explicitly ("return prefilteredColor * params.clearcoat
			// * params.ambientOcclusion").
			outThroughput = surf.clearcoat * F * (G2 / std::max(G1v, 1e-6f)) / coatProb;
			outDir = L;
			outEnvRoughness = surf.clearcoatRoughness;
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
			const bool polishedMetalMirrorApprox = surf.metalness >= 0.9f && surf.roughness <= 0.12f;
			if (!hasAniso && (surf.roughness <= 0.01f || polishedMetalMirrorApprox))
			{
				outDir = glm::reflect(-V, basisN);
				if (glm::dot(basisN, outDir) <= 0.0f)
					return false;
				const glm::vec3 F = applyIridescenceToFresnel(fresnelSchlick(NdotV0, F0, surf.F90), NdotV0, F0, surf);
				outThroughput = F / specProbScaled;
				outEnvRoughness = 0.0f;
				return true;
			}

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
			outEnvRoughness = surf.roughness;
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
				if (outTransmittedDiffuse)
					*outTransmittedDiffuse = true;
			}
			else
			{
				const float NdotV = std::max(glm::dot(N, V), 0.0f);
				const glm::vec3 F = applyIridescenceToFresnel(fresnelSchlick(NdotV, F0, surf.F90), NdotV, F0, surf);
				const glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - surf.metalness);
				outThroughput = (kD * surf.baseColor) / diffuseProb;
			}
			outEnvRoughness = -1.0f; // sentinel: diffuse lobe, use the irradiance map if this escapes to the environment
			return true;
		}
	}

	// Procedural radial opacity fade for the large RT floor plate - see
	// RtMaterial::hasRadialAlphaFade's doc comment for the full rationale.
	// Multiplies into the ordinary blendMode==BLEND alphaTest at every
	// alpha-aware hit site in this file (tracePixel()'s primary bounce,
	// traceShadowRay(), findGuideSurfaceThroughTransmission()'s glass peek)
	// rather than adding a separate blend-to-background code path - camera,
	// shadow, and reflection rays all then naturally pass through the faded
	// rim to whatever's really behind it (the real environment or other
	// geometry), instead of hitting a hard rectangular silhouette. Returns
	// 1.0 (no effect) for every ordinary, non-floor material.
	float radialFadeAlpha(const RtMaterial& mat, const glm::vec3& hitPosition)
	{
		if (!mat.hasRadialAlphaFade)
			return 1.0f;

		const glm::vec2 hitPlanar = mat.radialFadeUpAxisZUp
			? glm::vec2(hitPosition.x, hitPosition.y)
			: glm::vec2(hitPosition.x, hitPosition.z);
		const glm::vec2 centerPlanar = mat.radialFadeUpAxisZUp
			? glm::vec2(mat.radialFadeCenter.x, mat.radialFadeCenter.y)
			: glm::vec2(mat.radialFadeCenter.x, mat.radialFadeCenter.z);
		const float distance = glm::length(hitPlanar - centerPlanar);

		if (mat.radialFadeEndRadius <= mat.radialFadeStartRadius)
			return distance <= mat.radialFadeStartRadius ? 1.0f : 0.0f;

		const float t = std::clamp((distance - mat.radialFadeStartRadius)
			/ (mat.radialFadeEndRadius - mat.radialFadeStartRadius), 0.0f, 1.0f);
		const float smooth = t * t * (3.0f - 2.0f * t); // smoothstep, matching main_scene.frag's floor fade curve
		return 1.0f - smooth;
	}

	// NEE shadow-ray occlusion, alpha/transmission-aware. RtEmbreeScene::
	// occluded() alone is a plain any-hit test - ANY geometric intersection
	// blocks the ray, even a MASK cutout's transparent hole or a fully-
	// transmissive material (glass) - which made every light behind such a
	// surface read as fully shadowed instead of correctly lit/tinted. Walks
	// the closest-hit chain instead (bounded, since intersect() is more
	// expensive than the any-hit query it replaces), applying the SAME
	// stochastic existence tests tracePixel()'s primary-ray path already uses
	// for alpha (MASK deterministic threshold, BLEND stochastic pick) so a
	// hit that "isn't really there" for this sample doesn't block the light
	// either.
	//
	// Returns the light's remaining transmittance (0 = fully blocked, 1 =
	// fully visible), NOT a bool - a transmissive hit still reflects some
	// light per Fresnel (real glass is never 100% transmissive at every
	// angle: at grazing incidence it's almost a mirror), so treating
	// mat.transmission alone as a pass-through probability made every
	// ordinary (transmission~1.0) glass material cast literally no shadow at
	// all. reflectProb below mirrors tracePixel()'s own transmission-bounce
	// Fresnel/clamp(0.05,0.95) logic so a shadow ray blocks with roughly the
	// same probability a camera ray would reflect instead of transmit at
	// that hit - producing a dim, angle-dependent shadow rather than a
	// binary present/absent one. Transmitted light is also tinted by the
	// material's baseColor (and, for KHR_materials_volume surfaces, a rough
	// Beer-Lambert estimate using the authored thicknessFactor, the same
	// analytic approximation already used for diffuse-transmission NEE)
	// rather than passing through as pure white, so colored/stained glass
	// still casts a colored shadow. Thin-walled transmission is assumed
	// undeviated here too (same spec-mandated simplification as the primary
	// path's no-volume case), so the shadow ray continues in a straight line
	// through transmissive hits rather than refracting - an approximation
	// for volumetric surfaces that real reference renderers (DS included,
	// per this app's own research) also don't fully solve for shadow rays.
	glm::vec3 traceShadowRay(const RtEmbreeScene& scene, const RtSceneSnapshot& snapshot, RtRay ray, Rng& rng,
		int maxShadowRayHits)
	{
		glm::vec3 transmittance(1.0f);
		for (int i = 0; i < maxShadowRayHits; ++i)
		{
			const RtHit hit = scene.intersect(ray);
			if (!hit.hit)
				return transmittance; // reached the light with nothing further in the way

			if (hit.materialIndex >= snapshot.materials.size())
				return glm::vec3(0.0f); // no material data to test - conservative full occlusion

			const RtMaterial& mat = snapshot.materials[hit.materialIndex];

			// glTF material.doubleSided==false back-face culling - a
			// single-sided material's back face doesn't block light either,
			// matching tracePixel()'s identical hitBackface-gated pass-
			// through (see that call site's doc comment for the full
			// rationale/reference asset).
			bool passThrough = (!mat.twoSided) && (glm::dot(ray.direction, hit.geometricNormal) > 0.0f);
			if (!passThrough && mat.blendMode != 0) // not Opaque - see tracePixel()'s identical alphaTest block
			{
				float alphaTest = mat.opacity;
				if (mat.opacityTexture)
					alphaTest *= applyChannelPacking(sampleTexture(*mat.opacityTexture, hit.texCoords), *mat.opacityTexture);
				else if (mat.baseColorTexture)
					alphaTest *= sampleTexture(*mat.baseColorTexture, hit.texCoords).a;
				alphaTest *= radialFadeAlpha(mat, hit.position);
				alphaTest = std::clamp(alphaTest, 0.0f, 1.0f);

				passThrough = (mat.blendMode == 1) // Masked
					? (alphaTest < mat.alphaThreshold)
					: (rng.next01() >= alphaTest); // Alpha (glTF BLEND)
			}

			if (!passThrough && mat.transmission > 0.001f)
			{
				float transmissionFactor = mat.transmission;
				if (mat.transmissionTexture)
					transmissionFactor *= applyChannelPacking(sampleTexture(*mat.transmissionTexture, hit.texCoords), *mat.transmissionTexture);
				transmissionFactor = std::clamp(transmissionFactor, 0.0f, 1.0f);

				// Approximate dielectric Fresnel reflectance at this hit's
				// angle - see computeF0F90()'s f0FromIor derivation, simplified
				// to a flat-factor (non-textured) specular term since a shadow
				// ray doesn't need microfacet-accurate half-vector sampling,
				// just a representative reflect-vs-transmit split.
				const float f0FromIor = std::pow((mat.ior - 1.0f) / (mat.ior + 1.0f), 2.0f);
				const glm::vec3 dielectricF0 = glm::clamp(
					glm::vec3(f0FromIor) * mat.specularColorFactor * mat.specularFactor, glm::vec3(0.0f), glm::vec3(1.0f));
				const float NdotV = std::clamp(glm::dot(hit.geometricNormal, -ray.direction), 0.0f, 1.0f);
				const glm::vec3 fresnel = fresnelSchlick(NdotV, dielectricF0);
				const float reflectProb = std::clamp((fresnel.r + fresnel.g + fresnel.b) / 3.0f, 0.05f, 0.95f);

				passThrough = rng.next01() >= reflectProb;
				if (passThrough)
				{
					glm::vec3 tint = mat.baseColor;
					if (mat.baseColorTexture)
						tint *= glm::vec3(sampleTexture(*mat.baseColorTexture, hit.texCoords));
					if (mat.hasVolume)
						tint *= calculateVolumeAttenuation(mat.attenuationColor, mat.attenuationDistance, mat.thicknessFactor);
					transmittance *= tint * transmissionFactor;
				}
			}

			// KHR_materials_diffuse_transmission - a shadow ray hitting a
			// diffuse-transmissive surface (e.g. another leaf in a dense
			// plant) was previously treated as fully opaque here, since only
			// mat.transmission (glass/dielectric) had a pass-through branch
			// above. In practice a diffuse-transmission back-side NEE ray
			// (tracePixel()'s NdotL<=0 branch) toward the light almost always
			// clips at least one OTHER leaf instance before reaching it in a
			// densely packed model, so every leaf's transmitted glow read as
			// fully shadowed the instant Shadows was enabled - independent of
			// the Self Shadows toggle (which only excludes the ORIGINATING
			// instance, not other leaves genuinely in the ray's path).
			// Stochastic pass-through by diffuseTransmissionFactor, same
			// Russian-roulette pattern as the transmission block above (unbiased
			// over many samples) rather than a deterministic attenuation - no
			// Fresnel/IOR term needed since this isn't a refractive BTDF.
			if (!passThrough && mat.diffuseTransmissionFactor > 0.001f)
			{
				float diffuseTransFactor = mat.diffuseTransmissionFactor;
				if (mat.diffuseTransmissionTexture)
					diffuseTransFactor *= applyChannelPacking(sampleTexture(*mat.diffuseTransmissionTexture, hit.texCoords), *mat.diffuseTransmissionTexture);
				diffuseTransFactor = std::clamp(diffuseTransFactor, 0.0f, 1.0f);

				passThrough = rng.next01() < diffuseTransFactor;
				if (passThrough)
				{
					glm::vec3 tint = mat.diffuseTransmissionColor;
					if (mat.diffuseTransmissionColorTexture)
						tint *= sRGBToLinear(glm::vec3(sampleTexture(*mat.diffuseTransmissionColorTexture, hit.texCoords))); // sRGB per RtMaterial::diffuseTransmissionColorTexture's doc comment
					if (mat.hasVolume)
						tint *= calculateVolumeAttenuation(mat.attenuationColor, mat.attenuationDistance, mat.thicknessFactor);
					transmittance *= tint;
				}
			}

			if (!passThrough)
				return glm::vec3(0.0f); // genuinely blocked here

			// Continue straight through from just past this hit, shrinking the
			// remaining distance budget by how far we've already travelled.
			const float eps = selfIntersectionEpsilon(hit.position);
			ray.origin = hit.position + ray.direction * eps;
			ray.tFar -= (hit.distance + eps);
			if (ray.tFar <= 0.0f)
				return transmittance;
		}
		return glm::vec3(0.0f); // exceeded the hit budget - conservatively treat as blocked
	}

	// KHR_materials_volume_scatter's free-flight random walk NEE (see
	// tracePixel()'s hitBackface/hasVolumeScattering gate) - next-event
	// estimation from a volume-INTERIOR scatter vertex, mirroring the
	// ordinary surface NEE blocks above but with the Henyey-Greenstein
	// phase function replacing the BRDF and no NdotL/surface-cosine term
	// (there is no surface normal at a scatter point - the phase
	// function's own normalization already accounts for the full sphere
	// integral). wi is the ray's incoming travel direction at the scatter
	// point (matches sampleHenyeyGreenstein()'s convention). Punctual
	// lights use the same "delta-direction, no extra pdf division"
	// convention as evaluatePunctualLight()'s existing surface NEE use
	// (see that block above); the environment term MIS-combines the
	// env-sampling pdf against the phase pdf, mirroring the surface
	// environment-NEE block's balance-heuristic weighting exactly.
	// No self-shadow-instance mask exclusion is needed here (that toggle
	// exists so a surface doesn't shadow itself - a scatter point in open
	// space isn't on any instance's own geometry), so shadow rays here
	// always use the default all-bits mask.
	glm::vec3 sampleVolumeScatterNEE(const RtEmbreeScene& scene, const RtSceneSnapshot& snapshot,
		const RtEnvironmentSampler& envSampler, const CpuPathTracer::Settings& settings,
		const glm::vec3& scatterPos, const glm::vec3& wi, const glm::vec3& throughput, Rng& rng)
	{
		glm::vec3 result(0.0f);

		for (const RtLight& light : snapshot.lights)
		{
			glm::vec3 lightDir, lightIntensity;
			float lightDistance;
			evaluatePunctualLight(light, scatterPos, lightDir, lightIntensity, lightDistance);
			if (lightIntensity == glm::vec3(0.0f))
				continue;

			RtRay shadowRay;
			shadowRay.origin = scatterPos;
			shadowRay.direction = lightDir;
			shadowRay.tFar = lightDistance;
			glm::vec3 shadowTransmittance = snapshot.shadowsEnabled
				? traceShadowRay(scene, snapshot, shadowRay, rng, settings.maxShadowRayHits)
				: glm::vec3(1.0f);
			if (light.range < 0.0f)
				shadowTransmittance = glm::max(shadowTransmittance, glm::vec3(0.15f)); // matches surface NEE's default-light floor
			if (shadowTransmittance == glm::vec3(0.0f))
				continue;

			const float phasePdf = henyeyGreensteinPdf(glm::dot(wi, lightDir), kVolumeScatterAnisotropy);
			result += throughput * phasePdf * lightIntensity * shadowTransmittance;
		}

		if (settings.enableEnvironmentImportanceSampling && envSampler.isValid())
		{
			glm::vec3 envDir;
			float envPdf;
			envSampler.sample(rng.next01(), rng.next01(), rng.next01(), envDir, envPdf);
			if (envPdf > 0.0f)
			{
				RtRay envShadowRay;
				envShadowRay.origin = scatterPos;
				envShadowRay.direction = envDir;
				envShadowRay.tFar = 1e6f; // environment is "at infinity" - no light-distance limit
				const glm::vec3 envTransmittance = snapshot.shadowsEnabled
					? traceShadowRay(scene, snapshot, envShadowRay, rng, settings.maxShadowRayHits)
					: glm::vec3(1.0f);
				if (envTransmittance != glm::vec3(0.0f))
				{
					const float phasePdf = henyeyGreensteinPdf(glm::dot(wi, envDir), kVolumeScatterAnisotropy);
					const float misWeight = phasePdf / (phasePdf + envPdf); // balance heuristic vs. the phase-sampled bounce's own MIS half (see the miss branch above using lastBsdfSamplePdf)
					const glm::vec3 envRadiance = sampleEnvironmentMiss(snapshot.environment, envDir) * envTransmittance;
					result += throughput * (misWeight / envPdf) * phasePdf * envRadiance;
				}
			}
		}

		return result;
	}

	// Above this roughness, a transmissive surface's true appearance is
	// meant to be diffused/frosted (see tracePixel()'s sqrt(roughness)-
	// weighted diffuse-transmission blend), not a sharp undeviated view of
	// what's behind it - see findGuideSurfaceThroughTransmission()'s use
	// site for why the peek below is gated on this threshold. Confirmed via
	// the CommercialRefrigerator sample asset (frosted glass door,
	// roughnessFactor 0.75 with a spatially-varying roughness texture):
	// unconditionally peeking straight through handed OIDN a perfectly
	// sharp guide for what should be a frosted/blurred result, causing it
	// to denoise the genuine stochastic diffuse-transmission scatter back
	// into a falsely crisp image instead of a frosted one. Matches the
	// same 0.15 cutoff the old kClearTransmissionMaxRoughness threshold
	// used (removed elsewhere in this file) to distinguish clear glass
	// from frosted/diffusing glass.
	constexpr float kMaxRoughnessForSeeThroughGuide = 0.15f;

	// OIDN denoiser guide buffer for a transmissive primary hit (glass) - its
	// OWN base color/normal are useless as a guide (a window's flat tint is
	// unrelated to what's actually visible through it), so instead this peeks
	// through the surface to find what's really there. Continues an
	// undeviated straight-through ray (the same thin-wall approximation
	// already accepted elsewhere in this tracer, rather than the full
	// microfacet refraction math the main bounce loop uses - this is only a
	// denoiser hint, not rendered radiance, so it doesn't need to be
	// physically exact) through any further transmissive surfaces (stacked
	// glass) up to kMaxPeekDepth deep, until it reaches a non-transmissive
	// surface (returns true, with that surface's own guide albedo/normal -
	// recursing through ITS clearcoat blend exactly like the primary-hit
	// case) or escapes to the environment / exhausts the peek depth (returns
	// false - caller should fall back to a neutral/zero guide, same as this
	// tracer's existing pure-environment-miss handling). Only meaningful for
	// near-clear transmission - see kMaxRoughnessForSeeThroughGuide's doc
	// comment and the caller's gate on it (which also excludes hasVolume
	// materials - see that gate's comment for why a solid/volumetric
	// dielectric can't use this undeviated peek at all). Also respects
	// alphaMode MASK/BLEND on intermediate hits (mirroring tracePixel()'s
	// own alpha pass-through handling) so the guide doesn't come from
	// geometry the real ray would actually see through as if absent.
	bool findGuideSurfaceThroughTransmission(const RtEmbreeScene& scene, const RtSceneSnapshot& snapshot,
		glm::vec3 origin, const glm::vec3& direction, Rng& rng, glm::vec3& outAlbedo, glm::vec3& outNormal)
	{
		constexpr int kMaxPeekDepth = 4;
		for (int i = 0; i < kMaxPeekDepth; ++i)
		{
			RtRay peekRay;
			peekRay.origin = origin;
			peekRay.direction = direction;
			const RtHit hit = scene.intersect(peekRay);
			if (!hit.hit || hit.materialIndex >= snapshot.materials.size())
				return false;

			const RtMaterial& mat = snapshot.materials[hit.materialIndex];

			if (mat.blendMode != 0) // not Opaque - see tracePixel()'s identical alphaTest block
			{
				float alphaTest = mat.opacity;
				if (mat.opacityTexture)
					alphaTest *= applyChannelPacking(sampleTexture(*mat.opacityTexture, hit.texCoords), *mat.opacityTexture);
				else if (mat.baseColorTexture)
					alphaTest *= sampleTexture(*mat.baseColorTexture, hit.texCoords).a;
				alphaTest *= radialFadeAlpha(mat, hit.position);
				alphaTest = std::clamp(alphaTest, 0.0f, 1.0f);

				const bool passThrough = (mat.blendMode == 1) // Masked
					? (alphaTest < mat.alphaThreshold)
					: (rng.next01() >= alphaTest); // Alpha (glTF BLEND)

				if (passThrough)
				{
					origin = hit.position + direction * 1e-4f;
					continue;
				}
			}

			const SurfaceParams surf = evaluateSurface(mat, hit.texCoords, hit.vertexColor);

			if (surf.transmission > 0.001f)
			{
				origin = hit.position + direction * 1e-4f; // keep peeking straight through
				continue;
			}

			const glm::vec3 N = applyNormalMap(hit.normal, hit.tangent, hit.bitangent, mat.normalTexture.get(), hit.texCoords, mat.normalScale);
			const glm::vec3 Ncoat = mat.clearcoatNormalTexture
				? applyNormalMap(hit.normal, hit.tangent, hit.bitangent, mat.clearcoatNormalTexture.get(), hit.texCoords, mat.clearcoatNormalScale)
				: N;
			const glm::vec3 V = glm::normalize(-direction);
			const glm::vec3 clearcoatBlend = surf.clearcoat * computeClearcoatFresnel(mat.ior, Ncoat, V);

			// See tracePixel()'s identical clearcoatGuideStrength doc comment -
			// clearcoatBlend alone carries no signal for a texture-driven
			// (rather than view-angle-driven) coat/no-coat boundary.
			const float clearcoatGuideStrength = std::max(surf.clearcoat,
				std::clamp((clearcoatBlend.r + clearcoatBlend.g + clearcoatBlend.b) / 3.0f, 0.0f, 1.0f));
			outAlbedo = glm::mix(surf.baseColor, glm::vec3(1.0f), clearcoatGuideStrength);
			outNormal = mat.clearcoatNormalTexture ? glm::normalize(glm::mix(N, Ncoat, surf.clearcoat)) : N;
			return true;
		}
		return false; // gave up after kMaxPeekDepth stacked transmissive surfaces
	}

	glm::vec3 tracePixel(const RtEmbreeScene& scene, const RtSceneSnapshot& snapshot,
		const RtEnvironmentSampler& envSampler,
		const CpuPathTracer::Settings& settings, int px, int py, int width, int height, uint32_t rngSeed,
		float* outPrimaryHit = nullptr, glm::vec3* outPrimaryAlbedo = nullptr, glm::vec3* outPrimaryNormal = nullptr)
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

		// ViewportWidget::drawSkyBox() always renders the skybox through a
		// PERSPECTIVE projection (RtEnvironment::skyBoxFOV's doc comment),
		// even under an orthographic scene camera, so raster's background
		// shows per-pixel directional variation a true orthographic camera's
		// parallel rays never would. Mirror that same cheat here: the
		// directly-visible background sample (bounce == 0 only, see
		// sampleEnvironmentBackground()'s two call sites below) uses this
		// synthetic perspective direction instead of the real (constant)
		// ray.direction whenever the camera is orthographic - every other
		// use of ray.direction (geometry intersection, bounce/reflection
		// escapes, NEE) is untouched and stays physically correct.
		glm::vec3 backgroundDirection = ray.direction;
		if (cam.orthographic)
		{
			const float skyBoxTanHalfFovY = std::tan(glm::radians(snapshot.environment.skyBoxFOV) * 0.5f);
			backgroundDirection = glm::normalize(
				cam.forward +
				ndcX * cam.aspectRatio * skyBoxTanHalfFovY * cam.right +
				ndcY * skyBoxTanHalfFovY * cam.up);
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

		// kDebugVisualizeClearcoat - captured once, at the primary hit, then
		// substituted for the real radiance at the very end of this function
		// (see the final `return radiance` below) instead of returning early
		// here - an early return skipped primaryHitResolved/outPrimaryHit/
		// the OIDN guide-buffer capture for every pixel in the scene, not
		// just clearcoat ones, which broke denoising and hit-tracking
		// wholesale. x >= 0.0f means "set".
		glm::vec3 debugClearcoatColor(-1.0f);

		// kDebugVisualizeShadowTransmittance - captured once, at the primary
		// hit, same pattern as debugClearcoatColor above. Summed (not
		// averaged) across every REAL scene light's raw transmittance
		// (light.range >= 0 - the app's own always-on default/fallback light
		// is deliberately excluded here, since it dominated the sum to
		// near-white everywhere and masked what a single small, close-range
		// point light like a firefly is actually doing) - front-hemisphere
		// shadowTransmittance (BEFORE the default light's 0.15 floor clamp,
		// moot anyway since that light is excluded) for lights on the N
		// side, back-hemisphere backTransmittance (the diffuse-transmission
		// NEE term) for lights on the far side.
		glm::vec3 debugShadowTransmittanceColor(-1.0f);
		// primaryHitResolved itself flips to true a few lines before the NEE
		// loop this debug capture lives in even runs (both belong to the SAME
		// hit's shading pass) - checking !primaryHitResolved at the NEE site
		// directly would therefore never be true. Snapshotting it here, right
		// before that flip, is what kDebugVisualizeClearcoat's earlier capture
		// site gets "for free" only because it happens to sit textually before
		// the flip; this one doesn't, so it needs its own copy.
		bool isPrimaryHitForShadowDebug = false;

		// bounce tracks ordinary (opaque diffuse/specular/clearcoat/alpha-
		// pass-through) depth against settings.maxBounces, same as before.
		// transmissionDepth tracks KHR_materials_transmission reflect/
		// refract/TIR events SEPARATELY, against the much larger
		// settings.maxTransmissionBounces - see that setting's doc comment
		// for why a high-IOR dielectric's narrow total-internal-reflection
		// escape cone needs far more bounces than an ordinary opaque scene
		// budget allows, without inflating the cost of every other material.
		// MIS bookkeeping for environment-NEE (see the environment-NEE block
		// further down and RtEnvironmentSampler) - lastBsdfSamplePdf is the
		// combined BSDF sampling pdf (evaluateBsdfPdf()) at the direction
		// sampleBSDFBounce() just picked, valid ONLY for the immediately
		// following loop iteration. Reset to 0 (meaning "not BSDF-lobe-
		// sampled, don't MIS-weight") whenever the continuing ray instead
		// came from an alpha pass-through or the separate deterministic
		// transmission reflect/refract pick - neither of those directions
		// was drawn from the diffuse/specular/coat mixture this pdf models,
		// so weighting them against it would incorrectly discount light.
		float lastBsdfSamplePdf = 0.0f;

		// Which lobe sampleBSDFBounce() picked most recently, for a
		// variance-reduced environment lookup if the resulting ray escapes
		// straight to the environment - see sampleEnvironmentSpecular()/
		// sampleEnvironmentDiffuse()'s doc comments. Only consulted when
		// lastBsdfSamplePdf > 0 (i.e. the escaping direction really did come
		// from sampleBSDFBounce(), not an alpha pass-through or the
		// transmission lobe's own deterministic pick - those keep using the
		// raw/sharp map unchanged, matching existing behavior), so this
		// doesn't need resetting alongside lastBsdfSamplePdf's own resets.
		float lastBounceEnvRoughness = -1.0f;

		int bounce = 0;
		int transmissionDepth = 0;

		// KHR_materials_volume_scatter's free-flight random walk (see the
		// hitBackface/surf.hasVolumeScattering gate below) counts its
		// scatter events separately from both bounce and transmissionDepth -
		// it never eats into the ordinary surface-bounce or transmission-
		// bounce budgets, matching NVIDIA's vk_gltf_renderer reference
		// (VOLUME_FREE_BUDGET, see this feature's plan doc). Free (no
		// Russian roulette) until settings.maxVolumeScatterBounces, then RR - this
		// is the single most important fix vs. this codebase's earlier
		// random-walk attempt, which applied RR every step and killed
		// nearly every walk in dense regions before it could converge.
		int scatterBounces = 0;

		while (true)
		{
			if (bounce > settings.maxBounces)
				break; // matches the original "for (bounce=0; bounce<=maxBounces;...)" loop's exact termination point

			const RtHit meshHit = scene.intersect(ray);
			RtHit hit = meshHit;
			bool hitInfinitePlane = false;
			if (snapshot.infinitePlane.enabled
				&& (snapshot.infinitePlane.material.isShadowCatcher
					|| !snapshot.infinitePlane.material.baseColorTexture))
			{
				const glm::vec3 planeNormal = snapshot.infinitePlane.cameraUpAxisZUp
					? glm::vec3(0.0f, 0.0f, 1.0f)
					: glm::vec3(0.0f, 1.0f, 0.0f);
				const float originUp = snapshot.infinitePlane.cameraUpAxisZUp ? ray.origin.z : ray.origin.y;
				const float dirUp = snapshot.infinitePlane.cameraUpAxisZUp ? ray.direction.z : ray.direction.y;
				if (originUp > snapshot.infinitePlane.height + ray.tNear && std::abs(dirUp) > 1e-6f)
				{
					const float tPlane = (snapshot.infinitePlane.height - originUp) / dirUp;
					if (tPlane > ray.tNear && tPlane < ray.tFar
						&& (!meshHit.hit || tPlane <= meshHit.distance))
					{
						hitInfinitePlane = true;
						hit.hit = true;
						hit.distance = tPlane;
						hit.position = ray.origin + ray.direction * tPlane;
						hit.normal = planeNormal;
						hit.geometricNormal = planeNormal;
						hit.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
						hit.bitangent = snapshot.infinitePlane.cameraUpAxisZUp
							? glm::vec3(0.0f, 1.0f, 0.0f)
							: glm::vec3(0.0f, 0.0f, 1.0f);
						hit.vertexColor = glm::vec4(1.0f);
						hit.instanceIndex = snapshot.infinitePlane.instanceIndex;
						hit.materialIndex = std::numeric_limits<uint32_t>::max();
						hit.uvAreaPerWorldArea = 0.0f;
					}
				}
			}

			if (!hit.hit)
			{
				if (!primaryHitResolved)
				{
					if (outPrimaryHit)
						*outPrimaryHit = 0.0f;
					const glm::vec2 screenUv(
						(static_cast<float>(px) + 0.5f) / static_cast<float>(width),
						1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(height));
					radiance += throughput * lastHitAO * sampleEnvironmentBackground(snapshot.environment, backgroundDirection, screenUv);
				}
				else
				{
					// MIS balance heuristic against environment-NEE's own
					// sampling pdf at this same direction - see the
					// environment-NEE block below for the symmetric half of
					// this weighting. lastBsdfSamplePdf is 0 (full weight,
					// unweighted) for escapes that weren't actually BSDF-
					// lobe-sampled (alpha pass-through, transmission's own
					// deterministic pick) - envSampler.pdf() in the
					// denominator then also naturally has no NEE
					// counterpart to divide against for those.
					const float envPdfAtRay = (settings.enableEnvironmentImportanceSampling && envSampler.isValid())
						? envSampler.pdf(ray.direction) : 0.0f;
					const float misWeight = (lastBsdfSamplePdf > 0.0f && envPdfAtRay > 0.0f)
						? (lastBsdfSamplePdf / (lastBsdfSamplePdf + envPdfAtRay))
						: 1.0f;
					// Diffuse escapes use the irradiance/diffuse lookup, but
					// specular escapes keep the raw/sharp map: the outgoing
					// direction was already sampled from the GGX lobe, so a
					// roughness-prefiltered cubemap would blur the reflection
					// a second time.
					const glm::vec3 envColor = (lastBsdfSamplePdf <= 0.0f)
						? sampleEnvironmentMiss(snapshot.environment, ray.direction)
						: (lastBounceEnvRoughness < 0.0f
							? sampleEnvironmentDiffuse(snapshot.environment, ray.direction)
							: sampleEnvironmentMiss(snapshot.environment, ray.direction));
					radiance += throughput * lastHitAO * envColor * misWeight;
				}
				break;
			}

			if (kDebugVisualizeUV)
				return glm::vec3(hit.texCoords[0].x, hit.texCoords[0].y, 0.0f);

			if (!hitInfinitePlane && hit.materialIndex >= snapshot.materials.size())
				break;

			const RtMaterial& mat = hitInfinitePlane
				? snapshot.infinitePlane.material
				: snapshot.materials[hit.materialIndex];

			// Camera pixel footprint at this hit, converted to this hit's own
			// UV space via hit.uvAreaPerWorldArea (see its doc comment) -
			// computeTextureLod() then combines this with each individual
			// texture's own width/height. Deliberately restricted to primary
			// (bounce==0, transmissionDepth==0) hits only: propagating a
			// proper footprint through bounces/transmission needs full ray
			// differentials, which this tracer doesn't carry - indirect hits
			// just sample the base mip level (footprintInUvArea=0.0f),
			// matching this tracer's pre-mipmap behavior there. Primary hits
			// are what the reported minification-aliasing "smudge" artifact
			// actually affects, so this is the case that matters.
			float footprintInUvArea = 0.0f;
			if (bounce == 0 && transmissionDepth == 0 && hit.uvAreaPerWorldArea > 0.0f)
			{
				const float pixelWorldSize = cam.orthographic
					? (2.0f * cam.orthoHalfHeight / static_cast<float>(height))
					: (hit.distance * 2.0f * cam.tanHalfFovY / static_cast<float>(height));
				footprintInUvArea = hit.uvAreaPerWorldArea * pixelWorldSize * pixelWorldSize;
			}

			SurfaceParams surf = evaluateSurface(mat, hit.texCoords, hit.vertexColor, footprintInUvArea);
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
			//
			// KHR_materials_volume_scatter genuine free-flight random walk
			// (mirrors NVIDIA's vk_gltf_renderer exactly - see this feature's
			// plan doc): when the medium ALSO carries volume-scatter data,
			// replace the deterministic full-segment Beer's-law multiply
			// with a stochastic per-segment scatter-or-absorb test, using
			// this segment's real traveled distance (since prevHitPos) as
			// the free-flight bound. A scatter event never reaches this
			// hit's surface at all - it redirects the ray from a point
			// partway along the segment and `continue`s the loop, skipping
			// every bit of this iteration's surface shading below (alpha
			// test, BSDF lobes, NEE) since evaluateSurface() already ran
			// (needed to know surf.hasVolumeScattering) but nothing else
			// has yet. No absorption-only case falls through to the
			// unmodified surface-shading code path below exactly as before.
			const float segmentDistance = glm::length(hit.position - prevHitPos);
			bool volumeScattered = false;
			if (hitBackface && surf.hasVolume)
			{
				if (surf.hasVolumeScattering)
				{
					glm::vec3 extinction, scatterCoeff;
					computeVolumeScatterCoefficients(surf.attenuationColor, surf.attenuationDistance, surf.multiScatterColor, extinction, scatterCoeff);
					const float maxScatter = std::max({ scatterCoeff.r, scatterCoeff.g, scatterCoeff.b });
					if (maxScatter > kVolumeMinScatter)
					{
						const float maxExtinction = std::max({ extinction.r, extinction.g, extinction.b });
						const float scatterDist = -std::log(std::max(rng.next01(), kVolumeRandFloor)) / maxExtinction;
						if (scatterDist < segmentDistance)
						{
							throughput *= glm::vec3(1.0f) - (extinction - scatterCoeff) / maxExtinction;

							const glm::vec3 scatterPos = ray.origin + ray.direction * scatterDist;
							const glm::vec3 wi = ray.direction;
							const float u1 = rng.next01();
							const float u2 = rng.next01();
							const glm::vec3 newDir = sampleHenyeyGreenstein(wi, kVolumeScatterAnisotropy, u1, u2);

							radiance += sampleVolumeScatterNEE(scene, snapshot, envSampler, settings, scatterPos, wi, throughput, rng);

							ray.origin = scatterPos;
							ray.direction = newDir;
							lastBsdfSamplePdf = henyeyGreensteinPdf(glm::dot(wi, newDir), kVolumeScatterAnisotropy);
							prevHitPos = scatterPos;

							++scatterBounces;
							if (scatterBounces >= std::max(1, settings.maxVolumeScatterBounces))
							{
								const float rrPcont = std::clamp(std::max({ throughput.r, throughput.g, throughput.b }) + kVolumeRrFloor, 0.0f, kVolumeRrCap);
								if (rng.next01() >= rrPcont)
									break;
								throughput /= rrPcont;
							}
							volumeScattered = true;
						}
						else
						{
							throughput *= glm::exp(segmentDistance * (glm::vec3(maxExtinction) - extinction));
						}
					}
					else
					{
						throughput *= calculateVolumeAttenuation(surf.attenuationColor, surf.attenuationDistance, segmentDistance);
					}
				}
				else
				{
					throughput *= calculateVolumeAttenuation(surf.attenuationColor, surf.attenuationDistance, segmentDistance);
				}
			}
			if (volumeScattered)
				continue;
			prevHitPos = hit.position;

			glm::vec3 N = hit.normal;
			// Shading view vector. Perspective: -ray.direction already equals
			// normalize(cam.position - hit.position) exactly (ray.origin is
			// cam.position, so cam.position-hit.position is just -t*ray.
			// direction) - no divergence there. Orthographic PRIMARY rays are
			// a genuinely different case: RtRay's ortho rays are traced
			// PARALLEL (constant ray.direction=cam.forward, only the origin
			// sweeps across the view plane - see this function's ray setup
			// above), so -ray.direction is constant across the whole object,
			// whereas main_scene.frag's frame.V = normalize(cameraPos -
			// fragPos) is NOT (it has no perspective/ortho branch at all) -
			// varying per-fragment even in ortho. Using the constant vector
			// here fed BOTH the Fresnel terms AND the specular lobe's own
			// sampling frame, so the reflected-environment direction came out
			// systematically differently scaled/framed than raster - the
			// user-reported "overstretched"/mismatched ortho reflection.
			// Fixed by computing V the same surface-varying way raster does,
			// but ONLY at the true primary camera-visible hit (bounce==0,
			// no transmission pass-through yet) - every other hit is a REAL
			// traced secondary ray with its own genuine incoming direction,
			// which must stay -ray.direction (there is no "camera position"
			// a bounce ray's shading point is at a fixed relative offset from).
			const bool isPrimaryOrthoHit = cam.orthographic && bounce == 0 && transmissionDepth == 0;
			const glm::vec3 V = isPrimaryOrthoHit ? glm::normalize(cam.position - hit.position) : -ray.direction;
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
			N = applyNormalMap(N, hit.tangent, hit.bitangent, mat.normalTexture.get(), hit.texCoords, mat.normalScale);

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
			const bool hasSelfMaskableInstance = hit.instanceIndex != std::numeric_limits<uint32_t>::max();
			const uint32_t shadowRayMask = (snapshot.selfShadowsEnabled || !hasSelfMaskableInstance)
				? 0xFFFFFFFFu
				: ~(1u << (hit.instanceIndex % 32u));

			// KHR shadow-catcher floor mode (path tracer only) - a faithful
			// port of NVIDIA's vk_gltf_renderer handleShadowCatcher()
			// (pathtrace_functions.h.slang:499-554), including the ONE
			// piece earlier versions of this block substituted with a
			// same-looking-but-not-equivalent approximation: the occlusion
			// test itself. NVIDIA's sampleLights() (pathtrace_functions.h.
			// slang:357-464) picks a SINGLE direction stochastically from a
			// unified pool - 50% chance a uniformly-random punctual light,
			// 50% chance an environment-importance-sampled direction
			// (getDirectLightingTechniqueProbabilities(), confirmed from
			// the actual source) - and traces exactly one shadow ray
			// against THAT direction. This tracer's ordinary NEE instead
			// deterministically sums every light's contribution every hit;
			// reusing that convention here (an earlier version's approach)
			// silently drops the environment-occlusion half entirely, which
			// is the actual reason the shadow read as a full, long, hard-
			// edged cast (a directional light's shadow has no proximity-
			// based falloff) instead of NVIDIA's soft, contact-localized
			// look (environment-sampled directions are occluded almost
			// nowhere except very close to the model, so accumulating many
			// stochastic samples naturally fades the darkening out with
			// distance) - and why later re-adding NVIDIA's continuation
			// bounce on top of that mismatch visibly worsened the result
			// instead of looking subtle like NVIDIA's own.
			if (mat.isShadowCatcher)
			{
				// Always exclude the floor's OWN instance from its
				// shadow-catcher shadow-ray test, regardless of the global
				// Self Shadows toggle - a single flat quad can never
				// legitimately self-shadow, so any self-hit here can only
				// be numerical/grazing-angle noise (a low-angle light
				// combined with the ray-offset epsilon), which would
				// otherwise stop shadowFactor from ever reading EXACTLY
				// (1,1,1) in genuinely-unoccluded areas, permanently
				// preventing them from ever qualifying as "essentially
				// invisible" below - i.e. the floor would never truly
				// disappear anywhere, only ever get very-slightly-darkened
				// and composited at full (opaque) alpha instead.
				const uint32_t shadowCatcherRayMask = hasSelfMaskableInstance
					? (shadowRayMask & ~(1u << (hit.instanceIndex % 32u)))
					: shadowRayMask;

				const bool haveLights = !snapshot.lights.empty();
				const bool haveEnv = envSampler.isValid();
				float lightTechWeight = haveLights ? 0.5f : 0.0f;
				float envTechWeight = haveEnv ? 0.5f : 0.0f;
				const float totalTechWeight = lightTechWeight + envTechWeight;

				glm::vec3 shadowFactor(1.0f);
				if (totalTechWeight > 0.0f)
				{
					lightTechWeight /= totalTechWeight;
					envTechWeight /= totalTechWeight;

					const bool sampleLightTech = rng.next01() < lightTechWeight;
					glm::vec3 sampleDir(0.0f);
					float sampleDistance = 1e6f; // environment/unbounded default
					bool haveSampleDir = false;

					if (sampleLightTech)
					{
						const size_t lightIndex = (std::min)(
							static_cast<size_t>(rng.next01() * static_cast<float>(snapshot.lights.size())),
							snapshot.lights.size() - 1);
						const RtLight& light = snapshot.lights[lightIndex];
						glm::vec3 lightDir, lightIntensity;
						float lightDistance;
						evaluatePunctualLight(light, hit.position, lightDir, lightIntensity, lightDistance);
						if (lightIntensity != glm::vec3(0.0f))
						{
							sampleDir = lightDir;
							sampleDistance = lightDistance;
							haveSampleDir = true;
						}
					}
					else
					{
						glm::vec3 envDir;
						float envPdf;
						envSampler.sample(rng.next01(), rng.next01(), rng.next01(), envDir, envPdf);
						if (envPdf > 0.0f)
						{
							sampleDir = envDir;
							haveSampleDir = true;
						}
					}

					if (haveSampleDir && glm::dot(sampleDir, N) > 0.0f)
					{
						RtRay shadowRay;
						shadowRay.origin = hit.position + Ng * eps;
						shadowRay.direction = sampleDir;
						shadowRay.tFar = (std::max)(sampleDistance - 2.0f * eps, eps);
						shadowRay.mask = shadowCatcherRayMask;
						shadowFactor = snapshot.shadowsEnabled
							? traceShadowRay(scene, snapshot, shadowRay, rng, settings.maxShadowRayHits)
							: glm::vec3(1.0f);
					}
				}

				// Background radiance in the ray's CURRENT direction -
				// mirrors the real miss branch's own dual convention exactly
				// (pixel-perfect skybox lookup at the primary hit, MIS-
				// weighted lookup for later bounces) so an unshadowed hit is
				// indistinguishable from a genuine miss at this same bounce
				// depth.
				glm::vec3 envColor;
				if (!primaryHitResolved)
				{
					const glm::vec2 screenUv(
						(static_cast<float>(px) + 0.5f) / static_cast<float>(width),
						1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(height));
					envColor = sampleEnvironmentBackground(snapshot.environment, backgroundDirection, screenUv);
				}
				else
				{
					const float envPdfAtRayFloor = (settings.enableEnvironmentImportanceSampling && envSampler.isValid())
						? envSampler.pdf(ray.direction) : 0.0f;
					const float misWeightFloor = (lastBsdfSamplePdf > 0.0f && envPdfAtRayFloor > 0.0f)
						? (lastBsdfSamplePdf / (lastBsdfSamplePdf + envPdfAtRayFloor))
						: 1.0f;
					envColor = ((lastBsdfSamplePdf <= 0.0f)
						? sampleEnvironmentMiss(snapshot.environment, ray.direction)
						: (lastBounceEnvRoughness < 0.0f
							? sampleEnvironmentDiffuse(snapshot.environment, ray.direction)
							: sampleEnvironmentMiss(snapshot.environment, ray.direction))) * misWeightFloor;
				}

				// Literal port of handleShadowCatcher()'s own branch split
				// (pathtrace_functions.h.slang:527-535): fully-lit (all three
				// channels read EXACTLY 1 - this sample's stochastic
				// direction was genuinely unoccluded) substitutes envColor
				// alone and terminates identically to a real miss; otherwise
				// the subtractive envColor*shadowFactor - envColor*(1-
				// shadowFactor)*darkenAmount split (physically-plausible
				// partial occlusion minus the user's artistic darkening
				// dial), then continues the path.
				const bool fullyLit = shadowFactor.r >= 1.0f && shadowFactor.g >= 1.0f && shadowFactor.b >= 1.0f;
				const float shadowStrength = std::clamp(mat.shadowCatcherDarkness, 0.0f, 1.0f);
				const glm::vec3 resultColor = fullyLit
					? envColor
					: envColor * shadowFactor - envColor * (glm::vec3(1.0f) - shadowFactor) * shadowStrength;

				if (!primaryHitResolved)
				{
					if (outPrimaryHit)
						*outPrimaryHit = fullyLit ? 0.0f : 1.0f;
					if (!fullyLit)
					{
						if (outPrimaryAlbedo)
							*outPrimaryAlbedo = mat.shadowCatcherBaseColor;
						if (outPrimaryNormal)
							*outPrimaryNormal = N;
					}
				}
				radiance += throughput * lastHitAO * resultColor;
				if (fullyLit)
					break;

				// Continue the path as an ordinary BSDF bounce off the
				// shadow-catcher's own flat material (shadowCatcherBaseColor/
				// Metalness/Roughness) - mirrors NVIDIA's
				// `bsdfSampleSimple(sampleData, pbrMat); ... return true`
				// continuation exactly (pathtrace_functions.h.slang:537-553).
				// Restored now that shadowFactor itself is computed the same
				// way NVIDIA's is (single stochastic light-or-environment
				// direction, not a deterministic sum over every light) - the
				// earlier attempt at this bounce visibly worsened the result
				// specifically because it amplified the OLD shadowFactor's
				// too-broad "shadowed" region, not because the bounce itself
				// is wrong.
				SurfaceParams catcherSurf;
				catcherSurf.baseColor = mat.shadowCatcherBaseColor;
				catcherSurf.metalness = std::clamp(mat.shadowCatcherMetalness, 0.0f, 1.0f);
				catcherSurf.roughness = std::clamp(mat.shadowCatcherRoughness, 0.0001f, 1.0f);
				computeF0F90(mat, catcherSurf.baseColor, catcherSurf.metalness, 1.0f, glm::vec3(1.0f),
					catcherSurf.F0, catcherSurf.F90, catcherSurf.directF0, catcherSurf.dielectricF0, catcherSurf.dielectricDirectF0);

				glm::vec3 catcherDir, catcherThroughput;
				float catcherEnvRoughness = -1.0f;
				const float catcherAlpha = catcherSurf.roughness * catcherSurf.roughness;
				if (!sampleBSDFBounce(N, N, V, catcherSurf, glm::vec3(0.0f), false,
						glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), catcherAlpha, catcherAlpha,
						rng, catcherDir, catcherThroughput, catcherEnvRoughness))
					break;

				throughput *= catcherThroughput;
				if (throughput.r <= 0.0f && throughput.g <= 0.0f && throughput.b <= 0.0f)
					break;
				ray.origin = hit.position + Ng * eps;
				ray.direction = catcherDir;
				lastBounceEnvRoughness = catcherEnvRoughness;
				lastBsdfSamplePdf = 0.0f; // no MIS - matches the alpha-pass-through/transmission convention
				++bounce;
				continue;
			}

			// glTF material.doubleSided==false back-face culling - previously
			// unimplemented in this tracer entirely (every material rendered
			// as if double-sided, matching neither raster nor the glTF spec's
			// "When doubleSided is false, back-face culling is enabled" rule).
			// Treated exactly like an alpha-cutout pass-through below (the
			// surface "isn't there" for this hit, ray continues undeviated) -
			// hitBackface (plain dot(ray.direction, hit.geometricNormal) > 0,
			// no negative-determinant-instance correction needed - see
			// RtEmbreeScene::intersect()'s derivation) is the same test
			// already used for volume/dispersion above. Reported against
			// glTF's own NegativeScaleTest.gltf sample, whose README
			// documents this exact failure mode ("PROBLEM: No Back-Face
			// Culling") independent of the negative-scale case it's paired
			// with in that asset.
			const bool solidVolumeExitCandidate = surf.hasVolume && surf.transmission > 0.001f;
			if (!mat.twoSided && hitBackface && !solidVolumeExitCandidate)
			{
				ray.origin = hit.position - Ng * eps;
				lastBsdfSamplePdf = 0.0f;
				++bounce;
				continue;
			}

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
				alphaTest *= radialFadeAlpha(mat, hit.position);
				alphaTest = std::clamp(alphaTest, 0.0f, 1.0f);

				const bool passThrough = (mat.blendMode == 1) // Masked
					? (alphaTest < mat.alphaThreshold)
					: (rng.next01() >= alphaTest); // Alpha (glTF BLEND)

				if (passThrough)
				{
					ray.origin = hit.position - Ng * eps;
					lastBsdfSamplePdf = 0.0f; // not a BSDF-lobe-sampled direction - see its declaration
					++bounce; // counts against the ordinary budget, same as before this loop was restructured
					continue;
				}
			}

			// KHR_materials_clearcoat - the coat has its own normal (falls
			// back to the smooth pre-normal-map shading normal Nsmooth, NOT
			// the base-normal-mapped N - see main_scene.frag's
			// buildSurfaceFrame(): "frame.Ncoat = frame.Ng" before optionally
			// applying its own normal map) and a fixed ior-derived Fresnel
			// weight used to blend the coat over the base layer (see
			// composeLayeredPBR()). clearcoatBlend is 0 whenever clearcoat is
			// 0, so this is a no-op for the (common) non-clearcoat case below.
			// Computed here (rather than after the primary-hit guide-buffer
			// block below, where it used to live) since that block now needs
			// Ncoat too.
			const glm::vec3 Ncoat = applyNormalMap(Nsmooth, hit.tangent, hit.bitangent, mat.clearcoatNormalTexture.get(), hit.texCoords, mat.clearcoatNormalScale);
			const glm::vec3 clearcoatBlend = surf.clearcoat * computeClearcoatFresnel(mat.ior, Ncoat, V);

			// kDebugVisualizeClearcoat - false-colors the primary hit only:
			// red = surf.clearcoat (the raw, texture-sampled mask - should
			// show the ClearCoatTest.gltf "Partial Coating" bands crisply if
			// the texture data reaching this point is genuinely banded),
			// green = clearcoatBlend.r (the angle-dependent Fresnel weight
			// actually used everywhere below to composite the coat over the
			// base layer - expected to look almost flat across the torus
			// regardless of the texture, since Fresnel depends on view angle
			// not on surf.clearcoat's spatial pattern), blue = the mip LOD
			// actually used to sample mat.clearcoatTexture, normalized against
			// its own mip-chain length (0 = base level/sharpest, 1 = smallest/
			// blurriest mip) - a saturated blue would mean minification
			// aliasing is collapsing this texture to a single near-flat
			// averaged color despite genuinely banded source data. Captured
			// here, then substituted in for the real radiance at this
			// function's final `return radiance` below, so the ordinary
			// primary-hit bookkeeping (primaryHitResolved/outPrimaryHit/OIDN
			// guide buffers) still runs untouched for every pixel. Flip the
			// constant at the top of this file to enable.
			if (kDebugVisualizeClearcoat && !primaryHitResolved)
			{
				float lodNorm = 0.0f;
				if (mat.clearcoatTexture && mat.clearcoatTexture->mips.size() > 1)
				{
					const float lod = computeTextureLod(*mat.clearcoatTexture, footprintInUvArea);
					lodNorm = std::clamp(lod / static_cast<float>(mat.clearcoatTexture->mips.size() - 1), 0.0f, 1.0f);
				}
				debugClearcoatColor = glm::vec3(surf.clearcoat, clearcoatBlend.r, lodNorm);
			}

			// This is the first REAL hit the ray has resolved to (opaque, or
			// an alphaMode surface that survived the test above) - see
			// primaryHitResolved's declaration for why this can no longer
			// just be "bounce == 0".
			if (!primaryHitResolved)
			{
				if (outPrimaryHit)
					*outPrimaryHit = 1.0f;

				// OIDN denoiser guide buffers (albedo/normal) - see
				// RtDenoiser::denoise()'s doc comment. Non-transmissive hits
				// get their real baseColor/shading normal, same as any
				// ordinary denoiser guide-buffer setup - EXCEPT two
				// clearcoat-specific adjustments, both needed even after the
				// MIS pdf fix above made the coat's sharp reflection converge
				// correctly pre-denoise:
				//  - guide NORMAL blended toward Ncoat, but ONLY when the
				//    material actually has its own clearcoatNormalTexture
				//    (mat.clearcoatNormalTexture present) - that's the ONLY
				//    case where Ncoat carries genuinely different, useful
				//    detail (the ClearcoatWicker case: bump/weave folds
				//    visible in the highlight, smoothed away by guiding with
				//    the flat base normal instead). An earlier version of
				//    this blended by raw surf.clearcoat strength alone,
				//    unconditionally - for a material with clearcoat~1 but
				//    NO coat normal map, Ncoat is just the plain smooth
				//    Nsmooth (LESS detail than N, which carries the base
				//    normal/bump map's real per-texel detail), so that
				//    blend replaced a detailed guide with a flat one across
				//    the WHOLE surface - reported by the user as a glittery/
				//    flecked clearcoat material losing its fine sparkle
				//    pattern broadly (not just at grazing angles) once
				//    denoised, though it converged correctly pre-denoise.
				//  - guide ALBEDO blended toward neutral white by
				//    clearcoatBlend (the same Fresnel-weighted "how much of
				//    what's visible is coat vs base" factor already used to
				//    composite the coat everywhere else in this function):
				//    a dielectric clearcoat's own reflectance is close to
				//    colorless, unrelated to the (possibly very different)
				//    base color underneath it - anchoring OIDN to e.g. a
				//    flat red base albedo while a bright, near-white
				//    environment reflection sits on top (strongest exactly
				//    where Fresnel ramps up toward grazing angles) made it
				//    read that reflection as an outlier and smooth it away,
				//    even with NO clearcoat normal map involved (the plain
				//    red-paint clearcoat sphere case, which the normal-blend
				//    fix above alone didn't help since Ncoat==N there). This
				//    one stays angle-dependent (not gated on a texture being
				//    present) since it's already naturally near-zero at
				//    normal incidence and only grows toward grazing angles,
				//    where the coat genuinely does dominate what's visible.
				//  - clearcoatBlend alone is angle-dependent ONLY - it carries
				//    NO signal for a SPATIALLY-TEXTURED clearcoat factor
				//    (KHR_materials_clearcoat's clearcoatTexture, e.g. glTF's
				//    ClearCoatTest.gltf "Partial Coating" bands): Fresnel
				//    varies smoothly with view angle across a surface, so a
				//    texture-driven coat/no-coat boundary (roughly constant
				//    view angle across it) produces almost no clearcoatBlend
				//    difference between the two sides, even though the real
				//    coated-vs-uncoated appearance differs sharply (correctly,
				//    pre-denoise - the bands are visible at any sample count,
				//    ruling out under-sampling). OIDN then reads the texture's
				//    own genuine pattern as noise and blurs the bands away
				//    entirely. clearcoatGuideStrength below takes the MAX of
				//    the existing angle-driven signal and a direct
				//    surf.clearcoat-driven floor, so a textured coat mask gets
				//    a guide-albedo difference regardless of view angle.
				//
				// A transmissive primary hit (glass) can't use ITS OWN
				// baseColor/normal here - a window's flat tint is unrelated
				// to what's actually visible through it - so instead
				// findGuideSurfaceThroughTransmission() peeks through the
				// surface for the real guide values (falling back to
				// neutral/zero, matching this tracer's existing pure-miss
				// handling, if it escapes to the environment or the peek
				// depth runs out).
				if (outPrimaryAlbedo || outPrimaryNormal)
				{
					glm::vec3 throughAlbedo(0.0f), throughNormal(0.0f);
					const bool haveThroughGuide = (surf.transmission > 0.001f) &&
						(surf.roughness <= kMaxRoughnessForSeeThroughGuide) &&
						!surf.hasVolume && // solid/volumetric dielectrics (spheres, lenses, the
						                   // IOR-cube/dragon test scenes) bend the real refracted
						                   // path by Snell's law - an undeviated straight-through
						                   // peek gives OIDN a guide from the WRONG background/
						                   // object entirely, causing it to denoise the correctly-
						                   // refracted image toward irrelevant structure (the
						                   // smudged/blurred regression Codex's review caught).
						                   // Thin-walled transmission (no volume) has no such bend,
						                   // so the straight-through peek stays valid there.
						findGuideSurfaceThroughTransmission(scene, snapshot, hit.position + ray.direction * 1e-4f,
							ray.direction, rng, throughAlbedo, throughNormal);

					if (outPrimaryAlbedo)
					{
						const float clearcoatGuideStrength = std::max(surf.clearcoat,
							std::clamp((clearcoatBlend.r + clearcoatBlend.g + clearcoatBlend.b) / 3.0f, 0.0f, 1.0f));
						*outPrimaryAlbedo = (surf.transmission > 0.001f)
							? throughAlbedo // zero if haveThroughGuide is false, matching prior neutral fallback
							: glm::mix(surf.baseColor, glm::vec3(1.0f), clearcoatGuideStrength);
					}
					if (outPrimaryNormal)
					{
						const glm::vec3 guideNormal = mat.clearcoatNormalTexture
							? glm::normalize(glm::mix(N, Ncoat, surf.clearcoat))
							: N;
						*outPrimaryNormal = (surf.transmission > 0.001f) ? throughNormal : guideNormal;
					}
				}

				isPrimaryHitForShadowDebug = !primaryHitResolved;
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

			radiance += throughput * surf.emissive * (glm::vec3(1.0f) - clearcoatBlend);

			// KHR_materials_sheen's base-layer INDIRECT energy-compensation
			// dampening (main_scene.frag's "iblSheenScaling") - computed
			// alongside the sheen env term below when sheen is present,
			// applied to the continuing bounce's throughput near this
			// function's final sampleBSDFBounce() call. 1.0 (no-op) for the
			// common non-sheen case.
			float sheenIndirectDampening = 1.0f;

			// KHR_materials_sheen, indirect/environment side. Raster evaluates
			// this as a split-sum lookup into sheenPrefilterMap multiplied by
			// min(charlieLUT.b, sheenELUT.r). Reuse the path tracer's existing
			// prefiltered environment chain here rather than a raw stochastic
			// cone: it is not a perfect Charlie prefilter, but it is much
			// closer to raster's stable prefiltered IBL than repeatedly adding
			// bright raw environment samples as a separate sheen overlay.
			if (surf.sheenColor != glm::vec3(0.0f))
			{
				const float sheenStrengthForIBL = std::max({ surf.sheenColor.r, surf.sheenColor.g, surf.sheenColor.b });
				const glm::vec3 R = glm::reflect(-V, N);

				const float sheenRoughFinal = std::clamp(surf.sheenRoughness, 0.0001f, 1.0f);
				const glm::vec3 envSheen = sampleEnvironmentSheen(snapshot.environment, R, sheenRoughFinal);

				const float NdotV_sheen = std::clamp(glm::dot(N, V), 0.0f, 1.0f);
				const float E_sheen = sampleSheenIblEnergy(NdotV_sheen, sheenRoughFinal);
				radiance += throughput * surf.ao * surf.sheenColor * envSheen * E_sheen;

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
						const glm::vec3 backTransmittance = snapshot.shadowsEnabled
							? traceShadowRay(scene, snapshot, backShadowRay, rng, settings.maxShadowRayHits)
							: glm::vec3(1.0f);

						if (kDebugVisualizeShadowTransmittance && isPrimaryHitForShadowDebug && light.range >= 0.0f)
						{
							if (debugShadowTransmittanceColor.x < 0.0f)
								debugShadowTransmittanceColor = glm::vec3(0.0f);
							debugShadowTransmittanceColor += backTransmittance;
						}

						if (backTransmittance != glm::vec3(0.0f))
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
							radiance += throughput * diffuseBTDF * lightIntensity * backTransmittance;
						}
					}
					continue;
				}

				RtRay shadowRay;
				shadowRay.origin = hit.position + Ng * eps;
				shadowRay.direction = lightDir;
				shadowRay.tFar = lightDistance - 2.0f * eps;
				shadowRay.mask = shadowRayMask;
				glm::vec3 shadowTransmittance = snapshot.shadowsEnabled
					? traceShadowRay(scene, snapshot, shadowRay, rng, settings.maxShadowRayHits)
					: glm::vec3(1.0f);

				if (kDebugVisualizeShadowTransmittance && isPrimaryHitForShadowDebug && light.range >= 0.0f)
				{
					if (debugShadowTransmittanceColor.x < 0.0f)
						debugShadowTransmittanceColor = glm::vec3(0.0f);
					debugShadowTransmittanceColor += shadowTransmittance;
				}

				if (light.range < 0.0f)
				{
					// Raster clamps the default light's shadow factor to 0.85
					// (main_scene.frag), so even fully shadowed default-light
					// direct terms retain 15%. Keep real glTF punctual lights
					// physically hard/colored through traceShadowRay().
					shadowTransmittance = glm::max(shadowTransmittance, glm::vec3(0.15f));
				}
				if (shadowTransmittance == glm::vec3(0.0f))
					continue;
				lightIntensity *= shadowTransmittance;

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

			// Environment NEE - next-event estimation directly against the
			// loaded HDRI, treated as an area light "at infinity" (see
			// RtEnvironmentSampler and CpuPathTracer.h). Without this, a
			// bright but small/sharp environment feature (a sun disk, a
			// window) only ever gets lit by BSDF-sampled bounces stumbling
			// into it by chance, which converges extremely slowly. MIS-
			// combined via the balance heuristic with the symmetric
			// weighting applied where a BSDF-sampled bounce escapes to the
			// environment (see the miss branch above using
			// lastBsdfSamplePdf) - each direction's total weight is exactly
			// 1 regardless of which technique(s) could have produced it, so
			// this never double-counts the environment's contribution.
			// Skipped for transmissive materials (surf.transmission has its
			// own dedicated deterministic reflect/refract handling below,
			// entirely bypassing the diffuse/specular/coat mixture this
			// pdf models - see evaluateBsdfPdf()'s doc comment).
			if (settings.enableEnvironmentImportanceSampling && envSampler.isValid() && surf.transmission <= 0.001f)
			{
				glm::vec3 envDir;
				float envPdf;
				envSampler.sample(rng.next01(), rng.next01(), rng.next01(), envDir, envPdf);

				const float NdotLEnv = glm::dot(N, envDir);
				if (envPdf > 0.0f && NdotLEnv > 0.0f)
				{
					RtRay envShadowRay;
					envShadowRay.origin = hit.position + Ng * eps;
					envShadowRay.direction = envDir;
					envShadowRay.tFar = 1e6f; // environment is "at infinity" - no light-distance limit
					envShadowRay.mask = shadowRayMask;
					const glm::vec3 envTransmittance = snapshot.shadowsEnabled
						? traceShadowRay(scene, snapshot, envShadowRay, rng, settings.maxShadowRayHits)
						: glm::vec3(1.0f);

					if (envTransmittance != glm::vec3(0.0f))
					{
						float specProb, coatProb;
						computeLobeProbabilities(surf, clearcoatBlend, specProb, coatProb);
						const float bsdfPdf = evaluateBsdfPdf(N, Ncoat, V, envDir, surf, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB, specProb, coatProb);
						const float misWeight = envPdf / (envPdf + bsdfPdf);

						const glm::vec3 envRadiance = sampleEnvironmentMiss(snapshot.environment, envDir) * envTransmittance;
						glm::vec3 envDirect = evaluateDirectBRDF(N, V, envDir, surf, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB) * envRadiance;

						if (surf.sheenColor != glm::vec3(0.0f))
						{
							const float sheenStrength = std::max({ surf.sheenColor.r, surf.sheenColor.g, surf.sheenColor.b });
							const float NdotVSheen = std::clamp(glm::dot(N, V), 0.0f, 1.0f);
							const float albedoSheenScaling =
								1.0f - sheenStrength * sampleSheenIblEnergy(NdotVSheen, surf.sheenRoughness);
							envDirect *= albedoSheenScaling;
						}

						// surf.ao - AO only ever darkens indirect/ambient
						// contributions in this tracer (see lastHitAO's doc
						// comment and the sheen-env term above, which already
						// applies it the same way): env-NEE IS this hit's
						// ambient/IBL contribution, just resolved via direct
						// importance sampling instead of a BSDF-escape miss,
						// so it needs the same AO scaling those get - an
						// earlier version of this block omitted it entirely.
						const glm::vec3 weightedEnv = throughput * surf.ao * (misWeight / envPdf);
						if (surf.clearcoat > 0.0f)
						{
							const glm::vec3 coatDirect = evaluateClearcoatDirect(Ncoat, V, envDir, surf.clearcoat, surf.clearcoatRoughness) * envRadiance;
							radiance += weightedEnv * glm::mix(envDirect, coatDirect, clearcoatBlend);
						}
						else
						{
							radiance += weightedEnv * envDirect;
						}
					}
				}
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
				// This branch's continuing ray is a deterministic Fresnel
				// reflect/refract pick, not a diffuse/specular/coat mixture
				// sample - see lastBsdfSamplePdf's declaration.
				lastBsdfSamplePdf = 0.0f;

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

			bool transmittedDiffuse = false;
			if (!sampleBSDFBounce(N, Ncoat, V, surf, clearcoatBlend, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB, rng, bounceDir, bounceThroughput, lastBounceEnvRoughness, &transmittedDiffuse))
				break;

			// KHR_materials_volume_scatter: entry into the medium is just the
			// ordinary diffuse_transmission back-hemisphere lobe
			// sampleBSDFBounce() already picked above (bounceDir/
			// bounceThroughput, unmodified) - no special-casing needed here.
			// The genuine free-flight random walk happens on SUBSEQUENT hits,
			// at the hitBackface/surf.hasVolumeScattering gate further up
			// this loop, which decides per-segment whether the ray scatters
			// before reaching the next surface. (transmittedDiffuse is kept
			// as a local for symmetry with sampleBSDFBounce()'s signature but
			// no longer branches on it here - see this feature's plan doc for
			// why the previous BSSRDF-substitution approach that lived here
			// was replaced.)

			// Stash this bounce's combined sampling pdf for the miss branch
			// at the top of the next iteration (see lastBsdfSamplePdf's
			// declaration) - MIS-weights this direction against
			// environment-NEE if it turns out to escape straight to the
			// environment.
			//
			// lastBounceEnvRoughness==0.0f (an EXACT literal, only ever
			// written by sampleBSDFBounce()'s polished-metal-mirror shortcut -
			// every other lobe reports a floored, always-nonzero roughness:
			// surf.roughness/surf.clearcoatRoughness are both clamped to a
			// 0.0001 minimum in evaluateSurface(), and the diffuse lobe uses
			// a negative sentinel instead) signals that this bounce is a
			// deterministic near-delta reflection, not a real finite-width
			// GGX sample. MIS's balance heuristic is meaningless for a delta
			// BSDF: NEE can (almost) never importance-sample the exact mirror
			// direction, so it contributes ~0 regardless, while the
			// bsdf-escape channel's pdf/(pdf+envPdf) weight would get
			// discounted by envPdfAtRay's mere PRESENCE at that direction -
			// not just noisily, but systematically (the mirror direction is
			// fixed per hit, so the same discount applies every sample). The
			// two channels then don't actually complement each other, and
			// energy is lost for good - this is a genuine bias, not variance
			// that more samples would resolve. Skipping MIS here (full
			// weight, matching the transmission/alpha-passthrough branches'
			// identical lastBsdfSamplePdf=0.0f convention) is what an
			// unbiased renderer must do for delta/near-delta BSDFs - this was
			// the actual cause of polished, highly-metallic spheres (e.g.
			// glTF's MetalRoughSpheresNoTextures) rendering with a dark body
			// and only sparse bright specks instead of a bright mirror-like
			// environment reflection; an earlier attempt at this fix instead
			// patched evaluateBsdfPdf()'s own GGX-D numerical stability
			// (still worth keeping - see its own comment - but insufficient
			// on its own, since the deeper issue is evaluating a "pdf" for a
			// direction that isn't really sampled from a continuous
			// distribution at all).
			if (lastBounceEnvRoughness == 0.0f)
			{
				lastBsdfSamplePdf = 0.0f;
			}
			else
			{
				float specProbForPdf, coatProbForPdf;
				computeLobeProbabilities(surf, clearcoatBlend, specProbForPdf, coatProbForPdf);
				lastBsdfSamplePdf = evaluateBsdfPdf(N, Ncoat, V, bounceDir, surf, hasAniso, anisoT, anisoB, anisoAlphaT, anisoAlphaB, specProbForPdf, coatProbForPdf);
			}

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

		// kDebugVisualizeVolumeScatterBounces - same heat-ramp pattern as
		// kDebugVisualizeTransmissionBounceCount above, over scatterBounces'
		// final value, normalized against kVolumeScatterDebugRampCap (a
		// separate, smaller cap than settings.maxVolumeScatterBounces - chosen purely
		// for a visually useful range).
		if (kDebugVisualizeVolumeScatterBounces && scatterBounces > 0)
		{
			const float t = std::clamp(static_cast<float>(scatterBounces) / kVolumeScatterDebugRampCap, 0.0f, 1.0f);
			if (t < 0.25f)
				return glm::mix(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), t / 0.25f);
			if (t < 0.5f)
				return glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), (t - 0.25f) / 0.25f);
			if (t < 0.75f)
				return glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), (t - 0.5f) / 0.25f);
			return glm::mix(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), (t - 0.75f) / 0.25f);
		}

		// A NaN/Inf channel (rare, but reachable through certain transmission/
		// volume/SSS edge cases - e.g. a near-zero-probability division in
		// the Fresnel/TIR or free-flight extinction math) must be caught
		// BEFORE the firefly clamp below, not by it: any comparison against
		// NaN is false, so "maxChannel > threshold" silently lets a NaN
		// sample straight through unclamped, and "threshold / NaN" would
		// still be NaN even if it didn't. That NaN then persists forever in
		// the running-mean accumulation buffer (a NaN blended with anything
		// stays NaN) and corrupts the denoiser's guide/color input - the
		// reported "denoiser sometimes hangs/goes into an infinite loop"
		// symptom is almost certainly this: OIDN and the native OptiX
		// denoiser were never designed to receive non-finite pixels and can
		// behave pathologically (up to and including appearing to hang) once
		// they do. Treat a non-finite sample as contributing nothing, same
		// as a firefly clamp already discards excess energy.
		if (!std::isfinite(radiance.r) || !std::isfinite(radiance.g) || !std::isfinite(radiance.b))
		{
			radiance = glm::vec3(0.0f);
		}
		else
		{
			// Firefly/outlier suppression - see Settings::fireflyClampThreshold's
			// doc comment. Scales the whole sample down proportionally (rather
			// than clamping each channel independently) so an extreme-value
			// sample is dimmed without shifting its hue.
			const float maxChannel = std::max({ radiance.r, radiance.g, radiance.b });
			if (maxChannel > settings.fireflyClampThreshold && maxChannel > 0.0f)
				radiance *= (settings.fireflyClampThreshold / maxChannel);
		}

		if (kDebugVisualizeClearcoat && debugClearcoatColor.x >= 0.0f)
			return debugClearcoatColor;

		if (kDebugVisualizeShadowTransmittance && debugShadowTransmittanceColor.x >= 0.0f)
			return debugShadowTransmittanceColor;

		return radiance;
	}
}

void CpuPathTracer::renderPass(
	const RtEmbreeScene& scene,
	const RtSceneSnapshot& snapshot,
	const RtEnvironmentSampler& envSampler,
	int width, int height,
	uint32_t sampleSeed,
	std::vector<glm::vec3>& outRadiance,
	const std::atomic<bool>* cancelFlag,
	std::vector<float>* outPrimaryHitMask,
	std::vector<glm::vec3>* outPrimaryAlbedo,
	std::vector<glm::vec3>* outPrimaryNormal) const
{
	if (width <= 0 || height <= 0)
	{
		outRadiance.clear();
		if (outPrimaryHitMask) outPrimaryHitMask->clear();
		if (outPrimaryAlbedo) outPrimaryAlbedo->clear();
		if (outPrimaryNormal) outPrimaryNormal->clear();
		return;
	}

	outRadiance.assign(static_cast<size_t>(width) * height, glm::vec3(0.0f));
	if (outPrimaryHitMask)
		outPrimaryHitMask->assign(static_cast<size_t>(width) * height, 0.0f);
	if (outPrimaryAlbedo)
		outPrimaryAlbedo->assign(static_cast<size_t>(width) * height, glm::vec3(0.0f));
	if (outPrimaryNormal)
		outPrimaryNormal->assign(static_cast<size_t>(width) * height, glm::vec3(0.0f));

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
				float primaryHit = 0.0f;
				glm::vec3 primaryAlbedo(0.0f), primaryNormal(0.0f);
				outRadiance[static_cast<size_t>(y) * width + x] =
					tracePixel(scene, snapshot, envSampler, _settings, x, y, width, height, sampleSeed,
						outPrimaryHitMask ? &primaryHit : nullptr,
						outPrimaryAlbedo ? &primaryAlbedo : nullptr,
						outPrimaryNormal ? &primaryNormal : nullptr);
				if (outPrimaryHitMask)
					(*outPrimaryHitMask)[static_cast<size_t>(y) * width + x] = primaryHit;
				if (outPrimaryAlbedo)
					(*outPrimaryAlbedo)[static_cast<size_t>(y) * width + x] = primaryAlbedo;
				if (outPrimaryNormal)
					(*outPrimaryNormal)[static_cast<size_t>(y) * width + x] = primaryNormal;
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
