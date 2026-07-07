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
	// Heitz 2018, "Sampling the GGX Distribution of Visible Normals" (JCGT),
	// isotropic case. Ve is the view direction in local tangent space (N=+Z),
	// must have Ve.z > 0. Returns the sampled half-vector, also in local
	// tangent space. Lower variance than plain GGX half-vector sampling
	// because it accounts for microfacet visibility (RayTrophiStudio's
	// closesthit.rchit uses the same method for its metallic GGX lobe).
	glm::vec3 sampleGGXVNDF(const glm::vec3& Ve, float alpha, float u1, float u2)
	{
		const glm::vec3 Vh = glm::normalize(glm::vec3(alpha * Ve.x, alpha * Ve.y, Ve.z));

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

		return glm::normalize(glm::vec3(alpha * Nh.x, alpha * Nh.y, std::max(0.0f, Nh.z)));
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

		// Ambient occlusion - only ever multiplied into indirect/environment
		// contributions (see main_scene.frag: applied to diffuseIBLOut/
		// specularIBLOut/envColor, never to direct-light terms), not general
		// surface darkening. See where this is consumed in tracePixel().
		float ao = 1.0f;
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

		return s;
	}

	// Ported from calcBumpedNormal() in main_scene.frag, minus its screen-
	// space-derivative fallback (dFdx/dFdy have no equivalent per-ray in a
	// path tracer) - when the mesh has no tangent data, this just returns N
	// unchanged rather than attempting a derivative-based tangent frame.
	glm::vec3 applyNormalMap(const glm::vec3& N, const glm::vec3& rawTangent, const glm::vec3& rawBitangent,
		const RtMaterial& mat, const glm::vec2 (&texCoords)[4])
	{
		if (!mat.normalTexture)
			return N;
		if (glm::length(rawTangent) <= 0.01f)
			return N; // no tangent data (matches the shader's own hasTangents check)

		glm::vec3 T = glm::normalize(rawTangent - glm::dot(rawTangent, N) * N);
		glm::vec3 B = glm::normalize(rawBitangent - glm::dot(rawBitangent, N) * N);

		// Ensure T, B, N form a right-handed basis; if not, flip B.
		const float handedness = glm::dot(glm::cross(T, B), N);
		if (handedness < 0.0f)
			B = -B;

		const glm::vec4 sampled = sampleTexture(*mat.normalTexture, texCoords);
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
		return sampleCubemapFaces(environment.faces, environment.faceSize, direction);
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
	glm::vec3 evaluateDirectBRDF(const glm::vec3& N, const glm::vec3& V, const glm::vec3& L,
		const SurfaceParams& surf)
	{
		const float NdotL = std::max(glm::dot(N, L), 0.0f);
		const float NdotV = std::max(glm::dot(N, V), 0.0f);
		if (NdotL <= 0.0f || NdotV <= 0.0f)
			return glm::vec3(0.0f);

		const glm::vec3 H = glm::normalize(V + L);
		const float NdotH = std::max(glm::dot(N, H), 0.0f);
		const float VdotH = std::clamp(glm::dot(H, V), 0.0f, 1.0f);

		const glm::vec3 dielectricF0(0.04f);
		const glm::vec3 F0 = glm::mix(dielectricF0, surf.baseColor, surf.metalness);

		const float D = distributionGGX(NdotH, surf.roughness);
		const float G = geometrySmith(NdotV, NdotL, surf.roughness);
		const glm::vec3 F = fresnelSchlick(VdotH, F0);

		const glm::vec3 specular = (D * G * F) / std::max(4.0f * NdotV * NdotL, 0.001f);
		const glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - surf.metalness);
		const glm::vec3 diffuse = kD * surf.baseColor / kPi;

		return (diffuse + specular) * NdotL;
	}

	// Stochastically samples one bounce direction from the BSDF (cosine-
	// weighted diffuse lobe or GGX specular lobe), returning the throughput
	// multiplier already divided by the sampling pdf and lobe-choice
	// probability (standard single-sample stochastic-lobe MC estimator - see
	// CpuPathTracer.h for why full MIS between NEE and BSDF sampling is not
	// implemented in v1).
	bool sampleBSDFBounce(const glm::vec3& N, const glm::vec3& V, const SurfaceParams& surf,
		Rng& rng, glm::vec3& outDir, glm::vec3& outThroughput)
	{
		glm::vec3 T, B;
		buildOrthonormalBasis(N, T, B);

		const glm::vec3 dielectricF0(0.04f);
		const glm::vec3 F0 = glm::mix(dielectricF0, surf.baseColor, surf.metalness);
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

		const float lobeXi = rng.next01();
		const float u1 = rng.next01();
		const float u2 = rng.next01();

		if (lobeXi < specProb)
		{
			// GGX specular lobe via VNDF importance sampling (Heitz 2018 - see
			// sampleGGXVNDF). Ve must be expressed in the local tangent frame
			// (N = +Z) for the algorithm as published.
			const float alpha = surf.roughness * surf.roughness;
			const float NdotV0 = std::max(glm::dot(N, V), 1e-4f);
			const glm::vec3 Ve(glm::dot(V, T), glm::dot(V, B), NdotV0);

			const glm::vec3 hLocal = sampleGGXVNDF(Ve, alpha, u1, u2);
			const glm::vec3 H = localToWorld(hLocal, N, T, B);
			const glm::vec3 L = glm::reflect(-V, H);

			const float NdotL = glm::dot(N, L);
			const float NdotV = glm::dot(N, V);
			if (NdotL <= 0.0f || NdotV <= 0.0f)
				return false;

			const float VdotH = std::clamp(glm::dot(H, V), 0.0f, 1.0f);
			const glm::vec3 F = fresnelSchlick(VdotH, F0);

			// VNDF sampling's throughput (BRDF(L)*NdotL/pdf(L)) simplifies to
			// F * G2/G1 - the standard result that makes VNDF sampling not
			// need D or NdotH at all in the final weight (see Heitz 2018 sec
			// 3.4, or RayTrophiStudio's ggxSampleVNDF/"VNDF weight = F*G1(L)"
			// comment for the same derivation using the non-height-correlated
			// form). G1/G2 here must be the pair the VNDF pdf was derived
			// from - NOT geometrySmith() above, which is the raster-matched
			// direct-lighting remapping used for NEE's BRDF value instead.
			const float G1v = smithG1GGX(NdotV, alpha);
			const float G2  = smithG2HeightCorrelatedGGX(NdotV, NdotL, alpha);

			outThroughput = F * (G2 / std::max(G1v, 1e-6f)) / specProb;
			outDir = L;
			return true;
		}
		else
		{
			// Cosine-weighted diffuse lobe: BRDF(L)*NdotL/pdf(L) = kD*baseColor.
			const glm::vec3 local = cosineSampleHemisphere(u1, u2);
			outDir = localToWorld(local, N, T, B);

			const float NdotV = std::max(glm::dot(N, V), 0.0f);
			const glm::vec3 F = fresnelSchlick(NdotV, F0);
			const glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - surf.metalness);

			outThroughput = (kD * surf.baseColor) / (1.0f - specProb);
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

		for (int bounce = 0; bounce <= settings.maxBounces; ++bounce)
		{
			const RtHit hit = scene.intersect(ray);
			if (!hit.hit)
			{
				if (bounce == 0)
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
			if (bounce == 0 && outPrimaryHit)
				*outPrimaryHit = true;

			if (settings.debugVisualizeUV)
				return glm::vec3(hit.texCoords[0].x, hit.texCoords[0].y, 0.0f);

			if (hit.materialIndex >= snapshot.materials.size())
				break;
			const RtMaterial& mat = snapshot.materials[hit.materialIndex];
			const SurfaceParams surf = evaluateSurface(mat, hit.texCoords, hit.vertexColor);
			lastHitAO = surf.ao;

			radiance += throughput * surf.emissive;

			glm::vec3 N = hit.normal;
			const glm::vec3 V = -ray.direction;
			if (glm::dot(N, V) < 0.0f)
				N = -N; // shade the side the ray actually hit (thin/backfacing geometry)

			N = applyNormalMap(N, hit.tangent, hit.bitangent, mat, hit.texCoords);

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
			const float eps = selfIntersectionEpsilon(hit.position);

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
					continue;

				RtRay shadowRay;
				shadowRay.origin = hit.position + Ng * eps;
				shadowRay.direction = lightDir;
				shadowRay.tFar = lightDistance - 2.0f * eps;
				if (scene.occluded(shadowRay))
					continue;

				radiance += throughput * evaluateDirectBRDF(N, V, lightDir, surf) * lightIntensity;
			}

			// Russian roulette termination.
			if (bounce >= settings.russianRouletteStartDepth)
			{
				const float p = std::clamp(std::max({ throughput.r, throughput.g, throughput.b }), 0.05f, 1.0f);
				if (rng.next01() > p)
					break;
				throughput /= p;
			}

			glm::vec3 bounceDir, bounceThroughput;
			if (!sampleBSDFBounce(N, V, surf, rng, bounceDir, bounceThroughput))
				break;

			throughput *= bounceThroughput;
			if (throughput.r <= 0.0f && throughput.g <= 0.0f && throughput.b <= 0.0f)
				break;

			ray.origin = hit.position + Ng * eps;
			ray.direction = bounceDir;
		}

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
