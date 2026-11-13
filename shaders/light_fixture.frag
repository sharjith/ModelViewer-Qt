#version 450 core

in VS_OUT {
    vec3 viewNormal;
    float axial;
} fs_in;

out vec4 fragColor;

uniform vec3 lightColor;   // the light's own colour, already scaled by the display brightness
uniform vec3 baseColor;    // housing colour
uniform int  mode;         // 0 = housing (lit), 1 = emissive (glass, lens, sun), 2 = flat line, 3 = halo, 4 = beam glow, 5 = translucent housing

void main()
{
    if (mode == 2)
    {
        fragColor = vec4(lightColor, 1.0);
        return;
    }

    // A soft headlight plus a faint top light, in view space, so the shapes read without scene lighting.
    const vec3 headlight = normalize(vec3(0.25, 0.45, 1.0));
    vec3 n = normalize(fs_in.viewNormal);

    if (mode == 3)
    {
        // Halo: additive glow, strongest where the sphere faces the viewer. Back faces have n.z < 0 and add nothing.
        float f = max(n.z, 0.0);
        fragColor = vec4(lightColor, pow(f, 1.6) * 0.42);
        return;
    }

    if (mode == 4)
    {
        // Beam glow: additive, brightest at the lens and fading out towards the far rim (axial runs 0..1 along the cone).
        float fade = 1.0 - clamp(fs_in.axial, 0.0, 1.0);
        fragColor = vec4(lightColor, 0.04 + 0.32 * fade * fade);
        return;
    }

    if (!gl_FrontFacing)
        n = -n;
    float facing = max(dot(n, headlight), 0.0);

    if (mode == 1)
    {
        // Glowing parts: the light's colour, hotter towards white where the surface faces the viewer.
        vec3 glow = mix(lightColor, vec3(1.0), 0.6 * facing * facing);
        fragColor = vec4(glow * (0.9 + 0.1 * facing), 1.0);
        return;
    }

    float rim = pow(1.0 - max(n.z, 0.0), 3.0) * 0.12;
    vec3 shaded = baseColor * (0.28 + 0.72 * facing) + vec3(rim);
    // Housings pick up a trace of the light colour so the fixture reads as part of that light.
    shaded += lightColor * 0.05;
    // Mode 5 is the same housing shading, translucent (the lamp shade, so the holder shows through it).
    fragColor = vec4(shaded, mode == 5 ? 0.55 : 1.0);
}
