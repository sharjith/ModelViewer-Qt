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
// Background behind the pure wireframe (see the WIREFRAME branch); wireBgStyle < 0 = no adjustment (other passes, skybox)
uniform bool      wireHaloPass = false;     // first of two passes over a strong gradient: wider, opposite-luminance underlay
uniform bool      wireBgDarker = true;      // push lines darker (light background) or lighter
uniform int       wireBgStyle = -1;         // gradient style of background.frag (0..3)
uniform vec3      wireBgTop    = vec3(0.5);
uniform vec3      wireBgBottom = vec3(0.5);
uniform vec2      wireBgViewport = vec2(1.0); // size the background was drawn into

out vec4 fragColor;

void main()
{
    // In pure WIREFRAME mode (isWireframePass = false) edges are the sole visual element,
    // so hover/selection are signalled by replacing the edge colour directly.
    // In SHADED_WITH_EDGES (isWireframePass = true) the solid mesh already carries hover
    // and selection brightening from main_scene.frag; the edge overlay uses its normal colour.
    if (!isWireframePass && !wireHaloPass)
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
    else if (wireHaloPass)
    {
        // Over a gradient that crosses mid-grey no single colour contrasts along the whole line, so the line gets an underlay of
        // the opposite luminance; the line itself keeps its own colour (see ViewportWidget::setWireBackgroundUniforms).
        float haloLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(haloLum > 0.5 ? vec3(0.0) : vec3(1.0), 0.65);
    }
    else
    {
        // WIREFRAME — semi-transparent lit colour, kept apart from the background behind it.
        // The background gradient is a known function of the screen position (same formulas as background.frag), so its
        // luminance at this fragment is computed here instead of being read back. A line whose luminance is close to it is pushed
        // to one side only: darker (wireBgDarker, a mostly light background) or lighter. One side for the whole screen, so the
        // line colour follows the gradient without a switch; a gradient that spans mid-grey takes the halo pass instead.
        if (wireBgStyle >= 0)
        {
            vec2 uv = gl_FragCoord.xy / max(wireBgViewport, vec2(1.0));
            float f;
            if (wireBgStyle == 1)      f = 1.0 - uv.x;                      // horizontal: top colour at the left
            else if (wireBgStyle == 2) f = 1.0 - (uv.x + (1.0 - uv.y)) * 0.5; // diagonal, top-left to bottom-right
            else if (wireBgStyle == 3) f = 1.0 - ((1.0 - uv.x) + (1.0 - uv.y)) * 0.5;
            else                       f = uv.y;                            // vertical: top colour at the top
            vec3 bg = mix(wireBgBottom, wireBgTop, clamp(f, 0.0, 1.0));
            const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
            float bgLum   = dot(bg, lumaWeights);
            float lineLum = dot(color, lumaWeights);
            float gap = abs(lineLum - bgLum);
            // A line already on the pushed side fades in below a 0.28 gap. A line on the other side of the background would pass
            // through the background luminance on its way to the target, so it is moved only when it is nearly invisible anyway
            // (gap < 0.02, where it could hardly be seen before either). The weight is continuous in the gap, so the colour changes smoothly
            // along the gradient (no seam), and a line that was visible on that side is not touched.
            bool onPushedSide = wireBgDarker ? lineLum <= bgLum : lineLum >= bgLum;
            float weight = onPushedSide ? 1.0 - smoothstep(0.12, 0.28, gap) : 1.0 - smoothstep(0.0, 0.02, gap);
            if (weight > 0.0)
            {
                // Aim for a luminance 0.38 away from the background.
                float target = wireBgDarker ? max(bgLum - 0.38, 0.04) : min(bgLum + 0.38, 0.96);
                vec3 adjusted = target < lineLum
                    ? color * (target / max(lineLum, 0.001))
                    : clamp(color + vec3(target - lineLum), 0.0, 1.0);
                color = mix(color, adjusted, weight);
            }
        }
        fragColor = vec4(color, 0.75);
    }

    if (vertexAlphaEnabled)
        fragColor.a *= clamp(v_vertexColor.a, 0.0, 1.0);
    // The vertex-alpha draw scope and the halo pass (drawWireHalo) both select premultiplied blending.
    if (vertexAlphaEnabled || wireHaloPass)
        fragColor.rgb *= fragColor.a;
}
