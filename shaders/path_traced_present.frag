#version 450 core

uniform sampler2D pathTracedTexture;
uniform vec2 resolution;

// Mirrors main_scene.frag's applyToneMapping() uniforms/convention exactly
// (see that file's hdrToneMapping/gammaCorrection/screenGamma/iblExposure/
// toneMapMode) - this presentation pass used to hardcode ACES-Narkowicz +
// a fixed 2.2 gamma unconditionally, which visibly mismatched raster
// whenever the user had a different HDRToneMapMode/exposure selected (a
// known, previously-deferred gap - see git history for this file). Reading
// the SAME live settings raster uses keeps a PT-vs-raster comparison an
// apples-to-apples contrast/exposure comparison, not two different
// tonemap curves layered on top of otherwise-identical radiance.
uniform bool  hdrToneMapping = true;
uniform bool  gammaCorrection = true;
uniform float screenGamma = 2.2;
uniform float iblExposure = 1.0;
uniform int   toneMapMode = 0; // 0=KhronosPbrNeutral 1=ACES_Narkowicz 2=ACES_Hill 3=ACES_Hill_ExposureBoost 4=Uncharted2 5=Reinhard

out vec4 FragColor;

// ---- Tonemap operators, ported verbatim from main_scene.frag so path-traced
// presentation can match whichever one raster is currently configured to use ----

const mat3 ACESInputMat = mat3
(
	0.59719, 0.07600, 0.02840,
	0.35458, 0.90834, 0.13383,
	0.04823, 0.01566, 0.83777
);

const mat3 ACESOutputMat = mat3
(
	1.60475, -0.10208, -0.00327,
	-0.53108, 1.10813, -0.07276,
	-0.07367, -0.00605, 1.07602
);

vec3 toneMapACES_Narkowicz(vec3 color)
{
	const float A = 2.51;
	const float B = 0.03;
	const float C = 2.43;
	const float D = 0.59;
	const float E = 0.14;
	return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}

vec3 RRTAndODTFit(vec3 color)
{
	vec3 a = color * (color + 0.0245786) - 0.000090537;
	vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	return a / b;
}

vec3 toneMapACES_Hill(vec3 color)
{
	color = ACESInputMat * color;
	color = RRTAndODTFit(color);
	color = ACESOutputMat * color;
	return clamp(color, 0.0, 1.0);
}

vec3 toneMap_KhronosPbrNeutral(vec3 color)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression) return color;

	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;

	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return mix(color, newPeak * vec3(1.0), g);
}

vec3 uncharted2ToneMapping(vec3 color)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	const float W = 11.2;

	color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
	float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
	return color / white;
}

vec3 applyToneMapping(vec3 color)
{
	if (!hdrToneMapping)
		return color;

	color *= iblExposure;

	if (toneMapMode == 0)
		color = toneMap_KhronosPbrNeutral(color);
	else if (toneMapMode == 1)
		color = toneMapACES_Narkowicz(color);
	else if (toneMapMode == 2)
		color = toneMapACES_Hill(color);
	else if (toneMapMode == 3)
	{
		// boost exposure - see main_scene.frag's applyToneMapping() for the
		// same https://github.com/mrdoob/three.js/pull/19621 citation.
		color /= 0.6;
		color = toneMapACES_Hill(color);
	}
	else if (toneMapMode == 4)
		color = uncharted2ToneMapping(color);
	else
		color = color / (color + vec3(1.0)); // Reinhard

	return color;
}

void main()
{
	vec2 uv = gl_FragCoord.xy / resolution;

	// RtPathTracingSession's buffer has row 0 = top of image (matching
	// QImage/export convention), but gl_FragCoord.y = 0 is the bottom of the
	// viewport under OpenGL's default lower-left origin - flip here rather
	// than in the tracer, so the buffer stays a normal top-down image for
	// the NxN export/save path.
	uv.y = 1.0 - uv.y;

	vec4 hdrSample = texture(pathTracedTexture, uv);
	vec3 hdrColor = hdrSample.rgb;

	vec3 mapped = applyToneMapping(hdrColor);
	if (gammaCorrection)
		mapped = pow(mapped, vec3(1.0 / screenGamma));

	FragColor = vec4(mapped, hdrSample.a);
}
