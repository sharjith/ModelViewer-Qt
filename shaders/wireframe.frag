#version 450 core

// Lightweight wireframe / feature-edge fragment shader.
// Samples base colour (factor + optional albedo texture + vertex colour), applies a
// simple key-light diffuse term for 3D depth cues, then outputs:
//   isWireframePass = false  ->  WIREFRAME mode:        lit colour at 75% alpha;
//                                hover → hoverColor, selection → selectedColor (full opaque)
//   isWireframePass = true   ->  SHADED_WITH_EDGES pass: brightness-contrast overlay;
//                                hover/selection colours suppressed (main_scene.frag handles them)

in vec2 v_texCoord;
in vec3 v_worldNormal;
in vec4 v_vertexColor;

uniform vec3      baseColor;       // material albedo colour factor
uniform bool      hasAlbedoMap;    // true when an albedo texture is bound
uniform sampler2D albedoMap;       // unit 0 — only sampled when hasAlbedoMap
uniform bool      isWireframePass;
uniform bool      hovered;         // true when this mesh is the hover-highlighted mesh
uniform vec3      hoverColor;      // colour to use when hovered (gold by default)
uniform bool      selected;        // true when this mesh is in the selection set
uniform vec3      selectedColor;   // colour to use when selected (blue by default)
uniform bool      vertexAlphaEnabled = false; // authored BLEND vertex coverage

out vec4 fragColor;

void main()
{
    // In pure WIREFRAME mode (isWireframePass = false) edges are the sole visual element,
    // so hover/selection are signalled by replacing the edge colour directly.
    // In SHADED_WITH_EDGES (isWireframePass = true) the solid mesh already carries hover
    // and selection brightening from main_scene.frag; the edge overlay uses its normal colour.
    if (!isWireframePass)
    {
        if (hovered)  { fragColor = vec4(hoverColor,    1.0); return; }
        if (selected) { fragColor = vec4(selectedColor, 1.0); return; }
    }

    // Compose colour exactly as main_scene does for the base layer:
    //   albedo factor * vertex colour * albedo texture
    vec3 color = baseColor * v_vertexColor.rgb;
    if (hasAlbedoMap)
        color *= texture(albedoMap, v_texCoord).rgb;

    // Key-light diffuse — fixed world-space direction gives 3D depth cues
    // without depending on scene light positions.
    const vec3 keyLight = normalize(vec3(1.0, 1.5, 2.0));
    float diffuse  = max(dot(normalize(v_worldNormal), keyLight), 0.0);
    float lighting = 0.35 + 0.65 * diffuse;   // 35% ambient + 65% diffuse range
    color *= lighting;

    if (isWireframePass)
    {
        // WIRESHADED wire overlay — brightness-contrast logic applied after lighting.
        float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        vec3 overlay;
        if (brightness < 0.2)
            overlay = color + vec3(0.6);
        else if (brightness > 0.8)
            overlay = color * 0.3;
        else
            overlay = brightness > 0.5 ? color * 0.5 : color + vec3(0.4);
        fragColor = vec4(clamp(overlay, 0.0, 1.0), 1.0);
    }
    else
    {
        // WIREFRAME — semi-transparent lit colour
        fragColor = vec4(color, 0.75);
    }

    if (vertexAlphaEnabled)
    {
        fragColor.a *= clamp(v_vertexColor.a, 0.0, 1.0);
        // The matching per-draw scope selects premultiplied blending.
        fragColor.rgb *= fragColor.a;
    }
}
