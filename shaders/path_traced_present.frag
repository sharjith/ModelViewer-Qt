#version 450 core

uniform sampler2D pathTracedTexture;
uniform vec2 resolution;

out vec4 FragColor;

// Compact ACES filmic tonemap fit (Narkowicz 2015), ported from
// toneMapACES_Narkowicz() in main_scene.frag so path-traced presentation
// matches the raster pass's default tonemapped look at a glance. Full parity
// with the user's currently-selected HDRToneMapMode (RenderEnums.h) is
// deferred - see RtPresenter.h.
vec3 toneMapACES_Narkowicz(vec3 color)
{
	const float A = 2.51;
	const float B = 0.03;
	const float C = 2.43;
	const float D = 0.59;
	const float E = 0.14;
	return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
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

	vec3 mapped = toneMapACES_Narkowicz(hdrColor);
	mapped = pow(mapped, vec3(1.0 / 2.2));

	FragColor = vec4(mapped, hdrSample.a);
}
